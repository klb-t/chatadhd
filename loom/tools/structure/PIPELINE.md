# Conversation structure experiment

`pipeline.py` composes the offline experiments without creating another knowledge
store. It accepts an existing graph snapshot and a conversation with exact source
text. It returns topic candidates, source-located thought interpretations,
multiple comparison perspectives and graph-context review proposals. It creates
no Claims, writes no database and calls no model service.

```sh
python loom/tools/structure/pipeline.py /tmp/conversation.json --output /tmp/structure-report.json --pair-budget 128
python -m unittest discover -s loom/tools/structure -p 'test_*.py'
```

Input follows [TOPICS.md](TOPICS.md): `{id,turns:[{id,role,text}],entities,graph,claims?}`.
Unknown fields, attachments and the original input survive unchanged. The graph
is a supplied snapshot; the caller must export its actual node/edge qualifiers
and Claim IDs. A supplied ID is not verified entity identity. When no conversation
ID is given, a deterministic local input ID is recorded explicitly as generated.

## Output and interpretation

| Field | What it provides | What it does not establish |
| --- | --- | --- |
| `segmentation` | Exact local spans, candidate topic boundaries, current focus and one-hop graph context | General topic understanding or resolved coreference |
| `extractions` | Recognized envelopes and unchecked formulas, with exact source quotes and every unknown span | Complete interpretation of a sentence |
| `comparisons` | Lexical, role/relation, WL and bounded alignment scores for individual observations across proposed segments | Same subject, same project, truth or valid transfer |
| `scope_projections` | Whole-segment compositions under separate and literal-symbol binding hypotheses | Verified identity across statements |
| `boundary_alternatives` | Envelopes recovered from original physical lines across tentative segmentation cuts | A shared topic scope or permission to add them to primary coverage |
| `interpretation_context_proposals` | Candidate interpretations associated with current observation focus and old graph Claim IDs | New assessed Claims or permission to overwrite old knowledge |
| `reasoning.blocked_interpretations` | Formula candidates withheld because extraction has no Assessment | Disproof or inability of a future verified reasoner |
| `coverage` | Grammar coverage with explicit denominator and abstentions | Semantic accuracy |

Local extraction offsets are checked against the exact parent topic observation,
then converted to character and UTF-8 byte offsets in `input.turns[].text`.
They are not falsely reported as byte offsets in an original provider ZIP.
The original turn hash, role, segment evidence and every supplied locator remain
available. Context uses the observation's focus, never anchors accumulated later
in its segment. An earlier unrelated statement therefore cannot inherit a later
project name merely because both are in the same conversation.

The two scope projections preserve different assumptions. `separate` binds
symbols only within each formula. `literal_within_scope` also shares exactly
equal parser symbols across formulas in one declared conversation segment.
Every such reuse is listed as an unverified assumption; quotations, homonyms,
speaker disagreement, temporal changes and hidden branch conditions remain
unsolved. Predicate polarity and quantifier structure are represented only when
the extractor actually produced them. Statement order is not represented in
these logical projections. See `scoped_projection.py` and its author-owned tests.

To compare two projected segments, including segments from different conversations:

```python
from scoped_projection import compare_scopes
comparison = compare_scopes(left_scope, right_scope)  # same binding mode required
```

The result keeps logical structure, literal semantic labels and atomic operation
shapes as separate perspectives. It exposes missing coverage and exhausted search
budgets. A high structural score is a proposed analogy, never an inferred fact.

V3 also runs the unchanged grammar over original physical lines. This can recover
an envelope such as `Goal: record music; constraint: preserve dynamics.` when
topic observations split at its semicolon. Exact primary matches are suppressed.
Recovered alternatives retain every overlapping observation's focus and proposed
segment, choose no single scope, require scope review and stay outside main
coverage, scoped logic and proof inputs. See [BOUNDARY_ALTERNATIVES.md](BOUNDARY_ALTERNATIVES.md).

## Bounds and reproducibility

Observation comparisons sample cross-segment pairs in source order. `pair_budget`
limits actual comparisons; eligible and omitted pair counts are reported exactly.
They are not top-k search or estimates of a pattern's universality. Setting the
budget to zero disables comparisons while preserving extraction and context.
Scope composition defaults to at most 64 formulas and 256 graph nodes. An
oversized scope produces a visible refusal; all its source extractions survive.
The pipeline does not silently repartition an argument to fit a budget.

Input, graph and policy hashes plus component versions identify a run. Independent
reports additionally record implementation hashes. V1 independent evaluation is
retained, including its lack of eligible cross-segment comparisons in that corpus;
V2 adds scope composition without changing extraction grammar or native selection
thresholds. Author mechanism checks and held-apart semantic measurements are
different tests and must be reported separately.

## Production boundary

This is not yet an application feature or a general conversation analyzer. The
strict EN/PL grammar leaves substantial natural language uncovered. A later
model-backed interpreter can supply assessed interpretation candidates behind
this source/projection contract, but requires independent grounded extraction
evaluation. Existing graph refinement, contradiction handling and simplification
still require reviewed proposals, Assessments, reversible history and current
core-model verification. Current similarity outputs cannot authorize those writes.
