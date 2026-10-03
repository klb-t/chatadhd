"""
ChatADHD v0.07.01 - Zero-Knowledge Encryption Engine

Provides AES-256-GCM encryption with key derivation from a user-supplied
password.  The server (or anyone without the password) never sees plaintext.

Threat model:
  - Protects data at rest (DB, sync blobs, exports).
  - Does NOT protect against a compromised runtime (in-memory keys).
  - Does NOT protect LLM provider traffic (they see plaintext by design).

Dependencies: ``cryptography`` (pip install cryptography).
Falls back to a no-op stub if the library is unavailable, so the rest of
the app keeps working without encryption support.
"""
import base64
import json
import logging
import os
from dataclasses import dataclass
from typing import Optional

log = logging.getLogger(__name__)

try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
    from cryptography.hazmat.primitives import hashes
    _HAS_CRYPTO = True
except ImportError:
    _HAS_CRYPTO = False
    log.warning("cryptography package not installed — encryption disabled")


# PBKDF2 parameters.  OWASP 2024 recommends ≥600 000 for PBKDF2-SHA256.
_KDF_ITERATIONS = 600_000
_SALT_BYTES = 32
_KEY_BYTES = 32      # AES-256
_NONCE_BYTES = 12    # GCM standard


@dataclass(frozen=True)
class EncryptedBlob:
    """Serialisable container for an encrypted payload."""
    ciphertext: bytes
    nonce: bytes
    salt: bytes
    iterations: int = _KDF_ITERATIONS
    version: int = 1

    def to_dict(self) -> dict:
        return {
            "v": self.version,
            "ct": base64.b64encode(self.ciphertext).decode(),
            "n": base64.b64encode(self.nonce).decode(),
            "s": base64.b64encode(self.salt).decode(),
            "i": self.iterations,
        }

    @classmethod
    def from_dict(cls, d: dict) -> "EncryptedBlob":
        return cls(
            ciphertext=base64.b64decode(d["ct"]),
            nonce=base64.b64decode(d["n"]),
            salt=base64.b64decode(d["s"]),
            iterations=d.get("i", _KDF_ITERATIONS),
            version=d.get("v", 1),
        )

    def to_json(self) -> str:
        return json.dumps(self.to_dict())

    @classmethod
    def from_json(cls, s: str) -> "EncryptedBlob":
        return cls.from_dict(json.loads(s))


class CryptoEngine:
    """
    Encrypt / decrypt UTF-8 strings with a password-derived AES-256-GCM key.

    Usage::

        engine = CryptoEngine()
        blob = engine.encrypt("secret text", password="hunter2")
        plain = engine.decrypt(blob, password="hunter2")
    """

    @staticmethod
    def available() -> bool:
        return _HAS_CRYPTO

    def _derive_key(self, password: str, salt: bytes,
                    iterations: int = _KDF_ITERATIONS) -> bytes:
        if not _HAS_CRYPTO:
            raise RuntimeError("cryptography library is required for encryption")
        kdf = PBKDF2HMAC(
            algorithm=hashes.SHA256(),
            length=_KEY_BYTES,
            salt=salt,
            iterations=iterations,
        )
        return kdf.derive(password.encode("utf-8"))

    def encrypt(self, plaintext: str, password: str) -> EncryptedBlob:
        salt = os.urandom(_SALT_BYTES)
        key = self._derive_key(password, salt)
        nonce = os.urandom(_NONCE_BYTES)
        aesgcm = AESGCM(key)
        ct = aesgcm.encrypt(nonce, plaintext.encode("utf-8"), None)
        return EncryptedBlob(ciphertext=ct, nonce=nonce, salt=salt)

    def decrypt(self, blob: EncryptedBlob, password: str) -> str:
        key = self._derive_key(password, blob.salt, blob.iterations)
        aesgcm = AESGCM(key)
        plaintext = aesgcm.decrypt(blob.nonce, blob.ciphertext, None)
        return plaintext.decode("utf-8")


# Module-level singleton.
crypto = CryptoEngine()
