"""core.crypto <-> Loom CryptoEngine: AES-256-GCM + PBKDF2-SHA256 interop.

Both sides must produce/consume the exact same wire format:
  {"v":1,"ct":b64,"n":b64,"s":b64,"i":600000}
with ct = AES-GCM ciphertext || 16-byte tag (cryptography's AESGCM output).
"""
import base64
import json
import os
import unittest

from compat_common import CompatTestCase, tool

from core.crypto import CryptoEngine, EncryptedBlob, _HAS_CRYPTO


def b64(data: bytes) -> str:
    return base64.b64encode(data).decode()


# Command-line argv has an OS size limit; large base64 payloads (e.g. the 1 MB
# case) go through a "@path" file argument instead (tool_crypto.cpp accepts
# both forms via arg_text()).
_INLINE_LIMIT = 100_000


@unittest.skipUnless(_HAS_CRYPTO, "cryptography package not installed")
class CryptoCompatTest(CompatTestCase):

    def setUp(self):
        super().setUp()
        self.assertTrue(tool("crypto-available")["available"],
                        "loom_compat_tool was not built with OpenSSL (LOOM_WITH_OPENSSL)")

    # ── Python encrypts, C++ decrypts ───────────────────────────────────

    def test_python_encrypts_cpp_decrypts(self):
        engine = CryptoEngine()
        payloads = [
            "",
            "hello world",
            "unicode: żółć ąę 中文 \U0001F600 emoji",
            "x" * (1024 * 1024),  # 1 MB
        ]
        for password in ("hunter2", "unicode pässwörd 密码"):
            for text in payloads:
                with self.subTest(password=password, size=len(text)):
                    blob = engine.encrypt(text, password)
                    out = tool("crypto-decrypt", b64(password.encode()), self._blob_file(blob.to_json()))
                    self.assertNotIn("error", out, out)
                    decrypted = base64.b64decode(out["plaintext_b64"]).decode("utf-8")
                    self.assertEqual(decrypted, text)

    def _blob_file(self, blob_json: str) -> str:
        p = self.tmp.path / "blob.json"
        p.write_text(blob_json, encoding="utf-8")
        return "@" + str(p)

    def _arg(self, name: str, b64_text: str) -> str:
        if len(b64_text) <= _INLINE_LIMIT:
            return b64_text
        p = self.tmp.path / name
        p.write_text(b64_text, encoding="ascii")
        return "@" + str(p)

    # ── C++ encrypts, Python decrypts ───────────────────────────────────

    def test_cpp_encrypts_python_decrypts(self):
        engine = CryptoEngine()
        payloads = ["", "hello from loom", "emoji \U0001F600 unicode żółć", "y" * (1024 * 1024)]
        for password in ("hunter2", "haslo z UTF-8 żółć"):
            for text in payloads:
                with self.subTest(password=password, size=len(text)):
                    out = tool("crypto-encrypt", b64(password.encode()), self._arg("plain.b64", b64(text.encode())))
                    self.assertNotIn("error", out, out)
                    blob = EncryptedBlob.from_json(out["blob"])
                    plain = engine.decrypt(blob, password)
                    self.assertEqual(plain, text)

    # ── Known-answer test: fixed key material, exact ciphertext bytes ───

    def test_known_answer_vector(self):
        # Fixed salt/nonce/password/plaintext -> deterministic ciphertext;
        # cross-checked once against `cryptography`'s AESGCM directly.
        password = "known-answer-password"
        salt = bytes(range(32))
        nonce = bytes(range(100, 112))
        plaintext = "The quick brown fox jumps over the lazy dog"

        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
        from cryptography.hazmat.primitives import hashes
        kdf = PBKDF2HMAC(algorithm=hashes.SHA256(), length=32, salt=salt, iterations=600_000)
        key = kdf.derive(password.encode())
        ct = AESGCM(key).encrypt(nonce, plaintext.encode(), None)
        blob = EncryptedBlob(ciphertext=ct, nonce=nonce, salt=salt)

        out = tool("crypto-decrypt", b64(password.encode()), self._blob_file(blob.to_json()))
        self.assertNotIn("error", out, out)
        self.assertEqual(base64.b64decode(out["plaintext_b64"]).decode(), plaintext)

        # And the derived key itself matches bit-for-bit.
        derived = tool("crypto-derive", b64(password.encode()), b64(salt), "600000")
        self.assertEqual(derived["key_hex"], key.hex())

    # ── Failure modes ────────────────────────────────────────────────

    def test_wrong_password_fails_on_both_sides(self):
        engine = CryptoEngine()
        blob = engine.encrypt("secret", "right password")
        out = tool("crypto-decrypt", b64(b"wrong password"), self._blob_file(blob.to_json()))
        self.assertTrue(out.get("error"))
        self.assertEqual(out["code"], "crypto")

        cpp_blob_json = tool("crypto-encrypt", b64(b"right password"), b64(b"secret"))["blob"]
        with self.assertRaises(Exception):
            engine.decrypt(EncryptedBlob.from_json(cpp_blob_json), "wrong password")

    def test_tampered_ciphertext_fails_on_both_sides(self):
        engine = CryptoEngine()
        blob = engine.encrypt("secret payload", "pw")
        tampered = bytearray(blob.ciphertext)
        tampered[0] ^= 0xFF
        tampered_blob = EncryptedBlob(ciphertext=bytes(tampered), nonce=blob.nonce, salt=blob.salt)
        out = tool("crypto-decrypt", b64(b"pw"), self._blob_file(tampered_blob.to_json()))
        self.assertTrue(out.get("error"))
        self.assertEqual(out["code"], "crypto")

    def test_encrypted_blob_wire_format_is_byte_identical(self):
        """Loom's EncryptedBlob.to_dict() must use the same keys/order/defaults
        as Python's, so a Loom-written crypto.json verifier is valid Python
        EncryptedBlob JSON and vice versa."""
        out = tool("crypto-encrypt", b64(b"pw"), b64(b"hello"))
        d = json.loads(out["blob"])
        self.assertEqual(list(d.keys()), ["v", "ct", "n", "s", "i"])
        self.assertEqual(d["v"], 1)
        self.assertEqual(d["i"], 600_000)
        # Round-trips through Python's own dataclass without error.
        blob = EncryptedBlob.from_json(out["blob"])
        self.assertEqual(blob.to_dict(), d)


if __name__ == "__main__":
    unittest.main(verbosity=2)
