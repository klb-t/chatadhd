# Assertion comparison view

`assertion_view.py` is an explicit, reversible comparison view of the frozen
`core_projection.project_core` output. The authoritative objects remain the
native Claim and Assessment records. It does not parse text, reinterpret a
predicate, change an Assessment, propose a persisted Claim, or enable inference.

Public API:

```python
view = project_assertions(core_view, literal_policy="exact_literals")
original_graph = restore_core_graph(view)
report = assessment_dimensions(left_view, right_view, supplied_node_mapping)
```

Only semantic and structural core projections are accepted. The input source
snapshot hash and selected Claim root map are checked. Each selected root retains
subject/predicate/object/value direction, shared reference vertices and their
lexical identities, entity kind, scope membership, validity, branch, language,
all extra logical qualifiers, universal slot roles, premise/counter/consequence
ports, and derivation/operator/morphism references. No unrecognized attribute is
silently classified as evidence metadata.

The shape traversal excludes support/observation/unit/source provenance edges
and their closure. A counter-observation remains an identified reference port.
An unselected or missing Claim target becomes `core_claim_reference`; its body
does not become another selected assertion. Dependency direction and relation
type remain explicit. Source unknowns and source contract gates remain visible.

Claim evidence class, origin, confidence, status and check state move to an
assessment sidecar. The view retains full original Assessments, source records,
attribution and reference maps. Known entity source metadata and alternative
scores likewise move out of shape. Every changed node and every deleted node or
edge is retained verbatim, with a loss-map entry. `restore_core_graph` verifies
the reconstructed graph hash. This reverses this projection only; upstream core
projection losses remain in `source_projection.dropped_attributes` and the full
snapshot retains their original data.

`assessment_dimensions` compares the source values of each mapped Claim's
evidence dimensions and source gates. Missing values are unknown, not equal.
It does not verify the supplied mapping, combine dimensions into confidence, or
declare truth. A shape match with different status is therefore visible without
pretending their Assessments are identical.

`exact_literals` retains complete JSON type/content/order. The deliberately unsafe
`literal_type_control` replaces only `core_literal.json_value` with its JSON type.
It does not change scope, negation or quantifier qualifiers. Literal text itself
may encode those meanings, so this control has `comparison_admissible: false`
and cannot establish analogy or entailment. The original values are retained.

Author tests cover reversible evidence removal, source-assessment separation,
hard logical qualifiers, entity kind and shared binding, external Claim ports,
dependency/derivation/slot roles, unsafe literal collapse, and tampering rejection.
They are not independent semantic validation. Neither independent fixture content
nor new validation outcomes informed this view. The motivating observation was
the documented native diagnostic's evidence-heavy closure and lack of Claim
vertices in recurring motifs.
