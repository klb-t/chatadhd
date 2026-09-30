# Agentic graph methods — verified handoff, 2026-09-30

## Implemented and measured

- `loom/tools/structure/agentic_graph_v1/packet.py`: single native-record-preserving
  graph exchange projection, open definitions and operation intents, explicit
  reversible record diffs, preview/auto acceptance as data, full old/new/source
  history, origin and known_at preservation, stale CAS protection, exact-head undo.
- Packet/diff Draft 2020-12 schemas, with code enforcing the additional
  source/hash/Assessment/history consistency rules. **8 packet instances and
  6 diff instances** independently validated against the generated schemas and
  packet runtime where applicable; no model-quality metric.
- `workflow.py`: compositional arbitrary stage list/mixed model labels, first
  response preservation before parsing, no retry, source/current context,
  raw versus instrument-bound proposed diffs, original/current alternative
  projection selection, configurable acceptance and resource/authorization data.
- **38/38 targeted tests passed** in top-level unittest discovery after the
  relative-import, freeze CLI path and cycle fixes. First earlier **36/36** test
  log retained. Full native CTest passed **74/74** (including structure **762/762**)
  after the import fix, before the last two new regression tests/cycle guard;
  that count belongs to the independent `validation_replay` agent's timestamp,
  not a claim that the later package snapshot was native-retested.
- Philosophy agent independently checked **27/27** packet cases against an
  earlier byte-hashed snapshot. It exposed cyclic Python input nontermination;
  the first 0.2 s counterexample receipt is kept in its own directory. The packet
  now rejects dict/list cycles while permitting shared acyclic values. Its latest
  independent rerun is reported separately by that agent.
- Scripted 3-stage first transcript persisted, including full native inputs,
  every first response, raw model diff, bound diff, preview, receipt and selected
  projection. **New paid API calls: 0. Paid cost: 0.** This is a mechanism example.

## Separate optional cheap source-only review diagnostic

`experiment.py` reuses complete immutable FREEv1/v2 batches, without reissuing
paid baseline calls. The fixed12 selection was saved before new FREEv2 outcomes;
the earlier FREEv1 transport/syntax failures were already known. Exact captured
FREEv1 first outputs give **9/12 wrapper-compilable objects**, not nine semantically
correct graphs. **11/12** stopped first text responses are eligible for explicit
new review; the failed/truncated transport remains unavailable. Record-level
invalid assertions/events are retained by the unchanged compiler. Complete raw
syntax-invalid text is data for a declared new stage, never a repaired baseline.

The **11 GPT-mini review requests are prepared**, one capped batch with
reservation **USD 0.0923928**, against the **same existing non-resetting USD 2**
session budget. No new review has been executed by this agent. A paid frontier
pilot is not authorized. Root alone may execute cheap calls if the actual key,
current owner authorization and total remaining ledger budget permit them.
Baseline costs remain in their original parent ledgers; do not double count them.

Prepared manifest:
`prepared_review_gpt41mini/batch01/prepared/manifest.json`.
Transfer: `baseline_v1_transfer_first.json`.
Frozen source-only recipe/scorer dependencies: `METHOD_FREEZE.json`, SHA256
`4f42f728141c81d51553f1d347046b944ff71b96122cf05e90a42d2776a80a4a`.
Packet/scripted mechanism snapshot: `PACKET_MECHANISM_FREEZE.json`, SHA256
`ab70044639eb5b2378d12a7b6d277b141b476ca6b29d30ad82b6758a29c85fff`.

The first freeze CLI failed before calls because a relative model config path
was not resolved before `relative_to(ROOT)`. Its empty stdout and failure receipt
are retained; the path was fixed and a direct regression test added. No quality
gate or threshold was lowered. First native discovery import failure is retained
in `graph_composed_validation_v1/native_baseline/ctest_first.log`.

## Limits and next useful steps

The codec binds source bytes and native shape; it does not prove a directed edge,
attribution, world truth, useful novelty or model calibration. Auto acceptance
selects a projection without secretly changing origin/evidence. Native import,
the entire archive, local/remote mixed-model execution and practical cost/latency
remain separate tests. The scripts do not write the native canonical store, and
the packet is not a mandatory schema/database for the entire ecosystem.

Root's AnalysisPlan and frontier adapter consume this single packet interface.
The frontier adapter independently tested raw-versus-bound packet diff preview
and exact undo using scripted responses. Those integration counts belong to its
own reports. A useful next experiment is an authorized cheap review of the
preserved fixed12 proposals with unchanged source-only metrics and an independent
semantic audit of additions/removals/overcorrections. Frontier live runs await
explicit authorization; richer real-archive quality needs imported owner data.
