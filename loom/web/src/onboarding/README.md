# W12 onboarding UI integration

This directory exports `OnboardingPanel` and `WhatAppKnows` from `index.ts`.
W10 owns the navigation and `App.tsx` integration. Instantiate one stable
`OnboardingAdapter` outside React rendering (or memoize it), then pass it to
either view. The views contain no production transport, simulated storage,
policy preset, provider allowlist or hardcoded interview questions. Questions,
categories, input definitions, section order and method source arrive in the
native scenario snapshot.

## Adapter contract

`types.ts` is the frontend contract. `normalizeNativeSnapshot(raw)` projects the
actual decorated `OnboardingStore` snapshot into it, preserving scenario
question order, native privacy field names and stable layer keys. Keep the raw
native store revision in the host adapter for CAS writes. The normalizer never
alters the native state or stores another copy of it.

- `getSnapshot()` reads the persistent native session. Translate native
  scenario question/field IDs into `ScenarioSection.fields`; keep the exact
  native method document in `scenario.source` for expert editing.
- `dispatch(action)` can use `nativeDispatchAction(action, {id, time})` for a
  unique native event envelope (reviews keep `candidate` distinct from the event
  ID). The host calls `OnboardingStore::apply(user, expectedRevision, action)`, persists the resulting
  graph/session through the host's existing APIs, then returns the new snapshot.
  The form and conversation both dispatch `answer` followed by candidate
  `review`; only provenance differs (`form` / `user_stated`). Inferred facts come
  from native `ingest_model_reply`, never a frontend fabricated observation.
- `modelRequest(provider)` must call native `model_request(provider)` first.
  It must check category, provider, `declined` / `never`, explicit-only and
  inference restrictions before preparing a request. The provider ID is chosen
  by the user; optional provider suggestions are supplied by the host.
- `completeModelRequest(request)` receives **only** that prepared request and
  may call the existing host model transport. It must not append raw profile
  fields, browser drafts or sensitive context. No model call is made on mount.
  Test replay supplies a saved response instead of a network provider. The
  controller passes only the closed model-facing whitelist (`prompt`, `section`,
  `questions`, `context`, `candidates`, `policy`, `reply_schema`) to this callback.
  The explicit provider selection is in the callback options, never guessed
  from response text. Local request-token/CAS/graph-run/authorization metadata,
  method provenance and future unknown envelope fields are kept local. After
  the callback returns, the controller binds the reply to the
  original local token, revision, graph run and user-chosen provider, overriding
  any provider-generated control fields. These local tokens are never model
  output or provider prompt input.
- `ingestModelReply(reply)` validates/stages the response natively and returns
  the snapshot including `latest_reply` or current `session.summary` and pending
  candidates. Keep the summary in persistent state for resume. Individual fact
  reviews are separate from explicit `confirm_section` (confirmed / corrected /
  rejected). `skip` is never used as a synonym for completion.
- `saveScenario(source)` validates/stores the scenario as a new graph method
  version. The component exposes it in an expert JSON editor. This is not a
  browser-only copy of policy.
- `dispatchLayer(action)` delegates to native `DefaultLayers`. Operations use
  `key`, with `override`, `disable`, `exclude`, `reenable`, `clear_override`,
  `set_area_mode` (native `proposal` / `direct`) and `accept_proposal`. Fill `snapshot.defaults` by resolving
  each key and projecting value, source layer, reason, enabled/excluded state
  and history. The projection retains both versioned `id` and stable `key`;
  mutations use `key`. The setting for new defaults in an excluded area goes
  through `set_area_mode`, never through an unrelated profile setting. Keys and
  areas are data and may describe preferences, methods, prompts, privacy
  consents or application profiles equally.

The host must coordinate revision/transaction checks across views and native
consumers. The controller serializes its own writes, invalidates an in-flight
model response if another write begins, ignores late replies on unmount, and
passes abort signals to the model pipeline. A privacy change through another
view/device must also invalidate the prepared native request in the host; a
component cannot enforce cross-client concurrency alone.

When native retention removes saved summaries, a successfully validated reply
is shown only in the current controller's ephemeral memory. It is never put back
into the native snapshot, localStorage or a file, and is cleared on any action,
reload or unmount. Users can still inspect a summary before confirmation with
history disabled. Pause remains available while a model request is pending;
it aborts the local request and transitions the native session to paused.

Privacy edits send complete rule data from the snapshot, preserving unknown
extensions. Native `max_detail`, `max_sensitivity` and retention are editable JSON values so the
UI does not invent a maximum, retention interval or classification scheme.
Deleting an active fact and inspecting the historical graph are separate
operations; the native retention policy decides which historical values remain.
If a optional native capability is absent, the view discloses the missing
adapter and disables that control. It never silently saves into localStorage.

## Offline checks

With Node 24 (built-in TypeScript stripping):

```sh
node --test loom/web/src/onboarding/controller.test.mjs
```

The 17 controller tests cover recorded interview/review, form provenance,
question suppression for declined/never, filtered request forwarding,
invalidation of a late model reply after a privacy change, unmount during
preparation, serialized loads/writes, error recovery, session resume and missing
adapter disclosure, local request-token binding, actual pack/native snapshot
projection, native event/layer envelopes, ephemeral retention-respecting
summaries, independent question disposition, reply-ingestion races and
interruption during model transport. The fixture adapter is deliberately synthetic. These tests
do not claim native privacy, pack update, migration or transport integration;
those gates belong to W12 native tests and W10 integration. No private input or
provider call is used.
