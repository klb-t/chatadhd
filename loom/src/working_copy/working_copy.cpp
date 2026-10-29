// R103 B1b: the working copy as one bundle. See include/loom/working_copy.h for what goes in and what never does.
#include "loom/working_copy.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/memory_engine.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::working_copy {
namespace {

constexpr std::string_view kLog = "loom.working_copy";
constexpr std::size_t kMinGuardLen = 6;  // shorter "secrets" (e.g. "1") would strip ordinary settings

const char* const kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64_encode(std::string_view in) {
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const unsigned v = static_cast<unsigned char>(in[i]) << 16 | static_cast<unsigned char>(in[i + 1]) << 8 | static_cast<unsigned char>(in[i + 2]);
    out += kB64[v >> 18 & 63]; out += kB64[v >> 12 & 63]; out += kB64[v >> 6 & 63]; out += kB64[v & 63];
  }
  if (i < in.size()) {
    unsigned v = static_cast<unsigned char>(in[i]) << 16;
    if (i + 1 < in.size()) v |= static_cast<unsigned char>(in[i + 1]) << 8;
    out += kB64[v >> 18 & 63]; out += kB64[v >> 12 & 63];
    out += i + 1 < in.size() ? kB64[v >> 6 & 63] : '=';
    out += '=';
  }
  return out;
}

std::string b64_decode(std::string_view in) {
  std::string out;
  unsigned v = 0;
  int bits = 0;
  for (char c : in) {
    const char* p = std::strchr(kB64, c);
    if (c == '=' || !p || !c) continue;
    v = v << 6 | static_cast<unsigned>(p - kB64);
    bits += 6;
    if (bits >= 8) { bits -= 8; out += static_cast<char>(v >> bits & 0xFF); }
  }
  return out;
}

// A key names a credential when one of its segments (split on _ . - and camel case) is such a word.
bool secret_named(std::string_view key) {
  std::vector<std::string> seg;
  std::string cur;
  auto flush = [&] { if (!cur.empty()) seg.push_back(cur); cur.clear(); };
  for (std::size_t i = 0; i < key.size(); ++i) {
    const char c = key[i];
    if (c == '_' || c == '.' || c == '-' || c == ' ' || c == '/') { flush(); continue; }
    if (std::isupper(static_cast<unsigned char>(c)) && !cur.empty() && std::islower(static_cast<unsigned char>(cur.back()))) flush();
    cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  flush();
  static const std::set<std::string> words = {"token", "secret", "secrets", "password", "passwd", "apikey", "credential",
                                              "credentials", "cookie", "bearer", "passphrase"};
  for (std::size_t i = 0; i < seg.size(); ++i) {
    if (words.count(seg[i])) return true;
    if (i + 1 < seg.size() && seg[i + 1] == "key" && (seg[i] == "api" || seg[i] == "private" || seg[i] == "access" ||
                                                      seg[i] == "secret" || seg[i] == "batch"))
      return true;
  }
  return false;
}

struct Guard {
  std::vector<std::string> values;  // credential values known to the runtime
  std::set<std::string> names;      // secret / overlay key names
  bool carries(const std::string& text) const {
    for (const auto& v : values)
      if (v.size() >= kMinGuardLen && text.find(v) != std::string::npos) return true;
    return false;
  }
};

Guard make_guard(Runtime& rt) {
  Guard g;
  g.values = rt.secrets().values_for_guard();
  const Json all = rt.secrets().all();
  if (all.is_object())
    for (auto it = all.begin(); it != all.end(); ++it) g.names.insert(it.key());
  for (auto& k : rt.secrets().overlay_keys()) g.names.insert(k);
  return g;
}

// Drops (recursively) members whose key names a credential or whose value carries a known credential value.
void strip(Json& v, const Guard& g, const std::string& path, Json& excluded) {
  if (!v.is_object()) return;
  std::vector<std::string> drop;
  for (auto it = v.begin(); it != v.end(); ++it) {
    const std::string k = it.key();
    const std::string p = path.empty() ? k : path + "." + k;
    if (secret_named(k) || g.names.count(k) || g.carries(json::dump(it.value()))) {
      if (!(it.value().is_object() && !secret_named(k) && !g.names.count(k))) { drop.push_back(k); excluded.push_back(p); continue; }
    }
    strip(it.value(), g, p, excluded);
  }
  for (const auto& k : drop) v.erase(k);
}

bool table_exists(sql::Connection& c, const std::string& t) {
  auto r = c.query_int("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?", t);
  return r && *r && **r > 0;
}

Result<std::vector<std::string>> columns_of(sql::Connection& c, const std::string& t) {
  LOOM_TRY_ASSIGN(sql::Stmt st, c.prepare("SELECT name FROM pragma_table_info(?)"));
  st.bind(1, t);
  std::vector<std::string> cols;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    cols.push_back(st.get_text(0));
  }
  return cols;
}

std::string quote_ident(const std::string& s) {
  std::string o = "\"";
  for (char c : s) { if (c == '"') o += '"'; o += c; }
  return o + "\"";
}

Json cell(const sql::Stmt& st, int i) {
  switch (st.column_type(i)) {
    case sql::Type::Null: return nullptr;
    case sql::Type::Integer: return st.get_int(i);
    case sql::Type::Float: return st.get_double(i);
    case sql::Type::Blob: return Json{{"$blob", b64_encode(st.get_blob(i))}};
    default: return st.get_text(i);
  }
}

void bind_cell(sql::Stmt& st, int i, const Json& v) {
  if (v.is_null()) st.bind_null(i);
  else if (v.is_boolean()) st.bind(i, std::int64_t(v.get<bool>() ? 1 : 0));
  else if (v.is_number_integer()) st.bind(i, v.get<std::int64_t>());
  else if (v.is_number()) st.bind(i, v.get<double>());
  else if (v.is_string()) st.bind(i, v.get_ref<const std::string&>());
  else if (v.is_object() && v.contains("$blob") && v["$blob"].is_string()) st.bind_blob(i, b64_decode(v["$blob"].get<std::string>()));
  else st.bind(i, json::dump(v));
}

std::vector<std::string> string_list(const Json& v, const std::vector<std::string>& fallback) {
  if (!v.is_array()) return fallback;
  std::vector<std::string> out;
  for (const auto& x : v)
    if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

}  // namespace

const std::vector<std::string>& default_tables() {
  static const std::vector<std::string> t = {"conversations", "messages", "nodes", "links", "loom_message_annotations",
                                             "loom_message_annotation_sources", "loom_sources", "loom_provenance",
                                             "loom_relation_types"};
  return t;
}

std::string content_sha256(const Json& content) { return Sha256::hex(json::dump(content)); }

Result<Json> export_bundle(Runtime& rt, const Json& options) {
  const Json opt = options.is_object() ? options : Json::object();
  const std::vector<std::string> tables = string_list(opt.value("tables", Json()), default_tables());
  Json content = Json::object();
  Json tabs = Json::object();
  {
    auto lk = rt.db().lock();
    sql::Connection& c = rt.db().conn();
    for (const auto& t : tables) {
      if (!table_exists(c, t)) continue;
      LOOM_TRY_ASSIGN(auto cols, columns_of(c, t));
      std::string select = "SELECT ";
      for (std::size_t i = 0; i < cols.size(); ++i) select += (i ? ", " : "") + quote_ident(cols[i]);
      select += " FROM " + quote_ident(t);
      auto st = c.prepare(select + " ORDER BY rowid");
      if (!st) st = c.prepare(select);  // WITHOUT ROWID
      if (!st) return st.error();
      Json rows = Json::array();
      while (true) {
        LOOM_TRY_ASSIGN(bool row, st->step());
        if (!row) break;
        Json r = Json::array();
        for (int i = 0; i < int(cols.size()); ++i) r.push_back(cell(*st, i));
        rows.push_back(std::move(r));
      }
      tabs[t] = Json{{"columns", cols}, {"rows", std::move(rows)}};
    }
  }
  content["tables"] = std::move(tabs);
  const Guard g = make_guard(rt);
  Json excluded = Json::array();
  if (opt.value("include_memory", true)) {
    Json mem = Json::array();
    for (const auto& n : rt.memory().get_all()) mem.push_back(n.to_json());
    content["memory"] = std::move(mem);
  }
  if (opt.value("include_config", true)) {
    Json cfg = rt.config().all();
    strip(cfg, g, "", excluded);
    content["config"] = std::move(cfg);
  }
  Json b = Json::object();
  b["schema"] = std::string(kSchema);
  b["saved_at"] = timeutil::utc_now_iso();
  b["producer"] = "loom";
  b["integrity"] = Json{{"algorithm", "sha256"}, {"content_sha256", content_sha256(content)}};
  b["excluded"] = Json{{"config_keys", excluded},
                       {"never_read", Json::array({"secrets.json", "credential_overlay", "session cookies", "other files"})}};
  b["content"] = std::move(content);
  return b;
}

Result<Json> import_bundle(Runtime& rt, const Json& bundle, const Json& options) {
  const Json opt = options.is_object() ? options : Json::object();
  if (!bundle.is_object() || bundle.value("schema", std::string()) != kSchema)
    return Error(Errc::InvalidArgument, "not a " + std::string(kSchema) + " bundle");
  const Json content = bundle.value("content", Json::object());
  if (!content.is_object()) return Error(Errc::InvalidArgument, "bundle has no content");
  if (opt.value("verify", true)) {
    const Json integ = bundle.value("integrity", Json::object());
    const std::string want = integ.is_object() ? integ.value("content_sha256", std::string()) : std::string();
    if (want.empty() || want != content_sha256(content))
      return Error(Errc::InvalidArgument, "bundle integrity check failed (content_sha256 does not match)");
  }
  const std::string mode = opt.value("mode", std::string("merge"));
  if (mode != "merge" && mode != "replace") return Error(Errc::InvalidArgument, "mode must be merge or replace");
  const std::vector<std::string> allowed = string_list(opt.value("tables", Json()), default_tables());
  const std::set<std::string> allow(allowed.begin(), allowed.end());

  Json restored_tables = Json::object();
  const Json tabs = content.value("tables", Json::object());
  {
    auto lk = rt.db().lock();
    sql::Connection& c = rt.db().conn();
    LOOM_TRY(c.exec("BEGIN IMMEDIATE"));
    auto fail = [&](const Error& e) -> Error { (void)c.exec("ROLLBACK"); return e; };
    if (mode == "replace")
      for (const auto& t : allowed)
        if (table_exists(c, t))
          if (auto s = c.exec("DELETE FROM " + quote_ident(t)); !s) return fail(s.error());
    if (tabs.is_object()) {
      for (auto it = tabs.begin(); it != tabs.end(); ++it) {
        const std::string t = it.key();
        if (!allow.count(t) || !table_exists(c, t)) continue;  // only the allow-list is ever written
        const Json& spec = it.value();
        if (!spec.is_object() || !spec.contains("columns") || !spec.contains("rows")) continue;
        auto have = columns_of(c, t);
        if (!have) return fail(have.error());
        const std::set<std::string> have_set(have->begin(), have->end());
        const std::vector<std::string> cols = string_list(spec["columns"], {});
        std::vector<int> idx;
        std::string names, marks;
        for (int i = 0; i < int(cols.size()); ++i) {
          if (!have_set.count(cols[i])) continue;  // a column this schema does not have
          names += (idx.empty() ? "" : ", ") + quote_ident(cols[i]);
          marks += idx.empty() ? "?" : ", ?";
          idx.push_back(i);
        }
        if (idx.empty()) continue;
        auto st = c.prepare("INSERT OR REPLACE INTO " + quote_ident(t) + " (" + names + ") VALUES (" + marks + ")");
        if (!st) return fail(st.error());
        std::int64_t n = 0;
        const Json& rows = spec["rows"];
        if (!rows.is_array()) continue;
        for (const auto& row : rows) {
          if (!row.is_array()) continue;
          st->reset();
          for (int k = 0; k < int(idx.size()); ++k)
            bind_cell(*st, k + 1, idx[k] < int(row.size()) ? row[std::size_t(idx[k])] : Json());
          if (auto s = st->run(); !s) return fail(s.error());
          ++n;
        }
        restored_tables[t] = n;
      }
    }
    LOOM_TRY(c.exec("COMMIT"));
  }
  if (auto s = rt.db().fts_sync(); !s) log::warn(kLog, "search index not brought up to date: {}", s.error().message);

  std::int64_t memory_nodes = 0;
  if (content.contains("memory") && content["memory"].is_array()) {
    Json arr = Json::array();
    std::set<std::string> ids;
    for (const auto& n : content["memory"])
      if (n.is_object() && n.contains("id") && n["id"].is_string()) { ids.insert(n["id"].get<std::string>()); arr.push_back(n); }
    if (mode == "merge")
      for (const auto& n : rt.memory().get_all())
        if (!ids.count(n.id)) arr.push_back(n.to_json());
    memory_nodes = std::int64_t(arr.size());
    json::DumpOptions o;
    o.indent = 2;
    o.ensure_ascii = false;
    fsutil::AtomicWriteOptions w;
    w.fsync = true;
    w.owner_only = false;
    LOOM_TRY(fsutil::atomic_write(rt.paths().memory, json::py_dumps(arr, o), w));
    LOOM_TRY(rt.memory().reload());
  }

  Json restored_keys = Json::array();
  if (content.contains("config") && content["config"].is_object()) {
    const std::vector<std::string> keep_v = string_list(opt.value("keep_config_keys", Json()), {});
    const std::set<std::string> keep(keep_v.begin(), keep_v.end());
    const Guard g = make_guard(rt);
    Json cfg = content["config"];
    Json dropped = Json::array();
    strip(cfg, g, "", dropped);  // a crafted bundle cannot plant a credential-named setting either
    if (mode == "replace")
      for (const auto& k : rt.config().keys())
        if (!keep.count(k) && !cfg.contains(k)) rt.config().erase(k);
    for (auto it = cfg.begin(); it != cfg.end(); ++it) {
      if (keep.count(it.key())) continue;
      rt.config().set(it.key(), it.value());
      restored_keys.push_back(it.key());
    }
    LOOM_TRY(rt.config().save());
  }
  return Json{{"schema", std::string(kSchema)},
              {"mode", mode},
              {"restored", Json{{"tables", restored_tables}, {"memory_nodes", memory_nodes}, {"config_keys", restored_keys}}},
              {"analysis_runs", 0}};
}

}  // namespace loom::working_copy
