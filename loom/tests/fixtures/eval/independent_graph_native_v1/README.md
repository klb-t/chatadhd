# Independent graph-native benchmark v1

This is a frozen, task-conditioned benchmark of explicitly represented graphs.
It does not label the owner's historical conversations, validate natural-language
extraction, or establish the truth of a Claim. The method authors received only
API/schema coordination; the evaluator did not read their test examples, the
existing `synthetic_dev` examples, or the real holdout key. Development and fresh
validation families, labels, and abstraction maps were fixed before running the
methods. No method or threshold was adjusted against these results.

## Frozen inputs and methods

| Artifact | SHA256 |
| --- | --- |
| `cases.json`, self-excluding canonical payload | `a23645a6eb8ffcd3b1ab231898d72b96f9c0ff9ff29a09649fde30eaf3ebe37c` |
| `core_claim_cases.json`, self-excluding canonical payload | `719b19fb140ab83ce58bbe2230f69bc257a68a5eb9c3ca974710f11e9b11e2a6` |
| Frozen `graph_search.py` | `798ea9ddde1168c5bf67e48c4538446db211ef815f57eb0984fab16842ed2d6b` |
| Frozen `core_projection.py` | `42e864af2046d036f31b1db30e1199a66d7a6b335d24e14090b5a9459ee8aba6` |

Canonical payload hashing removes only the root `frozen_sha256`, then serializes
sorted keys, compact separators, UTF-8, no final newline, and no nonfinite JSON
numbers. These are different from ordinary hashes of formatted files. The
method sources were copied before execution. `initial_search_report.json`
records the evaluator hash at its run; later additions to the independent core
audit do not change its oracle labels or measurement. The core report records
its own evaluator hash.

## Tasks and labels

There are 12 graph families, six development and six fresh validation, with 76
candidate graphs. Each family supplies three query tasks, giving 36 queries and
228 task-candidate judgments. Each split contains 38 judgments per task.

| Task | Required relation | Positives per split |
| --- | --- | ---: |
| `semantic_identity` | Full literal attributed graph isomorphism | 6 |
| `template_containment` | Literal directed non-induced injective subgraph | 12 |
| `structural_analogy` | The same containment after supplied lexical abstraction, retaining equality and distinctness | 18 |

The semantic-identity label concerns the represented graph's exact attributes;
it is not a claim that different wording cannot have the same meaning. A full
node/edge cardinality adapter distinguishes this task from the method's native
containment API. Extra host relations are allowed for both containment tasks.
Distinct parallel edges require distinct host edges.

Candidate families include a literal match, a match embedded in noisy context,
a different-domain match embedded in noise, same-word direction and qualifier
traps, and a missing relation or multiplicity trap. Additional source cases
distinguish merged source vertices and distinct vertices with a repeated
physical-source identity. Across the families the constraints include polarity,
scope, branch guards, quotation, uncertainty, shared bindings, feedback, and
repeated versus independent sources.

Abstraction is supplied, not learned: node lexical labels are mapped to explicit
kind/role classes. The oracle still requires a consistent injective renaming of
original lexical identities within each namespace. The same identifier cannot
split into unrelated identifiers, and distinct identifiers cannot merge. Roles,
typed directed edges, qualifiers, and edge multiplicity remain constraints.

## Independent truth and metrics

`graph_native_eval.py` computes truth by exhaustively enumerating all small node
injections, comparing exact attributes and directed edge-multiset capacities,
then checking original lexical binding constraints. It does not import matcher
code for truth or witness verification. Its bounds are six pattern nodes and
nine host nodes. The predeclared candidate labels agree with this oracle for all
228 judgments, of which 72 are positive.

Candidate-filter recall counts all oracle positives, including filtered-out
ones. Average precision, reciprocal rank, precision/recall at K, direct matching
status, witness validity, and state-budget limits are reported separately. A
budget-exhausted answer is unknown, never a proven negative. Ranking is called
three times per query for a determinism check and descriptive local latency.
The full-identity adapter is explicitly included in its filtering measurement.

The verifier checks both node and original edge-index witnesses. It rejects
duplicate edge use, non-integer indices (including booleans), unknown semantic
fields, and unknown task goals. Invalid or missing input has separate abstention
probes. Author-reported scores or witness labels are not used as gold truth.

## Native Claim preservation extension

`core_claim_cases.json` contains 16 curated exports in the actual native Claim
format: eight development and eight validation. See `CORE_CLAIM_PROTOCOL.md` for
native schema, exact-byte and identity construction. Fourteen raw source texts
are independently available outside the projector input. There are 20 exported
Observations, 22 Support entries, and 18 predeclared pairwise relationships.

The audit independently measures an untouched source copy and selected
restrictions reconstructed from actual graph incidence. It checks subject,
predicate and object identities; Claim-to-scope and scope-to-entity incidence;
the exact declared Claim qualifiers and Assessment attributes; distinct Support
ordinals; and each Support's exact Observation/source linkage. Equal support
counts alone cannot pass the identity check. Semantic-mode quotation text is
checked per Support. Each mode receives a fresh copy and is audited against an
immutable pre-call export. Pairwise relationship success requires both side
audits to succeed.

This audit does not establish total invertibility of every native field or check
all entity type/parent hierarchy incidence. Quotation, negation and modality are
already explicit in `qualifiers.extra`; interpreting those features from prose
is outside the experiment. Unique source count is a provenance count, not a
general statistical-independence or confidence estimate.

During evaluator review, count-only support checks were strengthened to exact
per-ordinal links, scope membership was checked, and pre-call gold isolation and
relationship gates were added. The same frozen method and unchanged data were
rerun: aggregate results were unchanged. Three dedicated metric guards cover
copied-source-only graphs, wrong source identities with unchanged counts, and
missing scope membership. These are evaluator repairs, not method tuning.

## Reproduction

From the repository root, using the recorded method versions:

```sh
python loom/tools/eval/graph_native_eval.py validate
python loom/tools/eval/graph_native_eval.py oracle --output /tmp/graph-oracle.json
python loom/tools/eval/graph_native_eval.py run --implementation loom/tools/structure/graph_search.py --output /tmp/graph-search-report.json
python loom/tools/eval/graph_native_eval.py --fixture loom/tests/fixtures/eval/independent_graph_native_v1/core_claim_cases.json run-core --implementation loom/tools/structure/core_projection.py --output /tmp/core-projection-report.json
python -m unittest discover -s loom/tools/eval -p test_graph_native_eval.py -v
```

Later method hashes must be reported as separate versions. Keep the frozen data
unchanged; do not turn validation outcomes into development examples. The saved
JSON reports retain query-level details, failures, timings, and witnesses.
