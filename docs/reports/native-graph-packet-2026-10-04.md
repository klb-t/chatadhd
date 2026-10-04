# Thread 4 — native GraphPacket, 2026-10-04

Branch: `gpt/native-graph-packet-2026-10-04`. Base: `161cc22`
(current public main, includes PR9). No changes to main, STATE, profiles, UI,
selector, database schema or existing GraphPacket store.

Delivered native packet creation/codec, diff preview/apply/inversion and full
backwards/forwards history checks; deterministic `loom.graph_reply/1,/2`
compilation with exact first-byte captures and host-computed UTF-8/code-point
spans. Draft semantic links remain draft; acceptance does not establish truth.
API: `loom_packet` and `POST /api/packet`.
[Commands, policy, boundaries and reproduction](../../loom/src/packet/README.md).

Optional `usage_estimate` uses thread 2's admission/reservation/settlement API
and binds the exact command. Without that dependency an explicit estimate
fails unavailable. No fabricated resource measurements or fixed codec ceilings.

| Check | Before | After |
|---|---|---|
| Full CTest | 108 entries in PR9's historical receipt; not rerun baseline here | **110/110**, 117.71 s |
| Native packet/FFI | No native algebra/graph-reply suite | **4/4** native cases, **24/24** assertions; **11/11** FFI cases |
| Python reply parity | Python-only compiler | Five pinned compilations agree with C++ |
| Shared-policy integration | No packet caller | 6/6 synthetic scenarios |
| Packet HTTP | No route | **6/6** synthetic scenarios |

Implementation: `4e4ceba`. [Pinned source/binary hashes and test logs](../../loom/src/packet/tests/evidence/2026-10-04/manifest.json).

All data are synthetic. Zero paid calls and zero GitHub Actions runs. Native
numbers are not model-quality measurements. The first build needed vendored
SQLite because the environment lacks system SQLite development files.
The isolated policy link check sets thread 2's config preset explicitly; its
initial failed run omitted that preset. It does not claim a fully merged build.

Integrator: select after thread 2; rebase onto fresh main and retain PR9's store
route/receipts. Thread 3 still owns actual chat/provider-request wiring. UI, live
model-quality tests, cloud calls and automatic canonical knowledge acceptance
were not done: they are outside this lane. Canonical writes remain the existing
explicit `loom_graph_packet_store` operation.

Thread 2 currently declares `loom_usage_policy_json` for static callers without
shared export registration. Integrator/client owners must expose that existing
confirmation API if the chosen UI uses FFI; this lane does not register a
second confirmation engine.
