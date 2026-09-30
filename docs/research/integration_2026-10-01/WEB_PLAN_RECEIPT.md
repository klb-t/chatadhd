# Web: explicit retrieval plan and W5 coexistence

Source base: `96ea1c727ee5bbba4ee056dfa15aa844d1102d22`, branch `gpt/verify-workbench-2026-10-01`.

Changes are confined to `loom/web` and this receipt/evidence. The chat's knowledge controls now offer an explicit, default-off retrieval plan. The author supplies plan/thesis IDs and text, optional declared source-reference JSON, per-thesis entity/claim anchors (inherit or explicit empty), graph reach, representation detail, relative budget weight, and traversal of existing recorded counter-evidence links. There is no thesis-count limit and no inferred ActiveTaskSpec, generated thesis, remote call, or new persistent store. The existing context trace inspector displays native per-thesis diagnostics and the resolved request. Declared source provenance is not verified task binding. Missing counter-evidence links are not a consistency proof.

Source settings remain opt-in; existing request defaults remain unchanged. Disabling knowledge omits the plan while retaining the in-memory draft. W5 perspectives neither save this draft nor enable it on reload. Invalid plans retain the message draft and make no chat request.

Pre-native verification:

- TypeScript and production Vite build: PASS (`verification/web-build-first.log`).
- Retrieval form semantics: PASS (`verification/web-plan-state-first.log`), including omitted vs explicit-empty anchors, preserved query text, per-thesis overrides, invalid requests, declared provenance, and more than 100 theses.
- Existing W5 state suite: 8/8 groups PASS (`verification/web-workspace-state-first.log`).
- Mock transport contract: PASS (`verification/web-transport-first.log`).
- `git diff --check` and E2E JavaScript syntax: PASS.

The revised browser test is `npm run test:chat-context`. It requires the shared final native server and verifies default chat, author-supplied two-thesis plan through HTTP/native to local fake provider, exact persisted messages equal captured provider messages, independent source toggles, recording off/on, provider failure without retry, and saved W5 perspective with an independent reference while chat stays visible. It also checks perspective restore and reload do not alter request options, native config, or view identities. Native/browser verification is pending the coordinated final binary; pre-native checks do not establish its success. No paid models or holdout fixtures are used.
