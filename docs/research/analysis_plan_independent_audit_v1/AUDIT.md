# Independent AnalysisPlan review: repaired and verified

Three concrete defects were preserved before requesting corrections: completion
metadata could reduce accounted money while the unchanged result digest passed;
invalid measurement metadata discarded a valid first returned candidate; direct
execution could dispatch an index outside an explicit selection. The owner
repaired all three before freeze. Original first results remain unchanged.

The approved runtime digest is
`2dfc9c3c67fb7135477b91e5599bbc30c96e006ab301b55b4a1d0f142f442372`,
schema `3f70b5db565c191e72af4200a6348c342e7210dfac4a96c2d217da78725e3776`,
tests `a5e4fa15178e0dd0f39f28515137aefc849b6eeff60a7d4182654a980cc10f60`.
Start/end hashes match in the final independent review (`repair3/FINAL_RESULT.json`).
The owner's separate freeze is
`docs/research/analysis_plan_v1/FREEZE.json`, digest
`b1e6fc6ee91cbad807066006abe0bfa3ec9c8cf531dfc96ad1ff0cd011ec49de`.

## Evidence and measurements

| Probe | Original outcome | Final independent outcome |
|---|---|---|
| Completion-only money mutation | Accounted 1 became 0; another callback admitted under cap 1 | Reject before usage, reuse or new admission |
| Finite output with measurement -1 | Candidate lost; reservation retained | Candidate preserved exclusively; uncertain, all ten reservations retained |
| Explicit selection [0], direct execution 1 | Unselected callback ran | Reject before reservation, loader or callback; coordinate inspection remains available |
| Six receipt corruption classes × three operations | New adversarial controls | All 18 reject before additional callback |
| Completion/result persistence faults | No repeated callback; unknown reservation retained | Same behavior; first return survives result-write failure |
| Four separate ledger instances and four real OS processes | Exactly two one-unit callbacks under cap two | Same; four processes exit successfully |
| Hard process exit 43 inside callback | Durable reservation remains | Reopen uncertain; zero retries; all ten dimensions held |
| Ten separate dimensional admission controls | Each blocks a reservation above its limit | Same, with the exact dimension reported |
| Independent 12-coordinate Cartesian product | All decoded coordinates match; unused axes invoke once | Same |
| Loaded packet preservation and hash binding | Added by owner after separate provenance review | Saved before callback; original packet remains intact; mutation rejects accounting/replay |
| Owner's targeted suite | 20 tests at first snapshot | Independently 32/32 at final pins |

The owner also corrected ambient Decimal rounding. Against a Fraction oracle,
the exact helper passes 512/512 stress cases (128 fixed tuples × four precision
settings). Ordinary Decimal accumulation differs in 128/128, 128/128, 103/128 and
81/128 cases at precision 2, 7, 28 and 81 respectively. Every operand and result
is retained. The final ledger blocks reservation 1 when an exact opening value
10^28 already equals the limit 10^28. This measures numerical mechanism behavior;
it is not evidence of a real billing loss or model quality.

`FIRST_REVIEW_SOURCES.zip` retains the original pre-repair runtime/schema/tests
and the validator/probes. Restoring it into a fresh tree reproduced both first
defects and all seven compared control artifacts exactly. The later selection
source was recovered by removing the final guard; its SHA256 exactly equals the
source digest recorded in the first selection probe. The recovery receipt is
explicit; production source was never changed. `FINAL_REVIEW_SOURCES.zip` retains
the final tested source closure and runnable independent audit scripts.

## Scope and reproduction

This review uses registered local scripted callbacks and deliberately corrupted
synthetic ledgers. All money values in those ledgers are synthetic test counters,
never external charges and never additions to the live shared API budget. No
model API, credentials, sealed validation data or canonical graph writes occur.
The reference allocates no agent scheduler; the process controls explicitly
spawn four local test processes to exercise POSIX locking.

Approval covers the callback-only reference. Packet snapshots are aggregate
first-input evidence. Raw source binding remains caller declared and is labelled
`caller_declared_not_verified`; domain adapters must provide any semantic/raw
binding proof. Nested callback/tool/model accounting and cancellation need a
runtime adapter or resource broker. No native wiring, sandbox, independent
billing verification, model quality, paid authorization or distributed
exactly-once external action guarantee follows from these tests.

Restore either source ZIP into a new empty path using the hash-checked,
zero-overwrite script in the adjacent graph experiment `FIRST_ARCHIVE.md`, with
the matching source ZIP receipt. Install the declared contract dependencies
before going offline. From the restored original tree run `probe.py` and
`process_controls.py`; from the restored final tree run `final_reprobe.py`.
Each is under `docs/research/analysis_plan_independent_audit_v1/` and writes
exclusively into new namespaces. The final harness intentionally refuses any
preexisting `repair3` directory.

An initial final-harness run (`repair2`) collided with its own source-receipt
filename before any callback. The first failure log was preserved; the complete
final run (`repair3`) uses separate orchestration receipt names. This is an audit
harness correction, not a production defect or overwritten experiment outcome.
