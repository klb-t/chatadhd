"""Offline, disposable public-key handoff. Never submits or prints a credential.

Generate a public HTML form; keep its private session outside Git and backups.
The recipient encrypts in a secure browser context and returns only the JSON
envelope. Losing the disposable private session requires generating a new form.
Requires cryptography; no network, environment-secret, or API integration.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import secrets
import stat
import time

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

PURPOSE = "loom.openrouter.api-key-handoff/1"
SCHEMA = "loom.credential_envelope/1"
ALGORITHM = "RSA-OAEP-256"
MAX_KEY_BYTES = 256  # RSA-3072 / SHA-256 permits 318; deliberately smaller.
REPO = Path(__file__).resolve().parents[3]


def outside_git(path):
    path = Path(path).absolute()
    resolved = path.resolve()
    if path != resolved or resolved == REPO or REPO in resolved.parents:
        raise ValueError("private_path_not_allowed")
    if any((p / ".git").exists() for p in (resolved, *resolved.parents)):
        raise ValueError("private_path_inside_git")
    return resolved


def private_directory(path, create=False):
    path = outside_git(path)
    if create:
        path.mkdir(mode=0o700, parents=True, exist_ok=False)
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or stat.S_IMODE(info.st_mode) != 0o700 or info.st_uid != os.getuid():
        raise ValueError("unsafe_private_directory")
    return path


def write_new(path, data):
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600), "wb") as out:
        out.write(data)
        out.flush()
        os.fsync(out.fileno())


def read_private(path, maximum):
    with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), "rb") as src:
        info = os.fstat(src.fileno())
        if not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode) != 0o600 or info.st_uid != os.getuid():
            raise ValueError("unsafe_private_file")
        data = src.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError("file_too_large")
    return data


def strict_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate_json_field")
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=pairs)


def label(metadata):
    return f"{metadata['purpose']}\n{metadata['session_id']}\n{metadata['expires_at']}".encode("utf-8")


def oaep(metadata):
    return padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=label(metadata))


HTML = r'''<!doctype html>
<html lang="pl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'none'; form-action 'none'; base-uri 'none'">
<title>Zaszyfrowane przekazanie klucza OpenRouter</title>
<style>body{font:18px system-ui;max-width:38em;margin:2em auto;padding:0 1em;background:#f5f6fa;color:#172033}input,button{font:inherit;box-sizing:border-box;width:100%;padding:.8em;margin:.5em 0}#status{overflow-wrap:anywhere}</style>
<h1>Przekaż klucz OpenRouter</h1>
<p>Wklej klucz OpenRouter → Zaszyfruj i pobierz → dołącz zaszyfrowany plik do rozmowy.</p>
<p>Ta strona szyfruje lokalnie i niczego nie wysyła. Do rozmowy dołącz tylko pobrany plik JSON. Nie wklejaj tam samego klucza.</p>
<p id="session"></p><label for="key">Klucz OpenRouter</label>
<input id="key" type="password" maxlength="256" autocomplete="off" autocapitalize="none" spellcheck="false" disabled>
<button id="encrypt" type="button" disabled>Zaszyfruj i pobierz</button>
<p id="status" role="status" aria-live="polite"></p>
<script>
'use strict';
const config = __PUBLIC_CONFIG__;
const input = document.getElementById('key'), button = document.getElementById('encrypt');
const status = document.getElementById('status');
document.getElementById('session').textContent = 'Sesja: ' + config.session_id + '. Ważna do: ' + new Date(config.expires_at * 1000).toLocaleString();
const supported = window.isSecureContext && window.crypto && window.crypto.subtle;
if (!supported) {
  status.textContent = 'Nie można szyfrować: przeglądarka nie udostępnia WebCrypto w bezpiecznym kontekście. Podgląd HTML może go nie obsługiwać. Nie wpisuj klucza; potrzebny jest obsługiwany bezpieczny kontekst przeglądarki.';
} else if (Date.now() >= config.expires_at * 1000) {
  status.textContent = 'Sesja wygasła. Potrzebny jest nowy formularz.';
} else { input.disabled = false; button.disabled = false; }
button.addEventListener('click', async () => {
  button.disabled = true;
  let bytes;
  try {
    if (!supported || Date.now() >= config.expires_at * 1000) throw new Error('expired');
    bytes = new TextEncoder().encode(input.value);
    if (!bytes.length || bytes.length > 256 || /[^\x21-\x7e]/u.test(input.value)) {
      status.textContent = 'Klucz musi mieć 1–256 znaków ASCII, bez spacji i znaków sterujących.';
      return;
    }
    const spki = Uint8Array.from(atob(config.public_spki_b64), c => c.charCodeAt(0));
    const key = await crypto.subtle.importKey('spki', spki, {name:'RSA-OAEP', hash:'SHA-256'}, false, ['encrypt']);
    const label = new TextEncoder().encode(config.purpose + '\n' + config.session_id + '\n' + config.expires_at);
    const encrypted = new Uint8Array(await crypto.subtle.encrypt({name:'RSA-OAEP', label}, key, bytes));
    const envelope = {schema:'loom.credential_envelope/1', algorithm:'RSA-OAEP-256', session_id:config.session_id, purpose:config.purpose, expires_at:config.expires_at, ciphertext_b64:btoa(String.fromCharCode(...encrypted))};
    input.value = '';
    const url = URL.createObjectURL(new Blob([JSON.stringify(envelope, null, 2)], {type:'application/json'}));
    const link = document.createElement('a'); link.href = url; link.download = 'openrouter-encrypted-' + config.session_id + '.json';
    document.body.appendChild(link); link.click(); link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 30000);
    status.textContent = 'Plik zaszyfrowany. Dołącz pobrany plik JSON do rozmowy. Jeżeli przeglądarka zablokowała pobieranie, użyj obsługiwanej przeglądarki.';
  } catch (_) {
    input.value = '';
    status.textContent = 'Szyfrowanie nie powiodło się lub sesja wygasła. Nie wysyłaj klucza jako tekstu.';
  } finally {
    if (bytes) bytes.fill(0);
    button.disabled = !supported || Date.now() >= config.expires_at * 1000;
  }
});
</script></html>
'''


def generate(session_dir, html_path, ttl_seconds=3600, now=None):
    if type(ttl_seconds) is not int or not 60 <= ttl_seconds <= 86400:
        raise ValueError("invalid_session_lifetime")
    session = private_directory(session_dir, create=True)
    metadata = {"session_id": secrets.token_hex(16), "purpose": PURPOSE,
                "expires_at": int(time.time() if now is None else now) + ttl_seconds}
    key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    write_new(session / "private.pem", key.private_bytes(serialization.Encoding.PEM,
              serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    write_new(session / "session.json", json.dumps(metadata).encode())
    public = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    config = {**metadata, "public_spki_b64": base64.b64encode(public).decode("ascii")}
    html_path = Path(html_path).absolute()
    write_new(html_path, HTML.replace("__PUBLIC_CONFIG__", json.dumps(config)).encode("utf-8"))
    return {"status": "ready", "session_dir": str(session), "html_path": str(html_path), **metadata}


def decrypt(session_dir, envelope_path, now=None):
    session = private_directory(session_dir)
    metadata = strict_json(read_private(session / "session.json", 1024))
    if not isinstance(metadata, dict) or set(metadata) != {"session_id", "purpose", "expires_at"}:
        raise ValueError("invalid_session")
    if (metadata["purpose"] != PURPOSE or type(metadata["expires_at"]) is not int
            or not isinstance(metadata["session_id"], str) or len(metadata["session_id"]) != 32
            or any(c not in "0123456789abcdef" for c in metadata["session_id"])):
        raise ValueError("invalid_session")
    if (time.time() if now is None else now) >= metadata["expires_at"]:
        raise ValueError("expired_session")
    with Path(envelope_path).open("rb") as src:
        raw = src.read(4097)
    if len(raw) > 4096:
        raise ValueError("envelope_too_large")
    envelope = strict_json(raw)
    if not isinstance(envelope, dict) or set(envelope) != {"schema", "algorithm", "session_id", "purpose", "expires_at", "ciphertext_b64"}:
        raise ValueError("invalid_envelope_fields")
    if envelope["schema"] != SCHEMA or envelope["algorithm"] != ALGORITHM:
        raise ValueError("invalid_envelope_algorithm")
    if type(envelope["expires_at"]) is not int or any(envelope[k] != v for k, v in metadata.items()):
        raise ValueError("envelope_session_mismatch")
    encrypted = base64.b64decode(envelope["ciphertext_b64"], validate=True)
    if len(encrypted) != 384:
        raise ValueError("invalid_ciphertext_size")
    private = serialization.load_pem_private_key(read_private(session / "private.pem", 4096), password=None)
    if not isinstance(private, rsa.RSAPrivateKey) or private.key_size != 3072:
        raise ValueError("invalid_private_key")
    plaintext = private.decrypt(encrypted, oaep(metadata))
    if not 1 <= len(plaintext) <= MAX_KEY_BYTES or any(c < 33 or c > 126 for c in plaintext):
        raise ValueError("invalid_credential")
    destination = session / "openrouter.key"
    write_new(destination, plaintext)
    # Consume the private decryption material only after durable credential output.
    (session / "private.pem").unlink()
    (session / "session.json").unlink()
    return {"status": "saved", "key_file": str(destination), "session_consumed": True}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    gen = commands.add_parser("generate")
    gen.add_argument("--session-dir", required=True)
    gen.add_argument("--html", required=True)
    gen.add_argument("--ttl-seconds", type=int, default=3600)
    dec = commands.add_parser("decrypt")
    dec.add_argument("--session-dir", required=True)
    dec.add_argument("--envelope", required=True)
    args = parser.parse_args(argv)
    try:
        result = (generate(args.session_dir, args.html, args.ttl_seconds) if args.command == "generate"
                  else decrypt(args.session_dir, args.envelope))
    except (OSError, ValueError, TypeError, KeyError):
        # Exception messages can contain supplied data; never echo them.
        print(json.dumps({"status": "error", "reason": "handoff_failed_check_paths_permissions_session_and_envelope"}))
        return 1
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
