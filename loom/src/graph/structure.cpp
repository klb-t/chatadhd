#include "graph/structure.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <map>
#include <optional>

#include "loom/db.h"
#include "loom/log.h"
#include "loom/runtime_profile.h"

namespace loom::structure {
namespace {

const Json& empty_object() {
  static const Json e = Json::object();
  return e;
}

// {pl, en} (or a plain string) in the language of the answer; the other language, then "" when the data has neither.
std::string words_of(const Json& w, const std::string& lang) {
  if (w.is_string()) return w.get<std::string>();
  if (!w.is_object()) return std::string();
  if (auto s = json::get_string(w, lang); !s.empty()) return s;
  for (const char* l : {"pl", "en"})
    if (auto s = json::get_string(w, l); !s.empty()) return s;
  return std::string();
}

std::string fill(std::string text, const Json& vars) {
  for (const auto& it : vars.items()) {
    const std::string key = "{" + it.key() + "}";
    const std::string val = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
    for (std::size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + val.size())) text.replace(at, key.size(), val);
  }
  return text;
}

// The value at a dotted path of a node ("attrs.kind", "evidence.class"); nullopt when absent.
std::optional<Json> at_path(const Json& node, const std::string& path) {
  const Json* cur = &node;
  std::size_t from = 0;
  while (true) {
    const std::size_t dot = path.find('.', from);
    const std::string key = path.substr(from, dot == std::string::npos ? std::string::npos : dot - from);
    if (!cur->is_object()) return std::nullopt;
    const Json* next = json::find(*cur, key);
    if (!next) return std::nullopt;
    cur = next;
    if (dot == std::string::npos) return *cur;
    from = dot + 1;
  }
}

bool has_prefix(const Json& v, const std::string& p) { return v.is_string() && v.get<std::string>().rfind(p, 0) == 0; }

bool one_of(const Json& value, const Json& wanted) {
  if (wanted.is_array()) return std::any_of(wanted.begin(), wanted.end(), [&](const Json& w) { return w == value; });
  return wanted == value;
}

}  // namespace

const std::vector<std::string>& template_fields() {
  static const std::vector<std::string> f{"schema", "id", "label", "description", "example", "derived_from", "match", "expects", "extends", "note"};
  return f;
}
const std::vector<std::string>& expect_fields() {
  static const std::vector<std::string> f{"id", "label", "relation", "child_match", "card", "confidence", "source", "fill", "note"};
  return f;
}

bool matches(const Json& p, const Json& node, std::set<std::string>& unknown_ops) {
  if (p.is_null()) return true;
  if (p.is_boolean()) return p.get<bool>();
  if (p.is_array()) {  // a bare list = all of them
    for (const auto& q : p)
      if (!matches(q, node, unknown_ops)) return false;
    return true;
  }
  if (!p.is_object()) {
    unknown_ops.insert(std::string("<") + p.type_name() + ">");
    return false;
  }
  bool ok = true;
  for (const auto& it : p.items()) {
    const std::string& op = it.key();
    const Json& v = it.value();
    if (op == "all") {
      if (!v.is_array()) { unknown_ops.insert("all:<not a list>"); ok = false; continue; }
      for (const auto& q : v) ok = matches(q, node, unknown_ops) && ok;
    } else if (op == "any") {
      if (!v.is_array()) { unknown_ops.insert("any:<not a list>"); ok = false; continue; }
      bool any = false;
      for (const auto& q : v) any = matches(q, node, unknown_ops) || any;
      ok = ok && any;
    } else if (op == "not") {
      std::set<std::string> inner;
      const bool m = matches(v, node, inner);
      unknown_ops.insert(inner.begin(), inner.end());
      ok = ok && inner.empty() && !m;  // an unknown operator inside not still matches nothing
    } else if (op == "true") {
      ok = ok && json::truthy(v);
    } else if (op == "prefix") {
      if (p.contains("field")) continue;  // with a field it tests the field (below)
      ok = ok && has_prefix(node.value("address", Json()), v.is_string() ? v.get<std::string>() : std::string("\x01"));
    } else if (op == "kind") {
      ok = ok && one_of(node.value("kind", Json()), v);
    } else if (op == "field") {
      if (!v.is_string()) { unknown_ops.insert("field:<not a text>"); ok = false; continue; }
      const std::optional<Json> got = at_path(node, v.get<std::string>());
      bool any_test = false;
      if (const Json* e = json::find(p, "equals")) { any_test = true; ok = ok && got && *got == *e; }
      if (const Json* in = json::find(p, "in")) { any_test = true; ok = ok && got && one_of(*got, *in); }
      if (const Json* pre = json::find(p, "prefix")) { any_test = true; ok = ok && got && pre->is_string() && has_prefix(*got, pre->get<std::string>()); }
      if (const Json* ex = json::find(p, "exists")) { any_test = true; ok = ok && (got.has_value() == json::truthy(*ex)); }
      if (!any_test) ok = ok && got.has_value();
    } else if (op == "equals" || op == "in" || op == "exists") {
      // read with "field"; alone they name no field
      if (!p.contains("field")) { unknown_ops.insert(op + ":<no field>"); ok = false; }
    } else {
      unknown_ops.insert(op);
      ok = false;
    }
  }
  return ok;
}

Json evaluate(const Json& data, const Json& focus, const Json& children, const Json& request) {
  const std::string lang = json::get_string(request, "lang", "en");
  const Json& templates = data.is_object() && data.contains("templates") && data.at("templates").is_object() ? data.at("templates") : empty_object();
  const Json& adv_words = data.is_object() && data.contains("advisories") ? data.at("advisories") : empty_object();
  const Json& presets = data.is_object() && data.contains("card_presets") ? data.at("card_presets") : empty_object();
  const std::string default_card = json::get_string(data, "default_card", "many");
  std::set<std::string> inferred_classes;
  for (const auto& c : data.value("inferred_classes", Json::array()))
    if (c.is_string()) inferred_classes.insert(c.get<std::string>());
  const Json states = data.value("states", Json::array());
  const bool show_expected = json::get_bool(request, "show_expected", true);
  std::set<std::string> declined;
  for (const auto& d : request.value("declined", Json::array()))
    if (d.is_string()) declined.insert(d.get<std::string>());
  const std::string root_address = json::get_string(focus, "address");

  Json advisories = Json::array();
  std::set<std::string> fingerprints;
  auto advise = [&](const std::string& code, Json vars, const std::string& tpl = "", const std::string& expect = "") {
    const std::string fp = code + "|" + tpl + "|" + expect + "|" + json::get_string(vars, "op") + json::get_string(vars, "other") + json::get_string(vars, "field");
    if (!fingerprints.insert(fp).second) return;
    vars["template"] = vars.value("template", Json(tpl));
    if (!expect.empty()) vars["expect"] = vars.value("expect", Json(expect));
    const Json w = json::find(adv_words, code) ? adv_words.at(code) : Json(code);
    Json words = Json::object();
    for (const char* l : {"pl", "en"}) words[l] = fill(words_of(w.is_object() && w.contains(l) ? w.at(l) : w, l), vars);
    Json a{{"code", code}, {"fingerprint", fp}, {"template", tpl}, {"text", fill(words_of(w, lang), vars)}, {"words", words}, {"blocking", false}};
    if (!expect.empty()) a["expect"] = expect;
    advisories.push_back(std::move(a));
  };
  auto label_of = [&](const Json& t, const std::string& id) {
    const std::string l = t.is_object() ? words_of(t.value("label", Json()), lang) : std::string();
    return l.empty() ? id : l;
  };
  auto check_fields = [&](const Json& obj, const std::vector<std::string>& known, const std::string& tpl) {
    if (!obj.is_object()) return;
    for (const auto& it : obj.items())
      if (std::find(known.begin(), known.end(), it.key()) == known.end()) advise("unknown_field", Json{{"field", it.key()}}, tpl);
  };
  auto predicate_ok = [&](const Json& pred, const Json& node, const std::string& tpl, const std::string& expect) {
    std::set<std::string> unknown;
    const bool m = matches(pred, node, unknown);
    for (const auto& op : unknown) advise("unknown_operator", Json{{"op", op}}, tpl, expect);
    return m;
  };

  // which templates: the asked ones (each must exist and fit), else every one that fits
  std::vector<std::string> asked;
  const bool explicit_list = request.contains("templates") && request.at("templates").is_array() && !request.at("templates").empty();
  if (explicit_list) {
    for (const auto& t : request.at("templates"))
      if (t.is_string()) asked.push_back(t.get<std::string>());
  } else {
    for (const auto& it : templates.items()) asked.push_back(it.key());
  }
  std::vector<std::string> applied;
  for (const auto& id : asked) {
    const Json* t = json::find(templates, id);
    if (!t) { advise("unknown_template", Json::object(), id); continue; }
    if (!t->is_object()) { advise("not_a_template", Json::object(), id); continue; }
    if (!predicate_ok(t->value("match", Json()), focus, id, "")) {
      if (explicit_list) advise("template_not_matching", Json{{"template", label_of(*t, id)}}, id);
      continue;
    }
    applied.push_back(id);
  }

  // the expectations of a template with the ones it extends (parents first); a cycle or an unknown parent is said and cut
  struct Exp { std::string tpl; Json e; };
  std::function<void(const std::string&, std::vector<std::string>&, std::vector<Exp>&, std::set<std::string>&)> collect;
  collect = [&](const std::string& id, std::vector<std::string>& stack, std::vector<Exp>& out, std::set<std::string>& done) {
    if (std::find(stack.begin(), stack.end(), id) != stack.end()) {
      std::string chain;
      for (const auto& s : stack) chain += s + " → ";
      advise("extends_cycle", Json{{"chain", chain + id}}, stack.front());
      return;
    }
    if (!done.insert(id).second) return;  // a diamond: the shared parent once
    const Json* t = json::find(templates, id);
    if (!t || !t->is_object()) {
      advise("unknown_extends", Json{{"other", id}}, stack.empty() ? id : stack.back());
      return;
    }
    stack.push_back(id);
    const Json parents = t->value("extends", Json::array());
    if (parents.is_array()) {
      for (const auto& p : parents)
        if (p.is_string()) collect(p.get<std::string>(), stack, out, done);
    } else if (parents.is_string()) {
      collect(parents.get<std::string>(), stack, out, done);
    }
    stack.pop_back();
    const Json expects = t->value("expects", Json::array());
    if (expects.is_array())
      for (const auto& e : expects) out.push_back(Exp{id, e});
  };

  Json templates_applied = Json::array();
  Json expectations = Json::array();
  Json ghosts = Json::array();
  std::map<std::string, Json> expected_by;  // child address -> ["template/expect"]
  std::int64_t present = 0, missing = 0;
  for (const auto& id : applied) {
    const Json& t = templates.at(id);
    check_fields(t, template_fields(), id);
    Json row{{"id", id}, {"label", label_of(t, id)}, {"example", json::get_bool(t, "example", false)},
             {"source_ref", "runtime/structure_templates.pack#/templates/" + id}};
    if (t.contains("description")) row["description"] = words_of(t.at("description"), lang);
    if (t.contains("derived_from")) row["derived_from"] = t.at("derived_from");
    templates_applied.push_back(row);
    std::vector<Exp> list;
    std::vector<std::string> stack;
    std::set<std::string> done;
    collect(id, stack, list, done);
    if (list.empty()) advise("empty_expects", Json{{"template", label_of(t, id)}}, id);
    std::map<std::string, std::string> seen_match, seen_id;
    for (std::size_t i = 0; i < list.size(); ++i) {
      const Json& e = list[i].e;
      const std::string owner = list[i].tpl;
      if (!e.is_object()) { advise("unknown_field", Json{{"field", std::string("<") + e.type_name() + ">"}}, owner); continue; }
      check_fields(e, expect_fields(), owner);
      std::string eid = json::get_string(e, "id");
      if (eid.empty()) eid = "#" + std::to_string(i);
      const std::string elabel = label_of(e, eid);
      if (!seen_id.emplace(owner + "/" + eid, eid).second) advise("duplicate_expect_id", Json{{"template", label_of(t, id)}, {"expect", eid}}, id, eid);
      const Json cm = e.value("child_match", Json());
      const std::string key = json::canonical(cm);
      if (auto [it, fresh] = seen_match.emplace(key, elabel); !fresh)
        advise("duplicate_expects", Json{{"template", label_of(t, id)}, {"expect", elabel}, {"other", it->second}}, id, eid);
      // the cardinality: a preset name or {min, max}; max null = no upper bound
      std::int64_t mn = 0;
      std::optional<std::int64_t> mx;
      Json card = e.value("card", Json(default_card));
      if (card.is_string()) {
        const std::string name = card.get<std::string>();
        if (const Json* pr = json::find(presets, name)) card = *pr;
        else {
          advise("unknown_card", Json{{"template", label_of(t, id)}, {"expect", elabel}, {"card", name}, {"fallback", default_card}}, id, eid);
          card = presets.value(default_card, Json{{"min", 0}, {"max", nullptr}});
        }
      }
      if (card.is_object()) {
        if (card.contains("min") && card.at("min").is_number()) mn = std::max<std::int64_t>(0, card.at("min").get<std::int64_t>());
        if (card.contains("max") && card.at("max").is_number()) mx = card.at("max").get<std::int64_t>();
      }
      if (mx && *mx < mn) advise("card_inverted", Json{{"template", label_of(t, id)}, {"expect", elabel}, {"min", mn}, {"max", *mx}}, id, eid);
      // the children it picks
      Json matched = Json::array();
      bool only_inferred = true;
      for (const auto& c : children) {
        if (json::get_int(c, "depth", 1) != 1) continue;
        if (!predicate_ok(cm, c, owner, eid)) continue;
        const std::string addr = json::get_string(c, "address");
        matched.push_back(addr);
        expected_by[addr].push_back(owner + "/" + eid);
        const std::string cls = json::get_string(c.value("evidence", Json::object()), "class");
        if (!inferred_classes.count(cls)) only_inferred = false;
      }
      const auto count = static_cast<std::int64_t>(matched.size());
      const std::string ref = owner + "/" + eid;
      std::map<std::string, bool> facts{{"declined", declined.count(ref) > 0}, {"below_min", count < mn}, {"above_max", mx && count > *mx},
                                        {"only_inferred", count > 0 && only_inferred}, {"any", count > 0}, {"always", true}};
      std::string state = "unknown", state_label;
      bool ghost = false;
      for (const auto& s : states) {
        const std::string when = json::get_string(s, "when");
        auto f = facts.find(when);
        if (f == facts.end()) { advise("unknown_state_rule", Json{{"field", when}}, "states"); continue; }
        if (!f->second) continue;
        state = json::get_string(s, "id", when);
        state_label = words_of(s.value("label", Json()), lang);
        ghost = json::get_bool(s, "ghost", false);
        break;
      }
      if (state_label.empty()) state_label = state;
      Json x{{"template", owner}, {"applied_by", id}, {"id", eid}, {"ref", ref}, {"label", elabel}, {"relation", e.value("relation", Json("part_of"))},
             {"card", Json{{"min", mn}, {"max", mx ? Json(*mx) : Json(nullptr)}}}, {"child_match", cm}, {"matched", matched}, {"count", count},
             {"state", state}, {"state_label", state_label}, {"confidence", e.value("confidence", Json(nullptr))},
             {"source", e.value("source", Json("template"))}, {"source_ref", "runtime/structure_templates.pack#/templates/" + owner}};
      if (e.contains("fill")) x["fill"] = e.at("fill");
      if (ghost) {
        ++missing;
        const std::string addr = fill(json::get_string(data, "ghost_address", "{root}#expect/{template}/{expect}"),
                                      Json{{"root", root_address}, {"template", owner}, {"expect", eid}});
        x["ghost"] = addr;
        if (show_expected)
          ghosts.push_back(Json{{"address", addr}, {"label", elabel}, {"ghost", true}, {"stored", false}, {"state", state}, {"state_label", state_label},
                                {"expected", Json{{"relation", x.at("relation")}, {"card", x.at("card")}, {"child_match", cm}}},
                                {"confidence", x.at("confidence")}, {"source", x.at("source")}, {"template", owner}, {"expect", eid}, {"parent", root_address}});
      } else if (count > 0) {
        ++present;
      }
      expectations.push_back(std::move(x));
    }
  }
  if (applied.empty() && !explicit_list) advise("no_template", Json::object(), "");

  Json kids = Json::array();
  std::int64_t unexpected = 0;
  for (const auto& c : children) {
    Json k = c;
    const std::string addr = json::get_string(c, "address");
    auto it = expected_by.find(addr);
    k["expected_by"] = it == expected_by.end() ? Json::array() : it->second;
    if (it == expected_by.end() && json::get_int(c, "depth", 1) == 1) ++unexpected;
    kids.push_back(std::move(k));
  }
  return Json{{"templates_applied", templates_applied}, {"expectations", expectations}, {"ghosts", ghosts}, {"children", kids},
              {"show_expected", show_expected}, {"advisories", advisories},
              {"counts", Json{{"children", kids.size()}, {"expectations", expectations.size()}, {"present", present}, {"missing", missing},
                              {"ghosts", ghosts.size()}, {"unexpected", unexpected}, {"advisories", advisories.size()}}}};
}

namespace {

std::string text_of(const Json& data, const std::string& id, const std::string& lang, const Json& vars) {
  const Json texts = data.value("texts", Json::object());
  return fill(words_of(texts.value(id, Json(id)), lang), vars);
}

Json suggestions(const projection::Env& env, const Json& data) {
  Json out = Json::array();
  const std::int64_t limit = json::get_int(data, "root_suggestions_limit", 12);
  for (const auto& a : data.value("root_suggestions", Json::array())) {
    if (!a.is_string()) continue;
    auto r = projection::navigate(env, Json{{"focus", a}, {"limit", limit}});
    if (!r) {
      out.push_back(Json{{"address", a}, {"unknown", r.error().message}});
      continue;
    }
    const Json focus = r->value("focus", Json::object());
    Json items = Json::array();
    for (const auto& c : r->value("children", Json::array()))
      items.push_back(Json{{"address", c.value("address", "")}, {"label", c.value("label", "")}, {"kind", c.value("kind", "")}});
    out.push_back(Json{{"address", a}, {"label", focus.value("label", "")}, {"items", items},
                       {"total", r->value("counts", Json::object()).value("total", items.size())}});
  }
  return out;
}


// Children graph.navigate does not list (data: member_readers). "conversations.units": the conversations whose metadata.units names the
// unit of the root (unit:<kind>/<id>), each as the navigator's own row of that conversation (graph.navigate on it, budget 1).
Result<Json> read_members(const projection::Env& env, const std::string& reader, const std::string& root, const std::string& prefix, Json& reader_notes) {
  Json out = Json::array();
  if (reader != "conversations.units") return Error(Errc::NotFound, "unknown member reader '" + reader + "'");
  if (!env.db) return Error(Errc::Unavailable, "database_unavailable");
  const std::string rest = root.substr(prefix.size());
  const std::size_t slash = rest.find('/');
  if (slash == std::string::npos) return out;
  const std::string kind = rest.substr(0, slash), id = rest.substr(slash + 1);
  std::vector<std::string> ids;
  {
    auto lock = env.db->lock();
    LOOM_TRY_ASSIGN(auto st, env.db->conn().prepare("SELECT id, metadata, json_valid(metadata) FROM conversations WHERE CASE WHEN metadata IS NULL THEN 0 WHEN json_valid(metadata) THEN json_extract(metadata, '$.units') IS NOT NULL ELSE 1 END ORDER BY created"));
    while (true) {
      LOOM_TRY_ASSIGN(bool more, st.step());
      if (!more) break;
      if (!st.get_int(2)) {
        reader_notes.push_back(Json{{"reader", reader}, {"error", "conversation_metadata_unreadable"}, {"address", "conv:" + st.get_text(0)}});
        continue;
      }
      const Json meta = json::parse_or(st.get_text(1), Json::object());
      const Json units = meta.is_object() ? meta.value("units", Json::object()) : Json::object();
      if (units.is_object() && json::get_string(units, kind) == id) ids.push_back(st.get_text(0));
    }
  }
  for (const auto& cid : ids) {
    auto nav = projection::navigate(env, Json{{"focus", cid}, {"limit", 1}});
    Json row = nav ? nav->value("focus", Json::object()) : Json{{"address", cid}, {"label", cid}, {"unknown", nav.error().message}};
    row["depth"] = 1;
    row["via"] = reader;
    out.push_back(std::move(row));
  }
  return out;
}

}  // namespace

Result<Json> structure(const projection::Env& env, const Json& request) {
  const std::string lang = env.lang;
  Json data = Json::object();
  Json base{{"schema", "loom.structure/1"}};
  try {
    auto prof = RuntimeProfile::load("structure_templates", env.root);
    if (prof) data = prof->values();
    else {
      // an unreadable overlay of the person: the presets, and the reason said (never silently)
      base["profile_error"] = prof.error().message;
      if (auto b = RuntimeProfile::builtin("structure_templates")) data = b->values();
    }
    std::string root = json::get_string(request, "root", json::get_string(request, "focus"));
    Json req = request;
    req["lang"] = lang;
    if (root.empty()) {
      base["found"] = false;
      base["reason"] = "root_empty";
      base["reason_human"] = text_of(data, "root_empty", lang, Json::object());
      base["suggestions"] = suggestions(env, data);
      return base;
    }
    // the words of a sentence („strukturę projektu Alfa”): a name is resolved to an address as the navigator does; an address is used as is
    std::string said;
    if (root.find(':') == std::string::npos) {
      if (auto found = projection::find_address(env, root); found && !found->empty()) {
        said = root;
        root = *found;
      }
    }
    Json nav_req{{"focus", root}};
    nav_req["hops"] = std::max<std::int64_t>(1, json::get_int(request, "depth", 1));
    if (request.contains("budget") && request.at("budget").is_number()) nav_req["limit"] = request.at("budget");
    else if (const std::int64_t l = json::get_int(data, "children_limit", 0); l > 0) nav_req["limit"] = l;
    else nav_req["limit"] = 0;
    auto nav = projection::navigate(env, nav_req);
    if (!nav) {
      base["found"] = false;
      base["root"] = Json{{"address", root}};
      base["reason"] = "root_missing";
      base["reason_human"] = text_of(data, "root_missing", lang, Json{{"root", root}});
      base["error"] = nav.error().message;
      return base;
    }
    const Json focus = nav->value("focus", Json::object());
    Json children = nav->value("children", Json::array());
    Json reader_notes = Json::array();
    const std::string focus_address = json::get_string(focus, "address", root);
    for (const auto& m : data.value("member_readers", Json::array())) {
      const std::string pre = json::get_string(m, "root_prefix");
      if (pre.empty() || focus_address.rfind(pre, 0) != 0) continue;
      auto extra = read_members(env, json::get_string(m, "reader"), focus_address, pre, reader_notes);
      if (!extra) { reader_notes.push_back(Json{{"reader", json::get_string(m, "reader")}, {"error", extra.error().message}}); continue; }
      std::set<std::string> have;
      for (const auto& c : children) have.insert(json::get_string(c, "address"));
      for (const auto& e : *extra)
        if (have.insert(json::get_string(e, "address")).second) children.push_back(e);
    }
    Json out = evaluate(data, focus, children, req);
    out["schema"] = "loom.structure/1";
    out["found"] = true;
    out["root"] = focus;
    if (!said.empty()) out["root_said"] = said;
    out["navigate"] = Json{{"counts", nav->value("counts", Json::object())}, {"breadcrumbs", nav->value("breadcrumbs", Json::array())},
                           {"limit", nav->value("limit", Json::object())}, {"edges", nav->value("edges", Json::array())}};
    if (base.contains("profile_error")) out["profile_error"] = base.at("profile_error");
    if (!reader_notes.empty()) out["reader_errors"] = reader_notes;
    const Json counts = out.at("counts");
    out["text"] = text_of(data, "head", lang, Json{{"label", focus.value("label", root)}, {"present", counts.value("present", 0)}, {"missing", counts.value("missing", 0)}});
    return out;
  } catch (const std::exception& e) {
    // never an exception to the caller: the reason in the answer
    log::write(log::Level::Warning, "loom.graph.structure", std::string("graph.structure: ") + e.what());
    base["found"] = false;
    base["reason"] = "internal";
    base["reason_human"] = e.what();
    return base;
  }
}

}  // namespace loom::structure
