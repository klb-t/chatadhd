// catalog.h: Catalog::ensure_schema — the loom_cat_* tables (idempotent,
// forward-only, guarded like the core migrations and loom_kb_*; never
// touches the core schema v4 or loom_kb_* tables, I10).
//
// Two tables beyond the six the header documents are catalog-internal
// bookkeeping (still loom_cat_*, still owned here, still safe to drop and
// rebuild from a re-scan):
//   loom_cat_sources    (id PK "sha256:<hex>", path, bytes, kind, scanned_at)
//                       maps a content-addressed source id back to the
//                       filesystem path scan() read it from, so read_unit()
//                       and import_selected() can re-open it by locator
//                       without keeping the whole catalog run's file list in
//                       memory.
//   loom_cat_checkpoint (source_id PK, member, element_ordinal, byte_offset,
//                       updated)   resumable scan progress per source.
#include "loom/catalog.h"

#include <string>
#include <vector>

#include "loom/db.h"
#include "loom/sqlite.h"

namespace loom::catalog {

namespace {
struct Table {
  const char* name;
  std::string create;
  std::vector<const char*> indexes;
};

const std::vector<Table>& tables() {
  static const std::vector<Table> kTables = [] {
    std::vector<Table> t;
    t.push_back({"loom_cat_units",
                "CREATE TABLE IF NOT EXISTS loom_cat_units (\n"
                "    id TEXT PRIMARY KEY,\n"
                "    source TEXT NOT NULL DEFAULT '',\n"
                "    kind TEXT NOT NULL DEFAULT '',\n"
                "    locator TEXT NOT NULL DEFAULT '{}',\n"
                "    title TEXT NOT NULL DEFAULT '',\n"
                "    date TEXT NOT NULL DEFAULT '',\n"
                "    lang TEXT NOT NULL DEFAULT '',\n"
                "    bytes INTEGER NOT NULL DEFAULT 0,\n"
                "    platform TEXT NOT NULL DEFAULT '',\n"
                "    ext_id TEXT NOT NULL DEFAULT '',\n"
                "    content_hash TEXT NOT NULL DEFAULT '',\n"
                "    prev_version TEXT NOT NULL DEFAULT '',\n"
                "    sketch TEXT NOT NULL DEFAULT '{}',\n"
                "    body TEXT NOT NULL DEFAULT '{}',\n"
                "    created TEXT NOT NULL DEFAULT ''\n"
                ")",
                {"CREATE INDEX IF NOT EXISTS idx_loom_cat_units_hash ON loom_cat_units(content_hash)",
                 "CREATE INDEX IF NOT EXISTS idx_loom_cat_units_extid ON loom_cat_units(platform, ext_id)",
                 "CREATE INDEX IF NOT EXISTS idx_loom_cat_units_source ON loom_cat_units(source)"}});
    t.push_back({"loom_cat_profiles",
                "CREATE TABLE IF NOT EXISTS loom_cat_profiles (\n"
                "    id TEXT PRIMARY KEY,\n"
                "    input_hash TEXT NOT NULL DEFAULT '',\n"
                "    body TEXT NOT NULL DEFAULT '{}',\n"
                "    created TEXT NOT NULL DEFAULT ''\n"
                ")",
                {}});
    t.push_back({"loom_cat_scores",
                "CREATE TABLE IF NOT EXISTS loom_cat_scores (\n"
                "    run_id TEXT NOT NULL,\n"
                "    unit_id TEXT NOT NULL,\n"
                "    score REAL NOT NULL DEFAULT 0,\n"
                "    label TEXT NOT NULL DEFAULT 'irrelevant',\n"
                "    features TEXT NOT NULL DEFAULT '{}',\n"
                "    reasons TEXT NOT NULL DEFAULT '[]',\n"
                "    projects TEXT NOT NULL DEFAULT '[]',\n"
                "    trap TEXT NOT NULL DEFAULT '',\n"
                "    PRIMARY KEY (run_id, unit_id)\n"
                ")",
                {"CREATE INDEX IF NOT EXISTS idx_loom_cat_scores_label ON loom_cat_scores(run_id, label)",
                 "CREATE INDEX IF NOT EXISTS idx_loom_cat_scores_score ON loom_cat_scores(run_id, score)"}});
    t.push_back({"loom_cat_overrides",
                "CREATE TABLE IF NOT EXISTS loom_cat_overrides (\n"
                "    unit_id TEXT PRIMARY KEY,\n"
                "    action TEXT NOT NULL DEFAULT 'include',\n"
                "    reason TEXT NOT NULL DEFAULT '',\n"
                "    seq INTEGER NOT NULL DEFAULT 0,\n"
                "    created TEXT NOT NULL DEFAULT ''\n"
                ")",
                {}});
    t.push_back({"loom_cat_decisions",
                "CREATE TABLE IF NOT EXISTS loom_cat_decisions (\n"
                "    run_id TEXT NOT NULL,\n"
                "    unit_id TEXT NOT NULL,\n"
                "    selected INTEGER NOT NULL DEFAULT 0,\n"
                "    decided_by TEXT NOT NULL DEFAULT '',\n"
                "    label TEXT NOT NULL DEFAULT '',\n"
                "    score REAL NOT NULL DEFAULT 0,\n"
                "    reasons TEXT NOT NULL DEFAULT '[]',\n"
                "    PRIMARY KEY (run_id, unit_id)\n"
                ")",
                {"CREATE INDEX IF NOT EXISTS idx_loom_cat_decisions_sel ON loom_cat_decisions(run_id, selected)"}});
    t.push_back({"loom_cat_imports",
                "CREATE TABLE IF NOT EXISTS loom_cat_imports (\n"
                "    unit_id TEXT PRIMARY KEY,\n"
                "    conv_id TEXT NOT NULL DEFAULT '',\n"
                "    raw_blob TEXT NOT NULL DEFAULT '',\n"
                "    source_id TEXT NOT NULL DEFAULT '',\n"
                "    task_id TEXT NOT NULL DEFAULT '',\n"
                "    created TEXT NOT NULL DEFAULT ''\n"
                ")",
                {}});
    t.push_back({"loom_cat_sources",
                "CREATE TABLE IF NOT EXISTS loom_cat_sources (\n"
                "    id TEXT PRIMARY KEY,\n"
                "    path TEXT NOT NULL DEFAULT '',\n"
                "    bytes INTEGER NOT NULL DEFAULT 0,\n"
                "    kind TEXT NOT NULL DEFAULT '',\n"
                "    scanned_at TEXT NOT NULL DEFAULT ''\n"
                ")",
                {}});
    t.push_back({"loom_cat_checkpoint",
                "CREATE TABLE IF NOT EXISTS loom_cat_checkpoint (\n"
                "    source_id TEXT NOT NULL,\n"
                "    member TEXT NOT NULL DEFAULT '',\n"
                "    element_ordinal INTEGER NOT NULL DEFAULT -1,\n"
                "    byte_offset INTEGER NOT NULL DEFAULT 0,\n"
                "    done INTEGER NOT NULL DEFAULT 0,\n"
                "    updated TEXT NOT NULL DEFAULT '',\n"
                "    PRIMARY KEY (source_id, member)\n"
                ")",
                {}});
    return t;
  }();
  return kTables;
}
}  // namespace

Status Catalog::ensure_schema(Database& db) {
  auto lk = db.lock();
  sql::Connection& c = db.conn();
  if (c.has_table("loom_cat_units") && c.has_table("loom_cat_checkpoint")) return {};
  sql::Txn txn(c);
  LOOM_TRY(txn.begin_status());
  for (const auto& t : tables()) {
    LOOM_TRY(c.exec(t.create));
    for (const char* ix : t.indexes) LOOM_TRY(c.exec(ix));
  }
  return txn.commit();
}

}  // namespace loom::catalog
