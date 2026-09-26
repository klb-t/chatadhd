"""
engine/live_debug.py
=====================

Live debug server — minimalny HTTP na localhost dla oglądania logów i stanu
aplikacji w czasie rzeczywistym bez blokowania Kivy main loop.

Użycie (w main.py build, jak najwcześniej):

    from engine.live_debug import start_debug_server
    self._debug = start_debug_server(app=self, port=8765)

Potem w przeglądarce: http://localhost:8765

Endpointy:
    /           — HTML live view z auto-refresh (logi + stan)
    /logs       — plain text, ostatnie N wpisów (query: ?n=500)
    /logs/tail  — plain text, tylko od ostatniego id (query: ?since=123)
    /state      — JSON dump stanu (panele, jobs, video pipeline)
    /error      — ostatni unhandled exception (jeśli jakiś był)

Projekt (fundamentalne zasady):
  - Stdlib only. Żadnych nowych deps.
  - Thread-safe log buffer (deque + Lock).
  - Daemon threads — nie blokują zamknięcia apki.
  - Łapie też Kivy log i root logger przez custom Handler.
  - Bezpieczny: binduje się tylko na 127.0.0.1, więc nic nie wycieka z urządzenia.
  - Fail-open: błąd w serwerze nie wywala apki (wszystko w try/except).
  - Niskokosztowy: jeśli nikt nie połączony, tylko jeden idle thread.
"""

from __future__ import annotations

import json
import logging
import queue
import sys
import threading
import time
import traceback
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Callable, Deque, Dict, List, Optional
from urllib.parse import parse_qs, urlparse

log = logging.getLogger(__name__)


# ─── Ring buffer for log entries ────────────────────────────────

class _LogBuffer:
    """Thread-safe ring buffer. Każdy wpis ma unikalne id (monotonically increasing)."""

    def __init__(self, capacity: int = 4000) -> None:
        self._buf: Deque[Dict[str, Any]] = deque(maxlen=capacity)
        self._lock = threading.Lock()
        self._next_id = 1

    def append(self, level: str, name: str, message: str) -> None:
        with self._lock:
            entry = {
                "id": self._next_id,
                "t": time.time(),
                "level": level,
                "name": name,
                "msg": message,
            }
            self._buf.append(entry)
            self._next_id += 1

    def tail(self, n: int = 500) -> List[Dict[str, Any]]:
        with self._lock:
            if n >= len(self._buf):
                return list(self._buf)
            return list(self._buf)[-n:]

    def since(self, last_id: int) -> List[Dict[str, Any]]:
        with self._lock:
            return [e for e in self._buf if e["id"] > last_id]

    def max_id(self) -> int:
        with self._lock:
            return self._next_id - 1


# ─── Logging handler capturing everything ───────────────────────

class _BufferHandler(logging.Handler):
    def __init__(self, buffer: _LogBuffer) -> None:
        super().__init__(level=logging.DEBUG)
        self._buf = buffer

    def emit(self, record: logging.LogRecord) -> None:
        try:
            msg = self.format(record)
            self._buf.append(record.levelname, record.name, msg)
        except Exception:
            # Never raise from a log handler.
            pass


# ─── Stderr tee — capture tracebacks that don't go through logging ──

class _StderrTee:
    """Write-through: forwards to real stderr AND to buffer as WARN."""

    def __init__(self, real_stderr, buffer: _LogBuffer) -> None:
        self._real = real_stderr
        self._buf = buffer
        self._partial = ""

    def write(self, s: str) -> int:
        try:
            self._real.write(s)
        except Exception:
            pass
        # Line-buffer so we don't spam with single characters.
        self._partial += s
        while "\n" in self._partial:
            line, self._partial = self._partial.split("\n", 1)
            line = line.rstrip()
            if line:
                self._buf.append("STDERR", "stderr", line)
        return len(s)

    def flush(self) -> None:
        try:
            self._real.flush()
        except Exception:
            pass
        if self._partial:
            self._buf.append("STDERR", "stderr", self._partial.rstrip())
            self._partial = ""

    def __getattr__(self, name):
        return getattr(self._real, name)


# ─── HTTP handler ───────────────────────────────────────────────

class _DebugHTTPHandler(BaseHTTPRequestHandler):
    # Set from start_debug_server:
    buffer: _LogBuffer = None  # type: ignore
    app_ref: Any = None
    last_error: Optional[Dict[str, Any]] = None

    # Silence default access logs — they spam our own debug stream.
    def log_message(self, format, *args):  # noqa: A002
        pass

    def _send(self, status: int, body: bytes, content_type: str) -> None:
        try:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Client disconnected, fine.

    def do_GET(self):  # noqa: N802
        try:
            parsed = urlparse(self.path)
            q = parse_qs(parsed.query)

            if parsed.path in ("/", "/index.html"):
                self._send(200, _HTML.encode("utf-8"), "text/html; charset=utf-8")
                return

            if parsed.path == "/logs":
                n = int(q.get("n", ["500"])[0])
                entries = _DebugHTTPHandler.buffer.tail(n)
                body = _format_entries_as_text(entries).encode("utf-8")
                self._send(200, body, "text/plain; charset=utf-8")
                return

            if parsed.path == "/logs/tail":
                since = int(q.get("since", ["0"])[0])
                entries = _DebugHTTPHandler.buffer.since(since)
                payload = {
                    "entries": entries,
                    "max_id": _DebugHTTPHandler.buffer.max_id(),
                }
                body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
                self._send(200, body, "application/json; charset=utf-8")
                return

            if parsed.path == "/state":
                state = _dump_app_state(_DebugHTTPHandler.app_ref)
                body = json.dumps(state, ensure_ascii=False,
                                   indent=2, default=str).encode("utf-8")
                self._send(200, body, "application/json; charset=utf-8")
                return

            if parsed.path == "/error":
                body = json.dumps(_DebugHTTPHandler.last_error or {},
                                    ensure_ascii=False,
                                    indent=2).encode("utf-8")
                self._send(200, body, "application/json; charset=utf-8")
                return

            self._send(404, b"not found", "text/plain")

        except Exception as e:
            # Last-resort: do not propagate.
            try:
                body = f"debug server error: {e}".encode("utf-8")
                self._send(500, body, "text/plain")
            except Exception:
                pass


def _format_entries_as_text(entries: List[Dict[str, Any]]) -> str:
    out = []
    for e in entries:
        ts = time.strftime("%H:%M:%S", time.localtime(e["t"]))
        out.append(f"{ts} [{e['level']:5s}] {e['name']}: {e['msg']}")
    return "\n".join(out) + "\n"


def _dump_app_state(app: Any) -> Dict[str, Any]:
    """Best-effort introspekcja apki. Cokolwiek zepsute — skip."""
    state: Dict[str, Any] = {"timestamp": time.time()}

    if app is None:
        state["note"] = "no app reference"
        return state

    # Panele
    try:
        pm = getattr(app, "panel_manager", None)
        if pm:
            panels = []
            for inst in pm.list_all():
                try:
                    panels.append({
                        "panel_id": inst.panel.panel_id,
                        "panel_type": getattr(inst.panel, "panel_type", "?"),
                        "mount_mode": (inst.mount.mount_mode.value
                                         if inst.mount else None),
                        "visible": not (inst.mount
                                         and inst.mount.mount_mode.value == "hidden"),
                    })
                except Exception as e:
                    panels.append({"error": str(e)})
            state["panels"] = panels
    except Exception as e:
        state["panels_error"] = str(e)

    # Async jobs
    try:
        jm = getattr(app, "job_manager", None)
        if jm:
            active = jm.list_active()
            state["jobs_active"] = [
                {
                    "job_id": j.job_id,
                    "kind": j.job_kind,
                    "status": j.status.value,
                    "elapsed_s": round(j.elapsed(), 1),
                    "poll_count": j.poll_count,
                    "context": j.context,
                } for j in active
            ]
    except Exception as e:
        state["jobs_error"] = str(e)

    # Video pipeline
    try:
        vp = getattr(app, "video_pipeline", None)
        if vp:
            state["video_pipeline"] = "active"
        else:
            state["video_pipeline"] = "not initialized (no API key?)"
    except Exception:
        pass

    # Config
    try:
        cfg = getattr(app, "cfg", None)
        if cfg:
            state["data_dir"] = str(getattr(app, "data_dir", "?"))
            state["config_keys"] = sorted(list(cfg._data.keys())
                                             if hasattr(cfg, "_data") else [])
    except Exception:
        pass

    # Current conversation
    try:
        engine = getattr(app, "engine", None)
        if engine and getattr(engine, "conv", None):
            state["current_conv"] = {
                "id": engine.conv.get("id"),
                "title": engine.conv.get("title", "?")[:80],
            }
    except Exception:
        pass

    # Kivy root info
    try:
        from kivy.core.window import Window
        state["window"] = {
            "size": list(Window.size),
            "dpi": getattr(Window, "dpi", None),
        }
    except Exception:
        pass

    return state


# ─── Public API ─────────────────────────────────────────────────

class DebugServer:
    """Uchwyt do live debug serwera. Zawiera referencje do buffer'a i wątku."""

    def __init__(self, httpd: ThreadingHTTPServer, thread: threading.Thread,
                 buffer: _LogBuffer, port: int) -> None:
        self.httpd = httpd
        self.thread = thread
        self.buffer = buffer
        self.port = port

    def stop(self, timeout: float = 2.0) -> None:
        try:
            self.httpd.shutdown()
        except Exception:
            pass
        self.thread.join(timeout=timeout)

    def log(self, message: str, level: str = "DEBUG") -> None:
        """Wygodny direct write (gdy nie chcesz iść przez logging)."""
        self.buffer.append(level, "direct", message)


def start_debug_server(app: Any = None, *,
                         port: int = 8765,
                         capacity: int = 4000,
                         capture_stderr: bool = True,
                         install_excepthook: bool = True,
                         log_format: str = "%(message)s",
                         ) -> Optional[DebugServer]:
    """
    Uruchamia debug HTTP server w wątku tła.

    Zwraca DebugServer albo None gdy nie udało się bindować portu
    (już zajęty — np. restart apki bez clean-upu).
    """
    buffer = _LogBuffer(capacity=capacity)

    # Install logging handler
    handler = _BufferHandler(buffer)
    handler.setFormatter(logging.Formatter(log_format))
    root = logging.getLogger()
    root.addHandler(handler)
    # Not changing root level — respect user settings.

    # Tee stderr (Kivy often writes there)
    if capture_stderr:
        sys.stderr = _StderrTee(sys.stderr, buffer)

    # Install excepthook to grab uncaught exceptions
    if install_excepthook:
        prev_hook = sys.excepthook

        def _hook(exc_type, exc_value, tb):
            try:
                tb_text = "".join(traceback.format_exception(exc_type, exc_value, tb))
                buffer.append("CRIT", "uncaught", tb_text)
                _DebugHTTPHandler.last_error = {
                    "t": time.time(),
                    "type": exc_type.__name__,
                    "value": str(exc_value),
                    "traceback": tb_text,
                }
            except Exception:
                pass
            prev_hook(exc_type, exc_value, tb)

        sys.excepthook = _hook

        # threading.excepthook (3.8+) for background threads
        try:
            prev_thread_hook = threading.excepthook

            def _thread_hook(args):
                try:
                    tb_text = "".join(traceback.format_exception(
                        args.exc_type, args.exc_value, args.exc_traceback))
                    buffer.append("CRIT", f"thread:{args.thread.name}", tb_text)
                    _DebugHTTPHandler.last_error = {
                        "t": time.time(),
                        "type": args.exc_type.__name__,
                        "value": str(args.exc_value),
                        "thread": args.thread.name,
                        "traceback": tb_text,
                    }
                except Exception:
                    pass
                prev_thread_hook(args)
            threading.excepthook = _thread_hook
        except Exception:
            pass

    # Configure handler class with references
    _DebugHTTPHandler.buffer = buffer
    _DebugHTTPHandler.app_ref = app
    _DebugHTTPHandler.last_error = None

    # Try to bind — localhost only for safety.
    try:
        httpd = ThreadingHTTPServer(("127.0.0.1", port), _DebugHTTPHandler)
    except OSError as e:
        # Try a few alternate ports — if apka restartuje, poprzedni port
        # może jeszcze wisieć w TIME_WAIT.
        for alt in range(port + 1, port + 6):
            try:
                httpd = ThreadingHTTPServer(("127.0.0.1", alt), _DebugHTTPHandler)
                port = alt
                break
            except OSError:
                continue
        else:
            log.warning("Debug server: could not bind to %d or alt ports: %s",
                         port, e)
            return None

    thread = threading.Thread(
        target=httpd.serve_forever,
        name="LiveDebugHTTP",
        daemon=True,
    )
    thread.start()

    # Pierwszy wpis informujący że serwer działa.
    buffer.append("INFO", "live_debug",
                  f"Debug server running at http://127.0.0.1:{port}/")

    log.info("Live debug server: http://127.0.0.1:%d/", port)

    return DebugServer(httpd, thread, buffer, port)


# ─── Embedded HTML with JS live-tail ────────────────────────────

_HTML = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>ChatADHD Live Debug</title>
<style>
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    font-size: 12px;
    background: #0d1117;
    color: #c9d1d9;
    height: 100vh;
    display: flex;
    flex-direction: column;
  }
  header {
    padding: 8px 12px;
    background: #161b22;
    border-bottom: 1px solid #30363d;
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
  }
  header h1 {
    margin: 0;
    font-size: 14px;
    font-weight: 600;
  }
  header .status {
    color: #7ee787;
    font-size: 11px;
  }
  header .status.paused { color: #ff7b72; }
  header .controls { display: flex; gap: 6px; margin-left: auto; }
  button, input[type=text] {
    background: #21262d;
    border: 1px solid #30363d;
    color: #c9d1d9;
    padding: 4px 10px;
    border-radius: 4px;
    font: inherit;
    cursor: pointer;
  }
  button:hover { background: #30363d; }
  button.active { background: #1f6feb; color: #fff; }
  main {
    flex: 1;
    display: flex;
    overflow: hidden;
  }
  #logs {
    flex: 2;
    overflow-y: auto;
    padding: 6px 10px;
    white-space: pre-wrap;
    word-break: break-word;
  }
  #state {
    flex: 1;
    overflow-y: auto;
    padding: 6px 10px;
    border-left: 1px solid #30363d;
    background: #010409;
    white-space: pre-wrap;
    font-size: 11px;
  }
  .log-entry { padding: 1px 0; line-height: 1.4; }
  .log-entry .ts { color: #6e7681; }
  .log-entry .name { color: #a5a5ff; }
  .log-entry.CRIT    { color: #ff7b72; font-weight: bold; }
  .log-entry.STDERR  { color: #ffa657; }
  .log-entry.ERROR   { color: #ff7b72; }
  .log-entry.WARN    { color: #d29922; }
  .log-entry.INFO    { color: #c9d1d9; }
  .log-entry.DEBUG   { color: #8b949e; }
  .log-entry.hl      { background: #2d333b; }
  .log-entry pre     { margin: 0; padding-left: 1em; color: inherit; }
  .section-title {
    font-size: 11px;
    color: #7ee787;
    text-transform: uppercase;
    margin: 10px 0 4px 0;
    letter-spacing: 0.05em;
  }
  .section-title:first-child { margin-top: 0; }
</style>
</head>
<body>
<header>
  <h1>ChatADHD Live Debug</h1>
  <span id="status" class="status">connecting...</span>
  <div class="controls">
    <input type="text" id="filter" placeholder="filter text..." size="20">
    <button id="pause">Pause</button>
    <button id="clear">Clear</button>
    <button id="bottom" class="active">Auto-scroll</button>
  </div>
</header>
<main>
  <div id="logs"></div>
  <div id="state"></div>
</main>
<script>
const logsEl = document.getElementById('logs');
const stateEl = document.getElementById('state');
const statusEl = document.getElementById('status');
const pauseBtn = document.getElementById('pause');
const clearBtn = document.getElementById('clear');
const bottomBtn = document.getElementById('bottom');
const filterInput = document.getElementById('filter');

let lastId = 0;
let paused = false;
let autoScroll = true;
let filterText = '';

pauseBtn.onclick = () => {
  paused = !paused;
  pauseBtn.textContent = paused ? 'Resume' : 'Pause';
  pauseBtn.classList.toggle('active', paused);
};
clearBtn.onclick = () => { logsEl.innerHTML = ''; };
bottomBtn.onclick = () => {
  autoScroll = !autoScroll;
  bottomBtn.classList.toggle('active', autoScroll);
  if (autoScroll) logsEl.scrollTop = logsEl.scrollHeight;
};
filterInput.oninput = () => { filterText = filterInput.value.toLowerCase(); applyFilter(); };
logsEl.addEventListener('scroll', () => {
  const atBottom = logsEl.scrollHeight - logsEl.scrollTop - logsEl.clientHeight < 30;
  if (!atBottom && autoScroll) {
    autoScroll = false;
    bottomBtn.classList.remove('active');
  }
});

function applyFilter() {
  for (const el of logsEl.children) {
    if (!filterText || el.dataset.text.toLowerCase().includes(filterText)) {
      el.style.display = '';
    } else {
      el.style.display = 'none';
    }
  }
}

function tsFmt(t) {
  const d = new Date(t * 1000);
  return d.toTimeString().slice(0, 8);
}

async function tick() {
  try {
    const [logs, state] = await Promise.all([
      fetch('/logs/tail?since=' + lastId).then(r => r.json()),
      fetch('/state').then(r => r.json()),
    ]);

    statusEl.textContent = 'connected • ' + logs.max_id + ' entries';
    statusEl.classList.remove('paused');

    if (!paused && logs.entries.length) {
      const frag = document.createDocumentFragment();
      for (const e of logs.entries) {
        const div = document.createElement('div');
        div.className = 'log-entry ' + e.level;
        const text = `${tsFmt(e.t)} [${e.level}] ${e.name}: ${e.msg}`;
        div.dataset.text = text;
        const isMulti = e.msg.includes('\n');
        if (isMulti) {
          div.innerHTML = `<span class="ts">${tsFmt(e.t)}</span> [${e.level}] <span class="name">${e.name}</span>:<pre>${escapeHTML(e.msg)}</pre>`;
        } else {
          div.innerHTML = `<span class="ts">${tsFmt(e.t)}</span> [${e.level}] <span class="name">${e.name}</span>: ${escapeHTML(e.msg)}`;
        }
        if (filterText && !text.toLowerCase().includes(filterText)) {
          div.style.display = 'none';
        }
        frag.appendChild(div);
      }
      logsEl.appendChild(frag);
      lastId = logs.max_id;
      // Trim DOM if too many
      while (logsEl.children.length > 3000) {
        logsEl.removeChild(logsEl.firstChild);
      }
      if (autoScroll) logsEl.scrollTop = logsEl.scrollHeight;
    } else {
      lastId = logs.max_id;
    }

    stateEl.innerHTML = '<div class="section-title">state</div>' +
                          '<div>' + escapeHTML(JSON.stringify(state, null, 2)) + '</div>';
  } catch (err) {
    statusEl.textContent = 'disconnected: ' + err.message;
    statusEl.classList.add('paused');
  }
}

function escapeHTML(s) {
  return String(s)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;');
}

tick();
setInterval(tick, 700);
</script>
</body>
</html>
"""
