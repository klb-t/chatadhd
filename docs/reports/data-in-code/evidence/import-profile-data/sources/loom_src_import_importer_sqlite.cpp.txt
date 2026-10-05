// SQLite: Claude.ai layout (conversations+messages), ChatGPT layout
// (conversation/message tables — Python leaves this as a documented no-op
// stub), and a generic fallback that imports every table with role +
// (content|text) columns. Matches engine/importer.py's import_sqlite().
#include <algorithm>
#include <set>

#include "importer_internal.h"
#include "loom/importer.h"
#include "loom/log.h"
#include "loom/sqlite.h"
#include "loom/util/time.h"

namespace loom {

namespace fs = std::filesystem;

namespace {
constexpr std::string_view kLog = "loom.import";

std::vector<std::string> list_tables(sql::Connection& conn) {
  std::vector<std::string> out;
  auto st = conn.prepare("SELECT name FROM sqlite_master WHERE type='table'");
  if (!st) return out;
  while (true) {
    auto row = st->step();
    if (!row || !*row) break;
    out.push_back(st->get_text(0));
  }
  return out;
}

std::vector<std::string> table_columns(sql::Connection& conn, const std::string& table) {
  std::vector<std::string> out;
  auto st = conn.prepare("PRAGMA table_info(\"" + table + "\")");
  if (!st) return out;
  while (true) {
    auto row = st->step();
    if (!row || !*row) break;
    out.push_back(st->get_text(1));  // column 1 = name
  }
  return out;
}

bool has_col(const std::vector<std::string>& cols, std::string_view name) {
  return std::find(cols.begin(), cols.end(), name) != cols.end();
}

// Python: `'user' if msg['role'] in ('user', 'human') else 'assistant'`.
// Deliberately does not skip a 'system' role, unlike normalize_message()
// (the JSON/JSONL paths) — this is Python's own inconsistency and is kept
// for parity: a SQLite row with role='system' becomes 'assistant'.
std::string map_role(const std::string& raw) { return (raw == "user" || raw == "human") ? "user" : "assistant"; }

}  // namespace

Result<std::vector<Conversation>> ConversationImporter::sqlite_body(const fs::path& path, const ImportOptions& opts) {
  sql::OpenOptions oo;
  oo.create = false;
  oo.read_only = true;
  LOOM_TRY_ASSIGN(sql::Connection conn, sql::Connection::open(path, oo));

  auto tables = list_tables(conn);
  std::set<std::string> table_set(tables.begin(), tables.end());
  std::vector<Conversation> results;

  if (table_set.count("conversations") && table_set.count("messages")) {
    LOOM_TRY_ASSIGN(auto st, conn.prepare("SELECT id, title, created_at FROM conversations ORDER BY created_at DESC"));
    while (true) {
      if (cancelled(opts)) break;
      LOOM_TRY_ASSIGN(bool has_row, st.step());
      if (!has_row) break;
      std::string conv_id = st.get_text(0);
      std::string title_col = st.is_null(1) ? "" : st.get_text(1);
      std::string conv_title;
      if (opts.title && !opts.title->empty()) {
        conv_title = *opts.title;
      } else if (!title_col.empty()) {
        conv_title = title_col;
      } else {
        conv_title = "Imported " + timeutil::local_now_format("%Y-%m-%d");
      }

      LOOM_TRY_ASSIGN(auto mst, conn.prepare("SELECT role, content, created_at FROM messages "
                                             "WHERE conversation_id = ? ORDER BY created_at"));
      mst.bind(1, conv_id);
      std::vector<BatchMessage> batch;
      while (true) {
        LOOM_TRY_ASSIGN(bool mrow, mst.step());
        if (!mrow) break;
        std::string role_raw = mst.is_null(0) ? "" : mst.get_text(0);
        BatchMessage bm;
        bm.role = map_role(role_raw);
        bm.text = mst.is_null(1) ? "" : mst.get_text(1);
        batch.push_back(std::move(bm));
      }
      LOOM_TRY_ASSIGN(Conversation c, finish_import(conv_title, std::move(batch), "sqlite.claude"));
      results.push_back(std::move(c));
      if (opts.progress) opts.progress(static_cast<std::int64_t>(results.size()), -1, "sqlite");
    }
    return results;
  }

  if (table_set.count("conversation") || table_set.count("message")) {
    // Python's _import_chatgpt_db is a documented no-op stub ("Similar
    // structure, adapt as needed" — never implemented); kept identical.
    log::info(kLog, "sqlite: ChatGPT-shaped schema detected in {}, no-op (matches Python)", path.string());
    return results;
  }

  for (const auto& table : tables) {
    if (cancelled(opts)) break;
    auto cols = table_columns(conn, table);
    if (!has_col(cols, "role")) continue;
    bool has_content = has_col(cols, "content");
    if (!has_content && !has_col(cols, "text")) continue;
    std::string content_col = has_content ? "content" : "text";

    std::string conv_title = (opts.title && !opts.title->empty()) ? *opts.title : ("Import from " + table);
    LOOM_TRY_ASSIGN(auto st, conn.prepare("SELECT role, \"" + content_col + "\" FROM \"" + table + "\""));
    std::vector<BatchMessage> batch;
    while (true) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      BatchMessage bm;
      bm.role = map_role(st.is_null(0) ? "" : st.get_text(0));
      bm.text = st.is_null(1) ? "" : st.get_text(1);
      batch.push_back(std::move(bm));
    }
    LOOM_TRY_ASSIGN(Conversation c, finish_import(conv_title, std::move(batch), "sqlite.generic"));
    results.push_back(std::move(c));
  }
  return results;
}

Result<std::vector<Conversation>> ConversationImporter::import_sqlite(const fs::path& path, const ImportOptions& opts) {
  return with_source(path, "sqlite", opts, "file", [&](const fs::path& input_path) { return sqlite_body(input_path, opts); });
}

}  // namespace loom
