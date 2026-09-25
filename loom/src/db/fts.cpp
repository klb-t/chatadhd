// Full-text search over messages.text — derived state in a separate SQLite
// file attached as schema "fts". See db.h for the invariants.
//
// Sync model:
//   * Loom's own writes mark message ids dirty (create/edit/update text) or
//     request a scan for new rows (batch insert) / deleted rows (delete_conv).
//   * Writes by any other connection (e.g. the Python app on the same data
//     dir) are detected through PRAGMA main.data_version and trigger a full
//     reconcile: deletions, changed texts (length + 64-bit signature), new rows.
//   * Any FTS error, a format mismatch or a docs/index count mismatch drops and
//     rebuilds the index (self-healing). If FTS5 is missing we fall back to LIKE.
#include <set>

#include <sqlite3.h>

#include "db_internal.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/util/fs.h"
#include "loom/util/utf8.h"

namespace loom {
namespace {

constexpr std::string_view kLog = "loom.fts";
constexpr std::string_view kFtsFormat = "1";

// FNV-1a 64 over the UTF-8 bytes: cheap change detection for texts edited by
// other processes (not a security hash).
void loom_sig_fn(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  if (argc != 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_int64(ctx, 0);
    return;
  }
  const auto* p = sqlite3_value_text(argv[0]);
  int n = sqlite3_value_bytes(argv[0]);
  std::uint64_t h = 1469598103934665603ULL;
  for (int i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 1099511628211ULL;
  }
  sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(h));
}

std::uint64_t fnv(std::string_view s) {
  std::uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

// Turns free text into an FTS5 query: every whitespace-separated token is
// quoted (so punctuation/operators are literal); a trailing '*' becomes a
// prefix query. Tokens are ANDed.
std::string build_match(std::string_view q, std::vector<std::string>& tokens) {
  std::string out;
  std::string cur;
  auto flush = [&] {
    if (cur.empty()) return;
    bool prefix = cur.size() > 1 && cur.back() == '*';
    if (prefix) cur.pop_back();
    std::string quoted = "\"";
    for (char c : cur) {
      if (c == '"') quoted.push_back('"');
      quoted.push_back(c);
    }
    quoted.push_back('"');
    if (prefix) quoted.push_back('*');
    if (!out.empty()) out.push_back(' ');
    out += quoted;
    tokens.push_back(cur);
    cur.clear();
  };
  for (char c : q) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      flush();
    } else {
      cur.push_back(c);
    }
  }
  flush();
  return out;
}

std::string like_escape(std::string_view s) {
  std::string out = "%";
  for (char c : s) {
    if (c == '%' || c == '_' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('%');
  return out;
}

// Excerpt around the first case-insensitive (ASCII) occurrence of a token.
std::string make_snippet(const std::string& text, const std::vector<std::string>& tokens) {
  std::string lower = utf8::to_lower(text);
  std::size_t pos = std::string::npos;
  std::size_t len = 0;
  for (const auto& t : tokens) {
    std::string lt = utf8::to_lower(t);
    std::size_t p = lower.size() == text.size() ? lower.find(lt) : std::string::npos;
    if (p != std::string::npos && (pos == std::string::npos || p < pos)) {
      pos = p;
      len = lt.size();
    }
  }
  if (pos == std::string::npos) return std::string(utf8::prefix(text, 80));
  std::size_t start = pos > 60 ? pos - 60 : 0;
  while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) --start;
  std::size_t end = std::min(text.size(), pos + len + 60);
  while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) ++end;
  std::string out;
  if (start > 0) out += "...";
  out += text.substr(start, pos - start);
  out += "[";
  out += text.substr(pos, len);
  out += "]";
  out += text.substr(pos + len, end - pos - len);
  if (end < text.size()) out += "...";
  return out;
}

}  // namespace


void Database::fts_note_changed(std::string_view msg_id) {
  auto lk = lock();
  if (!fts_ || !fts_->available) return;
  if (msg_id.empty()) {
    fts_->scan_new = true;
  } else {
    fts_->dirty_ids.emplace(msg_id);
  }
}

void Database::fts_note_deletions() {
  auto lk = lock();
  if (fts_ && fts_->available) fts_->scan_deleted = true;
}

Status Database::fts_open() {
  fts_ = std::make_unique<FtsState>();
  if (!opts_.enable_fts) return {};
  if (!conn_.has_fts5()) {
    log::info(kLog, "FTS5 not available in SQLite {} - using LIKE search", sql::library_version());
    return {};
  }
  std::filesystem::path p = opts_.fts_path ? *opts_.fts_path : std::filesystem::path(path_).replace_extension(".fts.db");
  fts_->path = p;
  sqlite3_create_function_v2(conn_.handle(), "loom_sig", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, loom_sig_fn,
                             nullptr, nullptr, nullptr);

  auto attach = [&]() -> Status {
    LOOM_TRY_ASSIGN(auto st, conn_.prepare("ATTACH DATABASE ? AS fts"));
    st.bind(1, p.string());
    return st.run();
  };
  auto create = [&]() -> Status {
    (void)conn_.exec("PRAGMA fts.journal_mode=WAL");
    return conn_.exec(
        "CREATE TABLE IF NOT EXISTS fts.loom_fts_meta (key TEXT PRIMARY KEY, value TEXT);"
        "CREATE TABLE IF NOT EXISTS fts.loom_fts_docs ("
        "  docid INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  msg_id TEXT NOT NULL UNIQUE,"
        "  text_len INTEGER NOT NULL,"
        "  text_sig INTEGER NOT NULL);"
        "CREATE VIRTUAL TABLE IF NOT EXISTS fts.loom_fts USING fts5(text, tokenize='unicode61 remove_diacritics 2');"
        "INSERT OR IGNORE INTO fts.loom_fts_meta(key, value) VALUES ('format', '1');");
  };
  auto remove_files = [&] {
    std::error_code ec;
    for (const char* suffix : {"", "-wal", "-shm", "-journal"}) {
      std::filesystem::remove(std::filesystem::path(p.string() + suffix), ec);
    }
  };

  Status st = attach();
  if (!st) {
    log::warn(kLog, "cannot attach FTS index {} ({}) - recreating", p.string(), st.error().message);
    remove_files();
    st = attach();
    if (!st) {
      log::warn(kLog, "FTS disabled: {}", st.error().message);
      return {};
    }
  }
  st = create();
  bool healthy = static_cast<bool>(st);
  if (healthy) {
    auto fmt = conn_.query_text("SELECT value FROM fts.loom_fts_meta WHERE key='format'");
    healthy = fmt && fmt->has_value() && **fmt == kFtsFormat;
  }
  if (healthy) {
    auto docs = conn_.query_int("SELECT COUNT(*) FROM fts.loom_fts_docs");
    auto rows = conn_.query_int("SELECT COUNT(*) FROM fts.loom_fts");
    healthy = docs && rows && docs->value_or(-1) == rows->value_or(-2);
  }
  if (!healthy) {
    log::warn(kLog, "FTS index {} is unusable - rebuilding", p.string());
    (void)conn_.exec("DETACH DATABASE fts");
    remove_files();
    st = attach();
    if (st) st = create();
    if (!st) {
      log::warn(kLog, "FTS disabled: {}", st.error().message);
      (void)conn_.exec("DETACH DATABASE fts");
      return {};
    }
    fts_->rebuilds++;
  }
  fts_->available = true;
  fts_->scan_changed = true;
  fts_->scan_new = true;
  fts_->scan_deleted = true;
  return {};
}

Status Database::fts_index_ids_locked(const std::vector<std::string>& ids) {
  for (const auto& id : ids) {
    LOOM_TRY_ASSIGN(auto text, conn_.query_text("SELECT text FROM main.messages WHERE id = ?", id));
    LOOM_TRY_ASSIGN(auto docid, conn_.query_int("SELECT docid FROM fts.loom_fts_docs WHERE msg_id = ?", id));
    if (!text) {
      if (docid) {
        LOOM_TRY(conn_.run("DELETE FROM fts.loom_fts WHERE rowid = ?", *docid));
        LOOM_TRY(conn_.run("DELETE FROM fts.loom_fts_docs WHERE docid = ?", *docid));
      }
      continue;
    }
    auto sig = static_cast<std::int64_t>(fnv(*text));
    auto len = static_cast<std::int64_t>(text->size());
    if (docid) {
      LOOM_TRY(conn_.run("UPDATE fts.loom_fts SET text = ? WHERE rowid = ?", *text, *docid));
      LOOM_TRY(conn_.run("UPDATE fts.loom_fts_docs SET text_len = ?, text_sig = ? WHERE docid = ?", len, sig, *docid));
    } else {
      LOOM_TRY(conn_.run("INSERT INTO fts.loom_fts_docs(msg_id, text_len, text_sig) VALUES (?, ?, ?)", id, len, sig));
      LOOM_TRY(conn_.run("INSERT INTO fts.loom_fts(rowid, text) VALUES (?, ?)", conn_.last_insert_rowid(), *text));
    }
  }
  return {};
}

Status Database::fts_reconcile_locked(bool full) {
  auto& f = *fts_;
  sql::Txn txn(conn_, /*immediate=*/false);
  LOOM_TRY(txn.begin_status());
  if (full || f.scan_deleted) {
    LOOM_TRY(conn_.exec(
        "DELETE FROM fts.loom_fts WHERE rowid IN (SELECT d.docid FROM fts.loom_fts_docs d "
        "WHERE NOT EXISTS (SELECT 1 FROM main.messages m WHERE m.id = d.msg_id));"
        "DELETE FROM fts.loom_fts_docs WHERE NOT EXISTS "
        "(SELECT 1 FROM main.messages m WHERE m.id = fts.loom_fts_docs.msg_id);"));
  }
  if (full) {
    std::vector<std::string> changed;
    LOOM_TRY_ASSIGN(auto st, conn_.prepare(
                                 "SELECT m.id FROM main.messages m JOIN fts.loom_fts_docs d ON d.msg_id = m.id "
                                 "WHERE d.text_len != length(CAST(m.text AS BLOB)) OR d.text_sig != loom_sig(m.text)"));
    while (true) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      changed.push_back(st.get_text(0));
    }
    LOOM_TRY(fts_index_ids_locked(changed));
  }
  if (full || f.scan_new) {
    LOOM_TRY_ASSIGN(auto maxdoc, conn_.query_int("SELECT COALESCE(MAX(docid), 0) FROM fts.loom_fts_docs"));
    LOOM_TRY(conn_.exec(
        "INSERT INTO fts.loom_fts_docs(msg_id, text_len, text_sig) "
        "SELECT m.id, length(CAST(m.text AS BLOB)), loom_sig(m.text) FROM main.messages m "
        "WHERE NOT EXISTS (SELECT 1 FROM fts.loom_fts_docs d WHERE d.msg_id = m.id) ORDER BY m.rowid"));
    LOOM_TRY(conn_.run(
        "INSERT INTO fts.loom_fts(rowid, text) SELECT d.docid, m.text FROM fts.loom_fts_docs d "
        "JOIN main.messages m ON m.id = d.msg_id WHERE d.docid > ?",
        maxdoc.value_or(0)));
  }
  if (!f.dirty_ids.empty()) {
    std::vector<std::string> ids(f.dirty_ids.begin(), f.dirty_ids.end());
    LOOM_TRY(fts_index_ids_locked(ids));
  }
  LOOM_TRY(txn.commit());
  f.dirty_ids.clear();
  f.scan_new = f.scan_deleted = false;
  if (full) f.scan_changed = false;
  return {};
}

Status Database::fts_rebuild_locked() {
  auto& f = *fts_;
  f.rebuilds++;
  sql::Txn txn(conn_, false);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(conn_.exec("DELETE FROM fts.loom_fts; DELETE FROM fts.loom_fts_docs;"
                      "DELETE FROM fts.sqlite_sequence WHERE name='loom_fts_docs';"));
  LOOM_TRY(txn.commit());
  f.dirty_ids.clear();
  return fts_reconcile_locked(true);
}

Status Database::fts_ensure_synced_locked() {
  if (!fts_ || !fts_->available) return {};
  auto& f = *fts_;
  LOOM_TRY_ASSIGN(auto dv, conn_.query_int("PRAGMA main.data_version"));
  std::int64_t v = dv.value_or(0);
  if (f.last_data_version != -1 && v != f.last_data_version) f.scan_changed = true;  // another connection wrote
  f.last_data_version = v;
  if (!f.scan_changed && !f.scan_new && !f.scan_deleted && f.dirty_ids.empty()) return {};
  Status st = fts_reconcile_locked(f.scan_changed);
  if (!st) {
    log::warn(kLog, "FTS sync failed ({}) - rebuilding index", st.error().message);
    st = fts_rebuild_locked();
    if (!st) {
      log::warn(kLog, "FTS rebuild failed ({}) - falling back to LIKE", st.error().message);
      f.available = false;
    }
  }
  return {};
}

Status Database::fts_sync() {
  auto lk = lock();
  return fts_ensure_synced_locked();
}

Status Database::fts_rebuild() {
  auto lk = lock();
  if (!fts_ || !fts_->available) return Error(Errc::Unavailable, "FTS5 index not available");
  return fts_rebuild_locked();
}

FtsStatus Database::fts_status() {
  auto lk = lock();
  FtsStatus s;
  if (fts_) {
    (void)fts_ensure_synced_locked();
    s.available = fts_->available;
    s.path = fts_->path.string();
    s.rebuilds = fts_->rebuilds;
    s.dirty = !fts_->dirty_ids.empty() || fts_->scan_new || fts_->scan_deleted;
    if (s.available) {
      auto n = conn_.query_int("SELECT COUNT(*) FROM fts.loom_fts_docs");
      if (n && *n) s.indexed = **n;
    }
  }
  auto m = conn_.query_int("SELECT COUNT(*) FROM main.messages");
  if (m && *m) s.messages = **m;
  return s;
}

Result<SearchResult> Database::search_messages(std::string_view query, const SearchOptions& opts) {
  auto lk = lock();
  SearchResult res;
  std::vector<std::string> tokens;
  std::string match = build_match(query, tokens);
  int limit = opts.limit > 0 ? opts.limit : 50;
  if (tokens.empty()) {
    res.mode = (fts_ && fts_->available) ? "fts5" : "like";
    return res;
  }
  (void)fts_ensure_synced_locked();
  bool use_fts = fts_ && fts_->available && opts.mode != FtsMode::Like;
  if (opts.mode == FtsMode::Fts5 && !use_fts) return Error(Errc::Unavailable, "FTS5 index not available");

  if (use_fts) {
    std::string sql =
        "SELECT m.*, -bm25(loom_fts) AS loom_score, "
        "snippet(loom_fts, 0, '[', ']', '...', 16) AS loom_snippet "
        "FROM fts.loom_fts JOIN fts.loom_fts_docs d ON d.docid = loom_fts.rowid "
        "JOIN main.messages m ON m.id = d.msg_id WHERE loom_fts MATCH ?";
    if (opts.conv_id) sql += " AND m.conv_id = ?";
    if (!opts.include_inactive) sql += " AND m.status = 'active'";
    sql += " ORDER BY bm25(loom_fts) LIMIT ?";
    auto st = conn_.prepare(sql);
    if (st) {
      int i = 1;
      st->bind(i++, match);
      if (opts.conv_id) st->bind(i++, *opts.conv_id);
      st->bind(i, limit);
      dbi::RowMap m(*st);
      bool ok = true;
      while (true) {
        auto row = st->step();
        if (!row) {
          log::warn(kLog, "FTS query failed: {}", row.error().message);
          ok = false;
          break;
        }
        if (!*row) break;
        SearchHit h;
        h.message = dbi::read_message(*st, m);
        h.score = m.real(*st, "loom_score", 0.0);
        h.snippet = m.text(*st, "loom_snippet");
        res.hits.push_back(std::move(h));
      }
      if (ok) {
        res.mode = "fts5";
        return res;
      }
      res.hits.clear();
    }
  }

  // LIKE fallback: every token must occur (ASCII case-insensitive).
  std::string sql = "SELECT m.* FROM main.messages m WHERE 1=1";
  for (std::size_t i = 0; i < tokens.size(); ++i) sql += " AND m.text LIKE ? ESCAPE '\\'";
  if (opts.conv_id) sql += " AND m.conv_id = ?";
  if (!opts.include_inactive) sql += " AND m.status = 'active'";
  sql += " ORDER BY m.created DESC, m.rowid DESC LIMIT ?";
  LOOM_TRY_ASSIGN(auto st, conn_.prepare(sql));
  int i = 1;
  for (const auto& t : tokens) st.bind(i++, like_escape(t));
  if (opts.conv_id) st.bind(i++, *opts.conv_id);
  st.bind(i, limit);
  dbi::RowMap m(st);
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    SearchHit h;
    h.message = dbi::read_message(st, m);
    h.score = 1.0;
    h.snippet = make_snippet(h.message.text, tokens);
    res.hits.push_back(std::move(h));
  }
  res.mode = "like";
  return res;
}

}  // namespace loom
