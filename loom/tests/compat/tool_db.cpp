// Database compat commands. Output shapes mirror what engine/db.py returns,
// so tests/compat/test_db_compat.py can diff Python and C++ executions.
#include <climits>

#include "compat_registry.h"
#include "loom/db.h"
#include "loom/sqlite.h"

using namespace loom;
using namespace loom::compat;

namespace {

Json opt(const std::optional<std::string>& s) { return s ? Json(*s) : Json(nullptr); }

// Python create_conv() return shape.
Json py_conv_created(const Conversation& c) {
  return Json{{"id", c.id}, {"title", c.title}, {"created", c.created}, {"updated", c.updated}};
}

// Resolve "$name" / "$name.key" / "$name.0.key" references in op arguments.
Result<Json> resolve(const Json& v, const Json& vars) {
  if (v.is_string()) {
    const auto& s = v.get_ref<const std::string&>();
    if (s.size() > 1 && s[0] == '$') {
      std::string path = s.substr(1);
      std::vector<std::string> parts;
      std::size_t start = 0;
      while (true) {
        auto dot = path.find('.', start);
        parts.push_back(path.substr(start, dot - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
      }
      const Json* cur = json::find(vars, parts[0]);
      if (!cur) return Error(Errc::NotFound, "unknown variable " + parts[0]);
      for (std::size_t i = 1; i < parts.size(); ++i) {
        if (cur->is_array()) {
          std::size_t idx = std::stoul(parts[i]);
          if (idx >= cur->size()) return Error(Errc::NotFound, "index out of range in " + s);
          cur = &(*cur)[idx];
        } else {
          cur = json::find(*cur, parts[i]);
          if (!cur) return Error(Errc::NotFound, "missing key in " + s);
        }
      }
      return *cur;
    }
    return v;
  }
  if (v.is_array()) {
    Json out = Json::array();
    for (const auto& x : v) {
      LOOM_TRY_ASSIGN(Json r, resolve(x, vars));
      out.push_back(std::move(r));
    }
    return out;
  }
  if (v.is_object()) {
    Json out = Json::object();
    for (auto it = v.begin(); it != v.end(); ++it) {
      LOOM_TRY_ASSIGN(Json r, resolve(it.value(), vars));
      out[it.key()] = std::move(r);
    }
    return out;
  }
  return v;
}

std::optional<std::string> sopt(const Json& a, const char* k) {
  const Json* v = json::find(a, k);
  if (v && v->is_string()) return v->get<std::string>();
  return std::nullopt;
}

template <class T>
Json rows(const std::vector<T>& v) {
  Json arr = Json::array();
  for (const auto& x : v) arr.push_back(x.to_json());
  return arr;
}

// Executes one op with Python-shaped results. Errors -> {"error": true}
// (Python: an exception was raised).
Json run_op(Database& db, const std::string& op, const Json& a) {
  auto str = [&](const char* k) { return json::get_string(a, k); };
  if (op == "create_conv") {
    auto r = db.create_conv(json::get_string(a, "title", "New Chat"));
    if (!r) return Json{{"error", true}};
    return py_conv_created(*r);
  }
  if (op == "list_convs") {
    auto r = db.list_convs(static_cast<int>(json::get_int(a, "limit", 50)));
    if (!r) return Json{{"error", true}};
    return rows(*r);
  }
  if (op == "get_conv") {
    auto r = db.get_conv(str("cid"));
    if (!r) return Json{{"error", true}};
    return *r ? (*r)->to_json() : Json(nullptr);
  }
  if (op == "update_conv") {
    auto p = ConvPatch::from_json(a.value("fields", Json::object()));
    if (!p) return Json{{"error", true}};
    if (!db.update_conv(str("id"), *p)) return Json{{"error", true}};
    return nullptr;
  }
  if (op == "delete_conv") return db.delete_conv(str("cid")) ? Json(nullptr) : Json{{"error", true}};
  if (op == "create_msg") {
    NewMessage m;
    m.conv_id = str("conv_id");
    m.text = str("text");
    m.role = str("role");
    m.model = sopt(a, "model");
    m.parent_id = sopt(a, "parent_id");
    if (const Json* x = json::find(a, "attachments")) m.attachments = *x;
    m.version_group_id = sopt(a, "version_group_id");
    m.weight = json::get_number(a, "weight", 1.0);
    if (const Json* x = json::find(a, "metadata")) m.metadata = *x;
    auto r = db.create_msg(m);
    if (!r) return Json{{"error", true}};
    return *r;
  }
  if (op == "batch_create_msgs") {
    std::vector<BatchMessage> batch;
    for (const auto& m : a.value("messages", Json::array())) batch.push_back(BatchMessage::from_json(m));
    auto r = db.batch_create_msgs(str("conv_id"), batch, static_cast<int>(json::get_int(a, "batch_size", 1000)));
    if (!r) return Json{{"error", true}};
    return *r;
  }
  if (op == "get_unanalysed_msgs") {
    auto r = db.get_unanalysed_msgs(static_cast<int>(json::get_int(a, "limit", 100)));
    if (!r) return Json{{"error", true}};
    return rows(*r);
  }
  if (op == "count_pending_semantic") {
    auto r = db.count_pending_semantic();
    if (!r) return Json{{"error", true}};
    return *r;
  }
  if (op == "mark_analysed") {
    return db.mark_analysed(str("msg_id"), a.value("analysis", Json::object())) ? Json(nullptr)
                                                                                : Json{{"error", true}};
  }
  if (op == "get_msgs") {
    auto r = db.get_msgs(str("conv_id"), json::get_bool(a, "include_all", false));
    if (!r) return Json{{"error", true}};
    return rows(*r);
  }
  if (op == "get_msg") {
    auto r = db.get_msg(str("mid"));
    if (!r) return Json{{"error", true}};
    return *r ? (*r)->to_json() : Json(nullptr);
  }
  if (op == "get_versions") {
    auto r = db.get_versions(str("version_group_id"));
    if (!r) return Json{{"error", true}};
    return rows(*r);
  }
  if (op == "set_msg_status") return db.set_msg_status(str("mid"), str("status")) ? Json(nullptr) : Json{{"error", true}};
  if (op == "update_msg") {
    auto p = MsgPatch::from_json(a.value("fields", Json::object()));
    if (!p) return Json{{"error", true}};
    return db.update_msg(str("id"), *p) ? Json(nullptr) : Json{{"error", true}};
  }
  if (op == "edit_msg") {
    auto r = db.edit_msg(str("mid"), str("new_text"));
    if (!r) return Json{{"error", true}};
    return *r ? Json(**r) : Json(nullptr);
  }
  if (op == "restore_version") {
    auto r = db.restore_version(str("mid"));
    if (!r) return Json{{"error", true}};
    return *r;
  }
  if (op == "create_link") {
    auto r = db.create_link(str("src"), str("dst"), json::get_string(a, "link_type", "related"),
                            json::get_number(a, "weight", 1.0), a.value("metadata", Json(nullptr)));
    if (!r) return Json{{"error", true}};
    return *r;
  }
  if (op == "get_links") {
    auto nid = sopt(a, "node_id");
    auto lt = sopt(a, "link_type");
    auto r = db.get_links(nid ? std::optional<std::string_view>(*nid) : std::nullopt,
                          lt ? std::optional<std::string_view>(*lt) : std::nullopt);
    if (!r) return Json{{"error", true}};
    return rows(*r);
  }
  if (op == "delete_link") return db.delete_link(str("lid")) ? Json(nullptr) : Json{{"error", true}};
  if (op == "create_node" || op == "get_or_create_node") {
    NodeOptions o;
    o.content = json::get_string(a, "content");
    if (const Json* x = json::find(a, "tags")) o.tags = *x;
    if (const Json* x = json::find(a, "metadata")) o.metadata = *x;
    o.node_id = sopt(a, "node_id");
    auto r = op == "create_node" ? db.create_node(str("label"), json::get_string(a, "kind", "entity"), o)
                                 : db.get_or_create_node(str("label"), json::get_string(a, "kind", "entity"), o);
    if (!r) return Json{{"error", true}};
    return *r;
  }
  if (op == "get_node" || op == "find_node") {
    auto kind = sopt(a, "kind");
    auto r = op == "get_node"
                 ? db.get_node(str("nid"))
                 : db.find_node(str("label"), kind ? std::optional<std::string_view>(*kind) : std::nullopt);
    if (!r) return Json{{"error", true}};
    return *r ? (*r)->to_json() : Json(nullptr);
  }
  if (op == "list_nodes") {
    auto kind = sopt(a, "kind");
    auto r = db.list_nodes(kind ? std::optional<std::string_view>(*kind) : std::nullopt,
                           static_cast<int>(json::get_int(a, "limit", 200)));
    if (!r) return Json{{"error", true}};
    return rows(*r);
  }
  if (op == "update_node") {
    auto p = NodePatch::from_json(a.value("fields", Json::object()));
    if (!p) return Json{{"error", true}};
    return db.update_node(str("id"), *p) ? Json(nullptr) : Json{{"error", true}};
  }
  if (op == "delete_node") return db.delete_node(str("nid")) ? Json(nullptr) : Json{{"error", true}};
  if (op == "get_graph_data") {
    auto cid = sopt(a, "conv_id");
    auto r = db.get_graph_data(cid ? std::optional<std::string_view>(*cid) : std::nullopt);
    if (!r) return Json{{"error", true}};
    return *r;
  }
  if (op == "vacuum") return db.vacuum() ? Json(nullptr) : Json{{"error", true}};
  return Json{{"error", "unknown op " + op}};
}

Json api_dump(Database& db) {
  Json out = Json::object();
  auto convs = db.list_convs(INT_MAX);
  Json cj = Json::array();
  Json mj = Json::array();
  if (convs) {
    for (const auto& c : *convs) {
      cj.push_back(c.to_json());
      auto msgs = db.get_msgs(c.id, true);
      if (msgs) {
        for (const auto& m : *msgs) mj.push_back(m.to_json());
      }
    }
  }
  out["conversations"] = cj;
  out["messages"] = mj;
  auto nodes = db.list_nodes(std::nullopt, INT_MAX);
  out["nodes"] = nodes ? rows(*nodes) : Json::array();
  auto links = db.get_links();
  out["links"] = links ? rows(*links) : Json::array();
  auto g = db.get_graph_data();
  out["graph"] = g ? *g : Json(nullptr);
  auto pending = db.count_pending_semantic();
  out["pending"] = pending ? Json(*pending) : Json(nullptr);
  auto un = db.get_unanalysed_msgs(INT_MAX);
  Json ids = Json::array();
  if (un) {
    for (const auto& p : *un) ids.push_back(p.id);
  }
  out["unanalysed"] = ids;
  auto sv = db.get_meta("schema_version");
  out["schema_version"] = (sv && *sv) ? Json(**sv) : Json(nullptr);
  return out;
}

}  // namespace

LOOM_COMPAT_COMMAND(cmd_db_ops, "db-ops", "<db> @ops.json [@vars.json] : run Python-shaped DB ops") {
  if (args.size() < 2) return fail("usage: db-ops <db> @ops.json [@vars.json]");
  auto ops = arg_json(args[1]);
  if (!ops || !ops->is_array()) return fail("ops must be a JSON array");
  Json vars = Json::object();
  if (args.size() > 2) {
    auto v = arg_json(args[2]);
    if (!v) return fail("bad vars json");
    vars = *v;
  }
  auto db = Database::open(args[0]);
  if (!db) return fail("open failed: " + db.error().to_string());
  Json results = Json::array();
  for (const auto& op : *ops) {
    auto a = resolve(op.value("args", Json::object()), vars);
    if (!a) return fail("resolve failed: " + a.error().to_string());
    Json r = run_op(**db, json::get_string(op, "op"), *a);
    if (const Json* save = json::find(op, "save"); save && save->is_string()) vars[save->get<std::string>()] = r;
    results.push_back(std::move(r));
  }
  print_json(Json{{"results", results}, {"vars", vars}});
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_db_api_dump, "db-api-dump", "<db> : canonical dump through the Database API") {
  if (args.empty()) return fail("usage: db-api-dump <db>");
  auto db = Database::open(args[0]);
  if (!db) return fail("open failed: " + db.error().to_string());
  print_json(api_dump(**db));
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_db_raw_dump, "db-raw-dump", "<db> : raw rows [typeof, value] of the core tables (no migration)") {
  if (args.empty()) return fail("usage: db-raw-dump <db>");
  sql::OpenOptions oo;
  oo.create = false;
  oo.read_only = true;
  auto c = sql::Connection::open(args[0], oo);
  if (!c) return fail("open failed: " + c.error().to_string());
  Json out = Json::object();
  for (const char* table : {"_meta", "conversations", "messages", "nodes", "links"}) {
    if (!c->has_table(table)) continue;
    auto cols = c->columns(table);
    std::string sql = "SELECT ";
    for (std::size_t i = 0; i < cols.size(); ++i) {
      if (i) sql += ", ";
      sql += "typeof(\"" + cols[i] + "\"), \"" + cols[i] + "\"";
    }
    sql += std::string(" FROM ") + table + " ORDER BY rowid";
    auto st = c->prepare(sql);
    if (!st) return fail(st.error().to_string());
    Json rowsj = Json::array();
    while (true) {
      auto row = st->step();
      if (!row) return fail(row.error().to_string());
      if (!*row) break;
      Json r = Json::object();
      for (std::size_t i = 0; i < cols.size(); ++i) {
        int tcol = static_cast<int>(2 * i);
        int vcol = tcol + 1;
        std::string type = st->get_text(tcol);
        Json val;
        switch (st->column_type(vcol)) {
          case sql::Type::Integer: val = st->get_int(vcol); break;
          case sql::Type::Float: val = st->get_double(vcol); break;
          case sql::Type::Text: val = st->get_text(vcol); break;
          case sql::Type::Blob: val = "<blob>"; break;
          case sql::Type::Null: val = nullptr; break;
        }
        r[cols[i]] = Json::array({type, val});
      }
      rowsj.push_back(std::move(r));
    }
    out[table] = Json{{"columns", cols}, {"rows", rowsj}};
  }
  print_json(out);
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_db_schema, "db-schema", "<db> : sqlite_master entries (type, name, tbl_name, sql)") {
  if (args.empty()) return fail("usage: db-schema <db>");
  sql::OpenOptions oo;
  oo.create = false;
  oo.read_only = true;
  auto c = sql::Connection::open(args[0], oo);
  if (!c) return fail("open failed: " + c.error().to_string());
  auto st = c->prepare(
      "SELECT type, name, tbl_name, sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type, name");
  if (!st) return fail(st.error().to_string());
  Json arr = Json::array();
  while (true) {
    auto row = st->step();
    if (!row) return fail(row.error().to_string());
    if (!*row) break;
    arr.push_back(Json::array({st->get_text(0), st->get_text(1), st->get_text(2), opt(st->get_opt_text(3))}));
  }
  print_json(arr);
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_db_open, "db-open", "<db> : open (create + migrate) and report schema/loom versions") {
  if (args.empty()) return fail("usage: db-open <db>");
  auto db = Database::open(args[0]);
  if (!db) return fail("open failed: " + db.error().to_string());
  auto sv = (*db)->get_meta("schema_version");
  auto lv = (*db)->get_meta("loom_schema_version");
  print_json(Json{{"schema_version", (sv && *sv) ? Json(**sv) : Json(nullptr)},
                  {"loom_schema_version", (lv && *lv) ? Json(**lv) : Json(nullptr)},
                  {"fts", (*db)->fts_status().to_json()}});
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_db_search, "db-search", "<db> <query> [mode] : full-text search (Loom extension)") {
  if (args.size() < 2) return fail("usage: db-search <db> <query> [auto|fts5|like]");
  auto db = Database::open(args[0]);
  if (!db) return fail("open failed: " + db.error().to_string());
  SearchOptions o;
  o.limit = 1000;
  if (args.size() > 2) {
    if (args[2] == "fts5") o.mode = FtsMode::Fts5;
    if (args[2] == "like") o.mode = FtsMode::Like;
  }
  auto r = (*db)->search_messages(args[1], o);
  if (!r) return fail(r.error().to_string());
  print_json(r->to_json());
  return 0;
}
