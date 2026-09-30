# W2 — Retrieval, scope and detail

## Identity and integration boundary

- Owner: independent W2 conversation, requested directly on 2026-09-30.
- `base_sha`: `b118c80e981c08ec6d7f9ab6aacc177979186cf2`, checked against the remote integration ref before implementation.
- Branch: `gpt/w2-retrieval-2026-09-30`.
- `head_sha` / verified implementation: `9a6f54c254b3996946bc87067b467d6c927b8361`.
- Verified implementation tree: `cd89eaebd4bb71c4b6a08a8a5d43af7e7c7ad685`. The originally tested local commit `3bd4b9eac3686a491d5bdc8ad117769f4c6d4c90` has exactly this tree; connector publication changed commit metadata only. The following evidence/receipt commit changes documentation only.
- Published ownership checkpoint: `e671b19c981a0fdb93f276fd844546db39ccfa21`; fixed DEV protocol: `4ade73fb5d941505d8009c774e693448218a4bcf`.
- No cross-conversation integrator messaging channel was available. Additive declarations in shared `context_engine.h` are isolated and explicitly require integration review; no agreement with W1/ROOT is claimed.
- W1 checkpoint `dfa28a7d95e18c35a480081c53503f970e20ad6e` was inspected locally. W2 consumes a caller-supplied retrieval projection; it does not compile or replace ActiveTaskSpec.
- No chat, web, model-ledger, shared STATE/index, integration branch, main or PR6 changes. No schema migration or new public C ABI symbol.

## Delivered behavior

The existing native `loom_context_build` JSON consumer executes the new request.
The contract and an example request are in
[CONTEXT_PLAN_RETRIEVAL_2026-09-30.md](../../research/CONTEXT_PLAN_RETRIEVAL_2026-09-30.md).

- Plans contain individually named theses with independent graph radius, display detail, claim/entity anchors, recorded counter-evidence requirements and budget weights. Missing settings inherit the request; explicit empty anchors clear inheritance. Parsing rejects malformed or ambiguous controls.
- Selection apportions one item budget, carries unused capacity forward and preserves per-thesis membership. Zero-budget theses remain explicitly unexecuted. Required premises remain included or marked incomplete. Safe duplicate representations merge with their dependency union; different detail/incompleteness views remain distinct.
- Explicit claim and one-layer recorded counter-link retrieval expose selected, filtered, missing and budget-dropped outcomes. This does not infer new contradictions or prove semantic support for a thesis.
- Per-item route/scoring factors and query diagnostics distinguish missing premises, eligibility filters, store failures, possible truncation and item-budget loss. Recovered budget drops are separated from final omissions. Budget-band sums and forward carry are corrected.
- Existing local TF-IDF VectorSpace and exact lexical search are available through an explicit candidate-channel interface, including native injection of other instruments. Candidate text includes support quotes and can be found outside graph reach. Capability absence/error/unrepresentable vectors remain distinct from measured zero scores. Channel thresholds and limits are enforced for injected instruments too.
- Lexical shadow compares omissions without changing selection. Its gaps remain undecided, not automatically relevant or irrelevant. Raw instrument scores remain separate from reciprocal-rank relevance and evidence confidence.

## Changed files

| Area | Files |
| --- | --- |
| Public C++ contract | `loom/include/loom/context_engine.h`, new `context_plan.h`, `context_retrieval.h` |
| Existing selector | `loom/src/context/context_engine.cpp` |
| Context implementation modules | `context_plan.cpp`, `retrieval.cpp`, `context_candidates.{h,cpp}`, `context_evidence.{h,cpp}`, `context_diagnostics.{h,cpp}`, `context_request_ext.{h,cpp}` under `loom/src/context/` |
| New tests | `loom/tests/test_context_{plan,retrieval,channel_integration,counter,diagnostics,native_consumer,retrieval_dev}.cpp` |
| Existing test fixtures | `loom/tests/test_context_engine.cpp`; assertions retained |
| Contract, protocol and evidence | W2 receipts, contract linked above, `docs/research/w2_retrieval_2026-09-30/verification/` |

## Verification and independent review

Five delegated lanes covered plan selection, retrieval instruments, diagnostics,
synthetic DEV, and native verification. Implementers also reviewed each other's
integration. Review findings corrected generic score-scale assumptions,
injected-channel limits, lexical alias handling, Decision/Claim identity,
filtered mandatory-premise outcomes and counter-observation eligibility.

- Full Debug build with `LOOM_WERROR=ON`, shared ABI, CLI/server and vendored SQLite: **passed**.
- Corrected targeted gate: **20/20 CTest entries passed**, zero skipped, **25.20 s**.
- New W2 tests: **53 doctest cases in seven suites**, included within the targeted/full gates, not added to their denominators.
- Existing controls: **9 cases**; chat-context cases: **15**; ABI exported-symbol, signature and ctypes checks: **3/3**, all passed.
- Full corrected regression: **84/84 CTest entries passed**, zero skipped/disabled, **133 s** (raw JUnit duration), process exit 0.

Exact commands, compiler/dependency versions, source hashes and raw first/corrected
logs are recorded in the
[verification receipt](../../research/w2_retrieval_2026-09-30/verification/README.md).
This verifies the changed tree, not a pristine before-change baseline.

First failures are retained: one inherited budget fixture no longer exercised
incompleteness after deduplication improved; it now uses a larger premise outside
normal goal selection, without relaxing any assertion. The DEV run exposed two
shadow metadata equality failures; consistent direct-route tagging repaired the
implementation without changing the DEV test or gold. Five initial full-suite
failures were missing Python imports in the test environment; the corrected run
uses the same source and binaries. Build logs also retain invalid zero-byte
derived objects and the successful build-directory workaround.

## DEV result and limits

The [fixed protocol](W2-2026-09-30-dev-protocol.md) preceded measurement.
[Results](W2-2026-09-30-dev-results.md) preserve both first and corrected output:
**7 synthetic claims, 6 scenarios, 8 query–gold incidences**, four configurations.
Candidate/selected metrics and ranked IDs/scores are unchanged across all 24
case–configuration rows; corrected dropped metadata is retained in the raw rerun.
MRR averages the five nonempty-gold scenarios, including the repeated low-budget
case; the no-answer scenario is excluded.

| Channel | Candidate evidence recovered | Selected evidence recovered | Selected precision | Candidate / selected MRR |
| --- | ---: | ---: | ---: | ---: |
| Graph | 2/8 | 1/8 | 1/2 | 0.40 / 0.20 |
| TF-IDF | 5/8 | 4/8 | 4/6 | 0.70 / 0.60 |
| Union | 7/8 | 5/8 | 5/8 | 0.70 / 0.60 |

Union coverage improved in this constructed fixture, but irrelevant wallpaper
still ranks ahead of checkpoint evidence. Shadow preserves graph results and
exposes undecided omissions. No answer correctness was measured. TF-IDF/glossary
success is not neural-semantic or general multilingual validation. No quality
threshold was tuned; no sealed validation, private archive, real holdout key,
model call or paid spending was used.

Known boundaries: the plan `source_ref` is opaque caller provenance, not a verified
ActiveTaskSpec binding; counter retrieval follows recorded links only; bounded
queries report possible truncation but do not paginate; weights allocate forward
without retrying earlier theses; the budget estimates rendered items only,
excluding headings/trace and the eventual provider request. Real-export quality,
neural retrieval quality and full R28 realization remain unmeasured.

## W1 / ROOT handoff

The native consumer is ready. Existing chat scope/detail tests pass, but the chat
`knowledge_context` field whitelist still rejects the six new W2 request keys:
`plan`, `claim_targets`, `include_counter_evidence`, `candidate_channels`,
`candidate_scan_limit`, `lexical_shadow`. W1/ROOT should admit these keys at that
adapter boundary and retain `ContextRequest::from_json` validation of their
contents. Native instrument injection remains caller-owned; JSON never activates
a provider or discovers credentials.

W1 should derive a retrieval projection from its already-bound ActiveTaskSpec
where needed, rather than create a second instruction compiler. ROOT reviews the
additive shared header, integrates this branch with W1, then reruns the chat and
common gates and updates shared STATE. No merge or cross-branch integration is
claimed by this receipt. Real-data comparison follows source agreement; the
synthetic result alone does not justify a retrieval-quality claim.
