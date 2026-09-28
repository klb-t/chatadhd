"""Offline tests using fabricated credentials only; no API calls."""
import base64
from contextlib import redirect_stdout
from io import StringIO
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives import serialization

try:
    from . import credential_handoff as handoff
except ImportError:
    import credential_handoff as handoff


class HandoffTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.session = self.root / "session"
        self.html = self.root / "public.html"
        self.destination = self.session / "openrouter.key"
        handoff.generate(self.session, self.html, ttl_seconds=600, now=1000)
        self.metadata = json.loads((self.session / "session.json").read_text())

    def envelope(self, plaintext=b"sk-or-FAKE-TEST-ONLY", label_metadata=None):
        private = serialization.load_pem_private_key((self.session / "private.pem").read_bytes(), password=None)
        encrypted = private.public_key().encrypt(plaintext, handoff.oaep(label_metadata or self.metadata))
        return {"schema": handoff.SCHEMA, "algorithm": handoff.ALGORITHM, **self.metadata,
                "ciphertext_b64": base64.b64encode(encrypted).decode("ascii")}

    def decrypt(self, envelope=None, now=1100):
        path = self.root / "envelope.json"
        path.write_text(json.dumps(self.envelope() if envelope is None else envelope))
        return handoff.decrypt(self.session, path, now=now)

    def test_roundtrip_owner_only_and_consumes_session(self):
        result = self.decrypt()
        self.assertEqual(result["status"], "saved")
        self.assertEqual(self.destination.read_bytes(), b"sk-or-FAKE-TEST-ONLY")
        self.assertEqual(self.destination.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.destination.parent.stat().st_mode & 0o777, 0o700)
        self.assertFalse((self.session / "private.pem").exists())
        self.assertFalse((self.session / "session.json").exists())

    def test_public_html_contains_no_private_material_or_network(self):
        html = self.html.read_text()
        self.assertNotIn("PRIVATE KEY", html)
        self.assertNotIn("sk-or-FAKE", html)
        for forbidden in ("fetch(", "XMLHttpRequest", "localStorage", "sessionStorage", "https://"):
            self.assertNotIn(forbidden, html)
        self.assertIn("window.isSecureContext", html)
        self.assertIn("connect-src 'none'", html)
        self.assertIn("input.value = ''", html)
        self.assertEqual((self.session / "private.pem").stat().st_mode & 0o777, 0o600)

    def test_tampered_ciphertext_fails_without_writing(self):
        envelope = self.envelope()
        encrypted = bytearray(base64.b64decode(envelope["ciphertext_b64"]))
        encrypted[5] ^= 1
        envelope["ciphertext_b64"] = base64.b64encode(encrypted).decode()
        with self.assertRaises(ValueError):
            self.decrypt(envelope)
        self.assertFalse(self.destination.exists())
        self.assertTrue((self.session / "private.pem").exists())

    def test_session_purpose_expiry_and_extra_fields_rejected(self):
        for field, value in (("session_id", "a" * 32), ("purpose", "different-purpose"),
                             ("expires_at", 1601), ("extra", "ignored?"), ("expires_at", 1600.0)):
            with self.subTest(field=field, value=value):
                envelope = self.envelope()
                envelope[field] = value
                with self.assertRaises(ValueError):
                    self.decrypt(envelope)
        self.assertFalse(self.destination.exists())

    def test_oaep_label_binds_metadata(self):
        envelope = self.envelope(label_metadata={**self.metadata, "purpose": "wrong-label"})
        with self.assertRaises(ValueError):
            self.decrypt(envelope)

    def test_expired_session(self):
        with self.assertRaisesRegex(ValueError, "expired_session"):
            self.decrypt(now=1600)

    def test_unsafe_private_file_and_directory_permissions(self):
        envelope = self.envelope()
        for target, bad, good in ((self.session / "private.pem", 0o644, 0o600),
                                  (self.session / "session.json", 0o644, 0o600),
                                  (self.session, 0o755, 0o700)):
            target.chmod(bad)
            with self.assertRaisesRegex(ValueError, "unsafe_private"):
                self.decrypt(envelope)
            target.chmod(good)

    def test_oversized_or_invalid_plaintext(self):
        for plaintext in (b"a" * 257, b"", b"has space", b"line\n", b"\xff", "sk-or-żółw".encode()):
            with self.subTest(plaintext=plaintext):
                with self.assertRaises(ValueError):
                    self.decrypt(self.envelope(plaintext))

    def test_refuse_git_session_and_existing_output(self):
        (self.root / ".git").mkdir()
        with self.assertRaisesRegex(ValueError, "inside_git"):
            self.decrypt()
        (self.root / ".git").rmdir()
        self.destination.write_bytes(b"existing")
        with self.assertRaises(FileExistsError):
            self.decrypt()
        self.assertEqual(self.destination.read_bytes(), b"existing")

    def test_duplicate_fields_and_oversized_envelope(self):
        path = self.root / "bad.json"
        for raw in (b'{"schema":1,"schema":2}', b" " * 4097):
            path.write_bytes(raw)
            with self.assertRaises(ValueError):
                handoff.decrypt(self.session, path, now=1100)

    def test_private_symlink_rejected(self):
        private = self.session / "private.pem"
        saved = self.session / "saved.pem"
        private.rename(saved)
        private.symlink_to(saved)
        with self.assertRaises(OSError):
            self.decrypt()

    def test_cli_does_not_echo_supplied_secret_on_error(self):
        path = self.root / "bad.json"
        fabricated = "sk-or-FAKE-NOT-A-REAL-KEY"
        path.write_text(fabricated)
        output = StringIO()
        with redirect_stdout(output), patch.object(handoff.time, "time", return_value=1100):
            status = handoff.main(["decrypt", "--session-dir", str(self.session),
                                   "--envelope", str(path)])
        self.assertEqual(status, 1)
        self.assertNotIn(fabricated, output.getvalue())


if __name__ == "__main__":
    unittest.main()
