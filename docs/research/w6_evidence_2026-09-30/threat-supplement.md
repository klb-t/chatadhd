# W6 / T12 — independent data-flow supplement

Native/source baseline: `b118c80e981c08ec6d7f9ab6aacc177979186cf2`, reviewed
2026-09-30 at `62639f7e7b5fcfabffbd56ed74a80bcf759b9800`. The latter adds
only the W6 claim and real-export protocol; it is not a production-code change.
This supplements [the T12 review](../THREAT_MODEL_2026-09-30.md); it does not
replace it or introduce owner policy. Evidence here is source inspection, not
an executed attack test. No real owner archive, credential store, protected
holdout, deployment or live provider was inspected.

## Boundaries that matter to W6 interpretation

| Actual flow | What the implementation establishes | What it does not establish |
|---|---|---|
| File → raw blob → parser → conversation/message metadata | `ConversationImporter::prepare_source` calls `BlobStore::put_file` before interpretation when provenance and both stores are available. The original file is streamed into a SHA-256-addressed blob. OpenAI message `metadata.export.raw` holds a **parsed JSON value**; conversation `fields` and node bookkeeping retain additional parsed structure. | `raw` in JSON metadata is not original bytes. Whitespace, lexical number spelling and escaping are not preserved by parse/serialize. `parse_tolerant` can repair invalid UTF-8 and lone surrogates before interpretation. A `json_leaves == leaves_preserved` result concerns the parsed conversation representation, not byte fidelity or independent verification of every value, attachment and locator. |
| Blob name → blob read / repeated import | `put_file` hashes copied bytes; `verify` can recalculate the stored blob hash. Existing blobs are reused. | `read` does not invoke `verify`, and reuse of an existing destination does not compare its current bytes. Read-only permissions are not an immutable storage boundary. A W6 byte recount must read and hash the saved blob independently of the import report. |
| Compiled context → saved user metadata → HTTP request | `ChatEngine::build_messages` records `messages` and its hash. `send` saves `context_trace` before API-key validation and before transport invocation. The request later adds model, temperature, max tokens, streaming, reasoning and optional provider-side web plugin. | A saved trace can exist even when no dispatch was attempted, including missing-key failure. Its hash covers the message array, not the full HTTP body, headers or destination. Matching an intercepted transport call proves that local invocation, not remote provider receipt. |
| Memory / graph / history → trace → conversation JSON export | Enabled sources can contribute text to the compiled array; `ConversationExporter::export_conversation` serializes `Message::to_json()` and conversation metadata for JSON. Therefore trace excerpts may leave with the conversation export even when absent from its visible message text. | JSON export is not a source-archive bundle or proof of all original assets being present. Markdown/text/HTML exports follow a different, reduced representation. Disabling **future** trace recording does not remove traces already in SQLite or previous exports. |
| Provider/parser diagnostics → logger ring / stderr / sinks | `log::write` retains formatted records in a process ring (default 500), optionally stderr, and registered sinks. Semantic failure snippets and malformed chat SSE data have logging sites; chat/model registry HTTP errors include response prefixes. | The central logger performs no redaction. Prefix length is not a confidentiality control: a provider response can contain source text or an echoed credential. Storage and retention beyond the ring depend on configured sinks or the process host. |
| Source/model Markdown → sanitized HTML → browser network | `ChatView.renderMarkdown` uses `marked` and DOMPurify; its local configuration forbids `style`, and the hook protects anchor links with `noopener noreferrer`. It does not forbid `img` or rewrite remote image sources. | XSS sanitization does not establish zero browser egress. Rendering a permitted remote image can contact its host without a new model call. The reviewed native server sources contain no Content-Security-Policy header; an external host/proxy might add one and was not inspected. Anchor `noreferrer` does not itself govern image fetches. |
| AnalysisPlan → caller capabilities / registered callback → durable receipt | `execute_variant` checks declared capabilities, registered method/runtime pair, dependency states and bound-source declarations; it reserves resources and saves the loaded packet and first callback return. It deliberately reports `source_binding_verification: caller_declared_not_verified`. | The callback and packet loader execute in the caller process; capability strings are dispatch prerequisites, not an OS sandbox. A stored result hash proves content identity, not source truth or absence of callback side effects. No automatic `apply_diff` occurs in this executor; that does not make arbitrary callbacks side-effect-free. |
| Local credentials → configurable endpoint → provider request | `JsonStore::save_locked` passes owner-only options to atomic JSON writing. Native secret routes list names, set/delete values and report presence. Chat/model registry read `api_key` and send it in an Authorization header to the configured `base_url`. | Plaintext JSON plus requested permissions is not application-level encryption. A trusted API operator who changes `base_url` controls the destination of subsequent credential-bearing calls; the absence of a secret-value read endpoint is not a separate least-privilege user boundary. |

One operational qualification remains especially easy to miss: turning off
graph **inclusion** in a chat request does not turn off separately configured
semantic provider calls. `Runtime::open` wires the graph handler independently
of `start_workers`; `ChatEngine::send` emits message-created before compiling
the request. W6 fixtures must disable/configure those paths explicitly when
claiming zero remote calls, and report what the independent transport saw.

## Exact source locators

All paths below are relative to the repository root; symbols remain usable
after line movement.

- Import and raw-byte distinction: `loom/src/import/importer_core.cpp`
  (`prepare_source`); `loom/src/core/provenance.cpp` (`BlobStore::put_file`,
  `read`, `verify`); `loom/src/import/export_common.cpp` (`json_leaves`,
  `parse_tolerant`); `loom/src/import/export_openai.cpp` (`parse_openai_conversation`,
  assignments to `ex["raw"]`, `ex["fields"]`, `out.leaves_kept`);
  `loom/src/util/json.cpp` (`parse`).
- Request, retention and export: `loom/src/chat/chat_engine.cpp`
  (`build_messages`, `send`); `loom/src/import/exporter.cpp`
  (`ConversationExporter::export_conversation`); `loom/src/runtime.cpp`
  (`Runtime::open`); `loom/src/graph/graph_engine.cpp` (`on_message`).
- Diagnostics: `loom/src/util/log.cpp` (`write`); `loom/src/semantic/semantic_llm.cpp`
  (`call_llm`); `loom/src/providers/providers.cpp` (`ModelRegistry::update_from_api`);
  `loom/src/chat/chat_engine.cpp` (`send`).
- Browser and operator surface: `loom/web/src/components/ChatView.tsx`
  (`renderMarkdown`); `loom/server/src/app.cpp` (`register_middleware`,
  `route_config_secrets`, `route_import_export`).
- Executable trust and credentials: `loom/tools/contracts/analysis_plan_ref.py`
  (`execute_variant`); `loom/src/core/config.cpp` (`JsonStore::save_locked`);
  `loom/src/chat/chat_engine.cpp` (`send`).

## Acceptance implications, not additional restrictions

Keep separate receipts for raw-file hashes, parsed-value/locator comparison,
context compilation, intercepted dispatch and the returned result. Report
unexamined surfaces as unexamined. A faithful source import can still yield a
semantically wrong context, and a semantically suitable context can still have
no evidence of dispatch. Configurable disclosure, resource and acceptance
presets remain the owner's choices.
