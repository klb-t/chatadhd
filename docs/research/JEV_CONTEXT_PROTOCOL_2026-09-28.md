# Jev conversation-context development protocol

**Prepared, unrun.** The adapter prepares 48 independent causal prefixes from the
existing development split (eight bilingual conversations, four whole families).
It asks 128 Noul questions: 84 topic memberships and 44 memory-Claim selections.
There are 1–4 questions per request; complete canonical request bodies are
2,916–13,307 UTF-8 bytes, below the existing runner's 32-question/16,384-byte limits.
No source text or candidate needed truncation. No label file or validation result
was opened to design this projection. No API request, credential access, graph
write or GitHub Actions run is performed by the preparation tool.

This is a narrower alternative to the full-JSON conversation experiment. It tests
whether Jev can track supplied topics despite unrelated earlier conversation and
select useful old Claims. It does **not** test free topic discovery, exact spans,
onset/return boundaries, correction/contradiction/analogy edge extraction,
historical-availability classification or the full conversation response schema.
It does not establish synonym invariance: the corpus has bilingual versions,
not controlled synonym interventions. The separate structure-pair arm addresses a
different question.

## Frozen questions and source projection

`loom/tools/structure/jev_context_pilot.py` calls the existing, unchanged
`conversation_context_pilot_v1/materialize.py::project(case, ordinal)` once per
target. It never sends the full case or corpus. Every prior message and the entire
target message are visible, and no later message is visible. Every temporally
eligible old Claim is retained, including irrelevant and unknown-time candidates;
their Assessment support, original observations, endpoints, scopes and temporal
metadata are retained without lexical filtering.

Only these export fields are omitted or replaced mechanically:

- The original instruction requests full-JSON output. The adapter substitutes one
  fixed instruction for independent binary classification, including supported
  reference resolution, topical discontinuity, ambiguity and source-as-data rules.
- `base_hash` and `packet_hash` are removed from the model-facing packet view:
  they depend on the complete snapshot and can vary with hidden future changes.
- `excluded_context_counts` is removed because it describes withheld context.
- The outer schema identifies the derived Jev view. This is not a native packet
  replacement and must not be accepted as a graph mutation input.

The exact unmodified exports are retained locally in `causal_exports.json`, with
hashes in the audit. Full-snapshot hashes never enter the model request. Source
Assessment fields are original input provenance, not target relevance labels.
Candidate availability remains distinct from candidate relevance and known prior
availability. Unknown-time Claim selection does not assert historical availability.

For each topic sorted by its supplied opaque ID, one question asks whether the
target belongs to that topic by discourse meaning. Several topics may be true;
there is no argmax, exclusive choice or keyword prerequisite. For every eligible
Claim sorted by ID, one question asks whether that particular Claim is needed to
interpret, explicitly compare, or answer the target. Broad topical similarity
alone is explicitly insufficient. The fixed templates live in `TOPIC`, `CLAIM`
and `INSTRUCTION`; no wording is generated from gold labels.

`q01` is a request-local question position, **not a globally stable phenomenon**.
Use `question_map.json` to map each answer to `(case, message, kind, topic/Claim ID)`.
Do not aggregate scores by `q01` and call them topic- or memory-quality scores.
The evaluator-only split selects development cases and remains outside all states.
The map contains IDs and hashes, no labels. An oversized request fails rather than
silently pruning source text, history, topics or candidates.

## Frozen preparation and later live use

From repository root:

```sh
python -B loom/tools/structure/jev_context_pilot.py --output-dir /ABS/NEW/prepared
python -B -m unittest discover -s loom/tools/structure -p 'test_jev_context_pilot.py' -v
```

Preparation writes `inputs.json`, `question_map.json`, `causal_exports.json`,
`offline_manifest.json`, and `request.disabled.json` into a new directory. The
request is disabled. It specifies `typesafe/jev-1.13`, at most 48 calls, the existing
USD 2 overall key cap and USD 0.10 batch cap. The runner reserves USD 0.001 per
request: USD 0.048 for this arm, **not a measured cost or additional budget**.
Actual model alias, provider availability, balance, billing and endpoint snapshot
must be checked by the existing runner immediately before any separately activated
local live run. Keep all original first responses, missing/failed requests and
uncertain-attempt accounting; no paid retries are authorized by this file.

The prepared input is compatible with `jev_live_pilot.prepare` and its body
validator. Its generic `score` output is **not** a complete context evaluator:
request-local question meanings, separate selection tasks and exact message sets
need the mapping above. Do not send the map, exports audit, split, gold or offline
manifest as extra model state. Freeze a live manifest before calls; the offline
manifest is not a provider/billing manifest.

Frozen initial preparation:

| Item | SHA-256 of canonical JSON |
|---|---|
| Inputs | `e38268bf7c20ee59f73b3c8cd678fa7b7a0af4d206d5dd6ac44df1aa454a6af0` |
| Question map | `b102c3c07d6a1edfccd33d1eabd9acec7571249322219bc0a482620e9d5da4b8` |
| Unmodified causal exports | `0129910a6527152324357c966aea30a2cfa2b8250f572860c7747e386d7d660b` |
| Fixed prompt templates | `3c79e3fcfc2fa32d82ecfcbbdd58d2d87ca5779807817cecfb77c591bf87ff98` |

These digests exclude the newline used for JSON file storage. Source fixture,
materializer, runner and adapter code hashes are in `offline_manifest.json`.
Any subsequent prompt change needs a new experiment/version and fresh freeze.

## Evaluation fixed before inference

After the first-response artifacts are captured, a separate evaluator may map
development `memberships[].topic_id` and `selected_claim_ids` through the question
map. It must not reuse validation labels or transmit any labels to the model.
Freeze the following conventions before reading outcomes:

- Primary threshold is `p >= 0.5`. Report topic-membership and Claim-selection
  TP/FP/FN, precision, recall and F1 separately, alongside valid-output coverage,
  per-message exact-set accuracy, negative prevalence and always-false accuracy.
- Every one of 48 target messages remains in all-query exact-set denominators.
  Missing/invalid first responses fail exact-set scoring even for true empty sets.
  Missing positives remain FN; no failed request is silently treated as a valid
  all-false answer. Also report metrics conditional on valid responses.
- For Claim-set accuracy, show all-message results **and** the subset with at
  least one candidate. No-candidate messages are structurally empty and cannot
  establish learned Claim selection. Report Claim bit metrics over all eligible
  candidates, without counting unavailable Claims as model negatives.
- Undefined precision/recall/F1 stays null with counts. Report complete topic and
  Claim vectors, and the joint vector, rather than relying on bit accuracy.
- A predeclared exploratory selective view may use `p <= 0.2` and `p >= 0.8`;
  report retained coverage and errors, without treating scores as calibrated
  production confidence. No threshold tuning on validation.
- Break down by language and evaluator-side family. Bilingual versions and
  repeated prefixes are correlated; do not treat the 48 prefixes or 128 bits as
  independent samples. Four development families cannot establish generality.

Candidate selection is conditional on a fully supplied, allowlisted candidate
universe. It does not measure archive-scale retrieval recall, relevant Claims
missing from the export, graph search latency, long conversations or real-user
archives. The current development labels are authored judgments, not an unseen
independently adjudicated benchmark. Do not activate the validation split simply
because development calls succeed; freeze and review that stage separately.

## Verification

**8 unit tests pass.** They check all 48 bounded bodies and exact retention of
source/candidate fields; preservation of independent overlapping-topic questions;
unchanged request bytes when future messages, handles or future Claims are mutated;
rejection of extra export metadata; rejection rather than truncation at limits;
no gold/validation file or network access during preparation; manifest replay and
disabled activation. This verifies containment and preparation, not model quality.
