# Catalog semantic coverage gap — 2026-09-28

The remaining 13/45 recall failure is a **relevance-evidence bottleneck**, not a
missing-file, retrieval-count or source-access failure. All 32 missed relevant
conversations are scored, but the present lexical features leave every one in
the **irrelevant** band. The catalog does not invoke the semantic model or
compare argument graphs. Enabling semantic triage only for its existing
candidate band would therefore reach **none of these 32 misses**.

This supplements the preserved historical `CATALOG_RECALL_DIAGNOSIS_2026-09-28.md`
with a fresh local diagnostic. This is a diagnosis of the frozen synthetic development case. No policy,
threshold, alias list, source fixture or production code was changed. It does
not establish how much a future semantic method will recover.

## Observed evidence

After the full local verification, one focused rerun used the existing
`LOOM_CATALOG_EVAL_VERBOSE=1` diagnostic in `test_catalog_eval.cpp`. It reproduced
13/45 recall and printed all 32 missed conversations' feature vectors. No full
suite or model request was rerun.

| Diagnostic | Result for the 32 misses |
|---|---|
| Final label / selection reason | 32 `irrelevant`; 32 `decided_by=score` |
| Score range | 0.0544–0.2512; every score below candidate threshold 0.35 |
| Exact source available | 32/32 |
| Alias, principle, title, version, code and project-member features | All zero |
| Negative-context/trap feature | Zero for all 32 |
| Nonzero BM25 self / philosophy | 25/32 and 32/32 |
| Nonzero propagated link support | 25/32 |
| Units passing the current own-evidence guard | 32/32 |

For every missed unit, its printed final score is exactly reproduced, to the
stored four decimal places, by:

```text
sigmoid(-3 + 1.4 * bm25_self + bm25_phil + link)
```

The largest positive sum in this expression is 1.907812310716904; with bias -3
it still produces only 0.2512. Their rejection is therefore explained by the
available feature values and current transform. It is not caused by noise-trap
penalties, a selection override or the tiny-candidate rule. Lowering thresholds
to pass these examples would not demonstrate synonym-invariant understanding.

The scan reports 68 units, 93,279 source bytes, zero warnings and zero unavailable
identity sources. The diagnostic scan hash is
`8969651f368c2e4019c23291ed5a580a3d42fa263a9eeef58b126f9c265bae51`.
The source population is much smaller than the 4 MiB per-unit sketch prefix cap;
prefix truncation does not explain this result.

## What the code actually does

1. `Catalog::score` in `loom/src/catalog/score.cpp` loads **every** catalog unit
   and sketch with `SELECT body, sketch FROM loom_cat_units ORDER BY id`. There
   is no top-N SQL filter before scoring. It re-reads each unit for alias and
   principle matching, then scores normalized word keys with BM25.
2. `set_base_features` gives `code_evidence` only when an alias/principle hit
   already exists; version evidence also requires a nearby accepted alias.
   These are coupled lexical channels, not independent evidence of argument
   structure. Separate identity/principle diagnostic counts were added earlier,
   but the weighted legacy combined channels remain unchanged.
3. Vocabulary expansion draws top words only from the already-relevant set,
   requiring corpus lift and at least three relevant-unit occurrences under
   the bundled policy. The current run adds one term. It cannot itself discover
   a structurally equivalent idea expressed with unrelated vocabulary.
4. Links use provider project membership, lexical MinHash, shared rare words
   and temporal proximity. `MinHash::build` in `sketch.cpp` hashes word
   **three-shingles**; the header's five-shingle comment is stale. This is not
   a graph-role or inference-pattern comparison. The pass uses one damped
   maximum-neighbor contribution, not semantic propagation over thought atoms.
5. The link guard requires alias or BM25 evidence in the receiving unit. That
   is a general barrier for lexically silent units, but **not the explanation
   for this particular set**: all 32 missed units pass it and 25 receive support.
6. `ScoreConfig.llm` and `verify_max_units` in `catalog.h` are parsed and
   serialized in `catalog.cpp`, but `Catalog::score` never consumes them to
   invoke a model or cap verification. The configuration's candidate-triage
   comment describes an unrealized path. Merely setting `llm=auto` here does
   not add semantic classification.
7. `Catalog::select` applies owner overrides, then policy rules, then the
   relevant-label default. All 32 misses reach the default. A candidate-only
   semantic attachment would inherit their exclusion.

Code-grounded pipeline consequence: `catalog::run_stage` calls scan, profile,
score, select and import, then returns the **actual imported unit IDs**.
`loom/src/extract/stage.cpp` reads those catalog input IDs and invokes
`propose_semantics` only after observations have been extracted. The existing
semantic graph extraction is therefore downstream of catalog scope selection
in this pipeline. Full-import mode and configured import expansion can change
that scope, but do not constitute a semantic catalog scorer. Improving only
the downstream extractor cannot discover a unit that never reaches it.

## Generic integration seam to explore

The useful seam is **before final catalog selection/import**, between immutable
source access and the relevance decision. A semantic proposer should be able to
inspect source-located conversation windows from every score band under an
explicit budget; a lexical label must not determine eligibility. Use the same
canonical graph's observations and candidate structures with provenance, not
a second authoritative representation of ideas.

Proposed flow, not an implemented result:

1. Produce branch-aware, source-located windows with explicit coverage. Spread
   an initial budget across beginnings, endings and interior windows, then
   refine around detected topic changes and unresolved context. Record unvisited
   spans as unknown; do not convert budget omissions into irrelevant labels.
2. Extract candidate operations, roles, quantifiers, conditions, scope and
   references. Retain uncertainty and exact source anchors. Resolve references
   against existing graph context while preserving conversation/branch identity.
3. Retrieve possible structural matches through derived graph projections and
   compare relation roles and polarity. A typed judge such as Jev can evaluate
   specific match hypotheses, provided candidate discovery also reaches units
   with weak lexical evidence. Jev pair classification is not graph extraction.
4. Feed separated structural-match, topic-continuation and project-context
   evidence into catalog review/selection. Keep lexical scores as an additional
   signal and omission diagnostic. Respect owner overrides; no automatic graph
   merge or promotion of inferred claims to observations.
5. Preserve the source/proposal/decision chain and re-evaluate affected evidence
   when graph context changes. Cache by source bytes, span, model/schema and
   relevant graph-context version so a changed interpretation is traceable.

The budget scheduler and graph projection are the generic extension points;
adding development-fixture names or patching the word list would not address
the demonstrated gap. Exact topology alone is also insufficient for project
membership: common structures can occur across unrelated projects, so structural
analogy, topic identity and relevance must remain distinct evidence dimensions.

## What remains a hypothesis

Semantic windows and graph comparison may recover these misses, but this audit
did not measure them. It also did not assign each miss a finer cause such as
paraphrase, omitted project name, anaphora or topic drift. The observed absence
of alias hits is not proof that a particular named cause applies to every text.
Future experiments should compare coverage-first semantic proposals against
candidate-band-only proposals, retain all original first responses, and measure
recall together with false inclusions, topic boundaries, source coverage and
cost. Use fresh synonym/domain/foil cases rather than fitting these 32 examples;
do not open the private holdout during development.

Diagnostic command (existing native binary; expected assertion failure):

```sh
LOOM_CATALOG_EVAL_VERBOSE=1 /workspace/scratch/edcd10c4764c/native-build/loom_tests \
  --source-file='*/test_catalog_eval.cpp' --no-intro=true
```

Current native executable SHA-256: `792985e25bb761cd5fee6841cf41f114dac4947750ed618f3a7d66c000f6f26c`.

Scratch evidence: `/workspace/scratch/edcd10c4764c/catalog-recall-diagnostic.log`;
SHA-256 `aaa41b3bbaaa09eb5c7ba6931bb704f671ea7f494a1bfab1af631d6cb3c84657`.
This audit made no provider requests, no GitHub Actions and no production edits.
