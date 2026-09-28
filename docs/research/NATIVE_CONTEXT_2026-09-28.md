# Native local-context correction

This is a production correctness change alongside the separate Python research
experiments. It does not integrate their grammar, topic segmenter or reasoning
engine into the application.

## Changes

- Ambiguous aliases now obey `thresholds.catalog.context_window_tokens` (default
  30 Unicode word tokens on each side of an occurrence). Previously the code
  searched the entire conversation despite the profile's documented window.
  Context cues retain their existing folded substring/stem semantics.
- Rejected alias matches no longer receive a title bonus. Version mentions must
  be near an accepted identity alias; philosophy probes and rejected aliases
  cannot anchor them.
- Scores expose actual `identity_alias_hits`, `principle_hits` and whether source
  context was available. `id_hits`, `class_diversity` and `code_evidence` retain
  their old combined alias/principle transforms and existing policy weights.
  The result explicitly identifies those legacy combined features. No weight or
  relevance threshold was changed.
- If original and retained source bytes are both unavailable, scoring does not
  reuse stale identity/principle/version mentions. Source availability is not
  called semantic identity verification.
- Scanner version 4 and knowledge pipeline version 5 invalidate the corresponding
  derived products. Scan checkpoints additionally fingerprint scanner, data pack
  and sketch parameters. Existing content with an old fingerprint refreshes its
  derived mentions/sketch while preserving unit/source IDs, locator, content hash,
  creation history and retained source bytes.
- A narrow additive migration adds `input_hash` to `loom_cat_checkpoint`; older
  checkpoints have an empty fingerprint and cannot suppress refresh. Core schema
  version 4 and the existing Python/C++ data model remain unchanged.

This token window is not semantic segmentation. Cues in a different sentence but
within the same window can still influence a mention. Inflection, quoted namesakes,
unnamed references and late-topic continuity remain separate unsolved problems.

## Verification

The seven new `test_catalog_context.cpp` cases check local positives versus distant
decoys, Unicode windows, title/version evidence, missing-source behavior, migration
and pack/sketch refresh without losing provenance. They pass in the final local
build. Full CTest is **62/63 passing** (49.94 seconds); ABI, Python compatibility,
HTTP server, CLI, retention, attribution and research protocol checks pass.

The sole failing gate remains `unit.test_catalog_eval`: recall **13/45 = 0.288889**
against the unchanged **0.55** minimum, labeled precision **13/13**, selected noise
**0/20**, and three auxiliary selected documents outside conversation labels.
Selected conversations do not change. Internal relevant/candidate labels become
9/7 from 10/6; this is not a recall improvement.

The independently authored 100-case source-only corpus also shows **zero selection
or score changes**. Per split it still selects five true direct-project cases and
two false positives; development misses three positives and validation misses
four. Thus mechanism regressions establish the correction, but this suite does
not establish a retrieval-quality gain. See the independent fixture's
`CATALOG_RESULTS.md` and before/after machine reports.

## Offline test repair

An automatic approval review stopped the initial full suite because an old C ABI
smoke test called GitHub status through the real default transport. Inspection
confirmed an unauthenticated, empty-body GET to
`https://api.github.com/repos/a/b/contents/?ref=main`; it was an obsolete assumption
that this API was still a stub. The request was not needed for the test.

Every fixture in `test_capi.cpp` now installs a deny-by-default callback and fails
on any unexpected request. The GitHub status transport path receives a local
scripted response and asserts URL, method, empty body and absence of authorization.
Another assertion verifies callback errors propagate without live fallback. The
corrected test and subsequent full suite ran successfully apart from the existing
catalog recall gate. No live request or permission workaround was used for retry.

The local build uses system SQLite, the restored project header and GCC in the
development preset. Browser/JNI checks from the prior round remain historical;
they were not rerun because this change does not modify those transports or UI.
No new ASan, Clang, Android-device or real-owner-export result is claimed.
