# Source-recorded actor projection diagnostic

This separate package projects exact metadata from raw sources referenced by
frozen native observations. It does not update native graphs, resolve world
identity, call models, read graph gold or open validation. All native inputs and
first outputs remain in the unchanged `native_source_free_v1` checkpoint.

Restore canonical native and actor measurement bytes after checkout:

```sh
python3 -m loom.tools.structure.retrieval_exploration_v1.native_source_free_v1.first_archive restore
python3 -m loom.tools.structure.retrieval_exploration_v1.actor_projection_v1.first_archive restore
python3 -m loom.tools.structure.retrieval_exploration_v1.actor_projection_v1.verify_first
TMPDIR=/var/tmp python3 -m unittest loom.tools.structure.retrieval_exploration_v1.actor_projection_v1.test_actor_projection loom.tools.structure.retrieval_exploration_v1.actor_projection_v1.test_actor_projection_v2 -v
```

The archive retains18 exact-byte payloads and one inventory member. Restore
refuses to overwrite different receipts. Recount verifies the archive, preserved
first control failure, corrected output and both primary84-object projections,
without writing new results or invoking native code. Execution tools intentionally
refuse to overwrite first output paths.

V1 preserved first outcomes:84/84 source-bound projections,11/12 independent
transport controls,103/105 expected field/state checks. Its content-part scope
was broader than the declared frozen wrapper. V2 uses recipe data
`allowed_content_parts=[0]`, preserves all84 objects and11 control objects exactly,
and abstains on the unsupported second part:12/12 controls,105/105 checks.
An explicit alternative part preset is supported without changing code.

Actor labels remain `source_recorded`, native speaker and transport role remain
separate, missing/conflicting/invalid fields abstain individually. Raw hashes and
message-local pointers preserve source namespaces. Equal labels or turn IDs across
sources never establish `same_as`. Controls were independently authored in
`docs/research/native_actor_control_audit_v1/` and are mechanism evidence, not
model-quality or real-person attribution measurements.
