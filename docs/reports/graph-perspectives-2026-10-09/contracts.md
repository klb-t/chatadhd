# Additive contract boundaries

Base inspected: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`.

`workspace.ts` delegates all parameter binding traversal to existing
`workspace/state.ts::connect` and `changeParameter`. Its data mapping descriptor
names an existing parameter and either a perspective component or focus. Each
parameter has one declared meaning in an envelope; conflicting mappings and
unsupported future parameters remain explicit diagnostics with their source
descriptor preserved. The same component mapping drives every reached pane.
An optional RFC6901 `pointer` selects a field within a composite value, such as
traversal `/hops`, without coupling that panel's relations or direction. Each
pointer composition requires a caller-supplied `NativeResolution` per affected
pane, and rejects missing or suppressed effective values. Authored components
and action journals are never used as a fallback merge base: they can differ
from effective values after `override` or `clear_override`. The host supplies
current snapshots and retains native transaction/CAS responsibility.
Each
component change appends an existing native `LayerAction` override intent to
`perspective.layerActions`; `nativeResolutionRequired` explicitly means the
host must resolve these through `NativeLayerClient`/DefaultLayers before using
the compiled plan. Focus is transient navigation and does not create a layer
override. Native disable/exclusion always wins; helpers never emit `reenable`.
It does not create another binding graph, settings resolver or graph database.
Snapshot changes use `changeParameter(..., "run", ...)`, not
`setWorkspaceRun`, so the existing selected identity remains intact.

The `loom.graph_perspective_workspace/1` envelope stores raw perspectives beside
the existing `loom.workbench/2` workspace. Unknown perspective/envelope fields
are preserved; the existing workspace parser still validates its own contract
and retains its documented whitelist behavior. R40 resolution is not performed
here. Native exclusion and disabled records can be preserved as source data;
their effective use is determined only by the native layer adapter/compiler.

`comparison.ts` compares arbitrary perspective/plan fields with RFC6901 paths.
Visibility inspection uses the common reference/canonical identity helpers and
selection omissions. It distinguishes an exact visible representation, a
different representation or aggregate, recorded omission, and incomplete
selection. A missing result never establishes that the object does not exist.

`workflow.ts::preparePerspectiveAnalysis` builds the existing `ChatContextPlan`
through `context/retrieval-plan.ts::buildRetrievalPlan`. Only
`perspective.analysis.selected` supplies anchors. Displayed nodes, focus and
presentation budgets never become implicit analysis membership. The caller
must explicitly choose `selectionMode: "anchors" | "exact"`. Native targets
and claims are retrieval anchors, not an allowlist: the existing context engine
can additionally retrieve principles, preferences, dependencies, project data
and candidates. This is disclosed in the export and native plan provenance.
Exact requested membership is blocked with no plan because the existing
ChatContextPlan contract cannot enforce that allowlist. No local R40 resolver
or fabricated permission constraint is substituted for the missing capability.
The caller
must explicitly supply a permission context and a native anchor resolver;
`canonicalId` alone is not accepted as proof of a native database ID. Denied or
unmaterialized references remain in explicit unsupported records and original
selection provenance in the local review sidecar. Denied references are excluded
from native plan metadata as well as anchors; that metadata records only their
count. A supplied complete query plan stays in the local sidecar, because its
original source perspective can contain references outside permission scope.
Empty supported membership produces no plan, preventing
ambient native retrieval inheritance.

Exports are preparation only (`executed: false`), not approval, dispatch or a
privacy-policy change. A partial export must be reviewed before execution.
Permissions must be revalidated by the existing runtime at execution. Visual
rules and arbitrary external selectors are retained as provenance; they are not
silently translated into native analysis semantics.

Read-only contract audit additionally inspected B at
`b20c0d8ac37934e1600c3b9216492fd524220941`: catalog resource projection and
external RuntimeProfile paths exist there, while generic discovery, composed
nested reference selectors and transparent ordinary-chat hydration remain
open. These utilities do not claim those host integrations.

Focused test command:

```sh
cd loom/web
node --test tests/graph-perspectives/workspace.test.mjs
```

The suite tests selective binding/cycles, preservation across snapshot changes,
unknown/disabled records, rejected bindings, immutable inputs, explicit analysis
membership, permission filtering, unsupported reference retention, plan compiler
semantics, differences and incomplete-selection explanations. It is a headless
suite and does not claim browser or native permission enforcement coverage.
The composite-binding case executes the real native DefaultLayers bridge, then
compiles the returned resolution: linked hops change the effective plan while
panel-specific relations/direction survive, disable remains ineffective, and
excluded override is rejected.
