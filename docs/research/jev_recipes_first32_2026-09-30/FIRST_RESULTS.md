# First32 LIVE result: source expression versus bounded inference

All 32 first calls completed, 64/64 decisions agreed with the frozen synthetic
T3 gold at p >= 0.5. There were 30 TP, 34 TN, zero FP and zero FN; no missing
calls or unknown reported costs. The invoice usage sum is USD **0.000950964**,
against the preregistered USD 0.032 reservation. This is already-known authored
development data; the corpus's `validation` label does not make it blind or
independent. The result does not measure end-to-end graph extraction/retrieval.

| Task | Instruction arm | TP / positives | TN / negatives | Precision | Recall | Brier | Selective decisions / planned |
|---|---|---:|---:|---:|---:|---:|---:|
| expressed | meaningful object | 5/5 | 11/11 | 5/5 | 5/5 | 0.0278125 | 13/16 |
| expressed | string | 5/5 | 11/11 | 5/5 | 5/5 | 0.03416875 | 13/16 |
| inferred | meaningful object | 10/10 | 6/6 | 10/10 | 10/10 | 0.03981875 | 11/16 |
| inferred | string | 10/10 | 6/6 | 10/10 | 10/10 | 0.02984375 | 10/16 |

The simpler all-negative baseline is 34/64, all-positive 30/64. For expressed,
the majority-negative baseline is 11/16 per arm; for inferred, majority-positive
is 10/16 per arm. Both arms beat those baselines here and tie each other on
decisions. All five proposed-abstraction cases r03/r07/r09/r13/r16 are classified
expressed No / inferred Yes in both arms: 10/10 paired distinctions. These Yes
inferences remain scoped research candidates, not observed intentions or facts.

The 16/16 paired accuracy contrasts are exactly zero. Meaningful-minus-string
Brier contrasts and preregistered case-bootstrap 95% intervals are:

| Task | Mean Brier difference | Percentile interval |
|---|---:|---:|
| expressed | -0.00635625 | [-0.0205500, 0.00165625] |
| inferred | 0.00997500 | [-0.00075703125, 0.02778125] |

Negative Brier differences favor meaningful; positive favor string. Both
intervals include zero. There is no supported winner of instruction format.
Eighteen of 64 probabilities differ between arms, without any thresholded
decision changing. There were no repeated calls, so this is not a stability
experiment. Overall Brier is 0.0329109375, log loss 0.1525310036 and five-bin
ECE 0.13078125 on 64 observed synthetic decisions; bin counts are 22/7/5/5/25.
Diagnostic selective coverage is 47/64, zero errors among those 47.

## Raw probabilities and error diagnostics

| Case | Gold expressed/inferred | Expressed object | Expressed string | Inferred object | Inferred string |
|---|---:|---:|---:|---:|---:|
| r01 | 1/1 | 0.87 | 0.88 | 0.90 | 0.90 |
| r02 | 0/0 | 0.02 | 0.02 | 0.03 | 0.03 |
| r03 | 0/1 | 0.09 | 0.09 | 0.76 | 0.77 |
| r04 | 0/0 | 0.04 | 0.03 | 0.11 | 0.11 |
| r05 | 1/1 | 0.97 | 0.97 | 0.95 | 0.96 |
| r06 | 0/0 | 0.05 | 0.05 | 0.41 | 0.37 |
| r07 | 0/1 | 0.10 | 0.13 | 0.81 | 0.79 |
| r08 | 1/1 | 0.98 | 0.97 | 0.96 | 0.95 |
| r09 | 0/1 | 0.06 | 0.06 | 0.76 | 0.75 |
| r10 | 0/0 | 0.02 | 0.02 | 0.03 | 0.03 |
| r11 | 0/0 | 0.04 | 0.03 | 0.23 | 0.21 |
| r12 | 1/1 | 0.96 | 0.96 | 0.95 | 0.96 |
| r13 | 0/1 | 0.44 | 0.44 | 0.87 | 0.86 |
| r14 | 1/1 | 0.95 | 0.94 | 0.93 | 0.93 |
| r15 | 0/0 | 0.35 | 0.48 | 0.45 | 0.27 |
| r16 | 0/1 | 0.28 | 0.26 | 0.90 | 0.90 |

No FP or FN exists in this first panel, so error analysis examines near misses
rather than inventing failures. r15 uses the same word in two explicitly
different senses (sample campaign versus audio buffer): expressed string p=0.48
is only 0.02 below the fixed threshold. Its inferred probabilities vary by
0.18 across formats (0.45 object / 0.27 string), the largest contrast. r13's
nonliteral author-strategy interpretation remains expressed p=0.44, inferred
0.87/0.86. r06's unsupported assertion of a shared date-formatting module is
inferred 0.41/0.37 despite similar screen output. These are diagnostic cases
for polysemy, abstraction scope and implementation extrapolation.
The independently recounted r15 alone contributes +0.0081 to the inferred mean
Brier difference +0.009975 (about 81% of that worsening); the apparent average
format effect is mostly a single authored case. This diagnostic does not remove
it from the primary score or change the frozen threshold.

Language and authored-split profiles all have full coverage and no errors:

| Group | TP | TN | Decisions | Brier |
|---|---:|---:|---:|---:|
| EN | 14 | 18 | 32 | 0.016765625 |
| PL | 16 | 16 | 32 | 0.04905625 |
| authored development | 16 | 16 | 32 | 0.019865625 |
| authored validation | 14 | 18 | 32 | 0.04595625 |

These groups have different case mixes and share the authored corpus; the Brier
differences do not isolate a language effect or independent generalization.

## Instrument, latency and durable evidence

Actual model: `typesafe/jev-1.13-20260917`, provider TypeSafe. Catalog price is
USD 0.042 per million input tokens, zero completion cost. Reported totals are
22,642 input and 1,248 output tokens; input × catalog price equals the reported
USD 0.000950964. All 32 optional generation audits were unavailable; mandatory
Decisions usage is present for every call. This does not fabricate independently
verified generation invoices. POST-only elapsed median is 0.2262075 s, mean
0.25586653125 s, minimum 0.197587 s, maximum 1.011693 s, sum 8.187729 s. Key and
generation GET times are outside those classification timings.

`first_score.json` preserves every row, denominator and paired contrast.
`first_evidence.zip` is an exclusive exact first-response archive, 141,937 bytes,
SHA256 `8723319f08a5f6590dcab8f697e60b551d6d6cb71df9cd8cab52dc5e9df5ca00`.
All 113 inventory payload hashes and the four executed-code hashes were checked.
Extracting to another path and running the included offline scorer reproduced
the identical JSON report. There are no keys or lock files in the archive.
Original prompts, gold, prepared manifest and executed source were not changed.
An independent reviewer reconstructed all 64 decisions and costs directly from
raw responses and gold, checked all 113 hashes (114 ZIP members including the
inventory), and reproduced the identical score with network/key loaders blocked.

## Decision and next hypothesis

**Keep** the first32 result and both query/format policies. **Investigate**
attribution and controlled source changes, which these T3 questions never
measured; **do not promote** a global Jev reliability or format winner from
perfect decisions on this small authored panel.

A second full neutral/nonsense-key panel could isolate instruction-key semantics
while holding object shape and all values constant, with the existing sixteen
cases × two remaining arms (32 calls, 64 decisions, USD 0.032 reservation).
r15 supplies a diagnostic signal for this follow-up, but it is selected after
first outcomes and therefore remains development, not validation. Before any
second outcomes, freeze the new arm IDs, code, source hashes and metrics, and
retain first32 as the immutable comparator. Do not retune thresholds or repeat
the original paid requests. A separate source-attribution/counterfactual panel
has higher semantic coverage value and needs new frozen source variants plus
epistemic labels before its first calls.
