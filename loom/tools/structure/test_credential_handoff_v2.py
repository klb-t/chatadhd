"""Hybrid WebCrypto/Python interop uses fabricated credentials; no provider calls."""
import base64
from contextlib import redirect_stdout
from io import StringIO
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

import credential_handoff_v2 as handoff


class HybridHandoffTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir="/tmp")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.session, self.html = self.root / "session", self.root / "public.html"
        handoff.generate(self.session, self.html, "fixture-separate-programme", now=1000)
        self.metadata = json.loads((self.session / "session.json").read_text())
        self.destination = self.session / "openrouter-new.key"

    def envelope(self, plaintext=b"fabricated-credential-for-test"):
        private = serialization.load_pem_private_key((self.session / "private.pem").read_bytes(), password=None)
        aes = AESGCM.generate_key(bit_length=256); nonce = bytes(range(12))
        ciphertext = AESGCM(aes).encrypt(nonce, plaintext, handoff.binding(self.metadata))
        wrapped = private.public_key().encrypt(aes, handoff.oaep(self.metadata))
        return {"schema": handoff.SCHEMA, "algorithm": handoff.ALGORITHM, **self.metadata,
                "wrapped_key_b64": base64.b64encode(wrapped).decode(), "iv_b64": base64.b64encode(nonce).decode(),
                "ciphertext_b64": base64.b64encode(ciphertext).decode()}

    def decrypt(self, envelope, **kwargs):
        path = self.root / "envelope.json"; path.write_text(json.dumps(envelope))
        return handoff.decrypt(self.session, path, now=1100, **kwargs)

    def test_real_webcrypto_javascript_roundtrip_consumes_private_session(self):
        # Execute the exact delivered script with DOM stubs, using Node's real
        # WebCrypto; decrypt its output with independent Python cryptography.
        script = self.html.read_text().split("<script>", 1)[1].split("</script>", 1)[0]
        javascript = r'''
const { webcrypto } = require('node:crypto'); const vm=require('node:vm');
let data=''; process.stdin.on('data',d=>data+=d); process.stdin.on('end',async()=>{
  const input=JSON.parse(data), nodes={};
  const document={getElementById(id){return nodes[id] ||= {disabled:true,value:'',textContent:'',addEventListener(){}};}};
  const context=vm.createContext({crypto:webcrypto,window:{isSecureContext:true,crypto:webcrypto},document,TextEncoder,Uint8Array,atob,btoa,Date});
  vm.runInContext(input.script,context);
  const result=await vm.runInContext("encryptCredential('fabricated-webcrypto-test-credential',config)",context);
  process.stdout.write(JSON.stringify(result));
});
'''
        raw = subprocess.check_output(["node", "-e", javascript], input=json.dumps({"script": script}).encode())
        result = self.decrypt(json.loads(raw))
        self.assertEqual(result["status"], "saved")
        self.assertEqual(self.destination.read_bytes(), b"fabricated-webcrypto-test-credential")
        self.assertEqual(self.destination.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.session.stat().st_mode & 0o777, 0o700)
        self.assertFalse((self.session / "private.pem").exists())
        with self.assertRaises(FileNotFoundError):
            self.decrypt(json.loads(raw))

    def test_nonce_bound_no_expiry_and_configurable_larger_credentials(self):
        self.assertIsNone(self.metadata["expires_at"])
        plaintext = b"fabricated-" + b"x" * 1024
        self.decrypt(self.envelope(plaintext))
        self.assertEqual(self.destination.read_bytes(), plaintext)

    def test_aes_ciphertext_and_wrapped_key_tampering_rejected_without_secret_output(self):
        for field in ("ciphertext_b64", "wrapped_key_b64", "iv_b64"):
            envelope = self.envelope(); value = bytearray(base64.b64decode(envelope[field])); value[0] ^= 1
            envelope[field] = base64.b64encode(value).decode()
            with self.assertRaises(Exception):
                self.decrypt(envelope)
        self.assertFalse(self.destination.exists())
        self.assertTrue((self.session / "private.pem").exists())

    def test_session_programme_and_expiry_tampering_rejected(self):
        for key, value in (("session_id", "a" * 32), ("programme_id", "old-programme"), ("expires_at", 999999)):
            envelope = self.envelope(); envelope[key] = value
            with self.assertRaises(ValueError):
                self.decrypt(envelope)

    def test_configurable_expiry_is_enforced(self):
        session, html = self.root / "expiring", self.root / "expiring.html"
        handoff.generate(session, html, "fixture", ttl_seconds=1, now=1000)
        envelope = self.root / "irrelevant.json"; envelope.write_text("{}")
        with self.assertRaisesRegex(ValueError, "expired_session"):
            handoff.decrypt(session, envelope, now=1001)

    def test_public_html_contains_no_private_material_no_network_or_persistent_storage(self):
        html = self.html.read_text()
        for forbidden in ("PRIVATE KEY", "fetch(", "XMLHttpRequest", "localStorage", "sessionStorage", "https://"):
            self.assertNotIn(forbidden, html)
        self.assertIn("connect-src 'none'", html)
        self.assertIn("window.isSecureContext", html)
        self.assertIn("input.value=''", html)
        self.assertEqual((self.session / "private.pem").stat().st_mode & 0o777, 0o600)

    def test_private_permissions_parser_budget_and_existing_destination_preserved(self):
        envelope = self.envelope(); private = self.session / "private.pem"
        private.chmod(0o644)
        with self.assertRaises(ValueError):
            self.decrypt(envelope)
        private.chmod(0o600)
        with self.assertRaises(ValueError):
            self.decrypt(envelope, max_envelope_bytes=100)
        self.destination.write_bytes(b"existing-private-fixture")
        with self.assertRaises(FileExistsError):
            self.decrypt(envelope)
        self.assertEqual(self.destination.read_bytes(), b"existing-private-fixture")

    def test_cli_error_does_not_print_credential_or_private_material(self):
        path = self.root / "bad.json"; path.write_text("fabricated-sensitive-test-input")
        output = StringIO()
        with redirect_stdout(output):
            status = handoff.main(["decrypt", "--session-dir", str(self.session), "--envelope", str(path)])
        self.assertEqual(status, 1)
        self.assertNotIn("fabricated-sensitive-test-input", output.getvalue())
        self.assertNotIn("PRIVATE KEY", output.getvalue())


if __name__ == "__main__":
    unittest.main()
