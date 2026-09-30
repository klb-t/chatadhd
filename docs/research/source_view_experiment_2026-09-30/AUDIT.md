# Independent audit: historical versus active source refutation

2026-09-30; posthoc diagnostic DEV review. All 96 retained responses were
replayed offline. No API, validation input/gold, index operation, frozen code
edit, score repair, threshold change or retry was used. `AUDIT.json` contains
every receipt/hash, all 48 paired probabilities and physically supplied source
prefixes, family/language confusion tables, and all six first-arm failures.

## Integrity, intervention and cost

All **10/10** prepared freeze hashes match: five code files, four specification
files and the fixture manifest. In all 48 request pairs, the only body changes
are `questions.q02.instructions`, `questions.q02.criteria.true` and
`questions.q02.criteria.false`. q01, query, node inventory, physically truncated
source, question type, provider/model, price cap and decision threshold are
unchanged. All **216** source-turn bindings across the two arms match their
expected prefix and respect `known_at <= as_of`.

All **96/96** raw hashes, request/ledger bindings, model/provider identities,
probabilities and usage costs match the saved compiled results and receipts.
Replaying the scorer after the verified responses gives the exact saved primary
score fields. Independently reconstructing the latest matching source action
gives the fixture labels/stances for **48/48** queries; all **36** fixture
assertion/event evidence bindings preserve exact character/UTF-8 coordinates
and source dates. These latter checks validate the mechanism, not model quality.

| Arm | Responses | Raw usage cost | Saved ledger/score cost |
|---|---:|---:|---:|
| Historical refutation v1 | 48 | USD 0.001482852 | Exact match |
| Active refutation v2 | 48 | USD 0.001664292 | Exact match |
| Total | 96 | USD 0.003147144 | No missing cost |

All receipts identify `typesafe/jev-1.13-20260917`, provider `TypeSafe`, HTTP
200. Per-request cost also equals input tokens × USD 0.042 per million; no
output charge occurs. Optional generation billing lookups are **unavailable
with HTTP 404 for 96/96**. Receipt reconciliation is complete; external
invoice-level verification is not claimed. Existing nonresetting budget
authorization is not renewed by this audit.

## Preserved primary results

| Class | v1 TP / FP / FN | v1 precision / recall | v2 TP / FP / FN | v2 precision / recall |
|---|---|---|---|---|
| Supported | 18 / 0 / 0 | 18/18; 18/18 | 18 / 0 / 0 | 18/18; 18/18 |
| Refuted | 14 / 6 / 0 | 14/20; 14/14 | 14 / 3 / 0 | 14/17; 14/14 |
| Unknown | 10 / 0 / 6 | 10/10; 10/16 | 13 / 0 / 3 | 13/13; 13/16 |

Accuracy improves **42/48 → 45/48** (87.5% → 93.75%); availability remains
48/48 in both arms. Every error is unknown misclassified as refuted. There
are three corrected labels and no new label errors. Always-unknown gives
16/48, with supported recall 0/18 and refuted recall 0/14. These are query
denominators, not independent sample counts.

| Whole bilingual family | v1 correct | v2 correct |
|---|---:|---:|
| Reaffirmation after denial | 8/8 | 8/8 |
| Positive withdrawal without replacement | 6/8 | 6/8 |
| Quoted withdrawal attribution | 8/8 | 8/8 |
| Repeated positive history | 8/8 | 8/8 |
| Another speaker's reaffirmation | 8/8 | 8/8 |
| Withdrawn denial and silence | 4/8 | 7/8 |

PL accuracy is **21/24 → 23/24**; EN is **21/24 → 22/24**. Each language
has 9 supported, 7 refuted and 8 unknown queries. The original concern about
historical denial persisting after reaffirmation **did not appear here**:
both original bilingual reaffirmation cases already pass every query under v1.
The observed gain is in withdrawal of a negative commitment. It would be
incorrect to describe these results as demonstrating the originally suspected
post-reaffirmation failure.

## Every first-arm error and the three remaining errors

All six gold labels below are unknown; v1 predicts refuted in all six.

| Query | q02 v1 → v2 | v2 label | Source diagnosis |
|---|---:|---|---|
| `svlv1_dev_003_q4` (PL) | 0.78 → 0.88 | Refuted, wrong | A→B is withdrawn, but the query asks B→A. No reverse assertion or reverse withdrawal exists. |
| `svlv1_dev_004_q4` (EN) | 0.55 → 0.78 | Refuted, wrong | Same reverse-direction counterexample. |
| `svlv1_dev_011_q2` (PL) | 0.67 → 0.34 | Unknown, correct | The earlier denial is explicitly withdrawn without either new stance. |
| `svlv1_dev_011_q3` (PL) | 0.64 → 0.33 | Unknown, correct | Subsequent drawer-related speech leaves that withdrawal in force. |
| `svlv1_dev_012_q2` (EN) | 0.65 → 0.44 | Unknown, correct | The earlier denial is explicitly withdrawn; neither side is asserted. |
| `svlv1_dev_012_q3` (EN) | 0.72 → 0.58 | Refuted, wrong | The same withdrawn denial reappears as refutation after irrelevant drawer speech. |

The two reverse-direction errors are not gold ambiguity. Withdrawal is bound
to the exact directed relation and speaker. The active recipe raises their
incorrect refutation probability, demonstrating why increased confidence is
not correctness.

The English last-prefix error contrasts with the correct preceding q2 view,
whose source prefix differs only by an irrelevant additional turn. That turn
neither reverses the withdrawal nor adds a negative commitment. The bilingual
PL counterpart passes. Losing applicability/polarity over prefix extension is
a **diagnostic hypothesis**, not an inferred internal explanation of the model.
The retained probabilities and exact sources support investigating it with a
separately frozen representation experiment.

## The unchanged question does not imply unchanged output

| Paired probability diagnostic | q01 support | q02 refutation |
|---|---:|---:|
| Identical probability | 28/48 | 15/48 |
| Changed probability | 20/48 | 33/48 |
| Mean absolute probability difference | 0.0060416667 | 0.0633333333 |
| Maximum absolute difference | 0.05 | 0.33 |
| Changed strictly-`>0.5` decision | 0/48 | 3/48 |

q01 is unchanged **in the request**, not in every response. Joint-question
context, independent-call variation, or both remain competing explanations for
these 20 probability differences. A single paired first response cannot separate
them or establish packing independence. Identical decisions are not proof of
correctness: the reverse-withdrawal pair is identically wrong. This experiment
does not measure repeated-call stability or probability calibration.

## Epistemic scope and next decision

Keep active-refutation v2 as a **DEV experimental recipe**, preserving v1 and
both first response sets. Investigate directed withdrawal scope and persistence
of a withdrawn negative commitment across irrelevant turns. Any new source
representation or question-packing probe must be declared and frozen separately
before observing its outputs.

The fixture is deliberately narrow: 12 related green-lamp/gate conversations,
six correlated bilingual families and repeated temporal prefixes. It supplies
the queried directed relation and proposition inventory. No natural-text
population estimate, holdout generalization, production readiness, causal
superiority claim or model-global reliability follows from 42/48 → 45/48.

The policy's `refuted` label for positive withdrawal concerns the speaker's
commitment; it does not fabricate a negative source assertion or establish that
the relation is false. Withdrawing a denial leaves unknown and never implies
positive affirmation. Attributed quoted withdrawal remains a reported source
act, not verified world history. Raw observations and dates remain retained;
active source views are reconstructable policy projections. World-content
truth stays unverified throughout.
