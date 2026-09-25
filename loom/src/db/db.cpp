// Port of engine/db.py. Keep method order and SQL in sync with the Python
// file; every deviation is commented.
#include "loom/db.h"

#include <unordered_map>

#include "db_internal.h"
#include "loom/log.h"
#include "loom/util/fs.h"
#include "loom/util/ids.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom {

namespace {
constexpr std::string_view kLog = "loom.db";

// Byte-identical copy of the Python _init_schema() executescript body, so that
// sqlite_master.sql is identical for Python- and Loom-created databases.
#include "schema_sql.inc"

std::string now_iso() { return timeutil::utc_now_iso(); }

// json.dumps(x or default) - Python falsy values collapse to the default.
std::string dumps_or(const Json& v, std::string_view fallback) {
  if (!json::truthy(v)) return std::string(fallback);
  return json::py_dumps(v);
}

// json.loads(text or default); invalid JSON is preserved as a JSON string
// (Python would raise; we keep the data visible instead of failing a read).
Json loads_or(const std::optional<std::string>& text, Json fallback) {
  if (!text || text->empty()) return fallback;
  auto r = json::parse(*text);
  if (!r) {
    log::debug(kLog, "invalid JSON in column: {:.60}", *text);
    return Json(*text);
  }
  return std::move(r).value();
}

}  // namespace

namespace dbi {

RowMap::RowMap(const sql::Stmt& st) {
  for (int i = 0; i < st.column_count(); ++i) {
    std::string n = st.column_name(i);
    if (idx_.find(n) == idx_.end()) idx_.emplace(std::move(n), i);
  }
}

int RowMap::col(std::string_view name) const {
  auto it = idx_.find(std::string(name));
  return it == idx_.end() ? -1 : it->second;
}

std::string RowMap::text(const sql::Stmt& st, std::string_view name, std::string_view fallback) const {
  int c = col(name);
  if (c < 0 || st.is_null(c)) return std::string(fallback);
  return st.get_text(c);
}

std::optional<std::string> RowMap::opt_text(const sql::Stmt& st, std::string_view name) const {
  int c = col(name);
  if (c < 0 || st.is_null(c)) return std::nullopt;
  return st.get_text(c);
}

double RowMap::real(const sql::Stmt& st, std::string_view name, double fallback) const {
  int c = col(name);
  if (c < 0 || st.is_null(c)) return fallback;
  return st.get_double(c);
}

std::int64_t RowMap::integer(const sql::Stmt& st, std::string_view name, std::int64_t fallback) const {
  int c = col(name);
  if (c < 0 || st.is_null(c)) return fallback;
  return st.get_int(c);
}

Message read_message(const sql::Stmt& st, const RowMap& m) {
  Message msg;
  msg.id = m.text(st, "id");
  msg.conv_id = m.text(st, "conv_id");
  msg.parent_id = m.opt_text(st, "parent_id");
  msg.role = m.text(st, "role");
  msg.text = m.text(st, "text");
  msg.model = m.opt_text(st, "model");
  msg.status = m.text(st, "status", "active");
  msg.version_group_id = m.opt_text(st, "version_group_id");
  msg.version_num = m.integer(st, "version_num", 1);
  msg.weight = m.real(st, "weight", 1.0);
  msg.attachments = loads_or(m.opt_text(st, "attachments"), Json::array());
  msg.metadata = loads_or(m.opt_text(st, "metadata"), Json::object());
  msg.created = m.text(st, "created");
  msg.semantic_status = m.text(st, "semantic_status", "pending");
  return msg;
}

Conversation read_conv(const sql::Stmt& st, const RowMap& m) {
  Conversation c;
  c.id = m.text(st, "id");
  c.title = m.text(st, "title");
  c.created = m.text(st, "created");
  c.updated = m.text(st, "updated");
  c.source = m.text(st, "source", "user");
  c.metadata = loads_or(m.opt_text(st, "metadata"), Json::object());
  return c;
}

Node read_node(const sql::Stmt& st, const RowMap& m) {
  Node n;
  n.id = m.text(st, "id");
  n.kind = m.text(st, "kind", "entity");
  n.label = m.text(st, "label");
  n.content = m.text(st, "content");
  n.tags = loads_or(m.opt_text(st, "tags"), Json::array());
  n.metadata = loads_or(m.opt_text(st, "metadata"), Json::object());
  n.created = m.text(st, "created");
  return n;
}

Link read_link(const sql::Stmt& st, const RowMap& m) {
  Link l;
  l.id = m.text(st, "id");
  l.src = m.text(st, "src");
  l.dst = m.text(st, "dst");
  l.link_type = m.text(st, "link_type", "related");
  l.weight = m.real(st, "weight", 1.0);
  l.metadata = loads_or(m.opt_text(st, "metadata"), Json::object());
  l.created = m.text(st, "created");
  return l;
}

}  // namespace dbi

// ── msg_status ─────────────────────────────────────────────────────
bool msg_status::is_valid(std::string_view s) noexcept {
  return s == kActive || s == kExcluded || s == kVersion || s == kDeleted;
}

// ── to_json ────────────────────────────────────────────────────────
namespace {
Json opt_json(const std::optional<std::string>& v) { return v ? Json(*v) : Json(nullptr); }
}  // namespace

Json Conversation::to_json() const {
  return Json{{"id", id}, {"title", title}, {"created", created}, {"updated", updated},
              {"source", source}, {"metadata", metadata}};
}

Json Message::to_json() const {
  return Json{{"id", id},
              {"conv_id", conv_id},
              {"parent_id", opt_json(parent_id)},
              {"role", role},
              {"text", text},
              {"model", opt_json(model)},
              {"status", status},
              {"version_group_id", opt_json(version_group_id)},
              {"version_num", version_num},
              {"weight", weight},
              {"attachments", attachments},
              {"metadata", metadata},
              {"created", created},
              {"semantic_status", semantic_status}};
}

Json PendingMessage::to_json() const {
  return Json{{"id", id}, {"conv_id", conv_id}, {"role", role}, {"text", text}, {"metadata", metadata}};
}

Json Node::to_json() const {
  return Json{{"id", id},     {"kind", kind},         {"label", label},    {"content", content},
              {"tags", tags}, {"metadata", metadata}, {"created", created}};
}

Json Link::to_json() const {
  return Json{{"id", id},          {"src", src},           {"dst", dst},        {"link_type", link_type},
              {"weight", weight},  {"metadata", metadata}, {"created", created}};
}

Json SearchHit::to_json() const {
  Json j = message.to_json();
  j["score"] = score;
  j["snippet"] = snippet;
  return j;
}

Json SearchResult::to_json() const {
  Json arr = Json::array();
  for (const auto& h : hits) arr.push_back(h.to_json());
  return Json{{"mode", mode}, {"results", std::move(arr)}};
}

Json FtsStatus::to_json() const {
  return Json{{"available", available}, {"path", path},   {"indexed", indexed},
              {"messages", messages},   {"dirty", dirty}, {"rebuilds", rebuilds}};
}

BatchMessage BatchMessage::from_json(const Json& j) {
  BatchMessage b;
  if (const Json* r = json::find(j, "role"); r && r->is_string()) b.role = r->get<std::string>();
  const Json* t = json::find(j, "text");
  if (!t) t = json::find(j, "content");
  if (t && t->is_string()) b.text = t->get<std::string>();
  b.model = json::get_opt_string(j, "model");
  b.parent_id = json::get_opt_string(j, "parent_id");
  if (const Json* w = json::find(j, "weight"); w && w->is_number()) b.weight = w->get<double>();
  if (const Json* a = json::find(j, "attachments")) b.attachments = *a;
  if (const Json* m = json::find(j, "metadata")) b.metadata = *m;
  return b;
}

// ── Patch parsing (C API / JSON callers) ───────────────────────────
namespace {
std::optional<std::string> str_or_null(const Json& v) {
  if (v.is_string()) return v.get<std::string>();
  return std::nullopt;
}
}  // namespace

Result<ConvPatch> ConvPatch::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "patch must be an object");
  ConvPatch p;
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& k = it.key();
    const Json& v = it.value();
    if (k == "title" && v.is_string()) p.title = v.get<std::string>();
    else if (k == "source" && v.is_string()) p.source = v.get<std::string>();
    else if (k == "metadata") p.metadata = v;
    else if (k == "created" && v.is_string()) p.created = v.get<std::string>();
    else if (k == "updated" || k == "id") continue;
    else return Error(Errc::InvalidArgument, "unknown or invalid conversation field: " + k);
  }
  return p;
}

bool MsgPatch::empty() const noexcept {
  return !conv_id && !parent_id && !role && !text && !model && !status && !version_group_id && !version_num &&
         !weight && !attachments && !metadata && !created && !semantic_status;
}

Result<MsgPatch> MsgPatch::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "patch must be an object");
  MsgPatch p;
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& k = it.key();
    const Json& v = it.value();
    if (k == "conv_id" && v.is_string()) p.conv_id = v.get<std::string>();
    else if (k == "parent_id") p.parent_id = str_or_null(v);
    else if (k == "role" && v.is_string()) p.role = v.get<std::string>();
    else if (k == "text" && v.is_string()) p.text = v.get<std::string>();
    else if (k == "model") p.model = str_or_null(v);
    else if (k == "status" && v.is_string()) p.status = v.get<std::string>();
    else if (k == "version_group_id") p.version_group_id = str_or_null(v);
    else if (k == "version_num" && v.is_number_integer()) p.version_num = v.get<std::int64_t>();
    else if (k == "weight" && v.is_number()) p.weight = v.get<double>();
    else if (k == "attachments") p.attachments = v;
    else if (k == "metadata") p.metadata = v;
    else if (k == "created" && v.is_string()) p.created = v.get<std::string>();
    else if (k == "semantic_status" && v.is_string()) p.semantic_status = v.get<std::string>();
    else return Error(Errc::InvalidArgument, "unknown or invalid message field: " + k);
  }
  return p;
}

bool NodePatch::empty() const noexcept { return !kind && !label && !content && !tags && !metadata && !created; }

Result<NodePatch> NodePatch::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "patch must be an object");
  NodePatch p;
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& k = it.key();
    const Json& v = it.value();
    if (k == "kind" && v.is_string()) p.kind = v.get<std::string>();
    else if (k == "label" && v.is_string()) p.label = v.get<std::string>();
    else if (k == "content" && v.is_string()) p.content = v.get<std::string>();
    else if (k == "tags") p.tags = v;
    else if (k == "metadata") p.metadata = v;
    else if (k == "created" && v.is_string()) p.created = v.get<std::string>();
    else return Error(Errc::InvalidArgument, "unknown or invalid node field: " + k);
  }
  return p;
}

// ── Open / schema ──────────────────────────────────────────────────
Result<std::unique_ptr<Database>> Database::open(const std::filesystem::path& path, const DbOptions& opts) {
  if (path.has_parent_path()) LOOM_TRY(fsutil::ensure_dir(path.parent_path()));
  auto db = std::make_unique<Database>(PrivateTag{});
  db->path_ = path;
  db->opts_ = opts;
  sql::OpenOptions oo;
  oo.busy_timeout_ms = opts.busy_timeout_ms;
  LOOM_TRY_ASSIGN(db->conn_, sql::Connection::open(path, oo));
  {
    auto lk = db->lock();
    // WAL can fail on read-only media; Python ignores the result as well.
    LOOM_TRY(db->conn_.exec("PRAGMA journal_mode=WAL"));
    LOOM_TRY(db->conn_.exec("PRAGMA synchronous=NORMAL"));
    LOOM_TRY(db->conn_.exec("PRAGMA foreign_keys=ON"));
    LOOM_TRY(db->init_schema());
    LOOM_TRY(db->migrate());
    LOOM_TRY(db->migrate_loom());
    LOOM_TRY(db->fts_open());
  }
  log::info(kLog, "Database opened: {}", path.string());
  return db;
}

Database::~Database() { close(); }

void Database::close() {
  auto lk = lock();
  if (conn_.is_open()) {
    conn_.close();
    log::info(kLog, "Database closed");
  }
}

Status Database::init_schema() {
  // Python: executescript(...) then commit().
  return conn_.exec(kInitSchemaSql);
}

Status Database::migrate() {
  auto& c = conn_;
  auto ensure_column = [&](std::string_view table, std::string_view col, std::string_view ddl) -> Status {
    if (!c.has_column(table, col)) {
      log::warn(kLog, "DB migrate: adding missing column {}.{}", table, col);
      return c.exec(ddl);
    }
    return {};
  };
  auto ensure_index = [&](std::string_view sql, std::initializer_list<std::pair<std::string_view, std::string_view>> req) {
    for (const auto& [t, col] : req) {
      if (!c.has_column(t, col)) {
        log::warn(kLog, "DB migrate: skipping index (missing column {}.{})", t, col);
        return;
      }
    }
    if (auto st = c.exec(sql); !st) log::warn(kLog, "DB migrate: index create failed ({}): {}", sql, st.error().message);
  };

  int cur_ver = 0;
  {
    auto v = get_meta("schema_version");
    if (v && v->has_value()) cur_ver = std::atoi((*v)->c_str());
  }

  // Ensure base tables exist (for very old DBs) - Python's compact DDL.
  if (!c.has_table("_meta")) LOOM_TRY(c.exec("CREATE TABLE IF NOT EXISTS _meta (key TEXT PRIMARY KEY, value TEXT)"));
  if (!c.has_table("conversations")) {
    LOOM_TRY(c.exec(
        "CREATE TABLE IF NOT EXISTS conversations ("
        "id TEXT PRIMARY KEY, title TEXT NOT NULL, created TEXT NOT NULL, "
        "updated TEXT NOT NULL, source TEXT NOT NULL DEFAULT 'user', "
        "metadata TEXT NOT NULL DEFAULT '{}'"
        ")"));
  }
  if (!c.has_table("messages")) {
    LOOM_TRY(c.exec(
        "CREATE TABLE IF NOT EXISTS messages ("
        "id TEXT PRIMARY KEY, conv_id TEXT NOT NULL, parent_id TEXT, "
        "role TEXT NOT NULL, text TEXT NOT NULL, model TEXT, "
        "status TEXT NOT NULL DEFAULT 'active', "
        "version_group_id TEXT, version_num INTEGER NOT NULL DEFAULT 1, "
        "weight REAL NOT NULL DEFAULT 1.0, attachments TEXT NOT NULL DEFAULT '[]', "
        "metadata TEXT NOT NULL DEFAULT '{}', created TEXT NOT NULL, "
        "semantic_status TEXT NOT NULL DEFAULT 'pending', "
        "FOREIGN KEY (conv_id) REFERENCES conversations(id) ON DELETE CASCADE"
        ")"));
  }
  if (!c.has_table("nodes")) {
    LOOM_TRY(c.exec(
        "CREATE TABLE IF NOT EXISTS nodes ("
        "id TEXT PRIMARY KEY, kind TEXT NOT NULL DEFAULT 'entity', "
        "label TEXT NOT NULL, content TEXT NOT NULL DEFAULT '', "
        "tags TEXT NOT NULL DEFAULT '[]', metadata TEXT NOT NULL DEFAULT '{}', "
        "created TEXT NOT NULL"
        ")"));
  }
  if (!c.has_table("links")) {
    LOOM_TRY(c.exec(
        "CREATE TABLE IF NOT EXISTS links ("
        "id TEXT PRIMARY KEY, src TEXT NOT NULL, dst TEXT NOT NULL, "
        "link_type TEXT NOT NULL DEFAULT 'related', "
        "weight REAL NOT NULL DEFAULT 1.0, metadata TEXT NOT NULL DEFAULT '{}', "
        "created TEXT NOT NULL"
        ")"));
  }

  LOOM_TRY(ensure_column("conversations", "source",
                         "ALTER TABLE conversations ADD COLUMN source TEXT NOT NULL DEFAULT 'user'"));
  LOOM_TRY(ensure_column("messages", "status", "ALTER TABLE messages ADD COLUMN status TEXT NOT NULL DEFAULT 'active'"));
  LOOM_TRY(ensure_column("messages", "version_group_id", "ALTER TABLE messages ADD COLUMN version_group_id TEXT"));
  LOOM_TRY(ensure_column("messages", "semantic_status",
                         "ALTER TABLE messages ADD COLUMN semantic_status TEXT NOT NULL DEFAULT 'pending'"));
  LOOM_TRY(ensure_column("links", "link_type", "ALTER TABLE links ADD COLUMN link_type TEXT NOT NULL DEFAULT 'related'"));

  ensure_index("CREATE INDEX IF NOT EXISTS idx_msg_conv ON messages(conv_id)", {{"messages", "conv_id"}});
  ensure_index("CREATE INDEX IF NOT EXISTS idx_msg_parent ON messages(parent_id)", {{"messages", "parent_id"}});
  ensure_index("CREATE INDEX IF NOT EXISTS idx_msg_vgroup ON messages(version_group_id)",
               {{"messages", "version_group_id"}});
  ensure_index("CREATE INDEX IF NOT EXISTS idx_msg_status ON messages(conv_id, status)",
               {{"messages", "conv_id"}, {"messages", "status"}});
  ensure_index("CREATE INDEX IF NOT EXISTS idx_msg_semantic ON messages(semantic_status)",
               {{"messages", "semantic_status"}});
  ensure_index("CREATE INDEX IF NOT EXISTS idx_nodes_kind ON nodes(kind)", {{"nodes", "kind"}});
  ensure_index("CREATE INDEX IF NOT EXISTS idx_links_src ON links(src)", {{"links", "src"}});
  ensure_index("CREATE INDEX IF NOT EXISTS idx_links_dst ON links(dst)", {{"links", "dst"}});
  ensure_index("CREATE INDEX IF NOT EXISTS idx_links_type ON links(link_type)", {{"links", "link_type"}});

  LOOM_TRY(set_meta("schema_version", std::to_string(kSchemaVersion)));
  if (cur_ver < kSchemaVersion) log::info(kLog, "Schema upgraded: v{} -> v{}", cur_ver, kSchemaVersion);
  return {};
}

Status Database::migrate_loom() {
  // Loom-only tables. Idempotent, forward-only, never touch core tables.
  static constexpr std::string_view kLoomDdl = R"SQL(
CREATE TABLE IF NOT EXISTS loom_blobs (
    hash     TEXT PRIMARY KEY,
    size     INTEGER NOT NULL,
    mime     TEXT,
    created  TEXT NOT NULL,
    metadata TEXT NOT NULL DEFAULT '{}'
);
CREATE TABLE IF NOT EXISTS loom_sources (
    id             TEXT PRIMARY KEY,
    kind           TEXT NOT NULL,
    uri            TEXT,
    blob_hash      TEXT,
    size           INTEGER,
    mime           TEXT,
    format         TEXT,
    title          TEXT,
    parser         TEXT,
    parser_version TEXT,
    imported_at    TEXT NOT NULL,
    metadata       TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX IF NOT EXISTS idx_loom_sources_hash ON loom_sources(blob_hash);
CREATE TABLE IF NOT EXISTS loom_provenance (
    id           TEXT PRIMARY KEY,
    subject_id   TEXT NOT NULL,
    subject_kind TEXT NOT NULL,
    source_id    TEXT,
    locator      TEXT NOT NULL DEFAULT '{}',
    transform    TEXT NOT NULL DEFAULT '',
    confidence   REAL NOT NULL DEFAULT 1.0,
    created      TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_loom_prov_subject ON loom_provenance(subject_id);
CREATE INDEX IF NOT EXISTS idx_loom_prov_source ON loom_provenance(source_id);
CREATE TABLE IF NOT EXISTS loom_events (
    seq         INTEGER PRIMARY KEY AUTOINCREMENT,
    id          TEXT NOT NULL UNIQUE,
    ts          TEXT NOT NULL,
    type        TEXT NOT NULL,
    subject_id  TEXT,
    payload     TEXT NOT NULL DEFAULT '{}',
    input_hash  TEXT,
    output_hash TEXT
);
CREATE INDEX IF NOT EXISTS idx_loom_events_type ON loom_events(type);
CREATE INDEX IF NOT EXISTS idx_loom_events_subject ON loom_events(subject_id);
CREATE TABLE IF NOT EXISTS loom_tasks (
    id           TEXT PRIMARY KEY,
    kind         TEXT NOT NULL,
    status       TEXT NOT NULL DEFAULT 'pending',
    params       TEXT NOT NULL DEFAULT '{}',
    input_hash   TEXT,
    output_hash  TEXT,
    checkpoint   TEXT,
    attempts     INTEGER NOT NULL DEFAULT 0,
    max_attempts INTEGER NOT NULL DEFAULT 3,
    error        TEXT,
    parent_id    TEXT,
    result       TEXT,
    created      TEXT NOT NULL,
    updated      TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_loom_tasks_status ON loom_tasks(status);
CREATE INDEX IF NOT EXISTS idx_loom_tasks_kind ON loom_tasks(kind, input_hash);
CREATE INDEX IF NOT EXISTS idx_loom_tasks_parent ON loom_tasks(parent_id);
CREATE TABLE IF NOT EXISTS loom_artifacts (
    id        TEXT PRIMARY KEY,
    kind      TEXT NOT NULL,
    title     TEXT NOT NULL DEFAULT '',
    blob_hash TEXT,
    mime      TEXT,
    task_id   TEXT,
    metadata  TEXT NOT NULL DEFAULT '{}',
    created   TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_loom_artifacts_task ON loom_artifacts(task_id);
CREATE TABLE IF NOT EXISTS loom_relation_types (
    name        TEXT PRIMARY KEY,
    inverse     TEXT,
    symmetric   INTEGER NOT NULL DEFAULT 0,
    transitive  INTEGER NOT NULL DEFAULT 0,
    category    TEXT NOT NULL DEFAULT 'custom',
    description TEXT NOT NULL DEFAULT '',
    metadata    TEXT NOT NULL DEFAULT '{}'
);
)SQL";
  sql::Txn txn(conn_);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(conn_.exec(kLoomDdl));
  // Future loom_ columns go here, guarded like the core migration:
  //   if (!conn_.has_column("loom_tasks", "x")) ALTER TABLE ...
  LOOM_TRY(set_meta("loom_schema_version", std::to_string(kLoomSchemaVersion)));
  return txn.commit();
}

// ── Meta ───────────────────────────────────────────────────────────
Result<std::optional<std::string>> Database::get_meta(std::string_view key) {
  auto lk = lock();
  return conn_.query_text("SELECT value FROM _meta WHERE key = ?", key);
}

Status Database::set_meta(std::string_view key, std::string_view value) {
  auto lk = lock();
  return conn_.run("INSERT OR REPLACE INTO _meta (key, value) VALUES (?, ?)", key, value);
}

Result<int> Database::schema_version() {
  LOOM_TRY_ASSIGN(auto v, get_meta("schema_version"));
  return v ? std::atoi(v->c_str()) : 0;
}

// ── Conversations ──────────────────────────────────────────────────
Result<Conversation> Database::create_conv(std::string_view title) {
  Conversation c;
  c.id = gen_id(id_prefix::kConversation);
  c.title = std::string(title);
  c.created = c.updated = now_iso();
  auto lk = lock();
  LOOM_TRY(conn_.run("INSERT INTO conversations (id, title, created, updated) VALUES (?, ?, ?, ?)", c.id, c.title,
                     c.created, c.updated));
  return c;
}

Result<std::vector<Conversation>> Database::list_convs(int limit) {
  auto lk = lock();
  // Deviation: rowid tie-break for deterministic order on equal timestamps.
  LOOM_TRY_ASSIGN(auto st, conn_.prepare("SELECT * FROM conversations ORDER BY updated DESC, rowid DESC LIMIT ?"));
  st.bind(1, limit);
  dbi::RowMap m(st);
  std::vector<Conversation> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    out.push_back(dbi::read_conv(st, m));
  }
  return out;
}

Result<std::optional<Conversation>> Database::get_conv(std::string_view cid) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto st, conn_.prepare("SELECT * FROM conversations WHERE id = ?"));
  st.bind(1, cid);
  dbi::RowMap m(st);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return std::optional<Conversation>{};
  return std::optional<Conversation>(dbi::read_conv(st, m));
}

Status Database::update_conv_locked(std::string_view cid, const ConvPatch& p) {
  // Python builds "k = ?" for each kwarg in order, then `updated`.
  std::string sets;
  std::vector<std::string> vals;
  auto add = [&](const char* col, std::string v) {
    if (!sets.empty()) sets += ", ";
    sets += col;
    sets += " = ?";
    vals.push_back(std::move(v));
  };
  if (p.title) add("title", *p.title);
  if (p.source) add("source", *p.source);
  if (p.metadata) add("metadata", json::py_dumps(*p.metadata));
  if (p.created) add("created", *p.created);
  add("updated", now_iso());
  LOOM_TRY_ASSIGN(auto st, conn_.prepare("UPDATE conversations SET " + sets + " WHERE id = ?"));
  int i = 1;
  for (const auto& v : vals) st.bind(i++, v);
  st.bind(i, cid);
  return st.run();
}

Status Database::update_conv(std::string_view cid, const ConvPatch& patch) {
  auto lk = lock();
  return update_conv_locked(cid, patch);
}

Status Database::delete_conv(std::string_view cid) {
  auto lk = lock();
  sql::Txn txn(conn_);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(conn_.run("DELETE FROM messages WHERE conv_id = ?", cid));
  LOOM_TRY(conn_.run("DELETE FROM conversations WHERE id = ?", cid));
  LOOM_TRY(txn.commit());
  fts_note_deletions();
  log::info(kLog, "Deleted conversation {}", cid);
  return {};
}

// ── Messages ───────────────────────────────────────────────────────
Result<std::string> Database::create_msg(const NewMessage& m) {
  std::string mid = gen_id(id_prefix::kMessage);
  std::string now = now_iso();
  auto lk = lock();
  sql::Txn txn(conn_);
  LOOM_TRY(txn.begin_status());
  std::string vg;
  std::int64_t version_num = 1;
  if (!m.version_group_id || m.version_group_id->empty()) {
    vg = gen_id(id_prefix::kVersionGroup);
  } else {
    vg = *m.version_group_id;
    LOOM_TRY_ASSIGN(auto mx, conn_.query_int("SELECT MAX(version_num) FROM messages WHERE version_group_id = ?", vg));
    version_num = mx.value_or(0) + 1;
    LOOM_TRY(conn_.run("UPDATE messages SET status = 'version' WHERE version_group_id = ? AND status = 'active'", vg));
  }
  LOOM_TRY_ASSIGN(auto st, conn_.prepare(
                               "INSERT INTO messages\n"
                               "                   (id, conv_id, parent_id, role, text, model, status,\n"
                               "                    version_group_id, version_num, weight, attachments, metadata, created)\n"
                               "                   VALUES (?, ?, ?, ?, ?, ?, 'active', ?, ?, ?, ?, ?, ?)"));
  st.bind_all(mid, m.conv_id, m.parent_id, m.role, m.text, m.model, vg, version_num, m.weight,
              dumps_or(m.attachments, "[]"), dumps_or(m.metadata, "{}"), now);
  LOOM_TRY(st.run());
  LOOM_TRY(update_conv_locked(m.conv_id, {}));
  LOOM_TRY(txn.commit());
  fts_note_changed(mid);
  return mid;
}

Result<int> Database::batch_create_msgs(std::string_view conv_id, const std::vector<BatchMessage>& messages,
                                        int batch_size) {
  if (batch_size <= 0) batch_size = 1000;
  std::string now = now_iso();
  int total = 0;
  auto lk = lock();
  static constexpr std::string_view kSql =
      "INSERT INTO messages\n"
      "                           (id, conv_id, parent_id, role, text, model, status,\n"
      "                            version_group_id, version_num, weight, attachments,\n"
      "                            metadata, created, semantic_status)\n"
      "                           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  for (std::size_t i = 0; i < messages.size(); i += static_cast<std::size_t>(batch_size)) {
    std::size_t end = std::min(messages.size(), i + static_cast<std::size_t>(batch_size));
    sql::Txn txn(conn_);
    LOOM_TRY(txn.begin_status());
    LOOM_TRY_ASSIGN(auto st, conn_.prepare(kSql));
    int rows = 0;
    for (std::size_t k = i; k < end; ++k) {
      const BatchMessage& msg = messages[k];
      if (msg.text.empty() || utf8::is_blank(msg.text)) continue;
      st.reset();
      // json.dumps(msg.get("attachments", [])) - no `or` here, so null stays null.
      st.bind_all(gen_id(id_prefix::kMessage), conv_id, msg.parent_id, msg.role, msg.text, msg.model,
                  std::string_view("active"), gen_id(id_prefix::kVersionGroup), std::int64_t{1}, msg.weight,
                  json::py_dumps(msg.attachments), json::py_dumps(msg.metadata), now, std::string_view("pending"));
      LOOM_TRY(st.run());
      ++rows;
    }
    LOOM_TRY(txn.commit());
    total += rows;
  }
  LOOM_TRY(update_conv_locked(conv_id, {}));
  if (total > 0 && fts_) fts_note_changed({});  // bulk: reconcile new rows
  log::info(kLog, "Batch inserted {} messages into {}", total, conv_id);
  return total;
}

Result<std::vector<PendingMessage>> Database::get_unanalysed_msgs(int limit) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto st, conn_.prepare(
                               "SELECT id, conv_id, role, text, metadata FROM messages\n"
                               "                   WHERE semantic_status = 'pending'\n"
                               "                     AND status = 'active'\n"
                               "                     AND length(text) >= 20\n"
                               "                   ORDER BY created ASC LIMIT ?"));
  st.bind(1, limit);
  std::vector<PendingMessage> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    PendingMessage p;
    p.id = st.get_text(0);
    p.conv_id = st.get_text(1);
    p.role = st.get_text(2);
    p.text = st.get_text(3);
    p.metadata = loads_or(st.get_opt_text(4), Json::object());
    out.push_back(std::move(p));
  }
  return out;
}

Result<std::int64_t> Database::count_pending_semantic() {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto n, conn_.query_int("SELECT COUNT(*) FROM messages WHERE semantic_status = 'pending'"));
  return n.value_or(0);
}

Status Database::mark_analysed(std::string_view msg_id, const Json& analysis) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto st, conn_.prepare("SELECT metadata FROM messages WHERE id = ?"));
  st.bind(1, msg_id);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return {};
  Json meta = loads_or(st.get_opt_text(0), Json::object());
  if (!meta.is_object()) meta = Json::object();  // Python would raise TypeError here.
  const Json* src = json::find(analysis, "source");
  meta["semantic_source"] = src ? *src : Json("regex");
  const Json* ents = json::find(analysis, "entities");
  meta["entity_count"] = ents ? json::py_len(*ents) : 0;
  const Json* tops = json::find(analysis, "topics");
  meta["topic_count"] = tops ? json::py_len(*tops) : 0;
  const Json* sum = json::find(analysis, "summary");
  meta["summary"] = (sum && sum->is_string()) ? std::string(utf8::prefix(sum->get_ref<const std::string&>(), 200))
                                              : std::string();
  const Json* sent = json::find(analysis, "sentiment");
  meta["sentiment"] = sent ? *sent : Json("neutral");
  return conn_.run("UPDATE messages SET metadata = ?, semantic_status = 'done' WHERE id = ?", json::py_dumps(meta),
                   msg_id);
}

Result<std::vector<Message>> Database::get_msgs(std::string_view conv_id, bool include_all) {
  auto lk = lock();
  std::string where = include_all ? "conv_id = ?" : "conv_id = ? AND status = 'active'";
  // Deviation: rowid tie-break so batch-imported rows (identical `created`)
  // come back in insertion order.
  LOOM_TRY_ASSIGN(auto st, conn_.prepare("SELECT * FROM messages WHERE " + where + " ORDER BY created, rowid"));
  st.bind(1, conv_id);
  dbi::RowMap m(st);
  std::vector<Message> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    out.push_back(dbi::read_message(st, m));
  }
  return out;
}

Result<std::optional<Message>> Database::get_msg(std::string_view mid) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto st, conn_.prepare("SELECT * FROM messages WHERE id = ?"));
  st.bind(1, mid);
  dbi::RowMap m(st);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return std::optional<Message>{};
  return std::optional<Message>(dbi::read_message(st, m));
}

Result<std::vector<Message>> Database::get_versions(std::string_view version_group_id) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto st, conn_.prepare("SELECT * FROM messages WHERE version_group_id = ? ORDER BY version_num"));
  st.bind(1, version_group_id);
  dbi::RowMap m(st);
  std::vector<Message> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    out.push_back(dbi::read_message(st, m));
  }
  return out;
}

Status Database::set_msg_status(std::string_view mid, std::string_view status) {
  if (!msg_status::is_valid(status)) return Error(Errc::InvalidArgument, "Invalid status: " + std::string(status));
  auto lk = lock();
  return conn_.run("UPDATE messages SET status = ? WHERE id = ?", status, mid);
}

Status Database::update_msg(std::string_view mid, const MsgPatch& p) {
  if (p.empty()) return Error(Errc::InvalidArgument, "update_msg: nothing to update");
  if (p.status && !msg_status::is_valid(*p.status)) {
    // Python update_msg does not validate; we keep the whitelist invariant.
    return Error(Errc::InvalidArgument, "Invalid status: " + *p.status);
  }
  std::string sets;
  auto add = [&](const char* col) {
    if (!sets.empty()) sets += ", ";
    sets += col;
    sets += " = ?";
  };
  if (p.conv_id) add("conv_id");
  if (p.parent_id) add("parent_id");
  if (p.role) add("role");
  if (p.text) add("text");
  if (p.model) add("model");
  if (p.status) add("status");
  if (p.version_group_id) add("version_group_id");
  if (p.version_num) add("version_num");
  if (p.weight) add("weight");
  if (p.attachments) add("attachments");
  if (p.metadata) add("metadata");
  if (p.created) add("created");
  if (p.semantic_status) add("semantic_status");
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto st, conn_.prepare("UPDATE messages SET " + sets + " WHERE id = ?"));
  int i = 1;
  if (p.conv_id) st.bind(i++, *p.conv_id);
  if (p.parent_id) st.bind(i++, *p.parent_id);
  if (p.role) st.bind(i++, *p.role);
  if (p.text) st.bind(i++, *p.text);
  if (p.model) st.bind(i++, *p.model);
  if (p.status) st.bind(i++, *p.status);
  if (p.version_group_id) st.bind(i++, *p.version_group_id);
  if (p.version_num) st.bind(i++, *p.version_num);
  if (p.weight) st.bind(i++, *p.weight);
  if (p.attachments) st.bind(i++, json::py_dumps(*p.attachments));  // Python: json.dumps(v), no `or`
  if (p.metadata) st.bind(i++, json::py_dumps(*p.metadata));
  if (p.created) st.bind(i++, *p.created);
  if (p.semantic_status) st.bind(i++, *p.semantic_status);
  st.bind(i, mid);
  LOOM_TRY(st.run());
  if (p.text) fts_note_changed(mid);
  return {};
}

Result<std::optional<std::string>> Database::edit_msg(std::string_view mid, std::string_view new_text) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto old, get_msg(mid));
  if (!old) return std::optional<std::string>{};
  NewMessage nm;
  nm.conv_id = old->conv_id;
  nm.text = std::string(new_text);
  nm.role = old->role;
  nm.model = old->model;
  nm.parent_id = old->parent_id;
  nm.attachments = old->attachments;
  nm.version_group_id = old->version_group_id;
  nm.weight = old->weight;
  nm.metadata = old->metadata;
  LOOM_TRY_ASSIGN(std::string id, create_msg(nm));
  return std::optional<std::string>(std::move(id));
}

Result<bool> Database::restore_version(std::string_view mid) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto msg, get_msg(mid));
  if (!msg) return false;
  sql::Txn txn(conn_);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(conn_.run("UPDATE messages SET status = 'version' WHERE version_group_id = ? AND status = 'active'",
                     msg->version_group_id));
  LOOM_TRY(conn_.run("UPDATE messages SET status = 'active' WHERE id = ?", mid));
  LOOM_TRY(txn.commit());
  return true;
}

// ── Links ──────────────────────────────────────────────────────────
Result<std::string> Database::create_link(std::string_view src, std::string_view dst, std::string_view link_type,
                                          double weight, const Json& metadata) {
  std::string lid = gen_id(id_prefix::kLink);
  std::string now = now_iso();
  std::string meta = dumps_or(metadata, "{}");
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto existing,
                  conn_.query_text("SELECT id FROM links WHERE src=? AND dst=? AND link_type=?", src, dst, link_type));
  if (existing) {
    LOOM_TRY(conn_.run("UPDATE links SET weight=?, metadata=? WHERE id=?", weight, meta, *existing));
    return *existing;
  }
  LOOM_TRY(conn_.run(
      "INSERT INTO links (id, src, dst, link_type, weight, metadata, created) VALUES (?, ?, ?, ?, ?, ?, ?)", lid, src,
      dst, link_type, weight, meta, now));
  return lid;
}

Result<std::vector<Link>> Database::get_links(std::optional<std::string_view> node_id,
                                              std::optional<std::string_view> link_type) {
  auto lk = lock();
  // Python treats empty strings as "not given" (`if node_id and link_type`).
  bool has_node = node_id && !node_id->empty();
  bool has_type = link_type && !link_type->empty();
  sql::Stmt st;
  if (has_node && has_type) {
    LOOM_TRY_ASSIGN(st, conn_.prepare("SELECT * FROM links WHERE (src=? OR dst=?) AND link_type=?"));
    st.bind_all(*node_id, *node_id, *link_type);
  } else if (has_node) {
    LOOM_TRY_ASSIGN(st, conn_.prepare("SELECT * FROM links WHERE src=? OR dst=?"));
    st.bind_all(*node_id, *node_id);
  } else if (has_type) {
    LOOM_TRY_ASSIGN(st, conn_.prepare("SELECT * FROM links WHERE link_type=?"));
    st.bind_all(*link_type);
  } else {
    LOOM_TRY_ASSIGN(st, conn_.prepare("SELECT * FROM links"));
  }
  dbi::RowMap m(st);
  std::vector<Link> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    out.push_back(dbi::read_link(st, m));
  }
  return out;
}

Status Database::delete_link(std::string_view lid) {
  auto lk = lock();
  return conn_.run("DELETE FROM links WHERE id = ?", lid);
}

// ── Nodes ──────────────────────────────────────────────────────────
Result<std::string> Database::create_node(std::string_view label, std::string_view kind, const NodeOptions& opts) {
  std::string nid = (opts.node_id && !opts.node_id->empty()) ? *opts.node_id : gen_id(id_prefix::kNode);
  std::string now = now_iso();
  auto lk = lock();
  LOOM_TRY(conn_.run(
      "INSERT OR IGNORE INTO nodes (id, kind, label, content, tags, metadata, created) VALUES (?, ?, ?, ?, ?, ?, ?)",
      nid, kind, label, opts.content, dumps_or(opts.tags, "[]"), dumps_or(opts.metadata, "{}"), now));
  return nid;
}

Result<std::optional<Node>> Database::node_query(std::string_view sql, std::string_view a,
                                                 std::optional<std::string_view> b) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto st, conn_.prepare(sql));
  st.bind(1, a);
  if (b) st.bind(2, *b);
  dbi::RowMap m(st);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return std::optional<Node>{};
  return std::optional<Node>(dbi::read_node(st, m));
}

Result<std::optional<Node>> Database::get_node(std::string_view nid) {
  return node_query("SELECT * FROM nodes WHERE id=?", nid, std::nullopt);
}

Result<std::optional<Node>> Database::find_node(std::string_view label, std::optional<std::string_view> kind) {
  if (kind && !kind->empty()) return node_query("SELECT * FROM nodes WHERE label=? AND kind=?", label, kind);
  return node_query("SELECT * FROM nodes WHERE label=?", label, std::nullopt);
}

Result<std::vector<Node>> Database::list_nodes(std::optional<std::string_view> kind, int limit) {
  auto lk = lock();
  sql::Stmt st;
  if (kind && !kind->empty()) {
    LOOM_TRY_ASSIGN(st, conn_.prepare("SELECT * FROM nodes WHERE kind=? ORDER BY created DESC, rowid DESC LIMIT ?"));
    st.bind_all(*kind, limit);
  } else {
    LOOM_TRY_ASSIGN(st, conn_.prepare("SELECT * FROM nodes ORDER BY created DESC, rowid DESC LIMIT ?"));
    st.bind_all(limit);
  }
  dbi::RowMap m(st);
  std::vector<Node> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    out.push_back(dbi::read_node(st, m));
  }
  return out;
}

Status Database::update_node(std::string_view nid, const NodePatch& p) {
  if (p.empty()) return Error(Errc::InvalidArgument, "update_node: nothing to update");
  std::string sets;
  auto add = [&](const char* col) {
    if (!sets.empty()) sets += ", ";
    sets += col;
    sets += " = ?";
  };
  if (p.kind) add("kind");
  if (p.label) add("label");
  if (p.content) add("content");
  if (p.tags) add("tags");
  if (p.metadata) add("metadata");
  if (p.created) add("created");
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto st, conn_.prepare("UPDATE nodes SET " + sets + " WHERE id = ?"));
  int i = 1;
  if (p.kind) st.bind(i++, *p.kind);
  if (p.label) st.bind(i++, *p.label);
  if (p.content) st.bind(i++, *p.content);
  if (p.tags) st.bind(i++, json::py_dumps(*p.tags));
  if (p.metadata) st.bind(i++, json::py_dumps(*p.metadata));
  if (p.created) st.bind(i++, *p.created);
  st.bind(i, nid);
  return st.run();
}

Status Database::delete_node(std::string_view nid) {
  auto lk = lock();
  sql::Txn txn(conn_);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(conn_.run("DELETE FROM links WHERE src=? OR dst=?", nid, nid));
  LOOM_TRY(conn_.run("DELETE FROM nodes WHERE id=?", nid));
  return txn.commit();
}

Result<std::string> Database::get_or_create_node(std::string_view label, std::string_view kind,
                                                 const NodeOptions& opts) {
  auto lk = lock();
  LOOM_TRY_ASSIGN(auto existing, find_node(label, kind));
  if (existing) return existing->id;
  return create_node(label, kind, opts);
}

// ── Graph data ─────────────────────────────────────────────────────
Result<Json> Database::get_graph_data(std::optional<std::string_view> conv_id) {
  auto lk = lock();
  Json nodes = Json::array();
  Json edges = Json::array();
  std::unordered_map<std::string, bool> seen;
  if (conv_id && !conv_id->empty()) {
    LOOM_TRY_ASSIGN(auto msgs, get_msgs(*conv_id, true));
    for (const auto& m : msgs) {
      nodes.push_back(Json{{"id", m.id},
                           {"label", std::string(utf8::prefix(m.text, 25))},
                           {"type", m.role},
                           {"kind", "message"},
                           {"status", m.status},
                           {"weight", m.weight},
                           {"version_group", m.version_group_id ? Json(*m.version_group_id) : Json(nullptr)}});
      seen[m.id] = true;
      if (m.parent_id && !m.parent_id->empty()) {
        edges.push_back(Json{{"src", *m.parent_id}, {"dst", m.id}, {"type", "reply"}, {"weight", 1.0}});
      }
    }
  }
  LOOM_TRY_ASSIGN(auto all_nodes, list_nodes());
  for (const auto& n : all_nodes) {
    if (seen.count(n.id)) continue;
    nodes.push_back(Json{{"id", n.id},
                         {"label", n.label},
                         {"type", n.kind},
                         {"kind", n.kind},
                         {"status", "active"},
                         {"weight", 1.0},
                         {"tags", n.tags}});
    seen[n.id] = true;
  }
  LOOM_TRY_ASSIGN(auto links, get_links());
  for (const auto& l : links) {
    edges.push_back(Json{{"src", l.src}, {"dst", l.dst}, {"type", l.link_type}, {"weight", l.weight}});
  }
  return Json{{"nodes", std::move(nodes)}, {"edges", std::move(edges)}};
}

// ── Housekeeping ───────────────────────────────────────────────────
Status Database::vacuum() {
  auto lk = lock();
  return conn_.exec("VACUUM");
}

}  // namespace loom
