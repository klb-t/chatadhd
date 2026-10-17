"""Sensitive recipe values of a redacted package: the redaction dictionary, an edit's terms, and any other
recipe literal (an expression, a search term, the typed sentence, a source path) that the package's redaction
would mask.  The package never carries them in clear and never masks them value by value either (a masked
recipe reproduces a different package): each is replaced by a reference ``{"secret": "sha256:…", …}`` and the
value is kept in a local store OUTSIDE every package (``<state dir>/secrets.json``, owner-only permissions).
A run of such a recipe (``przepis uruchom``, ``odtworz``, the API) resolves the references from that store or
from a file the user names (``--slownik-plik``); when a value is missing it refuses with a Polish message —
it never runs un-redacted.  Which params are sensitive is data: ``"secret": true`` in ``data/ops.json``.
"""
import json
import logging
import os
from pathlib import Path
from typing import Any, Optional

from .model import canonical_json, sha256_text

log = logging.getLogger(__name__)
REF_KEY = "secret"


class SecretMissing(ValueError):
    pass


def is_ref(v: Any) -> bool:
    return isinstance(v, dict) and isinstance(v.get(REF_KEY), str) and str(v[REF_KEY]).startswith("sha256:")


def ref_of(value: Any, kind: str) -> dict[str, Any]:
    out: dict[str, Any] = {REF_KEY: "sha256:" + sha256_text(canonical_json(value)), "kind": kind}
    if isinstance(value, list):
        out["count"] = len(value)
    return out


def secret_params(ds: Any) -> set[tuple[str, str]]:
    from .recipe import ops_registry
    reg = ops_registry(ds)
    return {(op, name) for op, spec in reg["ops"].items() for name, ps in (spec.get("params") or {}).items() if ps.get("secret")}


def protect_recipe(recipe: dict[str, Any], red: Any, ds: Any) -> tuple[dict[str, Any], dict[str, Any]]:
    """(recipe with references, {sha: value}).  Secret params always become references; any other string the
    redactor would mask (inside params, settings, sources, manual decisions, tags …) becomes one too."""
    store: dict[str, Any] = {}
    secret = secret_params(ds)

    def keep(value: Any, kind: str) -> dict[str, Any]:
        r = ref_of(value, kind)
        store[r[REF_KEY]] = value
        return r

    def walk(v: Any) -> Any:
        if isinstance(v, str):
            return keep(v, "literal") if red is not None and red.would_mask(v) else v
        if isinstance(v, list):
            return [walk(x) for x in v]
        if isinstance(v, dict):
            return {k: walk(x) for k, x in v.items()}
        return v

    out = json.loads(json.dumps(recipe))
    for st in out.get("steps") or []:
        params = st.get("params") or {}
        for name in list(params):
            if (st.get("op"), name) in secret and params[name] not in (None, [], ""):
                val = params[name]
                if isinstance(val, list) and all(isinstance(x, str) for x in val):
                    val = sorted(val)  # order-free: a dictionary file given later in any order resolves it
                params[name] = keep(val, "dictionary")
            else:
                params[name] = walk(params[name])
    for key in ("sources", "settings", "manual", "tags", "bookmarks", "views", "label"):
        if key in out:
            out[key] = walk(out[key])
    if store:
        out["secrets_note"] = ("redacted package: the dictionary and the recipe values it masks are references "
                               "(sha256); a run resolves them from the local store or --slownik-plik, or refuses")
    return out, store


def refs_in(obj: Any) -> list[str]:
    out: list[str] = []
    if is_ref(obj):
        return [obj[REF_KEY]]
    if isinstance(obj, list):
        for x in obj:
            out += refs_in(x)
    elif isinstance(obj, dict):
        for x in obj.values():
            out += refs_in(x)
    return out


def resolve_recipe(recipe: dict[str, Any], store: dict[str, Any]) -> dict[str, Any]:
    """References → values; ``SecretMissing`` (with the missing hashes) when any is not in ``store``."""
    missing = sorted({r for r in refs_in(recipe) if r not in store})
    if missing:
        raise SecretMissing(", ".join(m[:19] + "…" for m in missing))

    def walk(v: Any) -> Any:
        if is_ref(v):
            return json.loads(json.dumps(store[v[REF_KEY]]))
        if isinstance(v, list):
            return [walk(x) for x in v]
        if isinstance(v, dict):
            return {k: walk(x) for k, x in v.items()}
        return v

    out = walk(recipe)
    out.pop("secrets_note", None)
    return out


# ───────────────────────────── values a face supplies (--slownik-plik) ─────────────────────────────
_SUPPLIED: dict[str, Any] = {}


def supply(values: dict[str, Any]) -> None:
    _SUPPLIED.update(values)


def supplied() -> dict[str, Any]:
    return dict(_SUPPLIED)


def reset_supplied() -> None:
    _SUPPLIED.clear()


# ───────────────────────────── the local store ─────────────────────────────
def store_path(ds: Any = None) -> Path:
    from .paths import state_dir
    return state_dir(ds) / "secrets.json"


def load_store(path: Optional[Path] = None, ds: Any = None) -> dict[str, Any]:
    p = path or store_path(ds)
    if not p.exists():
        return {}
    try:
        obj = json.loads(p.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        log.warning("secret store unreadable: %s", p, exc_info=True)
        return {}
    return dict(obj.get("values") or {}) if isinstance(obj, dict) else {}


def save_store(values: dict[str, Any], path: Optional[Path] = None, ds: Any = None) -> Path:
    p = path or store_path(ds)
    p.parent.mkdir(parents=True, exist_ok=True)
    cur = load_store(p)
    cur.update(values)
    tmp = p.with_name(p.name + ".tmp")
    tmp.write_text(json.dumps({"schema": "loom.extract.secrets/1", "values": cur}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    try:
        os.chmod(tmp, 0o600)
    except OSError:
        log.debug("chmod 600 failed on %s", tmp, exc_info=True)
    tmp.replace(p)
    return p


def load_user_file(path: Any) -> dict[str, Any]:
    """``--slownik-plik``: a store file (``{"values": {sha: value}}``), a ``{sha: value}`` object, or a plain list of
    dictionary terms / one term per line (then the reference of the whole list is what it resolves)."""
    p = Path(str(path)).expanduser()
    text = p.read_text(encoding="utf-8")
    try:
        obj = json.loads(text)
    except json.JSONDecodeError:
        obj = [ln.strip() for ln in text.splitlines() if ln.strip()]
    if isinstance(obj, dict):
        vals = obj.get("values") if isinstance(obj.get("values"), dict) else obj
        return {k: v for k, v in vals.items() if str(k).startswith("sha256:")}
    if isinstance(obj, list):
        terms = sorted(str(x) for x in obj)
        return {ref_of(terms, "dictionary")[REF_KEY]: terms}
    return {}


__all__ = ["SecretMissing", "is_ref", "protect_recipe", "resolve_recipe", "refs_in", "load_store", "save_store", "load_user_file",
           "store_path", "secret_params"]
