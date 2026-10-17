"""One engine, three faces.  The engine holds the STATE — sources, the immutable
unit snapshot (scan → read → merge), inventory, timeline, diff, cascade result,
the selection (grain ids with reasons), manual decisions, clusters, the recipe
being recorded and an event log — and exposes exactly one way to change it:
``op(name, params, by)``.  Every face (CLI, commander, serve API) calls ops
from ``data/ops.json`` and nothing else; an op with ``records: true`` becomes a
recipe step, so whatever a face did can be replayed with ``run_recipe``.
Long ops report ``progress`` events through a callback; nothing here draws.
"""
import logging
import sys
import time
from pathlib import Path
from typing import Any, Callable, Optional

from . import __version__, EXTRACT_SCHEMA
from .datafiles import DataStore, store as default_store
from .decisions import Decisions
from .expand import Selection
from .lexical import Lexicon, lexicon as default_lexicon
from .model import Unit
from .readers.base import ReadContext
from .recipe import RecipeError, add_step, new_recipe, normalise, op_spec, ops_registry
from .select_expr import ExprContext
from .sources import FormatCatalog
from .vectors import VectorIndex

log = logging.getLogger(__name__)
MODES = ("replace", "add", "subtract", "intersect")


class EngineError(ValueError):
    pass


class Engine:
    def __init__(self, *, ds: Optional[DataStore] = None, lex: Optional[Lexicon] = None,
                 progress: Optional[Callable[[dict[str, Any]], None]] = None, aggregate: bool = False,
                 settings: Optional[dict[str, Any]] = None) -> None:
        self.ds = ds or default_store()
        self.lex = lex or default_lexicon()
        self.catalog = FormatCatalog(self.ds)
        self.progress = progress
        self.aggregate = aggregate
        self.settings: dict[str, Any] = dict(settings or {})
        self.sources: list[str] = []
        self.archives: list[Any] = []
        self.units_raw: list[Unit] = []
        self.units: list[Unit] = []
        self.merge_result: Any = None
        self.inventory: Optional[dict[str, Any]] = None
        self.timeline: Optional[dict[str, Any]] = None
        self.timeline_rows: Optional[list[dict[str, Any]]] = None
        self.diff: Optional[dict[str, Any]] = None
        self.cascade: Any = None
        self.criteria: Any = None
        self.unit_decisions: dict[str, dict[str, Any]] = {}
        self.explain: list[dict[str, Any]] = []
        self.clusters: Optional[dict[str, Any]] = None
        self.decisions: Decisions = Decisions()
        self.selection: Selection = Selection()
        self.selection_meta: dict[str, dict[str, Any]] = {}
        self.expand_result: Optional[dict[str, Any]] = None
        self.recipe: dict[str, Any] = new_recipe([])
        self.events: list[dict[str, Any]] = []
        self.results: list[dict[str, Any]] = []
        self.model_calls: list[dict[str, Any]] = []
        self.ctx: Optional[ReadContext] = None
        self._vectors: dict[str, VectorIndex] = {}
        self.started = time.time()
        self.last_write: Optional[dict[str, Any]] = None
        # slice B: package tree, cut staging, redaction, statistics, hand-off, model runner
        self.package: Any = None
        self.stage_dir: Optional[Path] = None
        self.staged: Any = None
        self.cut_info: Optional[dict[str, Any]] = None
        self.handoff_info: Optional[dict[str, Any]] = None
        self.stats: Optional[dict[str, Any]] = None
        self.redaction_reports: list[dict[str, Any]] = []
        self.redaction_entries: list[dict[str, Any]] = []
        self.summaries: dict[str, dict[str, Any]] = {}
        self.model_runner: Any = None
        self.ask: Optional[Callable[[dict[str, Any]], bool]] = None
        self._tmp_stage: Any = None
        self.op_ctx: dict[str, Any] = {}
        # the staged cut/hand-off describe the package tree they were made from; write re-makes them when it changed
        self.cut_params: Optional[dict[str, Any]] = None
        self.cut_tree_hash: Optional[str] = None
        self.handoff_params: Optional[dict[str, Any]] = None
        self.handoff_tree_hash: Optional[str] = None
        # values a redacted package's recipe refers to by hash (``--slownik-plik``; the local store is read too)
        self.secret_values: dict[str, Any] = {}
        self.secrets_info: Optional[dict[str, Any]] = None

    # -- lifecycle --------------------------------------------------------------
    def close(self) -> None:
        if self.ctx is not None:
            self.ctx.close()
            self.ctx = None
        if self._tmp_stage is not None:
            self._tmp_stage.cleanup()
            self._tmp_stage = None

    def __enter__(self) -> "Engine":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    # -- events -----------------------------------------------------------------
    def emit(self, kind: str, **data: Any) -> None:
        ev = {"kind": kind, **data}
        self.events.append(ev)
        if self.progress is not None:
            try:
                self.progress(ev)
            except Exception:  # noqa: BLE001 - a face's display problem never stops the engine
                log.debug("progress callback failed", exc_info=True)

    # -- sources / state --------------------------------------------------------
    def set_sources(self, paths: list[str]) -> None:
        # recorded absolute (a package's recipe replays from anywhere on this machine); a path that
        # does not exist is kept as typed so the error names what the user wrote
        self.sources = [str(Path(p).expanduser().resolve()) if Path(p).expanduser().exists() else str(p) for p in paths]
        self.recipe["sources"] = list(self.sources)
        self.archives, self.units_raw, self.units, self.merge_result = [], [], [], None
        self.inventory = self.timeline = self.timeline_rows = None
        self._vectors.clear()

    @property
    def by_uid(self) -> dict[str, Unit]:
        return {u.uid: u for u in self.units}

    @property
    def conversations(self) -> list[Unit]:
        return [u for u in self.units if u.kind in ("conversation", "session")]

    def read_context(self) -> ReadContext:
        if self.ctx is None:
            self.ctx = ReadContext(self.ds, self.catalog)
            self.ctx.progress = lambda d: self.emit("progress", **d)
        return self.ctx

    def ensure_units(self) -> None:
        """scan → read → merge when the snapshot is not there yet (the ops are recorded)."""
        if not self.archives:
            self.op("scan", by="engine")
        if not self.units_raw and self.archives:
            self.op("read", by="engine")
        if self.merge_result is None and self.units_raw:
            self.op("merge", by="engine")

    # -- ops --------------------------------------------------------------------
    def op(self, name: str, params: Optional[dict[str, Any]] = None, *, by: str = "api", note: Optional[str] = None,
           sources: Optional[dict[str, str]] = None) -> dict[str, Any]:
        from . import ops as _ops  # local import: ops imports this module's types
        spec = op_spec(name, self.ds)
        params = dict(params or {})
        # presets and defaults, written out so the recipe says where each value came from; a replayed
        # step brings its recorded origins along, so the recipe stays byte-identical after a replay
        source: dict[str, str] = {k: "step" for k in params}
        if sources:
            source.update({k: str(v) for k, v in sources.items() if k in params})
        for pname, ps in (spec.get("params") or {}).items():
            if pname in params:
                continue
            if "preset" in ps:
                val = self.ds.preset(ps["preset"])
                if val is not None:
                    params[pname] = val
                    source[pname] = f"preset:{ps['preset']}"
            elif "default" in ps:
                params[pname] = ps["default"]
                source[pname] = "op_default"
        for pname, ps in (spec.get("params") or {}).items():
            if ps.get("required") and params.get(pname) is None:
                raise EngineError(f"{name}: missing required param {pname!r}")
        if spec.get("explicit") and not self.settings.get("allow_user_code"):
            raise EngineError(f"{name}: user code needs settings.allow_user_code = true")
        fn = _ops.OPS.get(name)
        if fn is None:
            raise EngineError(f"op {name!r} is registered in ops.json but not implemented in this slice")
        self.emit("op.start", op=name, by=by)
        t0 = time.time()
        # what the running op may ask about its call: an implicit pipeline step (by the engine, or replayed as one)
        # is the base state, not a user's change, so package ops do not log it for undo
        prev_ctx = self.op_ctx
        self.op_ctx = {"by": by, "note": note, "implicit": (by == "engine" and note is None) or note == "implicit"}
        try:
            result = fn(self, params)
        except Exception as exc:
            self.emit("op.error", op=name, error=f"{type(exc).__name__}: {exc}")
            raise
        finally:
            self.op_ctx = prev_ctx
        result = dict(result or {})
        result.setdefault("op", name)
        result["seconds"] = round(time.time() - t0, 4)
        no_record = bool(result.pop("_no_record", False))
        if spec.get("records", True) and not no_record:
            # implicit pipeline steps (by="engine") are recorded too, marked as such, so the recipe is complete;
            # every step carries all its parameters with their origin (step | preset:… | op_default)
            step = add_step(self.recipe, name, params, by="recipe" if by == "engine" else by,
                            note="implicit" if by == "engine" else note, params_source=source)
            result["step"] = step["id"]
        self.results.append({"op": name, "by": by, "summary": {k: v for k, v in result.items() if k in ("counts", "step", "seconds")}})
        self.emit("op.done", op=name, seconds=result["seconds"])
        return result

    def run_recipe(self, recipe: dict[str, Any], *, base_dir: Optional[Any] = None, by: str = "recipe") -> list[dict[str, Any]]:
        from .recipe import SCHEMA as _RECIPE_SCHEMA
        recipe = dict(recipe)
        recipe.setdefault("schema", _RECIPE_SCHEMA)  # an inline recipe (API, tests, task files) may omit it
        from .sensitive import SecretMissing, load_store, refs_in, resolve_recipe, supplied
        if refs_in(recipe):  # a redacted package's recipe: its dictionary is supplied at run time, or nothing runs
            try:
                recipe = resolve_recipe(recipe, {**load_store(ds=self.ds), **supplied(), **self.secret_values})
            except SecretMissing as exc:
                from .i18n import t as _t
                raise RecipeError(_t("recipe.secret_missing", refs=str(exc))) from exc
        norm = normalise(recipe, self.ds, base_dir=base_dir)
        if norm.get("sources"):
            self.set_sources(norm["sources"])
        self.settings.update(norm.get("settings") or {})
        self.model_runner = None  # settings may have changed (models enabled, classes, provider)
        self.recipe["id"] = norm.get("id", self.recipe.get("id"))
        self.recipe["label"] = norm.get("label", {})
        self.recipe["manual"] = norm.get("manual", self.recipe.get("manual"))
        self.recipe["views"], self.recipe["bookmarks"], self.recipe["tags"] = norm.get("views", []), norm.get("bookmarks", []), norm.get("tags", {})
        out: list[dict[str, Any]] = []
        for step in norm["steps"]:
            out.append(self.op(step["op"], dict(step["params"]), by=by if step.get("by") in (None, "recipe") else step["by"],
                               note=step.get("note"), sources=step.get("params_source")))
        return out

    def normalised_recipe(self) -> dict[str, Any]:
        if self.settings.get("allow_user_code"):  # a recipe with user code says so, or it would not replay
            self.recipe.setdefault("settings", {})["allow_user_code"] = True
        norm = normalise(self.recipe, self.ds)
        if self.aggregate:  # privacy: no path or file name leaves an aggregate run; the content hash still identifies the archive
            by_path = {a.path: a for a in self.archives}
            norm["sources"] = [f"{by_path[s].label}:sha256:{by_path[s].sha256}" if s in by_path else f"source-{i + 1}"
                               for i, s in enumerate(norm.get("sources") or [])]
            norm["sources_note"] = "aggregate mode: sources replaced by archive labels and content hashes"
        return norm

    # -- selection ---------------------------------------------------------------
    def apply_selection(self, new: Selection, mode: str, meta: Optional[dict[str, dict[str, Any]]] = None) -> dict[str, int]:
        if mode not in MODES:
            raise EngineError(f"unknown selection mode {mode!r} (one of {', '.join(MODES)})")
        before = len(self.selection.units)
        if mode == "replace":
            self.selection = Selection()
            self.selection_meta = {}
            mode = "add"
        if mode == "add":
            for uid, ms in new.units.items():
                for r in new.reasons.get(uid, []):
                    self.selection.add(uid, ms, r)
                if ms is not None and uid in self.selection.units and self.selection.units[uid] is not None:
                    pass
            if meta:
                for uid, m in meta.items():
                    self.selection_meta.setdefault(uid, {}).update(m)
        elif mode == "subtract":
            for uid, ms in new.units.items():
                if uid not in self.selection.units:
                    continue
                cur = self.selection.units[uid]
                if ms is None or cur is None and ms is None:
                    self.selection.units.pop(uid, None)
                    self.selection.reasons.pop(uid, None)
                    self.selection_meta.pop(uid, None)
                elif cur is None:
                    u = self.by_uid.get(uid)
                    rest = {m.id for m in (u.messages if u else [])} - set(ms)
                    if rest:
                        self.selection.units[uid] = rest
                    else:
                        self.selection.units.pop(uid, None)
                else:
                    rest = cur - set(ms)
                    if rest:
                        self.selection.units[uid] = rest
                    else:
                        self.selection.units.pop(uid, None)
                        self.selection.reasons.pop(uid, None)
        elif mode == "intersect":
            keep: dict[str, Optional[set[str]]] = {}
            for uid, cur in self.selection.units.items():
                if uid not in new.units:
                    continue
                ms = new.units[uid]
                if cur is None:
                    keep[uid] = None if ms is None else set(ms)
                elif ms is None:
                    keep[uid] = cur
                else:
                    inter = cur & set(ms)
                    if inter:
                        keep[uid] = inter
            self.selection.units = keep
            self.selection.reasons = {k: v for k, v in self.selection.reasons.items() if k in keep}
            for uid in keep:
                for r in new.reasons.get(uid, []):
                    if r not in self.selection.reasons.setdefault(uid, []):
                        self.selection.reasons[uid].append(r)
            self.selection_meta = {k: v for k, v in self.selection_meta.items() if k in keep}
        return {"before": before, "after": len(self.selection.units), "messages": self.selection.message_count(self.by_uid)}

    def selection_summary(self) -> dict[str, Any]:
        by = self.by_uid
        convs = [uid for uid in self.selection.units if uid in by]
        chars = 0
        for uid in convs:
            u = by[uid]
            ms = self.selection.units[uid]
            for m in u.messages:
                if ms is None or m.id in ms:
                    chars += m.chars()
        by_service: dict[str, int] = {}
        for uid in convs:
            s = by[uid].service or ""
            by_service[s] = by_service.get(s, 0) + 1
        return {"conversations": len(convs), "messages": self.selection.message_count(by), "chars": chars,
                "tokens_est": int(chars / float(self.ds.preset("tokens.chars_per_token.default", 3.6) or 3.6)),
                "by_service": dict(sorted(by_service.items())),
                "whole": sum(1 for uid in convs if self.selection.units[uid] is None),
                "partial": sum(1 for uid in convs if self.selection.units[uid] is not None)}

    # -- slice B helpers --------------------------------------------------------------
    def ensure_package(self) -> Any:
        """The package tree; created from the current selection on first use (recorded as an implicit step)."""
        if self.package is None:
            self.op("package.copy", {"from": "selection"}, by="engine")
        return self.package

    @property
    def package_marks(self) -> dict[str, str]:
        return self.package.marks if self.package is not None else {}

    def stage(self) -> Any:
        """Staging area for cut/hand-off outputs; ``write`` moves them into the package."""
        from .cut.common import Outputs
        if self.staged is None:
            import tempfile
            self._tmp_stage = tempfile.TemporaryDirectory(prefix="xport_stage_")
            self.stage_dir = Path(self._tmp_stage.name)
            self.staged = Outputs(self.stage_dir)
        return self.staged

    def models(self) -> Any:
        from .flows import ModelRunner
        if self.model_runner is None:
            cache = self.settings.get("cache_dir")
            self.model_runner = ModelRunner(self, ask=self.ask, cache_dir=Path(cache) if cache else None)
        return self.model_runner

    def model_enabled(self) -> bool:
        return bool(((self.settings.get("flows") or {}).get("model") or {}).get("enabled"))

    # -- helpers for ops ---------------------------------------------------------
    def expr_context(self) -> ExprContext:
        ctx = ExprContext(self.units, self.ds, self.lex)
        for uid, d in self.unit_decisions.items():
            ctx.bands[uid] = d.get("decision", "")
            ctx.scores[uid] = float(d.get("score") or 0.0)
        if self.clusters:
            for c in self.clusters.get("clusters") or []:
                for uid in c.get("conversations") or []:
                    ctx.clusters.setdefault(uid, []).append(c["id"])
        ctx.tags = {k: list(v) for k, v in (self.recipe.get("tags") or {}).items()}
        if self.timeline is not None:
            ctx.timeline = self.timeline
            ctx.timeline_rows = self.timeline_rows
        ctx.vectors_factory = self.vector_index
        return ctx

    def vector_index(self, level: str = "conversation") -> VectorIndex:
        if level in self._vectors:
            return self._vectors[level]
        tier = self.ds.preset("vectors.tier")
        idx = VectorIndex(tier=int(tier) if tier is not None else None)
        items: list[tuple[str, str]] = []
        if level == "message":
            for u in self.conversations:
                for m in u.messages:
                    t = m.text()
                    if t.strip():
                        items.append((f"{u.uid}#{m.id}", t))
        else:
            for u in self.conversations:
                t = (u.title or "") + "\n" + u.text()
                if t.strip():
                    items.append((u.uid, t))
        idx.index(items)
        if idx.note:
            self.emit("note", text=idx.note)
        self._vectors[level] = idx
        return idx

    def tool_info(self) -> dict[str, Any]:
        tiers = {lvl: v.describe() for lvl, v in self._vectors.items()}
        return {"name": "loom.tools.extract", "version": __version__, "extract_schema": EXTRACT_SCHEMA,
                "python": sys.version.split()[0], "selector": tiers or None}

    def ops_available(self) -> dict[str, Any]:
        from . import ops as _ops
        reg = ops_registry(self.ds)
        return {name: {**{k: v for k, v in spec.items() if k != "_source"}, "implemented": name in _ops.OPS}
                for name, spec in reg["ops"].items()}


def state_dir(package: Any) -> Path:
    p = Path(str(package)).expanduser()
    p.mkdir(parents=True, exist_ok=True)
    return p


__all__ = ["Engine", "EngineError", "RecipeError", "state_dir"]
