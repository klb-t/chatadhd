"""``CommanderState``: everything the commander shows, driven by key names — no curses here.

Two panes (``Tab``), each a view of a root (``o`` cycles: sources, package, selection, timeline,
attachments, clusters, recipe, inventory, diff, bookmarks) as a tree or a flat list (``f``), sorted
(``s``) and filtered live (``Ctrl-F``, after every key).  Keys resolve through the keymap profile to
actions (``actions.json``); actions are either navigation/display (never recorded) or calls of the
dispatcher (``api.Session``), whose engine operations become recipe steps with ``by: "tui"``.  Long
calls run as background jobs: the screen keeps answering keys, the status line shows progress,
``Esc`` stops the job and the state stays as it was.  ``frame(width, height)`` returns the screen as
lines of (text, attribute) segments that ``view_curses`` draws and the headless driver reads.
"""
import json
import logging
import re
import shlex
from pathlib import Path
from typing import Any, Callable, Optional

from ..api import ApiError, Session
from ..i18n import label as _label, t
from .autosave import Autosave, resume_recipe
from .keymap import Keymap

log = logging.getLogger(__name__)

KEY_NAMES = {f"F{i}" for i in range(1, 13)} | {"Up", "Down", "Left", "Right", "PageUp", "PageDown", "Home", "End", "Insert",
                                                 "Delete", "Enter", "Tab", "S-Tab", "Backspace", "Esc", "Space", "Resize"} | \
    {f"C-{c}" for c in "abcdefghijklmnopqrstuvwxyz"}


def is_key_name(tok: str) -> bool:
    return tok in KEY_NAMES or len(tok) == 1


def parse_keys(script: str) -> list[str]:
    """``F5 Down "role:user" Enter`` → key names; a token that is not a key name is typed character by character."""
    out: list[str] = []
    for tok in shlex.split(script):
        if is_key_name(tok):
            out.append(tok)
        else:
            out += ["Space" if ch == " " else ch for ch in tok]
    return out


class Pane:
    def __init__(self, root: str = "sources", mode: str = "tree", **kw: Any) -> None:
        self.root = root
        self.path: list[str] = list(kw.get("path") or [])
        self.flat: Optional[str] = kw.get("flat") or (None if mode == "tree" else mode.split(":", 1)[-1] if ":" in mode else None)
        self.sort: str = kw.get("sort") or ""
        self.filter: str = kw.get("filter") or ""
        self.cursor: int = int(kw.get("cursor") or 0)
        self.top: int = 0
        self.data: Optional[dict[str, Any]] = None
        self.key: Any = None

    def state(self) -> dict[str, Any]:
        return {"root": self.root, "path": list(self.path), "flat": self.flat, "sort": self.sort, "filter": self.filter, "cursor": self.cursor}

    @property
    def rows(self) -> list[dict[str, Any]]:
        return (self.data or {}).get("rows") or []

    def current(self) -> Optional[dict[str, Any]]:
        rows = self.rows
        return rows[self.cursor] if 0 <= self.cursor < len(rows) else None


class CommanderState:
    def __init__(self, session: Session, *, profile: Optional[str] = None, width: int = 120, height: int = 32,
                 autosave: Optional[Path] = None, background: bool = True) -> None:
        self.s = session
        self.ds = session.ds
        self.km = Keymap.load(self.ds, profile)
        self.actions = {a["id"]: a for a in self.ds.file("commander/actions.json")["actions"]}
        self.layout = session.layout
        self.panes = [Pane(**p) for p in (self.layout.get("panes") or [{"root": "sources"}, {"root": "package"}])]
        self.active = 0
        self.mode = "normal"
        self.pending: list[str] = []
        self.prompt: Optional[dict[str, Any]] = None
        self.overlay: Optional[dict[str, Any]] = None
        self.message = ""
        self.message_error = False
        self.quit = False
        self.clipboard: list[str] = []
        self.compare_mark: Optional[str] = None
        self.hits: list[dict[str, Any]] = []
        self.hit_pos = -1
        self.formats = list(self.layout.get("viewer_formats") or ["json", "markdown", "raw"])
        self.format_i = 0
        self.job: Optional[str] = None
        self.job_then: Optional[Callable[[dict[str, Any]], None]] = None
        self.progress: dict[str, Any] = {}
        self.ev_seq = 0
        self.background = background
        self.width, self.height = width, height
        self.replay: list[dict[str, Any]] = []
        self.replay_pos = 0
        self.keylog: list[str] = []
        self.status_data: dict[str, Any] = {}
        every = int(self.ds.preset("commander.autosave.every", 1) or 0)
        self.autosave = Autosave(Path(autosave), every) if autosave is not None else None
        self.s.on_change = self._changed
        if self.km.problems:
            self._msg("; ".join(self.km.problems), error=True)

    # ───────────────────────────── plumbing ─────────────────────────────
    @property
    def pane(self) -> Pane:
        return self.panes[self.active]

    @property
    def other(self) -> Pane:
        return self.panes[1 - self.active]

    def _msg(self, text: str, error: bool = False) -> None:
        self.message, self.message_error = text, error

    def call(self, req: dict[str, Any], *, then: Optional[Callable[[dict[str, Any]], None]] = None,
             background: bool = False) -> Optional[dict[str, Any]]:
        """One dispatcher call; errors become the message line (the screen never crashes on a bad input)."""
        if background and self.background:
            req = dict(req, wait=False)
        try:
            res = self.s.call(req)
        except Exception as exc:  # noqa: BLE001 - every error is shown to the user, the session goes on
            log.debug("call %s failed", req.get("call"), exc_info=True)
            from ..api import error_body
            body = error_body(exc)
            self._msg(t("commander.error", error=body.get("error")), error=True)
            return None
        if res.get("busy"):
            self._msg(t("commander.busy"), error=True)
            return None
        if "job" in res and isinstance(res["job"], dict) and res["job"].get("state") in ("queued", "running") and req.get("wait") is False:
            self.job, self.job_then = res["job"]["id"], then
            self._msg(t("commander.job_started", call=req.get("call")))
            return res
        if then is not None:
            then(res)
        return res

    def _changed(self, session: Session, name: str) -> None:
        if self.autosave is None:
            return
        steps = len(session.eng.recipe.get("steps") or [])
        if self.autosave.due(steps) or name in ("bookmark", "tag", "views"):
            self.save_now()

    def save_now(self) -> Optional[Path]:
        if self.autosave is None:
            return None
        try:
            return self.autosave.write(self.s.eng.normalised_recipe(), [p.state() for p in self.panes],
                                       {"active": self.active, "clipboard": self.clipboard, "package": self.s.package})
        except Exception:  # noqa: BLE001 - autosave trouble is shown, never stops the work
            log.warning("autosave failed", exc_info=True)
            self._msg(t("commander.autosave_failed"), error=True)
            return None

    def resume(self, doc: dict[str, Any]) -> None:
        """Replay an autosave: its recipe (steps keep their face) and the panes."""
        res = self.call({"call": "open", "recipe": resume_recipe(doc)})
        if res is None:
            return
        for p, st in zip(self.panes, doc.get("panes") or []):
            p.__init__(**st)
        self.active = int(doc.get("active") or 0) % 2
        self.clipboard = list(doc.get("clipboard") or [])
        if doc.get("package") and not self.s.package:
            self.s.package = doc["package"]
        self._msg(t("commander.resumed", steps=res.get("steps", 0)))

    def refresh(self, pane: Optional[Pane] = None, force: bool = False) -> None:
        for p in ([pane] if pane is not None else self.panes):
            key = (self.s.version, p.root, tuple(p.path), p.flat, p.sort, p.filter)
            if not force and p.key == key and p.data is not None:
                continue
            res = None
            try:
                res = self.s.call({"call": "list", "root": p.root, "path": p.path, "flat": p.flat, "sort": p.sort, "filter": p.filter})
            except Exception as exc:  # noqa: BLE001 - a view that cannot be built shows why
                log.debug("list failed", exc_info=True)
                if p.path:  # e.g. the node went away after an edit: back to the root of the view
                    p.path, p.cursor = [], 0
                    self._msg(t("commander.view_reset", error=str(exc)), error=True)
                    self.refresh(p, force=True)
                    continue
                self._msg(t("commander.error", error=str(exc)), error=True)
            if res is None or res.get("busy"):
                continue  # keep showing the last rows while the engine works
            p.data, p.key = res, key
            p.cursor = max(0, min(p.cursor, len(p.rows) - 1))

    def tick(self) -> None:
        """Poll the background job and the events (progress, questions, notes); refresh the panes."""
        for ev in self.s.events_after(self.ev_seq):
            self.ev_seq = ev["seq"]
            if ev["kind"] == "progress":
                self.progress = {k: v for k, v in ev.items() if k not in ("seq", "kind")}
            elif ev["kind"] == "question" and self.mode != "question":
                self._question(ev.get("text") or t("commander.question"), ev.get("id"))
            elif ev["kind"] == "note" and not self.message_error:
                self._msg(str(ev.get("text") or ""))
        if self.job is not None:
            res = self.s.call({"call": "jobs", "id": self.job})["job"]
            if res["state"] not in ("queued", "running"):
                then, self.job, self.job_then, self.progress = self.job_then, None, None, {}
                if res["state"] == "done":
                    self._msg(t("commander.job_done", call=res["call"], seconds=res["seconds"]))
                    if then is not None:
                        try:
                            then(res.get("result") or {})
                        except Exception as exc:  # noqa: BLE001 - shown, not raised
                            log.debug("job continuation failed", exc_info=True)
                            self._msg(t("commander.error", error=str(exc)), error=True)
                elif res["state"] == "cancelled":
                    self._msg(t("commander.job_cancelled", call=res["call"]))
                else:
                    self._msg(t("commander.error", error=(res.get("error") or {}).get("error")), error=True)
        if self.job is None:
            self.refresh()
            st = self.s.call({"call": "status"})
            if not st.get("busy"):
                self.status_data = st

    # ───────────────────────────── keys ─────────────────────────────
    def feed(self, key: str) -> None:
        self.keylog.append(key)
        if key == "Resize":
            return
        if self.mode == "normal" and not self.pending:
            self.message, self.message_error = "", False  # a message lives until the next key; then the key hints return
        handler = {"prompt": self._prompt_key, "viewer": self._viewer_key, "help": self._viewer_key, "menu": self._menu_key,
                   "preview": self._preview_key, "question": self._question_key}.get(self.mode)
        if handler is not None:
            handler(key)
            return
        if self.replay and key in ("Enter", "n") and not self.pending:
            self.a_replay_next()
            return
        action, self.pending = self.km.resolve(self.pending, key)
        if action is not None:
            self.do(action)

    def feed_all(self, keys: list[str]) -> None:
        for k in keys:
            self.feed(k)
            self.tick()

    def do(self, action: str) -> None:
        fn = getattr(self, "a_" + action.replace(".", "_"), None)
        if fn is None:
            self._msg(t("commander.no_action", action=action), error=True)
            return
        self.refresh(self.pane)
        fn()

    def _nav(self, key: str) -> Optional[str]:
        """Navigation action of a key in an overlay (arrows, page keys, and their keymap aliases)."""
        direct = {"Up": "cursor.up", "Down": "cursor.down", "PageUp": "page.up", "PageDown": "page.down", "Home": "top", "End": "bottom",
                  "Enter": "open", "Esc": "cancel"}
        if key in direct:
            return direct[key]
        act, _ = self.km.resolve([], key)
        return act

    # -- prompt ----------------------------------------------------------------------------
    def ask_text(self, label: str, on_submit: Callable[[str], None], default: str = "",
                 on_change: Optional[Callable[[str], None]] = None, on_cancel: Optional[Callable[[], None]] = None) -> None:
        self.mode = "prompt"
        self.prompt = {"label": label, "buffer": default, "submit": on_submit, "change": on_change, "cancel": on_cancel}

    def _prompt_key(self, key: str) -> None:
        p = self.prompt
        assert p is not None
        if key == "Enter":
            self.mode, self.prompt = "normal", None
            p["submit"](p["buffer"])
            return
        if key in ("Esc", "C-c", "C-g"):
            self.mode, self.prompt = "normal", None
            if p.get("cancel"):
                p["cancel"]()
            return
        if key == "Backspace":
            p["buffer"] = p["buffer"][:-1]
        elif key == "C-u":
            p["buffer"] = ""
        elif key == "Space":
            p["buffer"] += " "
        elif len(key) == 1:
            p["buffer"] += key
        else:
            return
        if p.get("change"):
            p["change"](p["buffer"])

    # -- overlays ------------------------------------------------------------------------------
    def show(self, mode: str, title: str, lines: list[str], **kw: Any) -> None:
        self.mode = mode
        self.overlay = {"title": title, "lines": lines, "top": 0, **kw}

    def close_overlay(self) -> None:
        self.mode, self.overlay = "normal", None

    def _scroll(self, act: Optional[str]) -> bool:
        o = self.overlay
        assert o is not None
        page = max(1, self.height - 6)
        n = len(o["lines"])
        moves = {"cursor.up": -1, "cursor.down": 1, "page.up": -page, "page.down": page}
        if act in moves:
            o["top"] = max(0, min(max(0, n - 1), o["top"] + moves[act]))
        elif act == "top":
            o["top"] = 0
        elif act == "bottom":
            o["top"] = max(0, n - page)
        else:
            return False
        return True

    def _viewer_key(self, key: str) -> None:
        act = self._nav(key)
        if self._scroll(act):
            return
        if act == "view.cycle" and self.mode == "viewer" and self.overlay and self.overlay.get("node"):
            self.format_i = (self.format_i + 1) % len(self.formats)
            self._open_viewer(self.overlay["node"])
            return
        if act in ("cancel", "view", "quit", "help", "open") or key in ("q", "F3", "F1", "F10"):
            self.close_overlay()

    def menu(self, title: str, items: list[dict[str, Any]]) -> None:
        self.show("menu", title, [], items=items, cursor=0, filter="")

    def _menu_items(self) -> list[dict[str, Any]]:
        o = self.overlay or {}
        f = (o.get("filter") or "").casefold()
        lex = self.s.eng.lex
        items = o.get("items") or []
        if not f:
            return items
        needle = lex.fold(f)
        return [i for i in items if needle in lex.fold(str(i.get("label", "")).casefold())]

    def _menu_key(self, key: str) -> None:
        o = self.overlay
        assert o is not None
        items = self._menu_items()
        if key in ("Up", "Down", "PageUp", "PageDown", "Home", "End"):
            step = {"Up": -1, "Down": 1, "PageUp": -10, "PageDown": 10, "Home": -10 ** 9, "End": 10 ** 9}[key]
            o["cursor"] = max(0, min(len(items) - 1, o["cursor"] + step))
        elif key == "Enter":
            if 0 <= o["cursor"] < len(items):
                item = items[o["cursor"]]
                self.close_overlay()
                item["do"]()
        elif key in ("Esc", "F2", "F10", "C-c"):
            self.close_overlay()
        elif key == "Backspace":
            o["filter"] = (o.get("filter") or "")[:-1]
            o["cursor"] = 0
        elif key == "Space" or len(key) == 1:
            o["filter"] = (o.get("filter") or "") + (" " if key == "Space" else key)
            o["cursor"] = 0

    def _preview_key(self, key: str) -> None:
        act = self._nav(key)
        if self._scroll(act):
            return
        o = self.overlay or {}
        if key in ("Enter", "y", "t", "F4"):
            self.close_overlay()
            if o.get("apply"):
                o["apply"]()
        elif key in ("Esc", "n", "q", "F10", "C-c"):
            self.close_overlay()
            self._msg(t("commander.preview_discarded"))

    def _question(self, text: str, qid: Optional[str], on_answer: Optional[Callable[[bool], None]] = None) -> None:
        self.show("question", t("commander.question"), [text, "", t("commander.question_keys")], qid=qid, on_answer=on_answer)

    def _question_key(self, key: str) -> None:
        o = self.overlay or {}
        if key in ("y", "t", "Y", "T"):
            yes = True
        elif key in ("n", "N", "Esc", "Enter", "C-c"):
            yes = False  # "no" is the preset answer: Enter alone never spends money
        else:
            return
        self.close_overlay()
        if o.get("qid"):
            self.call({"call": "answer", "question": o["qid"], "answer": "yes" if yes else "no"})
        if o.get("on_answer"):
            o["on_answer"](yes)

    # ───────────────────────────── helpers for actions ─────────────────────────────
    def cur(self) -> Optional[dict[str, Any]]:
        self.refresh(self.pane)
        return self.pane.current()

    def cur_id(self) -> Optional[str]:
        r = self.cur()
        return r["id"] if r else None

    def marked_rows(self, pane: Optional[Pane] = None) -> list[dict[str, Any]]:
        return [r for r in (pane or self.pane).rows if r.get("sel")]

    def current_folder(self, pane: Pane) -> str:
        if pane.root != "package":
            return "/"
        dirs = [p for p in pane.path if p.startswith("dir:")]
        return dirs[-1][4:] if dirs else "/"

    def _need_row(self) -> Optional[dict[str, Any]]:
        r = self.cur()
        if r is None:
            self._msg(t("commander.nothing_here"), error=True)
        return r

    def _content_id(self, r: Optional[dict[str, Any]]) -> Optional[str]:
        if r is None:
            return None
        if r["kind"] in ("conversation", "branch", "message", "block", "attachment"):
            return r["id"]
        self._msg(t("commander.not_content"), error=True)
        return None

    def _uid(self, nid: str) -> str:
        return re.split(r"[#@|]", nid.removeprefix("pkg:"), maxsplit=1)[0]

    # ───────────────────────────── actions: session ─────────────────────────────
    def a_help(self) -> None:
        lines = [t("commander.help.profile", profile=self.km.profile), ""] + self.km.help_lines()
        if self.km.shadowed:
            lines += ["", t("commander.help.shadowed")] + ["  " + s for s in self.km.shadowed]
        if self.km.problems:
            lines += ["", t("commander.help.problems")] + ["  " + s for s in self.km.problems]
        self.show("help", t("commander.help.title"), lines)

    def a_menu(self) -> None:
        items: list[dict[str, Any]] = []
        for aid, a in self.actions.items():
            if aid in ("menu", "cancel"):
                continue
            keys = ", ".join(self.km.keys_for(aid))
            items.append({"label": f"{_label(a.get('label'))}{'  [' + keys + ']' if keys else ''}", "do": (lambda x=aid: self.do(x))})
        for root in self.layout.get("roots") or []:
            items.append({"label": t("commander.menu.root", root=_label((self.layout.get("root_labels") or {}).get(root)) or root),
                          "do": (lambda r=root: self._set_root(self.pane, r))})
        for op_name in ("inventory", "timeline", "clusters", "stats", "propose", "dry_run"):
            items.append({"label": t("commander.menu.op", op=op_name), "do": (lambda o=op_name: self._run_op(o))})
        self.menu(t("commander.menu.title"), items)

    def _run_op(self, name: str, params: Optional[dict[str, Any]] = None) -> None:
        def done(res: dict[str, Any]) -> None:
            r = res.get("result") or res
            self._msg(t("commander.op_done", op=name, counts=json.dumps(r.get("counts") or {}, ensure_ascii=False)))
            if name in ("inventory", "clusters", "timeline", "propose", "dry_run", "stats"):
                self.show("viewer", name, json.dumps({k: v for k, v in r.items() if k not in ("op", "seconds")}, ensure_ascii=False,
                                                     indent=1, default=str).splitlines())
        self.call({"call": "op", "op": name, "params": params or {}}, then=done, background=True)

    def a_quit(self) -> None:
        if self.job is not None:
            self._question(t("commander.quit_running"), None, lambda yes: self._quit_now(cancel=True) if yes else None)
            return
        self._quit_now()

    def _quit_now(self, cancel: bool = False) -> None:
        if cancel and self.job is not None:
            self.call({"call": "cancel", "id": self.job})
        self.save_now()
        self.quit = True

    def a_cancel(self) -> None:
        if self.job is not None:
            self.call({"call": "cancel", "id": self.job})
            self._msg(t("commander.cancelling"))
        elif self.compare_mark or self.pending:
            self.compare_mark, self.pending = None, []
            self._msg("")
        else:
            self._msg("")

    def a_command(self) -> None:
        def run(text: str) -> None:
            text = text.strip()
            if not text:
                return
            word, _, rest = text.partition(" ")
            try:
                if word in ("q", "quit", "wyjdz"):
                    self.a_quit()
                elif word == "op":
                    name, _, js = rest.strip().partition(" ")
                    self._run_op(name, json.loads(js) if js.strip() else {})
                elif word == "call":
                    name, _, js = rest.strip().partition(" ")
                    res = self.call(dict(json.loads(js) if js.strip() else {}, call=name))
                    if res is not None:
                        self.show("viewer", name, json.dumps(res, ensure_ascii=False, indent=1, default=str).splitlines())
                elif word in ("sel", "zaznacz"):
                    self._select({"how": "expr", "expr": rest, "mode": "add"})
                elif word in ("filter", "filtr"):
                    self.pane.filter = rest
                    self.refresh(self.pane)
                else:
                    self._msg(t("commander.command_unknown", word=word), error=True)
            except json.JSONDecodeError as exc:
                self._msg(t("commander.error", error=str(exc)), error=True)
        self.ask_text(":", run)

    # ───────────────────────────── actions: navigation ─────────────────────────────
    def a_pane_switch(self) -> None:
        self.active = 1 - self.active

    def _move(self, delta: int) -> None:
        p = self.pane
        n = len(p.rows)
        p.cursor = max(0, min(n - 1, p.cursor + delta)) if n else 0

    def a_cursor_up(self) -> None:
        self._move(-1)

    def a_cursor_down(self) -> None:
        self._move(1)

    def a_page_up(self) -> None:
        self._move(-max(1, self.list_height()))

    def a_page_down(self) -> None:
        self._move(max(1, self.list_height()))

    def a_top(self) -> None:
        self.pane.cursor = 0

    def a_bottom(self) -> None:
        self.pane.cursor = max(0, len(self.pane.rows) - 1)

    def a_open(self) -> None:
        r = self.cur()
        if r is None:
            return
        if r.get("children") and self.pane.flat and r["kind"] in ("conversation", "message"):
            # from a flat list (or the timeline) into the node's own tree, where its blocks and branches are
            root = "package" if self.pane.root == "package" else "sources"
            res = self.call({"call": "goto", "target": r["id"], "root": root})
            if res is not None:
                p = self.pane
                p.root, p.flat, p.filter, p.sort = root, None, "", ""
                p.path, p.cursor = list(res["path"]) + [r["id"]], 0
                self.refresh(p)
            return
        if r.get("children"):
            self.pane.path.append(r["id"])
            self.pane.cursor = 0
            self.refresh(self.pane)
        else:
            self.a_view()

    def a_parent(self) -> None:
        p = self.pane
        if p.flat and not p.path:
            return
        if not p.path:
            return
        child = p.path.pop()
        self.refresh(p)
        p.cursor = next((i for i, r in enumerate(p.rows) if r["id"] == child), 0)

    def _set_root(self, p: Pane, root: str) -> None:
        p.root, p.path, p.cursor, p.flat, p.filter, p.sort = root, [], 0, None, "", ""
        self.refresh(p)

    def a_root_next(self) -> None:
        roots = self.layout.get("roots") or ["sources", "package"]
        i = roots.index(self.pane.root) if self.pane.root in roots else -1
        self._set_root(self.pane, roots[(i + 1) % len(roots)])

    def a_mode_flat(self) -> None:
        kinds = [None] + list(self.layout.get("flat_kinds") or [])
        p = self.pane
        p.flat = kinds[(kinds.index(p.flat) + 1) % len(kinds)] if p.flat in kinds else None
        p.cursor = 0
        self.refresh(p)
        self._msg(t("commander.flat", kind=p.flat or t("commander.tree")))

    def a_sort_cycle(self) -> None:
        p = self.pane
        kind = p.flat or ((p.rows[0]["kind"]) if p.rows else "*")
        sorts = (self.layout.get("sorts") or {}).get(kind) or (self.layout.get("sorts") or {}).get("*") or [""]
        p.sort = sorts[(sorts.index(p.sort) + 1) % len(sorts)] if p.sort in sorts else sorts[0]
        self.refresh(p)
        self._msg(t("commander.sorted", sort=p.sort or t("commander.natural")))

    def _go(self, pane: Pane, root: str, path: list[str], node: str) -> None:
        if pane.root != root:
            pane.root, pane.flat, pane.filter, pane.sort = root, None, "", ""
        pane.path = list(path)
        self.refresh(pane, force=True)
        pane.cursor = next((i for i, r in enumerate(pane.rows) if r["id"] == node), 0)

    def a_goto(self) -> None:
        def go(text: str) -> None:
            root = self.pane.root if self.pane.root in ("sources", "package") else "sources"
            res = self.call({"call": "goto", "target": text, "root": root})
            if res is not None:
                self._go(self.pane, res["root"], res["path"], res["node"])
        self.ask_text(t("commander.prompt.goto"), go)

    def a_search(self) -> None:
        def go(text: str) -> None:
            roots = [p.root for p in (self.pane, self.other) if p.root in ("sources", "package")] or ["sources"]
            res = self.call({"call": "search", "text": text, "roots": list(dict.fromkeys(roots))})
            if res is None:
                return
            self.hits, self.hit_pos = res["hits"], -1
            if not self.hits:
                self._msg(t("commander.search_none", text=text))
                return
            self.a_search_next()
        self.ask_text(t("commander.prompt.search"), go)

    def a_search_next(self) -> None:
        if not self.hits:
            self._msg(t("commander.search_none", text=""))
            return
        self.hit_pos = (self.hit_pos + 1) % len(self.hits)
        h = self.hits[self.hit_pos]
        pane = self.pane if self.pane.root == h["root"] else self.other if self.other.root == h["root"] else self.pane
        if pane is self.other:
            self.active = 1 - self.active
        self._go(pane, h["root"], h["path"], h["id"])
        self._msg(t("commander.search_hit", n=self.hit_pos + 1, total=len(self.hits), label=str(h.get("label", ""))[:50]))

    def a_filter_live(self) -> None:
        p = self.pane
        before = p.filter

        def change(text: str) -> None:
            p.filter = text
            p.cursor = 0
            self.refresh(p)

        def cancel() -> None:
            change(before)
        self.ask_text(t("commander.prompt.filter"), lambda text: change(text), default=p.filter, on_change=change, on_cancel=cancel)

    def a_filter_save(self) -> None:
        if not self.pane.filter:
            self._msg(t("commander.filter_empty"), error=True)
            return
        self.ask_text(t("commander.prompt.name"), lambda name: self.call(
            {"call": "filter", "action": "save", "name": name, "expr": self.pane.filter},
            then=lambda r: self._msg(t("commander.filter_saved", name=r.get("saved")))))

    def a_filter_load(self) -> None:
        res = self.call({"call": "filter", "action": "list"})
        if res is None:
            return
        if not res["filters"]:
            self._msg(t("commander.no_filters"), error=True)
            return

        def use(expr: str) -> None:
            self.pane.filter, self.pane.cursor = expr, 0
            self.refresh(self.pane)
        self.menu(t("commander.menu.filters"), [{"label": f"{f['id']}: {f['expr']}", "do": (lambda e=f["expr"]: use(e))}
                                               for f in res["filters"]])

    # ───────────────────────────── actions: view ─────────────────────────────
    def _open_viewer(self, nid: str) -> None:
        fmt = self.formats[self.format_i % len(self.formats)]
        res = self.call({"call": "view", "node": nid, "format": fmt})
        if res is not None:
            self.show("viewer", f"{fmt} · {nid[:60]}", res["text"].splitlines() or [""], node=nid)

    def a_view(self) -> None:
        r = self._need_row()
        if r is not None:
            self._open_viewer(r["id"])

    def a_view_cycle(self) -> None:
        self.format_i = (self.format_i + 1) % len(self.formats)
        self._msg(t("commander.format", format=self.formats[self.format_i]))

    def a_diff_nodes(self) -> None:
        r = self._need_row()
        if r is None:
            return
        if self.compare_mark is None:
            self.compare_mark = r["id"]
            self._msg(t("commander.compare_first", node=r["label"][:40]))
            return
        a, self.compare_mark = self.compare_mark, None
        res = self.call({"call": "compare", "a": a, "b": r["id"]})
        if res is None:
            return
        lines = [t("commander.compare_same")] if res["same"] else []
        for f in res["fields"]:
            lines.append(f"{f['op']:8} {f['path']}: {json.dumps(f.get('a'), ensure_ascii=False, default=str)[:60]} → "
                         f"{json.dumps(f.get('b'), ensure_ascii=False, default=str)[:60]}")
        lines += [""] + res["text_diff"]
        self.show("viewer", t("commander.compare_title"), lines)

    def a_explain(self) -> None:
        nid = self._content_id(self._need_row())
        if nid is None:
            return
        res = self.call({"call": "explain", "node": nid})
        if res is not None:
            lines = json.dumps(res.get("explain"), ensure_ascii=False, indent=1, default=str).splitlines()
            if res.get("why_not"):
                lines = [t("commander.why_not")] + ["  - " + w for w in res["why_not"]] + [""] + lines
            self.show("viewer", t("commander.explain_title"), lines)

    def a_selection_show(self) -> None:
        res = self.call({"call": "selection"})
        if res is None:
            return
        s = res["summary"]
        lines = [t("commander.status.selection", **s), ""]
        for it in res["items"]:
            lines.append(f"{'*' if it['messages'] == 'all' else '+'} {str(it.get('label') or it['id'])[:60]}")
            for r in it.get("reasons") or []:
                lines.append(f"    {r}")
        self.show("viewer", t("commander.selection_title"), lines)

    # ───────────────────────────── actions: selection ─────────────────────────────
    def _select(self, req: dict[str, Any], *, background: bool = False) -> None:
        def done(res: dict[str, Any]) -> None:
            s = res.get("selection") or {}
            self._msg(t("commander.selected", **{k: s.get(k, 0) for k in ("conversations", "messages")}))
        self.call(dict(req, call="select"), then=done, background=background)

    def a_select_toggle(self) -> None:
        nid = self._content_id(self._need_row())
        if nid is None:
            return
        self._select({"how": "toggle", "items": [nid]})
        self._move(1)  # Insert moves on, as in Norton Commander

    def a_select_invert(self) -> None:
        self._select({"how": "invert"})

    def _expr_prompt(self, mode: str) -> None:
        self.ask_text(t("commander.prompt.expr" if mode == "add" else "commander.prompt.deexpr"),
                      lambda text: self._select({"how": "expr", "expr": text, "mode": mode}) if text.strip() else None)

    def a_select_expr(self) -> None:
        self._expr_prompt("add")

    def a_deselect_expr(self) -> None:
        self._expr_prompt("subtract")

    def a_select_regex(self) -> None:
        def go(text: str) -> None:
            field, sep, pat = text.partition(":")
            if not sep:
                field, pat = "text", text
            self._select({"how": "regex", "field": field.strip() or "text", "pattern": pat.strip(), "flags": "i"})
        self.ask_text(t("commander.prompt.regex"), go)

    def a_select_pattern(self) -> None:
        pats = (self.ds.file("select_fields.json").get("patterns") or {})
        self.menu(t("commander.menu.patterns"), [
            {"label": f"{name}: {_label(p.get('label')) or p.get('expr')}  [{p.get('expr')}]",
             "do": (lambda n=name: self._select({"how": "pattern", "pattern": n}))} for name, p in sorted(pats.items())])

    def a_select_time(self) -> None:
        def go(text: str) -> None:
            text = text.strip()
            m = re.match(r"^(\d{4}-\d{2}(?:-\d{2})?)\s*±\s*(\d+)\s*d?$", text)
            if m:
                req = {"how": "time", "around": m.group(1), "days": int(m.group(2))}
            elif ".." in text:
                lo, hi = text.split("..", 1)
                req = {"how": "time", "from": lo.strip(), "to": hi.strip()}
            elif text:
                req = {"how": "time", "week_of": text}
            else:
                return
            self._select(req)
        self.ask_text(t("commander.prompt.time"), go)

    def a_select_similar(self) -> None:
        nid = self._content_id(self._need_row())
        if nid is not None:
            self._select({"how": "similar", "to": [self._uid(nid)]}, background=True)

    def a_select_criteria(self) -> None:
        self.ask_text(t("commander.prompt.terms"), lambda text: self._select(
            {"how": "criteria", "terms": [x.strip() for x in text.split(",") if x.strip()]}, background=True) if text.strip() else None)

    def a_select_clear(self) -> None:
        self._select({"how": "clear"})

    def _decide(self, decision: str) -> None:
        nid = self._content_id(self._need_row())
        if nid is None:
            return
        uid = self._uid(nid)
        p = {"unit": uid, "decision": decision, "reason": "tui"}
        if "#" in nid:
            p["message"] = nid.removeprefix("pkg:").split("#", 1)[1].split("/", 1)[0]
            p["grain"] = "message"
        self.call({"call": "op", "op": "mark", "params": p}, then=lambda r: self._msg(t("commander.decided", decision=decision)))

    def a_decide_include(self) -> None:
        self._decide("include")

    def a_decide_exclude(self) -> None:
        self._decide("exclude")

    # ───────────────────────────── actions: package ─────────────────────────────
    def _package_targets(self) -> list[str]:
        rows = self.marked_rows() if self.pane.root == "package" else []
        if rows:
            return [r["id"].removeprefix("pkg:") for r in rows if r["kind"] == "conversation"]
        r = self.cur()
        if r is None or r["kind"] != "conversation":
            return []
        return [r["id"].removeprefix("pkg:")]

    def _apply_op(self, op: str, params: dict[str, Any], done_key: str) -> None:
        self.call({"call": "apply", "op": op, "params": params},
                  then=lambda res: self._msg(t(done_key, counts=json.dumps((res.get("result") or {}).get("counts") or {}, ensure_ascii=False))))

    def a_copy(self) -> None:
        into = self.current_folder(self.other)
        if self.pane.root == "package":
            self._msg(t("commander.copy_from_package"), error=True)
            return
        # the selection when there is one (the status line shows its size), otherwise the node under the cursor
        st = self.s.call({"call": "status"})
        if not st.get("busy") and (st.get("selection") or {}).get("conversations", 0):
            self._apply_op("package.copy", {"from": "selection", "into": into}, "commander.copied")
            return
        nid = self._content_id(self._need_row())
        if nid is not None:
            self._apply_op("package.copy", {"items": [nid], "into": into}, "commander.copied")

    def a_yank(self) -> None:
        rows = self.marked_rows() or ([self.cur()] if self.cur() else [])
        self.clipboard = [r["id"].removeprefix("pkg:") for r in rows if r and r["kind"] in ("conversation", "branch", "message", "block", "attachment")]
        self._msg(t("commander.yanked", n=len(self.clipboard)))

    def a_paste(self) -> None:
        if not self.clipboard:
            self._msg(t("commander.clipboard_empty"), error=True)
            return
        pane = self.pane if self.pane.root == "package" else self.other
        self._apply_op("package.copy", {"items": list(self.clipboard), "into": self.current_folder(pane)}, "commander.copied")

    def a_move(self) -> None:
        items = self._package_targets()
        if not items:
            self._msg(t("commander.need_package_entry"), error=True)
            return
        default = self.current_folder(self.other) if self.other.root == "package" else self.current_folder(self.pane)
        self.ask_text(t("commander.prompt.folder"), lambda f: self._apply_op("package.move", {"items": items, "into": _folder(f)},
                                                                            "commander.moved") if f.strip() else None, default=default)

    def a_mkdir(self) -> None:
        base = self.current_folder(self.pane)

        def go(name: str) -> None:
            name = name.strip().strip("/")
            if name:
                self._apply_op("package.mkdir", {"path": _folder(name if name.startswith("/") else base.rstrip("/") + "/" + name)}, "commander.made")
        self.ask_text(t("commander.prompt.mkdir"), go)

    def a_remove(self) -> None:
        if self.pane.root == "package":
            items = self._package_targets()
        else:
            nid = self._content_id(self.cur())
            items = [self._uid(nid)] if nid else []
        if not items:
            self._msg(t("commander.need_package_entry"), error=True)
            return
        self._apply_op("package.remove", {"items": items}, "commander.removed")

    def a_rename(self) -> None:
        r = self.cur()
        if r is None or self.pane.root != "package" or r["kind"] != "conversation":
            self._msg(t("commander.need_package_entry"), error=True)
            return
        uid = r["id"].removeprefix("pkg:")
        self.ask_text(t("commander.prompt.rename"), lambda title: self._apply_op("package.rename", {"item": uid, "title": title},
                                                                                "commander.renamed") if title.strip() else None,
                      default=str(r.get("label") or ""))

    # ───────────────────────────── actions: batch edit ─────────────────────────────
    def a_edit(self) -> None:
        edits = self.ds.entries("edits")
        items = []
        for eid, spec in sorted(edits.items(), key=lambda kv: (str(kv[1].get("group")), kv[0])):
            items.append({"label": f"{spec.get('group', '')}: {_label(spec.get('label'))} ({eid})", "do": (lambda e=eid: self._edit_params(e))})
        self.menu(t("commander.menu.edits"), items)

    def _edit_params(self, eid: str) -> None:
        spec = self.ds.entry("edits", eid)
        params = [(k, v) for k, v in (spec.get("params") or {}).items() if k != "preview"]
        params.sort(key=lambda kv: (kv[0] != "where", not kv[1].get("required")))
        sel = (self.status_data.get("selection") or {}).get("conversations", 0)
        got: dict[str, Any] = {}

        def ask(i: int) -> None:
            if i >= len(params):
                self._edit_preview(eid, got)
                return
            name, ps = params[i]
            default = "selection" if name == "where" and sel else ps.get("default")
            shown = json.dumps(default, ensure_ascii=False) if isinstance(default, (dict, list)) else ("" if default is None else str(default))
            req = "*" if ps.get("required") else ""

            def submit(text: str) -> None:
                if text.strip() == "" and not ps.get("required"):
                    pass
                else:
                    try:
                        got[name] = _parse_value(text, ps.get("type", "string"))
                    except ValueError as exc:
                        self._msg(t("commander.error", error=str(exc)), error=True)
                        return
                ask(i + 1)
            self.ask_text(f"{eid}.{name}{req} ({ps.get('type', 'string')})", submit, default=shown)
        ask(0)

    def _edit_preview(self, eid: str, params: dict[str, Any]) -> None:
        res = self.call({"call": "edit", "edit": eid, "params": params})
        if res is None:
            return
        lines = list(res.get("lines") or []) + [t("commander.preview_count", count=res.get("count"), items=(res.get("scope") or {}).get("items"),
                                                   patch=res.get("patch_total")), ""] + list(res.get("patch_text") or [])
        if res.get("invertible") == "none":
            lines.insert(0, t("commander.irreversible"))

        def apply() -> None:
            self.call({"call": "apply", "edit": eid, "params": params},
                      then=lambda r: self._msg(t("commander.applied", edit=eid, lines="; ".join((r.get("result") or {}).get("lines") or []))))
        self.show("preview", t("commander.preview_title", edit=eid), lines, apply=apply)

    def a_undo(self) -> None:
        self.call({"call": "undo"}, then=lambda r: self._msg(t("commander.undone", ops=", ".join((r.get("result") or {}).get("undone") or []) or "—")))

    def a_redo(self) -> None:
        self.call({"call": "redo"}, then=lambda r: self._msg(t("commander.redone", ops=", ".join((r.get("result") or {}).get("redone") or []) or "—")))

    # ───────────────────────────── actions: recipe data ─────────────────────────────
    def a_bookmark(self) -> None:
        r = self._need_row()
        if r is None:
            return
        self.call({"call": "bookmark", "node": r["id"], "remove": bool(r.get("bookmark"))},
                  then=lambda res: self._msg(t("commander.bookmarked" if res.get("bookmarked") else "commander.unbookmarked")))

    def a_tag(self) -> None:
        r = self._need_row()
        if r is None:
            return

        def go(text: str) -> None:
            text = text.strip()
            if not text:
                return
            remove = text.startswith("-")
            self.call({"call": "tag", "node": r["id"], "tag": text.lstrip("-"), "remove": remove},
                      then=lambda res: self._msg(t("commander.tagged", tags=", ".join(res.get("tags") or []) or "—")))
        self.ask_text(t("commander.prompt.tag"), go)

    def a_view_save(self) -> None:
        self.ask_text(t("commander.prompt.name"), lambda name: self.call(
            {"call": "views", "action": "save", "name": name, "state": {"panes": [p.state() for p in self.panes], "active": self.active}},
            then=lambda r: self._msg(t("commander.view_saved", name=name))) if name.strip() else None)

    def a_view_load(self) -> None:
        res = self.call({"call": "views", "action": "list"})
        if res is None:
            return

        def load(name: str) -> None:
            r = self.call({"call": "views", "action": "load", "name": name})
            if r is None:
                return
            st = r.get("state") or {}
            for p, ps in zip(self.panes, st.get("panes") or []):
                p.__init__(**ps)
            self.active = int(st.get("active") or 0) % 2
            self.refresh(force=True)
            self._msg(t("commander.view_loaded", name=name))
        if not res["views"]:
            self._msg(t("commander.no_views"), error=True)
            return
        self.menu(t("commander.menu.views"), [{"label": v["name"], "do": (lambda n=v["name"]: load(n))} for v in res["views"]])

    def a_recipe_save(self) -> None:
        default = str(Path(self.s.package) / "recipe.tui.json") if self.s.package else "recipe.json"
        self.ask_text(t("commander.prompt.recipe_path"), lambda path: self.call(
            {"call": "save-recipe", "path": path}, then=lambda r: self._msg(t("commander.recipe_saved", path=r.get("path"), steps=r.get("steps"))))
            if path.strip() else None, default=default)

    def a_export(self) -> None:
        def go(path: str) -> None:
            if not path.strip():
                return
            self.call({"call": "export", "package": path.strip()},
                      then=lambda r: self._msg(t("commander.exported", package=r.get("package"), hash=str(r.get("reproducible_hash"))[:23])),
                      background=True)
        self.ask_text(t("commander.prompt.package"), go, default=self.s.package or "")

    def a_macro_run(self) -> None:
        try:
            macros = self.ds.entries("macros")
        except Exception:  # noqa: BLE001 - no macros directory anywhere = empty menu
            log.debug("no macros", exc_info=True)
            macros = {}
        if not macros:
            self._msg(t("commander.no_macros"), error=True)
            return
        self.menu(t("commander.menu.macros"), [{"label": f"{_label(m.get('label')) or k} ({k})",
                                               "do": (lambda n=k: self.call({"call": "macro", "name": n},
                                                                            then=lambda r: self._msg(t("commander.macro_done", name=n, steps=r.get("steps"))),
                                                                            background=True))}
                                              for k, m in sorted(macros.items())])

    # ───────────────────────────── replay (``komander --odtworz``) ─────────────────────────────
    def load_replay(self, recipe: dict[str, Any], base: Optional[str] = None) -> None:
        """The recipe's header (sources, settings, id, views, bookmarks, tags) now; its steps one per Enter."""
        self.call({"call": "open", "recipe": dict(recipe, steps=[]), "base": base})
        self.replay = [s for s in recipe.get("steps") or [] if s.get("op") not in ("scan", "read", "merge", "write")]
        self.replay_pos = 0
        self._msg(t("commander.replay_ready", n=len(self.replay)))

    def a_replay_next(self) -> None:
        if self.replay_pos >= len(self.replay):
            self.replay = []
            self._msg(t("commander.replay_done"))
            return
        st = self.replay[self.replay_pos]
        self.replay_pos += 1
        spec = self.s.eng.ops_available().get(st["op"], {})
        self._msg(t("commander.replay_step", n=self.replay_pos, total=len(self.replay), label=_label(spec.get("label")) or st["op"]))
        self.call({"call": "op", "op": st["op"], "params": dict(st.get("params") or {}), "by": st.get("by") or "recipe",
                   "sources": st.get("params_source")})

    # ───────────────────────────── the frame ─────────────────────────────
    def resize(self, width: int, height: int) -> None:
        self.width, self.height = max(20, width), max(8, height)

    def list_height(self) -> int:
        return max(1, self.height - 5)

    def frame(self) -> list[list[tuple[str, str]]]:
        """The screen: title line, header line, rows, status line, bottom line (prompt, message or keys)."""
        self.refresh()
        w, h = self.width, self.height
        lh = self.list_height()
        left_w = w // 2
        widths = [left_w, w - left_w - 1]
        cols: list[list[list[tuple[str, str]]]] = []
        for i, (p, pw) in enumerate(zip(self.panes, widths)):
            active = i == self.active
            if p.cursor < p.top:
                p.top = p.cursor
            elif p.cursor >= p.top + lh:
                p.top = p.cursor - lh + 1
            lines: list[list[tuple[str, str]]] = []
            lines.append([(_fit(self._title(p), pw), "title" if active else "title_inactive")])
            lines.append([(_fit(self._header(p, pw), pw), "header")])
            rows = p.rows
            for j in range(p.top, p.top + lh):
                if j < len(rows):
                    r = rows[j]
                    attr = ("cursor" if active else "cursor_inactive") if j == p.cursor else ("selected" if r.get("sel") else "normal")
                    lines.append([(_fit(self._row_text(r, pw), pw), attr)])
                elif j == 0 and p.data is not None and p.data.get("hint"):
                    lines.append([(_fit("  " + p.data["hint"], pw), "dim")])
                else:
                    lines.append([(" " * pw, "normal")])
            cols.append(lines)
        out: list[list[tuple[str, str]]] = []
        bar = "|" if self.colors_ascii else "│"
        for a, b in zip(cols[0], cols[1]):
            out.append(a + [(bar, "border")] + b)
        if self.overlay is not None:
            out = self._overlay_lines(out)
        out.append([(_fit(self.status_line(), w), "status")])
        out.append(self._bottom_line(w))
        return out[:h]

    colors_ascii = False

    def text(self) -> str:
        return "\n".join("".join(seg for seg, _ in line).rstrip() for line in self.frame())

    def _title(self, p: Pane) -> str:
        roots = self.layout.get("root_labels") or {}
        crumbs = [c["label"] for c in ((p.data or {}).get("breadcrumbs") or [])] or [_label(roots.get(p.root)) or p.root]
        mode = f" [{p.flat}]" if p.flat else ""
        flt = f" /{p.filter}/" if p.filter else ""
        srt = f" ↕{p.sort}" if p.sort else ""
        return " " + " › ".join(crumbs) + mode + flt + srt + " "

    def _col_widths(self, kind: str, width: int) -> list[tuple[dict[str, Any], int]]:
        cols = (self.layout.get("columns") or {}).get(kind) or [{"id": "label"}]
        fixed = []
        for c in cols:
            wv = c.get("width", "auto")
            if isinstance(wv, str) and wv.endswith("%"):
                fixed.append(max(3, int(width * float(wv[:-1]) / 100)))
            elif isinstance(wv, int):
                fixed.append(wv)
            else:
                fixed.append(-1)
        # a narrow pane drops columns from the right before the label gets narrower than the preset
        min_label = int(self.layout.get("min_label", 24) or 0)
        keep = list(zip(cols, fixed))
        while True:
            rest = width - 3 - sum(x + 1 for _, x in keep if x > 0)
            n_auto = max(1, sum(1 for _, x in keep if x < 0))
            if rest // n_auto >= min_label or sum(1 for _, x in keep if x > 0) == 0:
                break
            idx = max(i for i, (_, x) in enumerate(keep) if x > 0)
            keep.pop(idx)
        auto = max(4, rest // n_auto)
        return [(c, auto if x < 0 else x) for c, x in keep]

    def _header(self, p: Pane, width: int) -> str:
        kind = p.flat or (p.rows[0]["kind"] if p.rows else None)
        if kind is None:
            return ""
        parts = []
        for c, cw in self._col_widths(kind, width):
            parts.append(_cell(_label(c.get("label")) or c["id"], cw, c.get("align")))
        return "   " + " ".join(parts)

    def _row_text(self, r: dict[str, Any], width: int) -> str:
        mark = {"all": "*", "part": "+"}.get(r.get("sel") or "", " ")
        flag = "#" if r.get("bookmark") else (">" if r.get("children") else " ")
        parts = []
        for c, cw in self._col_widths(r["kind"], width):
            v = r["label"] if c["id"] == "label" else r["cols"].get(c["id"], "")
            parts.append(_cell(v, cw, c.get("align")))
        tags = f" [{','.join(r['tags'])}]" if r.get("tags") else ""
        return f"{mark}{flag} " + " ".join(parts) + tags

    def status_line(self) -> str:
        st = self.status_data or {}
        p = self.pane
        out = []
        for el in self.layout.get("status") or []:
            if el == "view" and p.data is not None:
                out.append(t("commander.status.view", shown=p.data.get("shown", 0), total=p.data.get("total", 0), cursor=p.cursor + 1 if p.rows else 0))
            elif el == "selection" and st.get("selection"):
                out.append(t("commander.status.selection", **st["selection"]))
            elif el == "package" and st.get("package"):
                out.append(t("commander.status.package", entries=st["package"]["entries"], size=_human(st["package"]["bytes_est"])))
            elif el == "cost":
                c = st.get("cost")
                if c is None:
                    out.append(t("commander.status.cost_unknown"))
                elif not c.get("calls"):
                    out.append(t("commander.status.cost_zero"))
                else:
                    out.append(t("commander.status.cost", calls=c["calls"], amount=c.get("amount") if c.get("known") else "?", currency=c.get("currency", "")))
            elif el == "steps":
                out.append(t("commander.status.steps", steps=st.get("steps", 0), undo=st.get("undo", 0)))
            elif el == "job" and self.job is not None:
                pr = self.progress
                done = pr.get("done") or pr.get("bytes") or pr.get("units")
                out.append(t("commander.status.job", job=self.job, progress=f"{done}/{pr.get('total')}" if done and pr.get("total") else (done or "…")))
            elif el == "last" and st.get("last"):
                out.append(t("commander.status.last", call=st["last"].get("op") or st["last"].get("call")))
        if self.replay:
            out.append(t("commander.status.replay", n=self.replay_pos, total=len(self.replay)))
        return " " + " │ ".join(x for x in out if x)

    def _bottom_line(self, w: int) -> list[tuple[str, str]]:
        if self.mode == "prompt" and self.prompt is not None:
            return [(_fit(f"{self.prompt['label']}: {self.prompt['buffer']}_", w), "prompt")]
        if self.message:
            return [(_fit(" " + self.message, w), "error" if self.message_error else "normal")]
        segs: list[tuple[str, str]] = []
        for i in range(1, 11):
            act = self.km.bindings.get(f"F{i}")
            if act:
                segs.append((f"{i}", "normal"))
                segs.append((_fit(t("commander.short." + act), max(1, w // 10 - len(str(i)) - 1)) + " ", "status"))
        used = sum(len(s) for s, _ in segs)
        if used < w:
            segs.append((" " * (w - used), "normal"))
        return segs

    def _overlay_lines(self, base: list[list[tuple[str, str]]]) -> list[list[tuple[str, str]]]:
        o = self.overlay
        assert o is not None
        w = self.width
        title = o["title"]
        if self.mode == "menu":
            items = self._menu_items()
            body = [("› " if i == o["cursor"] else "  ") + str(it["label"]) for i, it in enumerate(items)]
            if o.get("filter"):
                title += f"  /{o['filter']}/"
            top = max(0, o["cursor"] - len(base) + 3)
        else:
            body = o["lines"]
            top = o.get("top", 0)
        out = [[(_fit(" " + title + " ", w), "overlay_title")]]
        for i in range(top, top + len(base) - 1):
            attr = "cursor" if self.mode == "menu" and i == o.get("cursor") else "overlay"
            out.append([(_fit(" " + _clean(body[i]) if i < len(body) else "", w), attr)])
        return out


# ───────────────────────────── text helpers ─────────────────────────────
def _clean(s: Any) -> str:
    return re.sub(r"[\x00-\x08\x0b-\x1f\x7f]", " ", str(s)).replace("\t", "    ")


def _fit(s: str, w: int) -> str:
    s = _clean(s).replace("\n", " ")
    return s[:w] if len(s) >= w else s + " " * (w - len(s))


def _cell(v: Any, w: int, align: Optional[str]) -> str:
    s = _clean(v).replace("\n", " ")
    if len(s) > w:
        s = s[: max(0, w - 1)] + "…" if w > 1 else s[:w]
    return s.rjust(w) if align == "right" else s.ljust(w)


def _human(n: Any) -> str:
    try:
        n = float(n)
    except (TypeError, ValueError):
        return "?"
    for unit in ("B", "kB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} GB"


def _folder(f: str) -> str:
    f = "/" + "/".join(x for x in f.strip().split("/") if x)
    return f


def _parse_value(text: str, typ: str) -> Any:
    """A prompt's text → a parameter value of the edit's declared type."""
    text = text.strip()
    if typ in ("number",):
        return float(text) if "." in text else int(text)
    if typ == "bool":
        return text.lower() in ("1", "t", "tak", "true", "yes", "y")
    if typ == "list":
        if text.startswith("["):
            return json.loads(text)
        return [x.strip() for x in text.split(",") if x.strip()]
    if typ == "object":
        if text.startswith("{"):
            return json.loads(text)
        out: dict[str, Any] = {}
        for pair in text.split(","):
            if "=" in pair:
                k, v = pair.split("=", 1)
                out[k.strip()] = v.strip()
            elif pair.strip():
                raise ValueError(t("commander.bad_pairs", text=pair))
        return out
    if typ == "any":
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return text
    return text


__all__ = ["CommanderState", "Pane", "parse_keys", "is_key_name", "KEY_NAMES"]
