"""T5b F3 + F4: secrets.json is created owner-only from the first byte; Google Speech key stays out of the URL."""
import json
import os
import stat
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from compat_common import CompatTestCase  # noqa: F401  (sets sys.path to the repo root)
from engine import providers
from engine.config import Secrets


class SecretsPermsTest(unittest.TestCase):

    def test_tmp_is_created_0600_even_with_permissive_umask(self):
        with tempfile.TemporaryDirectory() as d:
            old = os.umask(0o000)
            try:
                modes = []
                real_replace = Path.replace

                def spy(self_, target):
                    modes.append(stat.S_IMODE(os.stat(self_).st_mode))
                    return real_replace(self_, target)

                with mock.patch.object(Path, "replace", spy):
                    s = Secrets(Path(d) / "secrets.json")
                    s.set("api_key", "sk-żółć")
                    s.save()
            finally:
                os.umask(old)
            self.assertEqual(modes, [0o600], "temp file was group/world accessible before the rename")
            self.assertEqual(stat.S_IMODE(os.stat(Path(d) / "secrets.json").st_mode), 0o600)

    def test_bytes_identical_to_json_dumps(self):
        with tempfile.TemporaryDirectory() as d:
            s = Secrets(Path(d) / "secrets.json")
            s.set("api_key", "sk-żółć")
            s.set("github_token", "ghp")
            s.save()
            raw = (Path(d) / "secrets.json").read_bytes()
            self.assertEqual(raw, json.dumps(s.all(), indent=2, ensure_ascii=False).encode("utf-8"))

    def test_stale_permissive_tmp_is_tightened(self):
        with tempfile.TemporaryDirectory() as d:
            stale = Path(d) / "secrets.tmp"
            stale.write_text("old")
            os.chmod(stale, 0o666)
            s = Secrets(Path(d) / "secrets.json")
            s.set("k", "v")
            s.save()
            self.assertEqual(stat.S_IMODE(os.stat(Path(d) / "secrets.json").st_mode), 0o600)


class GoogleSpeechKeyTest(unittest.TestCase):

    def _provider(self):
        p = providers.GoogleSpeechASR.__new__(providers.GoogleSpeechASR)
        p.api_key = "AIzaSECRETKEY"
        return p

    def test_key_in_header_not_url(self):
        seen = {}

        def fake_post(url, **kw):
            seen["url"], seen["headers"] = url, kw.get("headers")
            raise providers.requests.exceptions.ConnectionError(f"boom {url}?key=AIzaSECRETKEY")

        p = self._provider()
        with mock.patch.object(providers.requests, "post", fake_post):
            with self.assertRaises(providers.ProviderError) as cm:
                p.transcribe_bytes(b"\x00\x01", "wav", "pl-PL")
        self.assertNotIn("key=", seen["url"])
        self.assertNotIn("AIzaSECRETKEY", seen["url"])
        self.assertEqual(seen["headers"], {"X-Goog-Api-Key": "AIzaSECRETKEY"})
        self.assertNotIn("AIzaSECRETKEY", str(cm.exception))

    def test_redact(self):
        e = Exception("GET https://x/y?key=abc123&z=1 failed; token abc123")
        out = providers._redact(e, "abc123")
        self.assertNotIn("abc123", out)
        self.assertIn("z=1", out)


if __name__ == "__main__":
    unittest.main()
