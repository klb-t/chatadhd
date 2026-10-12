#!/usr/bin/env python3
"""HTTP facade: usage-guard pause/confirm over SSE, read-only usage preview, semantic confirm route."""
import json
import os
import sys
import unittest
import urllib.error

sys.path.insert(0, os.path.dirname(__file__))
from test_chat_active_task_server import ActiveTaskServerTest as Base  # noqa: E402


class UsageGuardServerTest(Base):
    def inspect(self):
        key = f"chat.send|http://127.0.0.1:{self.provider.server_port}|synthetic/local"
        return self.request("POST", "/api/usage-policy", {"action": "inspect", "baseline_key": key})

    def test_pause_preview_confirm_and_semantic_route(self):
        for index in range(3):
            self.assertEqual(self.chat(f"small {index}")[-1]["type"], "done")
        self.assertEqual(len(self.calls), 3)
        big = "x" * 60000

        before = self.inspect()
        preview = self.request("POST", "/api/chat/usage-preview", dict(self.base, message=big))
        self.assertEqual(preview["status"], "requires_confirmation")
        self.assertEqual(self.inspect(), before)
        self.assertEqual(len(self.calls), 3)

        events = self.chat(big)
        self.assertEqual(events[-1]["type"], "usage_pending")
        pending = events[-1]["usage_pending"]
        self.assertEqual(pending["status"], "requires_confirmation")
        self.assertFalse(pending["sent"])
        self.assertEqual(len(self.calls), 3)

        answer = {"approved": True, "receipt_id": pending["receipt_id"], "operation_id": pending["operation_id"], "ref": "server-test"}
        done = self.chat(big, usage_confirmation=answer)
        self.assertEqual(done[-1]["type"], "done")
        self.assertEqual(len(self.calls), 4)

        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.request("POST", "/api/semantic/usage-confirm", {"approved": True, "receipt_id": "none"})
        self.assertGreaterEqual(caught.exception.code, 400)


for name in dir(Base):
    if name.startswith("test_"):
        setattr(UsageGuardServerTest, name, getattr(UsageGuardServerTest, name)) if name == "test_pause_preview_confirm_and_semantic_route" else setattr(UsageGuardServerTest, name, None)

if __name__ == "__main__":
    unittest.main(verbosity=2)
