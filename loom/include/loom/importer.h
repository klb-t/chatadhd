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
//   JSON files > 5 MB are stream-parsed element by element (JsonArrayStreamer)
//     so memory stays bounded; non-arrays fall back to a full parse.
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

inline constexpr std::string_view kImporterParserVersion = "1";

// (current, total, status). Units: conversations when the total is known,
// otherwise bytes read; total = -1 when unknown.
using ImportProgressFn = std::function<void(std::int64_t current, std::int64_t total, std::string_view status)>;

struct ImportOptions {
  std::optional<std::string> title;
  ImportProgressFn progress;
  const CancelToken* cancel = nullptr;
  bool record_provenance = true;  // store raw bytes + sources + provenance rows
  std::int64_t stream_threshold_bytes = 5'000'000;  // Python: > 5 MB -> streaming JSON
};

struct ImportResult {
  std::vector<Conversation> conversations;
  std::string format;
  std::string source_id;   // loom_sources id ("" when provenance disabled)
  std::string blob_hash;
  std::int64_t messages = 0;
  bool cancelled = false;
  std::vector<std::string> warnings;
  // {"conversations":[...],"format","source_id","blob_hash","messages","cancelled","warnings"}
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
  Database& db_;
  EventBus& bus_;
  BlobStore* blobs_;
  ProvenanceStore* prov_;
  MediaProviders* media_;
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
