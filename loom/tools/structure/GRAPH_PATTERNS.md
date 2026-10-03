# Recurring graph neighborhoods

`graph_patterns.py` is a read-only candidate generator over supplied generic
graph views. It discovers repeated rooted neighborhoods without a separate query
pattern or argument ontology. Every merge needs an exact graph-isomorphism witness;
a cheap invariant fingerprint only chooses comparisons. No candidate becomes a
Claim, Assessment, validated fact or persistent graph update.

## Interface

```python
from graph_patterns import discover_patterns

report = discover_patterns(
    records,
    radii=(1, 2), roots=("node", "edge"),
    max_enumerations=256, max_nodes=12,
    min_nodes=2, min_edges=1, wl_rounds=2,
    alignment_budget=10000, max_verifications=10000,
    min_occurrences=2,
)
```

Each record has a unique nonempty `id`, a `structure: {nodes, edges}`, and optional
nonempty `source_group` and `domain` strings. Missing group/domain remains unknown;
record identity is not substituted for independent support. Graph node/edge fields
are checked by `graph_search.comparison_graph`. All node kinds, universal roles,
labels, lexical identities, edge predicates, directions, parallel edges and nested
qualifiers participate in comparison. Source Claim IDs and provenance are evidence
metadata, excluded from equality. Unknown node/edge semantic fields fail validation
rather than silently disappearing.

The CLI accepts an array of records, or `{records, parameters}`:

```bash
python loom/tools/structure/graph_patterns.py input.json
python -m unittest discover -s loom/tools/structure -p test_graph_patterns.py -v
```

The CLI emits a JSON report to stdout and never writes a graph or database.

## Enumeration and verification

For each requested node or edge root and radius, the generator traverses adjacency
in either direction to select nodes. It then retains every directed edge between
the selected nodes. Direction is never erased from matching. Repeated radii that
select the same root and neighborhood share one occurrence and retain all radii.
Root identity is a transient comparison annotation, not a new domain role. Node
and edge roots remain distinct; a self-loop preserves both endpoint positions.

The directed typed 1-WL fingerprint includes successive label multisets and edge
label multiplicities. It is invariant to node IDs and array ordering, not a
canonical graph labeling or proof of equality. A bucket can contain multiple
nonisomorphic motifs. `graph_search.match_subgraph` checks every proposed merge;
equal node/edge counts plus an injective directed multigraph match establish
isomorphism of the represented rooted neighborhoods. An exhausted match remains
unknown and cannot merge candidates. Candidate IDs are report-local: their bucket
suffix can change with comparison order. The full `fingerprint` is invariant for
fixed representation, root and WL settings, but may collide.

This comparison uses literal semantic labels of the supplied view. More abstract
motifs require an explicit upstream projection that documents removed information.
The generator does not erase names, scope, polarity or lexical identity to manufacture
recurrence. Homonyms require distinct supplied lexical identities; it cannot infer
missing senses. Local isomorphism is not source-wide equivalence: every occurrence
retains outgoing/incoming boundary edges and reports
`scope_completeness: not_established_by_neighborhood`. Scope constraints absent
from the graph cannot be recovered, and constraints beyond the selected neighborhood
do not enter its equality test.

## Evidence, support and detail

Each returned candidate retains the representative graph/root, every matched
occurrence, source Claim IDs, source node/edge provenance and exact node/edge
mappings. Edge witnesses include original source graph array indices so parallel
edges remain auditable. Boundary edges retain their original attributes and Claim
IDs separately from the motif's included-Claim union.

`support` distinguishes occurrence count, record count, declared source groups and
observed domains. Repeated roots, overlapping neighborhoods or copied records in
one source group contribute one `independent_support`, regardless of occurrence
count. This is an explicit caller assertion about source dependence, not verified
statistical independence. Ungrouped records are listed and add zero known support.
Different domains do not establish universality; `domain_source_groups` exposes
when several domains rely on the same source group. `min_occurrences` filters on
recurrence, not on independent support, so a candidate may have one or zero known
independent groups.

`specificity` reports retained node/edge/type/qualifier/lexical detail separately.
These counts do not change when occurrences or domains are added. They are neither
a probability nor an inverse measure of universality. Candidates carry null
confidence, `persistable_claim: false` and `validation_status: not_promoted`.

## Bounds and incompleteness

`max_enumerations` limits attempted root/radius neighborhoods, including oversized
and below-minimum cases. `max_nodes` omits entire oversized neighborhoods instead
of arbitrarily truncating them. Each verification has its own search-state bound;
`max_verifications` bounds the total calls. Coverage reports omitted enumeration,
oversized neighborhoods and unresolved/skipped comparisons. Missing recurrence
under these bounds is unknown, not evidence of absence. Minimum size filters are
deliberate and counted separately.

These bounds are not a total wall-clock guarantee: graph validation, record copying,
seed construction and neighborhood adjacency inspect supplied input. The experiment
enumerates radius neighborhoods, not all connected subgraphs, and has no learned
selection/ranking objective. The report's input hash identifies exact submitted
records; it is not a semantic canonical hash. Matching correctness is conditional
on the supplied graph representation and shared matcher.

## Countable author experiment

The author-owned `collision_experiment()` fixture constructs a triangular prism
and the complete bipartite graph K3,3. Each has six uniform nodes and nine undirected
adjacencies represented by eighteen directed edges. Node-root radius-two views
cover each whole graph. One-round rooted WL fingerprints collide, while exact
matching separates the graphs.

| Quantity | Measured result |
|---|---:|
| Distinct rooted occurrences | 12 |
| Cheap recurring fingerprint buckets | 1 |
| Verified recurring motifs | 2 |
| Verified merges | 10 |
| Verified nonmatches | 6 |
| Known independent source groups per verified motif | 1 |
| Unresolved comparisons | 0 |

Each graph's six symmetric roots are recurrence within one declared source. The
cheap bucket would mix the two sources; exact verification prevents this false
motif merge. These counts test a mechanism, not semantic precision or independently
measured natural-language quality. They are asserted directly by the test suite.

The 13 author checks cover ID/order invariance, provenance and complete Claim-ID
retention, duplicate-radius/copy support, unknown support, direction/polarity/scope,
boundary evidence, homonym identity, detail versus frequency, the WL collision,
enumeration/node budgets, exhausted verification, parallel edges, unknown semantic
fields and nonmutation. Independent evaluation is maintained separately; its
fixtures and labels were not read by the method author.

## Matcher peer review

The motif author also reviewed the separate frozen `graph_search.py` mechanism
(SHA256 `798ea9ddde1168c5bf67e48c4538446db211ef815f57eb0984fab16842ed2d6b`).
Read-only generated probes against a separately written exhaustive permutation
routine found no disagreements in 2,000 literal cases (843 matches, 1,157
nonmatches), 1,000 simultaneous label/identity-renaming cases (290 matches, 710
nonmatches), or 4,000 bounded checks of those analogy cases at budgets 0–3 (713
matches, 2,499 disproved matches, 788 unknowns). These were review probes, not
independent held-out evaluation or a proof for arbitrary graphs. Random seeds were
92846 and 92847; the independent evaluator maintains the reproducible oracle suite.

One concrete representation limit was confirmed with the matcher author: an
analogy requires consistent renaming of both display labels and explicit lexical
identities. Distinct homonym identities do not override repeated display labels.
For example, take two `kind: entity` nodes connected by `related`:

| Side | Node labels | `lexical_identity` values in namespace `sense` |
|---|---|---|
| Pattern | bank, bank | financial, river |
| Host | fund, shore | fund, shore |

Project every label and identity value to `entity` with the explicit analogy
projection. Necessary filters pass, but exact matching returns `different`: one
original display label cannot rename to both fund and shore. Omitting display
labels from these otherwise identical inputs yields `matched` with separate
identity bindings. This is the current conservative simultaneous-renaming contract,
not a filter error. A future goal that treats a display label as nonauthoritative
must state that change explicitly; the frozen matcher and literal motif discovery
were left unchanged.
