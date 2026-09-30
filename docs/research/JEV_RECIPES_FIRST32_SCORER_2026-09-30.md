# First32 analysis and durable replay

Scorer contract frozen 2026-09-30 before reading this experiment's LIVE responses.
The first32 input selection remains unchanged. This scorer reads only the
existing T3 synthetic gold, already known development material. It does not
read the new independent panel or adapt decisions/thresholds to any LIVE result.

For each of the planned 64 decisions retain case/task/arm/language/authored
split and original p. p >= 0.5 is positive; selective p <= 0.2 or p >= 0.8.
Report available TP/TN/FP/FN, planned positive/negative denominators, missing
positive/negative counts, precision (TP / observed predicted-positive count),
observed recall (TP / observed gold-positive count) and planned recall (TP /
all planned gold-positive count). Missing cases are unresolved, not invented
negative predictions. Report planned and observed accuracy separately; Brier,
clipped log loss and equal-width five-bin ECE use observed probabilities only,
with bin counts. Group by task, arm, language, authored split and task × arm.
All-positive/all-negative baselines use all planned labels. These are task
baselines, not model calls. Keep per-case rows for error analysis.

The primary paired contrast is meaningful minus string accuracy and Brier, by
task. Use only complete observed pairs; retain planned/complete pair counts and
unresolved IDs. Bootstrap cases with replacement, seed 29092026, 2000 replicates,
percentile 95% interval. These intervals summarize the small authored panel;
they do not establish a population winner or blind validation.

The execution CLI's current exit code can be zero on a stopped series. Inspect
ledger `stopped_reason`, completed/attempted counts and missing denominator.
Preserve this current code version after manifest preparation; changing it
would invalidate code hashes. Any later CLI correction belongs to a new code
snapshot, retaining the executed one for replay.

After root execution, copy exact first response bytes, immutable start/terminal
receipts, ledger checkpoint, prepared plan/manifest/catalog snapshot and the
executed source code into an exclusive compressed archive in repository
documentation. Include a SHA256 inventory; exclude credential files and locks.
Include frozen T3 requests.zip and the known-development gold needed for offline
replay. Commit the archive before ending this session. The archive is evidence;
do not replace it with rerun-generated responses after a workspace refresh.

Replay may score an extracted copy with explicit `--run-dir`; it does not load
a key or contact any endpoint. Preserve the original manifest `run_dir` for
identity, even when reading evidence from an extracted path. To resume execution
after restore, first restore the original receipts/checkpoint into the canonical
runner location; do not execute a prepared manifest against an empty directory.
The batch is a single experiment under the existing shared USD 2 key limit,
never a new allowance on restore.
