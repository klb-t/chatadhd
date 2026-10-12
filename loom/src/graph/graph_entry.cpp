// The graph entry view. See include/loom/graph_entry.h.
#include "loom/graph_entry.h"

#include <algorithm>
#include <set>

#include "loom/runtime_profile.h"

namespace loom::graph_entry {

Json known(std::int64_t value) { return Json{{"known", true}, {"value", value}}; }
Json known_by(std::int64_t value, Json by) { return Json{{"known", true}, {"value", value}, {"by", std::move(by)}}; }
Json unknown(std::string_view reason) { return Json{{"known", false}, {"reason", std::string(reason)}}; }

namespace {

// ── small helpers ──────────────────────────────────────────────────────

const Json* pointer_at(const Json& root, const std::string& pointer) {
  try {
    const Json::json_pointer p(pointer);
    return root.contains(p) ? &root.at(p) : nullptr;
  } catch (const std::exception&) {
    return nullptr;
  }
}

std::int64_t member_count(const Json& v) { return v.is_array() || v.is_object() ? static_cast<std::int64_t>(v.size()) : 1; }

std::string label_of(const Json& label, const std::string& lang, const std::string& fallback) {
  if (label.is_string()) return label.get<std::string>();
  if (!label.is_object() || label.empty()) return fallback;
  for (const std::string& key : {lang, std::string("en")}) {
    auto it = label.find(key);
    if (it != label.end() && it->is_string()) return it->get<std::string>();
  }
  for (const auto& item : label.items())
    if (item.value().is_string()) return item.value().get<std::string>();
  return fallback;
}

// Runs one grouped COUNT: "SELECT <group>, COUNT(*) FROM ... GROUP BY 1". A
// table that does not exist yet (lazily created stores) holds zero rows; any
// failure is unknown, never zero.
Json grouped(const Context& ctx, const char* table, const char* group_expr, const char* where = nullptr,
             const char* require_table = nullptr) {
  Database* db = ctx.env.db;
  if (!db) return unknown("database_unavailable");
  try {
    auto lock = db->lock();
    auto& c = db->conn();
    if (!c.has_table(require_table ? require_table : table)) return known_by(0, Json::object());
    std::string sql = std::string("SELECT COALESCE(") + group_expr + ", ''), COUNT(*) FROM " + table;
    if (where) sql += std::string(" WHERE ") + where;
    sql += " GROUP BY 1 ORDER BY 1";
    auto st = c.prepare(sql);
    if (!st) return unknown("query_failed");
    Json by = Json::object();
    std::int64_t total = 0;
    while (true) {
      auto more = st->step();
      if (!more) return unknown("query_failed");
      if (!*more) break;
      by[st->get_text(0)] = st->get_int(1);
      total += st->get_int(1);
    }
    return known_by(total, by);
  } catch (const std::exception&) {
    return unknown("query_failed");
  }
}

// Latest completed knowledge run, or "" when there is none (or on failure).
std::string latest_run(const Context& ctx, bool& ok) {
  ok = true;
  Database* db = ctx.env.db;
  if (!db) { ok = false; return {}; }
  try {
    auto lock = db->lock();
    auto& c = db->conn();
    if (!c.has_table("loom_kb_runs")) return {};
    auto r = c.query_text("SELECT run_id FROM loom_kb_runs WHERE status='done' ORDER BY created DESC, run_id DESC LIMIT 1");
    if (!r) { ok = false; return {}; }
    return r->value_or("");
  } catch (const std::exception&) {
    ok = false;
    return {};
  }
}

Json kb_by_run(const Context& ctx, const char* table, const char* group_expr) {
  bool ok = false;
  const std::string run = latest_run(ctx, ok);
  if (!ok) return unknown("database_unavailable");
  if (run.empty()) return known_by(0, Json::object());
  Database* db = ctx.env.db;
  try {
    auto lock = db->lock();
    auto& c = db->conn();
    auto st = c.prepare(std::string("SELECT COALESCE(") + group_expr + ", ''), COUNT(*) FROM " + table +
                        " WHERE run_id=? GROUP BY 1 ORDER BY 1");
    if (!st) return unknown("query_failed");
    st->bind(1, run);
    Json by = Json::object();
    std::int64_t total = 0;
    while (true) {
      auto more = st->step();
      if (!more) return unknown("query_failed");
      if (!*more) break;
      by[st->get_text(0)] = st->get_int(1);
      total += st->get_int(1);
    }
    Json out = known_by(total, by);
    out["run"] = run;
    return out;
  } catch (const std::exception&) {
    return unknown("query_failed");
  }
}

Json table_total(const Context& ctx, const char* table) {
  Database* db = ctx.env.db;
  if (!db) return unknown("database_unavailable");
  try {
    auto lock = db->lock();
    auto& c = db->conn();
    if (!c.has_table(table)) return known(0);
    auto r = c.query_int(std::string("SELECT COUNT(*) FROM ") + table);
    if (!r) return unknown("query_failed");
    return known(r->value_or(0));
  } catch (const std::exception&) {
    return unknown("query_failed");
  }
}

Json only(Json value) { return Json{{"", std::move(value)}}; }

// ── built-in providers (mechanism) ─────────────────────────────────────

void register_builtin(Providers& p) {
  p.add("db.conversations", [](const Context& c, const Json&) {
    return only(grouped(c, "conversations", "source"));
  });
  p.add("db.messages_by_status", [](const Context& c, const Json&) {
    return only(grouped(c, "messages", "status"));
  });
  p.add("db.sources", [](const Context& c, const Json&) {
    return only(grouped(c, "loom_sources", "format"));
  });
  p.add("db.recognition", [](const Context& c, const Json&) {
    return only(grouped(c, "nodes", "json_extract(metadata,'$.recognition.status')", "kind='import:unrecognized'"));
  });
  p.add("db.duplicates", [](const Context& c, const Json&) {
    return only(grouped(c, "links", "json_extract(metadata,'$.level')",
                        "link_type='derived_from' AND json_extract(metadata,'$.relation')='duplicate_of'"));
  });
  p.add("db.nodes_by_kind", [](const Context& c, const Json&) { return only(grouped(c, "nodes", "kind")); });
  p.add("db.provenance", [](const Context& c, const Json&) {
    return only(grouped(c, "loom_provenance", "subject_kind"));
  });
  p.add("kb.entities", [](const Context& c, const Json&) { return only(kb_by_run(c, "loom_kb_entities", "kind")); });
  p.add("kb.claims", [](const Context& c, const Json&) { return only(kb_by_run(c, "loom_kb_claims", "predicate")); });
  p.add("kb.runs", [](const Context& c, const Json&) { return only(grouped(c, "loom_kb_runs", "status")); });
  p.add("usage.ledger", [](const Context& c, const Json&) {
    return only(table_total(c, "usage_operations"));
  });
  // Registries that cannot be enumerated from here stay unknown.
  p.add("methods.registry", [](const Context& c, const Json&) {
    const Json* h = pointer_at(c.env.host_counts, "/methods");
    return only(h && h->is_number_integer() ? known(h->get<std::int64_t>()) : unknown("registry_not_listable"));
  });
  p.add("perspectives.store", [](const Context& c, const Json&) {
    const Json* h = pointer_at(c.env.host_counts, "/perspectives");
    return only(h && h->is_number_integer() ? known(h->get<std::int64_t>()) : unknown("store_not_wired"));
  });
  p.add("layers.entries", [](const Context& c, const Json&) {
    const Json* h = pointer_at(c.env.host_counts, "/layers");
    return only(h && h->is_number_integer() ? known(h->get<std::int64_t>()) : unknown("layers_not_wired"));
  });
  p.add("interfaces.enumerate", [](const Context& c, const Json&) {
    const Json* h = pointer_at(c.env.host_counts, "/interfaces");
    Json out = Json::object();
    if (h && h->is_object() && !h->empty()) {
      for (const auto& item : h->items())
        out[item.key()] = item.value().is_number_integer() ? known(item.value().get<std::int64_t>()) : unknown("not_a_count");
      return out;
    }
    return only(unknown("not_enumerable_from_core"));
  });
  p.add("profile.domains", [](const Context&, const Json&) {
    return only(known(static_cast<std::int64_t>(RuntimeProfile::domains().size())));
  });
  p.add("profile.items", [](const Context& c, const Json& params) {
    auto profile = RuntimeProfile::load(json::get_string(params, "domain"), c.env.data_dir);
    if (!profile) return only(unknown("profile_unavailable"));
    const Json* v = pointer_at(profile->values(), json::get_string(params, "pointer"));
    return only(v ? known(member_count(*v)) : unknown("pointer_absent"));
  });
  p.add("data.items", [](const Context& c, const Json& params) {
    const Json* v = pointer_at(c.values, json::get_string(params, "pointer"));
    return only(v ? known(member_count(*v)) : unknown("pointer_absent"));
  });
  p.add("pack.file", [](const Context& c, const Json& params) {
    std::shared_ptr<const kb::Pack> pack = c.env.pack;
    if (!pack) {
      auto loaded = kb::Pack::load_builtin();
      if (!loaded) return only(unknown("pack_unavailable"));
      pack = *loaded;
    }
    const Json& doc = pack->file(json::get_string(params, "path"));
    return only(doc.is_null() ? unknown("pack_file_absent") : known(member_count(doc)));
  });
}

// ── view assembly ───────────────────────────────────────────────────────

struct Gathered {
  Json counts = Json::object();
  Json sources = Json::array();
};

void gather(const Context& ctx, const Providers& providers, const std::string& binding_id, const Json& bindings,
            Gathered& into, Json& issues) {
  auto it = bindings.find(binding_id);
  if (it == bindings.end() || !it->is_object()) {
    issues.push_back(Json{{"code", "binding_missing"}, {"binding", binding_id}});
    into.counts[binding_id] = unknown("binding_missing");
    return;
  }
  const Json& binding = *it;
  if (json::get_bool(binding, "disabled", false)) return;
  const std::string provider_id = json::get_string(binding, "provider");
  const std::string as = json::get_string(binding, "as", binding_id);
  into.sources.push_back(Json{{"binding", binding_id}, {"provider", provider_id}});
  const ProviderFn* fn = providers.find(provider_id);
  if (!fn) {
    issues.push_back(Json{{"code", "provider_missing"}, {"binding", binding_id}, {"provider", provider_id}});
    into.counts[as] = unknown("provider_missing");
    return;
  }
  Json result;
  try {
    const Json* params = json::find(binding, "params");
    result = (*fn)(ctx, params ? *params : Json::object());
  } catch (const std::exception&) {
    result = only(unknown("provider_failed"));
  }
  if (!result.is_object()) result = only(unknown("provider_failed"));
  for (const auto& item : result.items()) into.counts[item.key().empty() ? as : item.key()] = item.value();
}

int order_of(const Json& node) {
  return static_cast<int>(json::get_int(node, "order", 1000));
}

std::string cell_key(const Json& axes) {
  std::string key;
  for (const char* name : {"about", "kind", "time"}) {
    if (!key.empty()) key += ".";
    key += json::get_string(axes, name);
  }
  return key;
}

Json axes_view(const Json& values, const std::string& lang) {
  Json out = Json::object();
  const Json* groups = json::find(values, "axes");
  if (!groups || !groups->is_object()) return out;
  for (const auto& group : groups->items()) {
    std::vector<std::pair<int, std::string>> order;
    for (const auto& axis : group.value().items()) order.emplace_back(order_of(axis.value()), axis.key());
    std::sort(order.begin(), order.end());
    Json list = Json::array();
    for (const auto& entry : order) {
      const Json& axis = group.value().at(entry.second);
      Json vals = Json::array();
      if (axis.contains("values") && axis["values"].is_array())
        for (const auto& v : axis["values"])
          vals.push_back(Json{{"id", json::get_string(v, "id")}, {"label", label_of(v.value("label", Json()), lang, json::get_string(v, "id"))}});
      list.push_back(Json{{"id", entry.second}, {"label", label_of(axis.value("label", Json()), lang, entry.second)},
                          {"open", json::get_bool(axis, "open", false)}, {"values", vals}});
    }
    out[group.key()] = list;
  }
  return out;
}

std::string axis_value_label(const Json& values, const std::string& lang, const char* axis, const std::string& id) {
  const Json* a = pointer_at(values, std::string("/axes/entry/") + axis + "/values");
  if (a && a->is_array())
    for (const auto& v : *a)
      if (json::get_string(v, "id") == id) return label_of(v.value("label", Json()), lang, id);
  return id;
}

void check_axes(const Json& values, const Json& axes, const std::string& owner, Json& issues) {
  for (const char* name : {"about", "kind", "time"}) {
    const std::string v = json::get_string(axes, name);
    const Json* a = pointer_at(values, std::string("/axes/entry/") + name);
    bool found = false;
    if (a && a->contains("values"))
      for (const auto& x : (*a)["values"]) found = found || json::get_string(x, "id") == v;
    if (!found && !(a && json::get_bool(*a, "open", false)))
      issues.push_back(Json{{"code", "axis_value_unknown"}, {"owner", owner}, {"axis", name}, {"value", v}});
  }
}

Json list_view(const Json& values, const std::string& pointer, const std::string& lang) {
  Json out = Json::array();
  const Json* v = pointer_at(values, pointer);
  if (!v) return out;
  auto add = [&](const std::string& id, const Json& item) {
    Json node{{"id", id}, {"label", label_of(item.is_object() ? item.value("label", Json()) : Json(), lang, id)}};
    if (item.is_object() && item.contains("parent")) node["parent"] = item["parent"];
    out.push_back(node);
  };
  if (v->is_object()) {
    for (const auto& it : v->items()) add(it.key(), it.value());
  } else if (v->is_array()) {
    for (const auto& it : *v) add(it.is_object() ? json::get_string(it, "id") : (it.is_string() ? it.get<std::string>() : ""), it);
  }
  return out;
}

Json party_node(const Json& values, const std::string& cls, const std::string& lang) {
  Json slots = Json::array();
  const std::string state = json::get_string(*pointer_at(values, "/ontology"), "default_state");
  const Json* cl = pointer_at(values, "/ontology/classes/" + cls);
  std::string pkg = cl ? json::get_string(*cl, "package") : std::string();
  const Json* pk = pkg.empty() ? nullptr : pointer_at(values, "/ontology/packages/" + pkg + "/slots");
  if (pk && pk->is_array())
    for (const auto& s : *pk)
      slots.push_back(Json{{"id", json::get_string(s, "id")}, {"label", label_of(s.value("label", Json()), lang, json::get_string(s, "id"))},
                           {"state", state}, {"value", nullptr}});
  return Json{{"class", cls}, {"slots", slots}};
}

}  // namespace

const Providers& Providers::builtin() {
  static const Providers instance = [] {
    Providers p;
    register_builtin(p);
    return p;
  }();
  return instance;
}

const ProviderFn* Providers::find(const std::string& id) const {
  auto it = fns_.find(id);
  return it == fns_.end() ? nullptr : &it->second;
}

std::vector<std::string> Providers::ids() const {
  std::vector<std::string> out;
  for (const auto& e : fns_) out.push_back(e.first);
  return out;
}

Result<Json> compute(const Env& env, const Json& options, const Providers* custom) {
  const Json empty = Json::object();
  const Json& opt = options.is_object() ? options : empty;
  const std::string lang = json::get_string(opt, "lang", "en");
  const bool include_hidden = json::get_bool(opt, "include_hidden", false);
  const Json* overrides = json::find(opt, "overrides");
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_entry", env.data_dir, overrides ? *overrides : Json::object()));
  const Json& values = profile.values();
  Providers merged;
  const Providers* providers = &Providers::builtin();
  if (custom) {
    merged = Providers::builtin();
    for (const auto& id : custom->ids()) merged.add(id, *custom->find(id));
    providers = &merged;
  }
  const Context ctx{env, values};
  Json issues = Json::array();
  const Json no_object = Json::object();
  const Json& areas = values.contains("areas") && values["areas"].is_object() ? values["areas"] : no_object;
  const Json& bindings = values.contains("bindings") && values["bindings"].is_object() ? values["bindings"] : no_object;

  // 1. Order the calibration areas; remember which cell each occupies.
  struct Slot { int order; std::string id; };
  std::vector<Slot> slots;
  for (const auto& a : areas.items()) slots.push_back({order_of(a.value()), a.key()});
  std::sort(slots.begin(), slots.end(), [](const Slot& x, const Slot& y) { return x.order != y.order ? x.order < y.order : x.id < y.id; });
  std::map<std::string, std::string> cell_owner;       // cell key -> first area id (hidden areas own cells too)
  std::map<std::string, std::vector<std::string>> extra;  // area id -> cell-bound bindings
  for (const auto& s : slots) {
    const Json& axes = areas.at(s.id).value("axes", Json::object());
    check_axes(values, axes, s.id, issues);
    cell_owner.emplace(cell_key(axes), s.id);
  }

  // 2. Cell-bound bindings fill occupied cells or generate new ones.
  std::map<std::string, std::pair<Json, std::vector<std::string>>> generated;  // cell key -> (axes, bindings)
  for (const auto& b : bindings.items()) {
    if (!b.value().is_object() || json::get_bool(b.value(), "disabled", false)) continue;
    const Json* cells = json::find(b.value(), "cells");
    if (!cells || !cells->is_array()) continue;
    for (const auto& cell : *cells) {
      check_axes(values, cell, b.key(), issues);
      const std::string key = cell_key(cell);
      auto owner = cell_owner.find(key);
      if (owner != cell_owner.end()) {
        const Json& own = areas.at(owner->second);
        const Json* list = json::find(own, "bindings");
        bool listed = false;
        if (list && list->is_array()) for (const auto& id : *list) listed = listed || (id.is_string() && id.get<std::string>() == b.key());
        auto& v = extra[owner->second];
        if (!listed && std::find(v.begin(), v.end(), b.key()) == v.end()) v.push_back(b.key());
      } else {
        auto& g = generated[key];
        g.first = Json{{"about", json::get_string(cell, "about")}, {"kind", json::get_string(cell, "kind")}, {"time", json::get_string(cell, "time")}};
        if (std::find(g.second.begin(), g.second.end(), b.key()) == g.second.end()) g.second.push_back(b.key());
      }
    }
  }

  auto build = [&](const std::string& id, const std::string& label, const Json& axes, const std::vector<std::string>& ids,
                   const Json* expand, const char* origin, int order, bool hidden) {
    Gathered g;
    for (const auto& bid : ids) gather(ctx, *providers, bid, bindings, g, issues);
    Json node{{"id", id}, {"label", label}, {"origin", origin}, {"axes", axes}, {"order", order},
              {"counts", g.counts}, {"sources", g.sources}, {"expand", expand ? *expand : Json(nullptr)}};
    if (hidden) node["hidden"] = true;
    return node;
  };

  Json out_areas = Json::array();
  for (const auto& s : slots) {
    const Json& a = areas.at(s.id);
    const bool hidden = json::get_bool(a, "hidden", false);
    if (hidden && !include_hidden) continue;
    std::vector<std::string> ids;
    if (a.contains("bindings") && a["bindings"].is_array())
      for (const auto& id : a["bindings"]) if (id.is_string()) ids.push_back(id.get<std::string>());
    for (const auto& id : extra[s.id]) ids.push_back(id);
    const Json axes = a.value("axes", Json::object());
    out_areas.push_back(build(s.id, label_of(a.value("label", Json()), lang, s.id), axes, ids, json::find(a, "expand"),
                              "area", s.order, hidden));
  }
  for (const auto& g : generated) {
    const Json& axes = g.second.first;
    std::string label;
    for (const char* name : {"about", "kind", "time"}) {
      if (!label.empty()) label += " / ";
      label += axis_value_label(values, lang, name, json::get_string(axes, name));
    }
    out_areas.push_back(build("cell:" + g.first, label, axes, g.second.second, nullptr, "cell", 1000, false));
  }

  // 3. Zero seed (R52): data only, no database.
  Json out_seed = Json::array();
  const Json* elements = pointer_at(values, "/seed/elements");
  if (elements && elements->is_object()) {
    std::vector<Slot> order;
    for (const auto& e : elements->items()) order.push_back({order_of(e.value()), e.key()});
    std::sort(order.begin(), order.end(), [](const Slot& x, const Slot& y) { return x.order != y.order ? x.order < y.order : x.id < y.id; });
    for (const auto& s : order) {
      const Json& e = elements->at(s.id);
      const bool hidden = json::get_bool(e, "hidden", false);
      if (hidden && !include_hidden) continue;
      Gathered g;
      if (e.contains("bindings") && e["bindings"].is_array())
        for (const auto& id : e["bindings"]) if (id.is_string()) gather(ctx, *providers, id.get<std::string>(), bindings, g, issues);
      Json node{{"id", "seed:" + s.id}, {"kind", json::get_string(e, "kind")}, {"label", label_of(e.value("label", Json()), lang, s.id)},
                {"order", s.order}, {"counts", g.counts}, {"sources", g.sources}};
      if (e.contains("lists") && e["lists"].is_array())
        for (const auto& l : e["lists"]) node[json::get_string(l, "as")] = list_view(values, json::get_string(l, "pointer"), lang);
      if (const Json* n = json::find(e, "node"); n && n->is_object() && values.contains("ontology"))
        node["node"] = party_node(values, json::get_string(*n, "class"), lang);
      if (hidden) node["hidden"] = true;
      out_seed.push_back(node);
    }
  }

  return Json{{"schema", "loom.graph_entry/1"}, {"read_only", true}, {"lang", lang}, {"profile_hash", profile.hash()},
              {"axes", axes_view(values, lang)}, {"areas", out_areas}, {"seed", out_seed}, {"issues", issues}};
}

}  // namespace loom::graph_entry
