# Authoritative usage preset — fresh actual-kernel receipt

Based on main `7282437b1c88933977f64b3468b9f42f7b400494` plus W2 only.
Production/data commit `a3b127cb2cfd63eab9491e46b258d72965c72e26`;
native verification source `5a73a360f44626333ce6510f26261c32239de73c`.
All evidence is offline and synthetic. Zero paid calls; no tests, timeouts or
thresholds changed. Earlier receipts and negative attempts remain untouched.

## Results and identities

- Debug/O0/WERROR/assertions, bundled SQLite, server/CLI/shared/tests enabled:
  full CTest **108/108 in 274.38 s**.
- Focused actual-kernel policy groups **25/25**, preserving the original19;
  generator cases **5/5**; isolated native variants **4/4**.
- One source document supplies all five default paths; no manually initialized
  preset values remain in production C++. Config shallow overlay survives save
  and restart and does not require rebuild. Fresh reads create no config file.
- Edited data are genuine isolated regenerated consumer objects linked before
  the actual archive, not replacement API implementations. Missing-field and
  factor1 variants fail through checked/legacy APIs without creating a ledger.
  Complete commands, sources, results and hashes are in `source-edit/manifest.json`.

`manifest.json` pins raw/stored bytes, source hashes, binaries, compiler settings
and complete-output provenance. `source-edit-retention.json` maps original proof
names to retained bytes. Copied `.cpp` sources use `.cpp.txt` so the kernel's
recursive source glob cannot compile evidence. Buildable objects/executables and
empty synthetic SQLite fixtures have recorded hashes and are reproduced by the
runner; their binary bytes are not checked in.

## Faithful complete CTest output

The original JUnit capped stdout in16 entries. Its first independent coverage
guard correctly rejects that incomplete case evidence; see
`guard-original-negative.json`. The original XML and complete same-run
`LastTest.log` are both retained losslessly. `restore_ctest_output.py` cross-checks
all108 names/order/statuses/durations/run-start/output prefixes and replaces only
the16 capped stdout fields. Original result metadata and all other XML bytes
remain unchanged. `restoration.json` records each comparison. Eight inconsistent
input controls are rejected; a matching synthetic failure stays a failure.

The unchanged W8 guard from commit `2544cf3a75c1477797213328f7279b3e8576579e`
accepts the restored complete XML: **107 executed entries, 659 native cases,
24,465 assertions, 1,276 Python cases, zero Python skips/errors**. The existing
opt-in `unit.test_catalog_scale` entry is explicitly unexecuted. Thus108 outer
passes do not imply108 entries executed inner cases. No test rerun or status
change was used to fix stdout capture.

## Reproduce

From the repository root, with the documented development build configured:

```bash
python3 loom/src/policy/gen_usage_policy.py --check
python3 -m unittest discover -s loom/src/policy/tests -p test_gen_usage_policy.py -v
cmake --build loom/build/dev --target loom_core
python3 loom/src/policy/tests/verify_preset_paths.py --output-dir /tmp/usage-preset-proof-new
```

The output directory must be new/empty, so prior evidence cannot be overwritten.
The runner uses the actual `compile_commands.json`, consumer source and current
static kernel; canonical repository inputs are checked unchanged before/after.
The standalone25-group compile/run recipe is in the policy README.

Raw logs/XML use the existing `loom.raw_log/1` gzip/base64 envelope format.
Decode one envelope to a chosen file, retaining every original byte:

```bash
python3 - /path/to/ctest-original.xml.json /tmp/ctest-original.xml <<'PY'
import base64, gzip, hashlib, json, pathlib, sys
doc = json.loads(pathlib.Path(sys.argv[1]).read_text())
raw = gzip.decompress(base64.b64decode(doc['data']))
assert len(raw) == doc['bytes']
assert hashlib.sha256(raw).hexdigest() == doc['sha256']
pathlib.Path(sys.argv[2]).write_bytes(raw)
PY
```

Decode `last-test.log.json` and `ctest-list.json.json` similarly, to
`/tmp/last-test.log` and `/tmp/ctest-list.json`. The latter preserves CTest's
original JSON formatting and input hash. Independently reproduce restoration
and verification without rerunning tests:

```bash
python3 loom/src/policy/tests/restore_ctest_output.py \
  --junit /tmp/ctest-original.xml --last-test-log /tmp/last-test.log \
  --output /tmp/ctest-full.xml --receipt /tmp/restoration.json
git show 2544cf3a75c1477797213328f7279b3e8576579e:.github/scripts/verify_ctest.py > /tmp/verify-ctest.py
python3 /tmp/verify-ctest.py --preset dev \
  --manifest /tmp/ctest-list.json \
  --junit /tmp/ctest-full.xml --output /tmp/coverage.json
```

Receipts describe this actual build, not other lanes' unmerged integrations or
a matched performance comparison. Generic RuntimeProfile overlays, startup DIC
and public ABI ownership are separately recorded in the lane report.
