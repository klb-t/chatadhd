# Generalization data migration replay

`dic0301_replay.py` runs paired native modules for DIC-0301. Its default baseline
is `e4109df7e4af22b461def5f7d62e268d9b9a8825`, before the second thread-1 increment.
The seven legacy dictionaries are preserved verbatim as a test oracle. Product
code reads the same 120 ordered phrase/weight pairs from `lexicons/cues.json`.

After the native build has completed and its sources/archives are frozen:

```sh
python3 loom/src/generalize/tests/dic0301_replay.py \
  --core-build loom/build/dev \
  --output /tmp/loom-dic0301-fresh-run
```

The output directory must not exist. Both phases compile exact baseline/current
`common.cpp` and `principles.cpp` modules before the same static core, SQLite and
miniz archives. They load explicit baseline/current cue documents and the same
explicit current numeric policy through the native Pack validator. The exact
base producer ignores newly introduced recipe fields, while the after producer
uses their identical default values. This allows a frozen base core to support
the paired check without implying that it embeds the new pack. Every pack
document, DEV corpus file, generalization
source/header, binary, command and output receives a SHA256 receipt. Sources and
archives must remain unchanged until replay finishes.

The checks compare every default class and all 134 native score/typing samples,
then the complete principle-discovery output on the public fictional
`synthetic_dev` corpus. They also exercise deletion and replacement of each
class, including real whole-file overlays on disk and repeated loader calls.
Malformed overlay JSON must produce an explicit parse error. Empty phrase lists
are still rejected by the existing Pack validator; removing the class disables
its matching. No hidden dictionary restores a removed class.

Whole-file overlay deletion does not implement the shared R40 graph exclusion
marker. That integration belongs to the graph/profile owners. This focused
proof establishes default preservation and configurable dictionary lookup; it
does not measure a new precision gain or replace full CTest and evaluator runs.
No paid model call, private source or sealed/holdout input is used.

Archive the complete run before changing any of its source inputs:

```sh
python3 loom/src/generalize/tests/dic0301_archive_replay.py \
  --run /tmp/loom-dic0301-fresh-run \
  --output loom/src/generalize/tests/evidence/2026-10-05/dic0301-replay.tar.gz
```

The archive preserves full before/current source snapshots, pack and public DEV
inputs, fixture/runner, raw reports and the full command/error log. Its manifest
contains hashes of every member and the omitted native archives/objects/ELFs.
Rebuild those binaries from the pinned Git source and archived source overlay.
Members are sorted; tar timestamps/owners and gzip timestamp are fixed, so the
same frozen run produces identical archive bytes. Existing archive destinations
are rejected. Failed and interrupted runs can also be archived; absence of a
final summary is explicitly recorded and never treated as success. The replay
writes frozen input receipts and source snapshots before its first compile.
