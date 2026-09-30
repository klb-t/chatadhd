"""C ABI proof that accepted task ancestry survives message-layout changes.

The two tests use only a local loopback provider.  Their temporary databases
are deliberately retained so a failed run leaves inspectable evidence.
"""
import copy
import ctypes
import hashlib
import http.server
import json
import os
import tempfile
import threading
import unittest


def encoded(value):
    return json.dumps(value, ensure_ascii=False).encode("utf-8")


class DurableActiveTaskCAbiTest(unittest.TestCase):
    """Exercise the public C ABI across a real shutdown/re-initialization."""

    def setUp(self):
        library = os.environ.get("LOOM_LIBRARY")
        if not library:
            self.skipTest("LOOM_LIBRARY not set; shared native build required")

        self.lib = ctypes.CDLL(library)
        signatures = {
            "loom_init_ex": ([ctypes.c_char_p, ctypes.c_void_p], ctypes.c_void_p),
            "loom_shutdown": ([ctypes.c_void_p], None),
            "loom_free_string": ([ctypes.c_void_p], None),
            "loom_set_config_json": ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int),
            "loom_set_secret": ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p], ctypes.c_int),
            "loom_create_conversation": ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p),
            "loom_get_message": ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p),
            "loom_get_messages_ex": ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int], ctypes.c_void_p),
            "loom_update_message": ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p], ctypes.c_int),
            "loom_query_events": ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p),
            "loom_chat_ex": (
                [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p],
                ctypes.c_void_p,
            ),
        }
        for name, (args, result) in signatures.items():
            getattr(self.lib, name).argtypes = args
            getattr(self.lib, name).restype = result

        self.calls = []
        fixture = self

        class Provider(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                fixture.calls.append({"path": self.path, "body": body})
                answer = encoded(
                    {"choices": [{"message": {"content": f"LOCAL_REPLY_{len(fixture.calls)}"}}]}
                )
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(answer)))
                self.end_headers()
                self.wfile.write(answer)

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Provider)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.directory = tempfile.mkdtemp(prefix="loom-w1-durable-cabi-")
        print(f"W1 retained synthetic durable C ABI database: {self.directory}")
        self.ctx = None
        self.open_runtime()
        self.conv = self.read(
            self.lib.loom_create_conversation(self.ctx, b"W1 durable acceptance proof")
        )["id"]

    def tearDown(self):
        self.close_runtime()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)

    def read(self, pointer):
        self.assertTrue(pointer)
        try:
            return json.loads(ctypes.string_at(pointer))
        finally:
            self.lib.loom_free_string(pointer)

    def open_runtime(self):
        self.assertIsNone(self.ctx)
        self.ctx = self.lib.loom_init_ex(
            encoded({"data_dir": self.directory, "start_workers": False}), None
        )
        self.assertTrue(self.ctx)
        self.assertEqual(
            self.lib.loom_set_config_json(
                self.ctx,
                encoded(
                    {
                        "base_url": f"http://127.0.0.1:{self.server.server_port}",
                        "default_model": "synthetic/local",
                        "auto_title": False,
                        "semantic_analysis": False,
                        "system_prompt": "",
                        "stream": False,
                    }
                ),
            ),
            0,
        )
        self.assertEqual(
            self.lib.loom_set_secret(self.ctx, b"api_key", b"local-test-placeholder"), 0
        )

    def close_runtime(self):
        if self.ctx:
            self.lib.loom_shutdown(self.ctx)
            self.ctx = None

    def row(self, identity):
        return self.read(self.lib.loom_get_message(self.ctx, identity.encode()))

    def rows(self, conversation=None):
        conversation = conversation or self.conv
        return self.read(self.lib.loom_get_messages_ex(self.ctx, conversation.encode(), 1))

    def chat(self, message, product=None):
        request = {
            "conv_id": self.conv,
            "message": message,
            "stream": False,
            "include_memory": False,
            "include_graph_memory": False,
            "trace_context": False,
        }
        if product is not None:
            request.update(active_task_spec=product[0], active_task_bindings=product[1])
        return self.read(self.lib.loom_chat_ex(self.ctx, encoded(request), None, None))

    def product(self, identity, version, previous, source, instruction):
        event = f"event-{identity}"
        spec = {
            "schema": "loom.active_task_spec/1",
            "product_ref": {"kind": "product", "id": identity},
            "goal_id": "durable-report",
            "knowledge_run": None,
            "scope": {
                "conversation_id": self.conv,
                "branch_id": "native:active",
                "task_id": "durable-report",
            },
            "version": version,
            "previous_product_ref": previous,
            "known_at": "2026-10-01T00:30:00Z",
            "representation": "derived_product",
            "materializer": {"id": "durable-cabi-test", "version": "1"},
            "history_event_ids": [event],
            "source_refs": [
                {
                    "event_id": event,
                    "locator": {"source": "synthetic"},
                    "known_at": "2026-10-01T00:29:00Z",
                    "quote": source["text"],
                }
            ],
            "statements": [
                {
                    "id": "goal",
                    "kind": "goal",
                    "status": "active",
                    "text": instruction,
                    "source_event_ids": [event],
                    "claim_ids": [],
                    "conditions": [],
                    "supersedes": [],
                }
            ],
            "compiled_instruction": {
                "text": "UNTRUSTED",
                "source_map": [
                    {"span": {"byte_start": 0, "byte_len": 9}, "statement_ids": ["goal"]}
                ],
            },
        }
        bindings = {
            event: {
                "message_id": source["id"],
                "text_sha256": hashlib.sha256(source["text"].encode("utf-8")).hexdigest(),
            }
        }
        return spec, bindings

    def authority_events(self):
        return self.read(
            self.lib.loom_query_events(
                self.ctx,
                encoded(
                    {
                        "type": "chat.active_task.*",
                        "subject_id": self.conv,
                        "limit": 500,
                    }
                ),
            )
        )

    def assert_acceptance_event(self, event, message, snapshot, spec):
        self.assertEqual(event["type"], "chat.active_task.accepted.v1")
        self.assertEqual(event["subject_id"], self.conv)
        payload = event["payload"]
        self.assertEqual(payload["schema"], "loom.chat_active_task_acceptance/1")
        self.assertEqual(payload["acceptance"], "explicit_caller_supplied")
        self.assertEqual(payload["originating_message_id"], message["id"])
        self.assertEqual(payload["scope"], spec["scope"])
        self.assertEqual(payload["goal_id"], spec["goal_id"])
        self.assertEqual(payload["product_ref"], spec["product_ref"])
        self.assertEqual(payload["version"], spec["version"])
        self.assertEqual(payload["previous_product_ref"], spec["previous_product_ref"])
        self.assertEqual(payload["accepted_snapshot"], snapshot)
        canonical = json.dumps(
            snapshot, ensure_ascii=False, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        self.assertEqual(payload["snapshot_sha256"], hashlib.sha256(canonical).hexdigest())

    def assert_rejected_without_effect(self, product, conversations):
        before_rows = {conversation: self.rows(conversation) for conversation in conversations}
        before_calls = copy.deepcopy(self.calls)
        before_events = self.authority_events()
        result = self.chat("A competing root must not be accepted.", product)
        self.assertEqual(result.get("error", {}).get("code"), "invalid_argument", result)
        self.assertEqual(self.calls, before_calls)
        self.assertEqual(self.authority_events(), before_events)
        for conversation, rows in before_rows.items():
            self.assertEqual(self.rows(conversation), rows)

    def exercise_layout_change(self, mutation):
        seed = self.chat("SOURCE_V1: preserve the durable report contract.")
        self.assertNotIn("error", seed)
        self.assertEqual(len(self.calls), 1)
        sources = [self.row(seed["user_message_id"]), self.row(seed["assistant_message_id"])]

        v1 = self.product(
            "durable-v1", 1, None, sources[0], "Write the durable version-one report."
        )
        first = self.chat("Accept durable version one.", v1)
        self.assertNotIn("error", first)
        self.assertEqual(len(self.calls), 2)
        accepted_v1 = self.row(first["user_message_id"])
        snapshot_v1 = accepted_v1["metadata"]["active_task"]
        self.assertEqual(snapshot_v1["supplied_spec"], v1[0])
        self.assertEqual(snapshot_v1["bindings"], v1[1])

        authority_v1 = self.authority_events()
        self.assertEqual(
            [event["type"] for event in authority_v1],
            ["chat.active_task.acceptance_baseline.v1", "chat.active_task.accepted.v1"],
        )
        self.assertLess(authority_v1[0]["seq"], authority_v1[1]["seq"])
        self.assertEqual(authority_v1[0]["subject_id"], self.conv)
        self.assertEqual(
            authority_v1[0]["payload"],
            {
                "schema": "loom.chat_active_task_acceptance_baseline/1",
                "completed": True,
                "legacy_rows_observed": 0,
                "legacy_acceptances_imported": 0,
                "legacy_originating_message_ids": [],
                "last_legacy_acceptance_seq": None,
                "recovery_boundary": (
                    "upgrade-time observation of retained top-level metadata; "
                    "absent or moved legacy rows are not recovered"
                ),
            },
        )
        self.assert_acceptance_event(authority_v1[1], accepted_v1, snapshot_v1, v1[0])

        competitor = self.product(
            "competing-root", 1, None, sources[0], "Replace the accepted root incorrectly."
        )
        self.assert_rejected_without_effect(competitor, [self.conv])

        destination = None
        if mutation == "metadata_relayout":
            patch = {
                "metadata": {
                    "preserved_original_metadata": accepted_v1["metadata"],
                    "client_annotation": "synthetic relayout",
                }
            }
        elif mutation == "move_accepting_row":
            destination = self.read(
                self.lib.loom_create_conversation(self.ctx, b"W1 synthetic move destination")
            )["id"]
            patch = {"conv_id": destination}
        else:
            self.fail(f"unknown mutation: {mutation}")

        self.assertEqual(
            self.lib.loom_update_message(self.ctx, accepted_v1["id"].encode(), encoded(patch)), 0
        )
        changed_v1 = self.row(accepted_v1["id"])
        if mutation == "metadata_relayout":
            self.assertEqual(
                changed_v1["metadata"]["preserved_original_metadata"], accepted_v1["metadata"]
            )
        else:
            expected = copy.deepcopy(accepted_v1)
            expected["conv_id"] = destination
            self.assertEqual(changed_v1, expected)

        self.close_runtime()
        self.open_runtime()

        self.assertEqual(self.authority_events(), authority_v1)
        self.assertEqual(self.row(accepted_v1["id"]), changed_v1)
        for source in sources:
            self.assertEqual(self.row(source["id"]), source)

        conversations = [self.conv] + ([destination] if destination else [])
        self.assert_rejected_without_effect(competitor, conversations)
        self.assertEqual(len(self.calls), 2)

        v2 = self.product(
            "durable-v2",
            2,
            v1[0]["product_ref"],
            sources[1],
            "Write the durable version-two report.",
        )
        second = self.chat("Accept the proper durable successor.", v2)
        self.assertNotIn("error", second)
        self.assertEqual(len(self.calls), 3)
        self.assertTrue(all(call["path"] == "/chat/completions" for call in self.calls))

        accepted_v2 = self.row(second["user_message_id"])
        snapshot_v2 = accepted_v2["metadata"]["active_task"]
        inherited_v1 = [
            item
            for item in snapshot_v2["inherited_source_messages"]
            if item["product_ref"] == v1[0]["product_ref"]
        ]
        self.assertEqual(len(inherited_v1), 1)
        self.assertEqual(inherited_v1[0]["source_message"], snapshot_v1["source_messages"][0])

        authority_v2 = self.authority_events()
        self.assertEqual(len(authority_v2), 3)
        self.assertEqual(authority_v2[:2], authority_v1)
        self.assert_acceptance_event(authority_v2[2], accepted_v2, snapshot_v2, v2[0])

        self.assertEqual(self.row(accepted_v1["id"]), changed_v1)
        for source in sources:
            self.assertEqual(self.row(source["id"]), source)
        print(
            f"W1 durable {mutation}: 3 local provider calls, 0 remote; "
            "2 competing-root attempts add 0 calls, rows, or acceptance events."
        )

    def test_metadata_relayout_does_not_erase_durable_acceptance(self):
        self.exercise_layout_change("metadata_relayout")

    def test_moving_accepting_row_does_not_erase_durable_acceptance(self):
        self.exercise_layout_change("move_accepting_row")


if __name__ == "__main__":
    unittest.main(verbosity=2)
