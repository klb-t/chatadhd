# Direct lookup citation-basis ablation: first DEV results

Using the same immutable GPT responses, the citation-bound graph raises direct
lookup accuracy from **88/96 to 92/96**, with availability **96/96** in both arms.
The unchanged lookup policy gains four correct answers and loses zero:
`gpv1_dev_016_q1`, `016_q2`, `017_q2`, `018_q2`. Paired counts are 88 both correct,
4 binding-only correct, 0 original-only correct, 4 both wrong. There were zero
new API calls or model changes. This is an outcome-informed DEV replay.

The intervention is only the upstream evidence binding basis: unchanged source
turn text replaces the model's copied quote under the frozen explicit-turn-ID
binder. The exact direct lookup code, policy, typed model assertion fields and
actor/event application rule remain unchanged. All original quote hints and
source provenance are preserved in each case's trace and the hashed sidecar.

| Class | TP | FP | FN | Precision | Recall |
|---|---:|---:|---:|---:|---:|
| supported | 33 | 0 | 3 | 33/33 | 33/36 |
| refuted | 24 | 1 | 0 | 24/25 | 24/24 |
| unknown | 35 | 3 | 1 | 35/38 | 35/36 |

Macro recall is 0.962963; macro precision over three defined classes is
0.960351. The four restored judgments concern Polish corrections whose copied
model quotes had prevented assertion/event acceptance. The binder recovers
locators; it does not assert that the source semantically supports every claim.

The four residual errors are unchanged: `001_q1`, `003_q1`, `004_q1` lose an
older attributed assertion through an invented cross-speaker supersession,
and `006_q2` mistakes nonendorsement for a negative implication. Neither was
repaired from gold or phrases. These remain composed-pipeline errors despite
lookup faithfully executing the supplied graph.

Both graph arms came from full-conversation extraction, then source known_at
filtering. They remain **retrospective** and are not information equivalent to
raw judges that physically saw query-specific prefixes. Source timestamps are
not model claim availability. The separate authored-oracle 96/96 result is a
mechanism control, never extraction quality or world-truth accuracy. Sealed
validation and causal prefix extraction were not accessed or verified.

73/73 relevant tests passed before the new freeze and predictions. The entire
upstream binder receipt chain, raw response/source hashes, and all frozen direct
code/policy/protocol/tests were checked without drift. A separate counting
algorithm independently recounted both score dictionaries and reconstructed all
57 original / 58 citation-bound witness paths from source characters, UTF-8 byte
spans, typed fields, known_at and query cutoffs. `integrity_first.json` preserves
these checks and file hashes. Full structure regression passed **612/612** in
13.498 s. Its first sandbox run retained 12 credential fixture setup errors
because Python fell back to /tmp under a Git parent; the superseding run used
writable /var/tmp without code changes. Tests use fabricated credentials only.

**Keep as an opt-in locator improvement; investigate semantic authority.** The
observed gain diagnoses four development quote-copy failures, not generalization
or global model reliability. A separately frozen individual-source commitment
projection is the next parent-approved ablation; these first outputs stay intact.

From repository root, using new output paths:

```sh
python loom/tools/structure/graph_direct_binding_ablation.py freeze --output /tmp/binding-freeze.json
python loom/tools/structure/graph_direct_binding_ablation.py predict --freeze /tmp/binding-freeze.json --output /tmp/binding-predictions.json
python loom/tools/structure/graph_direct_binding_ablation.py score --freeze /tmp/binding-freeze.json --predictions /tmp/binding-predictions.json --output /tmp/binding-score.json
PYTHONPATH=loom/tools/structure python -m unittest test_graph_direct_binding_ablation test_new_graph_direct_lookup test_new_graph_direct_lookup_driver test_graph_panel_live test_graph_panel_score_run
```

The immutable first artifacts are in `direct_binding_ablation_v1/`: freeze,
predictions, score and integrity. Replay computation timestamps differ; compare
decisions and provenance while preserving original timestamp-bearing files.
