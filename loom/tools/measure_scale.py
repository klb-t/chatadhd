#!/usr/bin/env python3
"""Time + peak RSS of the Loom HTTP server on a synthetic data dir (not CI).
Usage: measure_scale.py SERVER_BIN DATA_DIR [--import-file F] [--provider-script deploy/first-steps-provider.py]
Prints one JSON object. Server runs single-user on 127.0.0.1 with the data dir given."""
import argparse, json, os, re, socket, subprocess, sys, time, urllib.request, uuid

def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def call(base, method, path, body=None, raw=None, headers=None, timeout=3600):
    data = raw if raw is not None else (None if body is None else json.dumps(body).encode())
    h = {"Content-Type": "application/json"} if raw is None else {}
    h.update(headers or {})
    t = time.time()
    r = urllib.request.Request(base + path, data=data, method=method, headers=h)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            b = resp.read(); code = resp.status
    except urllib.error.HTTPError as e:
        b = e.read(); code = e.code
    return round(time.time() - t, 3), code, len(b), b

def hwm(pid):
    try:
        for l in open("/proc/%d/status" % pid):
            if l.startswith("VmHWM"): return int(l.split()[1]) // 1024
    except OSError: pass
    return -1

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("server"); ap.add_argument("data")
    ap.add_argument("--import-file"); ap.add_argument("--provider-script")
    a = ap.parse_args()
    out = {"data_mb": int(subprocess.check_output(["du", "-sm", a.data]).split()[0])}
    port = free_port(); base = "http://127.0.0.1:%d" % port
    env = dict(os.environ, CHATADHD_DATA=a.data)
    t0 = time.time()
    srv = subprocess.Popen([a.server, "--host", "127.0.0.1", "--port", str(port), "--data-dir", a.data],
                           env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    while True:
        try:
            if urllib.request.urlopen(base + "/api/healthz", timeout=1).status == 200: break
        except Exception:
            if srv.poll() is not None: sys.exit("server died: " + srv.stderr.read().decode()[-500:])
            time.sleep(0.02)
    out["start_s"] = round(time.time() - t0, 3)
    pid = srv.pid; out["rss_after_start_mb"] = hwm(pid)  # VmHWM = peak resident set (no /usr/bin/time here)
    m = {}
    def step(name, *args, **kw):
        s, code, n, b = call(base, *args, **kw); m[name] = {"s": s, "http": code, "bytes": n}; return b
    b = step("conv_list_50", "GET", "/api/conversations?limit=50")
    first = json.loads(b); first = first if isinstance(first, list) else first.get("conversations", first.get("data", []))
    step("conv_list_all", "GET", "/api/conversations?limit=100000000")
    cid = first[0]["id"] if first else ""
    step("conv_get_first", "GET", "/api/conversations/" + cid)
    step("conv_messages_first", "GET", "/api/conversations/%s/messages" % cid)
    step("conv_view_first", "GET", "/api/conversations/%s/view" % cid)
    for i, q in enumerate(("graph memory", "zażółć", "budżet podsumowanie plan")):
        step("search_%d_%s" % (i, "cold" if i == 0 else "warm"), "GET", "/api/search?limit=20&q=" + urllib.request.quote(q))
    step("search_mode_keyword", "GET", "/api/search?limit=20&mode=keyword&q=" + urllib.request.quote("deploy review"))
    step("graph_data_all", "GET", "/api/graph/data")
    step("graph_data_conv", "GET", "/api/graph/data?conv_id=" + cid)
    nb = step("graph_nodes_200", "GET", "/api/graph/nodes?limit=200")
    nodes = json.loads(nb); nodes = nodes if isinstance(nodes, list) else nodes.get("nodes", [])
    step("graph_edges_200", "GET", "/api/graph/edges?limit=200")
    if nodes:
        step("graph_expand_d2", "POST", "/api/graph/expand", {"seed_ids": [n["id"] for n in nodes[:5]], "depth": 2})
    step("actions_for_message", "POST", "/api/actions/for", {"element": {"class": "message"}})
    step("actions_for_audio", "POST", "/api/actions/for", {"element": {"class": "audio_form"}, "scope": "installation"})
    if a.provider_script:
        pport = free_port()
        prov = subprocess.Popen([sys.executable, a.provider_script, str(pport)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        step("cfg", "PATCH", "/api/config", {"base_url": "http://127.0.0.1:%d" % pport, "default_model": "test/x", "stream": True})
        step("key", "POST", "/api/secrets/api_key", {"value": "k"})
        step("chat_new_conv", "POST", "/api/chat", {"message": "Hello, how are you?"})
        step("chat_in_big_conv", "POST", "/api/chat", {"message": "And now?", "conv_id": cid})
        prov.terminate()
    if a.import_file:
        bd = uuid.uuid4().hex; fn = os.path.basename(a.import_file)
        body = (("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\nContent-Type: application/json\r\n\r\n" % (bd, fn)).encode()
                + open(a.import_file, "rb").read() + ("\r\n--%s--\r\n" % bd).encode())
        step("import", "POST", "/api/import", raw=body, headers={"Content-Type": "multipart/form-data; boundary=" + bd})
        m["import"]["tail"] = _tail(m)
        step("conv_list_50_after_import", "GET", "/api/conversations?limit=50")
    out["steps"] = m; out["peak_rss_mb"] = hwm(pid)
    srv.terminate(); srv.communicate()
    print(json.dumps(out))

def _tail(m): return None

main()
