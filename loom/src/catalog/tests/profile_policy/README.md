# Profile policy replay (W6, increment 2)

These checks use authored fixtures and the public `synthetic_dev` fixture only.
They do not load another evaluation corpus, call a provider, or tune selection.
The production change consumes the existing effective Pack weights; it does
not install another preset or settings store.

## Owned native checks

From `loom/`, against a completed Linux core build:

```sh
python3 src/catalog/tests/profile_policy/run_native.py \
  --build build/dev --output /tmp/catalog-profile-native
```

The runner records compiler commands and hashes, uses the build's private
header, SQLite mode and actual TLS capability, and executes 12 cases with
579 assertions. It does not reconfigure or modify the supplied build. An old
baseline must use `--probe-only`, because it predates the checked API. The
probe must be compiled separately against each version's private header:
`AliasTerm` has a different private layout after this change.

These tests are not registered in central CTest yet: that file is outside
W6's scope. Run them in addition to the complete CTest preset.

## Exact before/after replay

The baseline is `66da570d3b5379492e128d940ad474467082c59f`.
The final base `0a81480bd1a70b394bac72e98f6b5e7cb49f5d0b` adds documentation
only, with identical native sources. Build the baseline in a separate checkout;
use this directory's runner with that checkout's completed build. Keep its
library and probe separate from the changed build. Do not use an old probe
with new private headers.

Create an evidence directory and choose one absolute authored fixture path
used for both probes. Each invocation needs a fresh runtime path; the probe
refuses existing runtimes and never deletes them. For example:

```sh
/tmp/catalog-before/profile_probe /tmp/catalog-authored-fixtures \
  /tmp/catalog-runtime-before-default default > /tmp/catalog-evidence/probe-before-default.json
/tmp/catalog-before/profile_probe /tmp/catalog-authored-fixtures \
  /tmp/catalog-runtime-before-weights weights > /tmp/catalog-evidence/probe-before-weights.json
/tmp/catalog-after/profile_probe /tmp/catalog-authored-fixtures \
  /tmp/catalog-runtime-after-default default > /tmp/catalog-evidence/probe-after-default.json
```

Run the changed probe also with `weights`, `missing-alias`, `missing-principle`,
`missing-path`, `empty-map`, `missing-map`, `invalid-map`, and `invalid-alias`,
writing `probe-after-<mode>.json` in the same evidence directory. Use a fresh
runtime for every mode. Run the DEV measurement once per built library:

```sh
python3 src/catalog/tests/profile_policy/evaluate_dev.py \
  --library /tmp/catalog-before/libloom.so.0.1.0 --output /tmp/catalog-evidence/dev-before.json
python3 src/catalog/tests/profile_policy/evaluate_dev.py \
  --library build/dev/libloom.so.0.1.0 --output /tmp/catalog-evidence/dev-after.json
python3 src/catalog/tests/profile_policy/verify_replay.py --evidence /tmp/catalog-evidence
```

The verifier compares complete default profiles and all 68 native DEV records,
including score reports and preparation. Only fresh execution task identities
(`/task_id` and `/stages/*/task_id`) are excluded from preparation equality.
The overlay probe checks every emitted weight and preserves the other fields.
Missing or invalid policy checks include the native catalogue table state.
The fixture path is part of the profile provenance; keep it identical before
and after. A replay elsewhere produces new receipts, rather than pretending
to reproduce historical runtime paths or task IDs.

## Recorded full gates

[results/2026-10-05/evidence.zip](results/2026-10-05/evidence.zip) contains the
complete logs, JSON/JUnit output, input and binary hashes, build snapshots,
commands and a manifest of its members. Extract it into a separate directory:

```sh
python3 -m zipfile -e src/catalog/tests/profile_policy/results/2026-10-05/evidence.zip /tmp/catalog-evidence-recorded
python3 /tmp/catalog-evidence-recorded/verify_ctest.py --preset dev \
  --manifest /tmp/catalog-evidence-recorded/ctest-manifest.json \
  --junit /tmp/catalog-evidence-recorded/ctest.xml \
  --output /tmp/catalog-evidence-recorded/ctest-guard-replay.json
python3 src/catalog/tests/profile_policy/verify_replay.py --evidence /tmp/catalog-evidence-recorded
```

The CTest guard is copied unchanged from the integrator's accepted evidence,
with source and hash recorded in the archive. It checks actual inner cases,
not just outer CTest entries. The existing opt-in `unit.test_catalog_scale`
entry runs zero cases and is explicitly recorded as unexecuted; it provides
no scale coverage. No timeout, quality threshold or test list is relaxed.
This session uses Debug without `NDEBUG`, strict warnings, vendored SQLite,
shared library and server, plus the unchanged web TypeScript/Vite build.
The archive records compile receipts rather than checking generated binaries
into this public repository.
