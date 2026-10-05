# W10 API increment — full negative evidence

This archive is evidence, not a passing validation receipt. Raw failed attempts are retained unchanged. Final positive runs are deliberately separate.

## Negative cases

| Case | Failure and scope | Replay/source boundary |
|---|---|---|
| api-build-initial / resume | Initial build incomplete; resumed link rejected an empty prompt_contract.cpp.o | Full logs retained. The original zero-byte object and contemporaneous resource metrics were not saved. Resource cause is not proven by this archive. |
| method-ui-syntax | Compiler killed; exit 1 | Full log and source retained; host memory pressure is environmental, not a deterministic source failure. |
| baseline fixtures (first two) | Geometry gate saw six POST then six GET method-settings reads during navigation | Full logs/receipts retained. Historical built web bundles were not retained. |
| methods-ui-attempt-002 / 003 | Missing visible lineage assertion; then only 9 of 10 declared groups | Full synthetic responses/logs retained. Exact original uncommitted JS sources lack a complete receipt. |
| methods-ui-native-attempt-001 | 11/14, native snapshot locator timeout | Complete original request/response captures and manifests retained. Missing JS source bytes are explicitly listed in source-replay-proofs.json. |
| methods-ui-native-attempt-002 | 12/14, float serialization assertion | Same; config-order faulty app is reconstructed with the exact historical SHA. |
| methods-ui-native-attempt-003 | 12/14, selected-profile readback timeout | Original faulty method header retained; reconstructed copy matches exact historical SHA. Source/binary changed between before/after receipts: mixed attempt, not a frozen whole-build receipt. |
| methods-ui-native-attempt-004 | 12/14, result locator timeout | Complete full response/source receipts and log now retained; original test JS hash is checked independently and missing bytes remain explicit. |
| first native onboarding | 1/8, new test wrongly required optional absent W11 foundation | The erroneous test is reconstructed to its exact recorded SHA; source/hash receipts retained. This was a test assumption, not a native onboarding failure. |
| onboarding-host network | Expected unknown-outcome marker absent after lost response and browser-retried POST produced HTTP409 | Full DOM/commands/error retained. Original source not retained; behavior-only fault patch is labelled reconstructed. |
| graph-chat opaque 001 / profile-raw | Malformed authored JSON fixture; then numeric representation assertion | Full logs retained; original uncommitted fixture source/bundle unavailable. |
| graph-chat native 001 | preparation_error/conflict instead of candidate: capture ID collision | Original full negative plus reconstructed byte strings/locators/IDs and fault patch retained. Reconstructed IDs match receipt; faulty native rebuild was not rerun. |
| analysis claim-ID setup | SIGABRT in fixture setup before provider request | Original stderr retained; source/header copies explicitly reconstructed after temp cleanup. No original full-source hash proof. |
| whole-App attempts 1–4 | New test assumed incorrect native data shapes | Exact captured source-inputs, synthetic DB, full HTTP/DOM/error retained; screenshots excluded. |
| whole-App attempt 5 | AnalysisPanel lost LoomHttpApi receiver (`reading req`); zero analysis HTTP requests | Exact captured source-inputs, synthetic DB, full HTTP/DOM/error retained; production binding bug, not native analysis failure. |
| observed Analysis layout clipping | Screenshot from functionally positive attempt 6 (7/7) showed clipping | Necessary PNG plus exact public old CSS blob retained. Separate positive receipt/source-inputs referenced; component CSS hash was not in that receipt, no measured negative geometry receipt existed. |
| full CTest | 120/121 entries passed; knowledge_semantic failed 3 cases/8 assertions | Full log/JUnit/receipt/LastTest/before+after inputs retained. Mixed server/app/header changes during test, not a frozen receipt. Execution-count report is derived after the run, not an original guard receipt. |
| isolated knowledge_semantic | Root-authored read-only focused reproduction; diagnosis attributes W1 stable operation ID + W2 unresolved accounting interaction | Full stdout/JUnit/receipt/diagnosis retained independently; no test thresholds or production source modified here. |

## Source and replay

`source-base-5917495/` contains tracked implementation/build/test/data source from public commit `5917495fe3def9a005dfdddc44623199843d1b0e`; `source-base-manifest.json` lists every byte hash and exclusion. Historical reports/results, compiled binaries, screenshots, media artifacts and holdout-key namespaces are excluded. For the complete authoritative repository and its historical resources, use an isolated checkout of that exact public commit. This package does not claim to replace every excluded repository artifact or the original compiler/system image.

`source-replay-proofs.json` distinguishes originally retained copies, current bytes matching historical hashes, exact-hash reconstructions, and missing original bytes. The config-order reconstruction matches `c34705e...`; the pre-selection header matches `c4536a08...` and also has a separately retained original copy. Exact original test blobs are not claimed where the receipt hashes differ.

Apply one fault only to an isolated checkout, never to the production branch:

```
git checkout 5917495fe3def9a005dfdddc44623199843d1b0e
git apply /absolute/package/replay/config-order/restore-fault.patch
# alternatively: replay/profile-selection/restore-fault.patch
# or raw/graph-chat-negative-source-capture-replay.patch
```

Build the isolated server with the recorded compiler/CMake settings, install the pinned web dependencies and build web. Run `LOOM_SERVER_BIN=<isolated-server> node e2e/methods-ui.mjs --native` or `node e2e/graph-chat-ui.mjs --native` from `loom/web`. Use the archived exact source where it exists; otherwise this is a behavioral replay recipe, not byte-for-byte replay proof. Whole-App attempts retain source-inputs and fixture DB for their exact frontend/native boundary; invoke `e2e/app-native-navigation.mjs --native` using the retained source and a fresh evidence directory. First-onboarding replay uses its exact reconstructed erroneous test and native bridge; the absent optional foundation must remain absent. The onboarding-network patch reproduces only the HTTP409 classification fault and requires the lost-response/browser-retry fixture from `e2e/onboarding-host.mjs`.

The native analysis setup reproduction uses the retained `analysis.cc` plus copied headers/fixture and the compile/run recipe in the analysis harness; the result explicitly states reconstructed source and SIGABRT before dispatch. CTest replay uses the preserved command receipt and pinned source; exact old binaries were excluded. Its original guard/resources/mixed-server execution cannot be fabricated from a later build.

No compiler, test or provider call was run while packaging. Fake loopback credential literals are labelled fixtures; raw evidence is unredacted after the source-provenance and secret-pattern review. `verify-package.py` checks every archived member against the manifest. The published XZ is authoritative; the earlier scratch Gzip was superseded during manifest correction. Compression receipts stay outside the archive to avoid self-reference.
