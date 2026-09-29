# T5 — Workspace UI-IR and parameter couplings

[U] R29–R31, conceptual model §11.10, acceptance checks #5 and #6;
`docs/GPT_OFFLINE_TASKS_2026-09-29.md` T5. Base read before this increment:
`f9d1168ac29acb91c10b2efb53520a306ca90de7` on the Claude development line.

[P] This is a **reference data contract and Python propagation engine**, not a
browser workspace, docking implementation or native graph integration. It adds
no knowledge database and executes no model/API/tool calls. Its JSON contains
references to graph data, not another copy of the authoritative graph.

## Composition

`workspace.schema.json` (draft 2020-12) describes:

```
Workspace(id, data_ref, parameters, profiles, scopes, bindings, limits)
  └─ Container(id, container_kind, layout, parameters)
       ├─ Container(...)
       └─ View(id, query, projection, renderer, interactions, components, parameters)
```

Container kinds, layout settings, query/projection/renderer identifiers and
component descriptions are **data**. Window, split, tabs and adaptive containers
are examples, not an exhaustive implementation list. Unknown renderer/container
names are preserved without pretending that a renderer exists. Invalid local
node references, however, are rejected. Containment must form a tree with unique
ids; parameter couplings form a separate directed graph and may contain cycles.

The example has **five graph views**, a table and cards, nested in windows,
splits, tabs and an adaptive stack. `reference` is frozen initially. Changing the
active tab is presentation configuration; it does not switch off other views or
change their data. This prototype preserves layout/interaction descriptions but
does not execute arbitrary query or interaction code.

Parameters belong to the workspace, a container or a view. Each has an initial
value, a type, optional numeric bounds/allowed values and explicit nullability.
The parameter namespace is open: selection, time, detail, depth and opacity are
fixture choices, not hardcoded names in the engine. `detail` is not `depth`, and
neither is the token budget or sampling density.

## Coupling semantics — deliberate reference policy

A binding has `source` and `target` endpoints `(owner, parameter)`, an `enabled`
flag, an event `scope`, and `{op,args}`. Direction is literal: the reverse link is
another explicit binding. Scope is a named event channel; `*` bindings listen to
all declared channels. It is **not** a statement about graph/evidence scope.

`dispatch({id,scope,writes:[{owner,parameter,value}],expected_revision?})` performs
an atomic transaction:

1. Validate all seeds and their endpoint types. Direct writes to a frozen node
   or descendant of a frozen container fail.
2. Traverse reachable enabled bindings in the event scope. Frozen destinations
   block both receipt and relay. Detached/out-of-scope links are recorded in trace.
3. An endpoint receives at most one proposed value. Equal proposals coalesce;
   unequal ones (including a return around a cycle or conflict with a seed)
   reject **the whole event**, without a partial state update or silent winner.
4. Validate resulting types/bounds; commit changes only after the traversal
   succeeds. A proposal budget bounds work; exceeding it also rolls back.

Identity cycles converge because each endpoint is expanded once. A cycle adding
one on every return is a conflict, not an infinite loop. No repeated numerical
fixed-point iteration, implicit averaging or last-write-wins rule is supplied.
Binding-array order cannot pick a winner. This is one explicit [P] policy, not a
claim that all future coupling types must follow it. Different numerical solvers
would need their own contract, convergence budget and tests.

Equality uses sorted-key finite UTF-8 JSON: key order is irrelevant; `true`, `1`
and `1.0` remain distinct. Floating-point rounding therefore may expose a cycle
as conflicting; there is no hidden tolerance. This is deterministic encoding for
this reference, not RFC 8785 or proven native C++ numeric parity.

A seed equal to its current value **still propagates**, allowing explicit resync.
Reattaching/thawing itself does not silently catch up. Resync uses a new event id.
Successful event ids are deduplicated within one runtime instance; reusing an id
with different content is rejected. Failed events do not enter that ledger.
`expected_revision` optionally rejects stale changes; public calls are serialized
with a lock. This is not a distributed multi-device synchronization protocol.

## Extensibility and trust boundary

Built-in operations are `identity`, numerical `affine`, and explicit data `lookup`.
Missing/ambiguous lookup results fail, not guess. Additional operations require
an explicitly registered **trusted, pure Python callable**. JSON never runs eval,
imports modules or executes commands. Callables get copies of inputs/arguments;
these are not sandboxed third-party plugins, and purity is a host responsibility.
A malicious callable could cause external effects outside this engine's rollback.

References to canonical data, view queries and input documents are not modified
by propagation. Values/profiles/document exports are detached copies. No model,
selector, query, renderer or remote tool is invoked when a parameter changes.
Before wiring UI events to paid or state-changing work, implement R33's separate
budget/permission/idempotency checks; this prototype does not replace them.

## Detachable provider profiles

Profiles are separately named/versioned configuration objects. The fixture uses
source, interface, conversation, context, request_adapter, tools, model_provider,
memory and rendering. A preset bundles them but applying it requires the caller
to choose an explicit nonempty subset: `apply_preset(preset,components=[...])`.
Applying `interface` alone changes **no other profile, permission data or selected
parameter**. There is no hidden "switch provider" operation.

`capability(name)` returns a profile's declaration: native/equivalent/limited/
unavailable; missing or malformed declarations return unavailable. This is not
a measurement that the tool actually works, an authorization grant, or a claim
that another provider's private service can be cloned. The fixture presets are
fictional and deliberately have unavailable capabilities.

## Save/load and use

`document()` serializes current layout, parameter values, active profiles,
binding toggles and frozen owners. It retains `data_ref` for the referenced graph
version/viewpoint. Reconstructing `Workspace(document)` restores these settings.
The in-memory event-id ledger and revision counter are **not** persisted; saved
viewpoints must not be called durable execution checkpoints. Exported values and
trace may contain private selections and should inherit workspace privacy rules.

From repository root, with existing contract dependencies installed:

```sh
python -m unittest discover -s loom/tools/contracts -p test_workspace_ref.py -v
python loom/tools/contracts/workspace_ref.py --demo
python loom/tools/contracts/workspace_ref.py workspace.json --events events.json
```

CLI returns 0 for successful local validation/propagation and 1 for rejected input
or event. No calls are made. Full trace goes to stdout; error output contains
codes, not parameter values. The demo selects an historical project and changes
local graph depth: selection reaches six live views/workspace, the reference
stays still, and depth changes only the coupled pair.

## Verification and remaining integration

Local **77/77 test methods** pass (author-written mechanism tests, not UX scores).
Tests cover the multi-view fixture, selective bindings, freeze/thaw, detach and
explicit resync, equal/inconsistent cycles, conflicting diamonds/seeds, atomic
rollback, types, transformations, event replay/revision races, serialization,
detached presets and no-network execution. One method also permutes bindings in
20 seeded random identity graphs. A malformed tool capability declaration found
by inspection was guarded explicitly and has a regression test.

Old packaged T1/T2/T3/T4 checks rerun: **74 + 81 + 30 + 35 = 220/220**. Total with
T5: **297/297**. Partial local checkout only; no full repo Python discovery, CTest,
C++ parity, browser, Android, paid requests or real multiwindow interaction tested.
The current head already contains the newer T3 (208 body files) and T4; the older
"T4 not delivered" line in the task note is stale. Existing task/STATE files were
not changed by this increment.

For native integration: map graph queries and renderer registry separately,
keep immutable data references, implement event dispatch at the view boundary,
and run actual multi-view E2E checks #5/#6. The reference contract does not by
itself satisfy the browser/native acceptance criteria.
