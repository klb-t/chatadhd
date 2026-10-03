// kb.h / knowledge_store.h: the Loom-only tables of the knowledge layer
// (loom_kb_*). Created lazily (first write), idempotent and forward-only.
// Core tables are never touched. The table list is documented in
// include/loom/knowledge_store.h.
//
// Upgrade of the v1 draft layout (commit d5ded53, never written by any
// engine): a loom_kb_* table that exists without the v2 `body` column is
// dropped and recreated when it is EMPTY (lossless); a non-empty one gets the
// missing columns added. The obsolete v1 tables loom_kb_facts (renamed to
// loom_kb_claims) and loom_kb_mentions (claim support replaces it) are
// dropped when empty and left alone otherwise.
#include <string>
#include <vector>

#include "loom/db.h"
#include "loom/kb.h"
#include "loom/sqlite.h"

namespace loom::kb {

namespace {

struct Column {
  const char* name;
  const char* decl;  // type + constraints; NOT NULL columns always have a DEFAULT
};

struct Table {
  const char* name;
  std::string create;               // CREATE TABLE IF NOT EXISTS ...
  std::vector<Column> columns;      // every column (guarded ADD COLUMN)
  std::vector<const char*> indexes; // CREATE INDEX IF NOT EXISTS ...
  bool has_body = true;             // v2 marker column
};

// Standard shape: (run_id, id) primary key, body, plus indexed columns.
Table run_table(const char* name, std::vector<Column> cols, std::vector<const char*> indexes) {
  std::string ddl = std::string("CREATE TABLE IF NOT EXISTS ") + name + " (\n    run_id TEXT NOT NULL,\n    id TEXT NOT NULL";
  for (const auto& c : cols) ddl += std::string(",\n    ") + c.name + " " + c.decl;
  ddl += ",\n    body TEXT NOT NULL DEFAULT '{}',\n    PRIMARY KEY (run_id, id)\n)";
  std::vector<Column> all = {{"run_id", "TEXT NOT NULL DEFAULT ''"}, {"id", "TEXT NOT NULL DEFAULT ''"}};
  for (auto& c : cols) all.push_back(c);
  all.push_back({"body", "TEXT NOT NULL DEFAULT '{}'"});
  return Table{name, ddl, all, std::move(indexes), true};
}

const std::vector<Table>& tables() {
  static const std::vector<Table> kTables = [] {
    std::vector<Table> t;
    t.push_back(Table{"loom_kb_meta",
                      "CREATE TABLE IF NOT EXISTS loom_kb_meta (\n    key TEXT PRIMARY KEY,\n    value TEXT NOT NULL\n)",
                      {{"key", "TEXT"}, {"value", "TEXT NOT NULL DEFAULT ''"}},
                      {},
                      false});
    t.push_back(Table{"loom_kb_runs",
                      "CREATE TABLE IF NOT EXISTS loom_kb_runs (\n    run_id TEXT PRIMARY KEY,\n"
                      "    archive_run_id TEXT NOT NULL DEFAULT '',\n    pack_hash TEXT NOT NULL DEFAULT '',\n"
                      "    status TEXT NOT NULL DEFAULT 'running',\n    inputs TEXT NOT NULL DEFAULT '{}',\n"
                      "    summary TEXT NOT NULL DEFAULT '{}',\n    created TEXT NOT NULL DEFAULT '',\n"
                      "    replayed_seq INTEGER NOT NULL DEFAULT 0\n)",
                      {{"run_id", "TEXT"},
                       {"archive_run_id", "TEXT NOT NULL DEFAULT ''"},
                       {"pack_hash", "TEXT NOT NULL DEFAULT ''"},
                       {"status", "TEXT NOT NULL DEFAULT 'running'"},
                       {"inputs", "TEXT NOT NULL DEFAULT '{}'"},
                       {"summary", "TEXT NOT NULL DEFAULT '{}'"},
                       {"created", "TEXT NOT NULL DEFAULT ''"},
                       {"replayed_seq", "INTEGER NOT NULL DEFAULT 0"}},
                      {},
                      false});
    // N3 receipts are separate from candidates and owner judgements. They
    // preserve a complete exchange packet; accepted rows use existing tables.
    t.push_back(Table{"loom_kb_graph_receipts",
                      "CREATE TABLE IF NOT EXISTS loom_kb_graph_receipts ("
                      "id TEXT PRIMARY KEY, run_id TEXT NOT NULL, body TEXT NOT NULL)",
                      {{"id", "TEXT"}, {"run_id", "TEXT NOT NULL DEFAULT ''"},
                       {"body", "TEXT NOT NULL DEFAULT '{}'"}},
                      {"CREATE INDEX IF NOT EXISTS idx_loom_kb_graph_receipts_run ON loom_kb_graph_receipts(run_id)"},
                      true});
    t.push_back(run_table("loom_kb_observations",
                          {{"unit", "TEXT NOT NULL DEFAULT ''"}, {"kind", "TEXT NOT NULL DEFAULT ''"},
                           {"date", "TEXT NOT NULL DEFAULT ''"}},
                          {"CREATE INDEX IF NOT EXISTS idx_loom_kb_obs_unit ON loom_kb_observations(run_id, unit)"}));
    t.push_back(run_table(
        "loom_kb_entities",
        {{"kind", "TEXT NOT NULL DEFAULT ''"}, {"canonical_key", "TEXT NOT NULL DEFAULT ''"},
         {"parent", "TEXT NOT NULL DEFAULT ''"}, {"status", "TEXT NOT NULL DEFAULT 'active'"}},
        {"CREATE INDEX IF NOT EXISTS idx_loom_kb_ent_kind ON loom_kb_entities(run_id, kind)",
         "CREATE INDEX IF NOT EXISTS idx_loom_kb_ent_key ON loom_kb_entities(run_id, canonical_key)",
         "CREATE INDEX IF NOT EXISTS idx_loom_kb_ent_parent ON loom_kb_entities(run_id, parent)"}));
    t.push_back(Table{"loom_kb_aliases",
                      "CREATE TABLE IF NOT EXISTS loom_kb_aliases (\n    run_id TEXT NOT NULL,\n"
                      "    entity_id TEXT NOT NULL,\n    alias_key TEXT NOT NULL,\n"
                      "    surface TEXT NOT NULL DEFAULT '',\n    lang TEXT NOT NULL DEFAULT '',\n"
                      "    method TEXT NOT NULL DEFAULT 'mined',\n    count INTEGER NOT NULL DEFAULT 0,\n"
                      "    confidence REAL NOT NULL DEFAULT 1.0,\n    PRIMARY KEY (run_id, entity_id, alias_key)\n)",
                      {{"run_id", "TEXT NOT NULL DEFAULT ''"},
                       {"entity_id", "TEXT NOT NULL DEFAULT ''"},
                       {"alias_key", "TEXT NOT NULL DEFAULT ''"},
                       {"surface", "TEXT NOT NULL DEFAULT ''"},
                       {"lang", "TEXT NOT NULL DEFAULT ''"},
                       {"method", "TEXT NOT NULL DEFAULT 'mined'"},
                       {"count", "INTEGER NOT NULL DEFAULT 0"},
                       {"confidence", "REAL NOT NULL DEFAULT 1.0"}},
                      {"CREATE INDEX IF NOT EXISTS idx_loom_kb_alias_key ON loom_kb_aliases(run_id, alias_key)"},
                      false});
    t.push_back(run_table(
        "loom_kb_claims",
        {{"subject", "TEXT NOT NULL DEFAULT ''"}, {"predicate", "TEXT NOT NULL DEFAULT ''"},
         {"object", "TEXT NOT NULL DEFAULT ''"}, {"evidence", "TEXT NOT NULL DEFAULT 'observed'"},
         {"origin", "TEXT NOT NULL DEFAULT 'archive'"}, {"status", "TEXT NOT NULL DEFAULT 'active'"},
         {"confidence", "REAL NOT NULL DEFAULT 0"}, {"branch", "TEXT NOT NULL DEFAULT ''"},
         {"version", "TEXT NOT NULL DEFAULT ''"}},
        {"CREATE INDEX IF NOT EXISTS idx_loom_kb_cl_subj ON loom_kb_claims(run_id, subject, predicate)",
         "CREATE INDEX IF NOT EXISTS idx_loom_kb_cl_obj ON loom_kb_claims(run_id, object, predicate)",
         "CREATE INDEX IF NOT EXISTS idx_loom_kb_cl_pred ON loom_kb_claims(run_id, predicate)",
         "CREATE INDEX IF NOT EXISTS idx_loom_kb_cl_ev ON loom_kb_claims(run_id, evidence)"}));
    t.push_back(Table{"loom_kb_claim_support",
                      "CREATE TABLE IF NOT EXISTS loom_kb_claim_support (\n    run_id TEXT NOT NULL,\n"
                      "    claim_id TEXT NOT NULL,\n    observation_id TEXT NOT NULL,\n"
                      "    PRIMARY KEY (run_id, claim_id, observation_id)\n)",
                      {{"run_id", "TEXT NOT NULL DEFAULT ''"},
                       {"claim_id", "TEXT NOT NULL DEFAULT ''"},
                       {"observation_id", "TEXT NOT NULL DEFAULT ''"}},
                      {"CREATE INDEX IF NOT EXISTS idx_loom_kb_sup_obs ON loom_kb_claim_support(run_id, observation_id)"},
                      false});
    t.push_back(run_table("loom_kb_principles",
                          {{"level", "TEXT NOT NULL DEFAULT ''"}, {"form", "TEXT NOT NULL DEFAULT ''"},
                           {"validation", "TEXT NOT NULL DEFAULT 'candidate'"}, {"owner", "TEXT NOT NULL DEFAULT ''"}},
                          {}));
    t.push_back(run_table("loom_kb_operators",
                          {{"produces", "TEXT NOT NULL DEFAULT ''"}, {"validation", "TEXT NOT NULL DEFAULT 'candidate'"}},
                          {}));
    t.push_back(run_table("loom_kb_morphisms",
                          {{"use", "TEXT NOT NULL DEFAULT ''"}, {"validation", "TEXT NOT NULL DEFAULT 'candidate'"}}, {}));
    t.push_back(run_table(
        "loom_kb_instances",
        {{"paradigm_kind", "TEXT NOT NULL DEFAULT ''"}, {"paradigm", "TEXT NOT NULL DEFAULT ''"},
         {"subject", "TEXT NOT NULL DEFAULT ''"}, {"model", "TEXT NOT NULL DEFAULT ''"}},
        {"CREATE INDEX IF NOT EXISTS idx_loom_kb_in_par ON loom_kb_instances(run_id, paradigm)",
         "CREATE INDEX IF NOT EXISTS idx_loom_kb_in_subj ON loom_kb_instances(run_id, subject)"}));
    t.push_back(Table{"loom_kb_slot_values",
                      "CREATE TABLE IF NOT EXISTS loom_kb_slot_values (\n    run_id TEXT NOT NULL,\n"
                      "    instance_id TEXT NOT NULL,\n    slot TEXT NOT NULL,\n    ord INTEGER NOT NULL DEFAULT 0,\n"
                      "    claim_id TEXT NOT NULL DEFAULT '',\n    role TEXT NOT NULL DEFAULT '',\n"
                      "    conflict INTEGER NOT NULL DEFAULT 0,\n    body TEXT NOT NULL DEFAULT '{}',\n"
                      "    PRIMARY KEY (run_id, instance_id, slot, ord)\n)",
                      {{"run_id", "TEXT NOT NULL DEFAULT ''"},
                       {"instance_id", "TEXT NOT NULL DEFAULT ''"},
                       {"slot", "TEXT NOT NULL DEFAULT ''"},
                       {"ord", "INTEGER NOT NULL DEFAULT 0"},
                       {"claim_id", "TEXT NOT NULL DEFAULT ''"},
                       {"role", "TEXT NOT NULL DEFAULT ''"},
                       {"conflict", "INTEGER NOT NULL DEFAULT 0"},
                       {"body", "TEXT NOT NULL DEFAULT '{}'"}},
                      {"CREATE INDEX IF NOT EXISTS idx_loom_kb_sv_role ON loom_kb_slot_values(run_id, role)",
                       "CREATE INDEX IF NOT EXISTS idx_loom_kb_sv_claim ON loom_kb_slot_values(run_id, claim_id)"},
                      true});
    t.push_back(run_table("loom_kb_areas", {{"subject", "TEXT NOT NULL DEFAULT ''"}}, {}));
    t.push_back(run_table("loom_kb_decisions",
                          {{"subject", "TEXT NOT NULL DEFAULT ''"}, {"status", "TEXT NOT NULL DEFAULT 'active'"},
                           {"date", "TEXT NOT NULL DEFAULT ''"}},
                          {}));
    t.push_back(run_table("loom_kb_forks",
                          {{"kind", "TEXT NOT NULL DEFAULT ''"}, {"subject", "TEXT NOT NULL DEFAULT ''"}}, {}));
    t.push_back(run_table("loom_kb_status_records",
                          {{"entity", "TEXT NOT NULL DEFAULT ''"}, {"branch", "TEXT NOT NULL DEFAULT ''"},
                           {"version", "TEXT NOT NULL DEFAULT ''"}, {"status", "TEXT NOT NULL DEFAULT ''"}},
                          {"CREATE INDEX IF NOT EXISTS idx_loom_kb_sr_ent ON loom_kb_status_records(run_id, entity)"}));
    t.push_back(run_table("loom_kb_predictions",
                          {{"operator", "TEXT NOT NULL DEFAULT ''"}, {"cut", "TEXT NOT NULL DEFAULT ''"},
                           {"outcome", "TEXT NOT NULL DEFAULT 'pending'"}},
                          {}));
    t.push_back(run_table("loom_kb_models",
                          {{"name", "TEXT NOT NULL DEFAULT ''"}, {"validation", "TEXT NOT NULL DEFAULT 'candidate'"}}, {}));
    t.push_back(run_table("loom_kb_products",
                          {{"kind", "TEXT NOT NULL DEFAULT ''"}, {"instance", "TEXT NOT NULL DEFAULT ''"}}, {}));
    t.push_back(Table{"loom_kb_judgements",
                      "CREATE TABLE IF NOT EXISTS loom_kb_judgements (\n    id TEXT PRIMARY KEY,\n"
                      "    seq INTEGER NOT NULL DEFAULT 0,\n    target_kind TEXT NOT NULL DEFAULT '',\n"
                      "    target TEXT NOT NULL DEFAULT '',\n    verdict TEXT NOT NULL DEFAULT '',\n"
                      "    created TEXT NOT NULL DEFAULT '',\n    body TEXT NOT NULL DEFAULT '{}'\n)",
                      {{"id", "TEXT"},
                       {"seq", "INTEGER NOT NULL DEFAULT 0"},
                       {"target_kind", "TEXT NOT NULL DEFAULT ''"},
                       {"target", "TEXT NOT NULL DEFAULT ''"},
                       {"verdict", "TEXT NOT NULL DEFAULT ''"},
                       {"created", "TEXT NOT NULL DEFAULT ''"},
                       {"body", "TEXT NOT NULL DEFAULT '{}'"}},
                      {"CREATE UNIQUE INDEX IF NOT EXISTS idx_loom_kb_ju_seq ON loom_kb_judgements(seq)",
                       "CREATE INDEX IF NOT EXISTS idx_loom_kb_ju_target ON loom_kb_judgements(target)"},
                      true});
    t.push_back(Table{"loom_kb_candidates",
                      "CREATE TABLE IF NOT EXISTS loom_kb_candidates (\n    id TEXT PRIMARY KEY,\n"
                      "    kind TEXT NOT NULL DEFAULT '',\n    payload TEXT NOT NULL DEFAULT '{}',\n"
                      "    support TEXT NOT NULL DEFAULT '[]',\n    eval TEXT NOT NULL DEFAULT '{}',\n"
                      "    status TEXT NOT NULL DEFAULT 'candidate',\n    created TEXT NOT NULL DEFAULT ''\n)",
                      {{"id", "TEXT"},
                       {"kind", "TEXT NOT NULL DEFAULT ''"},
                       {"payload", "TEXT NOT NULL DEFAULT '{}'"},
                       {"support", "TEXT NOT NULL DEFAULT '[]'"},
                       {"eval", "TEXT NOT NULL DEFAULT '{}'"},
                       {"status", "TEXT NOT NULL DEFAULT 'candidate'"},
                       {"created", "TEXT NOT NULL DEFAULT ''"}},
                      {},
                      false});
    t.push_back(Table{"loom_kb_policy_versions",
                      "CREATE TABLE IF NOT EXISTS loom_kb_policy_versions (\n    pack TEXT NOT NULL,\n"
                      "    version INTEGER NOT NULL,\n    hash TEXT NOT NULL DEFAULT '',\n"
                      "    status TEXT NOT NULL DEFAULT 'active',\n    metrics TEXT NOT NULL DEFAULT '{}',\n"
                      "    created TEXT NOT NULL DEFAULT '',\n    PRIMARY KEY (pack, version)\n)",
                      {{"pack", "TEXT NOT NULL DEFAULT ''"},
                       {"version", "INTEGER NOT NULL DEFAULT 0"},
                       {"hash", "TEXT NOT NULL DEFAULT ''"},
                       {"status", "TEXT NOT NULL DEFAULT 'active'"},
                       {"metrics", "TEXT NOT NULL DEFAULT '{}'"},
                       {"created", "TEXT NOT NULL DEFAULT ''"}},
                      {},
                      false});
    t.push_back(Table{"loom_kb_llm_cache",
                      "CREATE TABLE IF NOT EXISTS loom_kb_llm_cache (\n    prompt_hash TEXT NOT NULL,\n"
                      "    model TEXT NOT NULL,\n    response TEXT NOT NULL DEFAULT '',\n"
                      "    created TEXT NOT NULL DEFAULT '',\n    PRIMARY KEY (prompt_hash, model)\n)",
                      {{"prompt_hash", "TEXT NOT NULL DEFAULT ''"},
                       {"model", "TEXT NOT NULL DEFAULT ''"},
                       {"response", "TEXT NOT NULL DEFAULT ''"},
                       {"created", "TEXT NOT NULL DEFAULT ''"}},
                      {},
                      false});
    return t;
  }();
  return kTables;
}

Result<bool> table_empty(sql::Connection& c, const std::string& table) {
  LOOM_TRY_ASSIGN(auto n, c.query_int("SELECT COUNT(*) FROM " + table));
  return !n || *n == 0;
}

}  // namespace

Status ensure_schema(Database& db) {
  auto lk = db.lock();
  sql::Connection& c = db.conn();
  if (c.has_table("loom_kb_meta")) {
    auto v = c.query_text("SELECT value FROM loom_kb_meta WHERE key = 'schema_version'");
    if (v && *v && **v == std::to_string(kKbSchemaVersion)) return {};
  }
  sql::Txn txn(c);
  LOOM_TRY(txn.begin_status());
  // v1 draft tables that are no longer part of the layout.
  for (const char* old : {"loom_kb_facts", "loom_kb_mentions"}) {
    if (c.has_table(old)) {
      LOOM_TRY_ASSIGN(bool empty, table_empty(c, old));
      if (empty) LOOM_TRY(c.exec(std::string("DROP TABLE ") + old));
    }
  }
  for (const auto& t : tables()) {
    if (t.has_body && c.has_table(t.name) && !c.has_column(t.name, "body")) {
      LOOM_TRY_ASSIGN(bool empty, table_empty(c, t.name));
      if (empty) LOOM_TRY(c.exec(std::string("DROP TABLE ") + t.name));
    }
    LOOM_TRY(c.exec(t.create));
    for (const auto& col : t.columns) {
      if (!c.has_column(t.name, col.name)) {
        LOOM_TRY(c.exec(std::string("ALTER TABLE ") + t.name + " ADD COLUMN " + col.name + " " + col.decl));
      }
    }
    for (const char* ix : t.indexes) LOOM_TRY(c.exec(ix));
  }
  const std::string ver = std::to_string(kKbSchemaVersion);
  LOOM_TRY(c.exec("CREATE TRIGGER IF NOT EXISTS loom_kb_graph_receipts_no_update "
                  "BEFORE UPDATE ON loom_kb_graph_receipts BEGIN "
                  "SELECT RAISE(ABORT, 'graph receipt is immutable'); END"));
  LOOM_TRY(c.exec("CREATE TRIGGER IF NOT EXISTS loom_kb_graph_receipts_no_delete "
                  "BEFORE DELETE ON loom_kb_graph_receipts BEGIN "
                  "SELECT RAISE(ABORT, 'graph receipt is immutable'); END"));
  // SQLite REPLACE can bypass DELETE triggers when recursive_triggers is off.
  LOOM_TRY(c.exec("CREATE TRIGGER IF NOT EXISTS loom_kb_graph_receipts_no_replace "
                  "BEFORE INSERT ON loom_kb_graph_receipts "
                  "WHEN EXISTS (SELECT 1 FROM loom_kb_graph_receipts WHERE id = NEW.id) BEGIN "
                  "SELECT RAISE(ABORT, 'graph receipt is immutable'); END"));
  LOOM_TRY(c.run("INSERT OR REPLACE INTO loom_kb_meta (key, value) VALUES ('schema_version', ?)", ver));
  LOOM_TRY(c.run("INSERT OR REPLACE INTO _meta (key, value) VALUES ('loom_kb_schema_version', ?)", ver));
  return txn.commit();
}

bool has_schema(Database& db) {
  auto lk = db.lock();
  return db.conn().has_table("loom_kb_claims") && db.conn().has_column("loom_kb_claims", "body");
}

}  // namespace loom::kb
