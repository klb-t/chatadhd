// loom/db.h — port of engine/db.py (Database) + Loom schema extensions.
//
// Invariants (hard contract, proven by tests/compat/test_db_compat.py):
//   * The core schema (_meta, conversations, messages, nodes, links + indexes)
//     is byte-for-byte the Python DDL, and _meta.schema_version is "4".
//   * Python engine.db.Database can open and fully use a DB written by Loom,
//     and Loom can open and fully use a DB written by Python (any legacy
//     version: missing tables/columns are migrated forward exactly like
//     Database._migrate()).
//   * IDs: prefix + 12 lowercase hex (util/ids.h). Timestamps:
//     datetime.utcnow().isoformat()+"Z" (util/time.h). JSON columns are
//     written with Python json.dumps() formatting.
//   * Loom-only state lives in tables prefixed `loom_` (tracked by
//     _meta.loom_schema_version) and never changes core-table semantics.
//     There are no triggers on core tables.
//   * Full-text search is DERIVED state stored in a separate index file
//     (chatadhd.fts.db, attached as schema "fts"): deleting it is always safe; it
//     is rebuilt/reconciled automatically. The main DB never contains virtual
//     tables, so a Python/SQLite without FTS5 can still VACUUM and migrate it.
//
// Thread safety: every public method takes the recursive mutex (Python RLock),
// so the object is safe to share between the UI thread and workers.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/sqlite.h"
#include "loom/util/json.h"

namespace loom {

inline constexpr int kSchemaVersion = 4;       // Python _SCHEMA_VERSION
inline constexpr int kLoomSchemaVersion = 1;   // _meta.loom_schema_version
inline constexpr int kMessageAnnotationSchemaVersion = 1;  // independent, additive extension
inline constexpr int kImportCheckpointSchemaVersion = 1;   // independent, additive extension

// Message.status values (Python set_msg_status whitelist).
namespace msg_status {
inline constexpr std::string_view kActive = "active";
inline constexpr std::string_view kExcluded = "excluded";
inline constexpr std::string_view kVersion = "version";
inline constexpr std::string_view kDeleted = "deleted";
bool is_valid(std::string_view s) noexcept;
}  // namespace msg_status

struct Conversation {
  std::string id;
  std::string title;
  std::string created;
  std::string updated;
  std::string source = "user";
  Json metadata = Json::object();

  // All columns; metadata parsed. (Python list_convs/get_conv return the raw
  // metadata string - consumers of the C API get parsed JSON instead.)
  Json to_json() const;
};

struct Message {
  std::string id;
  std::string conv_id;
  std::optional<std::string> parent_id;
  std::string role;
  std::string text;
  std::optional<std::string> model;
  std::string status = "active";
  std::optional<std::string> version_group_id;
  std::int64_t version_num = 1;
  double weight = 1.0;
  Json attachments = Json::array();
  Json metadata = Json::object();
  std::string created;
  std::string semantic_status = "pending";

  // Same keys as Python dict(row) with attachments/metadata parsed
  // (Python get_msgs/get_msg shape).
  Json to_json() const;
};

// Row subset returned by get_unanalysed_msgs (Python: id, conv_id, role, text,
// metadata). metadata is parsed here (Python returns the raw string).
struct PendingMessage {
  std::string id;
  std::string conv_id;
  std::string role;
  std::string text;
  Json metadata = Json::object();
  Json to_json() const;
};

struct Node {
  std::string id;
  std::string kind = "entity";
  std::string label;
  std::string content;
  Json tags = Json::array();
  Json metadata = Json::object();
  std::string created;
  Json to_json() const;
};

struct Link {
  std::string id;
  std::string src;
  std::string dst;
  std::string link_type = "related";
  double weight = 1.0;
  Json metadata = Json::object();
  std::string created;
  Json to_json() const;  // metadata parsed (Python get_links returns the raw string)
};

// Arguments of Python create_msg(conv_id, text, role, model=None,
// parent_id=None, attachments=None, version_group_id=None, weight=1.0,
// metadata=None).
struct NewMessage {
  std::string conv_id;
  std::string text;
  std::string role;
  std::optional<std::string> model;
  std::optional<std::string> parent_id;
  Json attachments = Json::array();   // null/empty -> "[]"
  std::optional<std::string> version_group_id;  // empty/none -> new group, version 1
  double weight = 1.0;
  Json metadata = Json::object();     // null/empty -> "{}"
};

// One element of Python batch_create_msgs(messages): {role, text|content,
// model?, parent_id?, weight?, attachments?, metadata?}. Blank text rows are
// skipped (Python `if not text or not text.strip()`).
struct BatchMessage {
  std::string role = "user";
  std::string text;
  std::optional<std::string> model;
  std::optional<std::string> parent_id;
  double weight = 1.0;
  Json attachments = Json::array();
  Json metadata = Json::object();

  // From a Python-style dict (text falls back to "content").
  static BatchMessage from_json(const Json& j);
};

// Python update_conv(cid, **kwargs): only set fields are written; `updated`
// is always refreshed.
struct ConvPatch {
  std::optional<std::string> title;
  std::optional<std::string> source;
  std::optional<Json> metadata;
  std::optional<std::string> created;
  static Result<ConvPatch> from_json(const Json& j);  // unknown keys -> InvalidArgument
};

// Python update_msg(mid, **kwargs).
struct MsgPatch {
  std::optional<std::string> conv_id;
  std::optional<std::optional<std::string>> parent_id;  // outer = present, inner = NULL
  std::optional<std::string> role;
  std::optional<std::string> text;
  std::optional<std::optional<std::string>> model;
  std::optional<std::string> status;
  std::optional<std::optional<std::string>> version_group_id;
  std::optional<std::int64_t> version_num;
  std::optional<double> weight;
  std::optional<Json> attachments;
  std::optional<Json> metadata;
  std::optional<std::string> created;
  std::optional<std::string> semantic_status;
  bool empty() const noexcept;
  static Result<MsgPatch> from_json(const Json& j);
};

// Python update_node(nid, **kwargs).
struct NodePatch {
  std::optional<std::string> kind;
  std::optional<std::string> label;
  std::optional<std::string> content;
  std::optional<Json> tags;
  std::optional<Json> metadata;
  std::optional<std::string> created;
  bool empty() const noexcept;
  static Result<NodePatch> from_json(const Json& j);
};

struct NodeOptions {
  std::string content;
  Json tags = Json::array();
  Json metadata = Json::object();
  std::optional<std::string> node_id;  // explicit ID (INSERT OR IGNORE)
};

// ── Full-text search ────────────────────────────────────────────────
enum class FtsMode { Auto, Fts5, Like };

struct SearchOptions {
  int limit = 50;
  std::optional<std::string> conv_id;
  bool include_inactive = false;   // default: status = 'active' only
  FtsMode mode = FtsMode::Auto;
};

struct SearchHit {
  Message message;
  double score = 0.0;    // higher = better (negated bm25 for FTS5)
  std::string snippet;   // text excerpt with [..] around matches
  Json to_json() const;
};

struct SearchResult {
  std::string mode;      // "fts5" | "like"
  std::vector<SearchHit> hits;
  Json to_json() const;
};

struct FtsStatus {
  bool available = false;    // FTS5 usable (module present + index attached)
  std::string path;          // index file
  std::int64_t indexed = 0;  // documents in index
  std::int64_t messages = 0; // rows in messages
  bool dirty = false;        // pending changes not yet synced
  std::int64_t rebuilds = 0; // self-heal rebuilds since open
  Json to_json() const;
};

struct DbOptions {
  bool enable_fts = true;
  // Index location; default: db path with extension ".fts.db" (chatadhd.fts.db).
  std::optional<std::filesystem::path> fts_path;
  int busy_timeout_ms = 30000;
};

// Immutable full-message source, deduplicated by message ID + full record hash.
// Survives deletion of the mutable message. The original text, attachments,
// metadata and version identifiers remain in `message`.
struct MessageAnnotationSource {
  std::string id;
  std::string message_id;
  std::string text_hash;    // SHA-256 of exact UTF-8 text bytes
  std::string record_hash;  // SHA-256 of persisted full-message JSON bytes
  Json message = Json::object();
  std::string created;
  Json to_json() const;
};

// Ranges are half-open Unicode codepoint indices, not grapheme clusters or
// UTF-8 byte indices. Derived byte offsets/excerpt always refer to the immutable
// source, even after a direct update_msg or a deletion of the current message.
// Every correction/retraction appends a revision; old provenance is retained.
struct MessageAnnotation {
  std::string id;
  std::int64_t revision = 1;
  std::string source_snapshot_id;
  std::string message_id;
  std::string node_id;
  std::int64_t start_char = 0;
  std::int64_t end_char = 0;
  std::int64_t start_byte = 0;
  std::int64_t end_byte = 0;
  std::string origin = "recorded";  // recorded | model | user
  std::string state = "active";     // active | retracted
  Json metadata = Json::object();
  std::string created;
  std::string text_hash;
  std::string excerpt;
  bool message_exists = false;
  bool source_matches_current = false;  // exact text match, false when missing
  bool node_exists = false;
  Json to_json() const;
};

struct NewMessageAnnotation {
  std::string message_id;
  std::string node_id;
  std::int64_t start_char = 0;
  std::int64_t end_char = 0;
  std::string origin = "recorded";
  Json metadata = Json::object();
  // Optional optimistic source binding: mismatch => Conflict, no writes.
  std::optional<std::string> expected_text_hash;
};

struct MessageAnnotationPatch {
  std::optional<std::string> node_id;
  std::optional<std::int64_t> start_char;
  std::optional<std::int64_t> end_char;
  std::optional<std::string> origin;
  std::optional<std::string> state;
  std::optional<Json> metadata;
};

struct MessageAnnotationListOptions {
  // Preset, caller may choose any nonnegative limit; nullopt means unbounded.
  std::optional<std::int64_t> limit = 100;
  // Stable ID order; pass the last returned ID to obtain the next page.
  std::optional<std::string> after_id;
  std::optional<std::string> node_id;
  bool include_retracted = false;
};

class Database {
 public:
  // Opens (creating if needed) and migrates: Python _init_schema + _migrate,
  // then Loom tables, then attaches/validates the FTS index.
  static Result<std::unique_ptr<Database>> open(const std::filesystem::path& path, const DbOptions& opts = {});
  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  const std::filesystem::path& path() const noexcept { return path_; }

  // ── Meta ──────────────────────────────────────────────────────────
  Result<std::optional<std::string>> get_meta(std::string_view key);
  Status set_meta(std::string_view key, std::string_view value);
  Result<int> schema_version();

  // ── Conversations ─────────────────────────────────────────────────
  // Python returns {"id","title","created","updated"}; the struct also has
  // source="user" and metadata={} (the column defaults).
  Result<Conversation> create_conv(std::string_view title = "New Chat");
  Result<std::vector<Conversation>> list_convs(int limit = 50);  // ORDER BY updated DESC
  Result<std::optional<Conversation>> get_conv(std::string_view cid);
  Status update_conv(std::string_view cid, const ConvPatch& patch = {});
  Status delete_conv(std::string_view cid);  // deletes its messages first

  // ── Messages ──────────────────────────────────────────────────────
  // Returns the new message id. With version_group_id set: version_num =
  // MAX+1 and the group's active rows become 'version'. Touches the
  // conversation's `updated`.
  Result<std::string> create_msg(const NewMessage& m);
  // 1000-row transactions; returns inserted count; blank texts skipped.
  Result<int> batch_create_msgs(std::string_view conv_id, const std::vector<BatchMessage>& messages,
                                int batch_size = 1000);
  Result<std::vector<PendingMessage>> get_unanalysed_msgs(int limit = 100);
  Result<std::int64_t> count_pending_semantic();
  // Stamps semantic_source/entity_count/topic_count/summary[:200]/sentiment
  // into metadata and flips semantic_status to 'done'. No-op if missing.
  Status mark_analysed(std::string_view msg_id, const Json& analysis);
  Result<std::vector<Message>> get_msgs(std::string_view conv_id, bool include_all = false);  // ORDER BY created
  Result<std::optional<Message>> get_msg(std::string_view mid);
  Result<std::vector<Message>> get_versions(std::string_view version_group_id);  // ORDER BY version_num
  Status set_msg_status(std::string_view mid, std::string_view status);  // InvalidArgument if not whitelisted
  Status update_msg(std::string_view mid, const MsgPatch& patch);
  // New version of `mid` in the same group (copies role/model/parent/...).
  // Returns nullopt when mid does not exist.
  Result<std::optional<std::string>> edit_msg(std::string_view mid, std::string_view new_text);
  // Makes `mid` the active version of its group. false if mid does not exist.
  Result<bool> restore_version(std::string_view mid);

  // ── Message → node annotations (Loom extension) ──────────────────
  // Node/message must exist on creation. Source snapshots and revision rows
  // have no cascading FK to mutable core rows: deletion cannot erase evidence.
  Result<MessageAnnotation> create_message_annotation(const NewMessageAnnotation& annotation);
  Result<std::optional<MessageAnnotation>> get_message_annotation(std::string_view annotation_id);
  Result<std::vector<MessageAnnotation>> list_message_annotations(
      std::string_view message_id, const MessageAnnotationListOptions& opts = {});
  // expected_revision prevents lost updates; source identity is never retargeted.
  Result<MessageAnnotation> revise_message_annotation(std::string_view annotation_id,
      std::int64_t expected_revision, const MessageAnnotationPatch& patch);
  Result<std::vector<MessageAnnotation>> get_message_annotation_history(std::string_view annotation_id,
      std::int64_t after_revision = 0, std::optional<std::int64_t> limit = 100);
  Result<std::optional<MessageAnnotationSource>> get_message_annotation_source(std::string_view snapshot_id);

  // ── Links (graph edges) ───────────────────────────────────────────
  // Upsert on (src, dst, link_type): existing row gets weight+metadata
  // replaced and its id is returned.
  Result<std::string> create_link(std::string_view src, std::string_view dst, std::string_view link_type = "related",
                                  double weight = 1.0, const Json& metadata = Json::object());
  Result<std::vector<Link>> get_links(std::optional<std::string_view> node_id = std::nullopt,
                                      std::optional<std::string_view> link_type = std::nullopt);
  Status delete_link(std::string_view lid);

  // ── Nodes ─────────────────────────────────────────────────────────
  Result<std::string> create_node(std::string_view label, std::string_view kind = "entity",
                                  const NodeOptions& opts = {});
  Result<std::optional<Node>> get_node(std::string_view nid);
  Result<std::optional<Node>> find_node(std::string_view label, std::optional<std::string_view> kind = std::nullopt);
  Result<std::vector<Node>> list_nodes(std::optional<std::string_view> kind = std::nullopt, int limit = 200);
  Status update_node(std::string_view nid, const NodePatch& patch);
  Status delete_node(std::string_view nid);  // also deletes its links
  Result<std::string> get_or_create_node(std::string_view label, std::string_view kind = "entity",
                                         const NodeOptions& opts = {});

  // Python get_graph_data(conv_id=None): {"nodes":[...], "edges":[...]} with
  // exactly the Python keys (message nodes when conv_id given, all explicit
  // nodes via list_nodes() (limit 200), all links).
  Result<Json> get_graph_data(std::optional<std::string_view> conv_id = std::nullopt);

  // ── Full-text search (Loom extension) ─────────────────────────────
  Result<SearchResult> search_messages(std::string_view query, const SearchOptions& opts = {});
  FtsStatus fts_status();
  Status fts_sync();     // bring the index up to date (incremental)
  Status fts_rebuild();  // drop + rebuild from messages

  // ── Housekeeping ──────────────────────────────────────────────────
  void close();
  Status vacuum();

  // ── Access for sibling stores (provenance, tasks, relations) ──────
  // Lock first, then use conn(); keep the lock for the whole transaction.
  std::unique_lock<std::recursive_mutex> lock() const { return std::unique_lock(mu_); }
  sql::Connection& conn() noexcept { return conn_; }

 private:
  struct PrivateTag {};

 public:
  explicit Database(PrivateTag);  // use Database::open()

 private:
  Status init_schema();
  Status migrate();
  Status migrate_loom();
  Status migrate_message_extensions();
  Status update_conv_locked(std::string_view cid, const ConvPatch& patch);
  Result<std::optional<Node>> node_query(std::string_view sql, std::string_view a, std::optional<std::string_view> b);

  // FTS internals (src/db/fts.cpp).
  Status fts_open();
  Status fts_ensure_synced_locked();
  Status fts_reconcile_locked(bool full);
  Status fts_rebuild_locked();
  Status fts_index_ids_locked(const std::vector<std::string>& ids);
  void fts_note_changed(std::string_view msg_id);
  void fts_note_deletions();

  std::filesystem::path path_;
  DbOptions opts_;
  mutable std::recursive_mutex mu_;
  sql::Connection conn_;

  struct FtsState;
  std::unique_ptr<FtsState> fts_;
};

}  // namespace loom
