# Native full-gate boundary archive, 2026-10-05

This archive preserves two complete, unsuccessful 108-entry CTest runs, their exact affected Python sources, the initial native build/link boundary, and the corrected complete 108/108 replay. It contains verification logs and source snapshots only. It contains no credential file, environment dump, private financial ledger, or paid-provider response.

| Boundary | Observed result | Exact evidence |
| --- | --- | --- |
| First full CTest | 107/108, 121.33 seconds, exit 8 | `provisional-1/` raw CTest, JUnit, LastTest, LastTestsFailed, source map, counts and run status |
| Second full CTest | 107/108, 130.33 seconds, exit 8 | `provisional-2/` complete equivalent evidence |
| Corrected full replay | 108/108, 128.72 seconds, exit 0 | `corrected-full-replay/` complete raw/JUnit/LastTest, before/after maps and counts |
| Initial native build | exit 1: two generated object files were zero bytes; linker rejected the store object | `initial-native-build/build.log` and `build-provenance.json` |
| Generated-object repair and continued build | only the two generated objects removed/recompiled; continuation exit 0 | `initial-native-build/rebuild-empty-objects.log`, `build-resume.log`, exact commands in provenance |
| Compatibility-tool mode repair | observed 0644 changed to 0755; file SHA256 unchanged | `initial-native-build/build-provenance.json` |

The first failure was flat unittest discovery importing `test_stage2_semantic_review_v1.py` with a relative import. Its original uncommitted bytes were recovered by reversing the recorded one-line fix; their SHA256 exactly matches the first full-run map. The corrected import uses `loom.tools.structure.stage2_semantic_review_v1`. Four focused tests then passed (`semantic-flat-discovery.log`), and the complete corrected replay passed.

The second failure was `test_model_study_readiness_v1.ReadinessTests.test_original_first_failure_and_successor_request_equality`: appending a pure aggregation helper to the historically hash-bound `analysis_optimization_v1.py` added a real third source-drift entry. The expected drift set and test threshold were preserved. The legacy producer was restored byte-for-byte to SHA256 `b7c2ce5541c0f3fd7f0d647074105fd47bc414d287267ec939598b952b1e1a8d`, and new aggregation moved to `programme_study_results_v1.py`. Exact original producer/module/test bytes are under `sources/provisional-2/`; every archived affected source matches the original map, as recorded in `affected-source-provenance.json`. No source was accepted merely because an inverse patch looked plausible.

The first run began at Git HEAD `df9d7c6e30c320772c0c3e54e2d6ec6716002447`; the semantic producer/test were then uncommitted. The second began at `83a12601be1555cb2a3d4478d8a3f3c0b2aca479`; the appended scorer and new programme files were uncommitted. Each run status records concurrent functional edits, so these are preserved negative boundaries rather than frozen acceptance gates. The exact affected sources supplement their Git references and full SHA256 maps.

The corrected complete gate began and ended at HEAD `467b153a4729d1742f6d9c77470e0e028d0a0f5d`, with the separately captured uncommitted functional candidate. Its source map includes tracked and untracked code, native fixtures/data, and both public billing policies. All 1,041 functional file hashes, both policy hashes, and all six binary hashes/modes were unchanged after the gate. Native Git inputs match configured build commit `70587e4d12fac9fa0042e554dbeb7a4aa60127ba`. This records the observed built artifacts and continued-build result; it does not claim that another clean rebuild was run after the recorded repairs.

The final raw output and JUnit independently show 108 passing entries, zero failures, and zero skipped entries. The 81 native entries executed 659 cases with 24,465 assertions. The 25 unittest entries executed 1,582 tests with zero skip events. Both additional CLI/server smoke suites emitted their required completion markers, without SKIP markers. Thus there are 2,241 explicitly counted unit cases plus two custom smoke suites; no assertion cardinality is invented for the smoke scripts. The existing optional catalog-scale stress case remains source-disabled by default, while all 81 registered native suites ran; filtered doctest summary counts are not added as test executions.

Full-run command (caller `PYTHONPATH` and `TMPDIR` explicitly unset; CMake's own registered test environments remain authoritative):

```sh
env -u PYTHONPATH -u TMPDIR /root/.local/bin/ctest \
  --test-dir /workspace/scratch/407fe6f6ff42/build -j2 --output-on-failure \
  --output-junit /workspace/scratch/407fe6f6ff42/native-logs/final-current/ctest.junit.xml
```

`audit_native.py` reproduces the source/binary snapshots and parses the raw/JUnit counts in the recorded workspace. Configuration and build commands, compiler/CMake versions, native Git object IDs, 229 validated ELF objects, and exact six binary SHA256/modes are in `initial-native-build/build-provenance.json`. The native configuration is Debug `-O0 -g0`, WERROR on, vendored SQLite, and shared library/CLI/server/tests on. The initial build used `-j2`; generated-object repair and continuation used `-j1`.

`archive-manifest.json` binds every packaged file by size and SHA256. ZIP entry paths are relative and contain no traversal components. The publisher owns the archive commit and later binding of the accepted code commit to the successful functional map.
