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

The revised browser test is `npm run test:chat-context`. It requires the shared final native server and verifies default chat, author-supplied two-thesis plan through HTTP/native to local fake provider, exact persisted messages equal captured provider messages, independent source toggles, recording off/on, provider failure without retry, and saved W5 perspective with an independent reference while chat stays visible. It also checks perspective restore and reload do not alter request options, native config, or view identities. No paid models or holdout fixtures are used.


## Final native/browser verification

PASS against ROOT native source `da77c769d3e020c396db5bef6a3a3755314d75c0` / tree `abe386316d6fc66a3fa2b0ba8aabc2b28c8df41d`. ROOT and this branch had identical complete web trees `07d863044f992afd47ecebe40fd73a1e3b35e0e0` before execution. Production source/assets were unchanged; the test-only addition writes optional evidence and failure screenshots to a caller-selected fresh directory. It refuses to overwrite earlier payload files.

- Revised chat/context/W5 coexistence gate: PASS. Exactly 5 local fake-provider requests, 0 remote requests. The authored two-thesis plan reached the real native compiler; inherited and explicit-empty settings, native plan diagnostics, and captured provider messages matched the recorded context. The actual browser request bodies, provider request bodies, compiled trace and persisted messages are retained in `verification/web-chat-context-headless/chat-context-payloads.json` (authored synthetic data only).
- Existing W5 native workspace suite: 10/10 scenarios PASS, 0 model requests. Saved perspective, copied independent reference, directed couplings/cycle, run changes, preview, reload, catalog race, unavailable run and mobile layout were checked. Snapshot: `verification/web-workspace-headless/workspace-perspective.json`.
- Independent read-only review of web source `9c79181`: no blocker; no hidden request/task inference, defaults and field inheritance match the native contract.
- Server SHA-256 before and after: `52858ae162f449ae636429d3c2d4dd63e120651d1b17f3ca27939fc7ca556cc3`; shared library: `f1c7a45000bee6b6efc73a5ec92cb87d519872d4214dccc85df42dced479db40`.

First attempts with the full Chromium 1194 executable failed during browser bootstrap (`process_singleton_posix.cc`: `socket() ... Operation not permitted`), before any page or provider request. Both original logs and empty/partial JSON records remain under `web-*-first`; screenshots could not exist before browser launch. Using the installed Playwright `chromium_headless_shell-1194/chrome-linux/headless_shell` completed both suites, with unchanged production code and assertions. This is an environment failure followed by a successful compatible runtime, not an erased product failure.

`verification/web-native-manifest.json` pins source trees, executable/test-script/asset SHA-256 values, commands, artifacts and outcomes. No further optional matrix runs were made after both gates passed. These are functional integration checks on authored synthetic data, not model-quality or holdout evidence.
