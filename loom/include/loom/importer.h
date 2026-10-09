// loom/importer.h — port of engine/importer.py (ConversationImporter) plus a
// conversation exporter.                                     [OWNER: wave 2 importer]
//
// Every Python entry point has a counterpart; the Python-private stages are
// public here so they can be tested one by one. All paths converge on
// import_message_list() -> Database::batch_create_msgs().
//
// Python parity (see engine/importer.py for the exact heuristics):
//   detect_format: by extension (.zip .db .json .jsonl/.ndjson .html/.htm
//     .mht/.mhtml .png/.jpg/.jpeg/.webp .md .txt/.log) else by the first 200
//     bytes (PK\x03\x04, "SQLite format", '{'/'[' after lstrip, "<html"/
//     "<!doctype" case-insensitive) else "unknown"
//   import_file: dispatch; unknown -> Errc::Unsupported; emits import:done
//     {"count","source","format"} when >= 1 conversation was created
//   import_message_list: title or "Import %Y-%m-%d %H:%M" (local time);
//     conversation "[Import] <title>"; skip "system"; role user|human ->
//     "user" else "assistant"; content from "content" or "text"; list
//     content -> text parts joined by "\n" ("[image]" for image_url parts);
//     non-strings -> str(); blank dropped; batch insert; emits import:done
//     {"conv_id","count","title"}
//   JSON routing (_import_json_data), ChatGPT "mapping" tree walk (roots =
//     parent null or unknown; depth-first over children in mapping order;
//     parts strings or {"content_type":"text","text"}; only user/assistant),
//     Claude export ("chat_messages", sender human -> user, text or content
//     blocks of type text), conversation objects (messages | chat_messages |
//     items | data; dict -> values), JSONL (lines with role/content batch into
//     one conversation, lines with "messages" are conversation objects),
//     SQLite (conversations+messages tables = Claude layout; conversation/
//     message tables = ChatGPT stub (no-op, as in Python); else every table
//     with role + content|text), HTML (tag parser: class contains human/user
//     or data-role human|user -> user; assistant/ai -> assistant; then regex
//     fallback; then whole text as one assistant message [:50000]), MHT
//     (split on "------=_", part with text/html, raw <html or base64 body),
//     screenshot (OCR through MediaProviders; < 10 chars -> error "Could not
//     extract text from image. Try a clearer screenshot."; _parse_chat_text
//     role heuristics), Markdown (## Human/## User/**Human**/**User** and
//     ## Assistant/## Claude/**Assistant**/**Claude** headers, ---/*** split),
//     text (Human|User|You: / Assistant|AI|Claude: blocks, DOTALL|IGNORECASE).
//   JSON conversation arrays > 5 MB are stream-parsed element by element
//     (JsonArrayStreamer). Direct message arrays use one conversation batch;
//     non-arrays fall back to a full parse. Unknown mappings are Unsupported,
//     distinct from recognized empty exports.
//   ZIP: members extracted to a temp dir (miniz), sorted, recursively
//     imported with the member's relative path as title hint; failures of one
//     member are logged and skipped.
// Loom additions (MEGA MASTER 2.F raw source immutable):
//   * the input file is stored in the BlobStore and registered as a
//     loom_sources row (kind "file" / "zip_member", format, parser
//     "loom.importer.<format>", parser_version kParserVersion);
//   * every created conversation and message gets a loom_provenance row
//     (locator: {"conversation_index", "message_index", "zip_member"?,
//     "json_path"?}, transform "import.<handler>@<version>");
//   * progress callback + cooperative cancellation (checked between
//     conversations; conversations already created are kept and reported).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/db.h"
#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {

class EventBus;
class BlobStore;
class ProvenanceStore;
class MediaProviders;

inline constexpr std::string_view kImporterParserVersion = "2";
inline constexpr std::string_view kExportParserVersion = "export-3";

// (current, total, status). Units depend on status: export conversations,
// ZIP entry/member progress, or legacy stream bytes. total = -1 when unknown.
using ImportProgressFn = std::function<void(std::int64_t current, std::int64_t total, std::string_view status)>;

// Provider-export interpretation (OpenAI/ChatGPT and Anthropic/Claude data
// exports; see docs/exports/OPENAI_ANTHROPIC_EXPORT_FORMATS.md).
//   Auto: ZIP archives go through the lossless provider-export path (which
//         falls back to the legacy per-member importers for archives that are
//         not provider exports, and reports what it could not interpret);
//         bare .json files keep the legacy Python-parity flattening
//         (tests/test_import.cpp and tests/compat/test_import_compat.py pin it).
//   Off:  legacy behaviour everywhere.
//   On:   as Auto, and bare .json files that look like a provider export
//         (ChatGPT `mapping` / Claude `chat_messages` objects) are interpreted
//         losslessly too.
enum class ExportMode { Auto, Off, On };

struct ImportResult;

// Typed projection of the import data preset. Values are supplied by the
// compiled resource, not a second set of defaults in native code.
struct ImportPresetValues {
  std::int64_t stream_threshold_bytes;
  std::size_t json_read_chunk_bytes;
  std::size_t json_max_depth;
  std::int64_t json_inline_threshold_bytes;
  std::int64_t generic_inference_max_bytes;
};

const ImportPresetValues& default_import_preset();

struct ImportOptions {
  std::optional<std::string> title;
  ExportMode export_mode = ExportMode::Auto;
  ImportProgressFn progress;
  const CancelToken* cancel = nullptr;
  bool record_provenance = true;  // store raw bytes + sources + provenance rows
  std::int64_t stream_threshold_bytes = default_import_preset().stream_threshold_bytes;
  // Loom addition (MEGA MASTER 2.F): when record_provenance finds a prior
  // loom_sources row with the same blob hash and parser_version, the import
  // is skipped and the prior conversations are returned instead of being
  // recreated. `force` re-imports (and re-registers a source row) anyway.
  bool force = false;
  // Resume provider-export conversations and interpreted ZIP records from
  // durable per-item checkpoints. Requires the source/provenance stores;
  // force starts a fresh source identity even when resume is enabled.
  // Library callers opt in to preserve the historical retry/new-source
  // behavior; the import CLI enables resume in its caller preset.
  bool resume = false;
  // Result projection only: complete metadata remains durably stored. Disable
  // for large-archive summary callers to avoid retaining every graph/unknown
  // metadata value in the returned vector. IDs/title/source remain available.
  bool include_result_metadata = true;
  // Optional admission binding for the outer file (nested ZIP members inherit
  // the admitted archive, not its byte/hash identity). Size is checked before
  // copying; the snapshot hash is checked before source/conversation writes.
  std::optional<std::string> expected_source_hash;
  std::optional<std::int64_t> expected_source_bytes;
  // Caller presets for the provider JSON scanner; zero depth means unlimited.
  // Memory is bounded by this input buffer plus the largest conversation and
  // retained wrapper metadata, rather than the complete conversations array.
  std::size_t json_read_chunk_bytes = default_import_preset().json_read_chunk_bytes;
  std::size_t json_max_depth = default_import_preset().json_max_depth;
  std::int64_t json_inline_threshold_bytes = default_import_preset().json_inline_threshold_bytes;
  std::int64_t generic_inference_max_bytes = default_import_preset().generic_inference_max_bytes;  // zero = unlimited
  // Outer import_file execution hooks; stages remain available for offline
  // tools. Preflight runs before hashing/storage; completion follows durable
  // work and also runs for completed-source cache hits.
  std::function<Status(const std::filesystem::path&)> preflight;
  std::function<void(const ImportResult&)> completed;
};

struct ImportResult {
  std::vector<Conversation> conversations;
  std::string format;
  std::string source_id;   // loom_sources id ("" when provenance disabled)
  std::string blob_hash;
  std::int64_t messages = 0;
  bool cancelled = false;
  // True when this call found a prior source with the same blob hash and
  // parser version (see ImportOptions::force) and returned its prior
  // conversations instead of importing again.
  bool already_imported = false;
  bool resumed = false;  // an incomplete durable provider import was reused
  bool include_result_metadata = true;
  std::vector<std::string> warnings;
  // Provider-export interpretation report (null unless the lossless export
  // path ran): provider, member dispositions, counts, asset links, errors,
  // repairs. Shape documented in src/import/export_internal.h.
  Json export_report = nullptr;
  // {"conversations":[...],"format","source_id","blob_hash","messages","cancelled","warnings","already_imported"}
  // (+ "export_report" when set)
  Json to_json() const;
};

// Incremental splitter for a top-level JSON array (Python _iter_json_elements):
// feed() arbitrary chunks; each complete element's raw text is passed to the
// callback (strings/escapes/nesting tracked; commas at depth 0 separate).
class JsonArrayStreamer {
 public:
  using ElementFn = std::function<void(std::string_view element_json)>;
  // Returns false when the input is not a JSON array (first non-space byte).
  bool feed(std::string_view chunk, const ElementFn& on_element);
  bool finished() const noexcept { return done_; }
  // Bytes of an element that has started but not completed (truncated input).
  std::size_t pending_bytes() const noexcept { return buf_.size(); }

 private:
  std::string buf_;
  int depth_ = 0;
  bool started_ = false;
  bool in_string_ = false;
  bool escape_ = false;
  bool done_ = false;
  bool not_array_ = false;
};

class ConversationImporter {
 public:
  ConversationImporter(Database& db, EventBus& bus, BlobStore* blobs = nullptr, ProvenanceStore* prov = nullptr,
                       MediaProviders* media = nullptr);

  std::string detect_format(const std::filesystem::path& path) const;
  Result<ImportResult> import_file(const std::filesystem::path& path, const ImportOptions& opts = {});

  // Format handlers (Python import_<format>); return created conversations.
  Result<std::vector<Conversation>> import_zip(const std::filesystem::path& path, const ImportOptions& opts = {});
  Result<std::vector<Conversation>> import_jsonl(const std::filesystem::path& path, const ImportOptions& opts = {});
  Result<std::vector<Conversation>> import_sqlite(const std::filesystem::path& path, const ImportOptions& opts = {});
  Result<std::vector<Conversation>> import_json(const std::filesystem::path& path, const ImportOptions& opts = {});
  Result<std::vector<Conversation>> import_html(const std::filesystem::path& path, const ImportOptions& opts = {});
  Result<std::vector<Conversation>> import_mht(const std::filesystem::path& path, const ImportOptions& opts = {});
  Result<std::vector<Conversation>> import_screenshot(const std::filesystem::path& path,
                                                      const ImportOptions& opts = {});
  Result<std::vector<Conversation>> import_markdown(const std::filesystem::path& path,
                                                    const ImportOptions& opts = {});
  Result<std::vector<Conversation>> import_text(const std::filesystem::path& path, const ImportOptions& opts = {});

  // Stages (Python private helpers).
  Result<std::vector<Conversation>> import_json_data(const Json& data, const std::optional<std::string>& title);
  Result<std::optional<Conversation>> import_single_element(const Json& element, const std::optional<std::string>& title);
  Result<std::optional<Conversation>> import_chatgpt_mapping(const Json& conv, const std::optional<std::string>& title);
  Result<std::optional<Conversation>> import_claude_export(const Json& conv, const std::optional<std::string>& title);
  Result<std::optional<Conversation>> import_conversation_obj(const Json& conv, const std::optional<std::string>& title);
  Result<Conversation> import_message_list(const Json& messages, const std::optional<std::string>& title);
  Result<std::optional<Conversation>> parse_html_conversation(std::string_view html,
                                                              const std::optional<std::string>& title,
                                                              const std::optional<std::filesystem::path>& source);

  // Pure parsers (no DB access). Messages are [{"role","content"}].
  static Json parse_html_messages(std::string_view html);    // ConversationHTMLParser
  static Json extract_messages_regex(std::string_view html);  // _extract_messages_regex
  static std::string strip_html(std::string_view html);       // _strip_html
  static Json parse_chat_text(std::string_view text);         // _parse_chat_text (OCR text)
  static Json parse_markdown(std::string_view content);       // import_markdown's parser
  static Json parse_plain_text(std::string_view content);     // import_text's parser

 private:
  // Threaded through one import_file()/import_<format>() call tree (a single
  // ConversationImporter is not re-entrant across concurrent imports, same as
  // the rest of Loom's "one call at a time per instance" pieces). Set by the
  // RAII-ish prepare_source()/current_source_ pair in importer.cpp; consulted
  // by import_message_list() to attach provenance rows without threading an
  // extra parameter through every public Python-parity signature.
  struct SourceCtx {
    std::string source_id;
    std::string source_filename;
    std::string blob_hash;
    int conv_index = 0;
    bool resumed = false;
    std::optional<std::string> zip_member;
    std::optional<std::string> json_path;
    std::optional<std::int64_t> archive_index;
  };
  class SourceCtxGuard {
   public:
    SourceCtxGuard(ConversationImporter& self, SourceCtx* ctx) : self_(self), prev_(self.current_source_) {
      self_.current_source_ = ctx;
    }
    ~SourceCtxGuard() { self_.current_source_ = prev_; }
    SourceCtxGuard(const SourceCtxGuard&) = delete;
    SourceCtxGuard& operator=(const SourceCtxGuard&) = delete;

   private:
    ConversationImporter& self_;
    SourceCtx* prev_;
  };

  // Hashes+stores `path` (when record_provenance && blobs_/prov_ are set),
  // fills `ctx.source_id`/`blob_hash`, and registers a loom_sources row
  // (kind "file" or "zip_member"). When a prior source with the same hash
  // and parser_version already produced conversations (and !opts.force),
  // returns them instead of nullopt so the caller can short-circuit.
  Result<std::optional<std::vector<Conversation>>> prepare_source(const std::filesystem::path& path,
                                                                   std::string_view fmt, const ImportOptions& opts,
                                                                   std::string_view kind, SourceCtx& ctx,
                                                                   std::string_view parser_version = kImporterParserVersion,
                                                                   std::string_view parser_suffix = "");
  // Materializes one extracted ZIP entry in the existing source/blob model.
  // Its locator addresses the containing archive, including duplicate-name index.
  Result<Json> materialize_zip_member(const std::filesystem::path& path, const ImportOptions& opts,
                                      std::string_view member, std::int64_t archive_index);
  Status set_source_outcome(const SourceCtx& ctx, std::string_view status,
                            const std::vector<Conversation>& conversations, const Json& report);
  // Wraps a *_body() call with prepare_source()/SourceCtxGuard for the
  // public per-format entry points (import_zip, import_json, ...), which are
  // independently testable/callable and so each self-registers its source.
  Result<std::vector<Conversation>> with_source(const std::filesystem::path& path, std::string_view fmt,
                                                const ImportOptions& opts, std::string_view kind,
                                                const std::function<Result<std::vector<Conversation>>(const std::filesystem::path&)>& body);
  // import_file(), but lets zip recursion tag members as "zip_member" and
  // annotate their provenance locator with the member's path inside the zip.
  Result<ImportResult> import_file_as(const std::filesystem::path& path, const ImportOptions& opts,
                                      std::string_view source_kind,
                                      std::optional<std::string> zip_member_rel = std::nullopt,
                                      std::optional<std::int64_t> archive_index = std::nullopt);
  // Pure parse+insert bodies (no source/blob bookkeeping of their own; the
  // public import_<format>() wrappers and import_file_as() set up SourceCtx
  // around a call to these).
  Result<std::vector<Conversation>> zip_body(const std::filesystem::path& path, const ImportOptions& opts);
  // Lossless provider-export path (src/import/export_*.cpp). export_zip_body
  // handles every ZIP when ExportMode != Off; export_json_body returns nullopt
  // when a bare JSON file is not a provider export (caller falls back to the
  // legacy json_body). Both fill `report`.
  Result<std::vector<Conversation>> export_zip_body(const std::filesystem::path& path, const ImportOptions& opts,
                                                    Json& report);
  Result<std::optional<std::vector<Conversation>>> export_json_body(const std::filesystem::path& path,
                                                                     const ImportOptions& opts, Json& report);
  Result<std::vector<Conversation>> jsonl_body(const std::filesystem::path& path, const ImportOptions& opts);
  Result<std::vector<Conversation>> sqlite_body(const std::filesystem::path& path, const ImportOptions& opts);
  Result<std::vector<Conversation>> json_body(const std::filesystem::path& path, const ImportOptions& opts);
  Result<std::vector<Conversation>> html_body(const std::filesystem::path& path, const ImportOptions& opts);
  Result<std::vector<Conversation>> mht_body(const std::filesystem::path& path, const ImportOptions& opts);
  Result<std::vector<Conversation>> screenshot_body(const std::filesystem::path& path, const ImportOptions& opts);
  Result<std::vector<Conversation>> markdown_body(const std::filesystem::path& path, const ImportOptions& opts);
  Result<std::vector<Conversation>> text_body(const std::filesystem::path& path, const ImportOptions& opts);
  // Shared tail of import_message_list(): create_conv("[Import] "+title),
  // batch_create_msgs(batch), record_provenance(), emit import:done
  // {"conv_id","count","title"}. Used directly by handlers whose per-row
  // role/content normalisation differs from normalize_message() (the SQLite
  // importers: Python's _import_claude_db/_import_generic_db map roles
  // without the shared "system" role skip).
  Result<Conversation> finish_import(const std::string& conv_title, std::vector<BatchMessage> batch,
                                     std::string_view handler);
  // Records loom_provenance rows for one just-created conversation and its
  // messages. Provider exports propagate failure within their checkpoint
  // transaction; legacy handlers retain their best-effort logged policy.
  // No-op when no source/provenance store is active.
  Status record_provenance(const Conversation& conv, std::string_view handler);
  bool cancelled(const ImportOptions& opts) const noexcept { return opts.cancel && opts.cancel->cancelled(); }

  Database& db_;
  EventBus& bus_;
  BlobStore* blobs_;
  ProvenanceStore* prov_;
  MediaProviders* media_;
  SourceCtx* current_source_ = nullptr;
};

// Exports one conversation (active messages, or every version with
// include_all) — Loom addition backing loom_export_conversation.
//   "json":     {"conversation": {...}, "messages": [...]} (Database shapes)
//   "markdown": "# <title>\n\n## User\n<text>\n\n## Assistant\n..." (re-importable
//               by import_markdown)
//   "text":     "User: ...\n\nAssistant: ..." (re-importable by import_text)
//   "html":     minimal standalone page with data-role attributes
//               (re-importable by import_html)
class ConversationExporter {
 public:
  explicit ConversationExporter(Database& db) : db_(db) {}
  static std::vector<std::string> formats();  // {"json","markdown","text","html"}
  Result<std::string> export_conversation(std::string_view conv_id, std::string_view format, bool include_all = false);
  // Writes <dir>/<sanitised title>_<conv_id>.<ext> atomically; returns the path.
  Result<std::filesystem::path> export_to_file(std::string_view conv_id, std::string_view format,
                                               const std::filesystem::path& dir, bool include_all = false);

 private:
  Database& db_;
};

}  // namespace loom
