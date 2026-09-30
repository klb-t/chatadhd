# Frozen free schema-hint v2: no new model measurement

The original protocol, adapter, tests and two manifests were frozen at
2026-09-30T12:42:15.841426Z. The freeze SHA-256 is
`5ba4887f481ac8f2c0f0e3bba3da39575f32ae1f38936817b30de306f3cd7b0a`;
all 15 pinned files matched when independently checked. Eleven independent
mechanism tests passed. Two 12-request manifests reserve USD0.0625456 and
USD0.0627312 as planning values only.

After this freeze, root recovered the current owner's explicit instruction in
`docs/HANDOFF_SOL_2026-09-29.md` §3: no paid model calls on synthetic data; live
runs wait for the owner's imported archives. **No v2 live request was sent.**
The prepared manifests remain unexecuted. The frozen protocol is retained as
the first preregistration rather than rewritten after this change of execution
authorization.

The previously saved source-only v1 run is the actual observed comparator:
24 first calls with known reported cost USD0.0263512, 18/24 compiled outputs,
strict assertion alignment TP6/FP43/FN54. Those representation-dependent
counts are not a semantic hallucination rate. V1 output bodies differ from v2;
their actual responses cannot stand in for a v2 quality measurement.

Scripted v2 compiler/ledger checks, if produced, are separate mechanism evidence
and explicitly labeled `scripted_not_model`. They never enter real usage,
success-rate estimates, model profiles or the actual first-response archive.
