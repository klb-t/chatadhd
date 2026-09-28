# Independent supplied-input checks for the Gemini-inspired experiments

Protocol frozen on 2026-09-28 before executing or reading the new `logic_check.py`
and `context_scores.py` implementations or their author tests. The independent
reviewer read only the public occurrence-graph and context-packet contracts, and
the already existing `context_delta.prepare_context` packet producer. No old
holdout key, natural-language benchmark answers or provider calls are involved.

## Predeclared checks

The logic fixtures are independently constructed occurrence bundles, with exact
UTF-8 source spans and the existing candidate vocabulary. Each `predicate_application`
handle is one propositional atom. Equal labels and equal predicate spellings do
not identify different occurrence handles. A separately implemented enumerative
truth-table oracle uses only Python tuples and Boolean operations. It checks:

1. Material modus ponens entails its consequent.
2. Affirming the consequent does not entail its antecedent.
3. Contradictory premises are reported inconsistent before any entailment result.
4. Equal labels on distinct occurrence handles cannot supply the missing antecedent.
5. Double negation and conjunction elimination preserve classical truth conditions.
6. Nested negation/conjunction with a false conclusion produces a valid countermodel.
7. Quantified and quoted bundles are outside the supported asserted propositional fragment.
8. A lowered atom/valuation budget yields an explicit unknown/budget result.
9. A changed exact source quote rejects the input rather than proving anything.

The context replay fixture is a packet produced by `prepare_context`, with three
explicitly allowlisted old Claims and complete local source support. Its question
is a four-field object (`id`, `text`, `rubric_id`, `rubric`). The response is bound
to packet, request and question hashes. Predeclared checks:

10. Independent probabilities `[0.9, 0.8, 0.1]` yield keep/keep/drop at thresholds
    0.75/0.25; their sum is intentionally greater than one.
11. Missing and invalid probability values yield review for the affected candidate.
12. Unknown or duplicate candidate IDs invalidate the whole response.
13. Changed packet, request or question hash invalidates the whole response.
14. Invalid thresholds are rejected; threshold boundaries have explicit behavior.
15. Complete explicit ID remapping between two independently created packets allows
    comparison; incomplete/non-bijective maps and changed questions are rejected.
16. Inputs remain byte-for-byte JSON-equivalent; no canonical graph record is
    rewritten or promoted. A drop suggestion is not a deletion.

Public API field names may be aligned before the first execution. Such alignment
must not alter the cases or expected semantic outcomes. Fixture and implementation
hashes and the complete first execution are recorded separately before inspecting
failures. Corrections, if needed, retain the first report and get a distinct rerun.

## Interpretation limits

These are finite mechanical checks of supplied graph structures and supplied
scores. They do not measure parsing quality, causal or defeasible reasoning,
quantifier reasoning, learned relevance calibration, token cost, model quality,
probability truth, natural-language scope detection or production graph promotion.
Agreement under an explicit ID mapping does not prove semantic equivalence.

## Results

The first independent execution passed **13/13 test methods**, covering 41 API
calls, with no fixture changes or implementation fixes prompted by those results.
The complete first report was saved before assertions in
`GEMINI_EXPERIMENTS_INDEPENDENT_2026-09-28.initial.json` (810,029 bytes,
SHA256 `85c6276e8e73dc28b05bdfa9f661f6c9f1413161bd884322c93d853c181a26a0`).
Neither new module's implementation nor its author-test contents were inspected before this run.

| Supplied logical query | Independent expected and actual result | Assignments | Premise models |
| --- | --- | ---: | ---: |
| Material modus ponens | entailed | 4 | 1 |
| Affirming consequent | undetermined | 4 | 2 |
| Contradictory premises | inconsistent_premises | 4 | 0 |
| Same label, distinct application handles | undetermined | 8 | 3 |
| Double negation | entailed | 2 | 1 |
| Conjunction elimination | entailed | 4 | 1 |
| Negated conjunction with one member true | contradicted | 4 | 1 |

All returned witnesses independently satisfy the selected premises and the
reported conclusion value. Two unsupported-scope queries return `unsupported`;
two lowered budgets return `limit` with zero assignments; Boolean limits and
changed source quotes return `rejected`.

Context replay preserves the supplied independent scores exactly, including
multiple simultaneous keep suggestions. Missing scores and six invalid score
shapes abstain locally; changed hashes, duplicate/unknown IDs and a rebound
question invalidate the whole response and return review suggestions. Threshold
validation and inclusive boundaries behave as contracted. Complete declared ID
remapping yields 3/3 measured pairs, suggestion agreement 1.0, mean absolute
score drift 0.0833333333 and maximum drift 0.1. Incomplete/nonbijective remapping
and differing questions are refused. All 41 calls leave their input objects
unchanged; original packet, old Claim Assessments and grounded graph inputs are
retained. These finite checks found **no remaining failure within this protocol**.

After the first run, the score-module author independently moved root packet
shape rejection before bounded traversal, with an author regression for excluded
audit sidecars. That change was not caused by this suite. The unchanged 13-method
independent suite was rerun against the final revision: **13/13 passed**, and all
41 output records exactly matched the first run. The compact second record is
`GEMINI_EXPERIMENTS_INDEPENDENT_2026-09-28.rerun.json`; the full first report remains
unchanged. No new independent claim about audit-wrapper traversal is made here.

| Component | SHA256 in the first run | Final rerun change |
| --- | --- | --- |
| `logic_check.py` | `28c0225cea9afa9b765cdebe41c8bb4f3d3054860ada42d04b2e74fb597e2c1d` | unchanged |
| `context_scores.py` | `4b612c1a0e7b554b04a9c6d3c4b0a03ec716074fa55d841e44ad05a58b689938` | `04548caf29084e3ef53404140f204e548b6781464b84607d074584fff7edf61d` |
| `candidate_graph.py` | `ade0944646c3cfc5356787612beb14657335db2067b4a705d5221b0f3810125d` | unchanged |
| `context_delta.py` | `8fd14a79edbb280eb0a16c08eabecece50dbb59d75b25bb78d31d08d5feba470` | unchanged |

Independent suite SHA256:
`e213a43813e9594d7339d157abe43dd23581dec96d6ffbb3f0b487e80a8ba590`.

Reproduce the checks without modifying the recorded first report:

```bash
python -m unittest discover -s loom/tools/structure -p 'test_gemini_experiments_independent.py' -v
```

The script's `--write-first-report` option intentionally refuses to overwrite an
existing first report. The quoted counts are mechanical cases, not a success rate
for interpreting conversations or choosing which actual graph information to keep.

### Freeze record, before new-module execution

- Original protocol SHA256: `1bd88dbec0458fdb5fde3c8946a3bbcc275fe1d2b64227bbaeff008ff963fb76`.
- Final fixture SHA256: `b8fd68f0befe8e24eecdb1645f6410e72ab7b59374c28091864771d06f3159a3`.
- Preliminary fixture SHA256: `53fe35d7f1a0d08475dd85c52d059f02231db825dbeb02d62595b92f9d0594f8`.
  Before running either new module, the existing candidate validator rejected the
  fixture's literal Claim `object:null`. The established wire format requires
  `object:""`; this serialization-only correction changed no source, operation,
  label, queried root or expected logical outcome. All nine constructed bundle
  shapes then passed the existing structural validator.
- The logic public contract distinguishes `contradicted` (all premise models make
  the conclusion false) from `undetermined` (both outcomes possible). The
  independent oracle derives this distinction from its enumerated assignments;
  inconsistent premises remain a separate result.
- Public threshold boundaries are inclusive: `p >= keep`, `p <= drop`, with
  `0 <= drop < keep <= 1`; booleans and non-finite values are inadmissible.
- The suite contains 13 test methods with subcases. It writes the complete first
  run using exclusive file creation **before** assertions; ordinary discovery
  does not overwrite that record.
