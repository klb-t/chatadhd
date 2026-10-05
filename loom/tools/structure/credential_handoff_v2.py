"""Disposable hybrid-encryption handoff for a separate research programme.

The public HTML encrypts locally with WebCrypto. Only ciphertext is returned.
Private material and the received credential remain outside Git and Library.
No network call, environment-secret lookup or provider integration exists here.
"""
import argparse
import base64
import json
from pathlib import Path
import secrets
import time

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

try:
    from . import credential_handoff as storage
except ImportError:
    import credential_handoff as storage


PURPOSE = "loom.openrouter.separate-programme-api-key-handoff/2"
SCHEMA = "loom.credential_envelope/2"
ALGORITHM = "RSA-OAEP-256+A256GCM"


def binding(metadata):
    return json.dumps([metadata["purpose"], metadata["session_id"], metadata["programme_id"],
                       metadata["expires_at"]], ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def oaep(metadata):
    return padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=binding(metadata))


HTML = r'''<!doctype html>
<html lang="pl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'none'; form-action 'none'; base-uri 'none'">
<title>Nowy klucz OpenRouter — szyfrowanie lokalne</title>
<style>body{font:18px system-ui;max-width:38em;margin:2em auto;padding:0 1em;background:#f4f5f9;color:#182334}input,button{font:inherit;box-sizing:border-box;width:100%;padding:.85em;margin:.5em 0;border:1px solid #b6c1d1;border-radius:8px}button{background:#164bbb;color:white}#status{overflow-wrap:anywhere}small{color:#47566c}</style>
<h1>Nowy klucz dla badań — budżet 5 EUR</h1>
<p>Pobierz ten plik HTML i otwórz go w przeglądarce. Wpisz <strong>nowy, oddzielny klucz OpenRouter</strong>. Kliknij „Zaszyfruj i pobierz”, następnie dołącz pobrany <strong>zaszyfrowany JSON</strong> do tej rozmowy.</p>
<p>Formularz nie wykonuje połączeń sieciowych. Klucz jest szyfrowany lokalnie; plik JSON zawiera szyfrogram, a nie jawny klucz. Nie wklejaj klucza do rozmowy.</p>
<p><small>Limit klucza w OpenRouter jest w USD. Przeliczenie budżetu 5 EUR i kontrola aktualnych cen nastąpią przed uruchomieniem badań. Szyfrowanie samo nie uruchamia żadnego wywołania API.</small></p>
<p id="session"></p><label for="key">Nowy klucz OpenRouter</label>
<input id="key" type="password" autocomplete="off" autocapitalize="none" spellcheck="false" disabled>
<button id="encrypt" type="button" disabled>Zaszyfruj i pobierz</button>
<p id="status" role="status" aria-live="polite"></p>
<script>
'use strict';
const config = __PUBLIC_CONFIG__;
async function encryptCredential(credential, metadata) {
  const plaintext = new TextEncoder().encode(credential);
  let rawKey;
  try {
    if (!plaintext.length || /[^\x21-\x7e]/u.test(credential)) throw new Error('invalid_credential');
    if (metadata.expires_at !== null && Date.now() >= metadata.expires_at * 1000) throw new Error('expired');
    const aad = new TextEncoder().encode(JSON.stringify([metadata.purpose, metadata.session_id, metadata.programme_id, metadata.expires_at]));
    const spki = Uint8Array.from(atob(metadata.public_spki_b64), c => c.charCodeAt(0));
    const rsaKey = await crypto.subtle.importKey('spki', spki, {name:'RSA-OAEP', hash:'SHA-256'}, false, ['encrypt']);
    const aesKey = await crypto.subtle.generateKey({name:'AES-GCM', length:256}, true, ['encrypt']);
    rawKey = new Uint8Array(await crypto.subtle.exportKey('raw', aesKey));
    const iv = crypto.getRandomValues(new Uint8Array(12));
    const ciphertext = new Uint8Array(await crypto.subtle.encrypt({name:'AES-GCM', iv, additionalData:aad, tagLength:128}, aesKey, plaintext));
    const wrapped = new Uint8Array(await crypto.subtle.encrypt({name:'RSA-OAEP', label:aad}, rsaKey, rawKey));
    const b64 = bytes => { let text=''; for (const byte of bytes) text += String.fromCharCode(byte); return btoa(text); };
    return {schema:'loom.credential_envelope/2', algorithm:'RSA-OAEP-256+A256GCM', session_id:metadata.session_id,
      purpose:metadata.purpose, programme_id:metadata.programme_id, expires_at:metadata.expires_at,
      wrapped_key_b64:b64(wrapped), iv_b64:b64(iv), ciphertext_b64:b64(ciphertext)};
  } finally { plaintext.fill(0); if (rawKey) rawKey.fill(0); }
}
const input = document.getElementById('key'), button = document.getElementById('encrypt'), status = document.getElementById('status');
const supported = window.isSecureContext && window.crypto && window.crypto.subtle;
const expired = () => config.expires_at !== null && Date.now() >= config.expires_at * 1000;
document.getElementById('session').textContent = 'Sesja: ' + config.session_id + '. Jednorazowe przekazanie klucza.';
if (!supported) status.textContent = 'Ta przeglądarka lub podgląd nie udostępnia WebCrypto. Nie wpisuj klucza. Pobierz HTML i otwórz go lokalnie w przeglądarce obsługującej WebCrypto.';
else if (expired()) status.textContent = 'Formularz wygasł. Potrzebny jest nowy formularz.';
else { input.disabled=false; button.disabled=false; }
button.addEventListener('click', async () => {
  button.disabled=true;
  try {
    if (!supported || expired()) throw new Error('unavailable');
    const envelope = await encryptCredential(input.value, config);
    input.value='';
    const url = URL.createObjectURL(new Blob([JSON.stringify(envelope,null,2)], {type:'application/json'}));
    const link=document.createElement('a'); link.href=url; link.download='openrouter-new-key-encrypted-' + config.session_id + '.json';
    document.body.appendChild(link); link.click(); link.remove();
    setTimeout(() => URL.revokeObjectURL(url),30000);
    status.textContent='Pobrano zaszyfrowany JSON. Dołącz ten plik do tej rozmowy. Nie wysyłaj jawnego klucza.';
  } catch (_) { input.value=''; status.textContent='Nie udało się zaszyfrować klucza. Nie wysyłaj go jako tekstu.'; }
  finally { button.disabled=!supported || expired(); }
});
</script></html>
'''


def generate(session_dir, html_path, programme_id, ttl_seconds=None, now=None):
    if not isinstance(programme_id, str) or not programme_id:
        raise ValueError("programme_id_required")
    if ttl_seconds is not None and (type(ttl_seconds) is not int or ttl_seconds <= 0):
        raise ValueError("positive_lifetime_or_none_required")
    session = storage.private_directory(session_dir, create=True)
    metadata = {"session_id": secrets.token_hex(16), "purpose": PURPOSE, "programme_id": programme_id,
                "expires_at": None if ttl_seconds is None else int(time.time() if now is None else now) + ttl_seconds}
    private = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    storage.write_new(session / "private.pem", private.private_bytes(serialization.Encoding.PEM,
                      serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    storage.write_new(session / "session.json", json.dumps(metadata).encode())
    public = private.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    config = {**metadata, "public_spki_b64": base64.b64encode(public).decode("ascii")}
    # Escaping '<' prevents even a configurable programme ID from closing script.
    config_json = json.dumps(config, ensure_ascii=False).replace("<", "\\u003c")
    html = Path(html_path).absolute()
    storage.write_new(html, HTML.replace("__PUBLIC_CONFIG__", config_json).encode("utf-8"))
    return {"status": "ready", "session_dir": str(session), "html_path": str(html), **metadata}


def decrypt(session_dir, envelope_path, now=None, max_envelope_bytes=1048576):
    if type(max_envelope_bytes) is not int or max_envelope_bytes <= 0:
        raise ValueError("positive_parser_budget_required")
    session = storage.private_directory(session_dir)
    metadata = storage.strict_json(storage.read_private(session / "session.json", max_envelope_bytes))
    if not isinstance(metadata, dict) or set(metadata) != {"session_id", "purpose", "programme_id", "expires_at"}:
        raise ValueError("invalid_session")
    if (metadata["purpose"] != PURPOSE or not isinstance(metadata["session_id"], str) or len(metadata["session_id"]) != 32
            or any(char not in "0123456789abcdef" for char in metadata["session_id"])):
        raise ValueError("invalid_session")
    if not isinstance(metadata["programme_id"], str) or not metadata["programme_id"]:
        raise ValueError("invalid_session")
    expires = metadata["expires_at"]
    if expires is not None and (type(expires) is not int or (time.time() if now is None else now) >= expires):
        raise ValueError("expired_session")
    with Path(envelope_path).open("rb") as source:
        raw = source.read(max_envelope_bytes + 1)
    if len(raw) > max_envelope_bytes:
        raise ValueError("envelope_exceeds_configured_parser_budget")
    envelope = storage.strict_json(raw)
    fields = {"schema", "algorithm", "session_id", "purpose", "programme_id", "expires_at",
              "wrapped_key_b64", "iv_b64", "ciphertext_b64"}
    if not isinstance(envelope, dict) or set(envelope) != fields:
        raise ValueError("invalid_envelope")
    if envelope["schema"] != SCHEMA or envelope["algorithm"] != ALGORITHM:
        raise ValueError("invalid_algorithm")
    if any(envelope[key] != value or type(envelope[key]) is not type(value) for key, value in metadata.items()):
        raise ValueError("envelope_session_mismatch")
    wrapped = base64.b64decode(envelope["wrapped_key_b64"], validate=True)
    nonce = base64.b64decode(envelope["iv_b64"], validate=True)
    ciphertext = base64.b64decode(envelope["ciphertext_b64"], validate=True)
    if len(wrapped) != 384 or len(nonce) != 12 or len(ciphertext) <= 16:
        raise ValueError("invalid_algorithm_encoding")
    private = serialization.load_pem_private_key(storage.read_private(session / "private.pem", 16384), password=None)
    if not isinstance(private, rsa.RSAPrivateKey) or private.key_size != 3072:
        raise ValueError("invalid_private_key")
    aes_key = private.decrypt(wrapped, oaep(metadata))
    if len(aes_key) != 32:
        raise ValueError("invalid_wrapped_key")
    plaintext = AESGCM(aes_key).decrypt(nonce, ciphertext, binding(metadata))
    if not plaintext or any(byte < 33 or byte > 126 for byte in plaintext):
        raise ValueError("invalid_credential")
    destination = session / "openrouter-new.key"
    storage.write_new(destination, plaintext)
    (session / "private.pem").unlink()
    (session / "session.json").unlink()
    return {"status": "saved", "key_file": str(destination), "session_consumed": True}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    gen = commands.add_parser("generate")
    gen.add_argument("--session-dir", required=True); gen.add_argument("--html", required=True)
    gen.add_argument("--programme-id", required=True); gen.add_argument("--ttl-seconds", type=int)
    dec = commands.add_parser("decrypt")
    dec.add_argument("--session-dir", required=True); dec.add_argument("--envelope", required=True)
    dec.add_argument("--max-envelope-bytes", type=int, default=1048576)
    args = parser.parse_args(argv)
    try:
        result = (generate(args.session_dir, args.html, args.programme_id, args.ttl_seconds) if args.command == "generate"
                  else decrypt(args.session_dir, args.envelope, max_envelope_bytes=args.max_envelope_bytes))
    except Exception:
        print(json.dumps({"status": "error", "reason": "handoff_validation_failed"})); return 1
    print(json.dumps(result)); return 0


if __name__ == "__main__":
    raise SystemExit(main())
