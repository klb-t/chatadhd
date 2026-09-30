# N6: consistent default native run lookup

Source inspection at `926072c` found that `capi_knowledge.cpp::latest_run`
looks for a completed run only among the latest 50 runs, then falls back to an
unfinished one. Runtime chat and context already query status before LIMIT.

Protocol before measurement: one completed empty catalog run, then 51 newer
synthetic running rows; compare public `loom_kb_query` automatic and explicit
run selection through the preserved da77 shared library. Expected automatic
selection is the completed run. Preserve first results and instrument/library
hashes. No private inputs or provider calls. Correct only the completed-run
lookup; retain newest-run fallback when no completed run exists.

The probe is `loom/tools/coordination/probe_latest_native_run.py`. Native
regression tests will separately check automatic selection and explicit run
override plus fallback. Their execution belongs to the shared post-freeze gate.
