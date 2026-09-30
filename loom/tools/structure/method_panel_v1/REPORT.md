# Pair-method panel v1 — measured results and decisions

**Text similarity is an unreliable substitute for the declared argument/relation
skeleton on these 48 pairs.** The frozen learned text embedding recovers all
16 paraphrases at cosine ≥0.5, but also accepts all 16 structural foils and
misses 10/16 cross-domain transfers. Existing source-derived envelope graphs
supply useful additional candidates, while their sparse coverage and lost P/Q
identity remain decisive limitations. No method here authorizes a Claim merge,
truth transfer or canonical-graph promotion.

This is an **exploratory comparison on reused authored material**, including
both inherited splits. It is not a holdout result or a measure on real user
conversations. The fixture contains eight correlated families × two languages ×
three contrasts: 32 positive pairs and 16 negative foils. Individual pairs and
translations are not independent trials. Gold tests the authored operation-and-role
abstraction, not lexical/semantic similarity or complete logical equivalence.

## Freeze, evidence and independent inputs

`PROTOCOL.md`, `policy.json` and `freeze.json` precede every new model-free score.
All eight pair/source fixture files and five extractor/graph dependencies matched
current remote Git blob hashes (`3a3c6236`). Raw inputs and gold match their
fixture SHA-256 hashes. No holdout-key content, model/provider API or credential
was read by this panel. IDF fits only frequencies from 64 unique exact passage
texts (unlabelled/transductive); no threshold, feature or weight was changed after
seeing these results. Character TF-IDF is **not a learned embedding**.

Independent per-passage graph/formula/role annotations: **0/48 available**. The
pair fixture has q01 labels and source IDs; the original source fixture has
question labels, not grounded graphs. A graph inferred from family/gold would be
an oracle construction, so supplied-annotation role/WL/alignment quality is
**unavailable**, rather than fabricated. The measured raw channels below use
existing frozen extraction outputs, separately labelled unassessed projections.

The learned embedding is a separate arm, frozen without gold by another agent:
pinned multilingual MiniLM, local CPU ONNXRuntime, 64 vectors of 384 dimensions,
0/64 texts truncated, 48/48 scores available. Its original score artifact keeps
`prediction:null`; vectors/model/weights were not adjusted. The addendum protocol
preceded quality evaluation and reused the main arm's generic thresholds. This
is a learned **text** embedding, not a recovered argument-graph representation.
Its model/vector/runtime provenance is retained in `local_embedding_panel_v1/`
and linked by hashes in `embedding_addendum_results.json`.

## Fixed primary operating point: cosine ≥0.50

Precision denominators are predicted positives. Planned recall always retains
all 32 positive pairs, including extraction abstentions. Represented recall is
reported separately in machine-readable results. Undefined precision is `—`,
not a zero-quality estimate.

| Method | Available pairs | TP / FP | FN represented / positive abstentions | Precision | Recall on all positives | AUROC (available positive×negative comparisons) |
|---|---:|---:|---:|---:|---:|---:|
| Always yes | 48/48 | 32 / 16 | 0 / 0 | 32/48 = 66.7% | 32/32 = 100% | 0.500 (512) |
| Always no | 48/48 | 0 / 0 | 32 / 0 | — (0 predictions) | 0/32 | 0.500 (512) |
| Token-count cosine | 48/48 | 15 / 16 | 17 / 0 | 15/31 = 48.4% | 15/32 = 46.9% | 0.10254 (512) |
| Character 3–5-gram TF-IDF | 48/48 | 2 / 14 | 30 / 0 | 2/16 = 12.5% | 2/32 = 6.25% | 0.02148 (512) |
| Directed word-bigram TF-IDF | 48/48 | 0 / 12 | 32 / 0 | 0/12 | 0/32 | 0.09570 (512) |
| Equal word/character kernel | 48/48 | 0 / 12 | 32 / 0 | 0/12 | 0/32 | 0.06055 (512) |
| Bounded raw graph roles / WL / alignment | 0/48 | 0 / 0 | 0 / 32 | — (0 predictions) | 0/32 opportunities | unavailable (0) |
| Raw envelope role counts | 12/48 | 8 / 4 | 0 / 24 | 8/12 = 66.7% | 8/32 = 25.0% | 0.68750 (8×4=32) |
| Raw envelope directed WL, two rounds | 12/48 | 8 / 4 | 0 / 24 | 8/12 = 66.7% | 8/32 = 25.0% | 0.68750 (32) |
| Raw envelope exact alignment | 12/48 | 7 / 2 | 1 / 24 | 7/9 = 77.8% | 7/32 = 21.9% | 0.68750 (32) |
| Nongating union of model-free channels | 48/48 | 17 / 16 | 15 / 0 | 17/33 = 51.5% | 17/32 = 53.1% | 0.27832 (512) |
| Frozen learned multilingual text embedding | 48/48 | 22 / 16 | 10 / 0 | 22/38 = 57.9% | 22/32 = 68.8% | 0.00977 (5/512) |

The always-yes baseline has **higher precision and recall** than token cosine,
the new word/character kernel, the learned embedding and the nongating union at
this fixed operating point. These methods must not be presented as successful
structural classifiers merely because they return stable similarities. Envelope
alignment trades greater precision for very low full-target recall; its 7/8
represented recall must not be substituted for the planned 7/32.

Tie-block average precision: token cosine 0.48267; character TF-IDF 0.46411;
word paths 0.48690; word/character kernel 0.47243; model-free union 0.61176;
learned embedding 0.46174. The all-positive constant-score reference is 32/48 =
0.66667. Raw graph channels have AP 0.76389 on only eight represented positives
and four negatives; their 36 omitted pairs remain explicit. These are different
ranking denominators, not directly comparable full-coverage quality estimates.

## Contrast breakdown at the same fixed threshold

| Method | Paraphrase TP /16 | Domain-transfer TP /16 | Foil FP /16 |
|---|---:|---:|---:|
| Token cosine | 2 | 13 | 16 |
| Character TF-IDF | 0 | 2 | 14 |
| Directed word paths | 0 | 0 | 12 |
| Word/character kernel | 0 | 0 | 12 |
| Raw envelope exact alignment | 0 (1 represented) | 7 (7 represented) | 2 (4 represented) |
| Raw envelope roles / WL | 1 (1 represented) | 7 (7 represented) | 4 (4 represented) |
| Model-free nongating union | 3 | 14 | 16 |
| Learned text embedding | 16 | 6 | 16 |

The texts deliberately place vocabulary-sharing structural foils near their
bases. This explains why lexical and text-embedding rankings can be almost the
reverse of this **structural target**. It does not establish that TF-IDF or this
encoder is generally useless for topic retrieval, nor that similarity should be
inverted into a new classifier. Gold-aware sign reversal or a newly selected
threshold would be tuning on these reused cases. All family/language/inherited
split results are preserved; both split labels remain exploratory.

## Frozen threshold sensitivity; no best cutoff selected

Cells are TP/FP. Every row uses the four generic cutoffs declared before scores.

| Method | 0.25 | 0.50 | 0.75 | 0.90 |
|---|---:|---:|---:|---:|
| Token cosine | 29/16 | 15/16 | 3/11 | 0/2 |
| Character TF-IDF | 13/16 | 2/14 | 0/6 | 0/0 |
| Directed word paths | 11/13 | 0/12 | 0/0 | 0/0 |
| Word/character kernel | 13/16 | 0/12 | 0/2 | 0/0 |
| Raw envelope exact alignment | 7/2 | 7/2 | 7/2 | 7/2 |
| Model-free union | 30/16 | 17/16 | 10/11 | 7/2 |
| Learned text embedding | 28/16 | 22/16 | 10/16 | 0/12 |

The learned arm rejects some cross-domain transfers before it rejects any
structural foil. Raising its similarity cutoff therefore does not rescue this
target. Source graph alignment has discrete 0/1 scores; the identical threshold
results are a property of this mechanism, not four independent replications.

## Counterexamples and channel disagreements

Two positive pairs missed by token cosine enter the model-free union through a
graph channel, without lexical veto:

- `jp_28bf8d09143db45c`: Polish causal domain transfer, heater/water versus
  removed support/falling beam. Token cosine 0.18257; envelope alignment 1.0.
- `jp_d5d00e152f385b62`: English supporting-evidence paraphrase. Token cosine
  0.42720; envelope role cosine 0.83666 and WL 0.78174. Exact alignment rejects it:
  one source yields two envelopes, the other one. This is an extraction/projection
  count mismatch, not proof that the underlying arguments differ.

Both conditional reversal foils, `jp_4919e90b05331dde` (PL) and
`jp_879f4d33d712a1fe` (EN), match exactly in the raw envelope graph. Explicit
`P → Q` versus `Q → P` source slots have fixed `condition`/`consequence` labels,
but the graph's node labels omit the opaque slot text P/Q. Isomorphism can map
the vertices and reports 1.0. Thus the representation has erased a required
correspondence **before** WL/alignment runs. The independent mechanism test
already captured this limitation before first scores; the experiment confirms
it in the corpus. A stronger matcher cannot recover information its projection
has discarded. No fixture-specific repair was made in this arm.

There are 23 pairs with opposing available channel decisions at 0.50. Every
case's scores, positive/negative channels and gold are preserved; disagreement
is diagnostic, not an automatic negative veto. The union rescues two positives
but retains all 16 foils. It is an explicit recall-oriented candidate policy,
not a quality guarantee or graph merge operation.

Raw-source coverage: bounded grammar recognizes 2/64 unique passages, but no
pair has both sides represented; explicit envelopes recognize 21/64 passages,
25 envelopes, and both sides of 12/48 pairs. Coverage is grammar coverage,
not independently measured extraction correctness. Gold supplies no per-span
annotation from which extraction precision/recall could honestly be computed.

## Keep / reject / investigate

- **Keep** frozen text methods as transparent complementary retrieval controls;
  preserve cosine scores without equating topic proximity to shared reasoning.
- **Reject as a structural replacement on this evidence** the word/character
  kernel and unqualified learned text-embedding cutoff. The simpler always-yes
  policy wins at the declared primary point; threshold sensitivity and rankings
  do not provide an independent rescue.
- **Keep for investigation** raw graph channels and nongating candidate union.
  They expose useful cross-domain signal with explicit coverage, but their current
  projection/coverage does not meet the declared full-role target.
- **Next experiment:** obtain independent grounded argument-role/formula graphs
  and a new corpus with direction, negation, quantifier, evidence/endorsement,
  analogy and domain-transfer controls. Preserve explicit role correspondences
  before comparison; separately measure extraction and matching. Freeze that
  representation and protocol on development/mechanism cases, then evaluate
  unseen annotations/texts. Do not derive oracle graphs from these 48 pair labels.

## Verification and limits

New synthetic mechanisms: **10/10** pass. Frozen recomputation reproduces
**48/48 score rows** exactly; fitted-frequency hashes, four original first-run
artifacts and frozen arm source/policy/protocol hashes match. No first result is
overwritten. Current-remote existing structure suite: **436/436** pass, 34 verified
test modules, temporary paths outside Git. No existing source/test gate changed.

Initial broad discovery is retained: 447 attempted tests, 13 errors. Twelve used
a default temporary root rejected by the existing private-path guard; one was an
IndentationError in an old untracked semantic-sketch test absent from the current
remote index. The verified-module inventory excludes that old file and one other
old untracked module; root will relocate them after repository hydration. This
is restoration/test-isolation evidence, not a regression attributed to the new
panel. The corrected run uses `TMPDIR=/var/tmp`, not a weakened path guard.

Not verified: independent generalization, noisy conversations, true extraction
quality, pretrained encoder coverage/contamination, role-grounding beyond bounded
envelopes, robust orientation/long-context behavior or production performance.
Historical/live Jev and GPT comparison arms are integrated separately by root;
this panel makes no new paid call and does not substitute old reported accuracy
for independently verified new-arm measurements.
