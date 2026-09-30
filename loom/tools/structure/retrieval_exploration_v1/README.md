# Retrieval exploration v1

See `docs/research/retrieval_exploration_v1/PROTOCOL.md`, `BYTE_BUDGET_PROTOCOL.md` and `FIRST_RESULTS.md`. DEV only; no validation or paid API.

Run40 mechanism tests with:

```sh
TMPDIR=/var/tmp python3 -m unittest loom.tools.structure.retrieval_exploration_v1.test_explore loom.tools.structure.retrieval_exploration_v1.test_byte_budget loom.tools.structure.retrieval_exploration_v1.test_template_renderer_v2
python3 -m loom.tools.structure.retrieval_exploration_v1.first_archive verify
```

Raw first data are retained byte-for-byte in `first_evidence.zip`; restore ignored working JSON/vectors with `first_archive restore`. Do not re-run first exclusive-output commands in place. The original first freeze and causes-registration failure are preserved. All768 v2 data-backed query representations match executed v1 bytes. Model weights remain outside this artifact.
