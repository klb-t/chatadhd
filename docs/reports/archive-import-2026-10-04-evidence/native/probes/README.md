# Exact integrator checkpoint replay probes

The two original public probes came from
`archive/2026-10-04/integrator-import-checkpoint-replay`, reviewed at
`c7972252a23cb13c54797f6b57df24de903c5296`; its executed import bytes were
`d77867517bb128b7e43236c436a2dc3a31e1ae46`. The original negative archive remains
unchanged. `original/` retains both original probe sources. The only adapted
line is `options.resume = true`: the library compatibility default is now false,
while the original probes had inherited true. [adaptation.json](adaptation.json)
binds original/adapted byte counts and SHA-256 hashes; the runner verifies that
removing that one line recovers each exact original.

The runner links directly against the **actual completed static core** plus its
SQLite/miniz libraries. It adds no importer overlay or replacement implementation,
does not rebuild production code and does not generate large link maps. All
sources are synthetic, all executions offline; provider calls are zero.

From a fresh public checkout after its native build, use a fresh external output:

```bash
python3 docs/reports/archive-import-2026-10-04-evidence/native/probes/verify_checkpoint_replay.py \
  --build-dir /tmp/loom-thread5-native-build \
  --out-dir /tmp/loom-thread5-checkpoint-probes \
  --repo "$PWD" --source-commit "$(git rev-parse HEAD)"
```

`--source-commit` checks actual CMake source-tree `loom/src` and `loom/include`
bytes against that revision. It is not inferred from the build directory's Git
HEAD. The runner records actual source manifests, compiler/link commands,
libraries, binaries, input ZIPs, result JSON and logs. Libraries and source files
must remain unchanged throughout the probe; do not run it during a rebuild.
The recorded source tree is an observed input snapshot: the completed build's
own evidence remains necessary to bind compiler objects to those sources.

The valid-project scenario must remain partial after the injected link failure,
then restore exactly one project, one document and their `part_of` edge after
the trigger is removed. Its third import must use the completed-source cache.
The malformed known member must remain partial with errors on all three imports,
without a completed-source cache hit. Both scenarios preserve source and
conversation identities and retain database integrity.

Results are two independent probe scenarios, not a full CTest gate. A phase-1
execution does not certify later parser or legacy-memory changes; rerun against
the final stable build into another fresh directory and retain both receipts.
No success is claimed merely by preparing this reproduction recipe.

## Retained phase-1 result

The [phase-1 receipt](phase1/receipt.json) executed both scenarios successfully
against actual source/header bytes matching `abade8092af99b1d01f3640308314fd6cf15ea51`.
The actual static libraries and observed sources remained unchanged throughout.
The malformed-member scenario passed all 17 checks; valid-link recovery passed
all 14 checks and ended with one project, one document and their link. The original
source ID and conversation ID remained stable in each scenario.

[Logs, JSON results, synthetic inputs and byte manifest](phase1/) preserve this
execution separately. No database, binary or original negative archive is copied
here. This phase-1 result does **not** cover subsequent phase-2 legacy OpenAI or
memory corrections; its receipt remains separate from the final rerun below.

## Retained final result

The [final receipt](final/receipt.json) passed both scenarios against actual
source/header bytes matching `1474b260582a80f006f99953baaad1b3253ca6e1`, rebased
on documentation-only main `7282437b1c88933977f64b3468b9f42f7b400494`.
Source/library snapshots remained stable, and the expected-commit comparison
reported no mismatch. The actual core SHA-256 was
`f2002cbe0a527039bf9b1c03753c4707f5a85d0667da70b2cee805233f83a9c0`.

The malformed member passed **17/17 checks**, retaining partial/errors through
all three imports with no completed-source cache hit. The valid project case
passed **14/14 checks**, restored one project, one document and one `part_of`
link after removal of the trigger, then used the completed-source cache.
Both scenarios preserved source and conversation IDs and passed database
integrity checks. [Final hashes and retained inputs/results/logs](final/SHA256.json)
bind this execution. The two scenarios do not replace the separate full CTest
gate or its actual case/assertion counts.
