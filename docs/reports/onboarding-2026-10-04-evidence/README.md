# W12 offline validation evidence

Final native acceptance: **112/112 CTest entries passed, exit 0, 185.29 s**. This archive accompanies [the W12 report](../onboarding-2026-10-04.md). It retains UTF-8 logs, exact fixture inputs, runners and hash manifests collected from `/tmp/onboarding-verification-2026-10-04`; binaries, databases, build trees and private conversations are excluded. W12 model replies and receipt fixtures are saved or synthetic; no live provider calls were made for the checks described here.

## Final results and their denominators

| Check | Result | Evidence |
|---|---|---|
| Strict native build and full CTest | Build exit 0; 112/112 entries pass; 0 failed or skipped CTest statuses | [configuration](native/configuration.json), [build](native/build-final.log), [CTest log](native/ctest-full-final.log), [JUnit](native/ctest-full-final.xml), [final verdict](native/final-verdict.json) |
| Executed native tests | 710 cases, 27,169 assertions; 0 failures across 85 native CTest entries | [untruncated LastTest](native/ctest-full-final-LastTest.log), [independent count query](native/doctest-final-count.log), [count derivation](native/derive_final_verdict.py) |
| W12 native tests | 51 cases, 2,704 assertions: profile 18/228; graph 11/2,139; layers 8/51; store 14/286 | [per-entry verdict](native/final-verdict.json) |
| Python unittest checks | 25 CTest entries report 1,276 cases, 0 skips | [untruncated LastTest](native/ctest-full-final-LastTest.log) |
| Server and CLI smoke | Both scripts pass; no unittest case denominator | [per-entry verdict](native/final-verdict.json) |
| Generated embedded onboarding data | Generator `--check` exits 0; successful log is empty | [command and result](native/builtin-generated-check.json), [log](native/builtin-generated-check.log) |
| UI controller | 17/17 tests pass, exit 0 | [command metadata](ui-final/controller-tests.json), [log](ui-final/controller-tests.log) |
| UI TypeScript/Vite build | Exit 0; 85 modules transformed | [command metadata](ui-final/web-build.json), [log](ui-final/web-build.log) |
| Actual W3 MethodRegistry declaration contract | Load/resolve pass, including two versions of one method | [verification](w3-method-contract-final/verification.json), [runner](w3-method-contract-final/runner.cpp), [run](w3-method-contract-final/run.log) |
| Actual W11 RuntimeProfile adapter supplement | Standalone mixed-source harness: 8 cases, 58 assertions pass | [verification](w11-runtime-adapter/verification.json), [run](w11-runtime-adapter/run.log), [input hashes](w11-runtime-adapter/input_source_hashes.json) |
| Independent targeted reproductions | Two executables exit 0 | [scope](independent-review/README.txt), [commands](independent-review/commands.txt), [result](independent-review/result.json) |

`unit.test_catalog_scale` reports **0 executed cases and 0 assertions** in this run. Its opt-in approximately 1 GB corpus case was not enabled with `LOOM_RUN_SLOW_TESTS`; this is not evidence of corpus-scale acceptance. Successful JUnit stdout truncates at 1,024 bytes for some entries, so case totals are derived from the full LastTest log, not JUnit excerpts. Native cases, Python cases and CTest entries use different denominators and are not added together.

## Exact source and configuration binding

The final [335-input native manifest](native/native-source-hashes-final.json) was captured with HEAD `8fd3cbcceb1b24f8d6a91ad14d53a2cf22ea5f93`, before the final source bytes were published. The [after-publication comparison](native/native-source-hashes-after-publication.json) checked all 335 inputs against published HEAD `40907ad28f3244bfe508da647273d2e1d3bcf1b3`: **no changed or missing inputs**. Its source-map digest is `dbe6532d6482e64b3feab17014e350297cbd80071fcff37360e31b98f21ee487`; this differs from the SHA of the complete JSON artifact because it identifies the source map. [Capture script](native/capture_native_manifest.py) and [summary](native/native-source-hashes-final-summary.json) retain that distinction. Base main was `30ad7d37337d6641cb7714b03e9feff0e6e25d25`.

[Configuration](native/configuration.json) records Ninja, GNU 13.3.0, Debug `-g0 -O0`, `LOOM_WERROR=ON`, shared library, CLI, server and tests enabled, bundled SQLite, OpenSSL AUTO resolved enabled, and parallelism 2. Its HEAD and timestamp describe metadata capture, not the final source identity or build start. [Configure output](native/configure.log) and the final verdict provide exact commands. Full CTest used `env -u PYTHONPATH .../ctest --test-dir /tmp/onboarding-build-2026-10-04 --output-on-failure -j2 --output-junit .../ctest-full-final.xml`. The derivation script uses the original absolute workspace/log paths; adapt its `folder` and `repo` variables when replaying the archived inputs elsewhere. No thresholds or assertions were relaxed.

[UI source hashes before](ui-final/sources-before.json) and [after](ui-final/sources-after.json) match, including actual onboarding packs and web configuration. [UI summary](ui-final/SUMMARY.json) records exact commands and boundaries. The UI checks exercise standalone exports and a recorded adapter. App/navigation wiring and production transport integration are not exercised; native persistence and privacy enforcement are tested separately.

## Dependency and provenance boundaries

Final W3 evidence uses actual public W3 source commit `03c670caa6ee3d8ac2c478186548114f8e83927f`; [verification](w3-method-contract-final/verification.json) binds its source hashes, W12 export hashes and linked native core hash. The actual registry accepts the current [W12 method profile](w3-method-contract-final/input_method_profile.json), unavailable/advertised host resolution, parameter precedence, tampered-source rejection and [two-version fixture](w3-method-contract-final/multiversion_method_profile.json). The [saved receipt projection summary](w3-method-contract-final/native_saved_receipt_fixture_projection.summary.json) retains the original raw projection hash and exact two provenance edges with referenced entities and observation; the redundant full 2.3 MB graph is omitted. Receipt data is synthetic. **Actual method executions: 0; provider calls: 0.** Producer execution and a joint W3→W4 execution gate remain unverified.

The W11 supplement runs against extracted actual RuntimeProfile sources from public commit `d3488a6bee016d1ea5e6e286dbc7d3a0249761fc`, byte matches recorded in [input hashes](w11-runtime-adapter/input_source_hashes.json). Original compile-time source sealing was unavailable: these source hashes were captured after compilation, alongside the executable hash. This supports the targeted adapter harness, not a whole-repository or OnboardingStore acceptance run with W11 integrated. Main's optional-loader absence remains an explicit `Unavailable` status; no duplicate schema interpreter substitutes for RuntimeProfile.

Independent reproductions directly recompile current store/graph sources while using an exact copied static-library archive identified in [compile inputs](independent-review/compile-inputs.json). Frozen workspace hashes stayed unchanged before/after. They do not reconstruct source provenance for objects already in that archive and do not replace full CTest acceptance. The 325 MB archive and generated executables are not included.

## Historical negatives and interrupted runs

These records explain corrected fixtures and implementation iterations; they are not final acceptance results.

| Record | Status and correction |
|---|---|
| [Initial onboarding log](historical/ctest-onboarding-initial.log) / [JUnit](historical/ctest-onboarding-initial.xml) | 3/4 entries passed; missing-vocabulary fixture needed a pack revision change. Assertion retained. |
| [Typed assertion build failure](historical/build-test-type-initial.log) | Compile failed on Json/string comparison in a new receipt regression; corrected typed string value, assertion retained. |
| [Multiple-version fixture log](historical/ctest-onboarding-multiversion-fixture-negative.log) / [JUnit](historical/ctest-onboarding-multiversion-fixture-negative.xml) | 3/4 entries passed; changed fixture pack needed a revision change. Assertion retained. Final [graph rerun](native/ctest-onboarding-graph-final.log) then passed, followed by full 112/112. |
| [First interrupted full run](historical/ctest-full-interrupted-source-change.log) | Interrupted with exit 130 after source changes; no full-run verdict. |
| [Second interrupted full run](historical/ctest-full-interrupted-method-version-fix.log) | Interrupted with exit 130 after source changes; no full-run verdict. |
| [Earlier W3 checkpoint](historical/w3-method-contract/verification.json) | Earlier positive declaration checkpoint; superseded by the final W3 export above. |

Four historical native hash maps are retained losslessly as [deltas](historical/source-manifest-deltas.json) against the final map. Their original metadata and complete-file hashes are recorded. Reconstruction copies the final `sha256` map, removes `missing_paths`, applies `replacements`, sorts paths, then writes `original_metadata` followed by `sha256` with Python `json.dumps(indent=2) + '\n'`; byte-identical hashes were checked before removing duplicate maps.

## Archive integrity

[Validator artifact hashes](native/verification-artifact-hashes.json) identify exact original validation files, including historical maps represented here by deltas. [Archive manifest](artifact-manifest.json) lists every retained file's SHA-256 and byte size, excluding the manifest itself. All retained files decode as UTF-8. The manifest provides integrity and origin mapping; the scope limits above still apply.
