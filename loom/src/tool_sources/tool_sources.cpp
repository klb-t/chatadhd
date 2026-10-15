// Tool sources: configuration, listing into the user overlay, trust rules, graph nodes, and the port call. See include/loom/tool_sources.h.
#include "loom/tool_sources.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

#include "loom/action_overlay.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/port_graph.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "internal.h"

namespace loom::tool_sources {
namespace {

constexpr const char* kLog = "loom.tool_sources";

const Json& object_or_empty(const Json* j) {
  static const Json kEmpty = Json::object();
  return j && j->is_object() ? *j : kEmpty;
}

std::string sanitize(const std::string& s) {
  std::string out;
  for (unsigned char c : s) out += (std::isalnum(c) || c == '_' || c == '.' || c == '-') ? static_cast<char>(c) : '_';
  return out;
}

bool valid_source_id(const std::string& id) {
  if (id.empty()) return false;
  return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

Json sources_of(Runtime& rt) {
  Json s = rt.config().get("tool_sources", Json::object());
  return s.is_object() ? s : Json::object();
}

Status save_sources(Runtime& rt, const Json& sources) {
  rt.config().set("tool_sources", sources);
  return rt.config().save();
}

// A neutral rendering of a url for graph nodes and answers: no user info, no query, no fragment.
std::string clean_url(const std::string& url) {
  std::string u = url;
  if (auto q = u.find_first_of("?#"); q != std::string::npos) u.erase(q);
  if (auto s = u.find("://"); s != std::string::npos) {
    if (auto at = u.find('@', s + 3); at != std::string::npos && u.find('/', s + 3) > at) u.erase(s + 3, at - s - 2);
  }
  return u;
}

// What the settings may hold: references, never values.
Status validate_source(const std::string& id, const Json& cfg) {
  if (!valid_source_id(id)) return Error(Errc::InvalidArgument, "source id must be letters, digits, '_' or '-'");
  if (!cfg.is_object()) return Error(Errc::InvalidArgument, "a source is an object");
  const std::string kind = json::get_string(cfg, "kind");
  if (!registry().contains(kind)) return Error(Errc::Unsupported, registry().unknown_message(kind, id));
  const std::string transport = json::get_string(cfg, "transport");
  if (transport == "stdio" && !json::find(cfg, "command")) return Error(Errc::InvalidArgument, "a stdio source needs command");
  if (transport == "http" && json::get_string(cfg, "url").empty()) return Error(Errc::InvalidArgument, "an http source needs url");
  if (transport != "stdio" && transport != "http") return Error(Errc::InvalidArgument, "transport is stdio or http");
  if (const Json* a = json::find(cfg, "access")) {
    if (!a->is_object()) return Error(Errc::InvalidArgument, "access is {secret: <vault key name>, header?, scheme?}");
    for (auto it = a->begin(); it != a->end(); ++it)
      if (it.key() != "secret" && it.key() != "header" && it.key() != "scheme")
        return Error(Errc::InvalidArgument, "access holds a reference to the vault (secret: <key name>), not the key itself: '" + it.key() + "' is not allowed");
  }
  return Status{};
}

std::vector<std::string> secret_refs(const Json& cfg) {
  std::vector<std::string> refs;
  if (const Json* a = json::find(cfg, "access"); a && a->is_object())
    if (const std::string s = json::get_string(*a, "secret"); !s.empty()) refs.push_back(s);
  if (const Json* e = json::find(cfg, "env"); e && e->is_object())
    for (auto it = e->begin(); it != e->end(); ++it)
      if (it.value().is_object())
        if (const std::string s = json::get_string(it.value(), "secret"); !s.empty()) refs.push_back(s);
  return refs;
}

// ── the user overlay: actions and rules ───────────────────────────────────────────────────────────

std::string rule_prefix(const Json& profile, const std::string& source) {
  return json::get_string(object_or_empty(json::find(profile, "rule")), "prefix", "tool_source") + ":" + source + ":";
}

std::string param_type(const Json& schema) {
  const Json* t = json::find(schema, "type");
  std::string type;
  if (t && t->is_string()) type = t->get<std::string>();
  else if (t && t->is_array())
    for (const auto& e : *t)
      if (e.is_string() && e.get<std::string>() != "null") { type = e.get<std::string>(); break; }
  static const std::set<std::string> kKnown{"string", "number", "integer", "boolean", "object", "array"};
  return kKnown.count(type) ? type : "string";
}

Json params_of(const Json& input_schema) {
  Json out = Json::array();
  std::set<std::string> required;
  if (const Json* r = json::find(input_schema, "required"); r && r->is_array())
    for (const auto& e : *r) if (e.is_string()) required.insert(e.get<std::string>());
  const Json* props = json::find(input_schema, "properties");
  if (!props || !props->is_object()) return out;
  for (auto it = props->begin(); it != props->end(); ++it) {
    const Json& s = it.value();
    const std::string desc = s.is_object() ? json::get_string(s, "description") : std::string();
    Json p{{"name", it.key()}, {"type", s.is_object() ? param_type(s) : "string"}, {"required", required.count(it.key()) > 0},
           {"description", Json{{"en", desc}, {"pl", desc}}}, {"has_default", s.is_object() && s.contains("default")}, {"edit", "free_text"}};
    if (s.is_object() && s.contains("default")) p["default"] = s["default"];
    if (s.is_object() && s.contains("enum")) p["enum"] = s["enum"];
    out.push_back(std::move(p));
  }
  return out;
}

Json merged(Json base, const Json& over) {
  if (!over.is_object()) return base;
  for (auto it = over.begin(); it != over.end(); ++it) base[it.key()] = it.value();
  return base;
}

Json entry_of(const Json& profile, const std::string& source, const Json& cfg, const ToolDecl& t, const std::string& kind_label_en) {
  const Json& tmpl = object_or_empty(json::find(profile, "action_template"));
  const std::string port = json::get_string(profile, "port", kPort);
  const std::string prefix = json::get_string(profile, "action_prefix", "tool");
  const std::string id = prefix + "." + sanitize(source) + "." + sanitize(t.name);
  Json effects = merged(object_or_empty(json::find(profile, "unknown_effects")), json::find(cfg, "effects") ? *json::find(cfg, "effects") : Json());
  if (const Json* tc = json::find(object_or_empty(json::find(cfg, "tools")), t.name))
    effects = merged(effects, object_or_empty(json::find(*tc, "effects")));
  const std::string title = t.title.empty() ? t.name : t.title;
  const std::string label = json::get_string(cfg, "label", source);
  const std::string desc = (t.description.empty() ? title : t.description) + " [" + kind_label_en + " '" + label + "'; effects unknown until you say otherwise]";
  Json e = tmpl.is_object() ? tmpl : Json::object();
  e["id"] = id;
  e["kind"] = "atomic";
  e["status"] = "implemented";
  e["title"] = title + " (" + label + ")";
  e["description"] = desc;
  e["labels"] = Json{{"pl", title + " (" + label + ")"}};
  e["inputs"] = Json::array({"json"});
  e["outputs"] = Json::array({"json"});
  if (!e.contains("requires")) e["requires"] = Json::array({"application"});
  e["produces"] = Json::array();
  e["content_op"] = "none";
  e["form_op"] = "none";
  e["effects"] = effects;
  e["consumer"] = "loom/src/tool_sources";
  e["port"] = port;
  e["action"] = "call:" + source + ":" + t.name;
  e["params"] = params_of(t.input_schema);
  e["stores_result"] = false;
  e["result_text"] = Json{{"en", "Result of " + title + ":"}, {"pl", "Wynik: " + title + ":"}};
  e["tool_source"] = source;
  e["tool"] = t.name;
  e["hints"] = t.annotations;  // the server's claims; recorded, not believed
  e["input_schema"] = t.input_schema;
  return e;
}

Result<Json> overlay_object(const actions::OverlayStore& o, const char* domain, const char* key) {
  LOOM_TRY_ASSIGN(Json cur, o.entry(domain, {key}));
  return cur.is_object() ? cur : Json::object();
}

// Own actions of a source in the overlay: id -> entry.
std::vector<std::string> own_action_ids(const Json& overlay_actions, const std::string& source) {
  std::vector<std::string> ids;
  for (auto it = overlay_actions.begin(); it != overlay_actions.end(); ++it)
    if (it.value().is_object() && json::get_string(it.value(), "tool_source") == source) ids.push_back(it.key());
  return ids;
}

// ── graph ──────────────────────────────────────────────────────────────────────────────────────────

struct GraphNames {
  std::string env, cap, access;
};
GraphNames names_of(const Json& profile, const std::string& source) {
  const Json& g = object_or_empty(json::find(profile, "graph"));
  const std::string p = json::get_string(g, "node_prefix", "tool_source");
  return {"environment:service:" + p + ":" + source, "capability:service:" + p + ":" + source + ":tools",
          "access:service:" + p + ":" + source + ":vault"};
}

Result<std::string> put_node(Database& db, const std::string& label, const std::string& kind, Json metadata) {
  metadata = port_graph::redact(metadata);
  LOOM_TRY_ASSIGN(auto existing, db.find_node(label, kind));
  if (existing) {
    if (existing->metadata != metadata) {
      NodePatch patch;
      patch.metadata = metadata;
      LOOM_TRY(db.update_node(existing->id, patch));
    }
    return existing->id;
  }
  NodeOptions o;
  o.tags = Json::array({kind, "tool_source"});
  o.metadata = metadata;
  return db.get_or_create_node(label, kind, o);
}

Status put_link(Database& db, const std::string& src, const std::string& dst, const char* type) {
  auto made = db.create_link(src, dst, type, 1.0, Json::object());
  if (!made) return made.error();
  return Status{};
}

Status write_graph(Runtime& rt, const Json& profile, const std::string& source, const Json& cfg, const std::vector<ToolDecl>& tools,
                   const std::string& kind_label_en) {
  Database& db = rt.db();
  const Json& g = object_or_empty(json::find(profile, "graph"));
  const GraphNames n = names_of(profile, source);
  const std::string label = json::get_string(cfg, "label", source);
  Json names = Json::array();
  for (const auto& t : tools) names.push_back(t.name);
  Json meta{{"schema", "loom.environment/1"}, {"env_kind", "service"}, {"class", json::get_string(g, "environment_class", "service_environment")},
            {"service", "tool_source:" + source}, {"label", Json{{"en", label}, {"pl", label}}},
            {"tool_source", Json{{"id", source}, {"kind", json::get_string(cfg, "kind")}, {"transport", json::get_string(cfg, "transport")},
                                 {"url", clean_url(json::get_string(cfg, "url"))}, {"kind_label", kind_label_en}}}};
  LOOM_TRY_ASSIGN(const std::string env_id, put_node(db, n.env, "environment", std::move(meta)));
  const std::string port = json::get_string(g, "port", kPort);
  LOOM_TRY_ASSIGN(const std::string port_node, port_graph::port_node(db, port));
  Json cap{{"schema", "loom.capability/1"}, {"source", "tool_source"}, {"port", port},
           {"element_class", json::get_string(g, "element_class", "port")}, {"availability", "available"},
           {"label", Json{{"en", "Tools of " + label}, {"pl", "Narzędzia: " + label}}}, {"env", "service:tool_source:" + source},
           {"tools", names}};
  LOOM_TRY_ASSIGN(const std::string cap_id, put_node(db, n.cap, "capability", std::move(cap)));
  LOOM_TRY(put_link(db, env_id, cap_id, "has_capability"));
  LOOM_TRY(put_link(db, cap_id, port_node, "provided_by"));
  const auto refs_names = secret_refs(cfg);
  if (refs_names.empty()) {
    LOOM_TRY_ASSIGN(auto old, db.find_node(n.access, "access"));
    if (old) LOOM_TRY(db.delete_node(old->id));
    return Status{};
  }
  Json refs = Json::array();
  bool all_set = true;
  for (const auto& r : refs_names) {
    const bool set = rt.secrets().has(r);
    all_set = all_set && set;
    refs.push_back(Json{{"secret_ref", r}, {"state", set ? "set" : "missing"}});
  }
  Json am{{"schema", "loom.access/1"}, {"kind", "api_key"}, {"refs", refs}, {"state", all_set ? "set" : "missing"}, {"env", "service:tool_source:" + source}};
  LOOM_TRY_ASSIGN(const std::string acc_id, put_node(db, n.access, "access", std::move(am)));
  LOOM_TRY(put_link(db, env_id, acc_id, "accessed_via"));
  LOOM_TRY(put_link(db, cap_id, acc_id, "requires_access"));
  LOOM_TRY_ASSIGN(auto vault, db.find_node("vault:secrets", "secret_vault"));
  std::string vault_id;
  if (vault) vault_id = vault->id;
  else {
    Json vm{{"schema", "loom.environment/1"}, {"env_kind", "vault"}, {"file", "secrets.json"}, {"holds_values", false}};
    LOOM_TRY_ASSIGN(vault_id, put_node(db, "vault:secrets", "secret_vault", std::move(vm)));
  }
  LOOM_TRY(put_link(db, acc_id, vault_id, "stored_in"));
  return Status{};
}

Status remove_graph(Runtime& rt, const Json& profile, const std::string& source) {
  const GraphNames n = names_of(profile, source);
  for (const auto& [label, kind] : {std::pair<std::string, const char*>{n.access, "access"}, {n.cap, "capability"}, {n.env, "environment"}}) {
    LOOM_TRY_ASSIGN(auto node, rt.db().find_node(label, kind));
    if (node) LOOM_TRY(rt.db().delete_node(node->id));
  }
  return Status{};
}

// ── operations ─────────────────────────────────────────────────────────────────────────────────────

std::string kind_label(const Json& profile, const std::string& kind) {
  const Json& k = object_or_empty(json::find(object_or_empty(json::find(profile, "kinds")), kind));
  return json::get_string(object_or_empty(json::find(k, "label")), "en", kind);
}

Result<Json> sync_one(Runtime& rt, const std::string& id, const Json& cfg, const CancelToken* cancel) {
  LOOM_TRY_ASSIGN(Json profile, profile_values(rt));
  const auto& root = rt.paths().root;
  actions::OverlayStore overlay(root);
  LOOM_TRY_ASSIGN(Json mine, overlay_object(overlay, "actions", "actions"));
  const auto old_ids = own_action_ids(mine, id);
  Json result{{"id", id}, {"ok", true}};
  std::vector<ToolDecl> tools;
  const bool enabled = json::get_bool(cfg, "enabled", true);
  if (enabled) {
    auto src = registry().create(id, cfg, ToolSourceEnv{&rt});
    if (!src) return Json{{"id", id}, {"ok", false}, {"error", src.error().message}};
    auto listed = (*src)->list_tools(cancel);
    if (!listed) return Json{{"id", id}, {"ok", false}, {"error", listed.error().message}};  // the entries it had stay as they were
    tools = std::move(*listed);
  }
  const std::string kl = kind_label(profile, json::get_string(cfg, "kind"));
  std::set<std::string> keep;
  Json ids = Json::array();
  const Json& tool_cfg = object_or_empty(json::find(cfg, "tools"));
  for (const auto& t : tools) {
    if (const Json* tc = json::find(tool_cfg, t.name); tc && json::get_bool(*tc, "hidden", false)) continue;
    Json e = entry_of(profile, id, cfg, t, kl);
    const std::string aid = json::get_string(e, "id");
    keep.insert(aid);
    mine[aid] = e;
    ids.push_back(aid);
  }
  int removed = 0;
  for (const auto& old : old_ids)
    if (!keep.count(old)) { mine.erase(old); ++removed; }
  LOOM_TRY(overlay.put("actions", {"actions"}, mine));
  LOOM_TRY(write_graph(rt, profile, id, cfg, tools, kl));
  result["tools"] = tools.size();
  result["actions"] = ids;
  result["removed"] = removed;
  result["overlay"] = "profiles/actions.pack";
  return result;
}

Result<Json> do_sync(Runtime& rt, const Json& request, const CancelToken* cancel) {
  const Json sources = sources_of(rt);
  const std::string only = json::get_string(request, "source", json::get_string(request, "id"));
  Json out = Json::array();
  if (!only.empty() && !sources.contains(only)) return Error(Errc::NotFound, "no tool source '" + only + "'");
  for (auto it = sources.begin(); it != sources.end(); ++it) {
    if (!only.empty() && it.key() != only) continue;
    LOOM_TRY_ASSIGN(Json r, sync_one(rt, it.key(), it.value(), cancel));
    out.push_back(std::move(r));
  }
  return Json{{"schema", kSyncSchema}, {"sources", out}};
}

Result<Json> do_add(Runtime& rt, const Json& request, const CancelToken* cancel) {
  const std::string id = json::get_string(request, "id", json::get_string(request, "source"));
  Json cfg = Json::object();
  for (const char* k : {"kind", "transport", "command", "args", "url", "access", "env", "label", "enabled", "effects", "tools"})
    if (const Json* v = json::find(request, k)) cfg[k] = *v;
  if (!cfg.contains("kind")) cfg["kind"] = "mcp";
  LOOM_TRY(validate_source(id, cfg));
  Json sources = sources_of(rt);
  const bool replaced = sources.contains(id);
  sources[id] = cfg;
  LOOM_TRY(save_sources(rt, sources));
  Json out{{"schema", kSyncSchema}, {"id", id}, {"replaced", replaced}, {"source", cfg}};
  if (json::get_bool(request, "sync", true)) {
    LOOM_TRY_ASSIGN(Json r, sync_one(rt, id, cfg, cancel));
    out["sync"] = std::move(r);
  }
  return out;
}

Result<Json> do_remove(Runtime& rt, const Json& request) {
  const std::string id = json::get_string(request, "id", json::get_string(request, "source"));
  Json sources = sources_of(rt);
  if (!sources.contains(id)) return Error(Errc::NotFound, "no tool source '" + id + "'");
  LOOM_TRY_ASSIGN(Json profile, profile_values(rt));
  actions::OverlayStore overlay(rt.paths().root);
  LOOM_TRY_ASSIGN(Json mine, overlay_object(overlay, "actions", "actions"));
  const auto ids = own_action_ids(mine, id);
  for (const auto& a : ids) mine.erase(a);
  if (!ids.empty()) LOOM_TRY(overlay.put("actions", {"actions"}, mine));
  LOOM_TRY_ASSIGN(Json rules, overlay_object(overlay, "action_rules", "rules"));
  const std::string prefix = rule_prefix(profile, id);
  std::vector<std::string> drop;
  for (auto it = rules.begin(); it != rules.end(); ++it)
    if (it.key().rfind(prefix, 0) == 0) drop.push_back(it.key());
  for (const auto& r : drop) rules.erase(r);
  if (!drop.empty()) LOOM_TRY(overlay.put("action_rules", {"rules"}, rules));
  LOOM_TRY(remove_graph(rt, profile, id));
  sources.erase(id);
  LOOM_TRY(save_sources(rt, sources));
  return Json{{"schema", kSyncSchema}, {"id", id}, {"removed_actions", ids}, {"removed_rules", drop}};
}

// trust: {source, tool?, mode: allow | confirm | reset, scope?, scope_id?, reason?}
Result<Json> do_trust(Runtime& rt, const Json& request) {
  const std::string id = json::get_string(request, "source", json::get_string(request, "id"));
  const std::string tool = json::get_string(request, "tool");
  const std::string mode = json::get_string(request, "mode", "allow");
  const Json sources = sources_of(rt);
  if (!sources.contains(id)) return Error(Errc::NotFound, "no tool source '" + id + "'");
  LOOM_TRY_ASSIGN(Json profile, profile_values(rt));
  const Json& rcfg = object_or_empty(json::find(profile, "rule"));
  const auto& root = rt.paths().root;
  actions::OverlayStore overlay(root);
  LOOM_TRY_ASSIGN(Json mine, overlay_object(overlay, "actions", "actions"));
  std::vector<std::pair<std::string, std::string>> targets;  // action id, tool name
  for (const auto& aid : own_action_ids(mine, id)) {
    const std::string t = json::get_string(mine[aid], "tool");
    if (tool.empty() || t == tool) targets.emplace_back(aid, t);
  }
  if (targets.empty()) return Error(Errc::NotFound, tool.empty() ? "source '" + id + "' has no listed tools (tool_source.sync first)" : "no tool '" + tool + "' listed for source '" + id + "'");
  Json done = Json::array();
  if (mode == "reset") {
    LOOM_TRY_ASSIGN(Json rules, overlay_object(overlay, "action_rules", "rules"));
    for (const auto& [aid, t] : targets) rules.erase(rule_prefix(profile, id) + t);
    LOOM_TRY(overlay.put("action_rules", {"rules"}, rules));
    for (const auto& [aid, t] : targets) done.push_back(aid);
  } else {
    std::string effect;
    if (mode == "allow") effect = json::get_string(rcfg, "relax_effect", "allow");
    else if (mode == "confirm") effect = json::get_string(rcfg, "tighten_effect", "confirm");
    else return Error(Errc::InvalidArgument, "mode is allow, confirm or reset");
    for (const auto& [aid, t] : targets) {
      Json req{{"id", rule_prefix(profile, id) + t}, {"action", aid}, {"effect", effect},
               {"scope", json::get_string(request, "scope", json::get_string(rcfg, "scope", "person"))}, {"initiator", "user"}};
      if (const Json* sid = json::find(request, "scope_id")) req["scope_id"] = *sid;
      if (const Json* r = json::find(request, "reason")) req["reason"] = *r;
      else if (const Json* r = json::find(rcfg, "reason")) req["reason"] = *r;
      LOOM_TRY(actions::rules_set(root, req));
      done.push_back(aid);
    }
  }
  return Json{{"schema", kSyncSchema}, {"source", id}, {"mode", mode}, {"actions", done}};
}

Result<Json> do_list(Runtime& rt) {
  const Json sources = sources_of(rt);
  LOOM_TRY_ASSIGN(Json profile, profile_values(rt));
  actions::OverlayStore overlay(rt.paths().root);
  LOOM_TRY_ASSIGN(Json mine, overlay_object(overlay, "actions", "actions"));
  LOOM_TRY_ASSIGN(Json rules, overlay_object(overlay, "action_rules", "rules"));
  Json out = Json::array();
  for (auto it = sources.begin(); it != sources.end(); ++it) {
    Json row{{"id", it.key()}, {"source", it.value()}, {"actions", Json::array()}, {"trusted", Json::array()}};
    for (const auto& aid : own_action_ids(mine, it.key())) {
      row["actions"].push_back(aid);
      const std::string rid = rule_prefix(profile, it.key()) + json::get_string(mine[aid], "tool");
      if (const Json* r = json::find(rules, rid); r && !json::get_bool(*r, "disabled", false))
        row["trusted"].push_back(Json{{"action", aid}, {"effect", json::get_string(*r, "effect")}});
    }
    Json refs = Json::array();
    for (const auto& r : secret_refs(it.value())) refs.push_back(Json{{"secret_ref", r}, {"state", rt.secrets().has(r) ? "set" : "missing"}});
    row["access"] = refs;
    out.push_back(std::move(row));
  }
  return Json{{"schema", kListSchema}, {"sources", out}, {"kinds", registry().names()}};
}

Result<Json> do_call(Runtime& rt, std::string_view action, const Json& request, const CancelToken* cancel) {
  std::string source = json::get_string(request, "source"), tool = json::get_string(request, "tool");
  if (action.rfind("call:", 0) == 0) {  // call:<source>:<tool>
    const std::string rest(action.substr(5));
    const auto c = rest.find(':');
    if (c == std::string::npos) return Error(Errc::InvalidArgument, "call:<source>:<tool> expected");
    source = rest.substr(0, c);
    tool = rest.substr(c + 1);
  }
  if (source.empty() || tool.empty()) return Error(Errc::InvalidArgument, "source and tool are required");
  const Json sources = sources_of(rt);
  const Json* cfg = json::find(sources, source);
  if (!cfg) return Error(Errc::NotFound, "no tool source '" + source + "' (it was removed?)");
  if (!json::get_bool(*cfg, "enabled", true)) return Error(Errc::Unavailable, "tool source '" + source + "' is switched off");
  LOOM_TRY_ASSIGN(Json profile, profile_values(rt));
  // Only what the tool declares goes out: the arguments of the call, not the rest of the request.
  Json args = Json::object();
  actions::OverlayStore overlay(rt.paths().root);
  LOOM_TRY(auto entry, overlay.entry("actions", {"actions", action_id(rt, source, tool)}));
  std::set<std::string> declared;
  if (const Json* ps = json::find(entry, "params"); ps && ps->is_array())
    for (const auto& p : *ps) declared.insert(json::get_string(p, "name"));
  if (const Json* a = json::find(request, "arguments"); a && a->is_object()) args = *a;
  else
    for (auto it = request.begin(); it != request.end(); ++it) {
      if (!declared.empty() ? declared.count(it.key()) > 0 : (it.key() != "source" && it.key() != "tool" && it.key() != "arguments")) args[it.key()] = it.value();
    }
  auto src = registry().create(source, *cfg, ToolSourceEnv{&rt});
  port_graph::PortRun run;
  run.port = kPort;
  run.kind = "tool_source.call";
  run.adapter = json::get_string(*cfg, "kind");
  run.operation_id = source + ":" + tool;
  run.extra = Json{{"source", source}, {"tool", tool}, {"argument_names", [&] { Json n = Json::array(); for (auto it = args.begin(); it != args.end(); ++it) n.push_back(it.key()); return n; }()}};
  auto finish = [&](const Status& st, const std::string& msg) {
    run.status = st ? "ok" : "failed";
    run.error = msg;
    if (auto g = port_graph::record_port_run(rt.db(), run); !g) log::debug(kLog, "tool call not recorded in the graph: {}", g.error().message);
  };
  if (!src) { finish(Error(src.error()), src.error().message); return Error(src.error()); }
  auto out = (*src)->call_tool(tool, args, cancel);
  if (!out) { finish(Error(out.error()), out.error().message); return Error(out.error()); }
  if (out->is_error) {
    const std::string msg = "the tool reported an error: " + out->text;
    finish(Error(Errc::Http, msg), msg);
    return Error(Errc::Http, msg);
  }
  finish(Status{}, "");
  Json r{{"schema", kCallSchema}, {"source", source}, {"tool", tool}, {"is_error", false}, {"text", out->text}, {"content", out->content}};
  if (!out->structured.is_null()) r["structured"] = out->structured;
  return r;
}

}  // namespace

ToolSourceRegistry& registry() {
  static ToolSourceRegistry* reg = [] {
    auto* r = new ToolSourceRegistry("kind", "tool source kind");
    ToolSourceRegistry::Descriptor mcp;
    mcp.name = "mcp";
    mcp.factory = [](const std::string& id, const Json& cfg, const ToolSourceEnv& env) { return make_mcp(id, cfg, env); };
    mcp.meta = Json{{"protocol", "JSON-RPC 2.0"}, {"transports", Json::array({"stdio", "http"})}};
    r->add(std::move(mcp));
    return r;
  }();
  return *reg;
}

Result<Json> profile_values(Runtime& rt) {
  LOOM_TRY_ASSIGN(auto p, RuntimeProfile::load("tool_sources", rt.paths().root));
  return p.values();
}

std::string action_id(Runtime& rt, const std::string& source, const std::string& tool) {
  std::string prefix = "tool";
  if (auto v = profile_values(rt)) prefix = json::get_string(*v, "action_prefix", "tool");
  return prefix + "." + sanitize(source) + "." + sanitize(tool);
}

Result<Json> invoke(Runtime& rt, std::string_view action, const Json& request, const CancelToken* cancel) {
  if (!request.is_object()) return Error(Errc::InvalidArgument, "request must be a JSON object");
  if (action == "list") return do_list(rt);
  if (action == "sync") return do_sync(rt, request, cancel);
  if (action == "add") return do_add(rt, request, cancel);
  if (action == "remove") return do_remove(rt, request);
  if (action == "trust") return do_trust(rt, request);
  if (action == "call" || action.rfind("call:", 0) == 0) return do_call(rt, action, request, cancel);
  return Error(Errc::InvalidArgument, "unknown tool_source action '" + std::string(action) + "'");
}

}  // namespace loom::tool_sources
