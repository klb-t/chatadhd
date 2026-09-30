# Individual-source commitment projection: first DEV results

The new optional source-commitment view scores **95/96**, versus **92/96** with
the same citation-bound graph and original event application policy. Availability
is **96/96** for both. It gains `gpv1_dev_001_q1`, `003_q1`, `004_q1`, loses no
previously correct answers, and retains `006_q2` as the sole error. Paired counts:
92 both correct, 3 projection-only correct, 0 baseline-only correct, 1 both wrong.
No model calls, threshold changes, assertion filters or gold/phrase repair occur.

The policy is explicit data with `source_scope=individual_source_commitment`.
Exact equal attributed-source strings are required for a superseded event to
remove an older assertion in this view. Three cross-source events are withheld
from the view while **all nine raw candidate events** remain byte-equivalent
objects in its audit. Every typed assertion is unchanged. The original global
event projection, all earlier outputs, canonical source and raw model ledger
remain intact; no event extraction denominator or quality score is erased.
The unchanged upstream strict extraction score remains assertions **60 TP / 2 FP /
0 FN**, precision 60/62 and recall 60/60; status events **0 TP / 9 FP / 6 FN**,
precision 0/9 and recall 0/6 under the original strict gold convention. The known
replacement-event convention ambiguity remains in the upstream report. The
95/96 downstream query result cannot be substituted for that event score.

| Class | TP | FP | FN | Precision | Recall |
|---|---:|---:|---:|---:|---:|
| supported | 36 | 0 | 0 | 36/36 | 36/36 |
| refuted | 24 | 1 | 0 | 24/25 | 24/24 |
| unknown | 35 | 0 | 1 | 35/35 | 35/36 |

Macro recall is 0.990741; macro precision over three defined classes is 0.986667.
The remaining query mistakes a reporter's nonendorsement of another person's
statement for an explicit negative implication. Its full-turn source locator is
valid but its typed claim is semantically unsupported. That error is retained;
source bytes are evidence locators, not semantic or world-truth proof.

The named policy concerns individual commitments, not universal correction
rights. A different domain may allow an authorized speaker to retract another
speaker's institutional statement; authority is unresolved outside this view.
No aliases are invented: different strings remain different sources until an
explicit identity policy supplies a justified mapping. Same-source corrections,
alternative replacements and latest explicit negatives still work. Malformed
time/reference/binding events are delegated unchanged to the frozen lookup and
can remain unavailable; withholding does not sanitize bad provenance.

`projection_audit.applied_in_view` records candidate retention in the derived
view; the lookup's separate `eligible_status_events` records actual application
at the query cutoff. Future candidate events in the audit are not past knowledge.
The source known_at filter remains a retrospective projection because extraction
saw the complete conversation. Source known_at is not model claim availability,
no causal prefix comparison with raw judges is claimed, and upcoming validation
families do not independently test temporal or cross-source authority. Oracle
mechanism success is not a model-quality observation.

Before predictions, 86/86 relevant mechanism/driver tests passed. Independent
post-result counting recounted all class denominators, independently reconstructed
all three withheld-event reasons from attributed strings, and checked **61**
selected witness paths against raw compiled records, source Unicode/UTF-8 spans,
source timestamps and query cutoffs. Frozen new files are unchanged; original
binding/direct freeze checks and raw-response receipt checks remain enforced.
Full structure regression passed **664/664** in 13.852 s using writable /var/tmp;
its complete output is preserved in `source_commitment_projection_v1/structure_regression.log`.
This includes concurrently added tests from other authorized agents. Tests use
fabricated credentials; no real key or sealed validation was read.

**Keep as a named optional view policy; do not promote it to a global authority
rule.** The observed three gains support this policy's intended development
semantics, not independent generalization. The original projection remains an
explicit competing mode. The next useful research question is whether source-only
extraction and causally prefix-only extraction produce the same typed commitments;
those need separately frozen model experiments, not further tuning on this error.

Reproduce from repository root into new paths:

```sh
python loom/tools/structure/graph_source_commitment_projection.py freeze --output /tmp/source-freeze.json
python loom/tools/structure/graph_source_commitment_projection.py predict --freeze /tmp/source-freeze.json --output /tmp/source-predictions.json
python loom/tools/structure/graph_source_commitment_projection.py score --freeze /tmp/source-freeze.json --predictions /tmp/source-predictions.json --output /tmp/source-score.json
PYTHONPATH=loom/tools/structure python -m unittest test_graph_source_commitment_projection test_graph_direct_binding_ablation test_new_graph_direct_lookup test_new_graph_direct_lookup_driver test_graph_panel_live test_graph_panel_score_run
```

Immutable first freeze/predictions/score/integrity are in
`source_commitment_projection_v1/`. Timestamp-bearing replay predictions differ
in computation time; compare the unchanged semantic decisions and provenance.
