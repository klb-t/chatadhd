# Independent audit of the eight completed key-control calls

2026-09-30. The preserved first score establishes **8/8 completed calls and
16/16 available decisions**, with **USD 0.000249312** provider-reported usage.
The earlier `HANDOFF.md` still describes an unexecuted plan; it is a preserved
pre-run document, not the current execution status. This audit adds no API call,
secret-file read, validation access, retry, original artifact edit or graph write.

## Integrity and exact offline reproduction

All **43 archive payload hashes and byte lengths** match `INVENTORY.json`.
The zip has 44 members including the inventory itself; no duplicate or unsafe
path was accepted. All eight raw receipt hashes, costs, returned probabilities,
model/provider identities and ledger values were independently reconciled.
Costs sum exactly to USD 0.000249312 and each raw cost equals input tokens ×
USD 0.042 per million.

The archive was relocated to a fresh temporary directory. Running its preserved
scorer with key environment variables removed reproduced `first_score.json`
**byte for byte**, including all 32 contrasts and the unchanged 16 selected
first32 comparator rows. Both score hashes are
`c9bdb0f9a4f582b5752376cbb1c0018ac74f0707fc6ab684caf56aca603c27fe`.
The immutable first32 comparator retains SHA-256
`28a297a834749d369334a189d0aacc4a8cb7786b861ddd64f5387452c72bcfa4`.
This validates replay and receipt accounting, not an external invoice audit.

## Denominators and the simpler baseline

The 16 new binary decisions comprise four true positives and twelve true
negatives, with zero FP/FN. The original T3 threshold is **`>= 0.5`**; it is
preserved and differs from the graph panel's strictly-`> 0.5` ternary adapter.

| Task, per arm on the same four cases | TP | TN | FP | FN | Gold positive | Predicted positive |
|---|---:|---:|---:|---:|---:|---:|
| Expressed | 0 | 4 | 0 | 0 | 0 | 0 |
| Inferred | 2 | 2 | 0 | 0 | 2 | 2 |

These counts hold for **all four** compared arms: original meaningful object,
original string, new neutral object and new nonsense object. Every arm has
4/4 hard accuracy per task. There is no hard-label accuracy improvement to
attribute to the new key names.

All four expressed labels are negative. Expressed precision and positive
recall are **undefined**, rather than 100%. An all-negative baseline already
has 4/4 expressed accuracy and Brier score **0**, better than any arm's
nonzero expressed Brier. Inferred has two positive and two negative labels,
so its constant-label baseline is 2/4. Combining tasks gives the all-negative
baseline 12/16; this combined count must not hide the absent expressed-positive
denominator.

| Same four-case arm | Expressed Brier | Inferred Brier |
|---|---:|---:|
| Meaningful object, earlier comparator | 0.09925 | 0.099375 |
| String, earlier comparator | 0.123525 | 0.05985 |
| Neutral object, new first calls | 0.08895 | 0.05865 |
| Nonsense object, new first calls | 0.08375 | 0.06465 |

Decimal recount reproduces these values. Lower Brier on four selected cases is
a descriptive result, not evidence of a general winner or calibrated model.
For example, r13's neutral expressed probability is 0.47 and nonsense is 0.37;
both remain negative against gold 0. Every raw probability is retained in the
JSON companion, so unchanged hard labels do not conceal these differences.

## Control scope and epistemic limits

For each selected case, the original request archive confirms identical source
state, Noul type, criteria and **instruction value sequence** across meaningful,
neutral and nonsense object arms. Only the instruction key names change. The
original string arm is contextual; changing object to string is a representation
change and is not a key-only comparison.

The four cases r06/r13/r15/r16 were chosen after first32 outcomes. This is
targeted development with two new calls per selected case, not independent
validation. Each object arm has one physical response per case; temporal and
independent-call variation remain alternative explanations for probability
differences. Hard-label agreement is not a proof of semantic correctness or a
repeated-call stability measurement.

The inferred label concerns a scoped working interpretation or candidate
extension. A yes does not establish a fact, author intent, hidden shared
implementation or world truth. Source observation, interpretation and proposal
must remain distinct. No architecture, production threshold or model-global
reliability estimate is promoted by this audit.

`AUDIT.json` records all eight receipts, exact replay hashes, all 16 new rows,
the eight task/arm recounts, four key-only control checks and all 32 preserved
contrasts. First results, executed code, original fixtures and comparator files
remain unchanged.
