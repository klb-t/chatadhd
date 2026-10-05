# Preserved first attempts — 2026-10-05

These logs are negative or interrupted evidence, never positive test coverage.
Compiler logs do not certify CTest execution. `gcc-first-attempt/` preserves
the separate first **full** CTest execution: 109/110 passed; research.structure
timed out at the existing 60-second limit. No limit or test was changed.
The direct diagnostic also exposed a missing historical Git object; fetching
the preserved original archive supplied the exact expected tree. The isolated
CTest retry passed in 33.28 seconds and remains a diagnostic, not a full gate.

- `asan-first-build-memory.log`: GCC13.3, ASan/UBSan, full `-g`, parallel2;
  compiler reports a killed `cc1plus`, then the pending command was interrupted.
- `asan-serial-full-debug-memory.log`: the same configuration, parallel1,
  compiler again reports a killed `cc1plus` and Ninja returns failure.
  The shared cgroup memory limit was8GiB and `memory.events` reported5OOM kills.
  Those cumulative counters do not attribute every kill to this session.
- `clang-controlled-pause.log`: ordinary Clang18/vendored compilation on the
  combined W8/W10 archive, intentionally paused to reduce concurrent memory use.
- `clang-main-source-blocker.json` / `.log`: a syntax-only source probe of the
  unchanged main server TU with Clang18 and WERROR. It reproduces exactly the
  two unused `[this]` captures at768/772. This is not a full vendored build.

The W10 source correction is `23c6e74c50b87c2157ffc317d45fca79aade27fa`.
Automatic clean composition (all original W10 changes, with main Packet route
retained) is preserved under `archive/repo-hygiene-w10-validation-2026-10-05`,
commit `b31b4d439594bca0a051a2e95557a3fd06c39141`.
W8 has not edited or suppressed warnings in the server implementation.
Later compiler/test receipts are kept separately in the verification directory.

Final Clang build uses the narrower `archive/repo-hygiene-clang-fix-validation-2026-10-05`
snapshot: only the two applicable original capture fixes over W8. It preserves
the main Packet route without importing unrelated W10 changes. The older full
composition above is retained as historical source, not the final proof.

Other preserved negatives include the first GCC linker memory failure,
controlled compiler pauses, ASan regular-archive disk exhaustion and
`asan-thin-link-disk-full.log`: all objects and three binaries completed,
then linking `cli/loom` failed with ENOSPC. Local thin archives change archive
storage only; sanitizer/compiler flags remain unchanged. Retry outputs are
separate, so a later pass never overwrites a failed attempt.
