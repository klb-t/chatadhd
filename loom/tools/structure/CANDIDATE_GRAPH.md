# Source-grounded candidate occurrence graph, contract v1

Public contract frozen before independent scoring. This is a research compiler
for drafts carried by the existing candidate queue, not a second authoritative
AST, native promotion path, text parser or inference engine. It makes no provider
calls. Vocabulary is data in `candidate_graph_vocabulary.json`; it does not
modify the core universal-role enum or executable Operator vocabulary.

APIs are `validate_bundle(bundle, source_packet, limits=None)` and
`compile_bundle(bundle, source_packet, limits=None)`. Ordinary invalid JSON-shaped
inputs return a report, not an exception. Both return `valid`, `status`
(`valid`/`rejected`), `errors` (`code`, `path`, `message`), `coverage`,
`packet_hash` and `retained_input: {bundle,source_packet}`. Compilation adds
`drafts:{entities,claims}`, `graph:{id,nodes,edges}`,
`refmap:{entities,claims}`, `losses`, `no_inference:true`,
`no_persistence:true`. Rejection emits an empty graph and preserves both inputs.
Located unknown-only bundles are valid abstentions with an empty graph and
`coverage.representation_status:unrepresented`, not represented successes.

## Packet and bundle

Packet fields:

```json
{"schema":"loom.source_packet/1","snapshot_id":"snapshot-A",
 "observations":[],"entities":[],"claims":[]}
```

Observations are native-shaped records with `id`, `unit`, exact `text`, and
`locator.source`, plus any original fields. Entities and Claims are an explicit
allowlist of old graph context. Existing Claims retain their full Assessment;
the validator requires at least `basis`, `premises`, `evidence_class`, `origin`,
`confidence`, `status`. It does not invent missing defaults. Packet metadata is
retained verbatim but not interpreted as timing or authorization. The entire
packet is hashed, so reusing a snapshot label with different bytes does not
identify the same compiler input. Context selection and future-data exclusion
are responsibilities of the packet producer, separately evaluated.

Bundle fields (all required; no unknown top-level fields):

```json
{"schema":"loom.candidate_graph/1","packet_id":"snapshot-A",
 "entity_drafts":[],"claim_drafts":[],"roots":[],"coverage":[],"unknowns":[]}
```

Local handles begin `@` and are unique across entity and claim drafts. They are
not canonical entity IDs. Claim endpoints resolve only to local entity handles
or allowlisted existing Entity IDs. Existing Claim IDs appear only in
`assessment.premises.claims`, never as endpoints or untyped literal shortcuts.
Competing readings are separate bundles; v1 has no in-band alternatives field.
The two-stage grounded-frames adapter compiles each alternative through this
same contract.

A support span is exactly
`{observation,byte_start,byte_len,quote}`. Offsets are nonnegative UTF-8 byte
offsets within the observation; length and quote are nonempty, boundaries must
align with UTF-8 code points, and the exact bytes must match. Every entity and
every claim draft has source support. The compiler copies the original locator
and observation text hash into compiled support. It checks source grounding,
not whether the interpretation follows from the quote.

Entity draft fields are exactly `{handle,kind,label,attrs,support}`:

| Kind | Exact attrs |
|---|---|
| `expression_occurrence` | `{}` |
| `term_occurrence` | `{term_type: constant/variable/predicate, symbol: nonempty string}` |
| `binder` | `{symbol: nonempty string}` |
| `scope` | `{scope_type: assertion/quantifier/quotation/hypothesis, assertion_context: asserted/hypothetical/quoted/unknown}` |

No entity draft inherits native `observed`, confidence or active defaults.
Occurrence labels are source labels, not an instruction to identify equal
strings. Canonical identity, when known, is an explicit `denotes` relation.

Claim draft fields are exactly:

```json
{
 "handle":"@claim",
 "subject":"@entity",
 "predicate":"operand",
 "object":"@other-entity",
 "value":null,
 "qualifiers":{"scope":"@scope","extra":{
   "polarity":"positive","assertion_context":"asserted",
   "port":"body","ordinal":0}},
 "assessment":{"basis":{"support":[]},"premises":{"claims":[]}}
}
```

Object XOR non-null value is mandatory. `operation_type` and `quantifier_kind`
use literal strings; all other supported predicates use entity objects. The
Assessment is deliberately partial: no claim ID, evidence class, confidence,
Expected Property or invented native `status:candidate` appears in the input.
Candidate review status belongs to the candidate envelope/table. Premises must
be allowlisted Claims and cannot be absent/extrapolated or rejected/superseded.
Using an eligible reference is still not executing an inference.

## Operations, ports and scope

Each expression has exactly one `operation_type` Claim, whose value is one of:

| Operation | Required ports / optional ports |
|---|---|
| `predicate_application` | `predicate` exactly 1; `argument` 0–64 |
| `conditional` | `antecedent` exactly 1; `consequent` exactly 1 |
| `negation` | `body` exactly 1 |
| `conjunction` | `member` 2–64 |
| `quantifier` | `binder` exactly 1; `body` exactly 1; `restriction` 0–1 |

Ports are `operand` Claims from the operation to an entity. Each port's ordinals
are contiguous from zero; a fixed port has ordinal zero. Predicate targets are
predicate terms or explicitly supplied existing entities. Arguments target
terms or existing entities. Other expression ports target expression
occurrences; `binder` targets a binder. A quantifier also has exactly one
`quantifier_kind` Claim with value `forall` or `exists` and one
`introduces_scope` Claim to its own child scope.

Conjunction order is retained as source representation order. Its reordering
is not claimed to change classical truth conditions. Conditional direction,
negation nesting, quantifier kind/order and repeated binding remain explicit
graph structure, not labels in an opaque formula literal.

Every local non-scope entity has exactly one `in_scope` Claim. Each scope has
zero or one `scope_parent` to another scope; parent cycles are forbidden.
Every quantifier scope has exactly one introducing quantifier. Its binder,
body and optional restriction live in that child scope, whose parent is the
quantifier occurrence's scope. Other operand relations stay in the same scope,
or enter a direct child quotation/hypothesis scope; they cannot escape to a
parent, sibling or unrelated quantifier scope.

Claim qualifier scope is subject scope, except `in_scope`, whose qualifier is
its target scope, and `scope_parent`, whose qualifier is the child subject
scope. Thus `introduces_scope` is qualified by the quantifier's parent scope,
and `bound_to` by the variable occurrence's scope. Every claim's explicit
assertion_context must equal its qualifying scope's context. Polarity is
retained explicitly; it is coarse interpretation metadata, not a substitute
for a nested `negation` occurrence or a validity guarantee.

`bound_to` links a variable term to exactly one visible binder with the same
symbol. The binder must be owned by a quantifier and be in the variable's scope
or an ancestor. A nearer binder of the same symbol shadows the outer one, so
an explicit reference cannot silently capture or escape a binder. Printed
symbol equality by itself never creates a binding. Free variables are outside
v1: retain their source as located partial/unknown coverage. `denotes` links a
nonvariable term to an existing Entity; it never merges occurrences or rewrites
that entity.

Expression operand nesting is acyclic and bounded in depth. Every expression
is reachable from declared expression roots, and every term/binder is used by
the represented expression structure. Scope/reference edges may form ordinary
graph cycles without being confused with syntactic recursion.

## Coverage, output and limits

Coverage row:

```json
{"support":[],"status":"represented","reason":"explicit source shape",
 "drafts":["@entity","@claim"]}
```

Statuses are `represented`, `partial`, `unsupported`, `ambiguous`, `omitted`.
Each row needs nonempty exact support and reason; represented rows need draft
references. Unknown row is exactly `{support:[span],reason:nonempty string}`.
Unknowns are located records, never graph nodes that match any missing operand.
Uncovered byte intervals remain explicit. Coverage counts unioned byte ranges
per status and may overlap; it does not infer semantic correctness from a
self-reported represented label.

Compiled drafts have deterministic *proposed* IDs (`draft_e_…`, `draft_c_…`),
not promotion-ready canonical IDs. Their partial Assessment remains partial.
The graph is a derived claim-incidence projection: claim nodes connect to
subject, object/literal, qualifying scope and prior-claim reference nodes.
Port/ordinal, polarity/context and scalar operation values remain exact graph
labels. An expression node also retains explicit `declared_root` membership;
adding a separately declared root is not silently identical to merely nesting
the expression as an operand. Binder links and scope links therefore participate in matching. Full
Assessment and source bytes remain in the retained packet and draft sidecars;
moving them out of shape comparison is a declared loss, not deleting evidence.
`refmap` connects every proposed entity/claim ID to its input handle or existing
record. No AST is required or persisted.

Default hard limits: packet 1 MiB; bundle 256 KiB; 128 observations; 256 KiB
total observation text; 256 local entities; 1024 local claims; 16 support spans
per record; nesting depth 32. `limits` may lower these named maxima; unknown
keys, booleans or values outside 1..default are rejected. Full invalid input
is retained even when a size limit prevents compilation.

Author examples/tests are in `test_candidate_graph.py`. Independent validation
texts and labels are deliberately withheld from the compiler author. This
contract test is not live-model quality, broad natural-language coverage or
proof validity.
