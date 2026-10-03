# Graph panel DEV live-arm protocol

Frozen before root-coordinated paid calls. The fixture author/scorer has not
seen any response for this 24-conversation panel. Validation remains sealed.

## Arms and identical inputs

- Assisted extraction: 24 GPT requests, turns and supplied proposition
  inventory only. No candidate questions, labels or gold enter the request.
- Supplied-edge judgment: 96 query requests per instrument, GPT ternary versus
  Jev support/refute noul pairs. Each body contains exactly one query and its
  physically truncated source prefix. It contains no later turn and no other
  query whose future context could leak into an earlier question.
- Jev runs in two immutable batches of 48 requests through the existing
  bounded Jev runner; each request has exactly two typed noul questions.
- GPT judgment also uses two batches of 48: its conservative 96-request total
  reservation is USD 0.1299392, above the single-batch USD 0.10 cap. The 24
  extraction requests reserve USD 0.0957628 and fit one batch. Jev reserves
  USD 0.048 per 48-request batch. These are bounds, not observed costs.
- These are different task/instrument arms. Jev judgment is not free graph
  extraction; GPT assisted extraction is not source-only node discovery.

## Scoring frozen before calls

Extraction outputs have distinct local assertion IDs, predicate, directed
inventory endpoints, positive/negative relation polarity, attributed speaker,
original known_at and exact quote/turn identity. The compiler accepts only
unique unchanged substrings and derives Unicode-codepoint/UTF-8 spans; it
refuses missing/ambiguous quotes and mismatched known_at. Content stays
unverified. Inferred paths and confidence-to-fact promotion are not accepted.

Primary edge metrics match predicate, direction, polarity, attribution,
known_at and supporting turn identity, with all raw quotes/spans revalidated
at scoring. Extra or invalid candidate assertions are FP, missing assertions
are FN. Missing/unavailable requests retain all gold denominators. Correction
events are scored separately against retained historical assertions.
Subspans can match source binding, but narrow-quote clause adequacy remains an
explicit review count and limitation; exact quote location is not a proof that
the chosen quote semantically supports the edge. Full-turn citations are
requested to preserve context. These metrics are typed source-edge/binding
quality, never world-content truth accuracy.

Judgment threshold is fixed at **strictly greater than 0.5** independently for
support and refutation. Support only = supported; refutation only = refuted;
neither = unknown; both = conflicting/unavailable. The last state never becomes
unknown or a forced yes/no. Invalid/missing/conflicting queries remain in gold
class recall and overall accuracy denominators. Report the full confusion
matrix, per-class precision/recall, coverage and every failure ID. Defined-class
macro precision includes its defined-class count; undefined precision stays null.

## Cost, execution and evidence

The adapter performs no network request, reads no key and creates no spending
authorization. Root execution inherits the existing **non-resetting global USD
2 session cap**, existing immutable ledgers, exact model/provider/price pins,
fresh public endpoint evidence and no paid retries. Each batch is capped at
USD 0.10; it is not allocated a fresh global budget. The coordinator must
reserve and reconcile each batch against the same session accounting before
and after execution.

GPT request bodies pin the public-preflight model `openai/gpt-4.1-mini` and
provider `openai`, with no fallback, JSON mode and deterministic temperature.
Extraction requests cap output at 1536 tokens; ternary judgments at 64.
Price caps must be verified against the fresh public snapshot by the bounded
runner. Request specs alone are not a billing guarantee.

Root must save the freeze (adapter, dependency runner, prompts, fixture,
protocol), prepared request hashes, public endpoint snapshots, model identity,
first raw responses, ledgers, costs and compile/score failures. Scoring may
read development gold only after response preservation. No validation access,
canonical graph mutation, synthetic-source-to-fact promotion or claim of
production graph understanding follows from this DEV arm.

## Mechanical verification

`python3 -m unittest loom.tools.structure.test_graph_panel_live -v` passes
20 independently authored mechanism checks. They exercise input separation,
physical prefixing, exact Unicode/UTF-8, ambiguity rejection, preservation of
denominators, direction/attribution/time mismatch, correction history,
source-bound subspan review, conflict handling and first-output immutability.
These are not measurements of model quality.
