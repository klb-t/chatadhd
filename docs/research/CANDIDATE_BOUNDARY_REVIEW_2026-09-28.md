# Candidate graph boundary review, 2026-09-28

This read-only review covered `candidate_graph.py` (A), `grounded_frames.py` (B),
`context_delta.py` (C), and `candidate_flow.py`. The reviewer did not author these
modules. Review inputs were their implementation, public contracts, author tests,
and small reviewer-created examples. No independent evaluation fixture contents
or new independent scoring outcomes were inspected. This is a code and contract
review, not independent semantic-quality validation.

The reproduced representation and chronology defects listed below were corrected
by the module owners and rechecked. The reviewed path remains an offline,
nonpersisting candidate mechanism. It does not establish that a model interpreted
the source correctly, that two patterns mean the same thing, or that a candidate
is a valid inference.

## Reviewed identities

| Module | SHA256 at completed review |
|---|---|
| `candidate_graph.py` | `ade0944646c3cfc5356787612beb14657335db2067b4a705d5221b0f3810125d` |
| `grounded_frames.py` | `bea98192009085e26fe7c161942ed4dbd682e3b3c3bb204c8e7a6b70f9b010c4` |
| `context_delta.py` | `8fd14a79edbb280eb0a16c08eabecece50dbb59d75b25bb78d31d08d5feba470` |
| `candidate_flow.py` | `4a9e4dae2aba8795a81a17cc04e818855b101f0bfdb338eaccf7382e699658d2` |

The reviewer ran 56 passing author tests: 12 for A, 17 for B, 19 for C, and eight
for the flow.
Separately, the targeted reproductions below exercised the reported boundaries.
The reviewer edited only this review document; implementation and regression
test changes were made by the owners before their final freezes.

## Findings resolved during review

| Boundary | Minimal reproduction or code finding | Rechecked result |
|---|---|---|
| A typed node identity | Existing Entity ID `prior:c` plus existing Claim ID `c`; a `denotes` edge and a premise reference caused the generated `prior:c` Claim-reference node to overwrite the Entity node. | Native Entity remains `existing_entity:component`; generated Claim reference uses a collision-free projection namespace. |
| A declared roots | B's author conditional compiled identically with roots `[@if]` and `[@if,@q]`. | Expression nodes retain `declared_root`; the two graphs now differ. |
| A shared syntax bounds | Recursive traversal revisited shared expression children without memoization, allowing exponential paths within the node/depth limits. | Memoized subtree heights retain cycle/depth checks. A reviewer-created 13-expression shared-child DAG required 25 walk calls. |
| A unsupported coverage location | Contract promised uncovered intervals while the first implementation exposed only aggregate uncovered byte count. | Exact per-observation `uncovered_spans` now accompany the counts. |
| B scope integration | Its all-operations author example qualified `scope_parent(child,parent)` by the parent, contrary to A's child-scope rule. | Author example corrected; all five operation families compile through the shared compiler. |
| C exact packet binding | Two packets selected different spans from `Alpha. Beta.` under the same local span ID and base snapshot; one delta previously validated against both. | Delta requires both base hash and exact packet hash. Original packet accepts; rebound span packet rejects. |
| C model-facing future data | A future observation was absent from selected arrays but remained verbatim in the packet's original-snapshot audit fields. | Original bytes are outside the model-facing packet. A reviewer future-text sentinel is absent from serialized packet data. Explicit-base application still succeeds and supports undo. |
| C relationship evidence chronology | A correction sourced at `Early.` could cite only the later `Later.` span. | Source/evidence membership and chronology checks reject later-only evidence. The same guard covers correction, contradiction and analogy proposals. |
| C retained Claim support chronology | Claim availability January 1, support observation January 10, reference January 5, global cut January 20: target checking originally admitted the reference. | Target chronology includes retained support availability; the reference is rejected despite being before the global cut. |
| Flow malformed context isolation | `context: {}` raised a missing-key exception and interrupted the batch. | The malformed record is `context_rejected`; a valid neighboring B record still compiles. |
| C-to-A/B selected support boundary | Exact quotation elsewhere in a selected whole Observation could pass A/B even when outside C's selected current spans. The C owner identified this integration gap. | The flow now checks every compiled Entity/Claim support span against the union of selected current intervals. Outside support is rejected; adjacent selected intervals may legitimately cover one span. Old Claim premises remain separately referenced context. |

Owners also added regression coverage for renamed local span handles and
packet-namespaced overlay rows, preserving reference assignment immutability.
The reviewer ran the resulting C author suite and examined the changed guards.
The final C revision additionally rejects new overlay IDs that collide with any
base record, including records intentionally absent from the model packet. A
reviewer reproduction using an unselected future Observation ID was valid at
packet-relative validation but correctly rejected by explicit-base application.

## Scope, binding and source integrity

A represents operation and operand relations as Claim-incidence graph structure.
Conditional direction, negation nesting, quantifier kind and order, operand
ordinals, introduced scope, and variable-to-binder references survive comparison.
The validator checks scope parent cycles, each occurrence's membership, the
quantifier's owned child scope, binder ownership, nearest same-symbol binder
visibility, operand cardinality and expression reachability. Unknown operands
do not become wildcard graph nodes. Shared occurrences remain actual shared
vertices rather than inferred identity from equal labels.

B binds prepared anchors to the complete source packet hash and binds composition
to the anchor hash. Composition cannot replace or add local anchors. Source
support is expanded only from those anchored records. Native Entity endpoints
and Claim premises remain subject to A's allowlists and type checks. Alternative
readings compile as separate bundles; they are not joined into one interpretation.

A and B verify exact observation-local UTF-8 bytes, including multibyte code-point
boundaries. Compiled support preserves the source locator and observation text
hash. Coverage retains represented/partial/unsupported/ambiguous/omitted rows,
located unknowns, and uncovered spans. These are representation declarations,
not evidence that the interpretation follows from the quoted text.

C retains complete old Claim bodies and Assessments with context selection
reasons, timestamps and source grouping. Unknown timestamps remain unknown.
Anchored reference selection requires a single explicit candidate and temporally
valid evidence; ambiguous and unresolved cases cannot select an identity.
Apply operates on an immutable-base overlay, checks the explicit base snapshot,
preserves delta/packet identities, and does not rewrite canonical records.
When C prepares the flow input, an additional post-compilation support check
prevents whole-Observation visibility or old context from becoming uncited new
draft evidence outside the explicit current-span selection.

## Assessments and comparison

A produces partial candidate drafts. It does not invent an evidence class,
confidence or native candidate status. Existing Claim Assessment fields remain
in the retained packet; their graph reference nodes retain selected evidence
and status labels, with full sidecar retention declared. The compiler does not
execute the premises or certify the old Claims' truth.

The flow's `occurrence_shape` view explicitly removes local display labels and
printed symbols with a per-node loss list. It retains operation literals,
ports/ordinals, declared roots, scope/context kinds, binding edges, shared
vertices and existing canonical identities. Matching notation-free occurrence
shape is not a statement that unresolved constants denote the same object.

Accepted alternatives are searched separately. Alternatives from the same input
record are excluded as recurrence candidates. Source observation, located-text,
quote and prior-Claim overlaps are reported; different IDs or declared groups
do not establish independence. Partial coverage remains available alongside
each variant. No inference eligibility, Claim promotion or canonical mutation
path was found in these modules.

## Remaining contract limits

1. A's direct bundle binds `packet_id` to the supplied snapshot label. The full
   packet hash identifies the compiled output, but the direct response does not
   carry an attestation of its original request packet. Reusing a snapshot label
   with changed context cannot be detected as an original-response mismatch by
   A alone. B's prepared-anchor hash and C's delta packet hash provide stronger
   binding at their respective boundaries. Any future live caller should retain
   an immutable request/response packet-hash association or use a versioned
   response envelope. This offline review does not establish provider provenance.
2. C's standalone `validate_delta` is packet-relative. `apply_view` additionally
   rebuilds the packet against the explicit base snapshot. The audit sidecar and
   retained-input material are for local replay/undo and must not be confused
   with the model-facing packet.
3. A selected Observation retains its full exact text. Citation chronology guards
   do not prove that a future model avoided implicitly reading a later clause
   within that same Observation. A stricter streaming experiment would need its
   own visibility/cut contract and validation; no such result is claimed here.
4. Exact support, typed syntax and graph matching do not validate natural-language
   extraction, quotation interpretation, evidence independence, analogy quality
   or logical consequence. Confidence calibration remains outside this contract.

No additional production changes, native build, provider calls or independent
fixture inspection were performed for this review.
