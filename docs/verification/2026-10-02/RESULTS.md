# Fresh verification — 2026-10-02

**The curated accepted source passes 107/107 CTest entries, zero failed/skipped,
in 82.02 seconds.** The command had `PYTHONPATH` and `TMPDIR` explicitly unset;
the repository's own CMake test setup supplied what its tests require.
These are suite entries, not individual assertions. Historical counts are not
added to this denominator.

Native implementation is byte-for-byte source from
`af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0`; the active-tree change is archival
selection/current documentation plus the separately tested analysis experiment
preparation/scoring tool. All original-main paths are retained.

| Gate | Fresh result |
|---|---|
| C++20 Debug build, warnings as errors, bundled SQLite, server enabled | Passed |
| Combined CTest, preset `dev`, four jobs | 107/107; 82.02 s |
| Structure after the added prompt/parameter experiment tool | 853 tests; CTest `research.structure` passed in 29.25 s |
| New experiment boundary tests | 10/10, included in that 853; not model-quality evidence |
| Cost auditor's dedicated fixture checks | 5/5 |
| Web TypeScript/Vite production build | Passed |
| Retrieval-plan state and mocked transport | Passed |
| Browser-local workspace state | 8/8 groups |
| Browser → real native workspace | 10/10 scenarios |
| Browser → real native chat → local fake provider | Passed; 5 captured provider calls |
| General browser/native end-to-end suite | 16/16 steps |
| N2 matched cache ablation | 18/18 exact seed/request/output pairs; 54 calls per arm |
| New Jev/cheap-LLM quality study | 432 requests prepared; 0 executed; credential unavailable |

The first combined CTest preceded the ten new experiment tests. After that
Python-only addition the complete structure entry was rerun; native code,
headers, tests and measured shared library remained unchanged. Direct earlier
843 structure / 197 contract / 50 coordination counts are supporting runs,
not extra tests to add to the combined CTest denominator.

## Environment and reproduction

GCC 13.3.0, CMake 4.4.3, Python 3.12.14, Node 24.19.0. Exact package and binary
hashes are in [receipt.json](receipt.json). Debug uses `-O0 -g0`; assertions and
warnings-as-errors remain enabled. This is not a Release or sanitizer result.
Android packaging and real external providers were not exercised.

```bash
python3 -m pip install cmake ninja requests cryptography -r loom/tools/contracts/requirements.txt
cd loom
cmake --preset dev -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_SERVER=ON -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g0' -DCMAKE_C_FLAGS_DEBUG='-O0 -g0'
cmake --build --preset dev -j 4
env -u PYTHONPATH -u TMPDIR ctest --preset dev
```

Use the actual installed CMake/Ninja executables if they are outside PATH. In
this runtime generated build files were moved to a local temporary volume with
`loom/build/dev` pointing there. Public source and committed settings were not
altered for that relocation. Web gates use `npm ci`, `npm run build`, then
`test:transport`, `test:context-plan`, `test:workspace`, `test:chat-context` and
`e2e`. The supported `PLAYWRIGHT_CHROMIUM_EXECUTABLE` override selected the
installed Chromium headless shell. Browser payloads/trace snapshots are in the
[recovery archive](../../archive/README.md).

## First attempts retained

Initial native builds produced several zero-byte object files and a malformed
static archive in the synchronized workspace, although compilation steps had
reported success. Rebuilding generated artifacts on the local volume completed
without source changes. The underlying filesystem cause was not proven; this
is not a repaired C++ defect. Initial logs are retained.

Instrumented Python discovery while compilation was running hit two 60-second
timeouts. The final uninstrumented preset kept its original timeouts and passed.
One diagnostic invocation from stdin was incompatible with multiprocessing
spawn; normal module execution passed. The first workspace browser attempt
preceded the native server build and failed its prerequisite check; final native
browser runs passed. None of these first attempts is relabelled as a pass.

## N2 performance outcome and limits

[All paired timings and memory snapshots](N2_TFIDF.md) retain the uninstalled
channel controls. TF-IDF warm wall ratios range from **0.98× to 4.28×**; the
largest 1,024-claim/16-thesis cell went from roughly 28.85 s to 6.74 s warm.
Controls range from **0.75× to 2.03×**, showing environmental variability.
This one fixed-order Debug matrix is evidence of exact output preservation and
a workload-specific reuse gain, not a general speed guarantee. No semantic
quality improvement follows from it. The protocol was published before either
arm ran, at archive anchor `9626969bce806b2270d764ebdea3a8e139453de3`.

No paid model call, private export, sealed holdout or GitHub Actions run was
part of this verification. The separate
[prompt/parameter study](../../research/analysis_optimization_2026-10-02/README.md)
needs authenticated access before its prepared requests become measurements.
