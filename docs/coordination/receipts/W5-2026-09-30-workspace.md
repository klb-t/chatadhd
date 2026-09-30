> Publication update — 2026-10-01: the W5 source bundle has now been recovered
> and published on `gpt/w5-workspace-2026-09-30`. Each published payload tree
> matches its original source tree exactly. See
> [the publication receipt](W5-2026-10-01-publication.json) for original → published
> commit mapping. The original report below preserves its earlier **not pushed**
> status as historical evidence; that publication blocker is now resolved.
> Integration with the other W packages remains pending.

# W5 — durable coupled workspace

- `base_sha`: `b118c80e981c08ec6d7f9ab6aacc177979186cf2`
- `head_sha` (implementation): `c6a83bd87de0249ca65e4228192e5b469ac60f74`
- Branch: `gpt/w5-workspace-2026-09-30`
- Local implementation commits: `d9a9e05`, `8c62f18`, `c6a83bd` (full graph in Git).
- Assigned by owner: “lecim z W5”, with coordination README and STATE attached.
- Integration HEAD checked through GitHub before work and again after implementation;
  still `b118c80e981c08ec6d7f9ab6aacc177979186cf2`.
- Delivery state: **implemented and locally verified; NOT pushed or integrated**.
  Automatic approval review rejected `git push origin HEAD`: it considered the
  repository an unverified external destination and said W5 lacked explicit
  authorization for this disclosure/push target. No alternative write route was
  used. A read-only GitHub ref check subsequently returned 404 for the W5 branch.
  Obtain approval for the exact branch before publishing these commits.
- No main/PR6/STATE/native API/schema change, deployment, remote model call, paid
  inference, source-archive publication, or unrelated branch merge.

## Implemented result and mapping to T5

| Existing/T5 concept | W5 implementation and boundary |
|---|---|
| Workspace/container/view | Existing workbench, adaptive/stacked/compact grid, stable per-view IDs and ordered view list. No five-view cap. No new app. |
| Query/projection | Real native knowledge API, explicit run and record limit per view, collection/graph/context/catalog/candidate renderers. |
| Parameters | Persistent search, evidence/confidence, graph depth/opacity/node limit, entity focus/selection references, context prompt/budget, candidate kind/page/offset, catalog filter/selected-only/offset. |
| Couplings | Directed identity links, per parameter, with explicit source, target and detach. One incoming driver per target/parameter in this UI. Cycles/fan-out converge by visiting an endpoint once per event. |
| Independent reference | Duplicate keeps current parameters with a new ID and no links. Unlink removes incoming/outgoing links and stops following workspace run/limit. Adding a new pane never recruits an unlinked reference. |
| Profiles | Presentation-only adaptive/stacked/compact choices. No execution configuration changes or implicit requests. Full provider interaction emulation is not implemented. |
| Persistence | Versioned `loom.workbench/2`, local autosave plus one explicit saved perspective. Stable IDs, references and links survive reload; old layout list migrates, including empty layouts. Invalid/unsupported state is preserved and autosave pauses with a visible error. |
| Inspector | Resolves saved IDs from fetched data, retaining real provenance/uncertainty. Displays requested run, actual run metadata/status, fetched time and partial/missing data. Missing run never falls back silently to latest. Cross-run ID reuse does not resolve or retain a mismatched inspector. |
| Context trace | Existing native preview remains explicitly a preview, never proof of provider delivery. Inputs persist; preview output is rebuilt explicitly. Existing recorded chat traces remain untouched. |

This is a deliberately smaller live adapter of T5, **not** a reader/writer of the
full `workspace.schema.json`. It implements identity-only, single-parameter
transactions in one analysis scope; no affine/lookup transforms, arbitrary
container tree/docking, event replay ledger, multi-seed conflict solver or
cross-device synchronization is claimed. Canonical data is neither copied into
browser storage nor changed by parameter propagation. Saved source/query text
and references are local browser data, not credentials or complete data rows.

Data is reloaded from the chosen run. A run ID is not claimed to be an immutable
server snapshot; unlink freezes parameters, not mutable server state. Catalog
is explicitly labelled live and outside run scope. No native schema extension
was necessary, so no shared native/API file required a ROOT edit.

## Verification

Final implementation source: `c6a83bd87de0249ca65e4228192e5b469ac60f74`.

- TypeScript + Vite production build: **PASS**.
- Pure state tests: **8/8 groups PASS**, including convergence, unlink, duplication,
  detached-first-pane addition, pagination, round-trip, >5 panes, legacy migration,
  malformed snapshots, and separation from execution-profile keys.
- Chromium → native C++ HTTP server: **10/10 scenarios PASS**. Two coupled graph
  views plus an independent reference, scope changes, keyboard selection, saved
  perspective, exact reload restoration, two native runs, context preview and
  input persistence, mobile 390px/no document overflow, >5 views, late catalog
  preview after close, catalog inspector restore, unavailable saved run.
- Extended semantic-controls browser suite: **PASS**. All APIs mocked, separately
  labelled evidence. Existing proposal/graph controls remain operational; candidate
  inspector restores from rows after reload; same-ID/different-run selection is
  rejected both on live linking and after reload. No model inference measurement.
- Existing full browser regression: **16/16 steps PASS** at the earlier W5 stage
  before the final candidate-only run-provenance guard. This is a regression
  checkpoint, not a second final-head native suite; included log identifies it.
- `git diff --check`: PASS. No quality threshold changed.

Exact commands, from `loom/web`:

```sh
npm run build
LOOM_SERVER_BIN=/workspace/scratch/310a8f8399f0/verification/loom-server npm run test:workspace
node e2e/semantic-controls.mjs
PLAYWRIGHT_CHROMIUM_EXECUTABLE=/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell node e2e/run.mjs
```

For a normal checkout build the native server first (default
`loom/build/dev/server/loom-server`); install browser dependencies as needed.
No new npm dependency was added. Tests used existing node modules matching the
lockfile and Playwright 1.56.1 / Chromium Headless Shell 1194. Node 24.19.0.

Native server reused from the prior verified integration build, copied into this
lane, not rebuilt or modified. Its SHA-256 matches the existing verification
receipt exactly:
`3fc10a60b7aa4e5336cf98e38a8b88d175d27b4ce0b351b4a43b823ff3262bd6`.
All **258 native source hashes** listed in that receipt match this checkout.
No new complete native CTest pass is claimed. Data provenance: repository-authored
`loom/tests/fixtures/eval/synthetic_dev/chatgpt_export.zip`; separate mock candidate
fixtures in `semantic-controls.mjs`. No owner exports or sealed holdout read.

## Failures retained and independent review

- Initial full Chromium launch failed because its singleton Unix socket was
  unavailable in the environment. Official Headless Shell ran successfully.
  The initial log is retained; this was not a product success or a sandbox
  escalation.
- First existing UI regression run failed on an ambiguous `.kb-count` locator
  because new run status reused that class. Run status now has its own semantic
  class; the unchanged regression script passed 16/16 afterward.
- Independent read-only agent review found: accidental relinking of a detached
  first pane; missing catalog/candidate inspector restoration; late catalog
  preview activating a closed pane; and cross-run same-ID inspector leakage.
  All were fixed and targeted tests added. The reviewer confirmed the first
  three repairs, then specified both resolver and already-visible-inspector
  guards; both final guards are now implemented and exercised by the mocked
  browser scenario. Review did not claim to execute tests independently.

## Files and integration

Owned changes only:

- `loom/web/src/workspace/state.ts`
- `loom/web/src/components/KnowledgeWorkbench.tsx`
- `loom/web/src/components/knowledge.css`
- `loom/web/e2e/workspace-state.mjs`, `workspace.mjs`, `semantic-controls.mjs`
- `loom/web/package.json`
- This W5 receipt and its evidence directory.

Retained limits: browser-origin local persistence (not account sync), one saved
perspective slot, no immutable data snapshot, independent pane fetches can repeat
collection requests, native candidate caps remain visible, Android native bridge
still reports unavailable knowledge UI, and multiple browser tabs are not a
collaborative transaction protocol. Execution forms and catalog import approval
are intentionally not replayed from a perspective.

Next: approve/push W5 branch, then ROOT reviews and integrates the commits against
its current integration HEAD. Re-run affected browser tests after any concurrent
web merge. Only ROOT updates STATE and global coordination. No PR6 merge.
