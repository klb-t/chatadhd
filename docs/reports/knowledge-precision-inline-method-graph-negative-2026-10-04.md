# Rejected automatic inline method graph — 2026-10-04

Full proposed source is retained on this archive branch, based on main 30ad7d3 and W1 50e6bb9. This is NOT the accepted W1 runtime default.

The actual native ScriptedTransport semantic execution measured candidate JSON 2,117 → 350,630 bytes (165.63×), full graph 348,497 bytes, stats 385,137 bytes, checkpoint 373,144 bytes, and unchanged cached model content 446 bytes. Physical SQLite disk usage was not measured. The owner requires confirmation for expected ×10 consumption; no such confirmation was obtained. The implementation duplicates a complete capsule per candidate and checkpoint. A cold new W2 graph-only resource has no legacy baseline and does not protect this augmentation. Cache persistence was also outside that admission. These findings reject automatic durable embedding, despite functional tests.

Corrected functional run: 24/24 native cases, 370/370 assertions, 8/8 real N3 native store accept/read/restart/replay controls, no live/paid calls. This uses the main N3 GraphPacketStore plus the existing Python packet algebra; it does NOT prove W4 C++ packet execution or the joint W3/W4 golden gate. First run: 22/24 cases, 366/369 assertions; wrong fixture assumptions about ordered JSON equality and exact request-message template representation. Exact sources for BOTH runs, full receipts with stdout/errors, commands, source/library hashes and native receipts are retained losslessly under loom/src/extract/tests/evidence/2026-10-04/inline_graph_negative/. run1 log is the complete original JSON output, byte identical to its evidence file.

Reproduction: unpack run2-sources.tar.gz over the checkout (paths are relative to repository root), configure/build the normal native core/shared library, then:
```sh
python3 loom/src/extract/tests/method_graph_semantic_probe.py --build-dir loom/build/dev --evidence /tmp/inline-method-graph-replay.json
```
Use run1-sources.tar.gz instead to reproduce the first fixture expectation failures. Gzip decoding reconstructs full JSON/raw logs, rather than summarized failures. Build artifacts are reproducible and excluded from this public source archive.

Accepted follow-up retains graph method/version/prompt/recipe/preset/parameter/result DTO APIs, source-validated previews, registry reader proof and native N3 store proof. Normal semantic candidates/cache/checkpoint remain at their previous representation. W3/W4 must provide immutable shared method/run/capture references and pre-persistence consumption comparison/confirmation, including cache writes. Do not reference a mutable checkpoint or stable ca row as a new-run capsule: persistence is INSERT OR IGNORE and checkpoint follows candidate writes. Acceptance does not establish model content truth.

## Do wątku 3/4
Use loom/src/packet/METHOD_GRAPH.md and loom.method_graph/1 / loom.method_run_trace/1. Resolve shared immutable storage, atomicity, hash validation, requested/observed model aliases and same-unit storage admission before enabling the archived default. No foreign source was edited.
