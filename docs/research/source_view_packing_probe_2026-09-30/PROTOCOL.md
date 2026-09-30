# Frozen DEV diagnostic: q01 question packing and deliberate replication

Registered after SOURCE_VIEW v1/v2 first responses were observed, but before
the first paid call of this probe. Prior DEV labels and source-view outcomes
are known to the reviewer. Selection is explicitly post-results, probability
based, and is not a blind validation design. No validation data are accessed.

## Observation, competing explanations and fixed selection

The original SOURCE_VIEW request intervention changed only q02 wording. Its
q01 request was identical across arms, but q01 probabilities differed in 20/48
pairs (maximum absolute delta 0.05); no q01 threshold decision changed.
Potential explanations include joint-question packing/context effects,
independent-call variation, or both. There is no relied-upon contractual claim
that a question's probability is independent of other packed questions. This
probe measures descriptive behavior; it does not identify an internal cause.

Select the three largest absolute q01 deltas from the retained original paired
DEV compiled outputs. Resolve ties by lexicographic query ID. Add the
lexicographically first query with an exactly equal q01 probability as the
stable control. Decimal subtraction avoids float-sensitive tie selection.
The source file hashes and selected values are retained in `selection.json`.

| Original query | Previous q01 v1 → v2 | Absolute delta | Selection role |
|---|---:|---:|---|
| `svlv1_dev_001_q4` | 0.15 → 0.20 | 0.05 | Largest observed delta |
| `svlv1_dev_012_q2` | 0.07 → 0.10 | 0.03 | Second largest |
| `svlv1_dev_005_q3` | 0.06 → 0.08 | 0.02 | Third; lexical tie break |
| `svlv1_dev_001_q1` | 0.97 → 0.97 | 0 | Exact stable control |

This selection enriches for observed q01 differences, so later contrasts are
not representative population estimates. Labels are never selection inputs or
sent to the model; the reviewer already knows prior DEV labels and does not
claim blindness.

## Conditions and 24 distinct physical measurements

| Condition | Questions | Source and q01 |
|---|---|---|
| A | Original historical q01 + q02 | Exactly preserved |
| B | Original q01 + active-refutation q02 | Exactly preserved |
| C | q01 alone | Exactly preserved; q02 absent |

Four queries × three conditions × **two deliberate physical measurements per
cell = 24 planned requests**. Both measurements are part of this preregistered
stability/packing design. They are not recovery retries and do not overwrite or
reissue an errored first response. Any unavailable response remains unavailable;
no replacement call is added. All original SOURCE_VIEW outputs remain unchanged.

The original state text—including the inner `query.id`, source timestamps,
speaker, direction, physical prefix and node inventory—is byte identical in
all conditions and both measurements for that selected query. q01 is byte
identical everywhere. A/B reuse their original complete question definitions;
C removes only q02. No labels, explanation, source transformation or trace
metadata enter the body. Outer `case_id`/manifest ID identifies the distinct
physical measurement; it is absent from the body and does not replace the
original inner query ID. Exact repeated bodies intentionally share their body
hash while keeping unique outer IDs.

Requests are ordered as two measurement rounds. Each round contains all four
selected queries in the retained selection order and all three conditions in
fixed A/B/C order. This reduces a simple whole-round time separation; it is
**not randomization** and cannot exclude provider/time/order effects. The
unchanged model/provider/price pins are those of the bounded Jev runner.

## Measurement, unavailable denominators and decision

Retain every first raw receipt/probability/hash/cost. For q01, report each of the
two probabilities and mean, minimum, maximum and range for every cell. Compare
the A/B mean difference with the within-A and within-B ranges; also report
C−A and C−B means and all within-condition ranges. With two measurements,
ranges are descriptive, not estimates of stable stochastic support. Preserve
all q02 values for A/B without introducing a new label-quality score.

No gold accuracy is computed. Missing measurements keep the planned two-cell
denominator; available-only summaries are explicitly labelled, and a mean
contrast is omitted unless every compared cell has both measurements. Artifact,
identity or billing mismatch is fatal to replay; it is never converted into an
unknown probability or a semantic abstention. An unavailable model response is
recorded without a fabricated value.

Decision rule is **investigate**: identical paired values cannot prove packing
independence, and a between-condition difference from this selected n=2 design
cannot prove a causal packing effect. Consistent larger between-condition
contrasts relative to observed within-condition variation can motivate a new,
separately frozen design with more measurements and broader unselected inputs.
If differences are comparable with within-condition variation, preserve that
result without attributing the earlier change to packing. No threshold tuning,
global model reliability, truth promotion or production adoption follows.

## Resource authorization and replay

The plan requests the existing **nonresetting global USD 2 budget**. It creates
no new allowance. Twenty-four requests reserve USD **0.024**, within the
existing USD 0.10 batch ceiling; root checks the live shared remaining budget
before execution. No key is read and no network request is made by the planning
or scoring module. Root alone prepares/runs the immutable manifest through the
unchanged bounded Jev runner; its preparatory public endpoint lookup is distinct
from paid calls.

An authored offline preparation test establishes that the existing runner
accepts repeated equal bodies under distinct outer IDs and permits the q01-only
question inventory. It uses a synthetic endpoint response and makes no real
request. `source_view_packing_probe.py` then scores preserved results offline,
checking endpoint identity/hash/price aliases, manifest and raw-response hashes,
request-body equality, returned question inventory, raw/ledger probability
consistency and fatal per-receipt/total billing mismatch. `freeze.json` pins the
protocol, selection, inputs, request, policy, module/tests and replay dependencies
before root's paid call.

Mechanism tests and their initial numeric-display correction are retained in
`FIRST_MECHANISM_RESULTS.json`. They establish implementation behavior, not
Jev quality. No sealed validation, source-view gold mutation, original recipe
edit, API call or canonical graph update occurs during this preparation.
