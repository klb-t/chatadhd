# Task E — graph perspectives, 2026-10-09

Branch: `gpt/graph-perspectives-2026-10-09`.
Pinned main base: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`.
Inspected B: `b20c0d8ac37934e1600c3b9216492fd524220941`.

## Current checkpoint

Core checkpoint E1 is verified: 18/18 headless tests, 11/11 native-layer tests,
9/9 adapter groups and actual existing exporter + demo packet validation PASS.
The full web build passes. UI/browser and final integration gates are still in progress.
Only new paths under the four owner-assigned directories belong to Task E.
Root application files, shared schemas, runtime and C/D modules are not edited.
Repository STATE/INDEX remain unchanged under the owner's explicit ownership restriction;
this report is the Task E state and resume point.

## Reuse and boundary

- Native `DefaultLayers` resolves composition, overrides, disable and persistent exclusion.
  The browser consumes native effective resolutions, not a second fallback engine.
- Existing workspace `connect` and `changeParameter` retain parameter-specific propagation.
  The new extension preserves unknown profile fields outside the legacy workspace whitelist.
- Existing `KnowledgeGraph` renderer is private and embeds selection. A separately reviewable
  patch exports it and permits an already-selected projection. The harness applies that exact
  patch in memory. This is not a claim that B has applied it to the production application.
- Source references and adapter capabilities are first-class. A reference can be addressed
  without opening its source in a view. Graph packets are read-only projection inputs, not a
  second canonical database. Unknown recognition, unavailable sources and denied access stay
  explicit. Adapter implementations remain outside the perspective algorithm.
- UI visibility, explicit analysis selection and caller permissions are distinct.
  View changes perform no model calls or canonical writes.

## Acceptance work

1. Executable profile-to-query compiler, native layer explanation and open capabilities.
2. Stable reference/representation/version identity, navigation and history.
3. Local detail/aggregation independent of relation structure; explained visual mappings.
4. Interactive module/harness, safe data packets and actual existing native resolver.
5. Basic/Advanced/Expert shared documents; import/export and unknown-field preservation.
6. Headless, native, integration and browser tests; bounded sparse-graph measurements.
7. Comparison, selected workspace bindings, visibility inspector and explicit workflow export.

D branch was not available on the initial remote query. Its pinned packet compatibility
will be checked again before closing; authored fixtures are labelled as such.

No private exports, paid model calls, GitHub Actions runs or main-branch writes are needed.

## E1 verified core

Compiler, shared headless traversal/projection, open adapter capabilities, immutable packet
views, on-demand selectors, local aggregation, explicit budget omissions/continuation,
reference identity/navigation, loss-preserving perspective serialization and native R40 bridge.
Tests: `node loom/web/tests/graph-perspectives/{headless,native-layers,adapters,compatibility}.test.mjs`
(run each separately; Node does not expand this notation).
Source remains at the pinned main for existing files. No production UI hook is applied.
Next: browser harness, reversible preset history, native-backed panel bindings, then final gates.
