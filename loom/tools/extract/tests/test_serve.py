"""Serve face smoke test with http.client: a server in a thread on 127.0.0.1, port 0, a token.
No token → 401; GET /api/ops = ops.json; POST /api/op short and long (202 + job + SSE progress and
done); GET /api/view = exactly the rows the commander's pane gets; busy → 409; answer; recipe run;
manifest; i18n; the token file is owner-only; a non-loopback address is warned about; the socket
guard of the suite confirms there is no outgoing connection."""
import http.client
import json
import os
import stat
import threading
import time
import unittest
from unittest import mock

from _support import EXPORTS, ExtractTest

from loom.tools.extract import ops as _ops
from loom.tools.extract import serve as _serve
from loom.tools.extract.api import Session
from loom.tools.extract.datafiles import store

SRC = [str(EXPORTS / "openai_2026_sharded.zip"), str(EXPORTS / "anthropic_2026_full.zip")]


class ServeTest(ExtractTest):
    def setUp(self) -> None:
        super().setUp()
        self.sess = Session(face="api", ds=store())
        self.sess.call({"call": "open", "sources": SRC})
        self.srv = _serve.Server(self.sess, port=0, token_file=self.tmp / "pkg" / "serve.token").start()

    def tearDown(self) -> None:
        self.srv.stop()
        self.sess.close()
        super().tearDown()

    def req(self, method: str, path: str, body=None, token: bool = True, raw: bool = False):
        c = http.client.HTTPConnection("127.0.0.1", self.srv.port, timeout=30)
        headers = {"Content-Type": "application/json"}
        if token:
            headers["X-Extract-Token"] = self.srv.token
        c.request(method, path, body=json.dumps(body).encode("utf-8") if body is not None else None, headers=headers)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, (data.decode("utf-8") if raw else json.loads(data.decode("utf-8")))

    def test_token_ops_view_and_short_op(self):
        st, body = self.req("GET", "/api/ops", token=False)
        self.assertEqual(st, 401)
        self.assertEqual(body["code"], "unauthorized")
        st, body = self.req("GET", "/api/ops")
        self.assertEqual(st, 200)
        self.assertEqual(set(body["ops"]), set(store().file("ops.json")["ops"]))
        mode = stat.S_IMODE(os.stat(self.tmp / "pkg" / "serve.token").st_mode)
        self.assertEqual(mode, 0o600)
        self.assertEqual((self.tmp / "pkg" / "serve.token").read_text().strip(), self.srv.token)
        st, view = self.req("GET", "/api/view?root=sources&path=arch:1&sort=-messages")
        self.assertEqual(st, 200)
        same = self.sess.call({"call": "list", "root": "sources", "path": ["arch:1"], "sort": "-messages"})
        self.assertEqual(view["rows"], same["rows"], "the API view is exactly what a commander pane draws")
        st, res = self.req("POST", "/api/op", {"op": "select.expr", "params": {"expr": "title~/migracja/i"}})
        self.assertEqual(st, 200)
        self.assertEqual(res["result"]["counts"]["matched"], 1)
        self.assertEqual(self.sess.eng.recipe["steps"][-1]["by"], "api")
        st, res = self.req("POST", "/api/call", {"call": "selection"})
        self.assertEqual(res["total"], 1)
        st, res = self.req("GET", "/api/status")
        self.assertEqual(res["selection"]["conversations"], 1)
        st, res = self.req("POST", "/api/call", {"call": "view", "node": "nope"})
        self.assertEqual(st, 404)
        st, res = self.req("GET", "/api/nothing")
        self.assertEqual(st, 404)
        st, res = self.req("POST", "/api/call", {"call": "select", "how": "expr", "expr": "title~/(/"})
        self.assertEqual(st, 400)
        st, res = self.req("GET", "/api/i18n?lang=en")
        self.assertEqual(res["strings"]["commander.short.copy"], "Copy")
        st, res = self.req("GET", "/api/keymap?profile=vim")
        self.assertEqual(res["bindings"]["j"], "cursor.down")

    def test_long_op_job_with_sse_progress_busy_and_answer(self):
        gate = threading.Event()

        def slow(eng, p):
            for i in range(3):
                eng.emit("progress", done=i + 1, total=3)
            ok = eng.ask({"kind": "estimate", "estimate": {"calls": 1, "total": {"amount": 0.001, "currency": "USD", "known": True}}})
            gate.wait(10)
            return {"counts": {"accepted": int(ok)}}
        orig = _ops.OPS["clusters"]
        _ops.OPS["clusters"] = slow
        self.addCleanup(lambda: _ops.OPS.__setitem__("clusters", orig))
        st, res = self.req("POST", "/api/op", {"op": "clusters", "wait": False})
        self.assertEqual(st, 202)
        jid = res["job"]["id"]
        # the question arrives as an event; answer it
        deadline = time.time() + 10
        q = None
        while q is None and time.time() < deadline:
            q = next((e for e in self.sess.events_after(0) if e["kind"] == "question"), None)
            time.sleep(0.02)
        self.assertIsNotNone(q)
        st, res = self.req("GET", "/api/view?root=sources")
        self.assertEqual(st, 409, "busy, not blocked")
        self.assertTrue(res["busy"])
        st, res = self.req("POST", "/api/answer", {"question": q["id"], "answer": "yes"})
        self.assertEqual((st, res["answer"]), (200, True))
        gate.set()
        st, text = self.req("GET", f"/api/events?after=0&job={jid}", raw=True)
        self.assertEqual(st, 200)
        kinds = [line.split(": ", 1)[1] for line in text.splitlines() if line.startswith("event: ")]
        self.assertIn("progress", kinds)
        self.assertIn("question", kinds)
        self.assertEqual(kinds[-1], "done")
        st, res = self.req("GET", f"/api/jobs/{jid}")
        self.assertEqual(res["job"]["state"], "done")
        self.assertEqual(res["job"]["result"]["result"]["counts"]["accepted"], 1)
        # the event stream also takes the token as a query parameter (EventSource cannot set headers)
        c = http.client.HTTPConnection("127.0.0.1", self.srv.port, timeout=30)
        c.request("GET", f"/api/events?after=0&job={jid}&token={self.srv.token}")
        r = c.getresponse()
        self.assertEqual(r.status, 200)
        r.read()
        c.close()

    def test_recipe_run_writes_the_same_package_and_manifest(self):
        rec = {"sources": SRC, "steps": [{"op": "select.expr", "params": {"expr": "title~/migracja/i"}}, {"op": "cut", "params": {"profiles": ["flat"]}}]}
        st, res = self.req("POST", "/api/recipe/run", {"recipe": rec, "package": str(self.tmp / "p1")})
        self.assertEqual(st, 200, res)
        h1 = res["reproducible_hash"]
        st, man = self.req("GET", "/api/package/manifest")
        self.assertEqual(man["manifest"]["outputs"] and st, 200)
        st, res2 = self.req("POST", "/api/recipe/run", {"recipe": str(self.tmp / "p1" / "recipe.json"), "package": str(self.tmp / "p2")})
        self.assertEqual(res2["reproducible_hash"], h1)

    def test_non_loopback_address_is_warned_about(self):
        class Dummy:
            def __init__(self, addr, handler):
                self.server_address = addr

        with mock.patch.object(_serve, "_Http", Dummy), self.assertLogs("loom.tools.extract.serve", "WARNING") as cm:
            _serve.Server(self.sess, address="0.0.0.0", port=0)
        self.assertTrue(any("not the loopback" in m for m in cm.output))


if __name__ == "__main__":
    unittest.main()
