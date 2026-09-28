# Graph-native continuation

The owner clarified on 2026-09-28:

> struktura argumentu, myśli, idei, to wszystko z założenia ma być reprezentowane w grafie więc szukanie powiązań to będzie szukanie podobnych struktur w grafie.

They requested parallel experiments, an evidence-based combined flow, autonomous
iteration and continued GitHub checkpoints. This round tests that simplification
against the existing model and actual stored outputs, rather than introducing a
second authoritative thought store.

## Working hypothesis

Claim + Assessment remains the authoritative knowledge representation. Thought
structures are queryable arrangements of its assertions, entities, premises,
scope, counterevidence and consequences. Atomic operations can describe typed
relations or reusable graph patterns; they need not form a disconnected ontology.
Logical ASTs from the previous round remain limited experimental projections.

Different projections still serve different tasks. A display graph, a literal
identity graph, a role/structure projection and a deliberately lossy topology
control retain different information. Every projection must identify its source
claims, missing semantics, abstraction assumptions and removed attributes.
Same topology is not same meaning; a structural match is not proof validity.

## Parallel experiments

1. Audit actual Claim/Assessment serializers, store schema, extractors and graph
   views. Distinguish stored expressive capacity from validated semantics and
   from information the extractor really fills.
2. Project an actual native knowledge run into a source-linked incidence graph.
   The SQLite reader uses an explicit run and a consistent read-only transaction;
   no live database/WAL pair is copied as a purported snapshot.
3. Compare necessary-condition filters, lexical/role/WL ranking and bounded exact
   directed multigraph containment. Preserve injective bindings, direction,
   parallel edges and scope. Exhausted or truncated searches remain unknown.
4. Enumerate bounded rooted/edge neighborhoods, bucket by invariant signatures,
   then verify motif equivalence. Count source groups/domains separately from
   repeated occurrences and specificity. No candidate motif becomes a fact.
5. Author new independent data and a separate exhaustive small-graph oracle,
   freeze before scoring, and measure each retrieval goal separately. Method
   authors do not receive validation examples or labels.
6. Combine the measured stages into an executable graph flow and inspect actual
   native outputs. Iterate on reproducible errors and performance limits.

## Baselines and constraints

- Previous full native CTest: 62/63; unresolved catalog recall 13/45 against 0.55.
- Previous broad text grammar coverage on three architecture files: zero.
- Previous fresh scoped-source comparison: 2/6 to 6/6 exact contrast ordering,
  conditional on supplied scope and supported syntax. This is not a general
  parsing or reasoning result.
- Production's universal roles, Operator contract and Claim/Assessment invariants
  are unchanged. Generic JSON capacity alone is not a checked logical language.
- No hidden holdout key access, threshold weakening or copied-source support
  inflation. Source observations and owner history remain intact.
- Report a flow justified within measured conditions; do not call it universally
  optimal or all thought fully represented.

Primary technical references checked this round:

- Shervashidze et al., *Weisfeiler-Lehman Graph Kernels*, JMLR 2011:
  https://www.jmlr.org/papers/v12/shervashidze11a.html
  — efficient neighborhood features as a comparison/indexing component.
- Sun and Luo, *In-Memory Subgraph Matching: An In-depth Study*, authors' paper
  and implementation: https://github.com/RapidsAtHKUST/SubgraphMatching
  — separation and interaction of candidate filtering, query order and exact
  enumeration motivates measuring both components and the combined flow.

These sources motivate experimental controls; their published results are not
claimed as performance of this repository.

## Owner steering: configurable semantic model

The owner recalled the separate inexpensive semantic/structural model from the
Python application. Audit confirms that `semantic_model` is configured in both
frontends and used by the live/background legacy graph paths. Its prompt extracts
entities, topics, simple relations, a summary and sentiment from a single message
(currently only its first 3000 characters). It does not request argument scopes,
premise bindings or conversation-level generalizations.

More importantly, the native knowledge pipeline accepts `llm: "auto"` but its
extract stage does not consume that setting or call a model. Legacy `nodes/links`
and canonical `Claim + Assessment` are distinct persistence paths. Model-free
research scores therefore cannot establish whether the intended cheap-model
workflow works. Completing a checked, source-linked model path is now part of
this round; changing the prompt alone would not close the integration gap.

An actual native baseline over the three architecture documents completed with
LLM off and seed priors disabled: 1002 observations, 60 entities, 252 extracted
claims, and 2182 claims after generalization (including 1861 absence placeholders).
These are output counts, not correctness scores. Graph experiments must expose
placeholder/template repetition separately from observed argument structure.
