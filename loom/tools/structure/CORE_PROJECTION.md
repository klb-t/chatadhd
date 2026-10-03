# Ephemeral projection of the authoritative core graph

`core_projection.py` consumes actual Loom model JSON bodies. The persistent
knowledge graph remains authoritative; the output is a read-only, reproducible
incidence view for bounded graph comparison. It does not create another Claim
model, parse text, generate a logical AST, change assessments, or write a store.

The implementation was based on:

- `loom/src/model/model_core.cpp`: `Claim`, `Assessment`, `Qualifiers`, `Entity`,
  `Observation`, support and dependency serialization/validation.
- `loom/src/model/model_general.cpp`: actual `SlotValue` serialization.
- `loom/src/kb/schema.cpp`, `store.cpp` and `include/loom/knowledge_store.h`:
  canonical `body` JSON and run-scoped persistence.
- `loom/src/materialize/materialize.cpp`: dossier product slots contain claim
  references and display strings, which must be dereferenced before projection.
- A fresh native repository-source run and its actual read-only SQLite export.

The old `docs/selfhost/graph.json` is a term/theme archive visualization, not the
authoritative Claim/Assessment graph. It is not used as a substitute.

## Input contract

```python
result = project_core(export, mode="structural", claim_ids=["cl_..."])
```

`export` requires a nonempty `run_id` and a `claims` array of canonical Claim
objects. Optional arrays are `entities`, `observations`, `principles`,
`operators`, `morphisms`, `instances`, `areas`, `models`, `predictions`, `checks`,
`sources`, `units` and `slot_values`. The full wrapper, including any other
fields, is retained unchanged in `source`. Selected claim IDs must exist in the
same run. Duplicate record IDs within one namespace are rejected.

The actual core Claim shape is:

```text
id, subject, predicate, object, value,
qualifiers {valid_from, valid_to, version, branch, scope, lang, extra},
assessment {
  basis {support [{observation, locator, quote, extractor, quality}], derivation},
  evidence_class, origin, confidence, premises, counter, status, consequences,
  open, expected_property, check_state, alternatives
}
```

Core Claims use an object entity **or** a literal JSON value; absent claims have
neither. Premises/counters/consequences retain their actual namespaced reference
lists. Derivations retain the source operator, version, morphism and depth. No
modal, negative or quoted reading is inferred from text: explicit
`qualifiers.extra` content stays exact, including unknown keys.

In SQLite, `(run_id, id)` identifies a canonical body in each `loom_kb_*` model
table. `loom_kb_slot_values.body` is a `SlotValue`; its `instance_id` SQL column
must be added as `instance` to obtain actual `SlotRow::to_json()` shape:
`{instance, slot, ord, claim, role, conflict}`. Product IDs, dossier display text
or an old generic `{nodes,edges}` visualization cannot replace these bodies.
The separate `core_snapshot.py` reader owns transport; this module accepts JSON.

## Incidence structure and source trace

Each selected Claim has one reified `core_claim` vertex. Directed edges retain
`subject`, `predicate`, `object`/`value`, scope membership, support, derivation,
premises, counters, consequences, alternatives and authoritative slot roles.
Shared core entity IDs reuse one vertex; equal display labels never merge two
entities. Predicate strings have their own vertices and remain exact relation
semantics. JSON literal values retain type, content and array order.

The source qualifier `scope` becomes a shared scope vertex and
`qualified_by_scope` edge. Scope names can be consistently renamed for an analogy
without merging scopes or deleting their membership relations. If exactly one
exported entity/area/instance has that ID, `scope_refers_to` records the existing
reference. Missing/ambiguous targets are explicit unknowns. Hierarchy comes only
from actual source relations, such as `Entity.parent`; no hierarchy is invented.

Universal roles are read only from actual slot rows and placed on
`core_slot_assignment` vertices. A predicate name does not invent an intent,
actor, constraint or other role. Slot conflict, order and additional fields
remain represented.

The native extractor's documented `Entity.attrs.units` and
`Entity.attrs.observations` arrays become typed provenance edges, preserving
their order and producer-convention version. The convention is grounded in
`extractors.cpp::Run::entity/run`, `extract/stage.cpp::merge_entity` and resolver
consumers. Other `attrs` fields remain opaque and exact; arbitrary ID-looking
strings are never interpreted as references. The native observation-array cap of
256 is reported when reached, because the list may be incomplete.

Optional principle/operator/instance and similar bodies are retained as exact
opaque attributes when referenced. Their internal semantics are not invented.
This can restrict analogy matching when such payloads contain names; the
`optional_record_semantics_retained_opaque` unknown records that limitation.
References outside the selected claim set remain explicit, with their source
body retained rather than silently expanding the selected query.

Deterministic projection IDs derive from the run, source namespace/ID and
incidence position. They do not replace source IDs. `claim_node_ids` maps root
Claim IDs to projected vertices. Graph vertices/edges use the existing
`claim_ids` trace convention. `reference_map` keeps record-keyed source paths and
`source_claim_ids`; these paths are references, not RFC 6901 array pointers.

## Three explicit views

| Mode | Comparison labels | Retained constraints |
|---|---|---|
| `semantic` | Literal source identities, descriptions, quotes and coordinates | All modeled qualifiers, statuses, evidence, roles, relation types and binding relations |
| `structural` | Named-reference identity can be consistently renamed; entity descriptions and raw quote/locator labels are omitted | Entity kind, predicate string, roles, direction, multiplicity, scope membership, non-reference qualifiers, assessment status/evidence/confidence/checks remain exact |
| `topology_control` | Kind, role, qualifiers, lexical identity and edge predicate labels removed | Directed incidence and multiplicity only; explicitly lossy and unsuitable for semantic conclusions |

Structural graphs retain `lexical_identity={namespace,value}` metadata.
`graph_search` requires an explicit per-side name projection for
`structural_analogy` and checks consistent injective renaming. There is no
implicit alias resolution. Fixed predicate strings and entity kinds cannot be
silently projected into different relation/type meanings.

`dropped_attributes` lists comparison-label omissions with source paths, values,
reasons and `retained_in_source=true`. No source attributes are deleted.
`projection_scope` lists selected/excluded Claim IDs, records kept only in the
source snapshot, and wrapper fields not projected. Thus a small query view does
not pretend to contain every exported record. All views remain ephemeral; no
projection is a new authoritative ontology or persistent parallel graph.

## Gates and unknowns

`gates` reports locally visible source-contract violations, missing assessment
information and restrictions such as absent/extrapolated premises, non-active
status and transfer depth. `locally_consistent` is a narrow validation report,
not a call to the full C++ validator, verified source truth, or a calibrated
assessment of the projector.

Missing referenced records, unsupported optional-record semantics and quote
mismatches are explicit `unknowns`. Raw source bytes are not fetched or verified
by this adapter. The source assessment, including its confidence, is preserved;
the projection's own confidence remains null. Inference eligibility, Claim
persistence and automatic mutation are always false. The output explicitly says
`semantic_soundness="not_assessed"`; topology matches do not establish meaning.

## Verification and native-source diagnostic

```sh
python -m unittest discover -s loom/tools/structure -p test_core_projection.py -v
```

**15/15 author-owned tests pass**. They exercise source/trace preservation,
shared-identity and scope distinctions, hard status/negation/modality/branch
labels, authoritative roles and relation types, dependency directions, explicit
losses, malformed assessments, source restrictions and the graph-search contract.
No independent fixture or answer key was read.

A native run over three repository architecture documents, with external models
and priors disabled, produced run `kr_4b76e077273881e3`: 1,002 observations,
60 entities, 2,182 Claims and 2,018 slot rows. Its source Claim counts were 238
observed/active, 14 observed/contested, 1 derived/active, 68 extrapolated/active
and 1,861 absent/active. These are stored output counts, not accuracy measures.

Projecting the first three observed Claim IDs from the read-only snapshot in
each mode produced **48 vertices and 109 edges**. Their visible source contracts
were locally consistent. Missing unit/source records and opaque optional-record
semantics remained explicit unknowns. This check establishes compatibility with
real persisted core bodies; it is neither an independent benchmark nor an
editorial assessment of the core extractor's interpretations.

Frozen projector SHA-256:
`42e864af2046d036f31b1db30e1199a66d7a6b335d24e14090b5a9459ee8aba6`.
