# Thread 4 — native GraphPacket, 2026-10-04

Branch: `gpt/native-graph-packet-2026-10-04`. Initial base: `161cc22`
(includes PR9). Current main base: `30ad7d3`, including accepted W2 and R39–R41. Historical gates below retain their original source bases. No changes to main, STATE, profiles, UI, selector or core/KB database schema.
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


## Owner nudge: shared gate intake on current main

Fetched main `30ad7d37337d6641cb7714b03e9feff0e6e25d25` and W3
`03c670caa6ee3d8ac2c478186548114f8e83927f`; read current INDEX and R39–R41.
Rebased the existing 11 commits linearly onto main and published tip `2a7ad39`.
Prior complete state `1377e20` is preserved at
`archive/2026-10-04/native-graph-packet-before-usage-intake`.
No other lane was reverted or edited. Fresh native build/gates are running;
this checkpoint does not claim their result.

W3 now supplies actual registry/checked fake-provider execution/native binding.
The earlier pending-producer statements below describe the previous session.
One canonical contract is `loom/src/packet/METHOD_GRAPH.md`. One canonical
shared artifact, identical to W3’s report, is
`docs/reports/chat-selector-2026-10-04-evidence/golden-consumer/input.json`
from `32e2381`; SHA-256
`8db8175c3b70c3947711ddf5daee5073b99bc5a5114ce0075e06e51932cec54e`.
It has 19 Entities, 30 Claims, 17 Observations, 3 results and 4 history events.
All 36 W3 recorded source/test hashes match pinned W3. The canonical exporter
artifact is materialized byte-for-byte for verification on this branch.
Requested/observed aliases and ordered nested combinations are being checked
with additional real W3 registry adapter runs; no paid provider call is used.

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

## Owner supplement — methods are graph entities

Fresh fetch for the owner's explicit supplement found main unchanged at
`7282437`, W3 rebased to `8293fc7`, W2 `910a1d6` and W11 `3cd3f47`.
W1 `2d08797` supplies prompt contracts/snapshots but explicitly leaves graph
method-node wiring open; its request for shared identities is addressed here.
W3's selector/embedding implementation still has no producing method-registry
adapter, GraphPacket reply composition or real method/run graph edges. Its
message metadata is not evidence that the shared W3→W4 contract is adopted.

The earlier fixture retained exact parameters in recipe/run attrs and a
combination reference in run attrs. This increment makes parameters independently
addressable as a hashed parameter-set-version Entity. Actual Claims now link the
specific method version and run to that set, and the run to its exact combination
version. Result→method-version→parameters and result→run→combination are graph
traversals. Captured definitions preserve exact values, including prompt text,
alongside hashes; this is checked against native Entity attrs after composition.
The exact captures exposed an inherited synthetic evaluator-origin mismatch:
the judge had been stamped with the evaluated method's recipe hash. Both judge
origins now retain null for its unavailable recipe; response bytes/date/evidence
remain captured. A full gate started before this review finding was interrupted
and retained, rather than presented as a final result.

| Packet-side measure | Before supplement (`8e0e86b`) | After supplement |
|---|---:|---:|
| Final synthetic graph entities / claims / sources | 13 / 19 / 5 | 14 / 22 / 5 |
| Parameter-set Entities | 0 | 1 |
| Real version/run→parameters and run→combination Claims | 0 | 3 |
| Result→run/version/compiler Claims | 9 | 9, retained |
| Schema checks | 14/14 | 14/14 |
| Native store cases | 23/23 | 23/23, stronger parameter/definition/edge assertions |
| Full CTest | 110/110, earlier pinned receipt | 110/110, fresh 341.71 s |

The schema adds optional typed parameter-set bindings/hashes and exact definition
captures, preserving the earlier v1 shapes. It remains a shape contract:
hash equality, actual edges and settings consumed are separate producer/native
checks. Kernel, C ABI, HTTP and KB implementation are unchanged; no method-name
branch, production default or policy ceiling was added. Builtin definitions must
come from pack data; this synthetic fixture is not such a pack.

Focused native-store CTest passed 1/1 (23/23 cases) in 10.42 s. The fresh full
CTest passed **110/110 in 341.71 s**, with unchanged gates; its store entry
executed **23/23 cases** in 6.64 s. The receipt is recorded separately in
`loom/src/packet/tests/evidence/2026-10-04-method-supplement/`.
Earlier receipts stay pinned to their original code, data and report snapshots.
The joint execution gate and production packs remain open in their assigned
lanes. No provider calls or GitHub Actions runs were made.
Final fetch kept main at `7282437`; W11 advanced to `1841c26` in runtime profiles,
archive/search/graph consumers and reports, with no packet/KB implementation
collision. The published packet-side
contract and fixtures are ready for the owning producers; joint adoption is not
claimed by this full gate.

## Owner nudge — executable native consumer and pack window

Fetched and read `7282437:docs/reports/INDEX.md`, including the held W3/W4 gate
and the W11 inventory for this lane. Main remains `7282437`. Initial W3 was
`8293fc7`; the subsequent fetch found `c4a9412` with caller-parameterized channel
operations/fusion and `chat/graph_reply` composition. Its callbacks still require
the producing method registry/binder; no actual `MethodRegistry` implementation
or exported joint golden exists at that published tip. The W3 report still
contains its earlier selector receipt. W11 is now `49e5e65`; no packet/KB source
collision was found. These are observations of published refs, not promises
about parallel uncommitted work.

The one common format file is
[`loom/src/packet/METHOD_GRAPH.md`](../../loom/src/packet/METHOD_GRAPH.md), using
`loom.method_graph/1` and `loom.method_run_trace/1`. It now specifies the concrete
`loom.method_graph_fixture/1` producer artifact and exact invocation of
`verify_method_graph_artifact.py`. The independently executable native consumer
checks schema/date formats, canonical/UTF-8 hashes, effective parameters,
applicable graph bindings and real provenance edges, exact definition/trace
captures, complete declared results and immutable version definitions across
history. Both outer input and inner captures use strict JSON parsing.

The consumer calls the real C ABI store directly, verifies exact native
acceptance, closes/reopens it, reads/replays the complete receipt and checks
identical acceptance retry. An existing evidence directory cannot be overwritten.
The proof explicitly says `producer_execution_verified:false` and reports
`verifier_provider_calls:0`; opaque producer diagnostics do not verify execution.
The local reference builder uses the retained synthetic response and pinned
native fixture, with no substitute W3 registry. Production defaults remain pack
and producer work in their assigned lanes.

| Measure | Before (`14eccaf`) | Current increment |
|---|---:|---:|
| Standalone producer-artifact/native consumer | absent | available, native acceptance + restart/read/replay/retry |
| Native store test methods | 23/23 | 26/26; 12 native-valid semantic counterexamples plus CLI evidence cases |
| Final synthetic graph Entities / Claims / Sources | 14 / 22 / 5 | 14 / 22 / 5, exact pinned identity retained |
| Declared outputs / actual result→run/version/compiler edges | 3 / 9 | 3 / 9 |
| Joint actual W3 registry → execution → W4 persistence | pending | pending; consumer is ready |
| Default `window_tokens` preset and loader/hash parity | 12 | 12, unchanged across embedded/directory/documents/unchanged overlay |
| Accepted configurable window domain | 1…200 | 0…native `int` maximum; no preset ceiling |
| Standalone regression against actual old / new core | 201 rejected | 6 accepted values and 11 malformed rejections, plus missing field/bad overlay |
| Full CTest | 110/110, prior receipt | 110/110, fresh final 99.95 s |
| Provider calls | 0 | 0 |

The independent KB change resolves the arbitrary window ceiling in DIC-0395.
The preset already lives in `loom/data/lexicons/version_patterns.json`; no data
or extraction file was edited. The existing consumer gives zero an empty local
`anchor_word` scan (`lo == hi`); message-wide `project_alias` matching remains.
Validation checks the full signed/unsigned integer before narrowing to its
existing `int` representation. Negative, fractional, malformed and overflowing
settings are rejected with the exact source pointer, rather than clamped.
This does not close the rest of the pack/query inventory.

Fresh `dev` builds, including shared core/server/CLI/tests, passed with `-Werror`,
bundled SQLite and unchanged gates. KB reproduction is in
`loom/src/kb/tests/pack_window.verify.cc` and its `evidence/2026-10-04/` commands,
baseline/final outputs, manifest and exact source/binary hashes. The baseline
probe links the actual old archive; it is not described as a fresh old-checkout
build. It is one standalone executable, not an invented CTest case count.

Packet receipts are separate in
`loom/src/packet/tests/evidence/2026-10-04-artifact/`. The first focused store run
failed because the newly written negative-test removal omitted the required
`reason` field; its log and exact test/verifier snapshots are retained. To replay,
restore those snapshots to their original source paths in a disposable checkout
and run the recorded focused command with the real shared library. The corrected
focused suite passed 26/26 in 27.32 s. The first full run also completed 110/110
in 96.87 s, before the final zero-window/strict-capture checks; its receipt is
labelled `before-zero`, not an interrupted or final run. The final CLI reference
has the current library hash and retained exact input/full receipt in
`reference-final/`; earlier positive reference proof remains pinned separately.
The final full gate passed **110/110 in 99.95 s**; its store entry ran **26/26**
cases in 48.62 s. All 12 semantic counterexamples use native-valid packet heads
and reversible histories and leave no canonical KB records on rejection. The
CLI cases retain failed duplicate-key/non-object inputs and preserve prior proof
on an attempted evidence-directory reuse. No thresholds or timeouts changed.

No main/STATE/README/UI/profile files were changed, and no provider calls or
GitHub Actions runs were made. The owner's shared W3/W4 gate remains open until
W3 actually exports its registry/execution golden and cites this same contract.

## Do wątku 1

Represent effective prompt and recipe definitions as ordinary versioned Entities,
with exact captured bytes/values and hashes using `METHOD_GRAPH.md`. The method
version must identify the recipe and parameter set actually used after overlays;
editing a prompt or parameter set creates a new version without rewriting old
run/result provenance. Packet-side representation is available; production prompt
data and pack construction stay in your lane.
At `2d08797`, `Contract.hash`/prepared snapshots can supply captured recipe
identity when they describe the effective invocation. Keep overlays and one-call
parameters bound separately and accurately. No new `compile_reply.host` fields
are required: method/version/run bindings are ordinary native Entities/Claims
composed through packet diffs, as in the fixture. The existing recipe hash field
does not establish provider execution or a verified first-response identity.

KB now accepts zero and large representable `window_tokens` through existing
pack/overlay loading. Zero empties the local anchor-word scan only; review any
new extraction semantics in your lane without changing default preset 12.

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
The current fixture also binds a parameter-set-version Entity and real
version/run→parameters and run→combination Claims. Preserve these in registry
output. At `c4a9412`, binder callbacks exist but the producing registry is not
published; declaring fields in message metadata does not fulfill the owner
requirement for graph edges. All producer paths,
including non-model methods, must supply applicable identities/edges; no fake
model or prompt is required for lexical methods.

Every emitted reply node/source carries `model_origin.kind=model`.
Diff/structural origin
`system` identifies the deterministic compiler, not model truth. Use model
content provenance for response annotations, and retain compiler provenance.
Semantic links remain unverified draft attributes; acceptance never confirms
content. An unresolved authorized usage reservation is not a new dispatch grant;
chat must preserve its own first responses/execution identity before paid work. Do not map system containment to model factual claims.

The single format reference for the held gate is
[`loom/src/packet/METHOD_GRAPH.md`](../../loom/src/packet/METHOD_GRAPH.md).
At `c4a9412`, graph-reply composition and binder callbacks are present, but the
actual registry and golden exporter are not published. Export the artifact
specified there from your real registry/execution path, including native packet,
final trace, declared result IDs and immutable capture-source IDs. Invoke the
provided native consumer; independently prove the actual request settings,
execution/transport instrumentation and complete result set. Add that same file
link to your report. A W4 offline reference pass does not close this gate.

## Do wątku 7

Use ordinary dated Claims about the exact method/version/model Entities for
experiment results and model profiles. Keep evaluator/source bytes and evidence
locators, dataset identity, measured dimensions and unavailable quantities.
The fixture shows a model judgement with explicit model origin; it is not a
measured quality result. Both evaluations and profiles use the same native
Claim/Observation structure; revisions preserve earlier assessments/history.

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
DIC-0395's window ceiling is now removed with native loader/default-overlay
parity evidence; zero is valid in the existing local scan. Other inherited
normalization/pack/query migrations remain open. Their proposed `loom/data/**` destinations need a
coordinated owner; this lane does not write another lane's policy/lexicon files.
Keep representation/schema/source/audit invariants distinct from adjustable
policy. Prompt/output-schema recipes belong in data/profiles with overlays;
packet compilation performs local contract operations and no provider request.

## Do wątku 8

The artifact consumer uses the existing strict-validator dependencies. Install
`loom/tools/contracts/requirements.txt` for the CTest environment; at current
main `.github/workflows/loom.yml` installs only `requests cryptography`.
Date-time checks fail explicitly without their validator. This lane does not
edit CI or silently skip the new proof. `unit.test_catalog_scale` still selects
zero cases in the inherited full gate; its registration remains your task.

## Do wątku 9

Integrate this branch after thread 2, rebase on fresh main, run full CTest and
web build, then fast-forward main. No main/STATE/README edits were made here.
The returned unresolved-replay defect is corrected in the packet dispatcher;
verify its 14 offline policy scenarios separately from main's CTest because W2
is not yet integrated. The packet-side method-graph contract, open JSON Schema and regression are
published here; W3's actual producing adapter and joint regression remain an
explicit integration prerequisite.
Current parameter-set and combination traversals are part of that shared
fixture; require W3's actual adapter to produce them before closing the gate.
Register/export the existing thread-2 confirmation C API if UI uses shared FFI;
this lane does not add another approval engine. Update STATE with native history
validation now including KB acceptance. Thread 8's presentation README draft
still describes full history validation as Python-only; update that claim only
after taking this native increment.

Further inherited KB policy work found during audit is not claimed fixed:
`kb/candidates.cpp:10` candidate page maximum 1000; `kb/store.cpp:69,615` silently
maps unlimited queries to 1,000,000; `kb/pack.cpp` still contains count/primitive
ceilings (100000, 1000000, 1000 and option-set 100) and alpha/beta maxima
1e9. This continuation implements the assigned GraphPacket deliverable. Route
these through thread 11's inventory back to the KB owner for a separate measured
settings/pack change, preserving mathematical and representation requirements.

The native artifact consumer and KB window fix are independently complete.
The KB increment is published as `78e43c2` on this branch.
Current shared gate checkpoint: the actual W3 golden is available and its
source/artifact hashes are verified. Native verification on the current W4
build and alias/nested compatibility checks are in progress; final readiness
will replace this checkpoint.
Both reports must cite the single `loom/src/packet/METHOD_GRAPH.md` contract.
For remaining KB candidate/query migration, coordinate the public knowledge
header/CAPI owner and W11's data/profile loader: the existing runtime profile
knowledge descriptor lacks query-policy fields. This lane adds no private loader
and does not edit `capi_knowledge`, public query defaults or another lane's data.
