# Catalog follow-up: DEV only

Base: `30ad7d37337d6641cb7714b03e9feff0e6e25d25` (current main at measurement).
Production change: `d02d2015c1c815b6f706c3b70d01b293f9feb060`.
Native tests/evaluator: `b687464c5804ceaa8699d1f6ba412f2f142aade0`.
The measurement inputs recorded the preceding HEAD with local changes;
their source manifests bind the exact measured files. All seven changed
production/evaluator/test source hashes match these published commits.

No blind or holdout data was read again. No provider calls, paid requests,
private archives or external vectors were used in the relevance calibration.
The public fictional DEV aliases and evaluation labels remain separated:
aliases are declared profile input; labels are used only for metrics.

## Measurements

| Receipt | TP / FN / FP / TN | Meaning |
|---|---|---|
| `before.json` + `before.inputs.json` | 31 / 14 / 0 / 20 | Refreshed main-base default before the recipe decoder |
| `after.json` + `after.inputs.json` | 31 / 14 / 0 / 20 | Final decoder, unchanged default |
| `calibrated.json` + `calibrated.inputs.json` | 33 / 12 / 0 / 20 | Optional full-file DEV overlay, bias −2.9 |

All 68 default score rows, labels, features, reasons and decisions are equal.
The optional overlay rescues `nf-02-storage` and
`nf-12-encryption-and-lost-again`, loses no previous selection and worsens
none of the recorded AUC/hits metrics. This is DEV calibration, not an
independent quality check. The built-in policy was not promoted or changed.
The original five lexical rescues remain selected.

Default and calibrated final measurements use the same shared library:
SHA-256 `4884ae327b984504ccd5807b2c77bd0c1d6482595bce4fc6c2b7a574c38af6ab`.
`before` uses an earlier library; exact binary/source/pack/fixture hashes
are retained in each input receipt. The mechanism replay uses a preceding
library (`3bd98d22…`), with its own exact recorded source manifest.
Its label-assigned mock vectors test admission/restoration only;
14/14 mock rescues are not model recall evidence.

Candidate bias −2.5 was rejected because lexical and local TF-IDF AUC
regressed. Its complete policy, raw rows, inputs, evaluator and reproduction
remain on `archive/2026-10-04/catalog-dev-bias-negative`,
commit `f61c94424b3f9953e07237beac0f1d2c8e77dd22`.

## Verification receipts

- `native-tests.log`: 18 cases, 260 assertions; strict `-Werror` build.
- `recipe-tests.log`: 7 cases, 255 assertions; strict `-Werror` build.
- `native-compile.log` / `recipe-compile.log`: compiler diagnostics; an empty
  file means a successful quiet compile, not zero tests.
- `configure.log`, `core-build.log`, `server-build.log`: final build/configure.
- `web-dependencies.log`, `web-build.log`: offline dependencies and successful
  web build (85 modules); no tracked web files changed.
- `ctest-108-first-timeout.log` and `ctest-108-first-raw.log`: first complete
  server-enabled run, 107/108; `research.contracts` timed out at the unchanged
  60 s. Keep this failure even if a later gate succeeds.
- `ctest-108-final.log` / `ctest-108-final.xml` / `ctest-108-final-raw.log`:
  final **108/108** full server-enabled gate, 354.07 s, unchanged timeouts;
  the original JUnit XML and full raw log are both retained.
- `ctest-manifest.json` / `ctest-108-context.json`: test inventory and exact
  gate command/binary/base, no external `PYTHONPATH` or `TMPDIR`.
- `contracts-diagnostic.log`: separate 209-case diagnostic, 58.82 s;
  it does not replace the full gate.
- `resource-relocation.json`: own tmpfs artifacts moved to disk before the
  final rerun, 1,228,260,408 bytes, all copied file hashes equal.
- `verification.json`: 57 checks on saved DEV receipts, including exact
  equality of all complete default rows and all three channel scores.
- `ctest-106-diagnostic.log`: earlier 106/106 diagnostic, with the server
  disabled and query rebuilt during that run. It is not the full final gate.
- `native-preliminary-failed.log`: initial native diagnostic, 15/18 cases;
  ordered-JSON comparison and an old object behind a build symlink were
  corrected before the final 18/18 run.

The final full CTest result and machine-readable manifest are recorded in the
[thread report](../../../../../../../docs/reports/catalog-selection-2026-10-04.md).
Failures are diagnostic history, not passing verification.

The unchanged count guard from main's integrator evidence confirms 659 native
cases / 24,465 assertions and 1,276 Python cases / zero Python skips. There
are 107 executed entries; the existing opt-in `unit.test_catalog_scale` ran
zero cases and is explicitly unexecuted, not counted as coverage. The separate
owned 18/260 and 7/255 runs are not part of the 659 native cases.

The original JUnit capture truncates 16 system-out texts at 1,024 bytes.
`count-guard-original.json` / `.log` retain that rejection. The separate
`ctest-108-restored.xml` restores only those output texts from the same run's
complete raw log, with matching names, commands, statuses and prefixes. All
other XML contents/attributes remain equal; original inputs are unchanged.
`output-restoration.json` records hashes and that transformation boundary.
`count-guard-restored.json` / `.log` contain the PASS. No tests are rerun by
this transformation. Both `restore-output.py` and the unchanged
`count-guard-source.py` are saved for replay; `count-guard-replay.json` records
their original commands and source provenance.

## Reproduce

From the repository root, with a completed native build:

```sh
bash loom/src/catalog/tests/run_native.sh loom/build/dev /tmp/catalog-native-followup
python3 loom/src/catalog/tests/evaluate_dev.py \
  --library loom/build/dev/libloom.so.0.1.0 --output /tmp/catalog-default.json
python3 loom/src/catalog/tests/evaluate_dev.py \
  --library loom/build/dev/libloom.so.0.1.0 --output /tmp/catalog-calibrated.json \
  --relevance-overlay loom/src/catalog/tests/results/2026-10-04/dev-followup/relevance-dev-bias.json
python3 loom/src/catalog/tests/verify_dev_followup.py \
  loom/src/catalog/tests/results/2026-10-04/dev-followup
```

The overlay is the complete native `policy/relevance.json` replacement,
installed under the temporary runtime's `kb/policy/` before opening it.
It changes only the intercept. Profile aliases, thresholds, remaining
relevance parameters and selection rules stay identical.

The pure decoder regression can be built independently of the static core:

```sh
c++ -std=c++20 -pipe -Wall -Wextra -Wpedantic -Wshadow \
  -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual \
  -Wnull-dereference -Wimplicit-fallthrough -Wno-unused-parameter -Werror \
  -Iloom/include -isystem loom/third_party/nlohmann -isystem loom/third_party/doctest \
  loom/src/catalog/tests/test_relevance_recipe.cc loom/src/catalog/relevance_recipe.cpp \
  loom/src/util/json.cpp loom/src/util/utf8.cpp loom/src/util/unicode.cpp \
  loom/src/util/result.cpp -o /tmp/catalog-relevance-recipe
/tmp/catalog-relevance-recipe --no-intro=true
```

These tests exercise the decoder, not the separate KB pack validator. The
existing pack bias/k1 bounds are still a thread-4 handoff.

To replay the output restoration/count audit from the saved inputs, choose a
fresh output directory and run from this receipt directory:

```sh
python3 restore-output.py --manifest ctest-manifest.json --junit ctest-108-final.xml \
  --last-test ctest-108-final-raw.log --output /tmp/catalog-restored.xml \
  --receipt /tmp/catalog-restoration.json
python3 count-guard-source.py --preset dev --manifest ctest-manifest.json \
  --junit /tmp/catalog-restored.xml --output /tmp/catalog-cases.json
```
