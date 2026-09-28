# Grounded frames experiment B

`grounded_frames.py` separates exact source anchoring from operation composition.
Its output is the same `loom.candidate_graph/1` Entity/Claim draft bundle consumed
by `candidate_graph.compile_bundle`. Frames exist only as transient input syntax.
There is no second knowledge store, authoritative AST, native promotion, model
request, calibrated confidence, or logical inference in this module.

The source packet and common candidate contract are defined by
`candidate_graph.py`. A packet contains exact located Observations, a snapshot
identity, and allowlisted older Entities and Claims with their Assessments.
Those older records are reference context, not newly observed support. This
adapter preserves the complete packet through the common compiler and never
looks up additional IDs. Source bytes are checked against the supplied
Observation; this does not independently verify the original source file.

## Two stages and shared compilation

```python
prepared_report = prepare_anchors(source_packet, anchor_response)
expanded_report = compose_frames(
    source_packet, prepared_report["prepared"], composition_response)
report = compile_frames(source_packet, anchor_response, composition_response)
```

All three APIs return reports for ordinary invalid input. `valid` and
`status: valid|rejected` describe contract acceptance, not semantic correctness.
`compile_frames` identifies rejection at `anchors`, `composition`, or `compiler`
in `stage` and `stages`. Each accepted composition alternative has its own
`{id, bundle, compiled}` result. Alternative graphs are never combined.
`no_inference` and `no_persistence` remain true at every stage.

Stage 1 has exactly these fields:

```json
{
  "schema": "loom.grounded_anchors/1",
  "packet_id": "the source packet snapshot_id",
  "anchors": [],
  "coverage": [],
  "unknowns": []
}
```

Each anchor is exactly a common-contract Entity draft:
`{handle, kind, label, attrs, support}`. Kinds are `expression_occurrence`,
`term_occurrence`, `binder`, and `scope`. Support is a nonempty list of
`{observation, byte_start, byte_len, quote}` with observation-local UTF-8 byte
coordinates. Handles start with `@`, are unique, contain no whitespace and have
at most 200 characters. Scope, term, and binder attributes retain the shared
compiler's meanings; the anchoring stage does not establish those meanings.

Stage 1 coverage uses the common fields
`{support, status, reason, drafts}`; `drafts` here references local anchors.
Unknowns use `{support, reason}`. Reasons are nonempty. Coverage statuses are
`represented`, `partial`, `unsupported`, `ambiguous`, and `omitted`; represented
coverage needs an anchor reference. These records survive composition unchanged.
They are proposed coverage labels, not an independently measured recall score.

The prepared result binds both the full source packet and the entire anchor
response, including source support, attributes, unknowns and coverage:

```python
C = lambda x: json.dumps(x, ensure_ascii=False, sort_keys=True,
                        separators=(",", ":"), allow_nan=False).encode("utf-8")
packet_hash = hashlib.sha256(C(source_packet)).hexdigest()
anchors_hash = hashlib.sha256(C({"packet_hash": packet_hash,
                                 "anchoring": anchor_response})).hexdigest()
```

This public digest detects changed inputs between stages. It is not a signature
or authentication boundary against a caller who controls both responses.

Stage 2 has exactly these fields:

```json
{
  "schema": "loom.grounded_frames/1",
  "packet_id": "the source packet snapshot_id",
  "anchors_hash": "the prepared digest",
  "alternatives": [
    {"id": "reading-a", "frames": [], "links": [], "roots": [],
     "coverage": [], "unknowns": []}
  ]
}
```

Every frame contains
`{anchor, operation, scope, operands, polarity, assertion_context, support, premises}`.
Each operand is `{port, ordinal, target}`. Anchor and scope references must
already exist in stage 1. Operand targets may also use an allowlisted native
Entity from the frozen packet where the common port contract permits it
(predicate-application predicate/argument). `support` is a nonempty list of earlier anchor
handles, expanded to a sorted, deduplicated union of their unchanged spans.
No stage 2 field can supply a new anchor, source quote or source byte range.

| Operation | Operand ports | Additional fields |
| --- | --- | --- |
| `predicate_application` | one `predicate`; zero or more ordered `argument` | none |
| `conditional` | one `antecedent`, one `consequent` | none |
| `negation` | one `body` | none |
| `conjunction` | at least two ordered `member` | none |
| `quantifier` | one `binder`, one `body`, optional `restriction` | `quantifier_kind: forall|exists`, `introduced_scope` |

Fixed ports have ordinal 0. Repeated ports use contiguous ordinals from 0.
The common compiler checks port counts, target types, scope and binder capture.
An unsupported operation must remain a located unknown; it cannot become a
wildcard frame. Negation, quantifier kind, direction, operand order, and repeated
binding are never erased by the adapter.

Links contain exactly
`{subject, predicate, object, scope, polarity, assertion_context, support, premises}`.
The supported predicates are `in_scope`, `scope_parent`, `bound_to`, and `denotes`.
An explicit `in_scope` link is required for every local non-scope entity,
including each operation occurrence. A `scope_parent` link is qualified by its
child subject scope; `in_scope` is qualified by its target scope. Other links
use their subject's scope. A frame's `scope` supplies its Claim
qualifier; the adapter does not guess entity membership from it. Quantifiers
introduce child scopes explicitly. Equal displayed variable names do not merge
occurrences or choose their binders.

`denotes.object` may reference an allowlisted native Entity in the frozen packet.
All other link objects are earlier local handles. `premises` contains existing
native Claim IDs, checked against the packet by the shared compiler. It becomes
`Assessment.premises.claims`; it is never an operand list. Both unresolved
reference alternatives and different binder/scope interpretations can be
preserved as separate alternative readings. No best reading is chosen here.

Polarity is explicitly `positive|negative|unknown`; assertion context is
`asserted|hypothetical|quoted|unknown`. These qualifiers describe the proposed
source interpretation and do not establish the truth of an expression.

Stage 2 coverage and unknown records use the same fields as stage 1, except
their `support` lists anchor handles. Coverage `drafts` also names earlier
anchors. Their expanded records are appended to the earlier records, preserving
unresolved stage 1 information. Unknowns never create graph vertices or satisfy
missing operation ports.

## Deterministic expansion and limits

A frame expands to an `operation_type` Claim and its `operand` Claims.
Quantifiers additionally produce `quantifier_kind` and `introduces_scope`.
Links expand directly to Claims. Every generated Claim uses the common native
field shapes, exact support and explicitly supplied prior Claim dependencies.
Its local handle is `@gf_` plus the full SHA-256 of canonical JSON of the draft
without its handle. Exact duplicate drafts are deduplicated. Different ordered
occurrences remain distinct because their port ordinals differ. Entity and
Claim arrays are sorted by local handle; array serialization order and the
alternative wrapper ID do not determine semantic graph identity.

Default adapter bounds are 256 anchors, 256 frames per reading, 2,048 links per
reading, eight readings and 4 MiB for each input envelope check. `limits` may
only reduce these bounds. A limit violation rejects the envelope and retains
the input; it does not silently omit an anchor or claim. Common compiler limits
apply separately through `compiler_limits`. No provider-token or paid-request
budget is claimed because this adapter makes no provider requests.

## Author checks and interpretation of results

Run the offline developer checks from the repository root:

```sh
python -m unittest discover -s loom/tools/structure -p test_grounded_frames.py -v
```

The checks cover exact Unicode byte offsets, packet/anchor mutation, forbidden
source-span injection, unknown operand handles, syntax versus epistemic
dependencies, explicit unknowns, separate alternatives, ordering, repeated
printed names, all five operations, and shared-compiler rejection of missing
scope or non-allowlisted references. They use author-owned examples only.

Passing checks shows preservation and rejection behavior for these supplied
annotations. It does not measure span extraction, reference selection, source
interpretation, natural-language coverage, provider reliability, or improvement
over direct graph generation. Independent A/B assessment must compare each
encoding with independently authored source gold; equality between two paths
through the shared compiler alone cannot detect a shared compiler defect.
