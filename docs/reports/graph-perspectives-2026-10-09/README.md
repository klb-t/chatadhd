# Task E — executable graph perspectives, 2026-10-09

Branch: `gpt/graph-perspectives-2026-10-09`.
Pinned main: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`.
Published core checkpoint E1: `3eaac2953c3ee0d01d085e595d68edc091c284f2`.
The final publication receipt is `checkpoint.json`; it identifies the implemented
commit separately from the later documentation receipt. `gates.json` records exact
SHA-256 hashes of tested source, test and data files, independent of commit metadata.

## Delivered and ownership

The additive TypeScript module, renderer-free API, working React view, interactive
local harness, native R40 bridge, data profiles, tests and measured evidence are
implemented. All changes are new files in the four owner-assigned directories.
No existing application/runtime files, shared schemas, root configuration, C/D
modules, main or other branches were modified. STATE/INDEX remain unchanged under
the explicit ownership restriction; this directory is E's state and resume point.

This is an executed harness integration with the existing renderer and native
resolver. B has **not** mounted it in the main application. The exact required
renderer patch, base SHA, application test and host API are in
[integration.md](integration.md) and [integration.patch](integration.patch).

## Executable module

- `index.ts` exports the headless compiler, selector, adapters, navigation,
  persistence, version comparison, workspace bindings, visibility inspector and
  analysis-plan preparation. It imports no React renderer.
- `compilePerspective` consumes effective native resolutions and capability
  descriptors; it reports effective values, origins, exclusions, errors and
  unsupported capabilities. Native `DefaultLayers` remains the only R40 resolver.
  The bridge compiles existing C++ sources; cache identity includes compiler flags
  and the compiler-discovered header closure.
- `selectPerspective` performs bounded, paginated demand traversal and projection.
  A selector works before opening its source or drawing a node. Structures,
  relation vocabulary and additional adapter parameters come from descriptors.
  Canonical identity, representation, address and snapshot remain distinct.
- Local detail, field projection, aggregation and visual mappings are data.
  Relevance, time and uncertainty have separate explanations. Parser-generated
  confidence is not treated as calibrated certainty without explicit mapping.
- `PerspectiveView.tsx` consumes that same result. Basic presets and Advanced/Expert
  editing share one loss-preserving intent document. Full Back/Forward, import/export,
  snapshot comparison, saved-perspective differences and visibility inspection work.
  Existing `KnowledgeGraph` supplies layout/rendering after the exact patch is
  applied in Vite memory; canonical layout slots persist across view changes.
- Existing workspace `connect`/`changeParameter` implement selective bindings.
  Composite bindings require current native resolutions and produce native override
  actions, preserving other fields and permanent disable/exclusion semantics.

There is no second canonical graph database, workbench, parser or fallback settings
engine. The packet adapter's optional transient row indexes do not copy source
content into a native store. Demand adapters need no full packet or upfront node
list. Embedding, persistence, caching and snapshots remain independent host choices.

## Executed scenarios

| Scenario | Executed path and evidence |
| --- | --- |
| A: content → computation → inputs → result → rationale | Safe native packet; mouse/keyboard navigation and recorded evidence. Decision links have explicit source/inference status. |
| B: one object, physical and logical structures | Same canonical identity and stable slot; common selector with data-described relation structures. |
| C: two snapshots | Old value 5 and current value 7 share identity, retain distinct representations and appear in the structural property diff. Old version can be the focus. |
| D: external profile → field → effective setting → override | Existing application-profile loader exercised headlessly. Interactive live-native projection reads the actual latest R40 response and its exact override record; changing 80 to 96 preserves identity. |
| E: conversation → source message → correction → current instruction | Labelled safe packet with explicit evidence/status; same engine and interactive controls. |
| F: unavailable/unopened/unknown object | Addressable placeholders preserve identity and explicit state. Absent content is not presented as empty content. Permission refusal performs no source read. |

The same resolution is tested against computational and history/comment structures.
One selection shows detailed focus, direct inputs and aggregate context. Local
exceptions are independent of relation filters. Unknown fields roundtrip;
unsupported options stay in the document with diagnostics. Budgets retain omission
reasons and adapter continuation cursors.

## Actual upstream integration and boundaries

| Dependency | Pinned evidence / limit |
| --- | --- |
| Main | Base above; workspace, profile loader, retrieval-plan builder, GraphPacket codec, native DefaultLayers and renderer reused. |
| B | Read-only review at `362cb8b951de23bdea989f37e67d014e62861342`. B must mount this component, provide live adapters/native ownership and persist the perspective alongside its workspace. |
| D | Actual `ResourceGraph` at `1d3d133154f213733b7a69af863613cec2dd8ca2`: attach performs zero reads, demand selector `/instrument/samples/1` returns 8, 9-entity/14-claim packet passes native validation and E projection. No E parser or native-store import. Persistent app transport belongs to B. |
| C/research | Existing `method_graph_export_v1` at pinned main executed with its safe authored fixture: 8 entities/10 claims, native validation and generic E mapping. C's current research projection description is not a ready packet; private exports were not loaded. |

A previously unseen sensor structure changes only adapter/descriptor data, not
perspective core. D's arbitrary fields work through RFC6901 mapping descriptions.
Unknown recognition and raw/alternative metadata remain available. Automatic
online discovery and model-assisted parsing are host/D capabilities; E does not
simulate or dispatch them. See [adapter-contract.md](adapter-contract.md).

Live native projection exposes the current response. Historical native response
reading needs a host snapshot reader and explicitly reports unavailable without
one; safe packet snapshot comparison is exercised separately.

## Analysis, visibility and access

Visibility, explicit analysis membership and permission are independent. View
changes never mutate canonical data, authorize a provider call or broaden access.
Omitted/aggregated/denied data is explained. Denied metadata is excluded from
relation projection and native workflow-plan provenance.

`workflow.ts` prepares existing `ChatContextPlan` with `executed: false`. The caller
chooses `anchors` or `exact`. Existing retrieval anchors are not a membership
allowlist: native retrieval may add contextual records. Exact mode therefore
returns blocked, not a misleading plan. Anchor mode requires verified host
native-store IDs; the fixture harness has none and reports that limit. Raw compiled
perspective-plan export works. Headless tests cover successful preparation with
verified IDs and explicit access. See [contracts.md](contracts.md).

## Reproduce

Requirements: Node/npm, Python 3 with `jsonschema`, a C++17 compiler and Playwright
Chromium. From a checkout of this branch:

```sh
git fetch origin gpt/resource-graph-2026-10-09
cd loom/web
npm ci
npx playwright install chromium
node tests/graph-perspectives/run-all.mjs
```

The runner executes eleven gates, writes logs/source hashes and fails on any failed
gate. `--no-browser` and `--no-benchmark` are explicit local subsets, not the final
delivered gate. No CI workflow or provider/model call is required.

Start the interactive module from repository root:

```sh
node loom/web/tests/graph-perspectives/harness-server.mjs --port 5179
```

Open `http://127.0.0.1:5179/src/graph-perspectives/harness.html`. The server binds
loopback. No root configuration needs editing. Headless callers execute
`compilePerspective(intent, nativeResolution, capabilities)` then
`await selectPerspective(plan, adapters, permission)`; `headless.test.mjs` and the
test loader provide executable Node examples of that same API.

## Verification and measurements

Final counts/ranges are in `checkpoint.json`. Detailed evidence:

- `gates.json` and eleven `.txt` logs: native, headless, adapter, workspace,
  producer compatibility, exact patch plus transformed TypeScript, existing
  workspace/application-profile tests, web build, browser and benchmark.
- `browser-evidence.json`: thirteen required groups, page errors, requests,
  headless/UI parity, mouse/keyboard/touch, Back/Forward, persistence, unknown data,
  suppression, actual version values, plan downloads and integrity checks.
- `screenshots/`: six images from the running tested module; supporting evidence,
  not substitutes for executed functionality.
- `benchmark.json`: twelve lazy sparse cases, sizes 10,000/100,000, chain/star,
  three focus positions. Query budget 256, render budget 80, page size 32; only
  9–256 objects materialized. Selection excludes layout/render. Browser evidence
  records layout, React duration and wall update through two frames separately.

Memory values are signed process heap/RSS deltas affected by GC/JIT, not exact
retained object sizes or production SLAs. A separate 10,001-entity packet hub test
covers lazy indexing and page-before-clone behavior: first page materializes zero
objects and three relations. Its cost is reported separately from lazy-source
selection timings in the adapter report.

## Resume point

E's implementation and further queue are complete within current contracts:
perspective comparison, selective bindings, why-visible/hidden, explicit workflow
preparation and executed producer compatibility are included. Resume with B's
host mount and live-resource/native-store wiring in `integration.md`; apply/rebase
the minimal patch against its stated base. The harness is not already installed
in production. No new C/D commits are needed to complete E's delivered scope.
