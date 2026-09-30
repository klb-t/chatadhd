# Retrieval exploration v1

For new source metadata projections use the active, offline
[`source_projection.py`](source_projection.py) adapter. It accepts a raw source
and native observations without restoring or creating an experiment directory:

```sh
python3 -m loom.tools.structure.retrieval_exploration_v1.source_projection \
  --raw-source source.json --observations native_snapshot.json --output projected.json
python3 -m unittest loom.tools.structure.retrieval_exploration_v1.test_source_projection
```

The observation input may also be a JSON array. The default recipe is the
explicit synthetic OpenAI wrapper contract, not a claim to support arbitrary
provider exports. Metadata paths and allowed content parts can be supplied with
`--policy recipe.json`. The adapter preserves source-local identity, rejects
ambiguous JSON member bindings and invalid pointer escapes, and never modifies
the native graph. See
[`SOURCE_PROJECTION_CONTRACT_2026-09-30.md`](../../../../docs/research/retrieval_exploration_v1/SOURCE_PROJECTION_CONTRACT_2026-09-30.md).

The historical actor v1/v2 scripts below remain byte-for-byte frozen for replay;
their boundary handling is not the active adapter.

See `docs/research/retrieval_exploration_v1/PROTOCOL.md`, `BYTE_BUDGET_PROTOCOL.md` and `FIRST_RESULTS.md`. DEV only; no validation or paid API.

Run40 mechanism tests with:

```sh
TMPDIR=/var/tmp python3 -m unittest loom.tools.structure.retrieval_exploration_v1.test_explore loom.tools.structure.retrieval_exploration_v1.test_byte_budget loom.tools.structure.retrieval_exploration_v1.test_template_renderer_v2
python3 -m loom.tools.structure.retrieval_exploration_v1.first_archive verify
```

Raw first data are retained byte-for-byte in `first_evidence.zip`; restore ignored working JSON/vectors with `first_archive restore`. Do not re-run first exclusive-output commands in place. The original first freeze and causes-registration failure are preserved. All768 v2 data-backed query representations match executed v1 bytes. Model weights remain outside this artifact.
