# Formal positive-implication paths: protocol before validation

2026-09-30. This separate arm is written before validation access. Its author
has inspected DEV inputs/gold only; DEV has no formal-path gold examples. Tiny
authored graphs and exhaustive small-graph checks test the mechanism, not any
model's quality. No API, credential, model response or authoritative graph
mutation is required. Validation inputs and gold remain sealed.

## Instrument and data policy

`loom/tools/structure/graph_formal_paths.py` accepts supplied assertions,
supplied supersession events and one `formal_implication`/`implies` query.
`graph_formal_paths_policy.json` declares positive-implies transitivity,
same-requested-speaker attribution, active-at-cutoff filtering and explicit
search bounds. It has no lexical, inventory-truth, embedding or confidence
gate. Proposition IDs are opaque: a negated proposition is an ordinary node
ID, not permission to negate another node or apply contraposition.

Only positive `implies` source assertions known by as_of and not explicitly
superseded by then are premise edges. A future correction does not change an
earlier view. Raw earlier assertions and every applicable event's provenance
remain in input/output trace. A new source assertion replacing a correction
may reinstate support under a new ID; an old superseded ID is never silently
reactivated. Cross-speaker supersession is invalid. Explicit negatives do not
derive complements and do not veto positives without a declared withdrawal
event. Co-present opposite-polarity pairs are reported as diagnostics: the
result is conditional formal support, not logical or world-truth endorsement.

## Complete alternative support, not shortest-path selection

Enumerate all simple directed paths, preserving distinct premise assertion IDs
including parallel assertions. Every such path is inclusion-minimal by its
edge-ID support set; a directed cycle adds removable premises. A longer path
whose support is incomparable to a shorter path remains an alternative. Never
select just the first or shortest witness. No zero-edge identity axiom is
declared; source==target returns unknown rather than inventing reflexivity.
This arm is complete only for distinct source/target under the declared
simple-path restriction. Even an explicitly supplied self-loop does not make
a reflexive query supported by this arm; report that declared boundary instead
of claiming general reflexive reachability or formal-logic completeness.

The three bounds are maximum path length, number of paths and search states.
If a bound prevents exhaustive enumeration, return unavailable with no paths
presented as exhaustive, no supported/unknown label, and an incomplete flag.
No-path after complete search means unknown, never refuted.

Each retained path includes ordered premise IDs, ordered node IDs, copies of
every premise's evidence/attribution/known_at, and path known_at equal to the
latest premise time. The conclusion known_at is the earliest time among its
currently active complete witnesses; it is not a claim that the model or
runtime actually computed it then. The as_of view is separate. Timestamps are
compared as instants, preserving an original source timestamp string.

Every supported result is `basis_class: inferred`, `content_truth: unverified`.
There is no observed direct-edge output, truth score or persistence action.
The solver preserves supplied provenance; it cannot independently prove the
premise quote's adequacy or revalidate coordinates without its raw source.

## Evaluation boundary

Run oracle-annotation premise graphs and LLM-predicted premise graphs as
separate named modes. Oracle reasoning correctness is not end-to-end extraction
quality. Report path premise TP/FP/FN, preservation of all minimal alternatives,
direct-vs-inferred distinction, source-attribution and temporal correctness,
as well as invalid/unavailable cases in every planned denominator. Compare to
the no-path/unknown baseline. Negative labels never arise just because search
finds no support. If the source claim is semantically ambiguous, record that
annotation uncertainty separately rather than promoting a proof to fact.

Freeze solver, policy, tests and this protocol before any validation release.
Any post-release algorithm or recipe change requires a new version and keeps
the first results. A freeze manifest will record exact SHA-256 values.

## Preserved mechanism verification

The independently authored suite passes 32/32 methods, including 128 tiny
graphs against an independent permutation oracle and a 1100-edge chain.
Initial runs failed on a state-budget counter, Python recursion and malformed
reference/mode types. `FORMAL_PATHS_FIRST_MECHANISM_RESULTS.json` preserves the
first failure counts. Repairs preceded validation release: iterator-frame DFS,
guard-before-increment bounds and explicit type checks. This is mechanism
verification, not a measured model result or independent natural-text quality.
