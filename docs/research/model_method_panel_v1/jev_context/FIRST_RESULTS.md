# Jev context development arm — first actual results, 2026-09-30

**Keep as a promising conditional selector; investigate its false positives.
Do not promote it to a general extraction method or production confidence.**
All **48/48 first requests** completed with HTTP 200 and valid response shape;
there were **0 missing/invalid units**, no stopped reason and no paid retries.
The unchanged scorer frozen before inference was executed once offline. No
model/key access, validation-label parsing, threshold change or corpus edit
occurred during this evaluation. Raw responses and the initial score remain intact.

## Instrument and scope

Requested `typesafe/jev-1.13`; all responses identify
**`typesafe/jev-1.13-20260917`, provider TypeSafe**. The frozen causal-prefix
recipe asks independent Noul questions for supplied topic membership and the
usefulness of each supplied temporally eligible old Claim. Eight synthetic
development conversations represent four bilingual families; 48 targets and
128 bits are correlated, not independent samples. This is not free topic/edge
extraction, archive retrieval, synonym invariance, historical-availability
classification or a globally reliable model profile. Labels are frozen authored
development judgments, not an independently adjudicated holdout.

Scorer SHA-256:
`41975d03937a9f15027eadab3f3ecb32a091a1cb4bdf3b52741963466c41c714`.
Source and test hashes match `../context_scorer_freeze.json`. The frozen bundle,
manifest, ledger and first-response hashes passed verification. A separate
mechanical read of all raw envelopes matched **128/128** answer values to score
rows and verified **48/48** response hashes; see `offline_score_audit.json`.
The first score SHA-256 is
`9d222317c309048b6f79a965c9913683f38d60095029211503a56f475cf260c0`.

## Primary results — unchanged p >= 0.5

| Task | Bits | TP / FP / FN / TN | Precision | Recall | F1 | Bit accuracy | Exact sets |
|---|---:|---|---:|---:|---:|---:|---:|
| Supplied topic membership | 84 | 36 / 1 / 0 / 47 | 36/37 = .9730 | 36/36 = 1 | .9863 | 83/84 | 47/48 |
| Eligible old-Claim selection | 44 | 16 / 2 / 0 / 26 | 16/18 = .8889 | 16/16 = 1 | .9412 | 42/44 | 46/48 |

Joint exact topic+Claim vector: **45/48**. Claim exact sets on the
candidate-bearing subset: **22/24**; the other **24** targets have no Claim
candidates and must not be used as proof of learned Claim selection. Coverage is
48/48 targets, 84/84 topic bits and 44/44 Claim bits, so conditional-on-valid
metrics equal the all-query metrics. Brier on these outputs: topic .0148071,
Claim .0612341; these values do not establish calibration.

The predeclared always-false baseline has topic accuracy **48/84**, Claim
accuracy **28/44**, topic exact **12/48**, Claim exact **32/48**, joint exact
**12/48**, and candidate-bearing Claim exact **8/24**. Positive recall is zero,
precision undefined. Jev improves this baseline while retaining all positives
in this development panel. No frozen comparable context lexical recipe was
found; graph-pair lexical/cosine arms answer a different task and cannot be
ranked numerically against this result. No post-outcome lexical recipe was tuned.

| Development family | Topic TP/FP/FN/TN | Claim TP/FP/FN/TN | Topic exact | Claim exact | Joint exact |
|---|---|---|---:|---:|---:|
| availability_and_unknown_time | 10/1/0/13 | 8/0/0/12 | 11/12 | 12/12 | 11/12 |
| correction_and_contradiction | 10/0/0/14 | 8/2/0/14 | 12/12 | 10/12 | 10/12 |
| late_project_onset | 4/0/0/8 | no candidates | 12/12 | 12/12 structurally empty | 12/12 |
| return_after_digression | 12/0/0/12 | no candidates | 12/12 | 12/12 structurally empty | 12/12 |

English: topics **18/0/0/24**, Claims **8/1/0/13**, joint **23/24**.
Polish: topics **18/1/0/23**, Claims **8/1/0/13**, joint **22/24**.
Per-family/language details and every decision are retained in `first_score.json`.

## First error analysis — no repair or relabelling

All three discordant decisions are false positives at the fixed threshold:

| Request ID | Candidate / kind | Noul | Frozen label | Diagnostic |
|---|---|---:|---:|---|
| `c_3553baf3e7eb-m_a7f80fb232c6` | `k_3f8cdf30a15f` / Claim | .60 | 0 | Polish request to update DB configuration after an explicit correction to PostgreSQL still selects old SQLite memory |
| `c_f69279d98b85-m_861d9cee96ff` | `k_67c710cbf045` / Claim | .62 | 0 | English counterpart retains the same obsolete DB comparison context |
| `c_64face0fd8ae-m_b9f8290151c9` | `t_6df51c1c4cfc` / topic | .50 | 0 | Question about whether Flint previously used port 8080 is assigned to API topic from the preceding message |

The first two are one bilingual structural phenomenon, not independent failures.
The supplied previous message already gives PostgreSQL; the authored label
judges old SQLite memory unnecessary for the subsequent operational instruction.
The model's broader notion of useful comparison context is a diagnostic
hypothesis, not proven internal reasoning. The third is a boundary score that
the predeclared `>= .5` must count as positive; no threshold was moved afterward.
`first_error_analysis.json` preserves request/candidate IDs, original probabilities,
response hashes/references, expected/predicted sets and the visible causal texts.
Transport/shape failure IDs: **none**; semantic failure IDs are the three above.

There are **12 eligible unknown-time Claim opportunities**, with **6** positive
selection labels; all 12 selection decisions agree with those labels. The Claim
and supporting Observation lack time on these cases. Selecting that Claim for a
historical question does not prove it existed earlier or that its content is
true: this arm does not ask or score the known-prior/unknown decision.

## Predeclared selective view — conditional result, not a new gate

At `p <= .2` or `p >= .8`, topics retain **77/84** bits with **0/77** errors;
Claims retain **27/44** with **0/27** errors. This keeps **33/36 topic positives**
but only **3/16 Claim positives**, alongside 44/48 and 24/28 negatives. The
selective recall of 1 is conditional on retained positives; if unretained units
were abstained from, positive coverage would be **33/36** and **3/16**. In
particular, the Claim selector cannot claim perfect recall at this selective
operating point. Scores remain uncalibrated and the primary result stays .5.

## Cost and timing — POST separated from accounting overhead

Usage-reported cost is **USD 0.005774496**, with cost present for **48/48**
attempts and **0** unknown usage-cost attempts. Usage totals: **137,488 input
tokens**, **2,496 output tokens**. All **48 generation billing GET lookups return
404/unavailable**. Thus `generation_verified_cost_usd = 0` means no independent
generation audit was available, not zero model cost. Key checks recorded zero
BYOK usage both before and after. Other concurrent arms can share the same key;
their aggregate balance deltas cannot be assigned to this arm.

The runner's per-attempt `elapsed_seconds` times POST plus serialization/local
response safety checks, stopping before semantic parsing and generation GET:

- Sum **11.919899 s**, mean **.248331 s**, median **.237424 s**;
  min **.190418 s**, max **.533052 s**.
- Sum of `finished_at - started_at`: **24.534730 s**. Its non-POST difference
  **12.614831 s** includes generation GET, parsing and persistence; it is not a
  separately measured pure GET latency.
- Successful key-check timestamps bracket **24.702831 s**. The path performs
  two key GET checks and 48 generation GET checks. Those GET calls have no
  individual latency timers in this frozen runner, so no precise model-vs-GET
  inference-time attribution is claimed.

No runtime/recipe was modified to improve these timings. The optional 404
generation audit is an operational cost to investigate separately, without
replaying paid first responses.

## Reproduction and next information

```sh
python -B loom/tools/structure/jev_context_score.py \
  --manifest docs/research/model_method_panel_v1/jev_context/live-prepared/manifest.json \
  --run-dir docs/research/model_method_panel_v1/jev_context/run \
  --prepared docs/research/model_method_panel_v1/jev_context/prepared \
  --output /ABS/NEW/score.json
```

The registered initial CLI output is `score_stdout.txt` / `score_stderr.txt`;
the first score was created once and not overwritten. Further value comes from
a separately frozen recipe or input intervention addressing over-selection of
obsolete memory and topic carry-over, then an untouched validation stage under
its own authorization/protocol. No present result supports automatic graph
promotion, historical fact assertion or substituting this panel for extraction
and retrieval evaluations.
