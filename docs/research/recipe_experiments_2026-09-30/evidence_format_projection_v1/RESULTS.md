# Evidence-format loss explains a large part of the first extraction deficit

On the **same original24 first source-only responses**, an alternate decoder
that only wraps exact unique turn-ID evidence strings recovered32 correct
reference edges and exposed6 additional source-invalid edges. No model was
called, no input prompt changed, no first result was overwritten, and the strict
primary compiler/scorer stayed unchanged.

| Measurement | Original strict decoder | Exact-string evidence projection |
|---|---:|---:|
| Compiled conversations / planned | 18/24 | 18/24 |
| Strict source-assertion TP / FP / FN | 6 / 43 / 54 | 38 / 11 / 22 |
| Strict source-assertion precision | 6/49 =12.24% | 38/49 =77.55% |
| Strict source-assertion recall | 6/60 =10.00% | 38/60 =63.33% |
| Used reference atom TP / FP / FN | 45 / 15 / 15 | 45 / 15 / 15 |
| Used reference atom precision / recall | 45/60 / 45/60 | 45/60 / 45/60 |
| Strict status-event TP / FP / FN | 0 / 6 / 6 | 0 / 6 / 6 |
| Original known model cost | USD0.0263512 | Same original cost; incremental cost0 |

All60 source assertions,60 used atoms,6 events and24 conversations remain in
their planned denominators. Six unavailable originals—including four duplicate
JSON graphs and two length truncations—remain unavailable. All prior accepted
typed records are retained byte-for-byte. No node/endpoint, timestamp, actor,
predicate, polarity or status policy changed.

The projection converted45 evidence items (mean45/24 =1.875 per planned case),
which yielded38 additional compiler-accepted assertions and5 additional events.
Three assertions remain structurally invalid: an unsupported `denies` predicate
in007 and010, and a self-loop in012. Do not repair these silently or remove their
FP contribution. Previously valid object evidence stays unchanged.

The saved nonblind manual source audit independently classifies the46 accepted
assertions as38 source-valid and8 source-invalid. All8 accepted strict FPs are
also manually source-invalid on this diagnostic panel;3 structurally invalid
records account for the remaining strict FP denominator. Of the38 newly accepted
edges,32 match reference and6 are source-invalid. This annotation join runs
after outputs persist and does not choose conversions or scoring labels.

The semantic failures remain material: reporter nonendorsement becomes denial,
and negated operand propositions are mistaken for a negative relation polarity
in paired English/Polish conditionals. Source bytes and exact timestamps alone
do not validate relation semantics. The6 accepted status events are annotated
as legitimate only under the previously disclosed alternative convention that
a later explicit denial supersedes the earlier positive assertion. The original
primary convention expects its different alternative replacement, so its
0TP/6FP/6FN is retained rather than recoded as a model success or failure.

Keep this operation as a separate **research decoding candidate**: it identifies
a large interface-format loss without altering original gates and makes the
remaining semantic faults inspectable. It is not a production promotion,
another model run, a blind validation result, a hallucination rate or a global
reliability estimate. The next informative model-input hypothesis is to
represent operand negation and assertion polarity as separate axes, then measure
its first responses while preserving this comparator.

Twelve independent synthetic mechanism checks passed before this projection's
first measurement. An additional independent count reconciliation reproduces
38TP/11FP/22FN and the exact32TP/6FP newly accepted split. The first code/policy
freeze, exact raw-response hashes, conversion pointers and original/derived
object hashes are stored alongside the first outputs. No sealed validation or
temporal holdout was read.
