"""T5b: loom.sealed/2 and loom.credential_envelope/3 interop between Python (cryptography) and Loom.

Python re-implements both formats from their written specification (loom/crypto_suite.h) using
cryptography's Argon2id, HKDF, AES-GCM, X25519 and ML-KEM; Loom must read what Python writes and
vice versa. Fabricated secrets only.
"""
import base64
import hashlib
import json
import sys
import unittest
from pathlib import Path

from compat_common import CompatTestCase, tool

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import mlkem, x25519
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.argon2 import Argon2id
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

from core.crypto import CryptoEngine

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "loom" / "tools" / "structure"))
import credential_handoff_v3 as v3  # noqa: E402

b64 = lambda raw: base64.b64encode(raw).decode()  # noqa: E731
T, M, P = 2, 64, 2  # cheap test parameters; the shipped presets live in the pack


def py_seal_v2(pw: str, pt: bytes, cat: str, oid: str, suite="pq_hybrid", ver=1):
    salt, nonce = bytes(range(32)), bytes(range(12))
    dek = Argon2id(salt=salt, length=32, iterations=T, lanes=P, memory_cost=M).derive(pw.encode())
    key = HKDF(algorithm=hashes.SHA256(), length=32, salt=None, info=f"loom.sealed/2|key|{cat}|{oid}".encode()).derive(dek)
    ct = AESGCM(key).encrypt(nonce, pt, f"loom.sealed/2|{suite}|{cat}|{oid}|{ver}".encode())
    return json.dumps({"v": 2, "fmt": "loom.sealed/2", "suite": suite,
                       "kdf": {"alg": "argon2id", "t": T, "m": M, "p": P},
                       "s": b64(salt), "n": b64(nonce), "ct": b64(ct)})


def py_open_v2(pw: str, blob: str, cat: str, oid: str, ver=1):
    j = json.loads(blob)
    k = j["kdf"]
    dek = Argon2id(salt=base64.b64decode(j["s"]), length=32, iterations=k["t"], lanes=k["p"],
                   memory_cost=k["m"]).derive(pw.encode())
    key = HKDF(algorithm=hashes.SHA256(), length=32, salt=None, info=f"loom.sealed/2|key|{cat}|{oid}".encode()).derive(dek)
    return AESGCM(key).decrypt(base64.b64decode(j["n"]), base64.b64decode(j["ct"]),
                               f"loom.sealed/2|{j['suite']}|{cat}|{oid}|{ver}".encode())


class PqCryptoCompatTest(CompatTestCase):

    def setUp(self):
        super().setUp()
        self.assertTrue(tool("crypto-available")["available"], "loom_compat_tool built without OpenSSL")

    def test_python_seals_v2_loom_opens(self):
        blob = py_seal_v2("pässword", "zażółć v2".encode(), "conversations", "c_1")
        f = self.tmp.file_json("blob.json", json.loads(blob))
        r = tool("sealed-open", b64("pässword".encode()), f, "conversations", "c_1")
        self.assertEqual(base64.b64decode(r["plaintext_b64"]).decode(), "zażółć v2")
        bad = tool("sealed-open", b64(b"wrong"), f, "conversations", "c_1")
        self.assertTrue(bad["error"])
        wrong_ctx = tool("sealed-open", b64("pässword".encode()), f, "conversations", "c_2")
        self.assertTrue(wrong_ctx["error"])

    def test_loom_seals_v2_python_opens(self):
        r = tool("sealed2-seal", b64("pw-ö".encode()), b64("loom → python".encode()), T, M, P, "memory", "n_9")
        self.assertEqual(py_open_v2("pw-ö", r["blob"], "memory", "n_9"), "loom → python".encode())

    def test_v1_blob_from_python_still_opens_through_the_dispatcher(self):
        blob = CryptoEngine().encrypt("legacy text", "pw").to_json()
        f = self.tmp.file_json("v1.json", json.loads(blob))
        r = tool("sealed-open", b64(b"pw"), f, "anything", "any")
        self.assertEqual(base64.b64decode(r["plaintext_b64"]), b"legacy text")

    def _meta(self):
        return {"session_id": "ab" * 16, "purpose": "loom.test/3", "programme_id": "prog-ż", "expires_at": 4600}

    def test_x_wing_public_key_matches_python(self):
        seed = bytes(range(32))
        self.assertEqual(base64.b64decode(tool("xwing-public", b64(seed))["pk_b64"]), v3.xwing_public(seed))

    def test_python_encrypts_envelope_loom_opens(self):
        seed = bytes(range(1, 33))
        meta = self._meta()
        env = v3.encrypt({**meta, "public_xwing_b64": b64(v3.xwing_public(seed))}, "fabricated-credential-py")
        r = tool("envelope3-open", b64(seed), self.tmp.file_json("m.json", meta), self.tmp.file_json("e.json", env))
        self.assertEqual(base64.b64decode(r["plaintext_b64"]), b"fabricated-credential-py")

    def test_loom_encrypts_envelope_python_opens(self):
        seed = bytes(range(2, 34))
        meta = self._meta()
        r = tool("envelope3-seal", b64(v3.xwing_public(seed)), self.tmp.file_json("m.json", meta), b64(b"fabricated-credential-cpp"))
        env = json.loads(r["envelope"])
        self.assertEqual(env["schema"], "loom.credential_envelope/3")
        # Python-side decrypt of the exact C++ output (same checks as v3.decrypt without the session files).
        kem_ct = base64.b64decode(env["kem_ct_b64"])
        ss = v3.xwing_decapsulate(seed, kem_ct)
        pt = AESGCM(v3._key(ss, meta)).decrypt(base64.b64decode(env["iv_b64"]), base64.b64decode(env["ciphertext_b64"]), v3.binding(meta))
        self.assertEqual(pt, b"fabricated-credential-cpp")

    def test_envelope_with_other_metadata_is_rejected_by_loom(self):
        seed = bytes(range(3, 35))
        meta = self._meta()
        env = v3.encrypt({**meta, "public_xwing_b64": b64(v3.xwing_public(seed))}, "fabricated-credential-x")
        other = {**meta, "programme_id": "other"}
        r = tool("envelope3-open", b64(seed), self.tmp.file_json("m.json", other), self.tmp.file_json("e.json", env))
        self.assertTrue(r["error"])


class NoSecretLoggingTest(unittest.TestCase):
    """Acceptance (e): the new crypto sources never log (so they cannot log keys, passwords or plaintext)."""

    def test_new_sources_have_no_logging_calls(self):
        crypto = ROOT / "loom" / "src" / "crypto"
        for name in ("backend.cpp", "kem_xwing.cpp", "sig_hybrid.cpp", "sealed.cpp", "envelope.cpp", "hash_portable.cpp"):
            text = (crypto / name).read_text(encoding="utf-8")
            for needle in ("log::", "loom/log.h", "printf", "std::cerr", "std::cout"):
                self.assertNotIn(needle, text, f"{name} must not log ({needle})")
        for name in ("credential_handoff_v3.py",):
            text = (ROOT / "loom" / "tools" / "structure" / name).read_text(encoding="utf-8")
            self.assertNotIn("logging", text)


if __name__ == "__main__":
    unittest.main()
