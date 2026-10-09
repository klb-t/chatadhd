# E → B: additive renderer hook and host integration

Pinned base: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`.

`integration.patch` changes only `loom/web/src/components/KnowledgeWorkbench.tsx`.
It is supplied for B; E did **not** edit that file. Apply with
`git apply --check docs/reports/graph-perspectives-2026-10-09/integration.patch`,
then review/apply the same patch in B's branch. The renderer transform test
compares the reviewable patch result with the bytes actually executed in the
isolated harness.

The hook exports existing `KnowledgeGraph` and adds optional `projection`.
Without it the existing filtering, neighborhood fade and controls retain their
behavior. With it the renderer accepts the exact completed headless result:
there is no second query, filtering, sort or budget truncation in the renderer.
The five-column grid remains the existing layout. Positions persist by asserted
canonical identity; simultaneously visible representations use explicit nearby
slots, and changing the sole snapshot retains the canonical slot. Visual styles,
labels, status and explanations arrive as data. Keyboard, click and touch use the
same existing node/relation callbacks. No graph store or settings engine is added.

## Host API

Import `PerspectiveView` and `createKnowledgeGraphBridge`, and pass the host's
existing `KnowledgeGraph` export to the bridge. `PerspectiveView` receives:

- `initial`: a `loom.graph_perspective/1` intent document, preserving extensions;
- `catalog`: descriptors, presets, labels and optional snapshot choices from data;
- `adapters`: trusted source adapters exposing descriptors, resolve and neighbors;
- `permission`: the caller's explicit access policy, separate from perspective;
- `resolve`: asynchronous **native** R40 resolution yielding `NativeResolution`;
- `Renderer`: the existing KnowledgeGraph bridge.

`native-layers.ts` provides `NativeLayerClient` over the current `OnboardingAdapter`
getSnapshot/dispatchLayer contract. The host controls native read/write ownership;
the view does not own a fallback resolver. The local harness applies baseline
`components` overrides followed by the complete chronological `layerActions`
journal to actual `loom::onboarding::DefaultLayers`, so `override`, `disable`,
`exclude`, `reenable`, `clear_override` and update exclusions retain native order.
This journal contains intent, not a parallel graph or a replacement profile store.

A production host should bind its existing workspace panel identity to the
perspective document and use the supplied workspace helpers for individual
parameter bindings. Exported plans are preparation for an existing analysis
workflow; rendering never authorizes an analysis or provider call.

## Executed integration and remaining B work

Executed: production `PerspectiveView` → real native C++ default resolver through
local JSON transport → shared compiler/selector → existing KnowledgeGraph after
applying the exact reviewable patch in Vite memory. The safe fixture is a real
`loom.graph_packet/1` packet (native codec validation is a separate test), read via
`createPacketAdapter` with source descriptors. The live-provenance scenario projects the exact latest native defaults response through
`createNativeResolutionAdapter`, including effective setting, value, layer and source
of override. Its values change with the same real native journal. Native application
profile loader integration is exercised separately; the authored packet is labelled
a safe fixture, not a live external source.

Remaining B hook: mount the component inside the existing workspace, provide its
live resource/runtime adapters and native layer host, and save the additive
perspective alongside that workspace. No main-application navigation entry has
been installed, and a working harness is **not** claimed as complete application
integration. The patch only exposes the reusable renderer required for that mount.

## Reproduce

From repository root:

```sh
node loom/web/tests/graph-perspectives/harness-server.mjs --port 5179
```

Open `http://127.0.0.1:5179/src/graph-perspectives/harness.html`.
The server binds loopback, has no provider/model calls, and compiles the native
resolver to a content-addressed temporary path before serving. Root Vite config,
root package scripts, B's source files and shared contracts remain unchanged.

`window.__graphPerspectives` is harness-only diagnostics, exposing current exact
selection, an independent rerun of the same headless selector, original packet
hash and current packet bytes for immutability checks, adapter materialization
counts and timing. It is not used to choose UI data.

Selection time measures source traversal/projection. Layout time is measured
inside the reused renderer grid calculation. React Profiler duration covers its
render/reconciliation including that layout and is **not** claimed as browser
paint time. Browser update wall time must be measured separately by the E2E
benchmark after animation frames. No timing is a production SLA.

## Review and analysis controls

The view compares the current perspective with its saved document, explains an
address without changing focus, exports the compiled plan, and displays structural
property differences for snapshots of the same asserted identity. Snapshot IDs
are opaque: left/right columns do not invent chronology. Local resolution fields
are produced by headless projection and displayed identically in cards/inspector.

An optional host `prepareAnalysis` callback uses the existing native retrieval-plan
contract through `workflow.ts`. The user explicitly chooses `exact` or `anchors`.
The harness has no verified native-store anchor resolver, so its workflow export
honestly stays blocked with unsupported refs; it never treats fixture packet IDs
as proof of native-store membership. Raw perspective plan export remains usable.
No export executes a request, changes analysis membership, or expands permissions.
