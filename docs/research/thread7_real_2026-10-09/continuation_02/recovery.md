# Recovery of an externally witnessed interrupted attempt

This continuation adds `research_programme_recovery_v1.py`. It ran only with
controlled transports and fabricated receipts; it did **not** recover or charge
an owner API attempt. Actual provider calls and new campaign cost are both zero.
Historical owner accounting remains 720 attempts / USD 0.873216500 spent;
USD 4.126783500 is an archival remainder, not a current preflight.

## What can now be executed

The new procedure consumes an independently retained transport capture of an
already reserved attempt, preserving its exact request and first response bytes.
It checks operation, programme, key fingerprint, frozen manifest, request hash,
route and selected model/provider/API type against the existing payer. The
capture must fall between the attempt's recorded start and recovery's observation
time, with timezone-aware timestamps. No clock skew is silently invented.

A separately supplied capture review binds the exact capture digest, reviewer
reference and basis for associating that request with that response. A digest is
not a signature. The procedure **does not prove the honesty of this external
attribution**: a trusted transport journal or an explicit reviewed equivalent is
a prerequisite. An arbitrary generation ID or caller assertion alone is not
accepted. The existing generation verifier does not attest the original request
hash; it verifies identity and billing fields.

Only `GET generation` and `GET key` are reachable. Generation identity,
model/provider/API type, credit billing, exact reported cost and reservation cap
must pass the existing receipt verifier. The current exact-key usage must equal
the snapshot's verified cumulative costs including this attempt. Unknown account
deltas are not assigned to a generation. Other attempts must pass ordinary ledger
validation; unrelated unresolved attempts, orphan files or durable contradictions
remain blocking. The historical accounting snapshot is retained so a later
legitimate paid attempt does not invalidate the earlier proof.

There is still one money authority: `research_programme_runner.PrivateLedger`.
Recovery uses its existing `ledger.sqlite3`, exclusive lock and
`attempt_resolutions` projection. The new `recovery_events` table is an append-only
evidence journal inside that database, not another balance or reservation ledger.
Original `attempts` rows and record files are neither overwritten nor completed
with fabricated missing files. Incoming capture and each GET response are saved
before another read. The final event and settlement projection commit in one
SQLite transaction. A crash before that transaction retains unknown; a crash
after it resumes idempotently. This is not an exactly-once inference guarantee.

The ordinary payer replays the recovered proof before accepting the projected
completion. The queue reads its response through `PrivateLedger.response_bytes`
and retains provenance `external_capture`. Reservation lookup also observes
settlement projections, so the original reserved row cannot become a second
dispatch permission. A pre-existing programme stop is **not** cleared by recovery;
the existing `reconcile_stop` performs the remaining read-only campaign gates.
New paid work still needs the ordinary fresh preflight.

## Unknown is a durable result

Missing capture, absent credential reference, partial response, request/first-byte
conflict, stale attribution, unavailable billing or unexplained key usage append
an unknown case with an explicit fixed reason. Cost remains null and the full
reservation remains active. Supplied malformed/tampered ledger state is rejected
before any network access. A later complete proof appends a new event; prior
observations remain byte-for-byte unchanged. Repeating a completed recovery
performs no GET and no POST. A process dying before even the initial capture
transaction commits can require resubmitting the same externally retained file.

No key is needed to record an unknown case: provide the previously bound public
key fingerprint and omit `--key-file`. This path cannot settle costs. It does not
search for credentials. With a supplied private key-file reference, existing
secret-path ownership, permissions, Git-tree and symlink checks remain unchanged.

Example invocation with private file references (placeholders, not real paths):

```sh
python3 loom/tools/structure/research_programme_recovery_v1.py \
  --policy /private/policy.json --manifest /private/manifest.json \
  --private-dir /private/payer --repo-root "$PWD" \
  --operation-id OPERATION --key-file /private/key-reference \
  --external-evidence /private/external-capture.json \
  --capture-review /private/capture-review.json
```

For offline unknown handling, replace `--key-file` with `--key-fingerprint` and
the existing fingerprint. Do not supply key contents as a CLI argument. Capture
and review schemas are described in `recovery-protocol.json`; examples and raw
controlled-transport receipts are retained in the private checkpoint.

## Evidence and limits

The new tests exercise five original payer crash boundaries, five recovery crash
boundaries, ordinary timeout/stop reconciliation, concurrent recovery, duplicate
proof, missing credential, missing evidence, partial/conflicting bytes, manifest
and campaign mismatch, wrong generation/model/provider/API/cost/BYOK, key usage
delta, historical usage replay, tampered original and saved proof, unrelated
orphan rejection, queue projection, repeat-dispatch rejection and metadata/error
privacy. Test transports never contact a provider. Exact full logs and hashes
are in `recovery-tests.json` and the private checkpoint.

Fault injection tests process exceptions and SQLite transaction restart, not
hardware power loss, hostile OS compromise or cryptographic provider receipts.
A complete external first-response capture is required; a truncated local
response cannot silently be replaced with different bytes. This conservative
case remains unknown. No retrieval/model-quality claim follows from these tests.
