# Graph methods panel v1: preregistration

Written on 2026-09-30 before any parser, Jev, LLM or vector predictions on
these new cases. No earlier Jev structure/pair fixture is reused. Authorship
is independent of method development; fixture integrity tests are mechanism
checks and must never be reported as model quality.

## Fixed design and family-held-out split

48 newly authored short conversations, eight diagnostic families, six cases
per family (three PL, three EN). **Whole families** are assigned before results:

| Split | Families | Cases |
|---|---|---:|
| development | attribution, negation_unknown, correction_known_at, paraphrase | 24 |
| sealed validation | reversed_implication, relation_vs_topic, omitted_relation, multihop_alternatives | 24 |

No family template crosses the split. The conversations within each family
are controlled variations, not 48 independent draws from a natural-text
distribution. The case-level unit of analysis is the conversation; queries
inside it are correlated. Family names and gold are absent from method inputs.
Validation inputs **and** gold are sealed until the implementations, adapters,
prompt recipes, vector configuration, thresholds and budget are frozen. The
author knows its own labels; implementers must not inspect them before release.

## Target and epistemic semantics

The target is a bounded graph of **what sources explicitly assert or deny**,
plus separately marked consequences of supplied formal implication rules.
It is not a world-truth graph. Every source assertion carries speaker or
quoted-speaker attribution, full evidence spans, turn source identity and
known_at. Content truth is `unverified`, including confidently worded claims.

- `supported`: the requested relation is explicitly asserted by the requested
  attributed speaker within the query's temporal view.
- `refuted`: that speaker explicitly denies that relation, or explicitly
  withdraws/corrects that earlier relation in the queried latest view.
- `unknown`: neither positive nor negative source support exists in that
  scope. Missing relation, similar topic or high embedding similarity is not
  evidence that the relation is false.
- Support and refutation must be evaluated per attribution, relation direction,
  scope and known_at cutoff. A later correction cannot leak into an earlier
  prefix; raw earlier assertions are retained. Withdrawal is a source event,
  not permission to erase history.
- A source may state a conditional whose operand is negated. Negating an
  operand is distinct from denying the conditional itself.
- Explicitly quoted assertions remain attributable observations; the reporting
  speaker does not automatically endorse them.
- A formal implication path is an `inferred` result with its premise IDs and
  alternative paths. It is not an explicitly observed direct edge. Failure
  to find a path is not a refutation.

Operation strings in this research fixture are labels, not additions to the
production conceptual closed sets. A production adapter needs deliberate
mapping and preserves evidence classes; model output never becomes fact by
score or confidence alone.

## Separate comparable tracks

1. **Source-only extraction:** turns only, no supplied proposition inventory
   or candidate edges. A free LLM proposer may run here; report generated
   node/edge and provenance errors separately. This track requires a frozen
   endpoint-matching policy and is not directly comparable to guided Jev.
2. **Assisted graph extraction:** identical turns and node inventory for every
   method; omit judgment queries from the extraction prompt. Predict typed,
   directed, attributed source assertion/denial records with evidence and
   known_at. Report this honestly as assisted extraction, not free extraction.
3. **Supplied-edge judgment:** the same source prefix, node inventory, candidate
   edge, attribution, query scope and temporal cutoff for every method. Jev
   gets simple support/refutation questions, an LLM may use a structured
   ternary answer, and vectors may rank evidence. Scores address this judgment
   task, never standalone graph extraction quality.
4. **Formal path support:** only queries explicitly marked `formal_implication`
   may use implication composition. Preserve minimal support paths and mark
   the conclusion `inferred`. Assess edge extraction and path reasoning as
   separate stages; an oracle-input reasoning score is not end-to-end quality.

Variants must actually vary the instrument or representation: source-bound
binary Jev questions, graph-bound binary Jev questions, an LLM extractor/judge,
and lexical/vector retrieval. Repeating identical prompts is a stability or
sampling experiment, not a comparison of methods. Disagreements are diagnostic;
one retrieval channel cannot veto another without an explicit policy.

## Baselines and recipes fixed before validation

- Empty-graph extraction baseline: no model, no edges. Preserve zero recall
  and undefined precision (0/0), not 100% precision.
- Always-unknown judgment baseline: no model. Report supported and refuted
  recall separately; do not hide them behind class-imbalanced accuracy.
- Optional lexical candidate/ranking baseline and local vector ranking must
  log exact tokenizer/encoder, representation, hash, normalization and tie
  policy. Ranking cannot itself promote an edge into `supported` or `refuted`.
- If a thresholded vector decision variant is tested, freeze thresholds on
  development and give it its own recipe/task profile. Compare it to retrieval
  ranking and the unknown baseline separately.
- Prompts/recipes supplied beside this protocol are instrument definitions,
  **not tested quality results**. Live Jev/LLM work requires separately recorded
  explicit authorization and a hard USD cap. This fixture has authorized no
  API expenditure and executes no model calls.

## Scoring preregistration

Preserve first responses, request/recipe hashes, provider/model/version, source
prefix hash, runtime and request cost. Record invalid, refusal and unavailable
responses; none count as correct. Parse and compare using a frozen adapter.

- Assisted edges: primary strict precision/recall, TP/FP/FN with direction,
  relation, polarity, attribution, known_at and exact evidence binding. Report
  family/operation-only and span-only metrics separately. Extra duplicates are
  FP; missing outputs retain their gold-positive denominators.
- Judgments: 3×3 confusion matrix, per-class precision/recall, macro averages,
  supported-versus-refuted confusion and unknown-versus-refuted confusion.
  Abstention/invalid coverage is separate. Accuracy is secondary and retains
  every denominator.
- Temporal pairs: earlier/later consistency and future-leakage rate with the
  count of eligible prefix queries. Stability is not correctness.
- Formal paths: direct-edge-vs-inferred confusion; premise precision/recall;
  preservation of alternative minimal paths. A correct yes/no answer without
  its required support path is not a strict path-extraction TP.
- Retrieval: rank/recall of explicitly annotated evidence spans and false
  relevance on same-topic/different-relation cases, conditional on the candidate
  inventory. Do not label this semantic graph truth accuracy.
- Report split and family separately. No aggregate hides a failed family.
  If confidence intervals are used, resample whole conversations, not individual
  correlated queries. Twenty-four validation conversations are a diagnostic
  panel, not a population-quality estimate.

## Release and decisions

Freeze fixture hashes first. Development may inform recipes and algorithms.
Before any validation access, freeze code, scoring adapter, request recipes,
model/encoder configurations and budget; save a release record. Execute each
declared finalist once on validation, preserving first results. Any later
changes reuse exposed validation as development and require a new independent
round for a fresh quality claim.

Keep / revert / investigate decisions compare to the fixed baselines, measured
per-class errors and regression gates. Retain competing methods while evidence
does not resolve them. No fabricated model quality, production readiness or
natural-text recall follows from fixture validity or synthetic graph examples.
