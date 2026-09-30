# Frozen current-native source-only baseline

See `docs/research/retrieval_exploration_v1/NATIVE_SOURCE_FREE_PROTOCOL.md`,
`NATIVE_SOURCE_FREE_PROTOCOL2.md`, and `NATIVE_SOURCE_FREE_FIRST_RESULTS.md`.

This directory preserves the first native source-only extraction outputs, not a
native implementation change. No paid API, validation, query endpoints or gold
enter this extraction arm. The same DEV sources were previously exposed elsewhere.

From repository root, restore exact first measurement bytes after checkout:

```sh
python3 -m loom.tools.structure.retrieval_exploration_v1.native_source_free_v1.first_archive restore
python3 -m loom.tools.structure.retrieval_exploration_v1.native_source_free_v1.verify_first
TMPDIR=/var/tmp python3 -m unittest loom.tools.structure.retrieval_exploration_v1.native_source_free_v1.test_native_panel_v3 -v
```

Restore refuses to overwrite different measurements. Verification recounts
inventory and source bindings without rewriting receipts or running native code.
The ZIP contains342 payloads plus one inventory metadata member; its sidecar
`FIRST_ARCHIVE.json` defines every byte/hash. Large restored working files are
ignored because the archive retains their exact bytes.

Authoritative actual source-output freeze: `freeze_before_outputs2.json`.
All48 native outputs were frozen before evaluation in
`freeze_before_evaluation2.json`. The corrected read-only inventory scorer and
its disclosed schema-field correction have a separate
`freeze_before_corrected_evaluation3.json` receipt. Earlier instrumentation,
preparation, scorer, and strict claim-quote audit failures are preserved. No native
output was discarded or rerun to substitute a better first result.

Fresh native reproduction requires the pinned native binary and compiled source
environment. `native_panel_v2 run` intentionally refuses existing output paths;
it is an experiment execution tool, not a command to overwrite this checkpoint.
Reproduce in a separate experiment directory and freeze its changed environment
first. Do not interpret native availability or source-grounding counts as typed
relation precision/recall.

Raw SQLite databases, generated local secret stores and caches stay outside Git;
only explicit read-only knowledge/body/source/message snapshots are archived.
