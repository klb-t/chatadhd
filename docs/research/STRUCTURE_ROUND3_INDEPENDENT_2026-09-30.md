# T6 round 3 independent fixture, scorer and source-scope audit

The optional explicit-relation channel improves the first frozen synthetic
validation result from **0/16** to **5/16** recall, with **5/5** precision.
Development was 32/32, so the validation result exposes a substantial transfer
gap. On repository documents, six exact source-span envelopes contain only
two clearly valid argument-envelope scopes, three wrongly grouped operand
scopes and one case requiring a richer ellipsis policy. No parser or fixture
was changed after validation/source inspection.

This measures source-envelope extraction. It does not measure claim truth,
logical validity, causal validity, or successful production graph reasoning.

## Independence and release chronology

- An isolated fixture author read the work contract, R22–R24 and required
  conceptual vocabulary, without reading the parser, earlier source benchmark
  outputs or source texts for parser development.
- `loom/tests/fixtures/eval/structure_round3_v1/PLAN.md` was written before
  authoring outputs. The complete 96-case fixture and hashes were frozen
  before any parser prediction: development 64, validation 32, eight families,
  PL/EN and supported/abstain balanced within each family and split.
- The parser developer and parent received development only. Validation stayed
  sealed during development. The author naturally knew its own labels; this
  is independent authorship relative to the parser developer, not blindness
  of the author to its own source sentences.
- Before release, the independent evaluator inspected the scorer and found
  three classes of defect: cue quotes were unchecked; offset values accepted
  booleans/floats; the CLI could create and release its freeze in one call.
  The parent repaired the scorer before any validation call. The old
  `FREEZE.json` is retained as history. These were scorer/protocol fixes, not
  tuning from validation labels.
- The independent mechanism suite passed **33/33** after repair. Its first
  run overlapped the parent's repairs and required adding the now-enforced
  `coordinate_space: input.text` to the synthetic test candidate and expecting
  argparse's `SystemExit(2)`. It is not a parser-quality result.
- `docs/research/structure_round3_v1/FREEZE2.json` fixed the implementation,
  scorer and fixture manifest before release. The evaluator explicitly
  released validation and executed each policy exactly once, baseline first.
- First outputs are immutable through exclusive-create writes. No second
  validation run, parser revision or label correction followed inspection.
- The repository-source candidates were audited after those frozen runs.
  Their stored parser hashes match the released parser; the older source
  artifact records the pre-audit scorer hash, because only the scorer changed.
  It was not silently regenerated.

Fixture SHA-256:

| File | SHA-256 |
|---|---|
| dev.json | `69bb3542c4abf24eb57444b21274f76e02a5c60d4e6ea184e852f089c41cb616` |
| validation.json | `3abca3a3abb835f5f854182a626bc69effd24ef6daab0f93a75f1d790ad74a5c` |

## Independent scorer mechanism checks

File: `loom/tools/structure/test_round3_eval_independent.py`.

```
python3 -m unittest loom.tools.structure.test_round3_eval_independent -v
```

The suite checks exact Unicode/UTF-8 offsets, raw quote equality, bounds,
integer-only coordinates, coordinate-space identity, negation retention,
directed operand swaps, wrong/extra roles, operation/family/qualifier mismatch,
strict versus family-only scoring, duplicate candidates as excess FP,
duplicate/unknown output rejection, missing-output FN and abstention counts,
per-family denominator reconciliation, empty denominators, implementation and
fixture drift, explicit release, pre-existing freeze and no-overwrite writes.

Scorer independence is limited to this mechanism audit and a direct recount:
it is not a formal proof that every possible malformed input is handled.

## First validation results

Each split has 16 positive relations and 16 deliberate abstentions. Precision
uses emitted relation count, recall uses gold-positive count; correct
abstentions are not folded into an inflated accuracy denominator.

| Policy | TP | FP | FN | Emitted | Gold | Precision | Recall |
|---|---:|---:|---:|---:|---:|---:|---:|
| bounded baseline | 0 | 0 | 16 | 0 | 16 | unavailable (0/0) | 0/16 = 0% |
| explicit_relations | 5 | 0 | 11 | 5 | 16 | 5/5 = 100% | 5/16 = 31.25% |

All 32 output cases were present, including the 16 abstention cases. The
explicit channel emitted no result for any deliberate abstention (**0/16**
false-positive abstention cases). All five emitted candidates had valid exact
source spans. Family-only counts equal strict counts on this first validation
run; that equality is a measurement, not a replacement of strict extraction
with cue detection.

| Family | Baseline TP / gold | Explicit TP / gold | Explicit FP |
|---|---:|---:|---:|
| conditional | 0/2 | 0/2 | 0 |
| cause | 0/2 | 1/2 | 0 |
| contrast | 0/2 | 0/2 | 0 |
| exception | 0/2 | 0/2 | 0 |
| means_goal | 0/2 | 0/2 | 0 |
| generalization | 0/2 | 2/2 | 0 |
| conjunction_alternative | 0/2 | 2/2 | 0 |
| evidence_claim | 0/2 | 0/2 | 0 |

First full predictions: `structure_round3_v1/baseline_validation.json` and
`structure_round3_v1/final_validation.json`.
`structure_round3_v1/independent_recount.json` contains a separate direct
multiset recount with its own source-span checks; it did not call the scorer's
`_expected`, `_actual`, `score` or metric functions. It reproduces both saved
TP/FP/FN counts and gold/prediction denominators exactly.

### Every final failure ID

All 11 are missing positive relations, with no emitted candidate:

| Case | Family | Diagnostic variation, observed only after release |
|---|---|---|
| sr3v1_validation_001 | conditional | `only if`, necessary-condition direction |
| sr3v1_validation_002 | conditional | Polish `O ile`, negative condition operand |
| sr3v1_validation_006 | cause | effect-first Polish `ponieważ` |
| sr3v1_validation_009 | contrast | English `Although` concession |
| sr3v1_validation_010 | contrast | Polish `Choć` concession |
| sr3v1_validation_013 | exception | condition-first `Unless` |
| sr3v1_validation_014 | exception | Polish `oprócz` |
| sr3v1_validation_017 | means_goal | `so that` finite purpose clause |
| sr3v1_validation_018 | means_goal | `w celu` nominalized purpose |
| sr3v1_validation_029 | evidence_claim | `supports the conclusion that` |
| sr3v1_validation_030 | evidence_claim | `wskazują na to, że` |

Every baseline failure ID:
`sr3v1_validation_001`, `sr3v1_validation_002`, `sr3v1_validation_005`,
`sr3v1_validation_006`, `sr3v1_validation_009`, `sr3v1_validation_010`,
`sr3v1_validation_013`, `sr3v1_validation_014`, `sr3v1_validation_017`,
`sr3v1_validation_018`, `sr3v1_validation_021`, `sr3v1_validation_022`,
`sr3v1_validation_025`, `sr3v1_validation_026`, `sr3v1_validation_029`,
`sr3v1_validation_030`.

Diagnosis: perfect development precision/recall did not transfer across
surface constructions and cue/argument order. The 5/5 precision denominator
is small and synthetic; it does not establish robust natural-text precision.
These cases are now exposed validation and may be diagnostic development data
in a later round, never a fresh holdout for a revised parser.

## Independent audit of all six repository-source candidates

Input: `structure_round3_v1/source_first.json`. The three source-file hashes
match current bytes. Every overall envelope, cue, operand and source-fragment
span maps exactly to both Unicode and UTF-8 source coordinates (**6/6**).
Source retention is therefore verified as a mechanism; operand scope must be
judged separately.

`structure_round3_v1/independent_source_scope_audit.json` records all six
verdicts, source hashes and recursive raw-span checks. All **44** raw-span
instances pass, including discontiguous source-fragment spans under Markdown
normalization. The parser, policy and scorer still match `FREEZE2.json` after
the independent audit.

Audit criterion: a valid result identifies the explicit relation and both
operands at the source's grammatical scope without silently putting a shared
governing instruction, definition or permission into only the left operand.
Opaque text is allowed and does not prove semantics. Ellipsis needing
explicit shared scope can remain `needs_review`; exact byte provenance alone
does not resolve it. Under this criterion: **2 valid, 3 invalid, 1 needs_review**.

| Candidate | Source | Verdict | Reason |
|---|---|---|---|
| `candidate_4e8054b22fe475c8d168` | OWNER_REQUIREMENTS, R21/R22 boundary | invalid | In “Scan the whole codebase for generalisations and for choices…”, `and` coordinates two search complements. Left contains the governing imperative; right is only `for choices…`. Treating them as conjunction operands of two source assertions misgroups scope. |
| `candidate_70c14f7f793f2a0284a6` | NOTATKA_GPT, §6 | valid | “Powinien zachować wspólną strukturę epistemiczną, ale lokalnie dostosowywać…” states an explicit preservation/adaptation contrast. Both source fragments are retained. The shared model subject/modal remain raw context; this verdict is envelope extraction, not two independent logical formulas. |
| `candidate_f79672008077916f51ae` | NOTATKA_GPT, §7 B. Heuristics | needs_review | “zasady skuteczne zazwyczaj, ale nie absolutne” contrasts two properties with an elliptical shared subject. Left also includes the definition label and raw Markdown. A property-level relation may be valid, but these operands need a declared shared-subject/scope policy before graph or logical use. |
| `candidate_394a3b959b6feb451274` | NOTATKA_GPT, §11 | invalid | “LEM — reprezentacja epistemiczna i operacje…” coordinates two nominal responsibilities under the shared LEM definition. The extracted left includes `LEM —`; the right does not. This is not the same operand grouping as a conjunction between independently stated clauses. |
| `candidate_542d40842978005dc3fc` | LOOM_CONCEPTUAL_MODEL, preamble | valid | Explicit “If a component needs a meaning that is not here, it stops…” supplies condition and complete consequence, including the negative prohibition. No truth of the rule or fulfillment by components is inferred. |
| `candidate_1b1a5d0e0d4145276461` | LOOM_CONCEPTUAL_MODEL, §3.3 | invalid | “lets the system produce solutions… and predict…” coordinates two activities under `lets the system`. The left includes the governing metaproposition; the right is a bare activity. Correct source bytes do not justify that asymmetric conjunction scope. |

This is a small manual audit of emitted candidates on already selected
architecture texts, not a labelled census of all missed thought structures.
Consequently it gives neither natural-corpus recall nor unbiased semantic
accuracy. The six envelope count must not be advertised as six validated
argument graphs. Known-at was unavailable for the supplied source records
and was retained as `null`; no timestamp was invented.

## Decision and next informative step

**Keep as an optional experimental representation channel; investigate
generalization and shared scope.** Its first synthetic recall improvement is
real against this frozen baseline, and exact provenance is preserved. It
does not yet satisfy broad thought-structure extraction or justify production
logical inference. No formulas, graph mutations, canonical facts or calibrated
confidence were manufactured in the source artifact.

The next round should first specify a reversible representation for shared
governors, subject/modal ellipsis and nominal coordination, using independently
authored development examples. Then freeze a new independently authored
validation split before parser changes are evaluated. Include cue/word-order
variations and adversarial scope cases as planned strata. Keep these first
results unchanged and report future reuse of this validation explicitly.

The quote/question/code abstention policy is local to this channel. It cannot
veto another retrieval channel or prevent scoped source observations from
being stored in the single knowledge graph.
