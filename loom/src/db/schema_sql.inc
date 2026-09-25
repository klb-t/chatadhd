// GENERATED from engine/db.py Database._init_schema() - keep byte-identical.
// Regenerate: see tests/compat/test_db_compat.py::test_schema_text_matches_python.
static constexpr const char* kInitSchemaSql = R"SQL(
                CREATE TABLE IF NOT EXISTS _meta (
                    key   TEXT PRIMARY KEY,
                    value TEXT
                );

                CREATE TABLE IF NOT EXISTS conversations (
                    id       TEXT PRIMARY KEY,
                    title    TEXT NOT NULL,
                    created  TEXT NOT NULL,
                    updated  TEXT NOT NULL,
                    source   TEXT NOT NULL DEFAULT 'user',
                    metadata TEXT NOT NULL DEFAULT '{}'
                );

                CREATE TABLE IF NOT EXISTS messages (
                    id               TEXT PRIMARY KEY,
                    conv_id          TEXT NOT NULL,
                    parent_id        TEXT,
                    role             TEXT NOT NULL,
                    text             TEXT NOT NULL,
                    model            TEXT,
                    status           TEXT NOT NULL DEFAULT 'active',
                    version_group_id TEXT,
                    version_num      INTEGER NOT NULL DEFAULT 1,
                    weight           REAL NOT NULL DEFAULT 1.0,
                    attachments      TEXT NOT NULL DEFAULT '[]',
                    metadata         TEXT NOT NULL DEFAULT '{}',
                    created          TEXT NOT NULL,
                    semantic_status  TEXT NOT NULL DEFAULT 'pending',
                    FOREIGN KEY (conv_id) REFERENCES conversations(id) ON DELETE CASCADE
                );

                -- Graph nodes
                CREATE TABLE IF NOT EXISTS nodes (
                    id        TEXT PRIMARY KEY,
                    kind      TEXT NOT NULL DEFAULT 'entity',
                    label     TEXT NOT NULL,
                    content   TEXT NOT NULL DEFAULT '',
                    tags      TEXT NOT NULL DEFAULT '[]',
                    metadata  TEXT NOT NULL DEFAULT '{}',
                    created   TEXT NOT NULL
                );

                -- Graph edges
                CREATE TABLE IF NOT EXISTS links (
                    id        TEXT PRIMARY KEY,
                    src       TEXT NOT NULL,
                    dst       TEXT NOT NULL,
                    link_type TEXT NOT NULL DEFAULT 'related',
                    weight    REAL NOT NULL DEFAULT 1.0,
                    metadata  TEXT NOT NULL DEFAULT '{}',
                    created   TEXT NOT NULL
                );
            )SQL";
