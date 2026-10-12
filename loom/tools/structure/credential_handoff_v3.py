"""Post-quantum hybrid credential handoff, envelope v3 (replaces RSA-OAEP v2; v2 stays readable).

KEM: X-Wing (X25519 + ML-KEM-768, draft-connolly-cfrg-xwing-kem) with a 32-byte secret seed and a
1216-byte public key. Key: HKDF-SHA256(ss, info = schema || binding). AEAD: AES-256-GCM, AAD = the same
binding(metadata) as v2. The native implementation (loom::cs::envelope_v3_*) produces and opens the same
bytes. The browser form is NOT part of this module: WebCrypto has no ML-KEM, so the HTML needs an embedded,
audited ML-KEM implementation (open item, see docs/STATE.md); the public session config written here is
what such a form would consume. No network call, environment-secret lookup or provider integration.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import secrets
import time

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import mlkem, x25519
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

try:
    from . import credential_handoff as storage
    from . import credential_handoff_v2 as v2
except ImportError:
    import credential_handoff as storage
    import credential_handoff_v2 as v2


PURPOSE = "loom.openrouter.separate-programme-api-key-handoff/3"
SCHEMA = "loom.credential_envelope/3"
ALGORITHM = "XWING-X25519-MLKEM768+HKDF-SHA256+A256GCM"
XWING_LABEL = bytes.fromhex("5c2e2f2f5e5c")  # "\.//^\"
PK_BYTES, CT_BYTES = 1216, 1120
binding = v2.binding


def _expand(sk):
    if len(sk) != 32:
        raise ValueError("invalid_xwing_secret")
    e = hashlib.shake_256(sk).digest(96)
    return mlkem.MLKEM768PrivateKey.from_seed_bytes(e[:64]), x25519.X25519PrivateKey.from_private_bytes(e[64:96])


def _combiner(ss_m, ss_x, ct_x, pk_x):
    return hashlib.sha3_256(ss_m + ss_x + ct_x + pk_x + XWING_LABEL).digest()


def xwing_public(sk):
    m, x = _expand(sk)
    return m.public_key().public_bytes_raw() + x.public_key().public_bytes_raw()


def xwing_encapsulate(pk):
    if len(pk) != PK_BYTES:
        raise ValueError("invalid_xwing_public")
    ss_m, ct_m = mlkem.MLKEM768PublicKey.from_public_bytes(pk[:1184]).encapsulate()
    ek = x25519.X25519PrivateKey.generate()
    ct_x = ek.public_key().public_bytes_raw()
    ss_x = ek.exchange(x25519.X25519PublicKey.from_public_bytes(pk[1184:]))
    return _combiner(ss_m, ss_x, ct_x, pk[1184:]), ct_m + ct_x


def xwing_decapsulate(sk, ct):
    if len(ct) != CT_BYTES:
        raise ValueError("invalid_xwing_ciphertext")
    m, x = _expand(sk)
    pk_x = x.public_key().public_bytes_raw()
    ss_m = m.decapsulate(ct[:1088])
    ss_x = x.exchange(x25519.X25519PublicKey.from_public_bytes(ct[1088:]))
    return _combiner(ss_m, ss_x, ct[1088:], pk_x)


def _key(ss, metadata):
    return HKDF(algorithm=hashes.SHA256(), length=32, salt=None,
                info=SCHEMA.encode() + binding(metadata)).derive(ss)


def encrypt(public_config, credential):
    """What the form does: public_config is the dict written by generate() (public_xwing_b64 + metadata)."""
    metadata = {k: public_config[k] for k in ("session_id", "purpose", "programme_id", "expires_at")}
    data = credential.encode("ascii") if isinstance(credential, str) else bytes(credential)
    if not data or any(b < 33 or b > 126 for b in data):
        raise ValueError("invalid_credential")
    ss, kem_ct = xwing_encapsulate(base64.b64decode(public_config["public_xwing_b64"], validate=True))
    iv = secrets.token_bytes(12)
    ct = AESGCM(_key(ss, metadata)).encrypt(iv, data, binding(metadata))
    return {"schema": SCHEMA, "algorithm": ALGORITHM, **metadata,
            "kem_ct_b64": base64.b64encode(kem_ct).decode(), "iv_b64": base64.b64encode(iv).decode(),
            "ciphertext_b64": base64.b64encode(ct).decode()}


def generate(session_dir, public_path, programme_id, ttl_seconds=None, now=None):
    if not isinstance(programme_id, str) or not programme_id:
        raise ValueError("programme_id_required")
    if ttl_seconds is not None and (type(ttl_seconds) is not int or ttl_seconds <= 0):
        raise ValueError("positive_lifetime_or_none_required")
    session = storage.private_directory(session_dir, create=True)
    metadata = {"session_id": secrets.token_hex(16), "purpose": PURPOSE, "programme_id": programme_id,
                "expires_at": None if ttl_seconds is None else int(time.time() if now is None else now) + ttl_seconds}
    seed = secrets.token_bytes(32)
    storage.write_new(session / "xwing.seed", seed)
    storage.write_new(session / "session.json", json.dumps(metadata).encode())
    config = {**metadata, "public_xwing_b64": base64.b64encode(xwing_public(seed)).decode("ascii")}
    public = Path(public_path).absolute()
    storage.write_new(public, json.dumps(config, ensure_ascii=False).encode("utf-8"))
    return {"status": "ready", "session_dir": str(session), "public_path": str(public), **metadata}


def decrypt(session_dir, envelope_path, now=None, max_envelope_bytes=1048576):
    """Opens a v3 envelope with a v3 session; a v2 envelope is delegated to credential_handoff_v2."""
    if type(max_envelope_bytes) is not int or max_envelope_bytes <= 0:
        raise ValueError("positive_parser_budget_required")
    with Path(envelope_path).open("rb") as source:
        raw = source.read(max_envelope_bytes + 1)
    if len(raw) > max_envelope_bytes:
        raise ValueError("envelope_exceeds_configured_parser_budget")
    envelope = storage.strict_json(raw)
    if isinstance(envelope, dict) and envelope.get("schema") == v2.SCHEMA:
        return v2.decrypt(session_dir, envelope_path, now=now, max_envelope_bytes=max_envelope_bytes)
    session = storage.private_directory(session_dir)
    metadata = storage.strict_json(storage.read_private(session / "session.json", max_envelope_bytes))
    if not isinstance(metadata, dict) or set(metadata) != {"session_id", "purpose", "programme_id", "expires_at"}:
        raise ValueError("invalid_session")
    sid = metadata["session_id"]
    if (metadata["purpose"] != PURPOSE or not isinstance(sid, str) or len(sid) != 32
            or any(c not in "0123456789abcdef" for c in sid)
            or not isinstance(metadata["programme_id"], str) or not metadata["programme_id"]):
        raise ValueError("invalid_session")
    expires = metadata["expires_at"]
    if expires is not None and (type(expires) is not int or (time.time() if now is None else now) >= expires):
        raise ValueError("expired_session")
    fields = {"schema", "algorithm", "session_id", "purpose", "programme_id", "expires_at",
              "kem_ct_b64", "iv_b64", "ciphertext_b64"}
    if not isinstance(envelope, dict) or set(envelope) != fields:
        raise ValueError("invalid_envelope")
    if envelope["schema"] != SCHEMA or envelope["algorithm"] != ALGORITHM:
        raise ValueError("invalid_algorithm")
    if any(envelope[k] != v or type(envelope[k]) is not type(v) for k, v in metadata.items()):
        raise ValueError("envelope_session_mismatch")
    kem_ct = base64.b64decode(envelope["kem_ct_b64"], validate=True)
    iv = base64.b64decode(envelope["iv_b64"], validate=True)
    ct = base64.b64decode(envelope["ciphertext_b64"], validate=True)
    if len(kem_ct) != CT_BYTES or len(iv) != 12 or len(ct) <= 16:
        raise ValueError("invalid_algorithm_encoding")
    seed = storage.read_private(session / "xwing.seed", 64)
    plaintext = AESGCM(_key(xwing_decapsulate(seed, kem_ct), metadata)).decrypt(iv, ct, binding(metadata))
    if not plaintext or any(b < 33 or b > 126 for b in plaintext):
        raise ValueError("invalid_credential")
    destination = session / "openrouter-new.key"
    storage.write_new(destination, plaintext)
    (session / "xwing.seed").unlink()
    (session / "session.json").unlink()
    return {"status": "saved", "key_file": str(destination), "session_consumed": True}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    gen = commands.add_parser("generate")
    gen.add_argument("--session-dir", required=True); gen.add_argument("--public", required=True)
    gen.add_argument("--programme-id", required=True); gen.add_argument("--ttl-seconds", type=int)
    dec = commands.add_parser("decrypt")
    dec.add_argument("--session-dir", required=True); dec.add_argument("--envelope", required=True)
    dec.add_argument("--max-envelope-bytes", type=int, default=1048576)
    args = parser.parse_args(argv)
    try:
        result = (generate(args.session_dir, args.public, args.programme_id, args.ttl_seconds) if args.command == "generate"
                  else decrypt(args.session_dir, args.envelope, max_envelope_bytes=args.max_envelope_bytes))
    except Exception:
        print(json.dumps({"status": "error", "reason": "handoff_validation_failed"})); return 1
    print(json.dumps(result)); return 0


if __name__ == "__main__":
    raise SystemExit(main())
