# ChatADHD threat and data-flow review — T12

Date: 2026-09-30. Code review baseline:
`e466c6a93fcaf825024a9c8e6286044a6671b08f`, plus the current context-in-chat and
metadata-preservation changes being integrated in this session. This is a
scoped code review, not a penetration test or a deployed-system certification.

The current native server is an operator-controlled application sharing one
runtime, data directory, and credential store. No per-person or per-project
authorization boundary was found in the reviewed HTTP routes. A person allowed
to operate that API has substantially more power than a read-only chat guest.
Whether the operator is the sole user, a trusted collaborator, or a future
restricted user is a deployment/product choice, not decided here.

## Data flows and actual boundaries

| Flow | Controls visible in code | Remaining boundary or uncertainty |
|---|---|---|
| Browser → HTTP API → native runtime | Default bind is `127.0.0.1`. An optional bearer token protects `/api/` routes; token comparison avoids prefix short-circuiting. CORS response headers are not enabled. Browser token lives in memory/sessionStorage. | The default token is empty. The server uses `httplib::Server`, so TLS termination, host exposure and network isolation depend on deployment. Same-origin/CORS is not authentication or a general CSRF guarantee. An authorized API operator can change configuration, initiate reads/work, and export data. |
| Local files / uploaded archives → parser → SQLite, raw blobs and provenance | Runtime importer has BlobStore/ProvenanceStore; `record_provenance` defaults true. SHA-256-addressed source copies and parser/locator records preserve a route to original bytes. ZIP path checks reject absolute and `..` paths. HTTP request-body size has a configured limit, defaulting to 64 MiB. | Provenance recording is configurable. Read-only blob mode is not tamper-proof storage: `BlobStore::read()` does not call `verify()`. The HTTP input limit is not an expansion limit: generic ZIP extraction has no explicit total uncompressed-byte/depth admission in its loop. Server upload staging uses the OS temporary directory and normal file creation permissions; interruption cleanup and actual filesystem privacy were not tested. |
| Stored memory, history, graph and selected knowledge → chat request → configured provider | Chat has independent memory/graph/history switches and an opt-in offline knowledge selector. When enabled, compilation traces preserve the actual `messages` array and hash, selected run and history IDs before the provider call. `trace_context: false` opts out without changing the request. The new reindex merge preserves recorded traces. API key is added as an HTTP header rather than a message. | The trace is evidence of compilation, not provider receipt or a complete immutable HTTP request. It may contain source excerpts, private memory and image data URLs. Text attachments undergo UTF-8 repair/truncation; the original and transmitted representation differ. Source text in a system message still requires interpretation; provenance labels do not enforce instruction isolation. |
| Configured semantic analysis / media operations → external services | Semantic LLM requires an enabled setting, model and key; absent capability can fall back to local regex. The new ContextEngine adapter itself selects offline. ASR/OCR providers are initialized from configured credentials; explicitly naming a provider selects only that provider. | Native graph handling starts independently of background workers. If semantic analysis is configured, message events can cause a separate provider request. Disabling graph **inclusion in chat** does not disable semantic **analysis**. Unspecified-provider media fallback can try another configured provider. A claim that the entire app is offline needs to account for these separate paths. |
| Credentials → local `secrets.json` → provider requests | Secret API exposes key names/presence/set/delete, not secret values. Secret saves request owner-only permissions. GitHub sync configuration omits its in-memory token. HTTPS transport enables certificate verification when built with TLS support. | Credentials are plaintext local JSON. Permission failure during the final chmod is logged rather than returned as save failure. Configurable `base_url` determines where the chat/model-registry credential is sent; endpoint configuration is therefore an operator capability. Proxy/CA configuration, redirects and deployment TLS were not adversarially tested. |
| Conversation → export / configured directory → GitHub sync | JSON exporter serializes messages and metadata; HTML export escapes source text. Sync has explicit direction and include/exclude patterns. The new default preset excludes root and nested `secrets.json` files in native and Python sync. The HTTP export route uses attachment disposition and a sanitized filename; archive/knowledge output routes restrict output names beneath their export folders. | JSON exports also carry `context_trace`, which can disclose more than the visible conversation. Persisted explicit exclusions are unchanged; callers can replace the default or explicitly select files. Other private JSON still matches `*.json`. The default exclusion is not a general credential-content filter or an upload ban. Plaintext export is not automatically protected by CryptoVault. |
| Provider/parser failures and runtime events → logs → console/API/operator | Log level and stderr output are configurable. Server exception handler returns a generic client error. Default persisted event types are limited to conversation-created, import-done and graph-changed. | Logger has no central redaction pass. Semantic error/invalid-response snippets, malformed SSE snippets, paths and some provider response bodies can reach logs or errors. `/api/logs` has the same operator access boundary. Extra configured event types may retain their payloads. No claim of universally secret-free diagnostics is justified. |
| Model text / source content → rendered UI or proposal callback | Chat Markdown passes through DOMPurify; links receive `noopener noreferrer`. Context trace is rendered as text. GraphPacket keeps proposal/instrument provenance distinct from content truth; AnalysisPlan executes registered callbacks and does not automatically apply graph changes. | HTML sanitization does not make model claims true and does not establish zero browser egress: permitted remote images can load independently of the model API. Native chat has no general local tool-call execution loop in the reviewed path. Callback registration is executable trust, not a sandbox; future tool adapters need their own action/resource contract. |

## Consequential scenarios and proposed improvements

These are review recommendations, not newly imposed restrictions or changes to
owner policy. Defaults remain replaceable; the owner can choose providers,
disclosure scope, automatic acceptance and resource limits.

1. **A “chat user” is given the server token but expected to lack operator
   powers.** That expectation is unsupported: the same boundary admits config,
   source work, secret use and exports. Before introducing restricted users,
   represent read/export/provider-configuration/tool-action capabilities
   separately. Test a restricted role against those operations. A sole trusted
   operator need not be forced into a multi-user permission workflow.
2. **Data leaves through a path the user did not associate with sending chat.**
   Show provider/endpoint and selected data categories for semantic analysis,
   ASR/OCR and chat independently. Provide an inspectable sync/export manifest
   that identifies source excerpts, request traces and credential-bearing files.
   Sensitive-file defaults or an explicit override can be a preset; do not turn
   this review into a permanent prohibition on arbitrary files or providers.
3. **A saved “source” or “request” is assumed to establish more than it does.**
   Distinguish raw bytes, transformed attachment text, compiled messages,
   attempted dispatch and provider response. Use existing blob verification at
   consequential integrity boundaries, with a configurable verification policy
   where cost matters. Keep inference/acceptance separate from factual truth.
4. **An archive or response exhausts resources or leaks through diagnostics.**
   Extend caller-owned resource policy to staging/extraction and nested calls;
   expose estimates and cancellation before expensive work. Test response
   snippets with injected sentinel secrets and give diagnostics a redaction/
   retention policy. The AnalysisPlan accounting limitation is recorded
   [separately](RESOURCE_PEAK_LIMITATION_2026-09-30.md); that reference runtime is
   not currently a global resource guard for native chat or import.

## Encryption and storage claim

`CryptoEngine`/`CryptoVault` implement explicit AES-GCM text encryption with an
OpenSSL-dependent backend and an unlockable password holder. The reviewed
SQLite, memory, blob, secret, trace and export paths do not route their storage
through that API automatically. Therefore “CryptoVault exists” does not
establish encrypted-at-rest application data or end-to-end encryption to an
external model. Filesystem/device encryption and any hosting controls are
unknown here. No credential file or personal source archive was opened for this
review.

## Source map and validation status

Paths are relative to the repository root. Symbol locators are given so line
movement during integration does not obscure the evidence.

| Area | Code inspected |
|---|---|
| HTTP/operator boundary | `loom/server/src/main.cpp` (`main`); `app.h` (`ServerOptions`, `App`); `app.cpp` (`register_middleware`, `route_config_secrets`, `route_import_export`, `route_logs_misc`, archive/knowledge output routes); `loom/web/src/api/loom-http.ts` (`setAuthToken`, `headers`, `req`) |
| Sources and storage | `loom/src/core/config.cpp` (`DataPaths::for_root`, `Secrets`); `loom/src/core/provenance.cpp` (`BlobStore::put_file`, `read`, `verify`); `loom/src/import/importer_core.cpp` (`prepare_source`); `importer_zip.cpp` (`safe_zip_relpath`, `zip_body`); `loom/include/loom/importer.h` (`ImportOptions`); `loom/src/util/fs.cpp` (`atomic_write`) |
| Request and secondary providers | `loom/src/chat/chat_engine.cpp` (`build_content`, `build_messages`, `send`); `loom/src/runtime.cpp` (`Runtime::open`); `loom/src/graph/graph_engine.cpp` (`on_message`); `loom/src/semantic/semantic_llm.cpp` (`enabled`, `call_llm`); `loom/src/media/media_providers.cpp` (`refresh`, provider fallback); `loom/src/providers/providers.cpp` (`ModelRegistry::update_from_api`); `loom/src/net/http_default.cpp` (`DefaultTransport::send`) |
| Export, crypto and logs | `loom/src/import/exporter.cpp` (`export_conversation`); `loom/include/loom/github_sync.h` (`SyncConfig`); `loom/src/github/github_sync.cpp` (`should_include`, `push_file`, `SyncConfig::to_json`); `loom/src/crypto/crypto.cpp`; `loom/src/capi/capi_media.cpp`; `loom/src/util/log.cpp` (`write`) |
| Presentation and executable adapters | `loom/web/src/components/ChatView.tsx` (`renderMarkdown`, `ContextTrace`); `loom/tools/structure/agentic_graph_v1/packet.py` (`apply_diff`); `loom/tools/contracts/analysis_plan_ref.py` (`execute_variant`) |

Existing targeted coverage was located in `loom/tests/test_config.cpp`
(secret permissions), `test_import_exports.cpp` (unsafe ZIP paths),
`test_chat_knowledge_context.cpp` (exact compiled messages, provider failure,
reindex preservation), and `loom/web/e2e/run.mjs` (Markdown XSS). Finding a test is
not a fresh passing result: execution results belong to the integration
verification record. The initial T12 review performed no attack simulation,
real provider call, deployment inspection or secret-export test; the follow-up
below adds a synthetic intercepted-export regression.

Follow-up to the sync finding: before the preset change, an offline Python
`push_all()` reproduction selected `notes.json`, root `secrets.json`, and nested
`secrets.json`; mocked PUT payloads included synthetic credential bytes. The
new default adds `secrets.json` and `*/secrets.json` exclusions. Regression cases
in `loom/tests/test_github.cpp` and
`loom/tests/compat/test_github_sync_defaults.py` compare default selection with
an explicit empty exclusion list using synthetic files and intercepted HTTP.
This changes new/defaulted configurations only, not saved explicit patterns,
direct file selection, or the owner's ability to override the preset. No real
credential file or live upload was used.

An independent read by the context-runtime lane confirmed the browser token
storage, sanitized message rendering, plain-text trace rendering, JSON-export
trace inclusion, and the explicit native trace-retention opt-out. Browser UI
changes were still being integrated; native support alone is not a claim that
every UI exposes each control.
