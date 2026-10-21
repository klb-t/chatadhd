"""One dispatcher for every face: ``Session.call({"call": NAME, ...}) → dict``.

The commander, ``serwuj`` (HTTP) and the CLI verb ``api`` all speak these calls
(``data/api.json``): ``list`` / ``open`` / ``view`` / ``select`` / ``selection`` / ``filter`` /
``edit`` (preview) / ``apply`` / ``undo`` / ``redo`` / ``save-recipe`` / ``export`` / ``op`` /
``compare`` / ``search`` / ``goto`` / ``bookmark`` / ``tag`` / ``views`` / ``status`` / ``jobs`` /
``cancel`` / ``answer`` / ``events`` / ``i18n`` / ``keymap`` / ``layout`` / ``recipe`` /
``explain`` / ``macro`` / ``manifest``.  Calls change state only through engine operations
(``ops.json``), so whatever a face does is a recipe step with ``by`` = the face.

Long calls run as background jobs when the request says ``"wait": false``: progress and
questions (cost consent) arrive as events, ``cancel`` stops the job at the next engine event
and puts the engine state back as it was (the recipe never records a cancelled step).  While a
job runs, calls that read the engine answer ``{"busy": true, "job": …}`` instead of waiting —
a face never blocks.  A question nobody answers is "no" (preset), never a silent "yes".
"""
import copy
import difflib
import itertools
import json
import logging
import re
import threading
import time
from pathlib import Path
from typing import Any, Callable, Optional

from .datafiles import DataStore, store as default_store, write_json
from .engine import Engine, EngineError
from .i18n import label as _label, language, t, table
from .nodes import ITEM_KINDS, ViewError, Views, column_labels, parse_id
from .recipe import RecipeError, load_recipe, normalise, op_spec, recipe_hash, save_recipe

log = logging.getLogger(__name__)
_SEQ = itertools.count(1)


class ApiError(ValueError):
    def __init__(self, message: str, code: str = "error", status: int = 400) -> None:
        super().__init__(message)
        self.code = code
        self.status = status


class Cancelled(BaseException):
    """Raised inside a job's thread at the next engine event after ``cancel`` (BaseException: the engine's
    display-error guard catches ``Exception`` only, so the cancel always gets through)."""


def jsonable(obj: Any) -> Any:
    return json.loads(json.dumps(obj, ensure_ascii=False, default=lambda o: sorted(o) if isinstance(o, set) else str(o)))


class Job:
    def __init__(self, call: str, req: dict[str, Any]) -> None:
        self.id = f"j{next(_SEQ)}"
        self.call = call
        self.req = req
        self.state = "queued"
        self.progress: dict[str, Any] = {}
        self.result: Optional[dict[str, Any]] = None
        self.error: Optional[dict[str, Any]] = None
        self.started = time.time()
        self.finished: Optional[float] = None
        self.cancel = False
        self.question: Optional[str] = None
        self.thread: Optional[threading.Thread] = None

    def public(self) -> dict[str, Any]:
        out = {"id": self.id, "call": self.call, "state": self.state, "progress": self.progress,
               "seconds": round((self.finished or time.time()) - self.started, 3)}
        if self.question:
            out["question"] = self.question
        if self.result is not None:
            out["result"] = self.result
        if self.error is not None:
            out["error"] = self.error
        return out


# engine attributes put back when a long call is cancelled or fails (they are replaced, not mutated, by the
# long operations; the selection and the recipe are mutated in place, so they are copied deeply)
_RESTORE = ("sources", "archives", "units_raw", "units", "merge_result", "inventory", "timeline", "timeline_rows", "diff",
            "cascade", "criteria", "unit_decisions", "explain", "clusters", "selection_meta", "expand_result", "package",
            "staged", "cut_info", "handoff_info", "stats", "last_write", "summaries",
            "cut_params", "handoff_params")  # the tree hashes are not put back: a cancelled cut is made again by write
_DEEP = ("selection", "recipe", "decisions")


class Session:
    def __init__(self, engine: Optional[Engine] = None, *, face: str = "api", ds: Optional[DataStore] = None,
                 aggregate: bool = False, package: Optional[str] = None, settings: Optional[dict[str, Any]] = None,
                 answer_default: Optional[bool] = None) -> None:
        self.ds = ds or (engine.ds if engine is not None else default_store())
        self._engine_args = {"aggregate": aggregate if engine is None else engine.aggregate,
                             "settings": dict(settings or {}) if engine is None else dict(engine.settings)}
        self.eng = engine or Engine(ds=self.ds, **self._engine_args)
        self._face_progress = self.eng.progress
        self.eng.progress = self._on_engine_event
        self.eng.ask = self._ask
        self.face = face
        self.package = package
        self.lock = threading.RLock()
        self.cond = threading.Condition()
        self.events: list[dict[str, Any]] = []
        self.seq = 0
        self.jobs: dict[str, Job] = {}
        self.job: Optional[Job] = None
        self.questions: dict[str, dict[str, Any]] = {}
        self.version = 0
        self.last: Optional[dict[str, Any]] = None
        self.on_change: Optional[Callable[["Session", str], None]] = None
        self.answer_default = bool(self.ds.preset("serve.answer_default", False)) if answer_default is None else answer_default
        self._sync_answer: Optional[bool] = None
        self._cost_cache: tuple[int, Any] = (-1, None)
        self._status_cache: tuple[int, dict[str, Any], Any] = (-1, {}, None)
        self._calls = self.ds.file("api.json")["calls"]
        self.layout = self.ds.file("commander/layout.json")

    def close(self) -> None:
        if self.job is not None:
            self.job.cancel = True
            if self.job.thread is not None:
                self.job.thread.join(timeout=30)
        self.eng.close()

    # -- events ---------------------------------------------------------------------------
    def event(self, kind: str, **data: Any) -> dict[str, Any]:
        with self.cond:
            self.seq += 1
            ev = {"seq": self.seq, "kind": kind, **jsonable(data)}
            self.events.append(ev)
            keep = int(self.ds.preset("serve.events_keep", 0) or 0)
            if keep and len(self.events) > keep:
                del self.events[: len(self.events) - keep]
            self.cond.notify_all()
        return ev

    def events_after(self, after: int) -> list[dict[str, Any]]:
        with self.cond:
            return [e for e in self.events if e["seq"] > after]

    def _on_engine_event(self, ev: dict[str, Any]) -> None:
        job = self.job
        if job is not None and threading.current_thread() is job.thread:
            if job.cancel:
                raise Cancelled(job.id)
            if ev.get("kind") == "progress":
                job.progress = {k: v for k, v in ev.items() if k != "kind" and isinstance(v, (int, float, str, bool, type(None)))}
            if ev.get("kind") in ("progress", "note", "op.start", "op.done"):
                self.event(ev["kind"], job=job.id, **{k: v for k, v in ev.items() if k != "kind"})
        elif ev.get("kind") == "note":
            self.event("note", **{k: v for k, v in ev.items() if k != "kind"})
        if self._face_progress is not None:
            self._face_progress(ev)

    # -- questions ------------------------------------------------------------------------
    def _ask(self, question: dict[str, Any]) -> bool:
        job = self.job
        if job is None or threading.current_thread() is not job.thread:
            return bool(self._sync_answer) if self._sync_answer is not None else self.answer_default
        qid = f"q{next(_SEQ)}"
        entry = {"id": qid, "job": job.id, "question": jsonable(question), "answer": None}
        self.questions[qid] = entry
        job.question = qid
        self.event("question", id=qid, job=job.id, question=question,
                   text=t("api.question." + str(question.get("kind", "estimate")), **_cost_words(question)))
        timeout = float(self.ds.preset("serve.question_timeout_s", 0) or 0)
        deadline = time.time() + timeout if timeout else None
        with self.cond:
            while entry["answer"] is None and not job.cancel:
                if deadline is not None and time.time() >= deadline:
                    entry["answer"] = self.answer_default
                    entry["timed_out"] = True
                    break
                self.cond.wait(timeout=0.2)
        job.question = None
        if job.cancel:
            raise Cancelled(job.id)
        return bool(entry["answer"])

    # -- the dispatcher --------------------------------------------------------------------
    def calls(self) -> dict[str, Any]:
        return self._calls

    def call(self, req: dict[str, Any]) -> dict[str, Any]:
        req = dict(req or {})
        name = str(req.pop("call", "") or "")
        spec = self._calls.get(name)
        if spec is None:
            raise ApiError(t("api.unknown_call", name=name, known=", ".join(sorted(self._calls))), "unknown_call", 404)
        running = self.job is not None and self.job.state in ("queued", "running")
        if running and not spec.get("during_job"):
            return {"call": name, "busy": True, "job": self.job.public()}
        long = bool(spec.get("long"))
        if name in ("op", "apply") and req.get("op"):
            try:
                long = long or bool(op_spec(str(req["op"]), self.ds).get("long"))
            except RecipeError:
                long = False
        if long and req.get("wait") is False:
            return {"call": name, "job": self._submit(name, req).public()}
        if spec.get("during_job"):
            # answered at once: with the lock when it is free (the engine is idle), without it otherwise
            got = self.lock.acquire(blocking=False)
            try:
                return self._invoke(name, spec, dict(req, _locked=got))
            finally:
                if got:
                    self.lock.release()
        with self.lock:
            return self._invoke(name, spec, req)

    def _invoke(self, name: str, spec: dict[str, Any], req: dict[str, Any]) -> dict[str, Any]:
        handler = getattr(self, "c_" + name.replace("-", "_").replace(".", "_"))
        if "answer" in req and name not in ("answer",):
            self._sync_answer = str(req.get("answer")).lower() in ("yes", "tak", "t", "y", "true", "1")
        try:
            out = handler(req)
        except ViewError as exc:
            raise ApiError(str(exc), "bad_node", 404) from exc
        finally:
            self._sync_answer = None
        out = jsonable(out)
        out.setdefault("call", name)
        if spec.get("mutates"):
            self.version += 1
            self.last = {"call": name, "op": req.get("op") or req.get("edit") or req.get("how"), "at": time.time()}
            if self.on_change is not None:
                try:
                    self.on_change(self, name)
                except Exception:  # noqa: BLE001 - autosave trouble is reported, never stops the call
                    log.warning("on_change hook failed", exc_info=True)
        return out

    def _submit(self, name: str, req: dict[str, Any]) -> Job:
        job = Job(name, req)
        self.jobs[job.id] = job
        spec = self._calls[name]

        def restore(eng: Engine, saved: dict[str, Any], deep: dict[str, Any]) -> None:
            if self.eng is not eng:  # an open replaced the engine: the old one comes back
                self.eng.close()
                self.eng = eng
            for k, v in saved.items():
                setattr(eng, k, v)
            for k, v in deep.items():
                setattr(eng, k, v)

        def run() -> None:
            with self.lock:
                old_eng = self.eng
                saved = {k: getattr(self.eng, k) for k in _RESTORE}
                deep = {k: copy.deepcopy(getattr(self.eng, k)) for k in _DEEP}
                job.state = "running"
                self.event("job.start", job=job.id, call=name)
                try:
                    job.result = self._invoke(name, spec, dict(req, wait=True))
                    if self.eng is not old_eng:
                        old_eng.close()
                    job.state = "done"
                    self.event("done", job=job.id, call=name, result=_brief(job.result))
                except Cancelled:
                    restore(old_eng, saved, deep)
                    job.state = "cancelled"
                    self.event("cancelled", job=job.id, call=name, text=t("api.cancelled"))
                except Exception as exc:  # noqa: BLE001 - every failure is reported as the job's error
                    log.debug("job %s failed", job.id, exc_info=True)
                    restore(old_eng, saved, deep)
                    job.error = error_body(exc)
                    job.state = "error"
                    self.event("error", job=job.id, call=name, error=job.error)
                finally:
                    job.finished = time.time()
                    if self.job is job:
                        self.job = None

        self.job = job
        job.thread = threading.Thread(target=run, name=f"xport-{job.id}", daemon=True)
        job.thread.start()
        return job

    def wait(self, job_id: str, timeout: Optional[float] = None) -> dict[str, Any]:
        job = self.jobs[job_id]
        if job.thread is not None:
            job.thread.join(timeout)
        return job.public()

    # -- helpers ---------------------------------------------------------------------------
    def views(self) -> Views:
        return Views(self.eng, self.layout)

    def fresh_engine(self) -> Engine:
        """``open`` starts from a clean state: a new engine with the session's settings (the old one is closed;
        a cancelled open puts it back)."""
        eng = Engine(ds=self.ds, aggregate=self._engine_args["aggregate"], settings=dict(self._engine_args["settings"]))
        eng.progress = self._on_engine_event
        eng.ask = self._ask
        self.eng = eng
        return eng

    def _need_units(self) -> None:
        if self.eng.sources and not self.eng.units:
            self.eng.ensure_units()

    def _op(self, name: str, params: Optional[dict[str, Any]] = None, **kw: Any) -> dict[str, Any]:
        kw.setdefault("by", self.face)
        try:
            return self.eng.op(name, params or {}, **kw)
        except (EngineError, RecipeError) as exc:
            raise ApiError(str(exc), "bad_request") from exc

    # ───────────────────────────── calls ─────────────────────────────
    def c_calls(self, req: dict[str, Any]) -> dict[str, Any]:
        return {"calls": {k: {**v, "label": _label(v.get("label"))} for k, v in self._calls.items()}}

    def c_ops(self, req: dict[str, Any]) -> dict[str, Any]:
        return {"ops": self.eng.ops_available()}

    def c_open(self, req: dict[str, Any]) -> dict[str, Any]:
        old = self.eng
        eng = self.fresh_engine()
        if old is not eng and self.job is None:
            old.close()
        if req.get("package"):
            rp = Path(str(req["package"])).expanduser() / "recipe.json"
            if not rp.exists():
                raise ApiError(t("api.no_recipe_in_package", path=str(rp)), "not_found", 404)
            req = dict(req, recipe=str(rp))
            self.package = self.package or str(Path(str(req["package"])).expanduser())
        if req.get("dictionary_file"):  # the values a redacted package's recipe refers to by hash
            from .sensitive import load_user_file
            eng.secret_values.update(load_user_file(str(req["dictionary_file"])))
        if req.get("recipe") is not None:
            rec, base = req["recipe"], (Path(str(req["base"])).expanduser() if req.get("base") else None)
            if isinstance(rec, str):
                base = Path(rec).expanduser().resolve().parent
                rec = load_recipe(rec)
            rec = dict(rec)
            rec["steps"] = [st for st in rec.get("steps") or [] if st.get("op") != "write"]  # where to write is this call's business
            res = eng.run_recipe(rec, base_dir=base)
            out: dict[str, Any] = {"opened": "recipe", "steps": len(res), "results": [_brief(r) for r in res]}
            if req.get("write"):
                w = self._op("write", {"package": str(req["write"])})
                self.package = str(req["write"])
                out.update(package=w.get("package"), reproducible_hash=w.get("reproducible_hash"))
            out["status"] = self.c_status({})
            return out
        sources = req.get("sources")
        if isinstance(sources, str):
            sources = [sources]
        if not sources:
            raise ApiError(t("api.open_needs"), "bad_request")
        eng.set_sources([str(s) for s in sources])
        eng.ensure_units()
        return {"opened": "sources", "archives": len(eng.archives), "conversations": len(eng.conversations),
                "messages": sum(len(u.messages) for u in eng.conversations), "status": self.c_status({})}

    def c_list(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        v = self.views()
        root = str(req.get("root") or "sources")
        path = req.get("path") or []
        if isinstance(path, str):
            path = [p for p in path.split(",") if p] if path else []
        flat = req.get("flat") or None
        node = path[-1] if path else None
        rows = v.flat_items(root, node, str(flat)) if flat else v.children(root, node)
        total = len(rows)
        rows, how = v.filtered(rows, str(req.get("filter") or ""), root == "package")
        rows = v.sorted_rows(rows, str(req.get("sort") or ""))
        offset = max(0, int(req.get("offset") or 0))
        limit = int(req.get("limit") or 0)
        page = rows[offset: offset + limit] if limit else rows[offset:]
        out_rows = [v.row(*r) for r in page]
        kinds = sorted({r["kind"] for r in out_rows})
        crumbs = [{"id": "", "label": _label((self.layout.get("root_labels") or {}).get(root)) or root}]
        for nid in path:
            crumbs.append({"id": nid, "label": self._crumb(v, root, nid)})
        out: dict[str, Any] = {"root": root, "path": list(path), "flat": flat, "breadcrumbs": crumbs, "rows": out_rows,
                               "total": total, "shown": len(rows), "offset": offset, "filter": {"text": req.get("filter") or "", "how": how},
                               "sort": req.get("sort") or "", "columns": {k: column_labels(self.layout, k) for k in kinds},
                               "version": self.version}
        if total == 0:
            out["hint"] = t(f"commander.hint.{root}")
        return out

    def _crumb(self, v: Views, root: str, nid: str) -> str:
        if nid.startswith("dir:"):
            return nid[4:]
        if nid.startswith("arch:"):
            i = int(nid[5:])
            return self.eng.archives[i - 1].label if 1 <= i <= len(self.eng.archives) else nid
        if nid.startswith(("grp:", "atype:", "cl:", "step:", "json:")):
            return nid.split(":", 1)[1].rsplit("/", 1)[-1] or nid
        p = parse_id(nid)
        if p is None:
            return nid
        if p["kind"] == "branch":
            return f"{t('commander.branch')} {p['branch']}"
        it, _ = v.item(nid)
        lab = str(v.value(nid, it.kind, it, "label") or "")
        return lab[:40]

    def _object(self, nid: str) -> tuple[str, Any]:
        """(kind, object) for view/compare: Items for content nodes, dicts for the rest."""
        v = self.views()
        p = parse_id(nid)
        if p is not None:
            it, _ = v.item(nid)
            if p["kind"] == "branch":
                return "branch", {"unit": it.unit, "index": p["branch"], "messages": v.branches(it.unit, p["package"])[p["branch"] - 1]}
            return it.kind, it
        for root in ("sources", "recipe", "attachments", "clusters", "inventory", "diff"):
            if nid.startswith("json:" + root + ":") or (root == "sources" and nid.startswith("arch:")) or \
                    (root == "recipe" and nid.startswith(("step:", "grp:", "view:"))) or (root == "clusters" and nid.startswith("cl:")):
                parent = None
                if nid.startswith("json:"):
                    ptr = nid.split(":", 2)[2]
                    parent = None if ptr.count("/") <= 1 else f"json:{root}:{ptr.rsplit('/', 1)[0]}"
                elif nid.startswith("step:"):
                    parent = "grp:steps"
                elif nid.startswith("view:"):
                    parent = "grp:views"
                for cid, kind, obj, _ in v.children(root, parent):
                    if cid == nid:
                        return kind, obj
        if nid.startswith("dir:"):
            pkg = self.eng.package
            ents = [e.unit.uid for e in pkg.entries if e.folder == nid[4:]] if pkg is not None else []
            return "folder", {"path": nid[4:], "entries": ents}
        raise ApiError(t("api.unknown_node", node=nid), "bad_node", 404)

    def c_view(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        nid = str(req.get("node") or "")
        fmt_ = str(req.get("format") or "json")
        if self.eng.aggregate:
            raise ApiError(t("api.aggregate_no_content"), "aggregate", 403)
        kind, obj = self._object(nid)
        if fmt_ == "markdown":
            text = _markdown(kind, obj)
        elif fmt_ == "raw":
            text = _raw(self.eng, kind, obj)
        elif fmt_ == "json":
            text = json.dumps(_as_json(kind, obj), ensure_ascii=False, indent=1, default=str)
        else:
            raise ApiError(t("api.bad_format", format=fmt_, known="json, markdown, raw"), "bad_request")
        return {"node": nid, "kind": kind, "format": fmt_, "text": text, "lines": text.count("\n") + 1}

    def c_select(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        how = str(req.get("how") or "items")
        mode = req.get("mode") or ("add" if how not in ("invert", "clear") else None)
        res: dict[str, Any]
        if how == "items":
            res = self._op("select.items", {"items": list(req.get("items") or []), "mode": mode, "reason": req.get("reason") or self.face})
        elif how == "toggle":
            v = self.views()
            on, off = [], []
            for nid in req.get("items") or []:
                it, _ = v.item(str(nid))
                (off if it is not None and v.sel_state(str(nid), it) == "all" else on).append(str(nid).removeprefix("pkg:"))
            res = {"counts": {"selected": len(on), "deselected": len(off)}}
            if on:
                res["add"] = self._op("select.items", {"items": on, "mode": "add", "reason": req.get("reason") or self.face})
            if off:
                res["subtract"] = self._op("select.items", {"items": off, "mode": "subtract", "reason": req.get("reason") or self.face})
        elif how in ("expr", "regex", "time", "all"):
            kind, expr = str(req.get("kind") or "conversation"), ""
            if how == "expr":
                expr = str(req.get("expr") or "")
                m = re.match(r"^\s*(conversation|message|block|attachment)\[(.*)\]\s*$", expr, re.S)
                if m:
                    kind, expr = m.group(1), m.group(2)
            elif how == "regex":
                pat = str(req.get("pattern") or "").replace("/", "\\/")
                expr = f"{req.get('field') or 'text'}~/{pat}/{req.get('flags') or ''}"
            elif how == "time":
                expr = _time_expr(req)
            else:
                expr = "uid~/./"
            res = self._op("select.expr", {"expr": expr, "kind": kind, "mode": mode})
        elif how == "pattern":
            res = self._op("select.structural", {"pattern": req.get("pattern"), "kind": req.get("kind") or "conversation", "mode": mode})
        elif how == "similar":
            p = {"to": [str(x).removeprefix("pkg:").split("#", 1)[0].split("@", 1)[0].split("|", 1)[0] for x in (req.get("to") or req.get("items") or [])],
                 "mode": mode}
            for k in ("threshold", "candidates"):
                if req.get(k) is not None:
                    p[k] = req[k]
            res = self._op("select.similar", p)
        elif how == "criteria":
            p = {k: req[k] for k in ("criteria", "terms", "regex", "cascade", "grain", "model") if req.get(k) is not None}
            res = self._op("select.criteria", dict(p, mode=mode))
        elif how == "cluster":
            res = self._op("select.cluster", {"cluster": req.get("cluster"), "mode": mode})
        elif how == "diff":
            res = self._op("select.diff", {"mode": req.get("diff") or "only_new"})
        elif how == "invert":
            res = self._op("select.invert", {})
        elif how == "clear":
            res = self._op("select.clear", {})
        else:
            raise ApiError(t("api.bad_select", how=how), "bad_request")
        return {"how": how, "result": _brief(res, keep=("matches", "pattern", "expr", "index")), "selection": self.eng.selection_summary()}

    def c_selection(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        by = self.eng.by_uid
        limit = int(req.get("limit") or 0)
        rows = []
        for uid, ms in self.eng.selection.units.items():
            u = by.get(uid)
            rows.append({"id": uid, "label": None if self.eng.aggregate or u is None else u.title,
                         "messages": "all" if ms is None else len(ms),
                         "reasons": [] if self.eng.aggregate else self.eng.selection.reasons.get(uid, [])})
        return {"summary": self.eng.selection_summary(), "items": rows[:limit] if limit else rows, "total": len(rows)}

    def c_filter(self, req: dict[str, Any]) -> dict[str, Any]:
        from .select_expr import ExprError, validate as validate_expr
        action = str(req.get("action") or "check")
        if action == "check":
            try:
                validate_expr(str(req.get("expr") or ""), self.ds)
                return {"ok": True}
            except ExprError as exc:
                return {"ok": False, "error": str(exc), "position": getattr(exc, "pos", None)}
        if action == "list":
            try:
                ents = self.ds.entries("filters")
            except Exception:  # noqa: BLE001 - no filters directory anywhere is an empty list
                log.debug("no filters", exc_info=True)
                ents = {}
            return {"filters": [{"id": k, "expr": v.get("expr"), "label": _label(v.get("label"))} for k, v in sorted(ents.items())]}
        base = self.ds.overlay
        if base is None:
            raise ApiError(t("cli.describe.no_overlay"), "no_overlay", 409)
        name = re.sub(r"[^\w.-]+", "_", str(req.get("name") or "")).strip("_")
        if not name:
            raise ApiError(t("cli.profile.need_name"), "bad_request")
        path = Path(base) / "filters" / f"{name}.json"
        if action == "delete":
            existed = path.exists()
            if existed:
                path.unlink()
            return {"deleted": existed, "id": name}
        if action == "save":
            validate_expr(str(req.get("expr") or ""), self.ds)
            write_json(path, {"schema": "loom.extract.filter/1", "id": name, "expr": str(req["expr"])})
            return {"saved": name, "path": str(path)}
        raise ApiError(t("api.bad_action", action=action), "bad_request")

    def c_edit(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        eid = str(req.get("edit") or "")
        params = dict(req.get("params") or {})
        if req.get("where") is not None:
            params["where"] = req["where"]
        name = "edit.user" if eid.startswith("user:") else f"edit.{eid}"
        if name == "edit.user":
            params = {"edit": eid[5:], "where": params.pop("where", None), "params": params}
        res = self._op(name, dict(params, preview=True))
        limit = int(self.layout.get("preview_lines", 0) or 0)
        patch = res.get("patch") or []
        return {"edit": eid, "preview": True, "count": res.get("count"), "scope": res.get("scope"), "lines": res.get("lines"),
                "patch": patch[:limit] if limit and isinstance(patch, list) else patch,
                "patch_total": len(patch) if isinstance(patch, list) else patch,
                "patch_text": _patch_lines(patch[:limit] if limit and isinstance(patch, list) else patch),
                "invertible": res.get("invertible"), "before": res.get("before"), "after_preview": res.get("after_preview")}

    def c_apply(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        if req.get("edit"):
            eid = str(req["edit"])
            params = dict(req.get("params") or {})
            if req.get("where") is not None:
                params["where"] = req["where"]
            if eid.startswith("user:"):
                res = self._op("edit.user", {"edit": eid[5:], "where": params.pop("where", None), "params": params})
            else:
                res = self._op(f"edit.{eid}", params)
            return {"applied": eid, "result": _brief(res, keep=("lines", "count", "invertible", "before", "after")),
                    "undo": len(self.eng.package.oplog) if self.eng.package is not None else 0}
        if req.get("op"):
            res = self._op(str(req["op"]), dict(req.get("params") or {}))
            return {"applied": req["op"], "result": _brief(res)}
        raise ApiError(t("api.apply_needs"), "bad_request")

    def c_undo(self, req: dict[str, Any]) -> dict[str, Any]:
        res = self._op("undo", {"steps": int(req.get("steps") or 1)})
        return {"result": _brief(res, keep=("undone", "tree_hash"))}

    def c_redo(self, req: dict[str, Any]) -> dict[str, Any]:
        res = self._op("redo", {"steps": int(req.get("steps") or 1)})
        return {"result": _brief(res, keep=("redone", "tree_hash"))}

    def c_save_recipe(self, req: dict[str, Any]) -> dict[str, Any]:
        norm = self.eng.normalised_recipe()
        out: dict[str, Any] = {"recipe": norm, "hash": recipe_hash(norm, self.ds), "steps": len(norm.get("steps") or [])}
        if req.get("path"):
            out["path"] = str(save_recipe(norm, str(req["path"])))
        return out

    def c_export(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        pkg = req.get("package") or self.package
        if not pkg:
            raise ApiError(t("api.export_needs"), "bad_request")
        cut_params = {k: req[k] for k in ("profiles", "order", "attachments", "redaction", "dictionary", "extract_text", "zone",
                                          "time_format", "shift", "anonymise", "granularity") if req.get(k) is not None}
        # cut when asked (explicit cut parameters), when there is no cut yet, or when the package changed after the
        # last one; otherwise the staged cut is written as it is (a replayed recipe exports to the same bytes)
        # the package changed after the cut when its tree differs from the tree the cut was made from (an edit, but
        # also an undo or a redo of one made BEFORE the cut); write re-makes a stale cut too, so this only decides
        # whether the cut is a recorded step of this export
        pkg_now = self.eng.package.tree_hash() if self.eng.package is not None else None
        never = self.eng.cut_params is None
        changed = not never and self.eng.cut_tree_hash != pkg_now
        results = []
        if cut_params or never or changed or self.eng.staged is None:
            # a changed package is cut again as it was cut (the recorded parameters), not with the presets
            again = dict(self.eng.cut_params or {}) if changed and not cut_params else cut_params
            results.append(self._op("cut", again))
        if req.get("handoff"):
            results.append(self._op("handoff", dict(req["handoff"]) if isinstance(req["handoff"], dict) else {}))
        w = self._op("write", {"package": str(pkg)})
        self.package = str(pkg)
        return {"package": w.get("package"), "reproducible_hash": w.get("reproducible_hash"), "outputs": w.get("outputs"),
                "counts": w.get("counts"), "steps": [r.get("op") for r in results] + ["write"]}

    def c_op(self, req: dict[str, Any]) -> dict[str, Any]:
        name = str(req.get("op") or "")
        if not name:
            raise ApiError(t("api.op_needs"), "bad_request")
        kw: dict[str, Any] = {}
        if req.get("by"):
            kw["by"] = str(req["by"])
        if isinstance(req.get("sources"), dict):
            kw["sources"] = req["sources"]
        if req.get("note"):
            kw["note"] = str(req["note"])
        res = self._op(name, dict(req.get("params") or {}), **kw)
        return {"op": name, "result": res}

    def c_compare(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        if self.eng.aggregate:
            raise ApiError(t("api.aggregate_no_content"), "aggregate", 403)
        a, b = str(req.get("a") or ""), str(req.get("b") or "")
        ka, oa = self._object(a)
        kb, ob = self._object(b)
        ja, jb = _as_json(ka, oa), _as_json(kb, ob)
        fields: list[dict[str, Any]] = []
        _field_diff(ja, jb, "", fields)
        ta, tb = _markdown(ka, oa).splitlines(), _markdown(kb, ob).splitlines()
        text = list(difflib.unified_diff(ta, tb, fromfile=a, tofile=b, lineterm="", n=2))
        return {"a": a, "b": b, "kinds": [ka, kb], "fields": fields, "text_diff": text, "same": not fields}

    def c_search(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        text = str(req.get("text") or "").strip()
        if not text:
            raise ApiError(t("api.search_needs"), "bad_request")
        roots = req.get("roots") or ["sources", "package"]
        kinds = req.get("kinds") or ["conversation", "message"]
        limit = int(req.get("limit") or 0)
        v = self.views()
        hits: list[dict[str, Any]] = []
        for root in roots:
            if root not in ("sources", "package"):
                continue
            for kind in kinds:
                rows, how = v.filtered(v.flat_items(root, None, kind), text, root == "package")
                for nid, k, obj, _ in rows:
                    hits.append({"root": root, "id": nid, "kind": k, "label": v.row(nid, k, obj, False)["label"],
                                 "path": self.path_of(root, nid), "how": how})
                    if limit and len(hits) >= limit:
                        return {"text": text, "hits": hits, "total": len(hits), "truncated": True}
        return {"text": text, "hits": hits, "total": len(hits)}

    def path_of(self, root: str, nid: str) -> list[str]:
        """Ancestor ids that open ``nid`` in a tree pane of ``root`` (the node itself is the cursor)."""
        p = parse_id(nid)
        if p is None:
            return []
        uid = p["uid"]
        if root == "package":
            pkg = self.eng.package
            e = pkg.by_uid().get(uid) if pkg is not None else None
            if e is None:
                return []
            path, acc = [], ""
            for part in [x for x in e.folder.split("/") if x]:
                acc += "/" + part
                path.append(f"dir:{acc}")
            pre = "pkg:"
        else:
            u = self.eng.by_uid.get(uid)
            if u is None:
                return []
            idx = next((i for i, a in enumerate(self.eng.archives, 1) if a.sha256 == (u.source or {}).get("sha256")), None)
            path = [f"arch:{idx}"] if idx else []
            pre = ""
        if p["kind"] == "conversation":
            return path
        path.append(f"{pre}{uid}")
        if p["kind"] in ("message", "block"):
            v = self.views()
            u = v.unit(uid, root == "package")
            brs = v.branches(u, root == "package")
            if len(brs) > 1:
                bi = next((i for i, br in enumerate(brs, 1) if any(m.id == p["mid"] for m in br) and all(m.on_current_path for m in br)), None)
                bi = bi or next((i for i, br in enumerate(brs, 1) if any(m.id == p["mid"] for m in br)), 1)
                path.append(f"{pre}{uid}|b{bi}")
            if p["kind"] == "block":
                path.append(f"{pre}{uid}#{p['mid']}")
        return path

    def c_goto(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        target = str(req.get("target") or "").strip()
        root = str(req.get("root") or "sources")
        if not target:
            raise ApiError(t("api.goto_needs"), "bad_request")
        v = self.views()
        node: Optional[str] = None
        if re.match(r"^\d{4}-\d{2}(-\d{2})?([T ]\d{2}:\d{2})?", target):
            want = target.replace(" ", "T")
            for nid, _, obj, _ in v.flat_items(root if root in ("sources", "package") else "sources", None, "message"):
                if Views.time_key(obj)[1] >= want:
                    node = nid
                    break
        else:
            pre = "pkg:" if root == "package" else ""
            key = target.removeprefix("pkg:")
            pid = parse_id(key)
            if pid is not None and v.unit(pid["uid"], bool(pre)) is not None:
                node = pre + key
            else:
                units = [e.unit for e in self.eng.package.entries] if (root == "package" and self.eng.package is not None) else self.eng.conversations
                hit = next((u for u in units if u.uid == key or u.uid.startswith(key) or u.uid.split(":", 1)[-1].startswith(key)
                            or (u.ext_id and u.ext_id == key)), None)
                node = pre + hit.uid if hit is not None else None
        if node is None:
            raise ApiError(t("api.goto_none", target=target), "not_found", 404)
        return {"root": root, "node": node, "path": self.path_of(root, node)}

    def c_bookmark(self, req: dict[str, Any]) -> dict[str, Any]:
        return self._op("bookmark", {"item": req.get("node"), "name": req.get("name"), "remove": bool(req.get("remove"))})

    def c_tag(self, req: dict[str, Any]) -> dict[str, Any]:
        return self._op("tag", {"item": req.get("node"), "tag": req.get("tag"), "remove": bool(req.get("remove"))})

    def c_views(self, req: dict[str, Any]) -> dict[str, Any]:
        action = str(req.get("action") or "list")
        views = self.eng.recipe.get("views") or []
        if action == "list":
            return {"views": [{"name": x.get("name")} for x in views]}
        if action == "load":
            hit = next((x for x in views if x.get("name") == req.get("name")), None)
            if hit is None:
                raise ApiError(t("api.view_none", name=req.get("name")), "not_found", 404)
            return {"name": hit["name"], "state": hit.get("state")}
        if action == "save":
            return self._op("view.save", {"name": req.get("name"), "state": req.get("state") or {}})
        if action == "delete":
            return self._op("view.delete", {"name": req.get("name")})
        raise ApiError(t("api.bad_action", action=action), "bad_request")

    def c_status(self, req: dict[str, Any]) -> dict[str, Any]:
        eng = self.eng
        job = self.job
        out: dict[str, Any] = {"version": self.version, "steps": len(eng.recipe.get("steps") or []),
                               "sources": len(eng.sources), "last": self.last}
        if job is not None or req.get("_locked") is False:
            if job is not None:
                out["job"] = job.public()
            out["busy"] = True
            return out  # the engine is busy: counters below would read a state that is changing
        if self._status_cache[0] != self.version or self._status_cache[2] is not eng:  # the commander asks every tick
            heavy = {"conversations": len(eng.conversations), "messages": sum(len(u.messages) for u in eng.conversations),
                     "selection": eng.selection_summary() if eng.units else {"conversations": 0, "messages": 0, "chars": 0, "tokens_est": 0},
                     "package": self._package_size(), "cost": self._cost(),
                     "undo": len(eng.package.oplog) if eng.package is not None else 0,
                     "redo": len(eng.package.redo) if eng.package is not None else 0}
            self._status_cache = (self.version, heavy, eng)
        out.update(self._status_cache[1])
        return out

    def _package_size(self) -> dict[str, Any]:
        pkg = self.eng.package
        if pkg is None:
            return {"entries": 0, "messages": 0, "chars": 0, "bytes_est": 0, "attachments_bytes": 0}
        chars = msgs = att = 0
        for e in pkg.entries:
            for m in e.unit.messages:
                if e.messages is None or m.id in e.messages:
                    msgs += 1
                    chars += m.chars()
            att += sum(int(a.size or 0) for a in e.unit.attachments if a.status == "resolved")
        per = float(self.ds.preset("commander.bytes_per_char", 1.5) or 1.5)
        return {"entries": len(pkg.entries), "folders": len(pkg.folders), "messages": msgs, "chars": chars,
                "attachments_bytes": att, "bytes_est": int(chars * per) + att}

    def _cost(self) -> Any:
        if self._cost_cache[0] == self.version:
            return self._cost_cache[1]
        cost: Any = None
        if self.eng.units:
            try:
                est = self.eng.op("cost", {}, by="engine")["estimate"]
                tot = est.get("total") or {}
                cost = {"calls": est.get("calls", 0), "amount": tot.get("amount"), "currency": tot.get("currency", "USD"),
                        "known": tot.get("known", False)}
            except Exception:  # noqa: BLE001 - the status line shows "?" rather than failing
                log.debug("cost estimate for the status line failed", exc_info=True)
        self._cost_cache = (self.version, cost)
        return cost

    def c_jobs(self, req: dict[str, Any]) -> dict[str, Any]:
        if req.get("id"):
            job = self.jobs.get(str(req["id"]))
            if job is None:
                raise ApiError(t("api.job_none", id=req["id"]), "not_found", 404)
            return {"job": job.public()}
        return {"jobs": [j.public() for j in self.jobs.values()], "running": self.job.id if self.job else None}

    def c_cancel(self, req: dict[str, Any]) -> dict[str, Any]:
        job = self.jobs.get(str(req["id"])) if req.get("id") else self.job
        if job is None or job.state not in ("queued", "running"):
            return {"cancelled": False}
        job.cancel = True
        with self.cond:
            self.cond.notify_all()
        return {"cancelled": True, "job": job.id}

    def c_answer(self, req: dict[str, Any]) -> dict[str, Any]:
        qid = str(req.get("question") or "")
        if not qid and self.job is not None and self.job.question:
            qid = self.job.question
        entry = self.questions.get(qid)
        if entry is None:
            raise ApiError(t("api.question_none", id=qid), "not_found", 404)
        yes = str(req.get("answer", "")).strip().lower() in ("yes", "tak", "t", "y", "true", "1")
        with self.cond:
            entry["answer"] = yes
            self.cond.notify_all()
        self.event("answer", question=qid, answer=yes)
        return {"question": qid, "answer": yes}

    def c_events(self, req: dict[str, Any]) -> dict[str, Any]:
        return {"events": self.events_after(int(req.get("after") or 0)), "seq": self.seq}

    def c_i18n(self, req: dict[str, Any]) -> dict[str, Any]:
        lang = str(req.get("lang") or language())
        return {"lang": lang, "strings": {k: v for k, v in table(lang).items() if k != "schema"}}

    def c_keymap(self, req: dict[str, Any]) -> dict[str, Any]:
        from .commander.keymap import Keymap
        km = Keymap.load(self.ds, req.get("profile"))
        return {"profile": km.profile, "bindings": km.bindings, "help": km.help_lines(), "problems": km.problems}

    def c_layout(self, req: dict[str, Any]) -> dict[str, Any]:
        return {"layout": self.layout}

    def c_recipe(self, req: dict[str, Any]) -> dict[str, Any]:
        norm = self.eng.normalised_recipe()
        return {"recipe": norm, "hash": recipe_hash(norm, self.ds)}

    def c_explain(self, req: dict[str, Any]) -> dict[str, Any]:
        self._need_units()
        nid = str(req.get("node") or "")
        p = parse_id(nid)
        if p is None:
            raise ApiError(t("api.unknown_node", node=nid), "bad_node", 404)
        res = self._op("explain", {"unit": p["uid"]})
        out = {"node": nid, "explain": res}
        if not res.get("selected"):
            why = []
            d = res.get("decision") or {}
            if d.get("decision"):
                why.append(t("api.why_not.decision", decision=d.get("decision"), source=d.get("source", "")))
            if res.get("manual"):
                why.append(t("api.why_not.manual", decision=res["manual"].get("decision")))
            if not why:
                why.append(t("api.why_not.no_step"))
            out["why_not"] = why
        return out

    def c_macro(self, req: dict[str, Any]) -> dict[str, Any]:
        name = str(req.get("name") or "")
        try:
            macro = self.ds.entry("macros", name)
        except Exception as exc:  # noqa: BLE001 - an unknown macro is the caller's error
            raise ApiError(t("api.macro_none", name=name), "not_found", 404) from exc
        preview = bool(req.get("preview"))  # edit steps only report what they would do (each one on the untouched package)
        results = [self._op(st["op"], dict(st.get("params") or {}, **({"preview": True} if preview and str(st["op"]).startswith("edit.") else {})),
                            note=f"macro:{name}") for st in macro.get("steps") or []]
        return {"macro": name, "steps": len(results), "preview": preview, "results": [_brief(r) for r in results]}

    def c_manifest(self, req: dict[str, Any]) -> dict[str, Any]:
        pkg = req.get("package") or (self.eng.last_write or {}).get("package") or self.package
        if not pkg or not (Path(str(pkg)) / "manifest.json").exists():
            raise ApiError(t("api.no_manifest"), "not_found", 404)
        return {"package": str(pkg), "manifest": json.loads((Path(str(pkg)) / "manifest.json").read_text(encoding="utf-8"))}


# ───────────────────────────── helpers ─────────────────────────────
def error_body(exc: BaseException) -> dict[str, Any]:
    """The answer to a failed call: a sentence in the face's language (the engine's own detail inside it, kept
    as ``detail`` too), and a stable ``code``."""
    from .edits import EditError
    from .flows import ModelRefused
    from .select_expr import ExprError
    if isinstance(exc, ApiError):
        return {"error": str(exc), "code": exc.code}
    if isinstance(exc, ModelRefused):
        return {"error": t("error.refused", detail=exc.reason), "detail": exc.reason, "code": "refused", "estimate": jsonable(exc.estimate)}
    if isinstance(exc, ExprError):
        return {"error": t("error.expr", detail=str(exc)), "detail": str(exc), "code": type(exc).__name__}
    if isinstance(exc, EditError):
        return {"error": t("error.edit", detail=str(exc)), "detail": str(exc), "code": type(exc).__name__}
    return {"error": f"{exc}", "code": type(exc).__name__}


def _brief(res: Any, keep: tuple[str, ...] = ()) -> dict[str, Any]:
    if not isinstance(res, dict):
        return {}
    out = {k: res[k] for k in ("op", "counts", "step", "seconds", "selection", "package", "reproducible_hash") if k in res}
    for k in keep:
        if k in res:
            out[k] = res[k]
    return out


def _cost_words(q: dict[str, Any]) -> dict[str, Any]:
    est = q.get("estimate") or {}
    tot = est.get("total") or {}
    return {"calls": est.get("calls", "?"), "amount": tot.get("amount", "?"), "currency": tot.get("currency", ""),
            "ratio": q.get("ratio", ""), "last": q.get("last", "")}


def _time_expr(req: dict[str, Any]) -> str:
    if req.get("around"):
        if req.get("days") is None:
            return f"around:{req['around']}"  # the grammar's preset window (around_default)
        return f"around:{req['around']}±{req['days']}{req.get('unit') or 'd'}"
    if req.get("week_of"):
        return f'week-of:"{req["week_of"]}"'
    lo, hi = req.get("from") or "", req.get("to") or ""
    field = req.get("field") or "created"
    if lo or hi:
        return f"{field}:{lo}..{hi}"
    if req.get("gap"):
        return f"gap:>{req['gap']}"
    raise ApiError(t("api.time_needs"), "bad_request")


def _as_json(kind: str, obj: Any) -> Any:
    from .select_expr import Item
    if isinstance(obj, Item):
        if kind == "conversation":
            return obj.unit.as_dict()
        if kind == "message":
            return obj.message.as_dict()
        if kind == "block":
            return obj.block.as_dict()
        return obj.attachment.as_dict()
    if kind == "branch":
        return {"conversation": obj["unit"].uid, "branch": obj["index"], "messages": [m.as_dict() for m in obj["messages"]]}
    if kind == "json":
        return obj["value"]
    return obj


def _markdown(kind: str, obj: Any) -> str:
    from .select_expr import Item

    def msg(m: Any) -> list[str]:
        out = [f"### {m.role} · {m.created_utc or '—'}{'' if m.on_current_path else ' (' + t('commander.side_branch') + ')'}", ""]
        for b in m.blocks:
            if b.kind == "text":
                out += [b.text or "", ""]
            else:
                body = b.text or (json.dumps(b.input, ensure_ascii=False, indent=1, default=str) if b.input is not None else "")
                out += [f"> **{b.kind}{' ' + b.name if b.name else ''}**", "", "```", body, "```", ""]
        return out
    if isinstance(obj, Item):
        u = obj.unit
        if kind == "conversation":
            lines = [f"# {u.title or u.ext_id or u.uid}", "",
                     f"- {t('commander.md.service')}: {u.service or '?'}", f"- {t('commander.md.created')}: {u.created_utc or '—'}",
                     f"- {t('commander.md.messages')}: {len(u.messages)}", f"- uid: `{u.uid}`", ""]
            for m in u.messages:
                lines += msg(m)
            for a in u.attachments:
                lines.append(f"- 📎 {a.name or a.ref} ({a.mime or '?'}, {a.size or '?'} B, {a.status})")
            return "\n".join(lines).rstrip() + "\n"
        if kind == "message":
            return "\n".join(msg(obj.message)).rstrip() + "\n"
        if kind == "block":
            b = obj.block
            body = b.text or (json.dumps(b.input, ensure_ascii=False, indent=1, default=str) if b.input is not None else "")
            return f"**{b.kind}{' ' + b.name if b.name else ''}**\n\n```\n{body}\n```\n"
        a = obj.attachment
        return f"**{a.name or a.ref}**\n\n- MIME: {a.mime or '?'}\n- {t('commander.md.size')}: {a.size or '?'} B\n- {t('commander.md.status')}: {a.status}\n- member: {a.member or '—'}\n"
    if kind == "branch":
        lines = [f"# {t('commander.branch')} {obj['index']}", ""]
        for m in obj["messages"]:
            lines += msg(m)
        return "\n".join(lines).rstrip() + "\n"
    return "```json\n" + json.dumps(_as_json(kind, obj), ensure_ascii=False, indent=1, default=str) + "\n```\n"


def _raw(eng: Any, kind: str, obj: Any) -> str:
    """The node's bytes as they are in the source: the conversation's element of the source array (found by
    the same span scanner ``preserve`` uses) or the sub-object a message's raw pointer names."""
    from .select_expr import Item
    if not isinstance(obj, Item):
        return json.dumps(_as_json(kind, obj), ensure_ascii=False, indent=1, default=str)
    u = obj.unit
    if u.raw is not None:
        base: Any = u.raw
    else:
        base = _raw_element(eng, u)
        if base is None:
            loc = json.dumps((u.source or {}).get("locator") or {}, ensure_ascii=False)
            return t("commander.raw_unavailable", locator=loc) + "\n"
    if kind == "conversation":
        return base if isinstance(base, str) else json.dumps(base, ensure_ascii=False, indent=1)
    ptr = obj.message.raw_pointer if obj.message is not None else None
    doc = json.loads(base) if isinstance(base, str) else base
    if ptr:
        loc = (u.source or {}).get("locator") or {}
        own = str(loc.get("json_pointer") or "")
        rel = ptr[len(own):] if own and ptr.startswith(own) else ptr
        from .readers.base import getpath
        try:
            sub = getpath(doc, rel.strip("/").replace("/", ".")) if rel.strip("/") else doc
        except Exception:  # noqa: BLE001 - a pointer the raw object does not have: the whole element is shown
            log.debug("raw pointer %s not in element", ptr, exc_info=True)
            sub = None
        if sub is not None:
            return json.dumps(sub, ensure_ascii=False, indent=1)
    return json.dumps(_as_json(kind, obj), ensure_ascii=False, indent=1, default=str)


def _raw_element(eng: Any, u: Any) -> Optional[str]:
    from .cut.common import find_archive
    from .cut.preserve import _indices
    from .jsonspan import SpanError, iter_spans
    a = find_archive(eng.archives, u)
    member = (u.source or {}).get("member")
    if a is None:
        return None
    wrapper, idx = _indices(u)
    if len(idx) != 1:
        return None
    want = next(iter(idx))
    try:
        fh = a.open_member(member) if member else a.open()
        with fh:
            for ev, i, data in iter_spans(fh, wrapper=wrapper, lead=True):
                if ev == "element" and i == want:
                    return data.decode("utf-8", errors="replace")
    except (OSError, SpanError, KeyError, ValueError):
        log.debug("raw bytes of %s not readable", u.uid, exc_info=True)
    return None


def _field_diff(a: Any, b: Any, path: str, out: list[dict[str, Any]]) -> None:
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b), key=str):
            if k not in a:
                out.append({"path": f"{path}/{k}", "op": "add", "b": b[k]})
            elif k not in b:
                out.append({"path": f"{path}/{k}", "op": "remove", "a": a[k]})
            else:
                _field_diff(a[k], b[k], f"{path}/{k}", out)
        return
    if isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        for i, (x, y) in enumerate(zip(a, b)):
            _field_diff(x, y, f"{path}/{i}", out)
        return
    if a != b:
        out.append({"path": path or "/", "op": "replace", "a": a, "b": b})


def _patch_lines(patch: Any) -> list[str]:
    if not isinstance(patch, list):
        return [str(patch)]
    out = []
    for p in patch:
        old = json.dumps(p.get("old"), ensure_ascii=False, default=str) if "old" in p else ""
        new = json.dumps(p.get("value"), ensure_ascii=False, default=str) if "value" in p else ""
        out.append(f"{p.get('op')} {p.get('path')}: {old[:80]}{' → ' if old and new else ''}{new[:80]}")
    return out


__all__ = ["Session", "ApiError", "Cancelled", "Job", "jsonable", "error_body"]
