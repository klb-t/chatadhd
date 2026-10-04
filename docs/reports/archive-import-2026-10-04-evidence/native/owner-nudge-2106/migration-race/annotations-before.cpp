#include "loom/db.h"

#include <charconv>
#include <limits>
#include <unordered_set>

#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom {
namespace {

Status check_origin(std::string_view origin) {
  if (origin == "recorded" || origin == "model" || origin == "user") return {};
  return Error(Errc::InvalidArgument, "annotation origin must be recorded, model or user");
}

Status check_state(std::string_view state) {
  if (state == "active" || state == "retracted") return {};
  return Error(Errc::InvalidArgument, "annotation state must be active or retracted");
}

Status check_range(std::string_view text, std::int64_t start, std::int64_t end) {
  if (!utf8::is_valid(text)) return Error(Errc::InvalidArgument, "annotation source text must be valid UTF-8");
  if (start < 0 || end < start || static_cast<std::uint64_t>(end) > utf8::length(text))
    return Error(Errc::InvalidArgument, "annotation range must be within source Unicode codepoints [start,end)");
  return {};
}

Status check_limit(const std::optional<std::int64_t>& limit) {
  if (limit && *limit < 0) return Error(Errc::InvalidArgument, "annotation page limit must be nonnegative or absent");
  return {};
}

// The caller holds an IMMEDIATE transaction before any authoritative read.
Status insert_annotation(sql::Connection& conn, const MessageAnnotation& annotation) {
  return conn.run(
      "INSERT INTO loom_message_annotations "
      "(id,revision,source_snapshot_id,node_id,start_char,end_char,origin,state,metadata,created) "
      "VALUES (?,?,?,?,?,?,?,?,?,?)", annotation.id, annotation.revision, annotation.source_snapshot_id,
      annotation.node_id, annotation.start_char, annotation.end_char, annotation.origin, annotation.state,
      json::py_dumps(annotation.metadata), annotation.created);
}

constexpr std::string_view kAnnotationSelect =
    "SELECT a.id,a.revision,a.source_snapshot_id,s.message_id,a.node_id,a.start_char,a.end_char,"
    "a.origin,a.state,a.metadata,a.created,s.text_hash,s.source_text,m.id,m.text,"
    "EXISTS(SELECT 1 FROM nodes n WHERE n.id=a.node_id),s.record_hash,s.message_json "
    "FROM loom_message_annotations a JOIN loom_message_annotation_sources s ON s.id=a.source_snapshot_id "
    "LEFT JOIN messages m ON m.id=s.message_id ";

Result<Json> validate_snapshot(std::string_view payload, std::string_view text,
    std::string_view text_hash, std::string_view record_hash, std::string_view message_id) {
  if (Sha256::hex(payload) != record_hash || Sha256::hex(text) != text_hash)
    return Error(Errc::Database, "annotation source snapshot hash is corrupt");
  auto parsed = json::parse(payload);
  if (!parsed) return Error(Errc::Database, "annotation source snapshot JSON is corrupt");
  Json message = std::move(*parsed);
  if (!message.is_object() || !message.contains("text") || !message["text"].is_string() ||
      message["text"].get_ref<const std::string&>() != text || !message.contains("id") || message["id"] != message_id)
    return Error(Errc::Database, "annotation source snapshot does not match its binding");
  return message;
}

// Within one SELECT statement the source rows are a consistent SQLite snapshot.
// Validate each full payload once per page/history batch, rather than once per
// annotation span of the same potentially large message.
Result<MessageAnnotation> read_annotation(const sql::Stmt& stmt,
    std::unordered_set<std::string>* verified_sources = nullptr) {
  MessageAnnotation annotation;
  annotation.id = stmt.get_text(0);
  annotation.revision = stmt.get_int(1);
  annotation.source_snapshot_id = stmt.get_text(2);
  annotation.message_id = stmt.get_text(3);
  annotation.node_id = stmt.get_text(4);
  annotation.start_char = stmt.get_int(5);
  annotation.end_char = stmt.get_int(6);
  annotation.origin = stmt.get_text(7);
  annotation.state = stmt.get_text(8);
  LOOM_TRY_ASSIGN(annotation.metadata, json::parse(stmt.get_text(9)));
  annotation.created = stmt.get_text(10);
  annotation.text_hash = stmt.get_text(11);
  const std::string text = stmt.get_text(12);
  if (!verified_sources || !verified_sources->contains(annotation.source_snapshot_id)) {
    LOOM_TRY(validate_snapshot(stmt.get_text(17), text, annotation.text_hash, stmt.get_text(16), annotation.message_id));
    if (verified_sources) verified_sources->insert(annotation.source_snapshot_id);
  }
  if (!check_range(text, annotation.start_char, annotation.end_char))
    return Error(Errc::Database, "annotation source range is corrupt");
  annotation.start_byte = static_cast<std::int64_t>(utf8::byte_offset(text, annotation.start_char));
  annotation.end_byte = static_cast<std::int64_t>(utf8::byte_offset(text, annotation.end_char));
  annotation.excerpt = text.substr(static_cast<std::size_t>(annotation.start_byte),
      static_cast<std::size_t>(annotation.end_byte - annotation.start_byte));
  annotation.message_exists = !stmt.is_null(13);
  annotation.source_matches_current = annotation.message_exists && stmt.get_text(14) == text;
  annotation.node_exists = stmt.get_int(15) != 0;
  return annotation;
}

}  // namespace

Json MessageAnnotationSource::to_json() const {
  return Json{{"id", id}, {"message_id", message_id}, {"text_hash", text_hash},
      {"record_hash", record_hash}, {"message", message}, {"created", created}};
}

Json MessageAnnotation::to_json() const {
  return Json{{"id", id}, {"revision", revision}, {"source_snapshot_id", source_snapshot_id},
      {"message_id", message_id}, {"node_id", node_id}, {"range_unit", "unicode_codepoint"},
      {"start_char", start_char}, {"end_char", end_char}, {"start_byte", start_byte}, {"end_byte", end_byte},
      {"origin", origin}, {"state", state}, {"metadata", metadata}, {"created", created},
      {"text_hash", text_hash}, {"excerpt", excerpt}, {"message_exists", message_exists},
      {"source_matches_current", source_matches_current}, {"node_exists", node_exists}};
}

Status Database::migrate_message_extensions() {
  // Independent additive schema versions keep core v4 and existing Loom v1
  // stable. Never overwrite a newer version written by another implementation.
  for (const auto& entry : {std::pair{"loom_message_annotation_schema_version", kMessageAnnotationSchemaVersion},
                            std::pair{"loom_import_checkpoint_schema_version", kImportCheckpointSchemaVersion}}) {
    LOOM_TRY_ASSIGN(auto version, get_meta(entry.first));
    if (!version) continue;
    int parsed = 0;
    auto [end, error] = std::from_chars(version->data(), version->data() + version->size(), parsed);
    if (error != std::errc{} || end != version->data() + version->size() || parsed < 0)
      return Error(Errc::Database, "invalid message extension schema version");
    if (parsed > entry.second)
      return Error(Errc::Unsupported, "message extension schema is newer than this implementation");
  }
  sql::Txn txn(conn_);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(conn_.exec(R"SQL(
CREATE TABLE IF NOT EXISTS loom_message_annotation_sources (
    id TEXT PRIMARY KEY,
    message_id TEXT NOT NULL,
    text_hash TEXT NOT NULL,
    record_hash TEXT NOT NULL,
    source_text TEXT NOT NULL,
    message_json TEXT NOT NULL,
    created TEXT NOT NULL,
    UNIQUE(message_id,record_hash)
);
CREATE INDEX IF NOT EXISTS idx_loom_annotation_sources_message
    ON loom_message_annotation_sources(message_id);
CREATE TABLE IF NOT EXISTS loom_message_annotations (
    id TEXT NOT NULL,
    revision INTEGER NOT NULL CHECK(revision > 0),
    source_snapshot_id TEXT NOT NULL REFERENCES loom_message_annotation_sources(id),
    node_id TEXT NOT NULL,
    start_char INTEGER NOT NULL CHECK(start_char >= 0),
    end_char INTEGER NOT NULL CHECK(end_char >= start_char),
    origin TEXT NOT NULL CHECK(origin IN ('recorded','model','user')),
    state TEXT NOT NULL CHECK(state IN ('active','retracted')),
    metadata TEXT NOT NULL DEFAULT '{}',
    created TEXT NOT NULL,
    PRIMARY KEY(id,revision)
);
CREATE INDEX IF NOT EXISTS idx_loom_annotations_source ON loom_message_annotations(source_snapshot_id);
CREATE INDEX IF NOT EXISTS idx_loom_annotations_node ON loom_message_annotations(node_id);
CREATE TABLE IF NOT EXISTS loom_import_checkpoints (
    source_id TEXT NOT NULL REFERENCES loom_sources(id),
    member TEXT NOT NULL,
    archive_index INTEGER NOT NULL DEFAULT -1,
    source_index INTEGER NOT NULL,
    conversation_id TEXT NOT NULL,
    metadata TEXT NOT NULL DEFAULT '{}',
    PRIMARY KEY(source_id,member,archive_index,source_index)
);
)SQL"));
  LOOM_TRY(set_meta("loom_message_annotation_schema_version", std::to_string(kMessageAnnotationSchemaVersion)));
  LOOM_TRY(set_meta("loom_import_checkpoint_schema_version", std::to_string(kImportCheckpointSchemaVersion)));
  return txn.commit();
}

Result<MessageAnnotation> Database::create_message_annotation(const NewMessageAnnotation& input) {
  LOOM_TRY(check_origin(input.origin));
  if (input.message_id.empty() || input.node_id.empty())
    return Error(Errc::InvalidArgument, "annotation message_id and node_id are required");
  auto lk = lock();
  sql::Txn txn(conn_);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto message, get_msg(input.message_id));
  if (!message) return Error(Errc::NotFound, "annotation message does not exist");
  LOOM_TRY_ASSIGN(auto node, get_node(input.node_id));
  if (!node) return Error(Errc::NotFound, "annotation node does not exist");
  LOOM_TRY(check_range(message->text, input.start_char, input.end_char));
  const std::string text_hash = Sha256::hex(message->text);
  if (input.expected_text_hash && *input.expected_text_hash != text_hash)
    return Error(Errc::Conflict, "annotation source text changed before creation");
  const std::string payload = json::py_dumps(message->to_json());
  const std::string record_hash = Sha256::hex(payload);
  LOOM_TRY_ASSIGN(auto snapshot_id, conn_.query_text(
      "SELECT id FROM loom_message_annotation_sources WHERE message_id=? AND record_hash=?",
      message->id, record_hash));
  const std::string now = timeutil::utc_now_iso();
  if (!snapshot_id) {
    snapshot_id = gen_id("ms_");
    LOOM_TRY(conn_.run("INSERT INTO loom_message_annotation_sources "
        "(id,message_id,text_hash,record_hash,source_text,message_json,created) VALUES (?,?,?,?,?,?,?)",
        *snapshot_id, message->id, text_hash, record_hash, message->text, payload, now));
  }
  MessageAnnotation annotation;
  annotation.id = gen_id("ann_");
  annotation.source_snapshot_id = *snapshot_id;
  annotation.node_id = input.node_id;
  annotation.start_char = input.start_char;
  annotation.end_char = input.end_char;
  annotation.origin = input.origin;
  annotation.metadata = input.metadata;
  annotation.created = now;
  LOOM_TRY(insert_annotation(conn_, annotation));
  LOOM_TRY_ASSIGN(auto result, get_message_annotation(annotation.id));
  if (!result) return Error(Errc::Internal, "created annotation not found");
  LOOM_TRY(txn.commit());
  return std::move(*result);
}

Result<std::optional<MessageAnnotation>> Database::get_message_annotation(std::string_view id) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto stmt, conn_.prepare(std::string(kAnnotationSelect) +
      "WHERE a.id=? ORDER BY a.revision DESC LIMIT 1"));
  stmt.bind(1, id);
  LOOM_TRY_ASSIGN(bool row, stmt.step());
  if (!row) return std::optional<MessageAnnotation>{};
  LOOM_TRY_ASSIGN(auto annotation, read_annotation(stmt));
  return std::optional<MessageAnnotation>(std::move(annotation));
}

Result<std::vector<MessageAnnotation>> Database::list_message_annotations(
    std::string_view message_id, const MessageAnnotationListOptions& opts) {
  LOOM_TRY(check_limit(opts.limit));
  auto lk = lock();
  std::string query = std::string(kAnnotationSelect) +
      "WHERE s.message_id=? AND a.revision=(SELECT MAX(h.revision) FROM loom_message_annotations h WHERE h.id=a.id)";
  if (!opts.include_retracted) query += " AND a.state='active'";
  if (opts.after_id) query += " AND a.id>?";
  if (opts.node_id) query += " AND a.node_id=?";
  query += " ORDER BY a.id LIMIT ?";
  LOOM_TRY_ASSIGN(auto stmt, conn_.prepare(query));
  int index = 1;
  stmt.bind(index++, message_id);
  if (opts.after_id) stmt.bind(index++, *opts.after_id);
  if (opts.node_id) stmt.bind(index++, *opts.node_id);
  stmt.bind(index, opts.limit.value_or(-1));
  std::vector<MessageAnnotation> annotations;
  std::unordered_set<std::string> verified_sources;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, stmt.step());
    if (!row) break;
    LOOM_TRY_ASSIGN(auto annotation, read_annotation(stmt, &verified_sources));
    annotations.push_back(std::move(annotation));
  }
  return annotations;
}

Result<MessageAnnotation> Database::revise_message_annotation(std::string_view id,
    std::int64_t expected_revision, const MessageAnnotationPatch& patch) {
  if (!patch.node_id && !patch.start_char && !patch.end_char && !patch.origin && !patch.state && !patch.metadata)
    return Error(Errc::InvalidArgument, "annotation revision patch is empty");
  auto lk = lock();
  sql::Txn txn(conn_);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto current, get_message_annotation(id));
  if (!current) return Error(Errc::NotFound, "annotation does not exist");
  if (current->revision != expected_revision)
    return Error(Errc::Conflict, "annotation revision changed before correction");
  if (current->revision == std::numeric_limits<std::int64_t>::max())
    return Error(Errc::InvalidArgument, "annotation revision cannot be represented");
  MessageAnnotation annotation = *current;
  if (patch.node_id) {
    LOOM_TRY_ASSIGN(auto node, get_node(*patch.node_id));
    if (!node) return Error(Errc::NotFound, "annotation replacement node does not exist");
    annotation.node_id = *patch.node_id;
  }
  if (patch.start_char) annotation.start_char = *patch.start_char;
  if (patch.end_char) annotation.end_char = *patch.end_char;
  if (patch.origin) annotation.origin = *patch.origin;
  if (patch.state) annotation.state = *patch.state;
  if (patch.metadata) annotation.metadata = *patch.metadata;
  LOOM_TRY(check_origin(annotation.origin));
  LOOM_TRY(check_state(annotation.state));
  LOOM_TRY_ASSIGN(auto source, get_message_annotation_source(annotation.source_snapshot_id));
  if (!source) return Error(Errc::Database, "annotation immutable source is missing");
  LOOM_TRY(check_range(source->message.at("text").get_ref<const std::string&>(),
      annotation.start_char, annotation.end_char));
  ++annotation.revision;
  annotation.created = timeutil::utc_now_iso();
  LOOM_TRY(insert_annotation(conn_, annotation));
  LOOM_TRY_ASSIGN(auto result, get_message_annotation(id));
  if (!result) return Error(Errc::Internal, "revised annotation not found");
  LOOM_TRY(txn.commit());
  return std::move(*result);
}

Result<std::vector<MessageAnnotation>> Database::get_message_annotation_history(std::string_view id,
    std::int64_t after_revision, std::optional<std::int64_t> limit) {
  LOOM_TRY(check_limit(limit));
  if (after_revision < 0) return Error(Errc::InvalidArgument, "annotation revision cursor must be nonnegative");
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto stmt, conn_.prepare(std::string(kAnnotationSelect) +
      "WHERE a.id=? AND a.revision>? ORDER BY a.revision LIMIT ?"));
  stmt.bind_all(id, after_revision, limit.value_or(-1));
  std::vector<MessageAnnotation> annotations;
  std::unordered_set<std::string> verified_sources;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, stmt.step());
    if (!row) break;
    LOOM_TRY_ASSIGN(auto annotation, read_annotation(stmt, &verified_sources));
    annotations.push_back(std::move(annotation));
  }
  return annotations;
}

Result<std::optional<MessageAnnotationSource>> Database::get_message_annotation_source(std::string_view id) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto stmt, conn_.prepare("SELECT id,message_id,text_hash,record_hash,message_json,created,source_text "
      "FROM loom_message_annotation_sources WHERE id=?"));
  stmt.bind(1, id);
  LOOM_TRY_ASSIGN(bool row, stmt.step());
  if (!row) return std::optional<MessageAnnotationSource>{};
  MessageAnnotationSource source;
  source.id = stmt.get_text(0);
  source.message_id = stmt.get_text(1);
  source.text_hash = stmt.get_text(2);
  source.record_hash = stmt.get_text(3);
  const std::string payload = stmt.get_text(4);
  LOOM_TRY_ASSIGN(source.message, validate_snapshot(payload, stmt.get_text(6), source.text_hash,
      source.record_hash, source.message_id));
  source.created = stmt.get_text(5);
  return std::optional<MessageAnnotationSource>(std::move(source));
}

}  // namespace loom
