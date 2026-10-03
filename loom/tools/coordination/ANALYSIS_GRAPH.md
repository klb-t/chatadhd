# Local AnalysisPlan graph execution

`analysis-graph` is an operator consumer of the existing AnalysisPlan executor,
resource ledger, GraphPacket diff algebra and SQLite lease store. It executes
new local graph transformations; it does not replay a model transcript. The
result is a packet projection artifact. Native graph/store writes and network
calls are zero. The existing `graph-replay` command remains unchanged.

## Run the synthetic example

From the repository root, with the contract dependencies (`jsonschema` and its
transitive dependencies) installed:

```sh
python3 -m loom.tools.coordination --database /tmp/my-graph-run/coordination.sqlite3 analysis-graph \
  --task graph-review-v1 --source-commit "$(git rev-parse HEAD)" --owner operator \
  --plan loom/tools/coordination/examples/graph-review-plan.json \
  --packet loom/tools/coordination/examples/graph-review-packet.json \
  --ledger-directory /tmp/my-graph-run/resources \
  --output-root /tmp/my-graph-run/artifacts --variant-index 0

python3 -m loom.tools.coordination --database /tmp/my-graph-run/coordination.sqlite3 status \
  --task graph-review-v1 --receipts
```

Use a persistent local directory instead of `/tmp` for retained work. Preserve
both the SQLite backup (existing `backup` command), resource ledger and artifact
tree. Git copies of a database are not a distributed coordinator.

The example changes a synthetic claim's status to contested in **preview**.
`execution.outcome.analysis_result.results.one.result.output` contains:

- `selected_packet`: original in preview, transformed packet when accepted;
- `preview`: candidate packet and exact changes;
- `application`: existing reversible application receipt, policy and provenance.

`input_first.json` and `result_first.json` are exclusive-created and fsynced under
`<output-root>/<task-id-hash>/fence-N`. Existing AnalysisPlan attempts retain
`reservation.json`, `loaded_packet_first.json`, `returned_first.json`, result and
completion metadata in the shared resource ledger. Ancestor entries for the
artifact tree are fsynced too; hardware/filesystem durability is still required.

## Plan contract

Each local transform method has:

```json
{
  "method": "graph:apply_diff",
  "runtime": {"id": "local:graph_packet", "config": {}},
  "required_capabilities": ["local", "graph:apply_diff"],
  "config": {
    "diff": "<GraphPacketDiff object>",
    "policy": "<graph_packet_apply_policy object>",
    "explicitly_accepted": false
  }
}
```

The full schema remains `docs/contracts/analysis_plan.schema.json`; use the
example for all required dimensions. `acceptance_policy` must exactly equal
`config.policy`; disagreement is rejected rather than silently preferring one.
Policies support existing `preview`, `auto`, explicit acceptance and configurable
source tombstones. Acceptance never establishes truth or upgrades epistemic
origin. Patches preserve original source bytes/history and use existing optimistic
packet/record hashes. Existing `invert_application` restores an unchanged head.

This loader supports one pinned packet: all plan source bindings must have
`mode: pinned_snapshot` and `snapshot_id: <packet.packet_id>`. The packet hash is
validated; this does not authenticate the underlying archive. To chain methods,
put a previous method ID in `config.input_dependency` and `depends_on`. That
method's **selected** packet becomes the input; an unaccepted preview does not
silently feed its candidate into the next transformation.

Variant selection is lazy and exactly one index is executed. Huge symbolic
spaces do not allocate workers. Variant axes are retained in execution context;
this adapter does not interpret axis values as code or template substitutions.
A patch is concrete data, including its base hash. Unsupported method/runtime
pairs and capabilities remain explicit `unavailable` results through the existing
executor. This local runtime offers no tool dispatch or runtime options; requests
for these fail explicitly. Other runtimes are not globally prohibited.

## Accounting, recovery and identity

Resource reservations and policy gates run before packet loading and callbacks.
Unknown memory, storage, agents and other uninstrumented quantities keep their
existing reservation treatment. Zero reported money/calls/tokens means this
specific local callback has no provider path; reservation admission is still
honoured, even for money (a caller can declare zero reservation for local work).
CPU and wall measurements cover only the graph callback, excluding preparation,
coordination and persistence. The response includes separately labelled full
adapter wall time. The benchmark includes interpreter/import, file reads, database
initialization, validation, ledger writes, artifact writes and output serialization.

The effective plan adds a reserved `_coordination_execution` extension binding
adapter version, caller-declared full source commit and exact packet digest.
It is saved beside the original plan; resource budget identity is unchanged.
This prevents a new code revision from silently using an old method-attempt ID.
The source commit is a declaration, not proof that the running tree is clean.
The CLI additionally binds exact input file bytes. A changed configuration or
file under the same task ID is an identity conflict; use a new task ID for a
new specification. Completed method attempts shared across new task IDs reuse
validated first results and report `replayed_bound_snapshot`.

The lease is checked/renewed immediately before every graph callback. Expiry
before dispatch is recoverable; expiry after dispatch is unknown, not proof of
absence of effects. Late results remain in artifacts and available receipt
evidence but do not become a successful fenced completion. Restart does not
silently execute a completed, busy or unknown task. Explicit outer reconciliation
never clears the inner resource ledger's uncertain attempts. No exactly-once
external-effects guarantee is implied.

`execution.outcome.all_methods_completed` and `uncertain_methods` qualify the
outer coordination state. Outer `succeeded` means the executor returned its audit
result, not that every method succeeded or that the graph is semantically correct.
The inherited top-level `acceptance_policy_executed: false` refers to the generic
plan executor; each local callback's actual acceptance is in `output.application`.
The CLI exit code 0 means a structured execution/status result is available, so
operators must inspect these fields; malformed commands/specifications return 2.

## Verify and measure

```sh
python3 -m unittest discover -s loom/tools/coordination -p 'test_*.py' -v
python3 -m unittest discover -s loom/tools/contracts -p 'test_*.py'
python3 -m loom.tools.coordination.benchmark_analysis_graph \
  --source-commit "$(git rev-parse HEAD)" --samples 5 --output /tmp/w4-timings.json
```

The benchmark uses the same public synthetic example and refuses to overwrite
its report. Raw graph algebra versus the complete CLI is total path overhead,
not isolated SQLite overhead. Deduplicated invocation is not a general cache.
There is no model-quality or real-archive performance claim.
