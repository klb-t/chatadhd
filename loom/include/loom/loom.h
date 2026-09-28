/*
 * loom.h — the Loom C ABI (stable boundary for every client: Android JNI,
 * the HTTP server, the CLI, FFI from other languages).
 *
 * CONVENTIONS
 *
 * Ownership
 *   Every `const char*` returned by a loom_* function is a heap-allocated,
 *   NUL-terminated UTF-8 JSON document owned by the caller. Release it with
 *   loom_free_string(). Never free it with free()/delete. Strings passed IN are
 *   borrowed for the duration of the call only.
 *
 * Errors
 *   - JSON-returning functions never return NULL (except loom_init/
 *     loom_init_ex on failure). On failure they return
 *       {"error": {"code": "<code>", "message": "<text>"}}
 *     where <code> is one of: invalid_argument, not_found, already_exists,
 *     io, database, parse, network, http, auth, cancelled, timeout,
 *     unavailable, not_implemented, crypto, conflict, busy, unsupported,
 *     rate_limited, paused, internal.
 *     A successful result is never an object whose only key is "error".
 *   - int-returning functions return 0 (LOOM_OK) on success and a negative
 *     LOOM_E_* code on failure (loom_has_secret returns 1/0 on success).
 *   - NULL `ctx` or a required NULL string argument -> invalid_argument.
 *   - No C++ exception ever crosses this boundary.
 *
 * Threading
 *   All functions are thread-safe and may be called from any thread.
 *   loom_chat/loom_chat_ex block the calling thread for the whole exchange and
 *   invoke the stream callback on that same thread. Event callbacks
 *   (loom_subscribe) run synchronously on the thread that emitted the event
 *   (possibly a Loom worker thread): keep them short and marshal to your UI
 *   thread yourself. Log callbacks may be called from any thread.
 *   loom_shutdown must not race with other calls on the same context.
 *
 * Streaming chunks (LoomStreamCallback `chunk`, always a JSON object)
 *   {"type":"start","request_id":"...","conv_id":"...","user_message_id":"..."}
 *   {"type":"delta","text":"..."}                 assistant text
 *   {"type":"reasoning","text":"..."}             reasoning/thinking tokens
 *   {"type":"done","message_id":"...","conv_id":"...","text":"...",
 *    "usage":{...},"model":"...","title":"..."?}  final (done = 1)
 *   {"type":"error","code":"...","message":"..."} final (done = 1)
 *   The `done` flag is 1 exactly once, on the final chunk.
 *
 * Data directory
 *   Shared with the Python ChatADHD app (same files: chatadhd.db,
 *   config.json, secrets.json, memory.json, models.json, attachments/,
 *   exports/, logs/). NULL data_dir resolves like Python: $CHATADHD_DATA,
 *   then a directory containing the .chatadhd_data sentinel, then
 *   ~/.chatadhd (Android: /storage/emulated/0/Documents/ChatADHD).
 */
#ifndef LOOM_LOOM_H
#define LOOM_LOOM_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(LOOM_BUILDING)
#define LOOM_API __declspec(dllexport)
#else
#define LOOM_API __declspec(dllimport)
#endif
#else
#define LOOM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LOOM_ABI_VERSION 1

/* Return codes (negated loom::Errc values). */
#define LOOM_OK 0
#define LOOM_E_INVALID_ARGUMENT (-1)
#define LOOM_E_NOT_FOUND (-2)
#define LOOM_E_ALREADY_EXISTS (-3)
#define LOOM_E_IO (-4)
#define LOOM_E_DATABASE (-5)
#define LOOM_E_PARSE (-6)
#define LOOM_E_NETWORK (-7)
#define LOOM_E_HTTP (-8)
#define LOOM_E_AUTH (-9)
#define LOOM_E_CANCELLED (-10)
#define LOOM_E_TIMEOUT (-11)
#define LOOM_E_UNAVAILABLE (-12)
#define LOOM_E_NOT_IMPLEMENTED (-13)
#define LOOM_E_CRYPTO (-14)
#define LOOM_E_CONFLICT (-15)
#define LOOM_E_BUSY (-16)
#define LOOM_E_UNSUPPORTED (-17)
#define LOOM_E_RATE_LIMITED (-18)
#define LOOM_E_PAUSED (-19)
#define LOOM_E_INTERNAL (-20)

/* Log levels (Python logging numbers). */
#define LOOM_LOG_DEBUG 10
#define LOOM_LOG_INFO 20
#define LOOM_LOG_WARNING 30
#define LOOM_LOG_ERROR 40

typedef struct LoomContext LoomContext;

typedef void (*LoomStreamCallback)(const char* chunk, int done, void* user_data);
typedef void (*LoomProgressCallback)(int current, int total, const char* status, void* ud);
typedef void (*LoomEventCallback)(const char* event, const char* payload_json, void* user_data);
typedef void (*LoomLogCallback)(int level, const char* logger, const char* message, void* user_data);

/* ── Lifecycle ─────────────────────────────────────────────────────── */

/* Opens (creating if needed) the data directory; NULL = default resolution.
 * Starts background workers. Returns NULL on failure. */
LOOM_API LoomContext* loom_init(const char* data_dir);
/* options_json: {"data_dir","start_workers","enable_fts","log_level"}.
 * On failure returns NULL and, if error_json_out != NULL, stores an error
 * JSON there (free with loom_free_string). */
LOOM_API LoomContext* loom_init_ex(const char* options_json, const char** error_json_out);
/* Stops workers, closes the database, frees ctx. NULL is a no-op. */
LOOM_API void loom_shutdown(LoomContext* ctx);
/* Frees any string returned by this API. NULL is a no-op. */
LOOM_API void loom_free_string(const char* str);
/* {"version","abi","sqlite","fts5","openssl","unicode"} */
LOOM_API const char* loom_version(void);
/* Runtime info for this context: build info + {"data_dir","paths":{...}}. */
LOOM_API const char* loom_info(LoomContext* ctx);

/* ── Logging (process-wide) ────────────────────────────────────────── */

/* Installs (or with cb == NULL removes) the host log sink; records below
 * min_level are not delivered to it. Returns LOOM_OK. */
LOOM_API int loom_set_log_sink(LoomLogCallback cb, int min_level, void* user_data);
/* Global threshold for all sinks and the ring buffer. */
LOOM_API void loom_set_log_level(int level);
/* Enables/disables the built-in stderr sink (default on). */
LOOM_API void loom_set_log_stderr(int enabled);
/* Last `max_lines` formatted log lines: ["HH:MM:SS [INFO ] logger: msg", ...]. */
LOOM_API const char* loom_get_logs(int max_lines);

/* ── Events ────────────────────────────────────────────────────────── */

/* Subscribes to an event name ("message:created", "graph:changed",
 * "import:done", "semantic:progress", "task:changed", ... or "*" for all).
 * Returns a token > 0, or a negative LOOM_E_* code. */
LOOM_API int64_t loom_subscribe(LoomContext* ctx, const char* event, LoomEventCallback cb, void* user_data);
LOOM_API int loom_unsubscribe(LoomContext* ctx, int64_t token);
/* Emits a custom event (payload_json may be NULL). */
LOOM_API int loom_emit(LoomContext* ctx, const char* event, const char* payload_json);

/* ── Conversations & messages ──────────────────────────────────────── */

/* [conversation...] ordered by updated desc; limit <= 0 -> 50. */
LOOM_API const char* loom_list_conversations(LoomContext* ctx, int limit);
LOOM_API const char* loom_create_conversation(LoomContext* ctx, const char* title);
LOOM_API const char* loom_get_conversation(LoomContext* ctx, const char* conv_id);
/* patch_json: {"title"?, "source"?, "metadata"?} -> updated conversation. */
LOOM_API const char* loom_update_conversation(LoomContext* ctx, const char* conv_id, const char* patch_json);
LOOM_API int loom_delete_conversation(LoomContext* ctx, const char* conv_id);
/* Active messages of a conversation, oldest first. */
LOOM_API const char* loom_get_messages(LoomContext* ctx, const char* conv_id);
/* include_all != 0: every status (versions, excluded, deleted). */
LOOM_API const char* loom_get_messages_ex(LoomContext* ctx, const char* conv_id, int include_all);
LOOM_API const char* loom_get_message(LoomContext* ctx, const char* msg_id);
/* Creates a new version of msg_id with new_text (old version kept as
 * "version"); returns the new message. */
LOOM_API const char* loom_edit_message(LoomContext* ctx, const char* msg_id, const char* new_text);
/* Makes msg_id the active version of its group. */
LOOM_API int loom_restore_version(LoomContext* ctx, const char* msg_id);
/* All versions of the group of msg_id (or of a "vg_" id), by version_num. */
LOOM_API const char* loom_get_versions(LoomContext* ctx, const char* msg_or_group_id);
/* status: active | excluded | version | deleted */
LOOM_API int loom_set_message_status(LoomContext* ctx, const char* msg_id, const char* status);
/* patch_json: any of {"text","weight","metadata","attachments","model",...}. */
LOOM_API int loom_update_message(LoomContext* ctx, const char* msg_id, const char* patch_json);
/* Full-text search (FTS5, LIKE fallback). options_json (nullable):
 * {"limit":50,"conv_id":null,"include_inactive":false,"mode":"auto|fts5|like"}
 * -> {"mode":"fts5|like","results":[message + "score" + "snippet"]} */
LOOM_API const char* loom_search(LoomContext* ctx, const char* query, const char* options_json);

/* ── Chat ──────────────────────────────────────────────────────────── */

/* Sends user_message to conv_id (NULL -> current/new conversation) with
 * model_id (NULL -> config default_model) and graph context_depth (< 0 ->
 * config graph_memory_depth, 0 -> no graph context). Streams chunks to
 * callback (see top of file); blocks until done. */
LOOM_API void loom_chat(LoomContext* ctx, const char* conv_id, const char* user_message, const char* model_id,
                        int context_depth, LoomStreamCallback callback, void* user_data);
/* request_json: {"message" (required), "request_id"?, "conv_id"?, "model"?,
 * "attachments"?:[paths], "web_search"?, "deep_research"?,
 * "reasoning_effort"?, "temperature"?, "max_tokens"?, "system_prompt"?,
 * "context_depth"?, "stream"?:true}. Streams like loom_chat (callback may be
 * NULL) and also returns the final result JSON (or error JSON). */
LOOM_API const char* loom_chat_ex(LoomContext* ctx, const char* request_json, LoomStreamCallback callback,
                                  void* user_data);
/* Cancels an in-flight loom_chat_ex by its request_id (the partial answer is
 * kept). LOOM_E_NOT_FOUND if no such request is running. */
LOOM_API int loom_chat_cancel(LoomContext* ctx, const char* request_id);

/* ── Models & providers ────────────────────────────────────────────── */

LOOM_API const char* loom_get_models(LoomContext* ctx);
/* Fetches the model list from the API -> {"count": n} */
LOOM_API const char* loom_refresh_models(LoomContext* ctx);
/* {"providers":[{"id","available","capabilities":[...]}]} (no secrets). */
LOOM_API const char* loom_get_providers(LoomContext* ctx);
/* 1 if an available provider offers capability on resource, 0 if not,
 * negative on error. constraints_json may be NULL. */
LOOM_API int loom_can(LoomContext* ctx, const char* resource, const char* capability, const char* constraints_json);

/* ── Config & secrets ──────────────────────────────────────────────── */

LOOM_API const char* loom_get_config(LoomContext* ctx);
/* value is JSON text ("0.5", "true", "\"x\""); text that is not valid JSON is
 * stored as a string. Saved to config.json immediately. */
LOOM_API void loom_set_config(LoomContext* ctx, const char* key, const char* value);
/* Merges an object of settings and saves once. */
LOOM_API int loom_set_config_json(LoomContext* ctx, const char* patch_json);
/* Secrets are write-only through the ABI: values are never returned. */
LOOM_API int loom_set_secret(LoomContext* ctx, const char* key, const char* value);
/* 1 = set and non-empty, 0 = not set, negative = error. */
LOOM_API int loom_has_secret(LoomContext* ctx, const char* key);
LOOM_API int loom_delete_secret(LoomContext* ctx, const char* key);
/* ["api_key", ...] names only. */
LOOM_API const char* loom_list_secret_keys(LoomContext* ctx);

/* ── Knowledge graph & context ─────────────────────────────────────── */

/* filter_json (nullable): {"kind"?, "label"?, "limit"?:200} -> [node...] */
LOOM_API const char* loom_get_nodes(LoomContext* ctx, const char* filter_json);
/* filter_json (nullable): {"node_id"?, "link_type"?, "limit"?} -> [edge...] */
LOOM_API const char* loom_get_edges(LoomContext* ctx, const char* filter_json);
/* seed_ids_json: ["n_...","m_..."] -> {"nodes":[...],"edges":[...]} */
LOOM_API const char* loom_expand_graph(LoomContext* ctx, const char* seed_ids_json, int depth);
/* Python Database.get_graph_data(conv_id) (conv_id may be NULL). */
LOOM_API const char* loom_get_graph_data(LoomContext* ctx, const char* conv_id);
/* Re-runs graph extraction for conv_id (NULL = every conversation) -> {"reindexed": n} */
LOOM_API const char* loom_graph_reindex(LoomContext* ctx, const char* conv_id);
/* ContextSet for text: {"items":[...],"prompt_text","token_estimate","truncated"} */
LOOM_API const char* loom_select_context(LoomContext* ctx, const char* text, int depth, int max_tokens);
/* request_json: {"text","depth","max_tokens","conv_id","include_memory","include_graph","include_search"} */
LOOM_API const char* loom_select_context_ex(LoomContext* ctx, const char* request_json);

/* ── Semantic worker ───────────────────────────────────────────────── */

/* {"pending","processed","errors","mode","rate","batch_id","batch_submitted","running","paused"} */
LOOM_API const char* loom_semantic_status(LoomContext* ctx);
LOOM_API void loom_semantic_pause(LoomContext* ctx);
LOOM_API void loom_semantic_resume(LoomContext* ctx);
LOOM_API void loom_semantic_wake(LoomContext* ctx);

/* ── Memory tree ───────────────────────────────────────────────────── */

/* {"nodes":[memory node...]} in insertion order. */
LOOM_API const char* loom_list_memory(LoomContext* ctx);
/* json: {"content", "parent_id"?, "node_type"?:"text", "metadata"?, "tags"?} -> node */
LOOM_API const char* loom_create_memory(LoomContext* ctx, const char* json);
/* json: any of {"content","active","weight","tags","metadata","node_type","parent_id"} -> node */
LOOM_API const char* loom_update_memory(LoomContext* ctx, const char* id, const char* json);
/* Deletes the node and its descendants (the ChatADHD UI behaviour). */
LOOM_API int loom_delete_memory(LoomContext* ctx, const char* id);
/* {"context": "<active memory text>"}; max_chars <= 0 -> 16000. */
LOOM_API const char* loom_get_memory_context(LoomContext* ctx, int max_chars);

/* ── Import / export ───────────────────────────────────────────────── */

/* {"format": "zip|sqlite|json|jsonl|html|mht|screenshot|markdown|text|unknown"} */
LOOM_API const char* loom_detect_format(LoomContext* ctx, const char* path);
/* Imports path (title may be NULL); cb (nullable) gets progress on the
 * calling thread. -> {"conversations":[...],"format","source_id",
 * "blob_hash","messages","cancelled","warnings"} */
LOOM_API const char* loom_import_file(LoomContext* ctx, const char* path, const char* title, LoomProgressCallback cb,
                                      void* ud);
/* Like loom_import_file with options_json (nullable):
 * {"title"?, "force"?: false, "record_provenance"?: true}. force re-imports a
 * file whose bytes were already imported (otherwise the prior conversations
 * are returned with "already_imported": true). */
LOOM_API const char* loom_import_file_ex(LoomContext* ctx, const char* path, const char* options_json,
                                         LoomProgressCallback cb, void* ud);
/* fmt: json | markdown | text | html -> {"format","content"} */
LOOM_API const char* loom_export_conversation(LoomContext* ctx, const char* conv_id, const char* fmt);

/* ── Provenance, events & tasks ────────────────────────────────────── */

/* [source...] newest first; limit <= 0 -> 100. */
LOOM_API const char* loom_list_sources(LoomContext* ctx, int limit);
/* {"subject_id","records":[provenance...],"sources":{source_id: source}} */
LOOM_API const char* loom_get_provenance(LoomContext* ctx, const char* subject_id);
/* query_json (nullable): {"after_seq","type","subject_id","limit"} -> [event...] */
LOOM_API const char* loom_query_events(LoomContext* ctx, const char* query_json);
/* filter_json (nullable): {"kind","status","parent_id","limit"} -> [task...] */
LOOM_API const char* loom_list_tasks(LoomContext* ctx, const char* filter_json);
LOOM_API const char* loom_get_task(LoomContext* ctx, const char* task_id);
/* Recovers interrupted tasks and wakes the workers -> {"recovered": n} */
LOOM_API const char* loom_resume_tasks(LoomContext* ctx);
LOOM_API int loom_cancel_task(LoomContext* ctx, const char* task_id);

/* ── Archive Intelligence / Project Compiler ───────────────────────── */

/* Runs (or resumes) the archive pipeline and blocks until it finishes or is
 * cancelled. config_json: {"sources":[paths], "repo"?, "code"?:true,
 * "git"?:true, "seed_terms"?:[...], "out_dir"?, "project"?, "max_passes"?,
 * "max_new_terms"?, "max_hits_per_term"?, "max_synthesis_rounds"?,
 * "llm"?:"off"|"auto", "include_db"?, "exclude"?:[...], "max_file_bytes"?,
 * "force"?}. cb (nullable) receives (current, total, "<stage>: <message>");
 * total is -1 when unknown, and it may be called from a Loom worker thread.
 * -> {"run_id","status":"done|paused|cancelled","stages":[{"stage","task_id",
 *    "input_hash","output_hash","cache_hit","resumed","stats"}],"summary":{...}}
 * Unchanged inputs are cache hits; a paused run resumes on the next call with
 * the same inputs. */
LOOM_API const char* loom_archive_run(LoomContext* ctx, const char* config_json, LoomProgressCallback cb, void* ud);
/* Asks the running loom_archive_run to pause at its next checkpoint.
 * LOOM_E_NOT_FOUND when no run is in progress. */
LOOM_API int loom_archive_cancel(LoomContext* ctx);
/* run_id NULL -> latest run. -> {"run":{...}|null,"stages":[...],"artifacts":[...]} */
LOOM_API const char* loom_archive_status(LoomContext* ctx, const char* run_id);
/* filter_json (nullable): {"kind"?, "run_id"?, "limit"?:100} -> [artifact...] newest first */
LOOM_API const char* loom_list_artifacts(LoomContext* ctx, const char* filter_json);
/* -> {"artifact":{...},"content":"..."}; content omitted when include_content == 0 */
LOOM_API const char* loom_get_artifact(LoomContext* ctx, const char* artifact_id, int include_content);

/* ── Knowledge layer: data pack, store, judgements, pipeline ──────── */
/* (LOOM_CONCEPTUAL_MODEL; include/loom/knowledge.h, knowledge_store.h) */

/* Data pack manifest: {"id","version","hash","files":[{"path","schema","sha256"}]} */
LOOM_API const char* loom_kb_pack(LoomContext* ctx);
/* One pack file by name: "evidence_encoding" (policy/), "goal_types",
 * "anchoring", or any pack path ("project_kinds/film.json") -> the document. */
LOOM_API const char* loom_kb_policy(LoomContext* ctx, const char* name);
/* Knowledge runs, newest first: [{"id","archive_run_id","pack_hash","status","inputs","summary","created"}] */
LOOM_API const char* loom_kb_runs(LoomContext* ctx, int limit);
/* query_json: {"run"?(default latest done run),"what":"claims|entities|instances|slots|principles|
 * operators|morphisms|decisions|forks|areas|predictions|models|products|status_history|stats",
 * filters: "subject","predicate","object","evidence","origin","status","observation","branch" (claims),
 * "kind","canonical_key","alias_key","parent" (entities), "paradigm","subject" (instances),
 * "instance","role","slot","claim" (slots), "entity" (status_history), "limit"}
 * -> {"run","items":[model objects]} (stats: {"run","items":{table: rows}}) */
LOOM_API const char* loom_kb_query(LoomContext* ctx, const char* query_json);
/* Appends an owner judgement (append-only, replayed last on every rebuild):
 * {"target_kind","target","verdict":"confirm|reject|edit|merge|split","payload"?,"reason"?}
 * -> the stored judgement {"id","seq",...}. Add {"replay_run":"kr_..."} to apply it now. */
LOOM_API const char* loom_kb_judge(LoomContext* ctx, const char* judgement_json);
/* Runs (or resumes) the knowledge pipeline; blocks. config_json: {"sources","repo"?,"stages"?,
 * "prior_cut"?,"priors"?,"llm"?,"out_dir"?,"project"?,"force"?,"stage_params"?}. cb receives
 * (current, total, "<stage>: <message>"), possibly from a worker thread.
 * -> {"task_id","run","pack_hash","status":"done|paused|failed|cancelled","error","stages":[...],"summary"} */
LOOM_API const char* loom_knowledge_run(LoomContext* ctx, const char* config_json, LoomProgressCallback cb, void* ud);
/* Pauses the running loom_knowledge_run at its next checkpoint (LOOM_E_NOT_FOUND when none). */
LOOM_API int loom_knowledge_cancel(LoomContext* ctx);
/* task_id NULL -> latest. -> {"run":{"task_id","knowledge_run","config"}|null,"stages":[...]} */
LOOM_API const char* loom_knowledge_status(LoomContext* ctx, const char* task_id);

/* ── Knowledge areas (next wave; currently return not_implemented) ─── */
/* Catalog (include/loom/catalog.h). JSON shapes are documented there. */
LOOM_API const char* loom_catalog_scan(LoomContext* ctx, const char* config_json, LoomProgressCallback cb, void* ud);
LOOM_API const char* loom_catalog_score(LoomContext* ctx, const char* config_json, LoomProgressCallback cb, void* ud);
/* Applies existing selection policy and owner overrides without importing.
 * run_id NULL/empty -> latest score run; -> {"decisions":[Decision]}. */
LOOM_API const char* loom_catalog_select(LoomContext* ctx, const char* run_id);
/* UnitQuery JSON -> [CatalogUnit] */
LOOM_API const char* loom_catalog_query(LoomContext* ctx, const char* query_json);
LOOM_API const char* loom_catalog_preview(LoomContext* ctx, const char* unit_id);
/* {"unit_id","action":"include|exclude|pin","reason"?} -> {"decisions":[...]} */
LOOM_API const char* loom_catalog_override(LoomContext* ctx, const char* override_json);
LOOM_API const char* loom_catalog_import(LoomContext* ctx, const char* options_json, LoomProgressCallback cb, void* ud);
/* Extract + resolve (extract.h, resolve.h). */
/* Detect, segment and extract one file without storing: -> Extraction JSON */
LOOM_API const char* loom_extract_preview(LoomContext* ctx, const char* path, const char* options_json);
/* {"snapshot":dir,"repo":dir} -> LineageResult JSON */
LOOM_API const char* loom_resolve_lineage(LoomContext* ctx, const char* request_json);
/* Generalize (generalize.h): {"run"?,"cut"} -> {"predictions":[...]} */
LOOM_API const char* loom_generalize_predict(LoomContext* ctx, const char* request_json);
/* Context + materialize (context_engine.h, materialize.h). */
/* ContextRequest JSON -> {"context_set":{...},"text":"..."} */
LOOM_API const char* loom_context_build(LoomContext* ctx, const char* request_json);
/* {"kind":"self_description|dossier|backlog|extrapolated_spec","run"?,"instance"?} -> Rendered JSON */
LOOM_API const char* loom_materialize(LoomContext* ctx, const char* request_json);

/* ── Platform HTTP injection ───────────────────────────────────────── */

/* Opaque response sink handed to the platform send function. */
typedef struct LoomHttpResponse LoomHttpResponse;
/* Called by Loom (on a Loom thread) for every HTTP request. request_json:
 * {"method","url","headers":{...},"body","timeout_ms","stream"} (body is a
 * UTF-8 string; binary bodies are base64 in "body_base64"). The function
 * must block until the exchange is complete: call loom_http_response_begin
 * once, then loom_http_response_write for body bytes (any chunking), then
 * return LOOM_OK - or call loom_http_response_fail / return a negative code
 * on transport errors. */
typedef int (*LoomHttpSendFn)(const char* request_json, LoomHttpResponse* response, void* user_data);
/* headers_json: {"name":"value",...} (nullable). Returns LOOM_E_CANCELLED
 * when Loom no longer wants the response (stop reading and return). */
LOOM_API int loom_http_response_begin(LoomHttpResponse* response, int status, const char* headers_json);
LOOM_API int loom_http_response_write(LoomHttpResponse* response, const char* data, size_t len);
/* error_code: a LOOM_E_* code (e.g. LOOM_E_NETWORK, LOOM_E_TIMEOUT). */
LOOM_API int loom_http_response_fail(LoomHttpResponse* response, int error_code, const char* message);
/* 1 if Loom requested cancellation of this request (poll between reads). */
LOOM_API int loom_http_response_cancelled(const LoomHttpResponse* response);
/* Routes all Loom HTTP through fn (NULL restores the built-in transport). */
LOOM_API int loom_set_http_transport(LoomContext* ctx, LoomHttpSendFn fn, void* user_data);

/* ── Encryption (AES-256-GCM, PBKDF2-SHA256 600k; Python-compatible) ─ */

/* {"available","configured","unlocked","kdf","iterations","cipher"} */
LOOM_API const char* loom_crypto_status(LoomContext* ctx);
LOOM_API int loom_crypto_setup(LoomContext* ctx, const char* password);
LOOM_API int loom_crypto_unlock(LoomContext* ctx, const char* password);
LOOM_API int loom_crypto_lock(LoomContext* ctx);
/* -> {"v","ct","n","s","i"} (core/crypto.py EncryptedBlob format) */
LOOM_API const char* loom_crypto_encrypt(LoomContext* ctx, const char* plaintext);
/* blob_json: EncryptedBlob JSON -> {"text": "..."} */
LOOM_API const char* loom_crypto_decrypt(LoomContext* ctx, const char* blob_json);

/* ── Media (ASR / OCR) ─────────────────────────────────────────────── */

/* options_json (nullable): {"provider","language"} -> {"text","confidence","alternatives","language","duration"} */
LOOM_API const char* loom_transcribe(LoomContext* ctx, const char* audio_path, const char* options_json);
/* options_json (nullable): {"provider","language"} -> {"text","confidence","lines","language"} */
LOOM_API const char* loom_ocr(LoomContext* ctx, const char* image_path, const char* options_json);
/* {"asr":{"available":[...],"configured"},"ocr":{...}} */
LOOM_API const char* loom_media_status(LoomContext* ctx);

/* ── GitHub sync ───────────────────────────────────────────────────── */

/* request_json: {"action":"test|status|pull|push|sync", "repo", "branch"?,
 * "local_path"?, "direction"?, "files"?:[paths]} (token from secrets
 * github_token) -> action-specific result JSON. */
LOOM_API const char* loom_github_sync(LoomContext* ctx, const char* request_json);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOOM_LOOM_H */
