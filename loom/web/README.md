# Loom web client

The React client uses `LoomApi` through either authenticated HTTP or the Android
WebView bridge. Both transports use the same knowledge-layer JSON contract.

```sh
npm ci
npm run build
npm run test:transport
npx playwright install chromium
npm run e2e
```

The browser end-to-end suite needs `../build/dev/server/loom-server`; it starts
an isolated server/data directory and a local mock chat provider. Knowledge
analysis runs through the actual C++ catalog and six-stage pipeline using the
synthetic export fixture. It checks claim provenance, coordinated graph views,
context budgets, import review and mobile layout as well as existing chat,
streaming, versions, imports, graph and XSS behavior. Screenshots are written
under `e2e/screenshots/`. `PLAYWRIGHT_CHROMIUM_EXECUTABLE` can select an installed
browser when Playwright's default is unavailable.

## Knowledge workbench

Open **Knowledge** in the header. Chat stays mounted and can remain visible
beside the workbench; **Hide chat / Show chat** controls its visibility. The
conversation sidebar can also be toggled independently.

- Add any number of entity, claim, graph, principle, operator, instance,
  product, catalog and context views. Duplicate buttons add another instance,
  including multiple graphs. View kinds/order persist locally; record data
  continues to come from Loom.
- Entity/claim selection sets shared focus. Claim and instance views can opt
  out of following it. Each view has its own filters; graph depth and background
  opacity reveal the selected neighborhood without removing surrounding nodes.
  Directed edges and nodes open the shared inspector.
- Evidence class, origin, confidence and status remain separate. Inferred
  nodes/claims have explicit labels; the inspector exposes all seven assessment
  questions, supporting quotes and source locators, premises, counterclaims,
  expected properties and every raw model field. Seed principles remain marked
  `candidate` unless their actual engine status says otherwise.
- **Analyze sources** accepts paths on the Loom host. Full/selective import and
  inclusion of candidate priors are explicit options. Runs use local analysis
  (`llm: off`) and surface failed/cancelled pipeline results as such.
- The source catalog scans without importing, searches/paginates units,
  computes profile selection, records include/exclude overrides, and previews
  source details. Import requires reviewing the selected scope and copy/link
  retention first. Changing those options invalidates the review. Catalog
  search filters do not change import scope.
- Context preview uses the chosen run, prompt, token budget and optional shared
  entity target. It shows the stable/project/goal bands, each inclusion reason,
  dependency closure, dropped items and rendered text. Counts are engine token
  estimates. Changing inputs marks the existing preview stale. Previewing does
  not send a model request or silently replace the chat engine's legacy context.

The first workbench slice uses responsive tiled views. Free docking, drag
reordering, provider-inspired layout profiles, knowledge judgement editors and
automatic context injection into chat are not implemented here. Graph node and
collection limits are owner controls and visible in their counts; a graph is a
view of loaded records, not a claim that the entire knowledge base is on screen.
## Application interface profiles

The workbench includes data-defined application views and workflow actions.
Use **Add application view** for simultaneous profiles, or **Import profile**
for a JSON definition. Model/context controls remain independent of view style.
The bundled ChatGPT/Claude/Gemini examples are inspired prototypes with unverified
original versions. See [the contract and limits](../../docs/APPLICATION_PROFILES.md).
Run `npm run test:application-profiles` for offline runtime/adapter checks and
`npm run test:application-profiles-browser` for integration with the native server
and a local scripted provider (build the server and web first).
