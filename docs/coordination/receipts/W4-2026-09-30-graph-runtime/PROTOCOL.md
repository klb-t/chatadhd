# W4 local graph runtime validation protocol

Base: b118c80e981c08ec6d7f9ab6aacc177979186cf2. Development inputs are synthetic
repository fixtures. No private exports, sealed validation, model calls or native
store writes. Operator path: AnalysisPlan -> resource reservation -> graph diff
preview/apply -> preserved first result -> fenced coordination receipt.

Before final evaluation, run all coordination, contract and GraphPacket/workflow
mechanism tests. Independent reviewer attacks concurrent processes, expiry,
configuration changes and late effects. Count test methods per suite, not summed
with historical CTest entries. Record failed environmental invocation: default
Python lacks jsonschema; use existing verification/deps via PYTHONPATH.

Performance experiment fixed before measurement: five samples, each an isolated
new temporary database, output and resource ledger; synthetic packet with one
claim status patch, preview policy. Compare raw graph algebra (decode input,
apply + preview), full cold operator subprocess (interpreter/import, file reads,
DB init, validation, ledger/artifacts, completion and JSON output), then same
completed-task invocation (deduplicated status, no new graph callback). Record
all sample durations plus medians. This difference is total path overhead, not
isolated SQLite overhead, and deduplication is not an independent result cache.
Report callback CPU/wall scope separately. No benchmark thresholds or tuning on
these measurements; filesystem cache and small synthetic input limit inference.
