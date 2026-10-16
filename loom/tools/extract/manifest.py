"""Package manifest (``loom.extract.manifest/1``), reproduction from it, and
the package check.

The manifest says where everything came from and what it cost: sources with
hashes, the normalised recipe (embedded, so the manifest alone reproduces the
package), settings and where each came from, criteria, cascade, decisions,
selection, cut and profiles, attachments (copied / missing), redaction
counts, edits, model calls and cost, statistics, hand-off, tool version, and
``reproducible_hash`` = sha256 of ``path:sha256`` of every output (the
manifest itself and run times are outside it).  ``SOURCE_DATE_EPOCH`` fixes
the run times, so the same inputs and recipe give identical package bytes.
"""
import json
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

from . import timestamps as _ts
from .model import canonical_json, sha256_text
from .recipe import recipe_hash, steps_by_face


def run_times(started: float) -> tuple[str, str]:
    sde = os.environ.get("SOURCE_DATE_EPOCH")
    if sde and sde.isdigit():
        t = datetime.fromtimestamp(int(sde), timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")
        return t, t
    now = datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")
    start = datetime.fromtimestamp(started, timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")
    return start, now


def build_manifest(eng: Any, outputs: list[dict[str, Any]], repro: str, recipe_norm: dict[str, Any], aggregate: bool) -> dict[str, Any]:
    started, finished = run_times(eng.started)
    tool = eng.tool_info()
    sources = []
    for a in eng.archives:
        unknown = [m.name for m in a.members if not m.format and m.kind not in ("asset", "container")]
        sources.append({"path": None if aggregate else a.path, "label": a.label, "sha256": a.sha256, "size": a.size,
                        "mtime": _ts.to_iso(datetime.fromtimestamp(a.mtime, timezone.utc)) if getattr(a, "mtime", None) else None,
                        "format": a.format, "units": sum(1 for u in eng.units_raw if u.source.get("sha256") == a.sha256),
                        "members_unknown": [] if aggregate else unknown, "members_unknown_count": len(unknown)})
    rsha = recipe_hash(recipe_norm, eng.ds)
    run_id = sha256_text(canonical_json({"sources": [s["sha256"] for s in sources], "recipe": rsha}))
    calls, cost_total = (eng.model_runner.summary() if eng.model_runner is not None else ([], {"amount": 0.0, "currency": "USD", "known": True}))
    if not calls:
        cost_total = {"amount": 0.0, "currency": "USD", "known": True, "note": "0 — bez modelu"}
    cut = eng.cut_info or {}
    pkg = eng.package
    edits = []
    if pkg is not None:
        for e in pkg.oplog:
            edits.append({"n": e["n"], "op": e["op"], "count": e["count"], "before": e["before"], "after": e["after"],
                          "invertible": e["invertible"], "params": {k: v for k, v in e["params"].items() if k not in ("terms",)}})
    settings_sources: dict[str, str] = {}
    for st in recipe_norm.get("steps") or []:
        for k, v in (st.get("params_source") or {}).items():
            settings_sources[f"{st['id']}.{st['op']}.{k}"] = v
    red = cut.get("redaction") or {"profile": "none", "entries": 0}
    return {
        "schema": "loom.extract.manifest/1",
        "tool": {"name": tool["name"], "version": tool["version"], "extract_schema": tool["extract_schema"], "python": tool["python"],
                 "selector": tool["selector"]},
        "run": {"id": run_id, "started": started, "finished": finished, "aggregate": aggregate, "slice": "B",
                "sentence": eng.settings.get("sentence"), "faces": sorted({s.get("by", "recipe") for s in recipe_norm.get("steps") or []})},
        "sources": sources,
        "settings": {k: v for k, v in eng.settings.items() if k not in ("cache_dir",)},
        "settings_sources": settings_sources,
        "criteria": eng.criteria.as_dict() if eng.criteria else None,
        "cascade": eng.cascade.as_dict() if eng.cascade else None,
        "decisions": {"applied": sum(1 for d in eng.unit_decisions.values() if d.get("source") == "manual"),
                      "entries": len(eng.decisions.entries), "source": eng.decisions.source},
        "selection": eng.selection_summary(),
        "expand": eng.expand_result,
        "package_tree": pkg.as_dict() if pkg is not None else None,
        "cut": {"profiles": cut.get("profiles"), "order": cut.get("order"), "entries": cut.get("entries")} if cut else None,
        "preserve": cut.get("preserve"),
        "attachments": cut.get("attachments"),
        "redaction": {k: red.get(k) for k in ("profile", "active", "entries", "by_kind", "by_rule", "distinct_values", "dictionary_terms",
                                               "attachments", "irreversible") if k in red},
        "edits": edits,
        "model_calls": calls,
        "cost_total": cost_total,
        "stats": ({k: eng.stats.get(k) for k in ("total", "period", "services", "languages")} if eng.stats else None),
        "handoff": eng.handoff_info,
        "outputs": outputs,
        "recipe": {"sha256": rsha, "steps": len(recipe_norm.get("steps") or []), "by": steps_by_face(recipe_norm), "body": recipe_norm},
        "timeline": {k: eng.timeline.get(k) for k in ("key", "zone", "ties") if eng.timeline} | (
            {"gaps": len(eng.timeline.get("gaps") or []), "bursts": len(eng.timeline.get("bursts") or []),
             "continuations": len(eng.timeline.get("continuations") or [])} if eng.timeline else {}),
        "diff": ({"against": eng.diff.get("old"), **eng.diff.get("counts", {})} if eng.diff else None),
        "reproducible_hash": repro,
    }


def reproducible_hash(outputs: list[dict[str, Any]]) -> str:
    return sha256_text("\n".join(f"{o['path']}:{o['sha256']}" for o in sorted(outputs, key=lambda o: o["path"])))


def load_manifest(path: Any) -> tuple[dict[str, Any], Path]:
    p = Path(str(path)).expanduser()
    if p.is_dir():
        p = p / "manifest.json"
    if not p.exists():
        raise FileNotFoundError(f"manifest not found: {p}")
    return json.loads(p.read_text(encoding="utf-8")), p.parent


def reproduce(eng: Any, manifest_path: Any, *, target: Optional[Any] = None) -> dict[str, Any]:
    """Run the recipe embedded in a manifest (sources must have the same content) into ``target``
    (preset: a sibling ``<package>.reproduced``) and compare the reproducible hash and every output."""
    man, pkg_dir = load_manifest(manifest_path)
    recipe = json.loads(json.dumps((man.get("recipe") or {}).get("body") or {}))
    if not recipe:
        rp = pkg_dir / "recipe.json"
        if not rp.exists():
            raise ValueError("manifest has no embedded recipe and the package has no recipe.json")
        recipe = json.loads(rp.read_text(encoding="utf-8"))
    out_dir = Path(str(target)).expanduser() if target else pkg_dir.with_name(pkg_dir.name + ".reproduced")
    recipe["steps"] = [st for st in recipe.get("steps") or [] if st.get("op") != "write"]
    recipe["steps"].append({"op": "write", "params": {"package": str(out_dir)}, "by": "recipe"})
    if man.get("settings", {}).get("flows"):
        recipe.setdefault("settings", {})["flows"] = man["settings"]["flows"]
    eng.run_recipe(recipe, base_dir=pkg_dir, by="recipe")
    new_man, _ = load_manifest(out_dir)
    src_old = {s.get("label"): s.get("sha256") for s in man.get("sources") or []}
    src_new = {s.get("label"): s.get("sha256") for s in new_man.get("sources") or []}
    changed_sources = sorted(k for k in set(src_old) | set(src_new) if src_old.get(k) != src_new.get(k))
    old_out = {o["path"]: o["sha256"] for o in man.get("outputs") or []}
    new_out = {o["path"]: o["sha256"] for o in new_man.get("outputs") or []}
    diffs = sorted(k for k in set(old_out) | set(new_out) if old_out.get(k) != new_out.get(k))
    return {"equal": man.get("reproducible_hash") == new_man.get("reproducible_hash"), "original": man.get("reproducible_hash"),
            "reproduced": new_man.get("reproducible_hash"), "package": str(out_dir), "differences": diffs,
            "sources_changed": changed_sources}


def check_package(pkg_dir: Any, *, ds: Any = None) -> dict[str, Any]:
    """Validate a written package: manifest schema, every output present with its hash, the reproducible hash,
    the recipe, and (when present) that the hand-off imports through ``engine.importer`` with the counts it states."""
    import hashlib
    man, root = load_manifest(pkg_dir)
    problems: list[str] = []
    if man.get("schema") != "loom.extract.manifest/1":
        problems.append(f"manifest schema: {man.get('schema')!r}")
    for o in man.get("outputs") or []:
        p = root / o["path"]
        if not p.exists():
            problems.append(f"missing output: {o['path']}")
            continue
        h = hashlib.sha256()
        with open(p, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
        if h.hexdigest() != o["sha256"]:
            problems.append(f"hash differs: {o['path']}")
    if reproducible_hash(man.get("outputs") or []) != man.get("reproducible_hash"):
        problems.append("reproducible_hash does not match the outputs list")
    from .recipe import validate
    body = (man.get("recipe") or {}).get("body")
    if body:
        problems += [f"recipe: {x}" for x in validate(body, ds)]
    handoff = None
    hz = root / "handoff.zip"
    if hz.exists() and man.get("handoff"):
        from .readers.textlike import _capturing_importer_class
        imp = _capturing_importer_class()()
        try:
            imp.import_file(str(hz))
        except Exception as exc:  # noqa: BLE001 - reported as a problem, never silent
            problems.append(f"handoff.zip does not import: {type(exc).__name__}: {exc}")
        msgs = sum(len([m for m in ms if str(m.get('content') or '').strip()]) for ms, _ in imp.captured)
        handoff = {"conversations": len(imp.captured), "messages_with_text": msgs}
        exp = man["handoff"]
        if handoff["conversations"] != exp.get("conversations_with_messages"):
            problems.append(f"handoff: importer saw {handoff['conversations']} conversations, manifest says {exp.get('conversations_with_messages')} with messages")
        if handoff["messages_with_text"] != exp.get("messages_with_text"):
            problems.append(f"handoff: importer saw {handoff['messages_with_text']} messages, manifest says {exp.get('messages_with_text')}")
    return {"ok": not problems, "problems": problems, "outputs": len(man.get("outputs") or []), "handoff_import": handoff}
