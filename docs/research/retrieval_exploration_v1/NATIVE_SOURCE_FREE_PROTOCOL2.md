# Native source-free protocol instrumentation extension 2

This extension is recorded before any actual native process has read a source. It leaves source serialization, 48 planned case/arm denominators, binary, pack, native stages, policies and measurement definitions unchanged from NATIVE_SOURCE_FREE_PROTOCOL.md.

First instrumentation attempt preserved all 48 unavailable rows in first_run_ledger.json and all per-case executed configs. No native child started: /usr/bin/time is absent in this execution environment. These rows are unavailable measurements, not zero extraction accuracy. First-source freeze, first ledger, and first code are retained unchanged. The earlier fixture-envelope prepare failure also occurred before native outputs and is retained in PREPARE_FIRST_FAILURE.json.

The second launcher uses a fresh Python supervisor for exactly one native CLI child. It forwards stdout/stderr bytes without recoding, records monotonic wall time around child execution, and reads Linux resource.getrusage(RUSAGE_CHILDREN) after child completion for CPU and peak RSS. This excludes supervisor CPU; outer series wall time includes supervisor startup, import, SQLite snapshots and file writes. Child supervisor returns the native exit status. Parent timeout kills the whole process group. No retry is substituted for a first actual native result.

Authoritative second freeze: freeze_before_outputs2.json. Exact first actual native bytes: first_runs2/. Full ledger: first_run_ledger2.json. All output hashes are frozen in freeze_before_evaluation2.json before the source-grounding/inventory scorer runs. Semantic relation scoring remains unavailable unless a legitimate typed/actor/polarity adapter exists.

Mechanism tests verify byte-perfect binary stream forwarding, preserved child failure code, nonzero actual child usage, plus the source wrapper/UTF8 grounding controls. These tests are not model-quality measurements.
