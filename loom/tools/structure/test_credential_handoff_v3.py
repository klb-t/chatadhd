"""Envelope v3 (X-Wing) round trips, tamper rejection, v2 still readable; fabricated credentials only."""
import base64
import json
from pathlib import Path
import tempfile
import unittest

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

import credential_handoff_v2 as v2
import credential_handoff_v3 as handoff

FIXTURES = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "crypto"


class EnvelopeV3Tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.session, self.public = self.root / "session", self.root / "public.json"
        handoff.generate(self.session, self.public, "fixture-separate-programme", ttl_seconds=3600, now=1000)
        self.config = json.loads(self.public.read_text())

    def write(self, envelope):
        path = self.root / "envelope.json"
        path.write_text(json.dumps(envelope))
        return path

    def test_round_trip(self):
        out = handoff.decrypt(self.session, self.write(handoff.encrypt(self.config, "fabricated-credential-1")), now=1100)
        self.assertEqual(out["status"], "saved")
        self.assertEqual((self.session / "openrouter-new.key").read_bytes(), b"fabricated-credential-1")
        self.assertFalse((self.session / "xwing.seed").exists())

    def test_public_config_has_no_private_material(self):
        self.assertEqual(len(base64.b64decode(self.config["public_xwing_b64"])), 1216)
        self.assertNotIn(base64.b64encode((self.session / "xwing.seed").read_bytes()).decode(), self.public.read_text())
        self.assertEqual(set(self.config), {"session_id", "purpose", "programme_id", "expires_at", "public_xwing_b64"})

    def test_tamper_is_rejected_in_every_field(self):
        env = handoff.encrypt(self.config, "fabricated-credential-2")
        for field in ("kem_ct_b64", "iv_b64", "ciphertext_b64"):
            raw = bytearray(base64.b64decode(env[field]))
            raw[len(raw) // 2] ^= 1
            bad = {**env, field: base64.b64encode(bytes(raw)).decode()}
            with self.assertRaises(Exception, msg=field):
                handoff.decrypt(self.session, self.write(bad), now=1100)
        for field, value in (("programme_id", "other"), ("session_id", "0" * 32), ("expires_at", None), ("purpose", "x")):
            with self.assertRaises(ValueError, msg=field):
                handoff.decrypt(self.session, self.write({**env, field: value}), now=1100)
        self.assertFalse((self.session / "openrouter-new.key").exists())

    def test_binding_mismatch_is_rejected(self):
        # Encrypted under a different binding (different programme) but labelled with this session's metadata.
        forged = {**self.config, "programme_id": "someone-else"}
        env = handoff.encrypt(forged, "fabricated-credential-3")
        env["programme_id"] = self.config["programme_id"]
        with self.assertRaises(Exception):
            handoff.decrypt(self.session, self.write(env), now=1100)

    def test_expired_session(self):
        env = handoff.encrypt(self.config, "x-credential")
        with self.assertRaises(ValueError):
            handoff.decrypt(self.session, self.write(env), now=10 ** 12)

    def test_x_wing_draft_vectors(self):
        for v in json.loads((FIXTURES / "xwing_draft_vectors.json").read_text())["vectors"]:
            sk = bytes.fromhex(v["sk"])
            self.assertEqual(handoff.xwing_public(sk).hex(), v["pk"])
            self.assertEqual(handoff.xwing_decapsulate(sk, bytes.fromhex(v["ct"])).hex(), v["ss"])

    def test_v2_envelope_still_decrypts_through_v3_entry_point(self):
        v2_session, v2_html = self.root / "s2", self.root / "p2.html"
        v2.generate(v2_session, v2_html, "fixture-separate-programme", now=1000)
        metadata = json.loads((v2_session / "session.json").read_text())
        private = serialization.load_pem_private_key((v2_session / "private.pem").read_bytes(), password=None)
        aes = AESGCM.generate_key(bit_length=256); nonce = bytes(range(12))
        env = {"schema": v2.SCHEMA, "algorithm": v2.ALGORITHM, **metadata,
               "wrapped_key_b64": base64.b64encode(private.public_key().encrypt(aes, v2.oaep(metadata))).decode(),
               "iv_b64": base64.b64encode(nonce).decode(),
               "ciphertext_b64": base64.b64encode(AESGCM(aes).encrypt(nonce, b"fabricated-v2", v2.binding(metadata))).decode()}
        out = handoff.decrypt(v2_session, self.write(env), now=1100)
        self.assertEqual(out["status"], "saved")
        self.assertEqual((v2_session / "openrouter-new.key").read_bytes(), b"fabricated-v2")


if __name__ == "__main__":
    unittest.main()
