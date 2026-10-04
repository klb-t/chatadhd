# Settings and graph-provenance continuation evidence

Runtime implementation: `03b0c4e`; focused contract source: `648a4e9`.
Later changes are comments, documentation and standalone contract tests; runtime
implementation files are unchanged. The manifest pins individual source and
binary hashes. The original parent receipt remains immutable.

Final unchanged full CTest: **108/108**, **350.64 s**. Actual-kernel standalone
contracts: **24/24**, including all original 19 groups, four settings groups and
the graph-reference regression. The intermediate settings-only source passed
23/23. No provider calls, test removals, weaker thresholds or timeout changes.
The authoritative preset migration is still blocked, as stated in the report.

## Reproduce the positive result

From a checkout of the contract-source commit, with CMake/Ninja/C++20 installed:

```bash
cd loom
cmake --preset dev -DLOOM_BUILD_SERVER=ON -DLOOM_USE_SYSTEM_SQLITE=OFF \
  -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g0' -DCMAKE_C_FLAGS_DEBUG='-O0 -g0'
cmake --build --preset dev -j3
task_tmp=$(mktemp -d /dev/shm/usage-verify.XXXXXX)
env -u PYTHONPATH TMPDIR="$task_tmp" ctest --preset dev -j1 \
  --output-junit "$task_tmp/ctest.xml"
cd ..
c++ -std=c++20 -pthread -O0 -g0 -Iloom/include -Iloom/third_party/nlohmann \
  -c loom/src/policy/tests/test_usage_policy.cc -o "$task_tmp/contracts.o"
c++ -std=c++20 -pthread "$task_tmp/contracts.o" loom/build/dev/libloom_core.a \
  loom/build/dev/libloom_sqlite3_amalgamation.a loom/build/dev/libloom_miniz.a \
  -lssl -lcrypto -ldl -lm -o "$task_tmp/contracts"
env TMPDIR="$task_tmp" "$task_tmp/contracts"
```

The run used `/root/.local/bin` for CMake/CTest/Ninja because those tools were
absent from its default PATH. Debug assertions and WERROR remain enabled. RAM
temporary storage addressed a full shared disk; its capacity and concurrent CPU
load affect reproducibility. No result assumes a timing improvement in the product.

## Retained negative observations

- `ctest-first-negative.*`: full preset/j4, **95/108**. Eleven compatibility
  entries could not execute the generated helper; two research entries timed
  out at their existing 60-second limits. Restoring the helper's executable
  mode changed no bytes or source. Do not interpret that failure as a weakened
  or removed compatibility gate.
- `ctest-serial-negative.*`: same full preset/j1, **106/108**; both research
  entries timed out. `ctest-research-retry.*`: contracts passed55.70 s,
  structure still timed out; this partial **1/2** run is not the final gate.
- `contracts-diagnostic.txt`: verbose unittest completed **209/209** in65.811 s
  under a90-second diagnostic watchdog. This was deliberately diagnostic,
  not evidence that the unchanged CTest60-second gate passed.
- `structure-enospc-diagnostic.txt`: **842 cases**, **65 errors + 1 failure**,
  with78 ENOSPC occurrences while shared `/tmp` had no free space. Failed
  initialization prevented some test cases from being instantiated.
  `structure-ram-diagnostic.txt`: identical source suite with a dedicated RAM
  TMPDIR passed **859/859** in28.427 s. Final CTest then passed structure45.09 s
  and contracts34.17 s at their unchanged60-second limits.
- Separate interrupted Debug/g0 builds, failed temporary checkout-move command
  and completed build are retained. The normal checkout was restored; the
  actual finished archive was checkpointed in RAM and identical copies were
  compared. No product source was changed to address storage pressure.

The verbose diagnostics used the same discovered unittest suites as CTest:
`python3.12 -m unittest discover -s loom/tools/{contracts,structure} -p 'test_*.py' -v`,
with the repository root on PYTHONPATH. Structure RAM diagnostics changed only
TMPDIR. Final CTest used `env -u PYTHONPATH TMPDIR=/dev/shm/usage-policy-test-temp`
and the preset's generated test environment. Negative attempts retain their
entire text/JUnit outputs; retry success does not erase them. Hashes cover raw
raw output bytes and stored envelopes, and the tracked source plus commands
reproduce the instruments.

Three negative outputs contain original unittest trailing whitespace. They are
stored losslessly as `*.txt.json` / `*.xml.json` gzip/base64 envelopes, with
raw-byte hashes in both envelope and manifest. This preserves every original
byte while keeping source whitespace checks strict. Decode an envelope to a
chosen destination, then verify its SHA-256:

```bash
python3 - /path/to/log.txt.json /path/to/restored-log.txt <<'PY'
import base64, gzip, hashlib, json, pathlib, sys
doc = json.loads(pathlib.Path(sys.argv[1]).read_text())
raw = gzip.decompress(base64.b64decode(doc['data']))
assert len(raw) == doc['bytes']
assert hashlib.sha256(raw).hexdigest() == doc['sha256']
pathlib.Path(sys.argv[2]).write_bytes(raw)
PY
```
