"""Recipes (``loom.extract.recipe/1``): everything the user does is data.
A recipe = sources + settings + steps + manual decisions + views/bookmarks/tags.
A step = ``{id, op, params, by}`` (``by``: cli | tui | api | sentence | recipe),
optionally ``at`` (outside the hash).  Validation checks the schema, op names
and parameter types against ``data/ops.json``; normalisation produces the
canonical form (fixed key order, step ids, preset parameters written out with
their origin, ``at`` stripped) whose sha256 is the recipe hash — two runs of
the same recipe over the same sources give the same package.  Files are JSON
or TOML (``tomllib``); no YAML.
"""
import json
import re
from pathlib import Path
from typing import Any, Optional

from .datafiles import DataFileError, DataStore, read_json, store as default_store, write_json
from .model import canonical_json, sha256_text

SCHEMA = "loom.extract.recipe/1"
BY = ("cli", "tui", "api", "sentence", "recipe", "test")
_STEP_ID = re.compile(r"^s\d+$")
_RECIPE_KEYS = ("schema", "id", "label", "sources", "settings", "steps", "manual", "views", "bookmarks", "tags")
_STEP_KEYS = ("id", "op", "params", "params_source", "by", "at", "note")


class RecipeError(ValueError):
    pass


# ───────────────────────────── registry ─────────────────────────────
def ops_registry(ds: Optional[DataStore] = None) -> dict[str, Any]:
    reg = (ds or default_store()).file("ops.json")
    if not isinstance(reg, dict) or "ops" not in reg:
        raise DataFileError("ops.json without 'ops'")
    return reg


def op_spec(name: str, ds: Optional[DataStore] = None) -> dict[str, Any]:
    reg = ops_registry(ds)
    if name not in reg["ops"]:
        raise RecipeError(f"unknown op {name!r} (known: {', '.join(sorted(reg['ops']))})")
    return reg["ops"][name]


# ───────────────────────────── load / save ─────────────────────────────
def load_recipe(src: Any) -> dict[str, Any]:
    """A dict, a JSON file, or a TOML file (by extension, else by content)."""
    if isinstance(src, dict):
        return json.loads(json.dumps(src))
    p = Path(str(src)).expanduser()
    if not p.exists():
        raise RecipeError(f"recipe file not found: {p}")
    text = p.read_text(encoding="utf-8")
    if p.suffix.lower() == ".toml" or (p.suffix.lower() != ".json" and not text.lstrip().startswith("{")):
        import tomllib
        try:
            obj = tomllib.loads(text)
        except tomllib.TOMLDecodeError as exc:
            raise RecipeError(f"{p}: not TOML: {exc}") from exc
    else:
        try:
            obj = json.loads(text)
        except json.JSONDecodeError as exc:
            raise RecipeError(f"{p}: not JSON: {exc}") from exc
    if not isinstance(obj, dict):
        raise RecipeError(f"{p}: a recipe is an object")
    obj.setdefault("schema", SCHEMA)
    return obj


def save_recipe(recipe: dict[str, Any], path: Any) -> Path:
    p = Path(str(path)).expanduser()
    write_json(p, recipe)
    return p


def new_recipe(sources: list[str], *, ident: str = "recipe", settings: Optional[dict[str, Any]] = None,
               label: Optional[dict[str, str]] = None) -> dict[str, Any]:
    return {"schema": SCHEMA, "id": ident, "label": label or {}, "sources": list(sources), "settings": dict(settings or {}),
            "steps": [], "manual": {"include": [], "exclude": []}, "views": [], "bookmarks": [], "tags": {}}


def add_step(recipe: dict[str, Any], op: str, params: Optional[dict[str, Any]] = None, *, by: str = "api",
             at: Optional[str] = None, note: Optional[str] = None, params_source: Optional[dict[str, str]] = None) -> dict[str, Any]:
    steps = recipe.setdefault("steps", [])
    used = {s.get("id") for s in steps}
    n = len(steps) + 1
    while f"s{n}" in used:
        n += 1
    step: dict[str, Any] = {"id": f"s{n}", "op": op, "params": dict(params or {}), "by": by}
    if params_source:
        step["params_source"] = {k: v for k, v in params_source.items() if k in step["params"]}
    if at:
        step["at"] = at
    if note:
        step["note"] = note
    steps.append(step)
    return step


# ───────────────────────────── validation ─────────────────────────────
_TYPE_CHECK = {
    "string": lambda v: isinstance(v, str),
    "expr": lambda v: isinstance(v, str) and v.strip() != "",
    "path": lambda v: isinstance(v, str) and v != "",
    "id": lambda v: isinstance(v, str) and v != "",
    "number": lambda v: isinstance(v, (int, float)) and not isinstance(v, bool),
    "bool": lambda v: isinstance(v, bool),
    "list": lambda v: isinstance(v, list),
    "object": lambda v: isinstance(v, (dict, str)),
    "enum": lambda v: isinstance(v, str),
    "mode": lambda v: isinstance(v, str),
}


def validate(recipe: dict[str, Any], ds: Optional[DataStore] = None) -> list[str]:
    """Problems as readable strings (empty = valid)."""
    ds = ds or default_store()
    reg = ops_registry(ds)
    modes = reg.get("modes") or ["replace", "add", "subtract", "intersect"]
    problems: list[str] = []
    if not isinstance(recipe, dict):
        return ["recipe is not an object"]
    if recipe.get("schema") != SCHEMA:
        problems.append(f"schema: expected {SCHEMA}, got {recipe.get('schema')!r}")
    if not isinstance(recipe.get("sources", []), list):
        problems.append("sources: must be a list of paths")
    steps = recipe.get("steps", [])
    if not isinstance(steps, list):
        return problems + ["steps: must be a list"]
    seen: set[str] = set()
    for i, step in enumerate(steps):
        where = f"steps[{i}]"
        if not isinstance(step, dict):
            problems.append(f"{where}: not an object")
            continue
        sid = step.get("id")
        if sid is not None:
            if sid in seen:
                problems.append(f"{where}: duplicate id {sid!r}")
            seen.add(sid)
        op = step.get("op")
        if not isinstance(op, str) or op not in reg["ops"]:
            problems.append(f"{where}: unknown op {op!r}")
            continue
        spec = reg["ops"][op]
        if step.get("by", "recipe") not in BY:
            problems.append(f"{where}: by must be one of {', '.join(BY)}")
        params = step.get("params", {})
        if not isinstance(params, dict):
            problems.append(f"{where}: params must be an object")
            continue
        pspec = spec.get("params") or {}
        for name, ps in pspec.items():
            if ps.get("required") and name not in params:
                problems.append(f"{where} ({op}): missing required param {name!r}")
        for name, value in params.items():
            if name not in pspec:
                problems.append(f"{where} ({op}): unknown param {name!r} (known: {', '.join(sorted(pspec)) or '-'})")
                continue
            ptype = pspec[name].get("type", "string")
            check = _TYPE_CHECK.get(ptype)
            if value is None:
                continue
            if check and not check(value):
                problems.append(f"{where} ({op}): param {name!r} must be {ptype}, got {type(value).__name__}")
            if ptype == "enum" and isinstance(value, str) and value not in (pspec[name].get("values") or []):
                problems.append(f"{where} ({op}): param {name!r} must be one of {', '.join(pspec[name].get('values') or [])}")
            if ptype == "mode" and isinstance(value, str) and value not in modes:
                problems.append(f"{where} ({op}): mode must be one of {', '.join(modes)}")
        if spec.get("explicit") and not (recipe.get("settings") or {}).get("allow_user_code"):
            problems.append(f"{where} ({op}): user code needs settings.allow_user_code = true")
    return problems


# ───────────────────────────── normalisation ─────────────────────────────
def _norm_path(p: Any, base: Optional[Path]) -> Any:
    if not isinstance(p, str):
        return p
    q = Path(p).expanduser()
    if base is not None and not q.is_absolute():
        q = (base / q)
    return q.as_posix()


def normalise(recipe: dict[str, Any], ds: Optional[DataStore] = None, *, base_dir: Optional[Any] = None) -> dict[str, Any]:
    """Canonical form: fixed key order, step ids, params with presets filled in (origin in ``params_source``),
    paths expanded (relative to ``base_dir`` when given), ``at`` removed.  Deterministic for equal inputs."""
    ds = ds or default_store()
    problems = validate(recipe, ds)
    if problems:
        raise RecipeError("invalid recipe: " + "; ".join(problems))
    reg = ops_registry(ds)
    base = Path(str(base_dir)).expanduser() if base_dir is not None else None
    out: dict[str, Any] = {"schema": SCHEMA, "id": str(recipe.get("id") or "recipe"), "label": dict(recipe.get("label") or {}),
                           "sources": [_norm_path(s, base) for s in recipe.get("sources") or []],
                           "settings": json.loads(canonical_json(recipe.get("settings") or {}))}
    steps_out: list[dict[str, Any]] = []
    used: set[str] = set()
    for i, step in enumerate(recipe.get("steps") or []):
        sid = step.get("id") or f"s{i + 1}"
        while sid in used:
            sid = f"{sid}_"
        used.add(sid)
        spec = reg["ops"][step["op"]]
        params = dict(step.get("params") or {})
        prior = step.get("params_source") if isinstance(step.get("params_source"), dict) else {}
        source: dict[str, str] = {k: str(prior.get(k, "step")) for k in params}  # a recorded origin is kept
        for name, ps in (spec.get("params") or {}).items():
            if name in params:
                continue
            if "preset" in ps:
                val = ds.preset(ps["preset"])
                if val is not None:
                    params[name] = val
                    source[name] = f"preset:{ps['preset']}"
            elif "default" in ps:
                params[name] = ps["default"]
                source[name] = "op_default"
        for name, ps in (spec.get("params") or {}).items():
            if ps.get("type") == "path" and name in params:
                params[name] = _norm_path(params[name], base)
            if name == "against" and isinstance(params.get(name), list):
                params[name] = [_norm_path(x, base) for x in params[name]]
        params = json.loads(canonical_json(params))
        s_out: dict[str, Any] = {"id": sid, "op": step["op"], "params": params,
                                 "params_source": {k: source[k] for k in sorted(source)}, "by": step.get("by", "recipe")}
        if step.get("note"):
            s_out["note"] = step["note"]
        steps_out.append(s_out)
    out["steps"] = steps_out
    manual = recipe.get("manual") or {}
    out["manual"] = {"include": sorted((manual.get("include") or []), key=lambda e: json.dumps(e, sort_keys=True)),
                     "exclude": sorted((manual.get("exclude") or []), key=lambda e: json.dumps(e, sort_keys=True))}
    out["views"] = json.loads(canonical_json(recipe.get("views") or []))
    out["bookmarks"] = json.loads(canonical_json(recipe.get("bookmarks") or []))
    out["tags"] = {k: sorted(v) for k, v in sorted((recipe.get("tags") or {}).items())}
    return out


def recipe_hash(recipe: dict[str, Any], ds: Optional[DataStore] = None) -> str:
    """sha256 of the canonical recipe (``at`` and labels excluded)."""
    norm = normalise(recipe, ds) if "params_source" not in json.dumps(recipe.get("steps") or []) else recipe
    view = {k: v for k, v in norm.items() if k not in ("label",)}
    return sha256_text(canonical_json(view))


def steps_by_face(recipe: dict[str, Any]) -> dict[str, int]:
    out: dict[str, int] = {}
    for s in recipe.get("steps") or []:
        by = s.get("by", "recipe")
        out[by] = out.get(by, 0) + 1
    return out
