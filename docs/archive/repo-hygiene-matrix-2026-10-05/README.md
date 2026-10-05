# Preserved first attempts — 2026-10-05

These logs are negative or interrupted evidence, never positive test coverage.
No CTest ran in these attempts.

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
