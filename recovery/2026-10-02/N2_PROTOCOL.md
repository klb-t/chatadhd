# N2 matched cache ablation — protocol frozen 2026-10-02

This finishes the unmeasured cache optimization using a new, explicitly labelled
matched ablation. It does not relabel the interrupted 2026-10-01 diagnostic as
a passing baseline, and is not the originally proposed old-revision comparison.

- Candidate: accepted native source at `af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0`.
- Baseline: the same source with only the two production-file changes from
  `f899559` reversed (`context_engine.cpp` and `retrieval.cpp`). Tests, schemas,
  seed, C ABI, other corrections and compiler settings remain the same.
- Instrument: unchanged `loom/tools/benchmarks/context_candidate_cost_v2.py`.
  Save its SHA-256, source delta and both library hashes with the results.
- Build: GCC 13, Debug, `-O0 -g0`, bundled SQLite, warnings as errors.
  The missing debug symbols do not turn off assertions. Generated files live
  on a local temporary volume after zero-byte build artifacts were observed
  in the synchronized workspace. Do not attribute those artifacts to C++ code.
- Baseline runs first, candidate second, with no concurrent build/test work.
  Matrix: 64/256/1024 claims × 1/4/16 theses × TF-IDF/uninstalled control.
  Each of 18 fresh child processes performs one cold and two warm calls.
- Required correctness: exact seed/request/cold output equality across arms;
  all three output hashes stable inside each cell; no error/timeout.
- Report all 18 cells, wall and CPU timings, memory snapshots and controls.
  Use medians of the two warm calls; retain each individual observation.
  This is one paired sequence, not a statistically powered performance study.
- No live provider, credentials, paid calls, sealed evaluation or real-user data.
- A failed/noisy result remains a result; retain first attempts and state the
  limit. Do not revise the instrument, retry for a nicer score or claim quality
  improvement from exact computational reuse.

The final active source must restore both original files byte for byte before
the combined CTest and browser gates. Original negative/diagnostic evidence
remains reachable from `af3c81a` regardless of active-tree curation.
