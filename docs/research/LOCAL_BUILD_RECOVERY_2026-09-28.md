# Local native build recovery

The owner authorized completing the missing environment while away and asked
for a notification if credential entry becomes necessary. Older Actions runs
are left alone, and all repository checkpoints use `[skip ci]`.

## Recovered locally

- Restored 386 missing source/dependency/test/data files from pinned remote
  commit `01d20df963f96cf2ed3b18c4ee36126c622d39ca`. Verified every Git blob SHA
  and byte length; preserved existing source files and their exact hashes.
- CMake 3.28.3 and Ninja 1.11.1 now run from a user-local toolchain. The first
  pip/system-install attempts failed. APT package metadata and downloads worked;
  packages were extracted under the workspace with `dpkg-deb -x`, without a
  system installation. Runtime libraries were included.
- A local Python environment inherits cryptography and adds requests 2.31.0
  with its dependencies from extracted distribution packages.
- CMake configuration succeeded with GCC 13.3, OpenSSL 3.0.13, vendored
  SQLite 3.47.2, shared library, native tests, CLI and HTTP server enabled.
- A real Debug build is running locally. Build and CTest results will be
  recorded separately when they finish; configuration success is not a test pass.

Neither dependency recovery nor compilation uses GitHub Actions. Public network
access to OpenRouter remains a separate unconfirmed capability.

## Credential handoff

`loom/tools/structure/credential_handoff.py` creates a disposable RSA-3072
OAEP/SHA-256 recipient and a public, self-contained HTML form. The form makes
no network requests and uses no browser storage. The user encrypts locally
and attaches only the resulting JSON envelope. A strict decoder verifies
session, purpose, expiration and ciphertext; it writes a fixed private file
using exclusive no-symlink creation, then consumes the private session material.
Neither the credential nor the private key belongs in Git, Library or logs.
Only the public HTML form is delivered.

Twelve tests pass, including tampering, expiry and permissions. Independent
execution of the actual embedded JavaScript in Node WebCrypto and Python
decryption passed using a fabricated key. Browser download behavior remains
dependent on the user's browser; unsupported/insecure WebCrypto contexts
disable credential entry. Plaintext is restricted to visible ASCII to match
the existing OpenRouter runner. Encryption protects confidentiality and context,
not sender identity: import only an envelope explicitly supplied by the owner.

This is an optional local credential transfer, not a way to retrieve a GitHub
Actions secret. If the disposable runtime session is lost or expires, generate
a new public form. No actual key was entered or provider call made here.

## User-controlled network setting

The official Work documentation identifies, where available:
Settings > Data controls > Work network access > Allow public internet access.
Changes apply after the current execution finishes and the environment refreshes;
workspace restrictions still apply. This setting is distinct from browser,
search and connected-app permissions.

https://learn.chatgpt.com/docs/enterprise/chatgpt-work-overview

Current requested actions are recorded in `local-execution-attention.json`.
Do not ask for BYOK setup again, increase budgets, expose a credential in chat,
or resume any old/uncertain paid request. The previously authorized USD 2 cap
and first-response/no-retry rules remain in force.
