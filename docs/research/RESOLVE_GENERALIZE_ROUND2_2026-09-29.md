# Resolve/generalize performance, round 2 (2026-09-29)

Scope: finish the interrupted STREAM 1 (performance, outputs must stay
byte-identical). STREAM 2 (precision) has no code in the WIP; nothing was
attempted there and nothing was changed.

## What the WIP contains

Performance work in `loom/src/generalize/{common,infer,internal,match,principles}.cpp`
(per-call caches: cue classes prepared once, every observation folded and
tokenized at most once, facets parsed once, type-principle normalizers/indexes
built once instead of per call) plus opt-in sub-phase profiling
(`loom/src/kb/stage_profile.h`, `LOOM_STAGE_PROFILE=1`) and a
`knowledge_eval.py bench` harness. It compiled with no fixes after merging
`claude/chataddhd-cpp-loom-core-IRGRN`.

## Tests

`ctest --preset dev`: 72/73, only `unit.test_catalog_eval` red. Same as the
main baseline. Nothing regressed, no threshold changed.

## Speed (both binaries built `-O0 -g0`, same machine, same frozen input)

Repository-scale run (tracked HEAD of this repo, frozen with `bench stage`):

| | before (upstream tip) | after (WIP) |
|---|---:|---:|
| wall | 1064.1 s | 176.6 s / 177.7 s (two runs) |
| speedup | | about 6.0x |

Profile of the WIP run (ms): extract 90444, generalize 28424 (of which
principles 9785, of which `typing` 8420; load_evidence 10066), resolve 15059,
assess 5372. The `generalize` stage no longer dominates; `extract_units`
(79 s) now does. There is no "before" per-phase profile, because the old
binary has no timers; only wall time is comparable.

Synthetic_dev, full import: 11.2 s -> 4.6 s (2.45x).

## Output identity

Proven on synthetic_dev (`bench run --synthetic --import-mode full`, same work
path for both binaries): run IDs identical, all six stage hashes identical, every
product file identical. The `synthetic` scorecard: 0 of 166 numeric metrics
differ, so no precision or recall effect. `db` digest differs only in
`loom_cat_*` and `loom_kb_runs/products` bookkeeping tables, which hold
timestamps or paths (the stage hashes and products of the same run are equal).

NOT proven at repository scale. There the stage hashes differ from `extract`
onward, and dossiers differ. Two runs of the same WIP binary on the same frozen
input (different work dirs) also differ from each other (stage hashes not
equal), so this is run-to-run nondeterminism in the repository run that exists
independently of the WIP: `extract` is unchanged apart from timers, and it is
the first stage to diverge. So identity of `generalize` in isolation at
repository scale is unshown. Determinism of `extract` at this scale should be
found first (candidates: work-dir path in run identity, time/size budgets in
the 79 s extract), or the identity check should run `generalize` from one
fixed extract/resolve output.

## What remains

- Find the repository-scale nondeterminism, then repeat the identity check for
  generalize on a fixed upstream store. Until then the 6x figure is a measured
  speedup with unproven repo-scale identity.
- `extract_units` is now the largest cost (79 s of 177 s), then
  `generalize.load_evidence` and `resolve.load` (about 10 s and 9 s each).
- STREAM 2 precision is untouched: code fragments such as `const Json& d`
  becoming projects, a software project matched to the music paradigm, and
  wrong computed versions. A `bench`/`spot.py` harness exists to measure them.
- Release-optimized (`-O2`) timings were not taken.

## Determinism fix

Cause: `Run::do_dates` in `loom/src/extract/extractors.cpp` looped over
`iso->finditer(utf8::decode(text))`. `re::Match` holds a `u32string_view` into
its subject, and the decoded `u32string` was a temporary destroyed after the
range-init expression, so `m.group_utf8(0)` read freed heap. The bytes read
depended on allocator state, so `has_event` claim values (for example
`{"date":"喽??6-09-26"}` instead of `2026-09-26`) and therefore claim ids,
the extract hash and every later stage varied from run to run. It appeared
only at repository scale because small inputs reuse no freed memory before the
read. Every other `finditer` call site in `src/` binds the subject to a named
variable.

Fix: the decoded subject is a named local that outlives the loop.

Regression test: `tests/test_extract.cpp`, "determinism (I5)": twelve `.eml`
units with ISO dates through `extract_units` twice and in reversed input order.
All persisted sections must match (only `stats.per_unit` mirrors input order),
and every `has_event` date must be well formed.

Proof: two repository-scale selfhost runs of the same binary with the same
work path (`mkdtemp` pinned): run id `kr_1d5b0d37977e88bd` both times, all six
stage output hashes equal, every product and dossier file byte-identical.
Before the fix, five dossiers and SELF.md differed between two runs. Note the
harness uses a random `selfhost-*` work subdirectory, and the path is part of
run identity, so runs compared for identity must pin it. `ctest --preset dev`:
72 of 73 pass, only `unit.test_catalog_eval` red (unchanged).
The identity check for `generalize` on the fixed extract output can now be
repeated on repository scale.
