"""Offline structural share fixtures; fictional text, no public conversation data."""
import base64
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import unittest

LOOM = Path(__file__).resolve().parents[2]
FIXTURES = LOOM / "tests/fixtures/url_share"
WORKER = LOOM / "src/import/url_share/worker.py"
spec = importlib.util.spec_from_file_location("provider_worker", WORKER)
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)
POLICY = json.loads((LOOM / "data/presets/url_share.pack").read_text())
URL = "https://chatgpt.com/share/synthetic-share"


class Response:
    status = 200
    def __init__(self, body, mime="text/html"):
        self.body, self.mime = io.BytesIO(body), mime
        self.size = len(body)
    def getheader(self, name, default=None):
        return {"Content-Type": self.mime, "Content-Length": str(self.size)}.get(name, default)
    def read1(self, size):
        return self.body.read(size)


class MemoryTransport:
    sock = None
    def __init__(self, body, mime="text/html"):
        self.body, self.mime, self.calls = body, mime, []
    def open(self, url, deadline):
        self.calls.append(url)
        if len(self.calls) != 1:
            raise AssertionError("references must never be fetched")
        return self, Response(self.body, self.mime)
    def close(self):
        pass


def snapshot(name="chatgpt_turbo_stream.html", url=URL, body=None, mime="text/html", policy=None):
    raw = (FIXTURES / name).read_bytes() if body is None else body
    transport = MemoryTransport(raw, mime)
    result = w.snapshot_request({"schema": "loom.url_import_request/1", "mode": "share", "url": url},
                                policy or POLICY, transport)
    return result, transport


def next_html(data):
    return ('<html><script id="__NEXT_DATA__" type="application/json">' +
            json.dumps({"props": {"pageProps": {"serverResponse": {"data": data}}}}) +
            '</script></html>').encode()


def fixture_data():
    page = w.ShareHTML()
    page.feed((FIXTURES / "chatgpt_next_data.html").read_text())
    return w.chatgpt_data(page, POLICY["limits"]["worker_output_bytes"])[0]


class ProviderShareTests(unittest.TestCase):
    def test_both_real_structures_preserve_roles_order_whitespace_and_empty_text(self):
        for name in ("chatgpt_next_data.html", "chatgpt_turbo_stream.html"):
            with self.subTest(name=name):
                result, transport = snapshot(name)
                parsed = result["parsed"]
                self.assertEqual(parsed["status"], "complete")
                self.assertEqual(parsed["adapter"], "chatgpt.share_html/1")
                self.assertEqual(parsed["title"], "Syntetyczny share — żółw 🐢")
                self.assertEqual([(m["role"],m["text"]) for m in parsed["messages"]], [
                    ("user", "  Zażółć & <tag>\nhttps://example.invalid/link  "),
                    ("assistant", "Poprzednia gałąź"), ("assistant", "Wybrana odpowiedź\n"), ("tool", "")])
                self.assertEqual(result["llm_calls"], 0)
                self.assertEqual(transport.calls, [URL])

    def test_branches_parent_edges_and_explicit_selection(self):
        parsed = snapshot()[0]["parsed"]
        rows = parsed["messages"]
        self.assertEqual([m["active"] for m in rows], [True, False, True, True])
        self.assertEqual([m["parent_pointer"] for m in rows], [None, rows[0]["pointer"], rows[0]["pointer"], rows[2]["pointer"]])
        self.assertEqual(parsed["branch"], {"selection":"current_node", "selected_node":"tool",
                         "selected_path":["root","u/1","a~2","tool"], "tree_preserved":True})

    def test_provenance_exact_original_bytes_and_escaped_locators(self):
        for name in ("chatgpt_next_data.html", "chatgpt_turbo_stream.html"):
            result, _ = snapshot(name)
            raw = (FIXTURES / name).read_bytes()
            self.assertEqual(base64.b64decode(result["resource"]["body_base64"]), raw)
            self.assertEqual(result["resource"]["sha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual(result["resource"]["source_url"], URL)
            self.assertEqual(result["resource"]["final_url"], URL)
            parsed = result["parsed"]
            self.assertTrue(parsed["messages"][0]["pointer"].endswith("/mapping/u~11/message"))
            self.assertTrue(parsed["messages"][2]["pointer"].endswith("/mapping/a~02/message"))
            self.assertIn(parsed["locator"]["format"], ("next_data", "turbo_stream_v2"))
        self.assertEqual(snapshot()[0]["parsed"]["locator"]["script_indices"], [0, 1])

    def test_references_survive_and_never_fetch_private_or_provider_assets(self):
        parsed = snapshot()[0]["parsed"]
        attachments = [a for m in parsed["messages"] for a in m["attachments"]]
        self.assertEqual([a["status"] for a in attachments], ["reference", "reference"])
        self.assertEqual(attachments[0]["original"]["download_url"], "https://127.0.0.1/private")
        self.assertEqual(attachments[1]["original"]["asset_pointer"], "sediment://synthetic-image")
        self.assertTrue(all("body_base64" not in a for a in attachments))
        self.assertTrue(attachments[0]["pointer"].endswith("/message/metadata/attachments/0"))
        self.assertTrue(attachments[1]["pointer"].endswith("/message/content/parts/1"))

    def test_snapshot_determinism_for_native_idempotency(self):
        self.assertEqual(snapshot()[0], snapshot()[0])

    def test_unknown_or_spoofed_host_path_and_mime_are_not_provider_success(self):
        for url in ("https://example.invalid/share/synthetic-share", "https://chatgpt.com.evil.invalid/share/synthetic-share",
                    "https://chatgpt.com/c/synthetic-share", "http://chatgpt.com/share/synthetic-share",
                    "https://claude.ai/share/synthetic-share"):
            with self.subTest(url=url):
                self.assertEqual(snapshot(url=url)[0]["parsed"]["messages"], [])
        self.assertEqual(snapshot(mime="image/png")[0]["parsed"]["reason"], "share_media_type")

    def test_login_walls_and_truncated_html_never_become_conversations(self):
        for body, url, reason in ((b'<html><input type="password"></html>',URL,"login_required"),
                                  (b'<html>Sign in</html>',"https://chatgpt.com/auth/login","login_required"),
                                  (b'<html><script>incomplete',URL,"incomplete_html")):
            parsed = snapshot(body=body, url=url)[0]["parsed"]
            self.assertEqual(parsed["messages"], [])
            self.assertEqual(parsed["reason"], reason)

    def test_malformed_script_and_unsupported_js_are_inert(self):
        for body in (b'<html><script id="__NEXT_DATA__" type="application/json">{bad}</script></html>',
                     b'<html><script>window.__reactRouterContext.streamController.enqueue(process.exit());</script></html>',
                     b'<html><script>window.__reactRouterContext.streamController.enqueue("[]" + evil());</script></html>',
                     b'<html><h1>A title is not a conversation</h1></html>'):
            parsed = snapshot(body=body)[0]["parsed"]
            self.assertEqual(parsed["messages"], [])
            self.assertEqual(parsed["status"], "unsupported")

    def test_broken_trees_and_ambiguous_branch_are_refused(self):
        for change in ("cycle", "parent", "selection", "empty"):
            data = fixture_data()
            if change == "cycle": data["mapping"]["root"]["parent"] = "tool"
            if change == "parent": data["mapping"]["tool"]["parent"] = "missing"
            if change == "selection": data.pop("current_node"); data.pop("linear_conversation")
            if change == "empty": data["mapping"] = {}; data["linear_conversation"] = []
            parsed = snapshot(body=next_html(data))[0]["parsed"]
            self.assertEqual(parsed["messages"], [], change)
            self.assertEqual(parsed["status"], "unsupported", change)

    def test_linear_only_import_names_the_shared_branch(self):
        data = fixture_data()
        data["linear_conversation"] = [data["mapping"][key] for key in ["root","u/1","a~2","tool"]]
        del data["mapping"]; del data["current_node"]
        parsed = snapshot(body=next_html(data))[0]["parsed"]
        self.assertEqual(parsed["status"], "complete")
        self.assertEqual(parsed["branch"]["selection"], "linear_conversation")
        self.assertEqual([m["role"] for m in parsed["messages"]], ["user","assistant","tool"])
        self.assertTrue(all(m["active"] for m in parsed["messages"]))

    def test_linear_supplied_parent_edges_are_not_overwritten(self):
        data = fixture_data()
        data["linear_conversation"] = list(data["mapping"].values())
        del data["mapping"]
        parsed = snapshot(body=next_html(data))[0]["parsed"]
        self.assertEqual(parsed["status"], "complete")
        self.assertEqual([m["active"] for m in parsed["messages"]], [True, False, True, True])
        self.assertEqual(parsed["messages"][2]["parent_pointer"], parsed["messages"][0]["pointer"])

    def test_unknown_content_is_partial_without_fabricated_text(self):
        data = fixture_data()
        data["mapping"]["tool"]["message"]["content"] = {"content_type":"future", "secret":"not inferred"}
        parsed = snapshot(body=next_html(data))[0]["parsed"]
        self.assertEqual(parsed["status"], "partial")
        self.assertEqual(parsed["reason"], "share_content_partial")
        self.assertEqual(parsed["messages"][-1]["text"], "")
        self.assertNotIn("not inferred", json.dumps(parsed))

    def test_turbo_references_literals_cycles_and_expansion_limit(self):
        self.assertEqual(w.turbo_json([{"_1":2}, "number", 42], 1024), {"number":42})
        for pool in ([{"_1":0},"cycle"], [{"_1":99},"out"], [{"bad":1}, "bad"],
                     [{"_1":2},"expansion", "x"*1024]):
            with self.assertRaises(ValueError): w.turbo_json(pool, 128)
        policy = copy.deepcopy(POLICY)
        policy["limits"]["worker_output_bytes"] = 128
        self.assertEqual(snapshot(policy=policy)[0]["parsed"]["messages"], [])

    def test_production_byte_limit_still_applies_before_parser(self):
        policy = copy.deepcopy(POLICY)
        policy["limits"]["max_bytes"] = 128
        parsed = snapshot(policy=policy)[0]["parsed"]
        self.assertEqual(parsed["reason"], "oversize")
        self.assertEqual(parsed["messages"], [])

    def test_isolated_process_contract_for_native_handoff(self):
        envelope = {"request":{"schema":"loom.url_import_request/1","mode":"share","url":URL}, "policy":POLICY}
        result = subprocess.run([sys.executable,"-I",str(Path(__file__).with_name("provider_fixture_worker.py"))],
                                input=json.dumps(envelope).encode(), capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        self.assertEqual(json.loads(result.stdout)["parsed"]["status"], "complete")


if __name__ == "__main__":
    unittest.main()
