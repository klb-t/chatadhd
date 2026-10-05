# Native policy parameter replay (DIC0322–DIC0324)

`principle_parameters_replay.py` compiles two native probes against the same
unchanged `loom_core`, miniz and SQLite libraries. The before probe uses
`principles.cpp`, `common.cpp`, `internal.h` and all Pack JSON documents from
`e4109df7e4af22b461def5f7d62e268d9b9a8825`. The after probe uses frozen working tree
sources and explicit current Pack JSON documents. Neither probe reads the embedded
Pack. The normal build and CTest remain separate gates for the embedded Pack.

After building the current source, run from the repository root:

```sh
python3 loom/src/generalize/tests/principle_parameters_replay.py \
  --build-dir loom/build/dev \
  --evidence /tmp/loom-principle-parameters-fresh-run
```

The evidence directory must be new. A failed run remains available, with full
command output, before/after reports if produced, and a deterministic source
archive. Do not replace it with the successful rerun.

The probe calls the actual `generalize::discover_principles` API on the public
fictional `synthetic_dev` corpus, both with and without prior principles, and on
three small fictional controls with three source units and three distinct dates.
Default report bytes must match for all five cases. The controls independently
exercise seed matching, repeated seed confidence, unsaturated discovered
confidence and saturated discovered confidence. They compare exact formula results
and show that the old implementation ignored each of the five new settings.

For every new setting, missing values, strings, nulls, NaN and infinity must cause
an explicit error identifying the setting. Some errors are caught by the Pack
validator and others by the analysis reader; the receipt names the actual phase.
This is validation of the declared numeric data type, rather than a new numeric
range restriction. The runner does not modify quality thresholds or CTest tests.

`receipt.json` records source hashes, library hashes, compiler commands and all
individual checks. `before_results.json` and `after_results.json` retain the full
native API reports, including their original serialized bytes. `sources.tar.gz`
contains exact before/after source and data bytes, the probe and runner, and the
public fixture inputs. The archive is a focused source snapshot; the complete
header closure and compiled libraries are not included. Compiled binaries are
scratch artifacts. Replay requires the pinned Git checkout and a normal build
whose library hashes match the receipt; the archived modules and data allow the
measured variants to be restored exactly.

This proves preservation of the measured DEV behavior and operation of the five
settings. It is not a new accuracy estimate on unseen conversation archives.

## Recorded run: 2026-10-05

The first paired run passed **46/46** controls. Both probes linked the immutable
baseline archives copied from the pristine `e4109df` build. The explicit current
modules and explicit current Pack documents supplied the after implementation;
this receipt does not claim that the linked archive contained the regenerated
after embedded Pack. The normal final build and full CTest cover that separately.

The five complete native report serializations were byte-identical: DEV with
priors **30 → 30**, DEV without priors **30 → 30**, and all three micro-controls
**1 → 1**. Each of the five settings had the intended actual effect. All 25
malformed-value controls returned `invalid_argument` naming the setting: 10 in
Pack validation and 15 in `discover_principles`.

For the three-unit seeded control, confidence stayed **0.9314** at the default
residual factor and changed to **0.8542000000000001** at `0.9`. The discovered
confidence controls matched the separate changed formulas: cap **0.784**, base
**0.936**, score factor **0.957125**. A zero seed Jaccard multiplier removed the
seed match. These are mechanism controls on fictional DEV input.

The committed evidence is in
[`evidence/2026-10-05/principle_parameters/summary.json`](evidence/2026-10-05/principle_parameters/summary.json).
The complete reports and command log are deterministic gzip files next to the
receipt and exact source tarball. The shared `libloom_core.a` hash is
`c0ebde6e82d3bbfd1e529550c71a56dc6000ac4c3a69d9a592b94cc2b558dd02`.
