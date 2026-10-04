# Thread 4 — native GraphPacket, 2026-10-04

Branch: `gpt/native-graph-packet-2026-10-04`. Initial base: `161cc22`
(includes PR9). Final main base: `7282437`, documentation-only integrator commits. No changes to main, STATE, profiles, UI, selector or core/KB database schema.
This continuation includes the newly assigned `kb/` scope and strengthens
PR9 GraphPacket acceptance without replacing its store or historical receipts.

Delivered native packet creation/codec, diff preview/apply/inversion and full
backwards/forwards history checks; deterministic `loom.graph_reply/1,/2`
compilation with exact first-byte captures and host-computed UTF-8/code-point
spans. Draft semantic links remain draft; acceptance does not establish truth.
API: `loom_packet` and `POST /api/packet`.
[Commands, policy, boundaries and reproduction](../../loom/src/packet/README.md).

Optional `usage_estimate` uses thread 2's admission/reservation/settlement API
and binds the exact command. Without that dependency an explicit estimate
fails unavailable. No fabricated resource measurements or fixed codec ceilings.

| Check | Before | After |
|---|---|---|
| Full CTest | 108 entries in PR9's historical receipt; not rerun baseline here | **110/110**, 117.71 s |
| Native packet/FFI | No native algebra/graph-reply suite | **4/4** native cases, **24/24** assertions; **11/11** FFI cases |
| Python reply parity | Python-only compiler | Five pinned compilations agree with C++ |
| Shared-policy integration | No packet caller | 6/6 synthetic scenarios |
| Packet HTTP | No route | **6/6** synthetic scenarios |

Implementation: `4e4ceba`. [Pinned source/binary hashes and test logs](../../loom/src/packet/tests/evidence/2026-10-04/manifest.json).

All data are synthetic. Zero paid calls and zero GitHub Actions runs. Native
numbers are not model-quality measurements. The first build needed vendored
SQLite because the environment lacks system SQLite development files.
The isolated policy link check sets thread 2's config preset explicitly; its
initial failed run omitted that preset. It does not claim a fully merged build.

Integrator: select after thread 2; rebase onto fresh main and retain PR9's store
route/receipts. The results above describe the first delivered increment. Thread 3 still owns actual chat/provider-request wiring. UI, live
model-quality tests, cloud calls and automatic canonical knowledge acceptance
were not done: they are outside this lane. Canonical writes remain the existing
explicit `loom_graph_packet_store` operation.

Thread 2 currently declares `loom_usage_policy_json` for static callers without
shared export registration. Integrator/client owners must expose that existing
confirmation API if the chosen UI uses FFI; this lane does not register a
second confirmation engine.


## Continuation under the 11-thread assignment

At the continuation fetch, main was `161cc22`. Final fetch found `5893b1f`;
the branch is rebased on it, preserving the integrator documents. Reviewed W2 `648a4e9`, W3 `36ec2a8`, W9 `125a043`, W10 `2f25145`
and W11 `b88154c` (the inventory is pinned to `9590625`). W2's new settings-preview API leaves its lifecycle unchanged;
its policy kernel is byte-identical to the previous pinned `34cc920`.
Only packet/KB implementation, the packet C ABI/HTTP route, affected tests and
this report changed. The source API contract is in `loom/src/packet/README.md`.

- Added verified `reply_fragment` addressing by local ID or native entity ID,
  with exact UTF-8/code-point span, retained display-source locator and model
  content provenance. Every lookup recompiles the captured bytes first.
- Native store acceptance now validates all packet records and full backward/
  forward history before writes, after PR9's existing selected-row checks.
  Source/CAS/owner judgement rules and immutable historical receipts remain.
  New receipts truthfully mark native history validation; old read/replay retains
  its original marker and database bytes. Acceptance retries preserve valid old
  receipts after current codec validation, without rewriting history.
- Fixed three audit findings: forged/rehashed application receipt flags accepted
  by inversion; caller limits lost on generated candidates/results; dangling
  counter-observation references accepted by the packet codec. Regression tests
  exercise rejection through the actual shared C ABI, with no test gate removed.
- Added compile/apply/store/read/replay coverage for all five synthetic reply
  fixtures, alongside fragment, output-limit and legacy-receipt regressions.
- Explicit zero resource budgets are valid settings. Configured-limit verdicts
  apply to inputs and successful generated results; errors still retain capture.
- Packet HTTP rejects literal NUL before the C-string ABI boundary and preserves
  escaped JSON NUL. Errors carrying usage metadata retain their HTTP error status.

The fresh integrator report `f088445` held the earlier tip for duplicate dispatch
with unresolved usage. This return was reproduced independently with the pinned
W2/W4 C ABI overlay. Added a packet-owned atomic dispatch claim/output cache
in `usage-policy.sqlite`, without changing W2 table definitions or ledger version.
Usage rows still change through W2's admission/settlement APIs. Completed,
unresolved, failed and concurrent duplicates do not execute again; interrupted
claims are explicit; failed settlement retries original saved measurements only.
Owner-confirmed first operations still execute. Exact scenarios and the two
legacy/admission boundaries are documented in the packet API README.

The first parallel and serial links ran out of environment memory; a later
archive build exhausted the shared disk. Own failed linker/archive temporary
outputs were discarded, with logs and portable negative reproductions retained.
The successful build uses GNU ld `--no-keep-memory` and `-j 1`; no repository
preset, timeout, assertion or quality threshold changes. The first full CTest
attempt also exposed the outdated HTTP receipt-marker assertion. It now requires
the new exact validation marker, matching the FFI gate. All failed gate attempts
are retained separately from any final successful receipt.
No paid calls, Actions runs, private archives or holdout keys were used.

The fetched W9 method-graph gate is addressed on the packet side by
[`loom.method_graph/1` and `loom.method_run_trace/1`](../../loom/src/packet/METHOD_GRAPH.md).
The native C ABI/store regression projects method/version/prompt/recipe/preset/
combination/run as actual graph records, with 9 result-to-run/version/compiler
edges. It preserves model content and includes a separate dated, explicitly
unmeasured model assessment. Final graph: 13 entities, 19 claims, 5 sources.
W3 has not published its producing adapter at `36ec2a8`; joint execution adoption
is open. This graph/persistence test alone does not satisfy that joint gate.

The local archive was converted to GNU thin format using the same 129 compiled
objects, each hashed in `thin-archive.json`, to free shared disk space. Tested
shared/server binaries were not altered by this cache-only conversion. No
repository archive rule or preset was changed.

Verification for this continuation is recorded separately from the initial
receipt in `loom/src/packet/tests/evidence/2026-10-04-followup/`.

| Follow-up check | Previous increment | Continuation on `161cc22` |
|---|---|---|
| Full CTest | 110/110, 117.71 s (parallel; retained receipt) | **110/110**, **395.91 s**, serial, unchanged gates |
| Native packet | 4 cases / 24 assertions | **4/4 cases**, **24/24 assertions** |
| Packet FFI | 11 cases | **16/16** |
| Native store | 18 existing methods in source | **23/23** executed; five compiled fixtures and versioned method graph included |
| Packet HTTP | 6 scenarios | **11/11** |
| Real W2 static overlay | 6 scenarios, W2 `34cc920` | **14/14**, W2 `03b0c4e`, full compiler warnings + WERROR |
| Rehashed receipts / generated limits / dangling counter | 9 accepted pre-fix transcript observations | **9/9 rejected** through actual FFI |
| Method manifest/trace schema | No shared versioned format | **14/14** standalone checks; model and lexical methods, null/omitted roles, open caller data |
| Reply compilation parity | Five pinned Python fixtures | Same **5/5**, unchanged exact outputs |

Elapsed times above are differently scheduled shared-host runs, not a throughput
before/after benchmark. No resource measurement is inferred from a timeout. A supplemental
unfiltered native run accidentally used a positional doctest filter, so it ran
663 cases together: 662 passed, one import RSS assertion failed with pre-existing
VmHWM 187292 kB and delta 0. That is retained, not substituted for CTest's
separate native groups. The corrected case-sensitive packet filter passed 4/4
cases and 24/24 assertions. The first method test incorrectly expected the
raw packet JSON helper to raise on an error envelope; it was corrected to check
the returned error and unchanged base. Its final store suite passed 23/23.

## Final main refresh

Main advanced to `5893b1f` through two integrator documentation commits.
The six native packet commits were rebased linearly and republished; the
original complete line/receipts remain under
`archive/2026-10-04/native-graph-packet-before-main-refresh` at `b8f29bf`.
Both lines have identical complete `loom/` tree
`d9d9effcb9f5c4f921d69c233f7806d83721513d` before adding the refreshed receipt.
No code, test, data or build-input change required recompilation. A separate
full CTest run passed **110/110 in 323.03 s** on the new main base, with unchanged
timeouts/assertions. Its log and pinned code/data hashes are in
`loom/src/packet/tests/evidence/2026-10-04-main-refresh/`; earlier receipts remain
intact. No provider calls or Actions runs were introduced.

The integrator then published `7282437`, updating only STATE/INDEX/its report
with the received W4 fix and joint-method gate. The branch was rebased again.
Its full `loom/` tree remains byte-identical to the freshly tested `f450b89`
line: `88720391db69200bae861c0d850cfbda98d7cc4a`. That verification line is retained
under `archive/2026-10-04/native-graph-packet-main-5893b1f`. The unchanged code,
test/data and binary hashes carry the green 110/110 result; no extra repeat or
rebuild is claimed for this documentation-only advance. `latest-base.json`
records the exact equivalence.

## Do wątku 2

Packet's optional `usage_estimate` already uses the shared admission/reservation/
settlement contract, rather than a second guard. Explicit estimates fail
unavailable on unintegrated main. A packet dispatch receipt is distinct from admission/reservation; it records
execution/result only. W2 has no atomic cancellation-versus-dispatch API, and
an admission snapshot cannot abort work on an external cancellation. Keep that
boundary explicit in future shared dispatch contracts. The isolated actual-policy
link runner pins `03b0c4e`; earlier retained checks pin `34cc920`. Later W2
`648a4e9` adds a proposed profile handoff, not a production lifecycle change.
Neither isolated run is evidence of a merged production build. A future root-aware
settings helper should receive the actual `rt.paths().root`, without a W4 loader.

## Do wątku 3

Use `loom_packet` / `packet::execute` (same algebra command contract) to compile `/2`,
validate/apply a compilation and address response fragments.
The usage-estimate admission/dispatch wrapper exists only in `loom_packet`;
direct `packet::execute` performs the local algebra without that guard. `reply_fragment` requires the original base packet and compilation; the full schema is documented
in the packet API README. Thread 3 owns provider calls, all four chat modes,
graph context settings, candidate/automatic admission and retaining ordinary
text after schema errors. Raw capture is retained even on compiler failure;
chat must retain any separately rendered text too.

Adopt the versioned packet-side method graph contract and fixture linked above.
Its regression covers real edges and native persistence; add a joint regression
using your actual registry/trace adapter before W9 admits the combination. Do not
stamp a builtin recipe hash on a result from different effective user settings.

Every emitted reply node/source carries `model_origin.kind=model`.
Diff/structural origin
`system` identifies the deterministic compiler, not model truth. Use model
content provenance for response annotations, and retain compiler provenance.
Semantic links remain unverified draft attributes; acceptance never confirms
content. An unresolved authorized usage reservation is not a new dispatch grant;
chat must preserve its own first responses/execution identity before paid work. Do not map system containment to model factual claims.

## Do wątku 10

`POST /api/packet` exposes capabilities, compilation and verified fragment
lookup. Use `{local_id: ...}` or `{node_id: ...}` to address expand/correct
commands, then send those commands through thread 3's chat API. Display offsets
are Unicode code points and UTF-8 bytes, not browser UTF-16 indices. No UI or
profile contract files were edited by thread 4. Packet duplicate responses
carry `executed:false, replayed:true`; display `usage_current_decision` separately
from the historical `usage_decision`. `pending_or_interrupted` and
`legacy_unresolved_indeterminate` must not be rendered as a new execution grant.

## Do wątku 11

Received `9590625:docs/reports/data-in-code/thread-4.md`, anchored to
main `161cc22`: 27 reviewed groups. DIC-0383–0389 describe language/stemming;
DIC-0390–0404 pack descriptors/validation; DIC-0405–0406 query limits.
DIC-0484–0485 are `capi_knowledge`, outside our literal C ABI scope.
DIC-0526 identifies the existing universal store route, retained here.
Those inherited normalization/pack/query migrations were not implemented in
this GraphPacket increment. Their proposed `loom/data/**` destinations need a
coordinated owner; this lane does not write another lane's policy/lexicon files.
Keep representation/schema/source/audit invariants distinct from adjustable
policy. Prompt/output-schema recipes belong in data/profiles with overlays;
packet compilation performs local contract operations and no provider request.

## Do wątku 9

Integrate this branch after thread 2, rebase on fresh main, run full CTest and
web build, then fast-forward main. No main/STATE/README edits were made here.
The returned unresolved-replay defect is corrected in the packet dispatcher;
verify its 14 offline policy scenarios separately from main's CTest because W2
is not yet integrated. The packet-side method-graph contract, open JSON Schema and regression are
published here; W3's actual producing adapter and joint regression remain an
explicit integration prerequisite. Register/export the existing thread-2 confirmation C API if UI uses shared FFI;
this lane does not add another approval engine. Update STATE with native history
validation now including KB acceptance. Thread 8's presentation README draft
still describes full history validation as Python-only; update that claim only
after taking this native increment.

Further inherited KB policy work found during audit is not claimed fixed:
`kb/candidates.cpp:10` candidate page maximum 1000; `kb/store.cpp:69,615` silently
maps unlimited queries to 1,000,000; `kb/pack.cpp` contains window/count/primitive
ceilings (200, 100000, 1000000, 1000 and option-set 100) and alpha/beta maxima
1e9. This continuation implements the assigned GraphPacket deliverable. Route
these through thread 11's inventory back to the KB owner for a separate measured
settings/pack change, preserving mathematical and representation requirements.
