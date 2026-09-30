# Shared task ownership and recovery — 2026-09-30

Base inspected: `fafc77f8eeebdff4c32897b5e8ef94dd26d4ec38`.

This is a new implementation of the missing coordination capability described
in `../RECOVERY_COORDINATION_2026-09-30.md`. It does **not** reconstruct the lost
`c26c525` source, reuse its historical 30-test claim, or claim its 851-test run
was repeated. It adds only `loom/tools/coordination/` and this report directory.

## Why this addition

`analysis_plan_ref.SharedLedger.reserve` already serializes reservations and
prevents repeating the same attempt. An incomplete reservation remains uncertain;
this is correct for its budget/effect boundary. `agentic_graph_v1.run_workflow`
preserves first responses and `frontier_panel_v1.execute_scripted` preserves
starts/results, but independent output directories have no shared task ownership.
The addition supplies explicit ownership, pre-dispatch recovery, fencing,
uncertainty reconciliation and durable receipts without weakening those guards.

## Behavior

| Event | New state | Can a new worker claim automatically? |
|---|---|---|
| Register immutable task specification and source commit | pending | yes |
| Claim within a SQLite write transaction | leased | no |
| Renew current unexpired claim | leased or dispatched | no |
| Expire before durable dispatch marker | pending | yes; next fence increments |
| Commit dispatch marker before callback | dispatched | no |
| Expire after dispatch marker, or ambiguous callback error | outcome_unknown | no |
| Complete with current unexpired fence and evidence | succeeded or failed | no |
| Explicitly reconcile unknown outcome | terminal or pending | only if caller explicitly chooses retry |

A claim binds task ID, full Git source commit, complete specification hash,
owner, random claim token and monotonically increasing fence. A specification
change under the same task ID is refused. `begin`, `renew` and `complete` reject
old/foreign/expired claims. `begin` is one-shot even for the same owner.

Every transition appends a source-bound, owner-bound, hash-chained receipt inside
the same transaction as the state change. Result/evidence JSON is kept verbatim
in meaning; JSON object keys are canonically ordered. The source commit and
evidence content are caller declarations, not independently authenticated facts.
Receipt triggers prevent accidental UPDATE/DELETE through SQLite; they are not
protection from an adversary with database-file access. Preserve an external
snapshot hash if detecting replacement of the complete history matters.

An expired dispatched attempt can already have changed an external system. Its
state and effect are unknown even if no result file exists. No timeout is treated
as proof that nothing happened. A reconciler supplies its identity, expected
fence, evidence, outcome and decision. A retry additionally records the caller's
open-text basis and the possibility of repeated external effects. This is an
explicit caller policy decision, not an automatic human-review requirement.
Old workers cannot complete in this store after reconciliation/reclaim. An old
worker's *external* effects require downstream fencing or idempotency support;
this module does not provide exactly-once external execution.

## Real integration seam

`loom.tools.coordination.graph_workflow.run_coordinated_workflow` invokes the
existing `run_workflow`, including its method/resource/authorization checks,
first response preservation, graph provenance, preview/auto acceptance and
no-retry policy. The complete input packet, method, acceptance selection and
absolute output destination are task data. Each explicit attempt gets a separate
`<output-root>/<task-id-hash>/fence-N` directory. Completion of this wrapper means
the workflow returned its audit result; inspect `completed_stages` to assess the
workflow. It is not evidence of semantic correctness.

```python
from loom.tools.coordination import LeaseStore
from loom.tools.coordination.graph_workflow import run_coordinated_workflow

store = LeaseStore("/persistent-run/coordination.sqlite3")
receipt = run_coordinated_workflow(
    store, task_id="graph-review:packet-and-method-revision-1",
    source_commit="<full Git commit hash>", owner="worker-identity",
    packet=packet, method=method, transport=injected_transport,
    output_root="/persistent-run/graph-artifacts", lease_seconds=300,
)
```

This is a new opt-in call site, not a claim that all existing entry points now
use leases. Root can replace a desired direct `run_workflow(...)` invocation with
this wrapper without editing `run_workflow` or changing graph semantics. A caller
already using `SharedLedger` should retain its original reservation and budget
checks; a lease is not resource admission, authorization or budget reconciliation.
Other callback-only runtimes can use `execute_once`. Long callbacks may explicitly
renew via the supplied store/lease. Lease duration is caller data with no imposed
maximum and no background renewal thread hidden in the runtime.

## Local command line

From the repository root, `python3 -m loom.tools.coordination --help` exposes
`register`, `status`, `backup` and `graph-replay`. The database location is always
explicit. These commands load no credentials and expose no provider/network
transport. Here is a runnable replay of the repository's saved three-stage
scripted workflow, using a user-owned directory outside the checkout:

```sh
python3 -m loom.tools.coordination --database "$HOME/loom-runs/coordinator.sqlite3" graph-replay \
  --task saved-graph-three-stage-v1 --source-commit "$(git rev-parse HEAD)" \
  --owner local-operator \
  --packet docs/research/agentic_graph_v1/scripted_three_stage_first/input_packet.json \
  --method docs/research/agentic_graph_v1/scripted_three_stage_first/method.json \
  --recorded-run docs/research/agentic_graph_v1/scripted_three_stage_first \
  --output-root "$HOME/loom-runs/replayed-graphs" --lease-seconds 300

python3 -m loom.tools.coordination --database "$HOME/loom-runs/coordinator.sqlite3" status \
  --task saved-graph-three-stage-v1 --receipts

python3 -m loom.tools.coordination --database "$HOME/loom-runs/coordinator.sqlite3" backup \
  --output "$HOME/loom-runs/coordinator-checkpoint-001.sqlite3"
```

The source commit argument binds the operator-declared runtime/source revision;
it does not prove the historical transcript was generated at that commit. Keep
the same commit/task identity for repeated invocations. A new method revision or
changed transcript requires a new task ID. A repeated completed/uncertain task
returns its current state without another coordinated dispatch. `status` includes
the registered specification, and `--receipts` adds exact outcomes and evidence.
Observing expired leases can append the corresponding expiry receipt. Unknown
outcomes remain unknown; explicit reconciliation is available through the
existing Python API, not silently inferred from a CLI restart.

To register an arbitrary task without running it:

```sh
python3 -m loom.tools.coordination --database "$HOME/loom-runs/coordinator.sqlite3" register \
  --task my-task-v1 --source-commit "$(git rev-parse HEAD)" --spec task-spec.json
```

`graph-replay` accepts saved `input_packet.json`, `method.json`, and a request /
first-response pair for **every** declared stage. A read-only preflight through
the existing graph workflow checks each generated request against the saved
request, including dependencies and prior proposals. It then replays the loaded
immutable responses through the coordinated wrapper. This repeats local graph
processing, not model calls. File byte hashes, transcript hash, packet/method
byte hashes and preflight result are bound into the task specification. Changes
to even whitespace in a saved response are therefore detectable on reuse.

Missing stages, changed requests or historical transport-error markers fail
before task registration/dispatch with JSON evidence on stderr and exit code 2.
No replacement response is generated. Replay preserves a stored model's output
and does not certify the model's identity or its semantic quality. A final
stored malformed model output may reproduce an unavailable workflow stage;
`completed_stages` remains the explicit diagnostic. `--explicit-acceptance`
can supply a JSON map accepted by the existing workflow; generated requests must
still match, and the selection is part of task identity. It does not upgrade
source/evidence status. All successful command output is machine-readable JSON.

## Durability scope and limits

SQLite uses local filesystem locking, `BEGIN IMMEDIATE`, WAL and FULL synchronous
writes; each operation owns a short connection. Different processes/threads must
point to the **same** database on a filesystem with correct SQLite locking. NFS,
cloud-synced copies, independent Git checkouts and multi-host consensus are not
established by these tests. All participants need one consistent wall clock.
Clock movement forward can conservatively produce uncertainty; backward movement
can delay recovery. Claim exclusivity does not depend on a worker PID surviving.

`store.backup(new_path)` creates a coherent SQLite backup including committed WAL
contents, fsyncs it and its parent directory, and returns its SHA256. It refuses
overwriting a prior snapshot. Do not copy only the live main database file and
omit its WAL. Remote publication of the snapshot and first-output artifacts is
still the coordinator's responsibility; this module cannot survive deletion of
every copy of its storage. Independent restored copies must not both execute.

If a callback result is available after expiry, the generic wrapper preserves
its finite JSON as a late observation without promoting the task to completed.
Invalid/non-JSON return data cannot be serialized by this JSON contract. Callback
exceptions are recorded by type only, avoiding accidental exception-message
credential capture. Provider/transport raw bytes remain the existing runtime's
responsibility. No provider client, credential lookup, paid call, native-store
write, API/ABI change, or new domain schema is introduced here.

## Verification

```sh
python3 -m unittest discover -s loom/tools/coordination -p 'test_*.py' -v
```

The suite includes twelve independently spawned processes competing for one
durable file effect, killed workers before/after external effects, kill during an
uncommitted database transaction, restart/reopen, stale fencing, terminal refusal,
explicit uncertainty reconciliation, late results, live-WAL backup, and the real
existing graph workflow. It uses synthetic fixtures only; no sealed data or
network access. Exact captured output and source hashes are adjacent. These tests
are separate from the historical structure/contract/native test denominators.

The initial 19-test local run passed before adding observer-expiry retention and
backup coverage. The subsequently captured run is a distinct measurement, not
19 additional tests. This is mechanism evidence, not model-quality evidence.

Independent review by the retrieval lane found a dispatch-order gap: fetching
the task after `begin` could observe expiry yet still call the executor. The
wrapper now fetches inputs first and calls `begin` as its final store operation
before dispatch. A forced-expiry regression proves no callback/effect occurs
after that known loss. `verification_after_review.*` records the corrected
22-test suite; the earlier 21-test receipt is retained separately.

The later operator CLI adds nine actual subprocess tests (register, inspect,
backup, existing three-stage transcript replay, duplicate refusal, ambiguity,
missing/mismatched saved stages, changed response bytes, unknown state and
historical transport errors). `verification_cli.*` captures the combined suite;
it is separate from the earlier 21- and 22-test snapshots.
