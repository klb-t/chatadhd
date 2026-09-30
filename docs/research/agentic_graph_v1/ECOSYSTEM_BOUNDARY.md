# Native graph packet in the ecosystem

`ECOSYSTEM.md` (2026-09-30) is a brainstorm, not an integration order. It
explicitly leaves database, application and graph semantics open. The packet in
this directory is an adapter for **Loom's native Conceptual Model/JSON records**.
It does not impose that model on iOmatrix, WatchDog, AGEDS, LEM, PixelSpace or a
different graph. Native Claim Assessment, predicates and source locators retain
their domain semantics.

An AnalysisPlan or provider invocation should identify its semantic contract,
domain, supplied source permissions and capabilities in task/method data. A
cross-domain adapter must describe what it preserves, drops or proposes; it must
not label a lossy mapping as native roundtrip or silently merge identities.
No cross-project source access, action permission or integration is granted by
the packet alone. The same codec can support different Loom projections without
making those projections independent truth stores.

Potential uses — not implemented integration claims — include reviewing an
AGEDS chronology against its exact sources, proposing WatchDog research gaps,
explaining iOmatrix learned preferences with their origin and correction history,
or exchanging a selected subgraph/annotation with another view. Shared memory
need not be owned by the chat interface; a request/graph remains interpretable
without assuming ChatADHD is the mandatory intermediary for every operation.
