// kb.h: Loom-only tables of the paradigm wave (loom_kb_*, loom_cat_*).
// Created lazily (first write of an engine), idempotent and forward-only.
// Contract: docs/architecture/LOOM_PARADIGM_ENGINE.md §5. Core tables are
// never touched; derived rows are keyed by run_id so re-runs never collide,
// and loom_kb_judgements is the only append-only, wall-clock table.
#include <string>

#include "loom/db.h"
#include "loom/kb.h"
#include "loom/sqlite.h"

namespace loom::kb {

namespace {
constexpr std::string_view kDdl = R"SQL(
CREATE TABLE IF NOT EXISTS loom_kb_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS loom_kb_runs (
    run_id         TEXT PRIMARY KEY,
    archive_run_id TEXT,
    pack_hash      TEXT NOT NULL,
    status         TEXT NOT NULL DEFAULT 'done',
    summary        TEXT NOT NULL DEFAULT '{}',
    created        TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS loom_kb_entities (
    run_id        TEXT NOT NULL,
    id            TEXT NOT NULL,
    kind          TEXT NOT NULL,
    canonical_key TEXT NOT NULL,
    label         TEXT NOT NULL,
    labels        TEXT NOT NULL DEFAULT '{}',
    status        TEXT,
    status_conf   REAL,
    parent_id     TEXT,
    level         INTEGER NOT NULL DEFAULT 2,
    first_seen    TEXT,
    last_seen     TEXT,
    importance    REAL NOT NULL DEFAULT 0,
    evidence      TEXT NOT NULL DEFAULT 'observed',
    confidence    REAL NOT NULL DEFAULT 1.0,
    node_id       TEXT,
    attrs         TEXT NOT NULL DEFAULT '{}',
    PRIMARY KEY (run_id, id)
);
CREATE INDEX IF NOT EXISTS idx_loom_kb_entities_kind ON loom_kb_entities(run_id, kind);
CREATE INDEX IF NOT EXISTS idx_loom_kb_entities_parent ON loom_kb_entities(run_id, parent_id);
CREATE TABLE IF NOT EXISTS loom_kb_aliases (
    run_id     TEXT NOT NULL,
    entity_id  TEXT NOT NULL,
    alias_key  TEXT NOT NULL,
    surface    TEXT NOT NULL,
    lang       TEXT,
    origin     TEXT NOT NULL DEFAULT 'mined',
    count      INTEGER NOT NULL DEFAULT 0,
    confidence REAL NOT NULL DEFAULT 1.0,
    PRIMARY KEY (run_id, entity_id, alias_key)
);
CREATE INDEX IF NOT EXISTS idx_loom_kb_aliases_key ON loom_kb_aliases(run_id, alias_key);
CREATE TABLE IF NOT EXISTS loom_kb_mentions (
    run_id     TEXT NOT NULL,
    entity_id  TEXT NOT NULL,
    doc_key    TEXT NOT NULL,
    unit       TEXT,
    span_start INTEGER NOT NULL DEFAULT 0,
    span_len   INTEGER NOT NULL DEFAULT 0,
    surface    TEXT,
    extractor  TEXT,
    confidence REAL NOT NULL DEFAULT 1.0
);
CREATE INDEX IF NOT EXISTS idx_loom_kb_mentions_entity ON loom_kb_mentions(run_id, entity_id);
CREATE TABLE IF NOT EXISTS loom_kb_facts (
    run_id        TEXT NOT NULL,
    id            TEXT NOT NULL,
    src           TEXT NOT NULL,
    rel           TEXT NOT NULL,
    dst           TEXT,
    value         TEXT,
    qualifier     TEXT NOT NULL DEFAULT '{}',
    valid_from    TEXT,
    valid_to      TEXT,
    evidence      TEXT NOT NULL DEFAULT 'observed',
    raw_score     REAL NOT NULL DEFAULT 0,
    confidence    REAL NOT NULL DEFAULT 0,
    support       TEXT NOT NULL DEFAULT '[]',
    counter       TEXT NOT NULL DEFAULT '[]',
    basis         TEXT NOT NULL DEFAULT '{}',
    status        TEXT NOT NULL DEFAULT 'active',
    superseded_by TEXT,
    PRIMARY KEY (run_id, id)
);
CREATE INDEX IF NOT EXISTS idx_loom_kb_facts_src ON loom_kb_facts(run_id, src, rel);
CREATE INDEX IF NOT EXISTS idx_loom_kb_facts_dst ON loom_kb_facts(run_id, dst, rel);
CREATE TABLE IF NOT EXISTS loom_kb_instances (
    run_id           TEXT NOT NULL,
    id               TEXT NOT NULL,
    paradigm         TEXT NOT NULL,
    paradigm_version INTEGER NOT NULL DEFAULT 1,
    subject          TEXT NOT NULL,
    subject_label    TEXT NOT NULL DEFAULT '',
    score            REAL NOT NULL DEFAULT 0,
    coverage         TEXT NOT NULL DEFAULT '{}',
    metadata         TEXT NOT NULL DEFAULT '{}',
    PRIMARY KEY (run_id, id)
);
CREATE INDEX IF NOT EXISTS idx_loom_kb_instances_subject ON loom_kb_instances(run_id, subject);
CREATE TABLE IF NOT EXISTS loom_kb_slot_values (
    run_id            TEXT NOT NULL,
    instance_id       TEXT NOT NULL,
    slot              TEXT NOT NULL,
    ord               INTEGER NOT NULL DEFAULT 0,
    value             TEXT,
    evidence          TEXT NOT NULL,
    confidence        REAL NOT NULL DEFAULT 0,
    expected_property TEXT,
    check_state       TEXT NOT NULL DEFAULT 'n/a',
    conflict          INTEGER NOT NULL DEFAULT 0,
    basis             TEXT NOT NULL DEFAULT '{}',
    support           TEXT NOT NULL DEFAULT '[]',
    semantic_key      TEXT NOT NULL,
    PRIMARY KEY (run_id, instance_id, slot, ord)
);
CREATE INDEX IF NOT EXISTS idx_loom_kb_slot_values_key ON loom_kb_slot_values(semantic_key);
CREATE TABLE IF NOT EXISTS loom_kb_judgements (
    id           TEXT PRIMARY KEY,
    semantic_key TEXT NOT NULL,
    target_kind  TEXT NOT NULL,
    verdict      TEXT NOT NULL,
    payload      TEXT NOT NULL DEFAULT '{}',
    author       TEXT NOT NULL DEFAULT 'user',
    run_id       TEXT,
    created      TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_loom_kb_judgements_key ON loom_kb_judgements(semantic_key);
CREATE TABLE IF NOT EXISTS loom_kb_candidates (
    id      TEXT PRIMARY KEY,
    kind    TEXT NOT NULL,
    payload TEXT NOT NULL DEFAULT '{}',
    support TEXT NOT NULL DEFAULT '[]',
    eval    TEXT NOT NULL DEFAULT '{}',
    status  TEXT NOT NULL DEFAULT 'candidate',
    created TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS loom_kb_policy_versions (
    pack    TEXT NOT NULL,
    version INTEGER NOT NULL,
    hash    TEXT NOT NULL,
    status  TEXT NOT NULL DEFAULT 'active',
    metrics TEXT NOT NULL DEFAULT '{}',
    created TEXT NOT NULL,
    PRIMARY KEY (pack, version)
);
CREATE TABLE IF NOT EXISTS loom_kb_llm_cache (
    prompt_hash TEXT NOT NULL,
    model       TEXT NOT NULL,
    response    TEXT NOT NULL,
    created     TEXT NOT NULL,
    PRIMARY KEY (prompt_hash, model)
);
CREATE TABLE IF NOT EXISTS loom_cat_profiles (
    id         TEXT PRIMARY KEY,
    blob_hash  TEXT NOT NULL,
    input_hash TEXT NOT NULL,
    created    TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS loom_cat_score_runs (
    id           TEXT PRIMARY KEY,
    profile_id   TEXT NOT NULL,
    catalog_hash TEXT NOT NULL,
    pack_hash    TEXT NOT NULL,
    task_id      TEXT,
    stats        TEXT NOT NULL DEFAULT '{}',
    created      TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS loom_cat_decisions (
    run_id     TEXT NOT NULL,
    unit_id    TEXT NOT NULL,
    platform   TEXT NOT NULL,
    ext_id     TEXT NOT NULL,
    file       TEXT NOT NULL DEFAULT '',
    title      TEXT NOT NULL DEFAULT '',
    score      REAL NOT NULL DEFAULT 0,
    label      TEXT NOT NULL,
    selected   INTEGER NOT NULL DEFAULT 0,
    projects   TEXT NOT NULL DEFAULT '[]',
    features   TEXT NOT NULL DEFAULT '{}',
    reasons    TEXT NOT NULL DEFAULT '[]',
    decided_by TEXT NOT NULL DEFAULT 'score',
    PRIMARY KEY (run_id, unit_id)
);
CREATE INDEX IF NOT EXISTS idx_loom_cat_decisions_ext ON loom_cat_decisions(platform, ext_id);
CREATE TABLE IF NOT EXISTS loom_cat_imports (
    unit_id   TEXT PRIMARY KEY,
    conv_id   TEXT,
    raw_blob  TEXT,
    source_id TEXT,
    task_id   TEXT,
    created   TEXT NOT NULL
);
)SQL";
}  // namespace

Status ensure_schema(Database& db) {
  auto lk = db.lock();
  sql::Connection& c = db.conn();
  if (c.has_column("loom_kb_meta", "value")) {
    auto v = c.query_text("SELECT value FROM loom_kb_meta WHERE key = 'schema_version'");
    if (v && *v && **v == std::to_string(kKbSchemaVersion)) return {};
  }
  sql::Txn txn(c);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(c.exec(kDdl));
  // Future loom_kb_/loom_cat_ columns go here, guarded:
  //   if (!c.has_column("loom_kb_facts", "x")) ALTER TABLE ...
  LOOM_TRY(c.exec("INSERT OR REPLACE INTO loom_kb_meta (key, value) VALUES ('schema_version', '" +
                  std::to_string(kKbSchemaVersion) + "')"));
  return txn.commit();
}

}  // namespace loom::kb
