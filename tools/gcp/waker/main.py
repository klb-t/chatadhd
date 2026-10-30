"""R107 waker (owner, 2026-10-30): a visit to a service starts the stopped VM, so nobody has to phone the owner.

Runs on Cloud Run (outside the VM, min-instances 0, stdlib only). GET /<service>[/path] → if the VM is RUNNING and the
service answers, 302 to it (path and query kept); otherwise start the VM (at most one start per START_GAP seconds per
instance of this service, so a stranger cannot spam starts) and show a PL/EN page that polls /status and redirects.
Settings are environment variables (presets): PROJECT, ZONE, INSTANCE, SERVICES (JSON {name: {"url":..., "health": path}}),
START_GAP. No passwords here: services behind basic auth count as up when they answer 401.
"""
import html, json, os, time, urllib.error, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROJECT = os.environ.get("PROJECT", "clever-stone-467803-g7")
ZONE = os.environ.get("ZONE", "europe-west4-c")
INSTANCE = os.environ.get("INSTANCE", "devbox-20260923")
BASE = os.environ.get("BASE", "34-158-117-249.sslip.io")
SERVICES = json.loads(os.environ.get("SERVICES", "") or json.dumps({
    "watchdog": {"url": f"https://watchdog.{BASE}", "health": "/"},
    "chat": {"url": f"https://chat.{BASE}", "health": "/api/healthz"},
    "ageds": {"url": f"https://ageds.{BASE}", "health": "/"},
}))
START_GAP = float(os.environ.get("START_GAP", "120"))
METADATA = os.environ.get("METADATA_URL", "http://metadata.google.internal/computeMetadata/v1")
COMPUTE = os.environ.get("COMPUTE_URL", "https://compute.googleapis.com/compute/v1")
UA = "r107-waker"  # the VM's idle check ignores this user agent
_last_start = 0.0


def _token():
    req = urllib.request.Request(METADATA + "/instance/service-accounts/default/token", headers={"Metadata-Flavor": "Google"})
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.load(r)["access_token"]


def _compute(method, suffix):
    url = f"{COMPUTE}/projects/{PROJECT}/zones/{ZONE}/instances/{INSTANCE}{suffix}"
    req = urllib.request.Request(url, method=method, data=b"" if method == "POST" else None,
                                 headers={"Authorization": "Bearer " + _token()})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.load(r)


def vm_status():
    return _compute("GET", "").get("status", "UNKNOWN")


def service_up(name):
    s = SERVICES[name]
    req = urllib.request.Request(s["url"] + s.get("health", "/"), headers={"User-Agent": UA})
    try:
        with urllib.request.urlopen(req, timeout=4) as r:
            return r.status < 500
    except urllib.error.HTTPError as e:
        return e.code < 500  # 401 behind basic auth = the service answers
    except Exception:
        return False


def maybe_start():
    global _last_start
    if time.time() - _last_start < START_GAP:
        return "start already requested"
    _last_start = time.time()
    _compute("POST", "/start")
    return "start requested"


PAGE = """<!doctype html><html lang="pl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Uruchamiam… / Starting…</title><style>body{{font:16px system-ui;margin:3rem auto;max-width:34rem;padding:0 16px;color:#222;background:#fff}}
@media (prefers-color-scheme:dark){{body{{color:#ddd;background:#111}}}}p.s{{opacity:.7}}</style>
<h1>Uruchamiam {name}</h1><p>Serwer śpi, gdy nikt z niego nie korzysta. Właśnie go budzę — zwykle trwa to 1–2 minuty. Ta strona sama przejdzie dalej.</p>
<h2 lang="en">Starting {name}</h2><p lang="en">The server sleeps when nobody uses it. It is waking up now, usually in 1–2 minutes. This page continues by itself.</p>
<p class="s" id="s">…</p>
<script>
const target={target};
async function tick(){{try{{const r=await fetch("/status/{name}",{{cache:"no-store"}});const j=await r.json();
document.getElementById("s").textContent=j.vm+(j.up?" · OK":"");if(j.up){{location.replace(target);return}}}}catch(e){{}}setTimeout(tick,4000)}}
tick();
</script></html>"""


class H(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="text/html; charset=utf-8", extra=None):
        b = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        path, _, query = self.path.partition("?")
        parts = path.lstrip("/").split("/", 1)
        if parts[0] == "status" and len(parts) == 2 and parts[1] in SERVICES:
            st = vm_status()
            return self._send(200, json.dumps({"vm": st, "up": st == "RUNNING" and service_up(parts[1])}), "application/json")
        name = parts[0]
        if name not in SERVICES:
            links = "".join(f'<li><a href="/{html.escape(n)}">{html.escape(n)}</a></li>' for n in SERVICES)
            return self._send(404 if name else 200, f"<!doctype html><meta charset=utf-8><ul>{links}</ul>")
        target = SERVICES[name]["url"] + "/" + (parts[1] if len(parts) == 2 else "") + (("?" + query) if query else "")
        st = vm_status()
        if st == "RUNNING" and service_up(name):
            return self._send(302, "", extra={"Location": target})
        if st in ("TERMINATED", "STOPPED"):
            try:
                maybe_start()
            except Exception as e:  # shown, not hidden: the page keeps polling
                print("start failed:", e, flush=True)
        return self._send(200, PAGE.format(name=html.escape(name), target=json.dumps(target)))

    def log_message(self, fmt, *a):
        print(fmt % a, flush=True)


if __name__ == "__main__":
    ThreadingHTTPServer(("0.0.0.0", int(os.environ.get("PORT", "8080"))), H).serve_forever()
