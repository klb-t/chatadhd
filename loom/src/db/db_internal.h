// Internal helpers shared by db.cpp and fts.cpp (not part of the public API).
#pragma once

#include <map>
#include <set>
#include <optional>
#include <string>
#include <string_view>

#include "loom/db.h"
#include "loom/sqlite.h"

namespace loom {

struct Database::FtsState {
  bool available = false;
  std::filesystem::path path;
  std::set<std::string> dirty_ids;
  bool scan_new = false;
  bool scan_deleted = false;
  bool scan_changed = true;  // first use after open: full reconcile
  std::int64_t last_data_version = -1;
  std::int64_t rebuilds = 0;
};

}  // namespace loom

namespace loom::dbi {

// Column-name based row access so legacy tables with a different column order
// (ALTER TABLE ADD COLUMN appends) or missing optional columns read correctly,
// mirroring Python's `dict(sqlite3.Row)`.
class RowMap {
 public:
  explicit RowMap(const sql::Stmt& st);
  int col(std::string_view name) const;
  std::string text(const sql::Stmt& st, std::string_view name, std::string_view fallback = "") const;
  std::optional<std::string> opt_text(const sql::Stmt& st, std::string_view name) const;
  double real(const sql::Stmt& st, std::string_view name, double fallback) const;
  std::int64_t integer(const sql::Stmt& st, std::string_view name, std::int64_t fallback) const;

 private:
  std::map<std::string, int, std::less<>> idx_;
};

Message read_message(const sql::Stmt& st, const RowMap& m);
Conversation read_conv(const sql::Stmt& st, const RowMap& m);
Node read_node(const sql::Stmt& st, const RowMap& m);
Link read_link(const sql::Stmt& st, const RowMap& m);

}  // namespace loom::dbi
