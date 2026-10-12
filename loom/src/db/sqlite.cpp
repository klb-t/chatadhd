#include "loom/sqlite.h"

#include <sqlite3.h>

#include "loom/util/ids.h"

namespace loom::sql {
namespace {

Error sqlite_error(sqlite3* db, int rc, std::string_view what) {
  std::string msg(what);
  msg += ": ";
  msg += db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
  Errc code = Errc::Database;
  int primary = rc & 0xFF;
  if (primary == SQLITE_BUSY || primary == SQLITE_LOCKED) code = Errc::Busy;
  if (primary == SQLITE_CONSTRAINT) code = Errc::Conflict;
  if (primary == SQLITE_CANTOPEN || primary == SQLITE_IOERR || primary == SQLITE_FULL) code = Errc::Io;
  return Error(code, std::move(msg));
}

std::string quote_ident(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out.push_back('"');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

}  // namespace

// ── Stmt ───────────────────────────────────────────────────────────
Stmt::~Stmt() {
  if (stmt_) sqlite3_finalize(stmt_);
}

Stmt::Stmt(Stmt&& other) noexcept
    : stmt_(other.stmt_), db_(other.db_), bind_error_(std::move(other.bind_error_)) {
  other.stmt_ = nullptr;
}

Stmt& Stmt::operator=(Stmt&& other) noexcept {
  if (this != &other) {
    if (stmt_) sqlite3_finalize(stmt_);
    stmt_ = other.stmt_;
    db_ = other.db_;
    bind_error_ = std::move(other.bind_error_);
    other.stmt_ = nullptr;
  }
  return *this;
}

#define LOOM_BIND_CHECK(rc)                                                       \
  do {                                                                            \
    int loom_rc_ = (rc);                                                          \
    if (loom_rc_ != SQLITE_OK && !bind_error_) bind_error_ = sqlite_error(db_, loom_rc_, "bind"); \
  } while (0)

Stmt& Stmt::bind(int idx, std::int64_t v) {
  LOOM_BIND_CHECK(sqlite3_bind_int64(stmt_, idx, v));
  return *this;
}
Stmt& Stmt::bind(int idx, double v) {
  LOOM_BIND_CHECK(sqlite3_bind_double(stmt_, idx, v));
  return *this;
}
Stmt& Stmt::bind(int idx, std::string_view v) {
  LOOM_BIND_CHECK(sqlite3_bind_text64(stmt_, idx, v.data(), v.size(), SQLITE_TRANSIENT, SQLITE_UTF8));
  return *this;
}
Stmt& Stmt::bind_blob(int idx, std::string_view bytes) {
  LOOM_BIND_CHECK(sqlite3_bind_blob64(stmt_, idx, bytes.data(), bytes.size(), SQLITE_TRANSIENT));
  return *this;
}
Stmt& Stmt::bind_null(int idx) {
  LOOM_BIND_CHECK(sqlite3_bind_null(stmt_, idx));
  return *this;
}
#undef LOOM_BIND_CHECK

Result<bool> Stmt::step() {
  if (bind_error_) return *bind_error_;
  int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  return sqlite_error(db_, rc, "step");
}

Status Stmt::run() {
  while (true) {
    LOOM_TRY_ASSIGN(bool row, step());
    if (!row) return {};
  }
}

void Stmt::reset() {
  sqlite3_reset(stmt_);
  sqlite3_clear_bindings(stmt_);
  bind_error_.reset();
}

int Stmt::column_count() const { return sqlite3_column_count(stmt_); }
std::string Stmt::column_name(int col) const {
  const char* n = sqlite3_column_name(stmt_, col);
  return n ? n : "";
}
Type Stmt::column_type(int col) const { return static_cast<Type>(sqlite3_column_type(stmt_, col)); }
bool Stmt::is_null(int col) const { return sqlite3_column_type(stmt_, col) == SQLITE_NULL; }
std::int64_t Stmt::get_int(int col) const { return sqlite3_column_int64(stmt_, col); }
double Stmt::get_double(int col) const { return sqlite3_column_double(stmt_, col); }
std::string Stmt::get_text(int col) const {
  const auto* p = sqlite3_column_text(stmt_, col);
  if (!p) return {};
  int n = sqlite3_column_bytes(stmt_, col);
  return std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n));
}
std::optional<std::string> Stmt::get_opt_text(int col) const {
  if (is_null(col)) return std::nullopt;
  return get_text(col);
}
std::string Stmt::get_blob(int col) const {
  const void* p = sqlite3_column_blob(stmt_, col);
  int n = sqlite3_column_bytes(stmt_, col);
  if (!p || n <= 0) return {};
  return std::string(static_cast<const char*>(p), static_cast<std::size_t>(n));
}

// ── Connection ─────────────────────────────────────────────────────
Connection::~Connection() { close(); }

Connection::Connection(Connection&& other) noexcept : db_(other.db_) { other.db_ = nullptr; }

Connection& Connection::operator=(Connection&& other) noexcept {
  if (this != &other) {
    close();
    db_ = other.db_;
    other.db_ = nullptr;
  }
  return *this;
}

void Connection::close() noexcept {
  if (db_) {
    sqlite3_close_v2(db_);
    db_ = nullptr;
  }
}

Result<Connection> Connection::open(const std::filesystem::path& path, const OpenOptions& opts) {
  sqlite3* db = nullptr;
  int flags = SQLITE_OPEN_FULLMUTEX;
  flags |= opts.read_only ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
  if (opts.create && !opts.read_only) flags |= SQLITE_OPEN_CREATE;
  int rc = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
  if (rc != SQLITE_OK) {
    Error e = sqlite_error(db, rc, "open " + path.string());
    sqlite3_close_v2(db);
    return e;
  }
  sqlite3_extended_result_codes(db, 1);
  sqlite3_busy_timeout(db, opts.busy_timeout_ms);
  return Connection(db);
}

Result<Connection> Connection::open_memory() {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
  if (rc != SQLITE_OK) {
    Error e = sqlite_error(db, rc, "open :memory:");
    sqlite3_close_v2(db);
    return e;
  }
  return Connection(db);
}

Status Connection::exec(std::string_view sql) {
  std::string s(sql);
  char* err = nullptr;
  int rc = sqlite3_exec(db_, s.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err ? err : sqlite3_errstr(rc);
    sqlite3_free(err);
    Errc code = ((rc & 0xFF) == SQLITE_BUSY || (rc & 0xFF) == SQLITE_LOCKED) ? Errc::Busy : Errc::Database;
    return Error(code, "exec: " + msg);
  }
  return {};
}

Result<Stmt> Connection::prepare(std::string_view sql) {
  sqlite3_stmt* st = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &st, nullptr);
  if (rc != SQLITE_OK) return sqlite_error(db_, rc, "prepare [" + std::string(sql.substr(0, 120)) + "]");
  return Stmt(st, db_);
}

std::int64_t Connection::last_insert_rowid() const noexcept { return sqlite3_last_insert_rowid(db_); }
int Connection::changes() const noexcept { return sqlite3_changes(db_); }
bool Connection::in_transaction() const noexcept { return sqlite3_get_autocommit(db_) == 0; }

bool Connection::has_table(std::string_view name, std::string_view schema) {
  std::string sql = "SELECT 1 FROM " + quote_ident(schema) + ".sqlite_master WHERE type='table' AND name=?";
  auto r = query_int(sql, name);
  return r && r->has_value();
}

std::vector<std::string> Connection::columns(std::string_view table, std::string_view schema) {
  std::vector<std::string> out;
  std::string sql = "PRAGMA " + quote_ident(schema) + ".table_info(" + quote_ident(table) + ")";
  auto st = prepare(sql);
  if (!st) return out;
  while (true) {
    auto row = st->step();
    if (!row || !*row) break;
    out.push_back(st->get_text(1));
  }
  return out;
}

bool Connection::has_column(std::string_view table, std::string_view column, std::string_view schema) {
  for (const auto& c : columns(table, schema)) {
    if (c == column) return true;
  }
  return false;
}

bool Connection::has_fts5() {
  auto st = prepare("SELECT sqlite_compileoption_used('ENABLE_FTS5')");
  if (st) {
    auto row = st->step();
    if (row && *row && st->get_int(0) == 1) return true;
  }
  // The module can also be registered without the compile option; probe.
  auto probe = exec("CREATE VIRTUAL TABLE temp.loom_fts5_probe USING fts5(x)");
  if (!probe) return false;
  (void)exec("DROP TABLE temp.loom_fts5_probe");
  return true;
}

std::string Connection::last_error() const { return db_ ? sqlite3_errmsg(db_) : "closed"; }

// ── Txn ────────────────────────────────────────────────────────────
Txn::Txn(Connection& c, bool immediate) : c_(c) {
  if (c_.in_transaction()) {
    nested_ = true;
    savepoint_ = "loom_sp_" + random_hex(8);
    begin_ = c_.exec("SAVEPOINT " + savepoint_);
  } else {
    begin_ = c_.exec(immediate ? "BEGIN IMMEDIATE" : "BEGIN");
  }
  if (!begin_) done_ = true;
}

Txn::~Txn() {
  if (!done_) rollback();
}

Status Txn::commit() {
  if (done_) return begin_ ? Status(Error(Errc::Internal, "transaction already finished")) : begin_;
  Status st = nested_ ? c_.exec("RELEASE " + savepoint_) : c_.exec("COMMIT");
  if (!st) {
    rollback();
    return st;
  }
  done_ = true;
  return {};
}

void Txn::rollback() noexcept {
  if (done_) return;
  done_ = true;
  if (nested_) {
    (void)c_.exec("ROLLBACK TO " + savepoint_);
    (void)c_.exec("RELEASE " + savepoint_);
  } else if (c_.in_transaction()) {
    (void)c_.exec("ROLLBACK");
  }
}

std::string library_version() { return sqlite3_libversion(); }

}  // namespace loom::sql
