# Resolved packet receipt: corrected harness 23

2026-09-30. Preserve22's frozen source/script and observed incomplete harness
receipt. Its expectation wrongly treated loaded_packet_first.json as the raw
packet rather than the explicit `{loader_supplied, packet}` envelope, so its
callback assertion failed before mutation and its later hash assertion stopped
the harness. This was an audit fixture error, not a producer defect.

Same authored packet, policy and tests now compare the retained packet envelope
and its digest. The raw packet remains separately identified inside it. Retain
the exact original envelope before callback mutation, correct explicit source
verification status on dispatch/replay, one loader/callback only, and rejection
of changed retained-envelope bytes. No source semantic/fingerprint verification
claim or real provider call follows. Capture a separate source/freeze/outcome.
