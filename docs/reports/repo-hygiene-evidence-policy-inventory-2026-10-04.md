# W8 — evidence policy inventory, 2026-10-04

Requested by `docs/reports/INDEX.md` on public main `7282437`. Audited branch:
`gpt/repo-hygiene-2026-10-04`, commit `2544cf3`. The first inventory below pins
the pre-migration guard. A subsequent authorized implementation is documented
in the final section; those two stages must not be conflated.

Original audited bytes, preserved in the migration evidence's `before/` folder:

- `.github/scripts/verify_ctest.py`: SHA-256
  `39e7748da96c3fb5a58e788b5a751bf8c80501b86ed17204e3f62057e2351c3a`.
- `.github/scripts/test_verify_ctest.py`: SHA-256
  `1a7f17c942ecf74378ef3b4f6bab599f5471416731ff88124ccda645b2b6d170`.

## Protocol and availability are different inputs

| Exact source sites | Classification | Current meaning / proposed data destination |
|---|---|---|
| `.github/scripts/verify_ctest.py:20` | Versioned output protocol | `loom.ctest_evidence/1` identifies the existing evidence format. A schema change needs explicit versioning; it is not an adjustable execution limit. |
| `.github/scripts/verify_ctest.py:25`, `:28`, `:31`, `:32`, `:33` | Runner-output codecs | Parse doctest/unittest summaries and terminal formatting. Keep universal parsing operations in code; declared runner dialect/version metadata can identify the appropriate parser. Regex changes are not availability exceptions. |
| `.github/scripts/verify_ctest.py:42`, `:55`, `:60`, `:71`, `:88`, `:92` | Evidence integrity | Complete manifest identity, unique entries, valid XML, outer success and untruncated output are required to substantiate the receipt. Availability policy must not suppress these failures. |
| `.github/scripts/verify_ctest.py:21`, `:40`, `:178` | Preset registry | Accepted names are currently `dev`, `asan`, `vendored`, repeated in API/CLI validation. Candidate destination: `.github/ctest-evidence-policy.json`, `presets`; generic loading can add a declared preset without changing parser code. |
| `.github/scripts/verify_ctest.py:98`, `:113`, `:114`, `:146` | Runner classification metadata | `unit.*` uses doctest; `compat.*`, `research.*`, `server.chat_active_task`, `eval.harness` use unittest; other entries are script checks. Candidate destination: policy `runner_bindings` with the same ordered matching semantics. A new binding must name an actually supported parser, not imply invented inner counts. |
| `.github/scripts/verify_ctest.py:109`, `:110` | Existing opt-in availability policy | Only `unit.test_catalog_scale` may have **0 cases / 0 assertions**, classified **unexecuted**. When it runs, the usual positive cases/assertions requirement applies. Candidate destination: policy `opt_in_entries`, retaining the exact name and zero/zero condition. |
| `.github/scripts/verify_ctest.py:22`, `:23`, `:134`, `:135`, `:136`, `:138` | Existing ASan build-capability policy | Only in `asan`, with a positive discovered count and the log reason `LOOM_LIBRARY not set`, `compat.test_graph_packet_store` and `compat.test_chat_active_task_http` may have all cases skipped. Both are **unexecuted**, never coverage. Candidate destination: `presets.asan.unavailable_entries`, exact names, `all_discovered_skipped`, required reason and capability `LOOM_SHARED=OFF`. |
| `.github/scripts/verify_ctest.py:139`, `:141` | Existing partial ABI availability policy | Under that same ASan/reason condition, `compat.test_abi_compat` permits **exactly two skipped cases**, while at least one case must execute. Candidate destination: `presets.asan.partial_unavailability`, `skipped_cases: 2`, `require_executed_case: true`. |
| `.github/scripts/verify_ctest.py:95`, `:107`, `:111`, `:117`, `:131`, `:142`, `:144` | Execution-evidence checks | Unexpected skips, zero discovery/assertions, ambiguous summaries and runner failures remain errors. Positive executed counts establish that a test actually ran; they are not new product-quality thresholds or spending limits. |
| `.github/scripts/verify_ctest.py:159`, `:160`, `:165`, `:167` | Universal accounting | Count executed entries separately; subtract and report Python skips; expose unexecuted entries. Loading policy data must preserve these calculations and labels. |

The current `dev` and `vendored` policies permit no Python skips. The ASan
exceptions reflect an existing non-shared build, not inferior extraction/model
quality that the guard is allowed to waive. No exception permits outer CTest
failure, missing entries, truncated evidence or a script's `SKIP:` response.

## Regression fixtures and migration boundary

The checked-in tests are fabricated receipt examples, not product policy data
or measurements of the actual CI build:

- `.github/scripts/test_verify_ctest.py:13`, `:19`, `:23` construct small
  doctest/unittest/XML examples. Their numeric case counts and elapsed time
  strings are frozen test inputs; they do not define execution quotas.
- `.github/scripts/test_verify_ctest.py:43`, `:51`, `:69`, `:76`, `:90`, `:98`,
  `:103`, `:111`, `:145`, `:150` check honest discovery, manifest identity,
  complete success evidence and rejection behavior.
- `.github/scripts/test_verify_ctest.py:55`, `:63` check the exact opt-in
  classification and the positive-assertion rule when scale execution is enabled.
- `.github/scripts/test_verify_ctest.py:118`, `:133`, `:141` check the named ASan
  exceptions, unexpected skip rejection and the required unavailability reason.
  These examples must remain intact when their policy source moves to data.

## Original migration proposal

Use one versioned, repository-owned `.github/ctest-evidence-policy.json` for
preset names, ordered runner bindings and explicit capability exceptions. Keep
the current values as its initial preset. A generic loader would validate the
descriptor, resolve the selected preset, and record the policy version/hash in
the evidence JSON. The policy file and schema would stay in W8's `.github/`
scope. No live provider, UI or product operation is involved.

This would let a future build preset declare its actual compiled capabilities
and supported receipt parsers without another preset-name branch in Python.
It imposes no product resource limit. New capabilities still require real
implementation and execution evidence; a descriptor alone cannot establish
them.

Preserve acceptance behavior by comparing the old and new guard on the same
complete manifest/JUnit bytes and on the existing regression fixtures. Check
identical validity, executed-case/assertion totals, skipped totals and
unexecuted classifications, apart from explicitly added policy provenance.
Retain default names, the exact two ABI skips, all-skip conditions and reason
matching. Do not introduce a generic ignore list, wildcard skip waiver, or a
setting that converts unavailable cases into executed coverage.

The initial inventory changed no code. DIC-0692, the separate seeding
dimensions/projection task, remains a distinct W8 item; the guard migration
does not implement it or revise historical results.

## Implemented follow-up

The guard now reads the versioned `.github/ctest-evidence-policy.json`, validated
against `.github/ctest-evidence-policy.schema.json`. Preset names, ordered runner
bindings and exact named availability conditions are data. The initial values
are unchanged. The CLI accepts explicit `--policy PATH` and keeps the shipped
descriptor as its default for existing invocations. A new declared preset and
runner binding work without adding a preset-name branch to Python.

The loader rejects malformed schema/policy, duplicate keys/selectors, ambiguous
availability declarations, unsupported parser IDs and runner mismatches. The
loaded JSON objects are recursive `MappingProxyType` snapshots and arrays are
tuples, including nested availability entries and runner selectors. Regression
checks reject appending an exception or editing nested selectors, and preserve
the same negative `dev` result and provenance after mutation attempts. Receipts
record schema ID, revision, exact-byte
policy SHA-256, schema SHA-256 and path; valid policy provenance survives later
manifest/JUnit failure. Invalid input produces a negative JSON receipt.

Parsing, complete manifest identity, outer success, positive actual execution,
skip subtraction and unexecuted classification remain universal operations.
The schema exposes no generic ignore or wildcard availability waiver. Named
unavailable cases still need their reason and skip condition; they never become
coverage. This is evidence instrumentation, not a product/resource ceiling.

| Verification | Before | After |
|---|---:|---:|
| Original regression methods | 16/16 | Same 16/16; method bodies unchanged |
| Exact generated-input comparisons | — | 44/44 identical results, excluding added policy provenance |
| Guard regression suite | 16/16 | 28/28, including 12 loader/policy regressions |
| All `.github/scripts` suites | — | 38/38 |
| Complete archived CI manifest/JUnit pairs | 2 | 2/2 exactly equivalent |

The archived `dev` replay remains 108 outer / 107 executed entries, 659 native
cases, 24,465 assertions and 1,276 Python cases. ASan remains 108 / 105,
659 / 24,464 and 1,252 Python cases with 24 reported skips. This follow-up
replays existing bytes; it does not perform another native build or CI run.
The first archived dev/ASan manifests have no original JUnit and are explicitly
excluded from XML equivalence claims. No XML was reconstructed.

[Complete comparison and pinned source hashes](../verification/repo-hygiene-followup-2026-10-04/evidence-policy/comparison.json),
[portable replay](../verification/repo-hygiene-followup-2026-10-04/evidence-policy/compare.py),
[guard regressions](../verification/repo-hygiene-followup-2026-10-04/evidence-policy/regressions-after.log)
and [all script regressions](../verification/repo-hygiene-followup-2026-10-04/evidence-policy/all-github-scripts.log)
retain exact evidence. New cases cover invalid policies/schema, no fallback,
declared presets/runners, reason mismatch, immutable provenance and exceptions
that cannot hide outer failure or a missing manifest entry.

## Do wątku 9

Record the inventory and the separately verified implementation alongside W11's
handoff. Select/rebase this W8 increment, retain exact guard semantics and the
archived comparisons, and use explicit policy selection in CI. Track DIC-0692
through its own implementation/receipt rather than this guard migration.

Do wątku 9
