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
public fixture inputs. Compiled binaries are scratch artifacts; replay requires a
normal build and the recorded source archive or Git revision.

This proves preservation of the measured DEV behavior and operation of the five
settings. It is not a new accuracy estimate on unseen conversation archives.
