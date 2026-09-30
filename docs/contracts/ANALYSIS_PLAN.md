# AnalysisPlan: independent methods, variants and resource accounting

`loom.analysis_plan/1` is a data contract and callback-only reference runtime.
It represents deterministic algorithms, regex, vector retrieval, model judgments
and multi-role agent methods in the same DAG without assigning a closed list of
method, runtime, domain or model names. It does not deploy those capabilities.
Scopes, source refs, variant axes, roles, tools, reasoning, acceptance and
evaluation policies remain data. Different ecosystem domains declare their own
`semantic_contract`; this contract does not silently impose one graph ontology.

The local schema is loaded by the existing `ContractValidator` registry. Its
global `SCHEMAS` dispatch table remains unchanged pending review. Use
`analysis_plan_ref.validate_plan` for schema and relational validation: cycles,
missing dependencies, duplicate identities, unknown refs/axes, invalid selections
and malformed reservations fail explicitly. No remote schema retrieval occurs.

## Lazy variants and execution

An axis is either an explicit values list or an integer range. Cartesian
cardinality is arbitrary-size integer arithmetic. `selected_variants` is a
generator; `variant_at(plan,index)` decodes one coordinate in O(number of axes).
A billion-worker role or a 10^60 range is representable without creating workers
or allocating its product. `selection` chooses all lazily or explicit indices;
the caller owns scheduling and how far to consume the generator.
Direct `execute_variant` also enforces an explicit indices selection before any
attempt or callback; `variant_at` can inspect any coordinate without execution.

`execute_variant` sorts the declared method DAG topologically. Each method has
its own effective axes; dependency attempt identities also enter the child's
identity, so changed parent variants cannot reuse a stale child. Method-list
presentation permutation preserves logical attempt identity. Other ordered data
such as source ordering and variant value ordering stays significant.

Each source has an explicit binding: `content_fingerprint` with SHA-256,
`revision`, `pinned_snapshot` with a named snapshot, or `unbound`. Bound identities
enter the plan/attempt hash. Reuse is explicitly labeled
`dispatch: replayed_bound_snapshot` and repeats the bindings; it is replay of that
snapshot, not fresh access to a mutable ref. A changed binding creates a new
attempt. An unbound source is valid descriptive plan data but this reference
reports `unavailable: unbound_source_snapshot` without calling a loader or method.
The caller can resolve a dynamic ref into a new immutable snapshot before
constructing the executable plan. Named revisions/snapshots are caller assertions
of immutability; this runtime does not fetch source bytes to verify them and does
not claim that a fingerprint proves a semantic interpretation.
Machine receipts label this `source_binding_verification:
caller_declared_not_verified`. A domain adapter can verify raw bytes separately;
an aggregate graph projection packet is not automatically equal to a raw source.

Only caller-registered `(method,runtime)` callbacks execute. Missing capability,
missing callback and declared unavailable dependency are explicit separate
outcomes. An independent method can still run; no semantic/lexical channel veto
is invented. Every branch result is returned and saved as an exclusive execution
receipt. This runtime performs no model API call or credential loading itself.
The callback registrant remains responsible for its own external side effects.

An optional `packet_loader(context)` supplies the graph lazily **after durable
reservation**. Its returned packet is copied before entering a callback. The
first finite JSON loaded packet is exclusively preserved with its own SHA-256
before the method callback, independently of source-binding verification. The
existing `agentic_graph_v1.packet` API works through this seam; alternate domain
packets also work. The executor never calls `apply_diff`, merges candidates or
executes acceptance policy. Caller policy may choose any configured acceptance
behavior in a separate consumer; the fixed demo is not a user restriction.

## Presets and explicit resource limits

Presets carry defaults. Independent `resource_limits` specify money, CPU and
GPU seconds, input/output tokens, calls, agents, wall seconds, memory bytes and
storage bytes. Every dimension explicitly declares `cumulative` or `peak`
accounting and a nonnegative limit or `null` for no ceiling. No universal agent,
variant, model or money cap is baked into this reference. User-configured limits
are admission controls, not proof that an arbitrary callback obeys them.

Reservations are estimates. Callback `measurements` are separate quantities;
every nonnull quantity requires typed provenance with a nonempty `source_ref`
and status `instrument_measured`, `provider_reported`, `estimated`, `unknown` or
`declared`. Empty/null provenance makes the attempt uncertain and cannot release
its reservation. Missing/null measurements always retain their dimension's
reservation. A null quantity cannot carry a measured/reported status.

Every dimension's explicit `measurement_policy` maps each status to one of
`use_declared_amount`, `retain_reservation` or `max_reservation_amount`. This is
admission accounting policy, not a change in epistemic status. The example uses
reported/instrument quantities and conservatively holds the greater of original
reservation and amount for estimates, unknowns and declarations. A caller can
allow estimates for planning via policy; the preserved quantity remains labeled
estimated. `resource_usage` is labeled policy accounting, not actual measured
consumption. Actual overruns remain recorded and block further admission under
the original limit; limits never expand to conceal a failure. Peak admission
sums outstanding/held reservations and reuses capacity after completed admitted
amounts, retaining the greatest accounted peak. Uncertain callbacks hold capacity.

One `ResourceLedger(directory, limits, budget_id=...)` is shared by all methods
and plans using that budget. Reopening cannot change limits or reset history.
When adopting an existing external budget, supply `opening_balances` for every
dimension: separately `measured`, `reserved_unknown` and typed `provenance`.
Nonzero known usage requires instrument/provider reported provenance. Otherwise
the caller explicitly declares a fresh zero opening accounting baseline, labeled
`declared` with its scope; this is not a measurement of previous usage. The runtime does
not discover existing API usage or infer that a new directory means new funding.

## Durable first attempts and limitations

Under a POSIX process lock plus thread lock, admission creates an exclusive
attempt directory, writes/fsyncs its reservation and fsyncs directory entries
**before packet loading or callback invocation**. A deterministic attempt ID
cannot invoke a callback twice, including concurrent requests. The first returned
finite JSON candidate is written/fsynced as `returned_first.json` before metadata
validation. Invalid metadata preserves that candidate for caller review, holds
the reservation and cannot create a completion. Success additionally writes
`result.json`, then a completion bound to its exact quantities/provenance and the
first return hash. Both replay and budget admission reject drift. An exception or
interruption leaves the reservation held and prohibits silent retry. Corrupt or
incomplete ledger files fail closed; exception text is not copied into receipts.

This is an offline reference with local filesystem durability, not a distributed
worker scheduler, sandbox, network policy engine, external-billing verifier,
CPU/GPU monitor or global transaction manager. Callback measurements have the
provenance and uncertainty their instrument actually provides. No exactly-once
guarantee is claimed for arbitrary external side effects after a crash: the
guarantee is at-most-one callback dispatch per durable attempt identity.

A callback that invokes another runtime still needs an adapter carrying the
same budget admission through each child model/tool step. This reference does
not transfer a parent reservation into child leases, aggregate tool fees,
propagate revocation or prevent nested-runtime overdrafts before they happen.
Counting both parent envelopes and child settlements would double-count; merely
reporting child totals after the callback is not pre-call admission. The separate
`docs/research/shared_runtime_budget_audit_v1/` records these future seam
requirements with scripted controls. No nested paid runtime is wired here.

Run the zero-API demonstration from the repo root with declared local contract
dependencies and a fresh output directory:

```sh
PYTHONPATH=DECLARED_CONTRACT_DEPS:. python3 -B docs/contracts/examples/analysis_plan_demo.py \
  --output /tmp/new-analysis-plan-demo
python3 -B -m unittest loom.tools.contracts.test_analysis_plan_ref -v
```

The demo preserves scripted graph diffs and uncertainty receipts, allocates zero
workers, and measures no model quality. It cannot justify a frontier pilot or
new spending authorization. Native wiring and remote runtimes remain outside
this increment.
