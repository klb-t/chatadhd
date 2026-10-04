# W2 integration candidate — checked, not admitted

Reviewed code: lane2 `34cc920dd3cdb0c0fca0a514569b19111429583f`,
linearly rebased hosted source `85ac0a0df74a8fa3504e80bd021ca5f535f421bd`.
Main remains `161cc22`; this source is retained on the integration/archive line.
The newer `03b0c4e` settings/preview delta is not covered by these executions.

Native and web builds pass (85 web modules). Independent standalone kernel
contracts: **19/19**, both fake HTTP sentinels zero. The successful final full
CTest: **108/108**, **107 executed entries**, **659 native cases /24465 assertions**,
**1276 Python cases /0 skipped**. Existing opt-in catalog_scale reports0/0 and
is explicitly classified unexecuted. Context and knowledge each execute18 cases.

The first complete candidate run:106/108, research.structure/contracts hit their
unchanged60s limits. Exact failed logs/XML remain in the bundle; success is a
separate complete run, not combined selected retries. Its elapsed time339.14s.
No tests, assertions, timeout or quality thresholds were changed. Full baseline
failure and earlier build failures are separately
[archived](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-baseline-timeouts/docs/reports/integrator-baseline-timeouts-2026-10-04.md).

**Admission is held:** the new authoritative preset remains literal C++.
Passing behavior tests does not satisfy the owner's new data/profile mandate.
W2 must deliver an actual default-data loader and preserve owner overlays.

[evidence.zip](evidence.zip) retains complete manifests, JUnit, logs, actual-case
guard, standalone source/output and hashes of129 real thin-archive members.
`SHA256.json` hashes each payload. Executable binaries are rebuilt from the
pinned source; they are not replaced by empty objects or stored in this bundle.
Local runtime/build options and exact compile/run commands are in `receipt.json`
and `usage-contract-review/manifest.json`.

Reproduce from the pinned public source using CMake dev, bundled SQLite,
server ON and Python3 withjsonschema/requests/cryptography dependencies.
The recorded low-memory linker/debug/archive flags are local build options,
not repository-source or test changes. Run the full suite without overrides:

```sh
ctest --test-dir loom/build/dev --output-on-failure -j1 --no-tests=error --test-output-size-passed 10485760 --test-output-size-failed 10485760 --output-junit /tmp/replay.xml
ctest --test-dir loom/build/dev --show-only=json-v1 > /tmp/replay-manifest.json
python3 final/verify_ctest.py --preset dev --manifest /tmp/replay-manifest.json --junit /tmp/replay.xml --output /tmp/replay-cases.json
```

Run the19-group standalone source separately with the recorded compiler command;
CMake does not register that test source. All provider interactions use fixtures;
zero paid calls and no private input/credential acquisition.
