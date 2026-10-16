"""CLI verbs for the faces of slice C: ``komander`` (the two-pane TUI, or headless with a key
script), ``serwuj`` (the HTTP API on the loopback) and ``api`` (calls of the same dispatcher from
the command line or a JSONL file — the scriptable twin of the commander and the server)."""
import argparse
import json
import logging
import sys
from pathlib import Path
from typing import Any, Optional

from .api import ApiError, Session, error_body
from .i18n import t
from .paths import overlay_dir

log = logging.getLogger(__name__)


def _session(args: argparse.Namespace, face: str) -> Session:
    return Session(face=face, aggregate=bool(args.aggregate), package=getattr(args, "pakiet", None))


def autosave_path(args: argparse.Namespace, ds: Any) -> Optional[Path]:
    """``--autozapis`` → the package → the user's overlay → the current directory (preset file name)."""
    if getattr(args, "autozapis", None):
        return None if args.autozapis == "off" else Path(args.autozapis).expanduser()
    if int(ds.preset("commander.autosave.every", 1) or 0) == 0:
        return None
    name = str(ds.preset("commander.autosave.file", "komander.autosave.json"))
    if getattr(args, "pakiet", None):
        return Path(args.pakiet).expanduser() / name
    base = overlay_dir()
    return (base / "commander" / name) if base is not None else Path.cwd() / name


def verb_commander(args: argparse.Namespace) -> int:
    from .commander.driver import Driver
    from .commander.state import CommanderState
    sess = _session(args, "tui")
    try:
        path = autosave_path(args, sess.ds)
        state = CommanderState(sess, profile=args.profil_klawiszy, autosave=path, background=not args.bez_ekranu,
                               width=args.szerokosc or 120, height=args.wysokosc or 32)
        found = state.autosave.load() if state.autosave is not None else None
        resume_pref = "yes" if args.wznow else "no" if args.nie_wznawiaj else str(sess.ds.preset("commander.autosave.resume", "ask"))
        if found is not None and resume_pref == "yes":
            state.resume(found)
        elif found is not None:
            prev = state.autosave.path.with_name(state.autosave.path.stem + ".prev.json")
            state.autosave.path.replace(prev)  # nothing is lost: the earlier session stays next to the new one
            note = t("commander.autosave_kept", path=str(prev), steps=len((found.get("recipe") or {}).get("steps") or []))
            if resume_pref == "ask" and not args.bez_ekranu:
                state._question(note + " " + t("commander.resume_q"), None,
                                lambda yes, d=found, p=prev: (state.resume(d), p.unlink()) if yes else None)
            else:
                print(note, file=sys.stderr)
        if args.odtworz:
            from .recipe import load_recipe
            state.load_replay(load_recipe(args.odtworz), base=str(Path(args.odtworz).expanduser().resolve().parent))
        elif args.zrodla:
            state.call({"call": "open", "sources": list(args.zrodla)}, background=not args.bez_ekranu)
        if args.bez_ekranu:
            drv = Driver(state)
            drv.settle()
            if args.klawisze:
                drv.script(args.klawisze)
            state.save_now()
            if args.json:
                print(json.dumps({"screen": drv.screen().splitlines(), "status": sess.call({"call": "status"}),
                                  "recipe": sess.eng.normalised_recipe(), "message": state.message}, ensure_ascii=False, indent=1, default=str))
            else:
                print(drv.screen())
            return 1 if state.message_error else 0
        try:
            from .commander import view_curses
        except ImportError as exc:
            print(t("commander.no_curses", reason=str(exc)), file=sys.stderr)
            return 2
        return view_curses.run(state, colors=args.kolory)
    finally:
        sess.close()


def verb_serve(args: argparse.Namespace) -> int:
    from .serve import Server
    sess = _session(args, "api")
    try:
        if args.zrodla:
            sess.call({"call": "open", "sources": list(args.zrodla)})
        token_file = Path(args.pakiet).expanduser() / str(sess.ds.preset("serve.token_file", "serve.token")) if args.pakiet else None
        srv = Server(sess, address=args.adres, port=args.port, token_file=token_file)
        print(t("serve.started", url=srv.url), file=sys.stderr)
        print(t("serve.token", token=srv.token) + (f" ({token_file})" if token_file else ""), file=sys.stderr)
        sys.stderr.flush()
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            print(t("serve.stopped"), file=sys.stderr)
        finally:
            srv.httpd.server_close()
        return 0
    finally:
        sess.close()


def verb_api(args: argparse.Namespace) -> int:
    """Calls as JSON objects (arguments, a JSONL file or stdin with ``-``) → one JSON result per line."""
    reqs: list[dict[str, Any]] = []
    for a in args.wywolania or []:
        reqs.append(json.loads(a))
    if args.plik:
        fh = sys.stdin if args.plik == "-" else open(args.plik, encoding="utf-8")
        with fh:
            for line in fh:
                if line.strip() and not line.lstrip().startswith("#"):
                    reqs.append(json.loads(line))
    sess = _session(args, "cli")
    code = 0
    try:
        if args.zrodla:
            sess.call({"call": "open", "sources": list(args.zrodla)})
        for req in reqs:
            try:
                res = sess.call(dict(req, wait=True) if req.get("wait") is False else req)
            except ApiError as exc:
                res, code = {"error": str(exc), "code": exc.code}, 1
            except Exception as exc:  # noqa: BLE001 - a failing call is reported and the next one runs
                log.debug("api call failed", exc_info=True)
                res, code = error_body(exc), 1
            print(json.dumps(res, ensure_ascii=False, default=str))
        return code
    finally:
        sess.close()
