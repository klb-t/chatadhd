"""Serve face: the same dispatcher over HTTP on the loopback (``serwuj``).

``ThreadingHTTPServer`` from the standard library, bound to ``127.0.0.1`` (preset ``serve.address``;
any other address is an explicit setting and is logged as a warning) on port ``serve.port`` (``0`` =
any free port).  A random token per run is printed on stderr and, with a package, written to
``<package>/serve.token`` (mode 600); every request carries it in ``X-Extract-Token`` (the event
stream also accepts ``?token=`` because a browser's EventSource cannot set headers).  No CORS
headers: only same-origin pages and local scripts can call it.  Zero outgoing network, as everywhere:
model steps still need explicit enabling in the request and pass the estimate and the guard.

Endpoints: ``GET /api/ops`` · ``GET /api/calls`` · ``POST /api/op`` ``{op, params, wait?}`` ·
``POST /api/call`` ``{call, …}`` (any call of ``data/api.json``) · ``GET /api/jobs/{id}`` ·
``GET /api/events?after=N&job=ID`` (Server-Sent Events: progress, question, done, error, cancelled) ·
``POST /api/answer`` · ``GET /api/view?root=&path=&flat=&filter=&sort=&offset=&limit=`` (exactly the
rows a commander pane draws) · ``GET /api/status`` · ``GET /api/recipe`` · ``POST /api/recipe/run`` ·
``GET /api/package/manifest`` · ``GET /api/i18n`` · ``GET /api/keymap`` · ``GET /api/layout``.
"""
import hmac
import json
import logging
import os
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Optional
from urllib.parse import parse_qs, urlparse

from .api import ApiError, Session, error_body, jsonable
from .i18n import t

log = logging.getLogger(__name__)
LOOPBACK = ("127.0.0.1", "::1", "localhost")


class _Http(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class Server:
    def __init__(self, session: Session, *, address: Optional[str] = None, port: Optional[int] = None,
                 token: Optional[str] = None, token_file: Optional[Path] = None) -> None:
        ds = session.ds
        self.session = session
        self.address = address or str(ds.preset("serve.address", "127.0.0.1"))
        self.port = int(ds.preset("serve.port", 8741) if port is None else port)
        if self.address not in LOOPBACK:
            log.warning("serve: listening on %s, not the loopback — anyone who can reach it and has the token can use the tool", self.address)
        self.token = token or secrets.token_urlsafe(24)
        self.requests = 0
        self.stopping = threading.Event()
        self.httpd = _Http((self.address, self.port), _handler(self))
        self.port = self.httpd.server_address[1]
        self.token_file = token_file
        if token_file is not None:
            token_file.parent.mkdir(parents=True, exist_ok=True)
            fd = os.open(str(token_file), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, "w", encoding="utf-8") as fh:
                fh.write(self.token + "\n")
            try:
                os.chmod(token_file, 0o600)
            except OSError:
                log.debug("chmod of the token file failed", exc_info=True)
        self.thread: Optional[threading.Thread] = None

    @property
    def url(self) -> str:
        host = f"[{self.address}]" if ":" in self.address else self.address
        return f"http://{host}:{self.port}"

    def start(self) -> "Server":
        self.thread = threading.Thread(target=self.httpd.serve_forever, kwargs={"poll_interval": 0.1}, name="xport-serve", daemon=True)
        self.thread.start()
        return self

    def serve_forever(self) -> None:
        self.httpd.serve_forever(poll_interval=0.2)

    def stop(self) -> None:
        self.stopping.set()
        with self.session.cond:
            self.session.cond.notify_all()
        self.httpd.shutdown()
        self.httpd.server_close()
        if self.thread is not None:
            self.thread.join(timeout=10)


def _handler(server: Server) -> type:
    session = server.session

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "loom-extract-serve"

        def log_message(self, fmt: str, *args: Any) -> None:  # the request log goes to logging, not stderr
            log.info("serve %s - %s", self.address_string(), fmt % args)

        # -- plumbing ------------------------------------------------------------------
        def _send(self, status: int, body: Any) -> None:
            data = json.dumps(jsonable(body), ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def _authorized(self, query: dict[str, list[str]], allow_query: bool = False) -> bool:
            got = self.headers.get("X-Extract-Token") or (query.get("token", [""])[0] if allow_query else "")
            try:  # bytes: a non-ASCII header is a wrong token (401), not a TypeError that drops the connection
                return bool(got) and hmac.compare_digest(str(got).encode("utf-8", "surrogateescape"), server.token.encode("utf-8"))
            except (TypeError, ValueError):
                log.debug("token comparison failed", exc_info=True)
                return False

        def _body(self) -> dict[str, Any]:
            n = _int(self.headers.get("Content-Length") or 0, "Content-Length")
            if n < 0:
                raise ApiError(t("serve.bad_request", what="Content-Length", value=str(n)), "bad_request")
            if not n:
                return {}
            raw = self.rfile.read(n)
            try:
                doc = json.loads(raw.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise ApiError(t("serve.bad_json", error=str(exc)), "bad_json") from exc
            if not isinstance(doc, dict):
                raise ApiError(t("serve.bad_json", error="not an object"), "bad_json")
            return doc

        def _call(self, req: dict[str, Any]) -> None:
            try:
                res = session.call(req)
            except ApiError as exc:
                self._send(exc.status, {"error": str(exc), "code": exc.code})
                return
            except Exception as exc:  # noqa: BLE001 - every failure answers with its reason
                log.debug("call failed", exc_info=True)
                body = error_body(exc)
                self._send(409 if body.get("code") == "refused" else 400, body)
                return
            self._send(409 if res.get("busy") else 202 if isinstance(res.get("job"), dict) and req.get("wait") is False else 200, res)

        def _route(self, method: str) -> None:
            server.requests += 1
            u = urlparse(self.path)
            q = parse_qs(u.query)
            path = u.path.rstrip("/") or "/"
            try:
                if not self._authorized(q, allow_query=(path == "/api/events")):
                    self._send(401, {"error": t("serve.unauthorized"), "code": "unauthorized"})
                    return
                if method == "GET":
                    self._get(path, q)
                else:
                    self._post(path, self._body())
            except ApiError as exc:
                self._send(exc.status, {"error": str(exc), "code": exc.code})
            except (BrokenPipeError, ConnectionResetError):
                log.debug("client went away during %s", path)
            except Exception as exc:  # noqa: BLE001 - every request gets an answer, never a dropped connection
                log.warning("serve %s %s failed: %s", method, path, exc)
                log.debug("serve failure", exc_info=True)
                try:
                    self._send(500, error_body(exc))
                except OSError:
                    log.debug("could not answer the failed request", exc_info=True)

        def do_GET(self) -> None:  # noqa: N802 - http.server's naming
            self._route("GET")

        def do_POST(self) -> None:  # noqa: N802
            self._route("POST")

        # -- routes ----------------------------------------------------------------------
        def _get(self, path: str, q: dict[str, list[str]]) -> None:
            one = lambda k, d=None: (q.get(k) or [d])[0]  # noqa: E731
            simple = {"/api/ops": "ops", "/api/calls": "calls", "/api/status": "status", "/api/recipe": "recipe",
                      "/api/package/manifest": "manifest", "/api/i18n": "i18n", "/api/keymap": "keymap", "/api/layout": "layout",
                      "/api/selection": "selection"}
            if path in simple:
                req: dict[str, Any] = {"call": simple[path]}
                for k in ("lang", "profile", "package", "limit"):
                    if one(k):
                        req[k] = one(k)
                self._call(req)
            elif path.startswith("/api/jobs/"):
                self._call({"call": "jobs", "id": path.rsplit("/", 1)[1]})
            elif path == "/api/view":
                req = {"call": "list", "root": one("root", "sources"), "path": q.get("path") or [], "flat": one("flat"),
                       "filter": one("filter", ""), "sort": one("sort", ""), "offset": _int(one("offset", 0) or 0, "offset"),
                       "limit": _int(one("limit", 0) or 0, "limit")}
                self._call(req)
            elif path == "/api/events":
                self._events(_int(one("after", 0) or 0, "after"), one("job"))
            else:
                self._send(404, {"error": t("serve.not_found", path=path), "code": "not_found"})

        def _post(self, path: str, body: dict[str, Any]) -> None:
            if path == "/api/op":
                self._call(dict(body, call="op"))
            elif path == "/api/call":
                self._call(body)
            elif path == "/api/answer":
                self._call(dict(body, call="answer"))
            elif path == "/api/recipe/run":
                req = {"call": "open", "recipe": body.get("recipe"), "wait": body.get("wait", True)}
                if body.get("package"):
                    req["write"] = body["package"]
                self._call(req)
            else:
                self._send(404, {"error": t("serve.not_found", path=path), "code": "not_found"})

        def _events(self, after: int, job: Optional[str]) -> None:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            seq = after
            beat = float(session.ds.preset("serve.heartbeat_s", 15) or 0)
            last = time.time()
            while not server.stopping.is_set():
                evs = session.events_after(seq)
                for ev in evs:
                    seq = ev["seq"]
                    data = json.dumps(ev, ensure_ascii=False)
                    self.wfile.write(f"id: {seq}\nevent: {ev['kind']}\ndata: {data}\n\n".encode("utf-8"))
                    self.wfile.flush()
                    last = time.time()
                    if job and ev.get("job") == job and ev["kind"] in ("done", "error", "cancelled"):
                        return
                if job and job in session.jobs and session.jobs[job].state in ("done", "error", "cancelled") and not evs:
                    return  # the job ended before the stream started and its last event is older than ``after``
                if not evs:
                    if beat and time.time() - last > beat:
                        self.wfile.write(b": keep-alive\n\n")
                        self.wfile.flush()
                        last = time.time()
                    with session.cond:
                        session.cond.wait(timeout=0.25)

    return Handler


def _int(value: Any, what: str) -> int:
    """A number from a header or the query; anything else is a 400 with the parameter's name."""
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        raise ApiError(t("serve.bad_request", what=what, value=str(value)[:40]), "bad_request") from None


__all__ = ["Server", "LOOPBACK"]
