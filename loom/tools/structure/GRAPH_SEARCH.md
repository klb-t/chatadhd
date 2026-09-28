# Graph-native pattern search

`graph_search.py` operates directly on supplied graph projections. It adds no
ontology, syntax tree, extracted Claim, database table or production write.
Existing entities, claims, references and qualifiers remain the responsibility
of the source graph/projection. A source-grounded graph can be searched for a
smaller directed pattern even when the host contains unrelated material.

Status: experimental `graph-native-search/1`. The frozen earlier
`structure_methods.py` controls are unchanged.

## Matching contract

```python
match_subgraph(pattern, host,
               goal="literal_semantic",
               label_projection=None,
               state_budget=10000)
```

Matching is **directed, typed, injective and non-induced**:

- Every pattern vertex maps to a distinct host vertex with the required label.
- Every directed pattern edge maps to a distinct host edge with the required
  predicate and qualifiers. Two parallel pattern edges require two host edges.
- Extra host vertices and edges are allowed, including extra edges between
  mapped vertices. A self-loop must remain a self-loop.
- This is containment, not whole-graph identity. To test exact graph isomorphism,
  also require equal vertex and edge counts. Neither establishes real-world
  entity identity, logical equivalence, inference validity or applicability.

Results include:

| Field | Meaning |
|---|---|
| `status` | `matched`, `different`, `budget_exhausted`, or `unrepresented` |
| `matched` | `true`, `false`, or `null` for incomplete/unrepresented results |
| `node_mapping` | Pattern vertex ID → host vertex ID for one complete witness |
| `edge_mapping` | `{pattern_edge,host_edge}` indices into original edge arrays |
| `lexical_renaming` | Consistent lexical correspondence used by the witness |
| `states_explored` / `state_budget` | Attempted vertex assignments and limit |
| `filters` | Necessary conditions checked and candidate domain sizes |
| `projection_report` | Explicit name replacements, information loss and retained constraints |

An empty pattern is unrepresented, not a successful retrieval. A search that
stops with unexplored assignments returns `budget_exhausted`, never `different`.
Proven necessary-condition failures may return `different` even at budget zero.
Only one witness is found; this function does not enumerate occurrences.

## Input labels and metadata

Graph schema:

```json
{
  "nodes": [{"id":"...","kind":"...","role":"part",
             "qualifiers":{},"claim_ids":[],"label":"optional name",
             "lexical_identity":{"namespace":"entity","value":"optional canonical symbol"},
             "provenance":{}}],
  "edges": [{"source":"...","target":"...","predicate":"...",
             "qualifiers":{},"claim_ids":[],"provenance":{}}]
}
```

`kind` is a nonempty type string. `role` uses Loom's fourteen universal roles,
or an empty unknown value. Node comparison preserves kind, role, all qualifiers,
optional label and optional lexical identity. Edge comparison preserves predicate
and every qualifier. Nested JSON values are exact; arrays retain order. Absent
qualifiers normalize to `{}`; an absent label differs from an explicitly empty
label. IDs, Claim IDs and provenance are metadata, excluded from matching labels.
They remain available in the original graph and witness.

Unknown node/edge fields are rejected, so a top-level `polarity` or `scope` cannot
quietly disappear. Put semantic constraints in qualifiers, and source metadata
in provenance. Top-level graph metadata describes a projection, not additional
matching constraints; scope must be represented on vertices/edges. Edge `id` is
optional metadata; array indices unambiguously distinguish parallel occurrences.

Node labels are not automatically alias-resolved. The `literal_semantic` goal
means exact *supplied semantic labels*, not that those labels have been verified
against reality. Missing references or unvalidated Claim/Assessment content
cannot be repaired by a match.

## Explicit cross-topic analogy

The `structural_analogy` goal requires an explicit projection object:

```python
projection = {
    "pattern": {"old_entity": {"lexical_identity": "entity"}},
    "host":    {"new_entity": {"lexical_identity": "entity"}}
}
result = match_subgraph(pattern, host, goal="structural_analogy",
                        label_projection=projection)
```

Per-node overrides may replace only:

- `label`;
- `symbol`, meaning `qualifiers.symbol`;
- `lexical_identity`, meaning `lexical_identity.value`.

Only fields actually present can be replaced. Kind, role, identity namespace,
edge predicates, direction and all other qualifiers remain exact. Each
replacement appears in the semantic-loss report. Many names may map to one
abstract comparison label; the projection does **not** supply a gold node mapping.

Crucially, name erasure alone cannot create false agreement. Matching also
requires a consistent **injective** original-name correspondence within each
channel/namespace: repeated `A` cannot become both `X` and `Y`, and distinct
`A`/`B` cannot collapse to one `X`. The channels are `label:<kind>`,
`symbol:<kind>` and `identity:<namespace>`. Repeated display labels therefore
carry equality constraints in this contract. If display text does not express
identity, keep it in provenance and supply appropriate shared identity vertices.

For graph-native evidence, shared entity/property/reference vertices and typed
binding edges preserve identity most directly. Scope identifiers from different
conversations should become shared scope vertices with membership/parent edges,
and explicitly renameable scope identities. A literal `qualifiers.scope` stays
exact; this matcher never drops it to obtain a higher score. Quantifier scope,
negation, modality, branch conditions and exceptions likewise require actual
labels/bindings in the input. The matcher cannot reconstruct information omitted
by its source graph.

`comparison_graph(...)` returns the prepared graph plus projection report. Use
its returned `graph` when computing exact-comparison fingerprints. The original
graphs are unchanged. This helper preserves projected labels; the matcher
additionally checks original-name consistency.

## Filtering, ranking and limits

```python
rank_candidates(pattern,
                [{"id":"candidate_id", "graph":host,
                  "label_projection": optional_full_projection}],
                goal="literal_semantic", label_projection=None,
                limit=10, rounds=2, verify_budget=0)
```

A candidate-specific projection is the full `{pattern,host}` object and replaces
the shared argument. Ranking returns `results`, `filtered_out`, `filter_survivors`
and `omitted_by_top_k`. Each result has role/relation containment, role/relation
cosine and directed WL cosine scores. They are not probabilities. By default
verification is `not_run`; a positive `verify_budget` performs bounded exact
matching on each returned candidate.

Hard filters use only necessary conditions for this contract:

1. Node-label multiplicities in the pattern cannot exceed the host.
2. Directed typed edge-label/endpoint-label multiplicities cannot exceed the host.
3. Each pattern vertex needs a host vertex with at least its labeled incoming
   and outgoing degree counts and self-loop counts.

These filters do not establish a witness. They deliberately keep false positives
for exact verification. Role/relation containment is often 1 for survivors because
those counts already form necessary filters; it is not a separate certainty score.

**WL is never a hard subgraph filter.** An extra host neighbor can change a WL
color even when an exact pattern is present. It remains a soft ranking signal,
so a top-k budget can still omit a genuine match. Such omissions are reported.
Whole-graph equality fingerprints and subgraph containment filters are different
contracts.

Indexes use `O(n+m)` graph storage plus canonical label bytes. Degree-domain
filtering compares compatible label buckets, so worst-case candidate construction
is still quadratic in pattern/host vertex counts. The remaining injective search
is exponential in the worst case. An explicit iterative search avoids recursion
depth failures; `state_budget` bounds assignment attempts, not wall-clock time.
Each attempt still checks edges against assigned vertices and lexical bindings.
Further indexing or native implementation requires separate measurement.

`topology_only_control(pattern,host)` is an explicitly **unsafe ablation**. It
discards types, roles, lexical names, all qualifiers and edge predicates, retaining
only directed multigraph wiring. Its result has `semantic_use_permitted:false`;
a match cannot justify semantic identity, analogy, inference or graph mutation.

## Verification

Run the author-owned checks:

```sh
python -m unittest discover -s loom/tools/structure -p 'test_graph_search.py' -v
```

The initial implementation passes 19 checks: containment with distractors,
non-induced extra edges, reversal/predicate differences, injectivity, parallel
edges, self-loops, budget unknowns, explicit analogy, repeated-name splitting and
collapsing, scope bindings, polarity/modality/quantifier preservation, rejected
unrepresented fields, WL-safe retrieval, top-k omissions, unsafe topology control,
input immutability and absent/empty labels. Independent validation uses separate
fixtures and an exhaustive small-graph oracle; these developer checks alone are
not a semantic-quality benchmark.
