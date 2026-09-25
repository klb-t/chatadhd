// loom/sqlite.h — minimal RAII wrapper over the SQLite C API.
//
// Every module that owns tables (db, provenance, tasks, relations, FTS) goes
// through this wrapper; no raw sqlite3* ownership anywhere else. The wrapper
// is not itself synchronised: Database serialises access with its recursive
// mutex (see Database::lock()).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"

struct sqlite3;
struct sqlite3_stmt;

namespace loom::sql {

class Connection;

// SQLite fundamental column types.
enum class Type { Integer = 1, Float = 2, Text = 3, Blob = 4, Null = 5 };

class Stmt {
 public:
  Stmt() = default;
  ~Stmt();
  Stmt(Stmt&& other) noexcept;
  Stmt& operator=(Stmt&& other) noexcept;
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  bool valid() const noexcept { return stmt_ != nullptr; }

  // 1-based bind indices, like sqlite3_bind_*.
  Stmt& bind(int idx, std::int64_t v);
  Stmt& bind(int idx, int v) { return bind(idx, static_cast<std::int64_t>(v)); }
  Stmt& bind(int idx, double v);
  Stmt& bind(int idx, std::string_view v);  // copied (SQLITE_TRANSIENT)
  Stmt& bind(int idx, const char* v) { return v ? bind(idx, std::string_view(v)) : bind_null(idx); }
  Stmt& bind(int idx, const std::string& v) { return bind(idx, std::string_view(v)); }
  Stmt& bind(int idx, std::nullptr_t) { return bind_null(idx); }
  template <class T>
  Stmt& bind(int idx, const std::optional<T>& v) {
    return v ? bind(idx, *v) : bind_null(idx);
  }
  Stmt& bind_blob(int idx, std::string_view bytes);
  Stmt& bind_null(int idx);

  // Bind all arguments positionally starting at 1.
  template <class... Args>
  Stmt& bind_all(const Args&... args) {
    int i = 1;
    (bind(i++, args), ...);
    return *this;
  }

  // Advances: true = a row is available, false = done.
  Result<bool> step();
  // Runs to completion (for DML); fails if a bind error was recorded.
  Status run();
  void reset();

  int column_count() const;
  std::string column_name(int col) const;
  Type column_type(int col) const;
  bool is_null(int col) const;
  std::int64_t get_int(int col) const;
  double get_double(int col) const;
  std::string get_text(int col) const;             // "" for NULL
  std::optional<std::string> get_opt_text(int col) const;
  std::string get_blob(int col) const;

  sqlite3_stmt* handle() const noexcept { return stmt_; }

 private:
  friend class Connection;
  Stmt(sqlite3_stmt* s, sqlite3* db) : stmt_(s), db_(db) {}
  sqlite3_stmt* stmt_ = nullptr;
  sqlite3* db_ = nullptr;
  std::optional<Error> bind_error_;
};

struct OpenOptions {
  bool create = true;
  bool read_only = false;
  int busy_timeout_ms = 30000;  // Python sqlite3.connect(timeout=30)
};

class Connection {
 public:
  Connection() = default;
  ~Connection();
  Connection(Connection&& other) noexcept;
  Connection& operator=(Connection&& other) noexcept;
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  static Result<Connection> open(const std::filesystem::path& path, const OpenOptions& opts = {});
  static Result<Connection> open_memory();

  bool is_open() const noexcept { return db_ != nullptr; }
  void close() noexcept;

  // Executes one or more statements (sqlite3_exec).
  Status exec(std::string_view sql);
  Result<Stmt> prepare(std::string_view sql);

  // Convenience: prepare + bind + run.
  template <class... Args>
  Status run(std::string_view sql, const Args&... args) {
    LOOM_TRY_ASSIGN(Stmt st, prepare(sql));
    st.bind_all(args...);
    return st.run();
  }
  // First column of the first row as text/int (nullopt when no row / NULL).
  template <class... Args>
  Result<std::optional<std::string>> query_text(std::string_view sql, const Args&... args) {
    LOOM_TRY_ASSIGN(Stmt st, prepare(sql));
    st.bind_all(args...);
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row || st.is_null(0)) return std::optional<std::string>{};
    return std::optional<std::string>(st.get_text(0));
  }
  template <class... Args>
  Result<std::optional<std::int64_t>> query_int(std::string_view sql, const Args&... args) {
    LOOM_TRY_ASSIGN(Stmt st, prepare(sql));
    st.bind_all(args...);
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row || st.is_null(0)) return std::optional<std::int64_t>{};
    return std::optional<std::int64_t>(st.get_int(0));
  }

  std::int64_t last_insert_rowid() const noexcept;
  int changes() const noexcept;
  bool in_transaction() const noexcept;  // !sqlite3_get_autocommit

  // Schema introspection (Python Database._migrate helpers).
  bool has_table(std::string_view name, std::string_view schema = "main");
  bool has_column(std::string_view table, std::string_view column, std::string_view schema = "main");
  std::vector<std::string> columns(std::string_view table, std::string_view schema = "main");

  // True if the linked SQLite can create FTS5 tables.
  bool has_fts5();

  std::string last_error() const;
  sqlite3* handle() const noexcept { return db_; }

 private:
  explicit Connection(sqlite3* db) : db_(db) {}
  sqlite3* db_ = nullptr;
};

// RAII transaction. Top-level: BEGIN IMMEDIATE (avoids the WAL read->write
// upgrade deadlock); nested: SAVEPOINT. Rolls back unless commit() succeeded.
class Txn {
 public:
  // immediate=false uses a DEFERRED transaction (read-mostly work that only
  // writes to an attached database, e.g. the FTS index).
  explicit Txn(Connection& c, bool immediate = true);
  ~Txn();
  Txn(const Txn&) = delete;
  Txn& operator=(const Txn&) = delete;

  const Status& begin_status() const noexcept { return begin_; }
  Status commit();
  void rollback() noexcept;

 private:
  Connection& c_;
  Status begin_;
  bool nested_ = false;
  bool done_ = false;
  std::string savepoint_;
};

// Human-readable SQLite library version ("3.47.2").
std::string library_version();

}  // namespace loom::sql
