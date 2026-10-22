// M1-K2c-1: see unit_ops.h.
#include "unit_ops.h"

#include <algorithm>
#include <limits>
#include <map>
#include <vector>

#include "loom/effective_view.h"
#include "loom/log.h"
#include "loom/util/utf8.h"

namespace loom::actions::units {

namespace {

constexpr const char* kLog = "units";
constexpr const char* kNodeKind = "unit";

std::string trim(std::string s) {
  const auto blank = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  while (!s.empty() && blank(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && blank(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string arg_string(const Json& args, const char* name) {
  const Json* v = json::find(args, name);
  if (!v) return std::string();
  if (v->is_string()) return trim(v->get<std::string>());
  if (v->is_number() || v->is_boolean()) return trim(json::dump(*v));
  return std::string();
}

// A kind of the effective view unit_kinds, labelled in the language of the run.
struct Kind {
  std::string id;
  std::string label;
};

std::string pick(const Json& labels, const std::string& lang, const std::string& fallback) {
  for (const std::string& l : {lang, std::string("en")})
    if (const Json* s = json::find(labels, l); s && s->is_string() && !s->get<std::string>().empty()) return s->get<std::string>();
  return fallback;
}

std::vector<Kind> kinds_in(const Env& env, const views::Effective& eff) {
  std::vector<Kind> out;
  for (const auto& e : eff.entries) {
    if (e.disabled) continue;
    out.push_back(Kind{e.id, pick(e.value.value("label", Json::object()), env.lang, e.id)});
  }
  return out;
}

Result<std::vector<Kind>> kinds_of(const Env& env) {
  LOOM_TRY_ASSIGN(auto eff, views::effective(env.root, "unit_kinds"));
  return kinds_in(env, eff);
}

std::string kinds_text(const std::vector<Kind>& kinds) {
  std::string out;
  for (const auto& k : kinds) out += (out.empty() ? "" : ", ") + k.label;
  return out;
}

// The kind the person named: by its id or by its label in any language, letters folded.
Result<Kind> kind_of(const Env& env, const std::string& given) {
  LOOM_TRY_ASSIGN(auto eff, views::effective(env.root, "unit_kinds"));
  const std::vector<Kind> kinds = kinds_in(env, eff);
  if (given.empty()) return Error(Errc::InvalidArgument, env.say("unit_kind_missing", Json{{"kinds", kinds_text(kinds)}}));
  const std::string want = env.fold(given);
  for (const auto& e : eff.entries) {
    if (e.disabled) continue;
    bool match = env.fold(e.id) == want;
    const Json labels = e.value.value("label", Json::object());
    if (labels.is_object())
      for (auto it = labels.begin(); it != labels.end() && !match; ++it)
        if (it.value().is_string() && env.fold(it.value().get<std::string>()) == want) match = true;
    if (match) return Kind{e.id, pick(labels, env.lang, e.id)};
  }
  return Error(Errc::InvalidArgument, env.say("unit_kind_unknown", Json{{"kind", given}, {"kinds", kinds_text(kinds)}}));
}

Status valid_id(const Env& env, const std::string& id) {
  const bool control = std::any_of(id.begin(), id.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
  if (id.empty() || control || id.find('/') != std::string::npos) return Error(Errc::InvalidArgument, env.say("unit_id_invalid", Json::object()));
  return ok_status();
}

// The units of a conversation: metadata.units when it is an object (anything else is not a map of units).
Json units_of(const Conversation& c) {
  if (c.metadata.is_object())
    if (const Json* u = json::find(c.metadata, "units"); u && u->is_object()) return *u;
  return Json::object();
}

bool assigned(const Conversation& c, const std::string& kind, const std::string& id) {
  const Json u = units_of(c);
  const Json* v = json::find(u, kind);
  return v && v->is_string() && v->get<std::string>() == id;
}

Result<std::vector<Conversation>> all_convs(const Env& env) { return env.db.list_convs(std::numeric_limits<int>::max()); }

// Writes the units of a conversation back through a MERGE: every other key of the metadata stays as it was (flow, settings, interview ...).
// `units` empty removes the key, so a conversation that never had units does not keep an empty map after a clear.
Status write_units(const Env& env, const Conversation& c, const Json& units) {
  Json meta = c.metadata.is_object() ? c.metadata : Json::object();
  if (units.empty()) meta.erase("units");
  else meta["units"] = units;
  ConvPatch patch;
  patch.metadata = std::move(meta);
  patch.keep_updated = true;
  return env.db.update_conv(c.id, patch);
}

Result<Conversation> conv_of(const Env& env, const Json& args) {
  std::string id = arg_string(args, "conv_id");
  if (id.empty()) id = env.open_conv;
  if (id.empty()) return Error(Errc::InvalidArgument, env.say("unit_no_conversation", Json::object()));
  LOOM_TRY_ASSIGN(auto found, env.db.get_conv(id));
  if (!found) return Error(Errc::NotFound, env.say("unit_conv_missing", Json{{"id", id}}));
  return *found;
}

std::string title_of(const Conversation& c) { return c.title.empty() ? c.id : c.title; }

// A unit node: its kind and id from the metadata, or from its own id when an older writer left them out.
struct UnitNode {
  std::string kind, id, label;
};

std::optional<UnitNode> unit_node(const Node& n) {
  UnitNode u;
  u.kind = json::get_string(n.metadata, "unit_kind");
  u.id = json::get_string(n.metadata, "unit_id");
  if (u.kind.empty() || u.id.empty()) {
    const std::string prefix = "unit:";
    if (n.id.rfind(prefix, 0) != 0) return std::nullopt;
    const std::string rest = n.id.substr(prefix.size());
    const auto slash = rest.find('/');
    if (slash == std::string::npos) return std::nullopt;
    u.kind = rest.substr(0, slash);
    u.id = rest.substr(slash + 1);
  }
  u.label = n.label.empty() ? u.id : n.label;
  return u;
}

Result<std::optional<Node>> get_unit(const Env& env, const std::string& kind, const std::string& id) {
  LOOM_TRY_ASSIGN(auto n, env.db.get_node(node_id(kind, id)));
  return n;
}

std::string kind_label(const std::vector<Kind>& kinds, const std::string& kind) {
  for (const auto& k : kinds)
    if (k.id == kind) return k.label;
  return kind;
}

}  // namespace

std::string node_id(const std::string& kind, const std::string& id) { return "unit:" + kind + "/" + id; }

Result<Reply> define(const Env& env, const Json& args) {
  LOOM_TRY_ASSIGN(auto kind, kind_of(env, arg_string(args, "kind")));
  const std::string label_given = arg_string(args, "label");
  std::string id = arg_string(args, "id");
  if (id.empty()) id = label_given;  // a name said once is both the id and the label
  LOOM_TRY(valid_id(env, id));
  LOOM_TRY_ASSIGN(auto existing, get_unit(env, kind.id, id));
  const std::string nid = node_id(kind.id, id);
  Json data{{"node", nid}, {"kind", kind.id}, {"kind_label", kind.label}, {"id", id}};
  if (!existing) {
    const std::string label = label_given.empty() ? id : label_given;
    NodeOptions opts;
    opts.node_id = nid;
    opts.metadata = Json{{"unit_kind", kind.id}, {"unit_id", id}};
    LOOM_TRY_ASSIGN(auto made, env.db.create_node(label, kNodeKind, opts));
    (void)made;
    data["label"] = label;
    data["created"] = true;
    return Reply{env.say("unit_defined", Json{{"kind", kind.label}, {"label", label}}), data};
  }
  const std::string was = existing->label.empty() ? id : existing->label;
  data["created"] = false;
  if (!label_given.empty() && label_given != was) {
    // The run is already confirmed (confirm:units.define); only the label changes, the id and the assignments stay.
    NodePatch patch;
    patch.label = label_given;
    LOOM_TRY(env.db.update_node(nid, patch));
    data["label"] = label_given;
    data["previous_label"] = was;
    return Reply{env.say("unit_relabelled", Json{{"kind", kind.label}, {"label", label_given}, {"was", was}}), data};
  }
  data["label"] = was;
  return Reply{env.say("unit_exists", Json{{"kind", kind.label}, {"label", was}}), data};
}

Result<Reply> list(const Env& env, const Json& args) {
  LOOM_TRY_ASSIGN(auto kinds, kinds_of(env));
  std::string only;
  if (const std::string given = arg_string(args, "kind"); !given.empty()) {
    LOOM_TRY_ASSIGN(auto k, kind_of(env, given));
    only = k.id;
  }
  LOOM_TRY_ASSIGN(auto nodes, env.db.list_nodes(std::string_view(kNodeKind), std::numeric_limits<int>::max()));
  LOOM_TRY_ASSIGN(auto convs, all_convs(env));
  std::map<std::string, int> counts;  // "kind/id" -> conversations
  for (const auto& c : convs) {
    const Json u = units_of(c);
    for (auto it = u.begin(); it != u.end(); ++it)
      if (it.value().is_string()) ++counts[it.key() + "/" + it.value().get<std::string>()];
  }
  struct Row {
    UnitNode unit;
    bool defined = true;
    int conversations = 0;
  };
  std::vector<Row> rows;
  std::map<std::string, bool> seen;
  for (const auto& n : nodes) {
    auto u = unit_node(n);
    if (!u || (!only.empty() && u->kind != only)) continue;
    const std::string key = u->kind + "/" + u->id;
    seen[key] = true;
    rows.push_back(Row{*u, true, counts.count(key) ? counts[key] : 0});
  }
  // Units that conversations point to but the registry does not know: shown, not hidden (the scope chain reads them all the same).
  for (const auto& [key, n] : counts) {
    if (seen.count(key)) continue;
    const auto slash = key.find('/');
    UnitNode u{key.substr(0, slash), key.substr(slash + 1), key.substr(slash + 1)};
    if (!only.empty() && u.kind != only) continue;
    rows.push_back(Row{u, false, n});
  }
  std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
    if (a.unit.kind != b.unit.kind) return a.unit.kind < b.unit.kind;
    return a.unit.label < b.unit.label;
  });
  Json items = Json::array();
  std::vector<std::string> lines;
  for (const auto& r : rows) {
    const std::string kl = kind_label(kinds, r.unit.kind);
    items.push_back(Json{{"node", node_id(r.unit.kind, r.unit.id)}, {"kind", r.unit.kind}, {"kind_label", kl}, {"id", r.unit.id}, {"label", r.unit.label},
                         {"conversations", r.conversations}, {"defined", r.defined}});
    lines.push_back("• " + env.say(r.defined ? "unit_list_line" : "unit_list_line_undefined",
                                    Json{{"kind", kl}, {"label", r.unit.label}, {"conversations", r.conversations}}));
  }
  Json kinds_json = Json::array();
  for (const auto& k : kinds) kinds_json.push_back(Json{{"id", k.id}, {"label", k.label}});
  Json data{{"items", items}, {"kinds", kinds_json}};
  if (rows.empty()) return Reply{env.say("unit_list_none", Json{{"kinds", kinds_text(kinds)}}), data};
  std::string text = env.say("unit_list_head", Json{{"count", rows.size()}});
  for (const auto& l : lines) text += "\n" + l;
  return Reply{text, data};
}

Result<Reply> set_unit(const Env& env, const Json& args) {
  LOOM_TRY_ASSIGN(auto kind, kind_of(env, arg_string(args, "kind")));
  const std::string id = arg_string(args, "id");
  LOOM_TRY(valid_id(env, id));
  LOOM_TRY_ASSIGN(auto conv, conv_of(env, args));
  LOOM_TRY_ASSIGN(auto unit, get_unit(env, kind.id, id));
  const bool defined = unit.has_value();
  const std::string label = defined && !unit->label.empty() ? unit->label : id;
  Json data{{"conv_id", conv.id}, {"kind", kind.id}, {"kind_label", kind.label}, {"id", id}, {"label", label}, {"defined", defined}};
  if (assigned(conv, kind.id, id)) {
    data["changed"] = false;
    return Reply{env.say("unit_set_same", Json{{"title", title_of(conv)}, {"kind", kind.label}, {"label", label}}), data};
  }
  Json units = units_of(conv);
  if (const Json* old = json::find(units, kind.id); old && old->is_string()) data["previous"] = *old;
  units[kind.id] = id;
  LOOM_TRY(write_units(env, conv, units));
  data["changed"] = true;
  std::string text = env.say("unit_set", Json{{"title", title_of(conv)}, {"kind", kind.label}, {"label", label}});
  if (!defined) text += " " + env.say("unit_set_undefined", Json::object());
  return Reply{text, data};
}

Result<Reply> clear_unit(const Env& env, const Json& args) {
  LOOM_TRY_ASSIGN(auto kind, kind_of(env, arg_string(args, "kind")));
  LOOM_TRY_ASSIGN(auto conv, conv_of(env, args));
  Json units = units_of(conv);
  Json data{{"conv_id", conv.id}, {"kind", kind.id}, {"kind_label", kind.label}};
  const Json* old = json::find(units, kind.id);
  if (!old) {
    data["changed"] = false;
    return Reply{env.say("unit_clear_none", Json{{"title", title_of(conv)}, {"kind", kind.label}}), data};
  }
  data["previous"] = *old;
  units.erase(kind.id);
  LOOM_TRY(write_units(env, conv, units));
  data["changed"] = true;
  return Reply{env.say("unit_cleared", Json{{"title", title_of(conv)}, {"kind", kind.label}}), data};
}

Result<Reply> forget(const Env& env, const Json& args) {
  LOOM_TRY_ASSIGN(auto kind, kind_of(env, arg_string(args, "kind")));
  const std::string id = arg_string(args, "id");
  LOOM_TRY(valid_id(env, id));
  LOOM_TRY_ASSIGN(auto unit, get_unit(env, kind.id, id));
  LOOM_TRY_ASSIGN(auto convs, all_convs(env));
  std::vector<Conversation> affected;
  for (const auto& c : convs)
    if (assigned(c, kind.id, id)) affected.push_back(c);
  if (!unit && affected.empty()) return Error(Errc::NotFound, env.say("unit_forget_none", Json{{"kind", kind.label}, {"id", id}}));
  const std::string label = unit && !unit->label.empty() ? unit->label : id;
  // The assignments first, the node last: when a write fails midway, asking again finishes the job (the node is still there and still counts).
  Json ids = Json::array();
  for (const auto& c : affected) {
    Json units = units_of(c);
    units.erase(kind.id);
    LOOM_TRY(write_units(env, c, units));
    ids.push_back(c.id);
  }
  if (unit) {
    LOOM_TRY(env.db.delete_node(node_id(kind.id, id)));
  }
  log::write(log::Level::Info, kLog, "unit " + node_id(kind.id, id) + " forgotten, " + std::to_string(affected.size()) + " conversation(s) unassigned");
  Json data{{"node", node_id(kind.id, id)}, {"kind", kind.id}, {"kind_label", kind.label}, {"id", id}, {"label", label},
            {"removed_node", unit.has_value()}, {"conversations", affected.size()}, {"conv_ids", ids}};
  return Reply{env.say("unit_forgotten", Json{{"kind", kind.label}, {"label", label}, {"conversations", affected.size()}}), data};
}

}  // namespace loom::actions::units
