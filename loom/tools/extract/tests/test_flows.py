"""Slice B — model steps behind an explicit flag: an estimate first, "no" by
default, the ×10 guard and limits, a stub OpenAI-compatible provider on the
loopback (the socket guard forbids anything else), a call log without keys or
text, the cache (a replay never pays twice), cascade levels L3/L4 deciding the
ambiguous band, summaries for cards, a description turned into criteria."""
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from _support import EXPORTS, ExtractTest
from loom.tools.extract.datafiles import read_json, store
from loom.tools.extract.flows import ModelRefused


class _Stub(BaseHTTPRequestHandler):
    calls: list = []

    def do_POST(self):  # noqa: N802 - http.server API
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])).decode("utf-8"))
        system = body["messages"][0]["content"]
        type(self).calls.append({"model": body["model"], "auth": self.headers.get("Authorization"), "system": system[:40]})
        if "krótką nazwę" in system or "short name" in system:
            answer = {"name": "Migracje baz", "description": "stub"}
        elif "kryteria" in system or "criteria" in system:
            answer = {"schema": "loom.extract.criteria/1", "id": "x", "keywords": [{"term": "migracja"}], "regex": [], "vectors": []}
        elif "Streść" in system or "Summarise" in system:
            answer = {"summary": "Rozmowa o migracji.", "open_questions": ["Kiedy?"]}
        else:
            answer = {"verdict": "include", "confidence": 0.9, "reason": "stub"}
        data = json.dumps({"choices": [{"message": {"content": "```json\n" + json.dumps(answer) + "\n```"}}],
                           "usage": {"prompt_tokens": 100, "completion_tokens": 20}}).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *a):  # quiet
        return


class FlowsTest(ExtractTest):
    def setUp(self) -> None:
        super().setUp()
        _Stub.calls = []
        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), _Stub)
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.srv.server_address[1]}/v1"
        ov = store().overlay
        (ov / "cascades").mkdir(parents=True)
        cas = read_json(store().shipped / "cascades/default.json")
        cas.update({"id": "all_ambiguous", "bands": {"include": 1.01, "exclude": -0.01}})
        (ov / "cascades/all_ambiguous.json").write_text(json.dumps(cas), encoding="utf-8")
        (ov / "prices.json").write_text(json.dumps({"models": {"stub-cheap": {"input_per_mtok": 1.0, "output_per_mtok": 2.0, "as_of": "2026-10-16"},
                                                               "stub-strong": {"input_per_mtok": 10.0, "output_per_mtok": 20.0, "as_of": "2026-10-16"}}}),
                                        encoding="utf-8")
        import os
        os.environ["XPORT_TEST_KEY"] = "sekretny-klucz-testowy"

    def tearDown(self) -> None:
        self.srv.shutdown()
        self.srv.server_close()
        super().tearDown()

    def settings(self, **over):
        m = {"enabled": True, "accept": True, "classes": {"cheap": "stub-cheap", "strong": "stub-strong"},
             "provider": {"base_url": self.url, "api_key_env": "XPORT_TEST_KEY"}}
        m.update(over)
        return {"flows": {"model": m}, "cache_dir": str(self.tmp / "cache")}

    def _eng(self, **over):
        from loom.tools.extract.engine import Engine
        eng = Engine(ds=store(), settings=self.settings(**over))
        self._engines.append(eng)
        eng.set_sources([str(EXPORTS / "openai_2026_sharded.zip")])
        return eng

    SELECT = {"terms": ["migracja"], "cascade": "all_ambiguous"}

    def test_no_model_without_the_flag_and_an_estimate_on_request(self):
        eng = self._eng(enabled=False)
        r = eng.op("select.criteria", dict(self.SELECT))
        self.assertEqual(_Stub.calls, [])
        self.assertEqual(r["counts"]["ambiguous"], r["counts"]["include"] + r["counts"]["ambiguous"])
        r = eng.op("select.criteria", dict(self.SELECT, model_count=True))
        est = r["model_estimate"]
        self.assertGreater(est["calls"], 0)
        self.assertTrue(est["total"]["known"])
        self.assertEqual(_Stub.calls, [], "an estimate never calls")

    def test_refused_without_consent_and_nothing_sent(self):
        eng = self._eng(accept=False)
        with self.assertRaises(ModelRefused) as cm:
            eng.op("select.criteria", dict(self.SELECT))
        self.assertTrue(cm.exception.question)
        self.assertIsNotNone(cm.exception.estimate)
        self.assertEqual(_Stub.calls, [])
        self.assertFalse((store().overlay / "usage.jsonl").exists())
        eng2 = self._eng(accept=False)
        eng2.ask = lambda q: q["kind"] == "estimate"
        eng2.op("select.criteria", dict(self.SELECT))
        self.assertTrue(_Stub.calls, "a 'yes' to the question sends")

    def test_model_levels_decide_the_ambiguous_band_with_a_log_and_cache(self):
        eng = self._eng()
        r = eng.op("select.criteria", dict(self.SELECT))
        n = len(_Stub.calls)
        self.assertGreater(n, 0)
        self.assertTrue(all(c["model"] == "stub-cheap" for c in _Stub.calls), "L3 includes, so L4 (strong) never runs")
        self.assertTrue(all(c["auth"] == "Bearer sekretny-klucz-testowy" for c in _Stub.calls))
        self.assertGreater(r["counts"]["include"], 0)
        self.assertTrue(all(d["source"] == "L3" for d in eng.unit_decisions.values() if d["decision"] == "include"))
        eng.op("cut", {"profiles": ["flat"]})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        log = (self.tmp / "pkg/log.jsonl").read_text(encoding="utf-8")
        self.assertEqual(len(log.splitlines()), n)
        self.assertNotIn("sekretny", log)
        self.assertNotIn("Migracja", log, "the log holds hashes, never text")
        man = json.loads((self.tmp / "pkg/manifest.json").read_text(encoding="utf-8"))
        self.assertTrue(man["cost_total"]["known"])
        self.assertAlmostEqual(man["cost_total"]["amount"], n * (100 * 1.0 + 20 * 2.0) / 1e6, places=9)
        self.assertIn("cheap_classify", (self.tmp / "pkg/RAPORT.md").read_text(encoding="utf-8"))
        # same inputs again: everything from the cache, nothing sent, nothing paid
        eng2 = self._eng()
        eng2.op("select.criteria", dict(self.SELECT))
        self.assertEqual(len(_Stub.calls), n)
        calls, total = eng2.model_runner.summary()
        self.assertEqual(calls[0]["cache_hits"], calls[0]["calls"])
        self.assertEqual(total["amount"], 0.0)
        # replay_only: with the cache fine, without it a question (refusal), never a call
        eng3 = self._eng(replay_only=True)
        eng3.op("select.criteria", dict(self.SELECT))
        import shutil
        shutil.rmtree(self.tmp / "cache")
        eng4 = self._eng(replay_only=True)
        with self.assertRaises(ModelRefused) as cm:
            eng4.op("select.criteria", dict(self.SELECT))
        self.assertTrue(cm.exception.question)
        self.assertEqual(len(_Stub.calls), n)

    def test_x10_guard_and_limits(self):
        usage = store().overlay / "usage.jsonl"
        usage.write_text(json.dumps({"at": "2026-10-01T00:00:00Z", "estimate": {"amount": 1e-9}, "accepted": True}) + "\n", encoding="utf-8")
        eng = self._eng()
        with self.assertRaises(ModelRefused) as cm:
            eng.op("select.criteria", dict(self.SELECT))
        self.assertIn("×", cm.exception.reason)
        self.assertEqual(_Stub.calls, [])
        eng = self._eng(accept_x10=True)
        eng.op("select.criteria", dict(self.SELECT))
        self.assertTrue(_Stub.calls)
        self.assertEqual(len(usage.read_text(encoding="utf-8").splitlines()), 2, "an accepted run is remembered for the next guard")
        eng = self._eng(guard={"ratio": 10.0, "limit_per_run": 1e-9, "limit_per_day": 0})
        with self.assertRaises(ModelRefused) as cm:
            eng.op("select.criteria", dict(self.SELECT))
        self.assertIn("limit_per_run", cm.exception.reason)
        eng = self._eng(classes={"cheap": "unpriced-model", "strong": "stub-strong"}, guard={"ratio": 10.0, "limit_per_run": 5, "limit_per_day": 0})
        with self.assertRaises(ModelRefused) as cm:
            eng.op("select.criteria", dict(self.SELECT))
        self.assertIn("price is unknown", cm.exception.reason)

    def test_summaries_for_cards_and_described_criteria(self):
        eng = self._eng()
        eng.op("select.expr", {"expr": "title~/Migracja/"})
        eng.op("flow.run", {"flow": "summarize"})
        eng.op("cut", {"profiles": ["cards"]})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        cards = json.loads((self.tmp / "pkg/cards/cards.json").read_text(encoding="utf-8"))["cards"]
        self.assertEqual(cards[0]["summary"], "Rozmowa o migracji.")
        self.assertEqual(cards[0]["summary_source"], "model:summarize")
        r = eng.op("describe", {"text": "wszystko o migracji bazy", "model": True})
        self.assertEqual(r["source"], "model:describe_criteria")
        self.assertEqual(r["criteria"]["keywords"][0]["term"], "migracja")
        rules = eng.op("describe", {"text": "wszystko o migracji bazy"})
        self.assertEqual(rules["source"], "rules")

    def test_cluster_names_by_model(self):
        from loom.tools.extract.engine import Engine
        eng = Engine(ds=store(), settings=self.settings())
        self._engines.append(eng)
        eng.set_sources([str(EXPORTS / "openai_2026_sharded.zip"), str(EXPORTS / "anthropic_2026_full.zip")])
        eng.op("clusters", {"min_conversations": 1})
        n = len(eng.clusters["clusters"])
        self.assertGreater(n, 0)
        r = eng.op("flow.run", {"flow": "name_clusters"})
        self.assertEqual(r["counts"]["calls"], n)
        self.assertTrue(all(c["name"] == "Migracje baz" for c in eng.clusters["clusters"]))

    def test_no_model_configured_is_explicit(self):
        eng = self._eng(classes={"cheap": None, "strong": None})
        eng.op("select.expr", {"expr": "title~/Migracja/"})
        with self.assertRaises(ModelRefused) as cm:
            eng.op("flow.run", {"flow": "summarize"})
        self.assertIn("no model for class", cm.exception.reason)
        self.assertEqual(_Stub.calls, [])
