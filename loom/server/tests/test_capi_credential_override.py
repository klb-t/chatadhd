#!/usr/bin/env python3
"""Real C API + loopback HTTP: an explicit secret replaces its overlay entry only."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import unittest

from test_server_guest_credentials import BYOK_B, OPERATOR_A, Provider


class CapiCredentialOverride(unittest.TestCase):
    def setUp(self):
        self.lib = ctypes.CDLL(os.environ["LOOM_LIB"])
        ptr = ctypes.c_void_p
        text = ctypes.c_char_p
        for name, args, result in (
            ("loom_init_ex", [text, ctypes.POINTER(text)], ptr),
            ("loom_set_log_stderr", [ctypes.c_int], None),
            ("loom_shutdown", [ptr], None),
            ("loom_free_string", [ptr], None),
            ("loom_set_config_json", [ptr, text], ctypes.c_int),
            ("loom_set_secret", [ptr, text, text], ctypes.c_int),
            ("loom_has_secret", [ptr, text], ctypes.c_int),
            ("loom_list_secret_keys", [ptr], ptr),
            ("loom_chat_ex", [ptr, text, ptr, ptr], ptr),
        ):
            fn = getattr(self.lib, name)
            fn.argtypes, fn.restype = args, result
        self.lib.loom_set_log_stderr(0)
        self.tmp = tempfile.TemporaryDirectory(prefix="loom-capi-credentials-", dir=Path.cwd())
        self.addCleanup(self.tmp.cleanup)
        self.provider = Provider()
        self.addCleanup(self.provider.close)
        err = text()
        options = {"data_dir": self.tmp.name, "start_workers": False, "only_hosts": ["127.0.0.1"],
                   "credential_overlay": {"api_key": OPERATOR_A, "unrelated": "fake-unrelated-overlay"}}
        self.ctx = self.lib.loom_init_ex(json.dumps(options).encode(), ctypes.byref(err))
        self.assertTrue(self.ctx, err.value)
        self.addCleanup(self.lib.loom_shutdown, self.ctx)
        config = {"base_url": self.provider.base, "default_model": "mock/credentials", "stream": False,
                  "semantic_analysis": False, "auto_title": False}
        self.assertEqual(self.lib.loom_set_config_json(self.ctx, json.dumps(config).encode()), 0)

    def take(self, value):
        self.assertTrue(value)
        try:
            return json.loads(ctypes.string_at(value))
        finally:
            self.lib.loom_free_string(value)

    def test_explicit_byok_replaces_matching_overlay_on_actual_request(self):
        self.assertEqual(self.lib.loom_set_secret(self.ctx, b"api_key", BYOK_B.encode()), 0)
        result = self.take(self.lib.loom_chat_ex(self.ctx, b'{"message":"test","stream":false}', None, None))
        self.assertNotIn("error", result)
        self.assertEqual([c[1] for c in self.provider.calls], [f"Bearer {BYOK_B}"])
        self.assertEqual(self.lib.loom_has_secret(self.ctx, b"unrelated"), 1)
        self.assertEqual(self.take(self.lib.loom_list_secret_keys(self.ctx)), ["api_key"])
        saved = json.loads((Path(self.tmp.name) / "secrets.json").read_text())
        self.assertEqual(saved, {"api_key": BYOK_B})

    def test_explicit_empty_secret_does_not_fall_back_to_operator_overlay(self):
        self.assertEqual(self.lib.loom_set_secret(self.ctx, b"api_key", b""), 0)
        self.assertEqual(self.lib.loom_has_secret(self.ctx, b"api_key"), 0)
        self.assertEqual(self.lib.loom_has_secret(self.ctx, b"unrelated"), 1)
        result = self.take(self.lib.loom_chat_ex(self.ctx, b'{"message":"test","stream":false}', None, None))
        self.assertEqual(result["error"]["code"], "auth")
        self.assertEqual(self.provider.calls, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
