#include "loom/settings_fields.h"

#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

#include "loom/action_rules.h"
#include "loom/effective_view.h"
#include "loom/log.h"
#include "loom/runtime_profile.h"

namespace loom {

namespace {
constexpr std::string_view kLog = "loom.settings_fields";
constexpr std::string_view kGroupsView = "settings_groups";  // view kind of graph_views.pack whose ROM is this pack's /groups

const Json* find_effect(const Json& effects, const std::string& id) {
  for (const auto& e : effects) if (e.value("id", "") == id) return &e;
  return nullptr;
}

// A JSON pointer (RFC 6901) into a profile's values; "" = the whole document. The failure is the reason, never swallowed and
// never an exception (an overlay's pointer is user data: an index that does not parse is "no element", like one past the end).
Result<Json> value_at(const Json& values, const std::string& pointer) {
  if (pointer.empty()) return values;
  if (pointer[0] != '/') return Error(Errc::InvalidArgument, "pointer " + pointer + ": must start with '/'");
  const Json* cur = &values;
  std::size_t pos = 1;
  while (true) {
    const std::size_t next = pointer.find('/', pos);
    std::string token = pointer.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
    for (std::size_t i = 0; (i = token.find("~1", i)) != std::string::npos; ++i) token.replace(i, 2, "/");
    for (std::size_t i = 0; (i = token.find("~0", i)) != std::string::npos; ++i) token.replace(i, 2, "~");
    if (cur->is_object()) {
      const Json* child = json::find(*cur, token);
      if (!child) return Error(Errc::NotFound, "pointer " + pointer + ": no key '" + token + "'");
      cur = child;
    } else if (cur->is_array()) {
      std::size_t index = 0;
      const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), index);
      if (token.empty() || ec != std::errc{} || end != token.data() + token.size() || index >= cur->size())
        return Error(Errc::NotFound, "pointer " + pointer + ": no element '" + token + "'");
      cur = &(*cur)[index];
    } else {
      return Error(Errc::NotFound, "pointer " + pointer + ": '" + token + "' is below a scalar");
    }
    if (next == std::string::npos) return *cur;
    pos = next + 1;
  }
}

// Values of runtime profiles shown as settings fields (M1-D0, §3.5): the value lives in its profile (overlay
// profiles/<profile>.pack), never in config; the entry names the profile, which is the chip "runtime profile: <domain>".
// An entry whose value cannot be read keeps a `problem` instead of a `value`: shown, never dropped.
Json profile_values_of(const Json& v, const std::filesystem::path& data_dir) {
  Json out = Json::array();
  const Json* list = json::find(v, "profile_values");
  if (!list || !list->is_array()) return out;
  for (const auto& p : *list) {
    Json o = p;
    const std::string profile = p.value("profile", ""), pointer = p.value("pointer", "");
    auto prof = RuntimeProfile::load(profile, data_dir);
    if (!prof) {
      o["problem"] = "profile '" + profile + "' not readable: " + prof.error().message;
      log::debug(kLog, "profile value {}{}: {}", profile, pointer, prof.error().message);
    } else if (auto val = value_at(prof->values(), pointer)) {
      o["value"] = *val;
    } else {
      o["problem"] = "no value in profile '" + profile + "': " + val.error().message;
      log::debug(kLog, "profile value {}{}: {}", profile, pointer, val.error().message);
    }
    out.push_back(std::move(o));
  }
  return out;
}

// A field whose values are its reader's data (`values_from` {profile, pointer, keys?}): the list is that runtime profile's value at
// the pointer (the object's keys when keys:true, else the array's items), read with the user's profile overlay the way the reader
// reads it, so a value the user adds in profiles/<profile>.pack (an order of chat_context, say) is a listed value on every write path
// and never refused by a copy. The pack's own `values` entries are annotations (meaning, preset, status) and come first; a value the
// profile has is marked `from_profile`, and one the profile has but the pack does not annotate is appended. An annotation `planned`
// whose value the profile now has is no longer planned (its reader knows it): the status moves to `status_superseded`. A profile that
// cannot be read keeps the annotations and says why in `values_from_problem` (shown, never silent).
void resolve_values_from(Json& field, const std::filesystem::path& data_dir) {
  const Json* from = json::find(field, "values_from");
  if (!from || !from->is_object()) return;
  const std::string profile = json::get_string(*from, "profile"), pointer = json::get_string(*from, "pointer");
  const bool keys = json::get_bool(*from, "keys", false);
  auto problem = [&](const std::string& why) {
    log::debug(kLog, "values_from {}{} of '{}': {}", profile, pointer, json::get_string(field, "key"), why);
    field["values_from_problem"] = why;
  };
  auto prof = RuntimeProfile::load(profile, data_dir);
  if (!prof) return problem("profile '" + profile + "' not readable: " + prof.error().message);
  auto at = value_at(prof->values(), pointer);
  if (!at) return problem("no value in profile '" + profile + "': " + at.error().message);
  std::vector<Json> found;
  if (keys && at->is_object()) {
    for (auto it = at->begin(); it != at->end(); ++it) found.emplace_back(it.key());
  } else if (!keys && at->is_array()) {
    for (const auto& x : *at) found.push_back(x);
  } else {
    return problem("the value at " + pointer + " of profile '" + profile + "' is not " + (keys ? "an object" : "an array"));
  }
  Json values = field.contains("values") && field["values"].is_array() ? field["values"] : Json::array();
  for (const auto& v : found) {
    Json* annotated = nullptr;
    for (auto& e : values)
      if (const Json* ev = json::find(e, "value"); ev && *ev == v) { annotated = &e; break; }
    if (!annotated) {
      values.push_back(Json{{"value", v}, {"from_profile", profile}});
      continue;
    }
    (*annotated)["from_profile"] = profile;
    if (json::get_string(*annotated, "status") == "planned") {
      (*annotated)["status_superseded"] = "planned";
      annotated->erase("status");
      annotated->erase("on_set");
    }
  }
  field["values"] = std::move(values);
}

// The groups as the view kind `settings_groups` composes them: the shipped pack's /groups is the ROM layer; the user's profile overlay
// (profiles/settings_fields.pack, which may add, rename or drop a group) is a layer of its own (views::profile_overlay), so what it did
// carries the origin overlay:profile:settings_fields, never "rom"; the view overlays of the data dir (<data_dir>/views/*.json, ops on
// settings_groups) regroup, rename or hide on top, with the policy the view kind declares. One list for every reader
// (loom_view_effective and this document agree on what an overlay did). A hidden group stays listed with hidden:true and who hid it, so
// the fields in it are never orphaned; a bad overlay op is an issue (`issues`), never a dropped group. A views directory that cannot be
// read at all (a file that is not JSON, a wrong schema, an unrelated view's overlay) never blanks the settings: the groups are composed
// without view overlays and the reason is an issue the owner reads (settings_fields_problems lists it). Only a pack without /groups is
// an error. `v` is the profile with the user's overlay (a hidden group the overlay added is shown with its overlaid entry).
struct Groups {
  Json object = Json::object();  // id -> group (+ origin, provenance, hidden?, hidden_by?, disabled?)
  Json issues = Json::array();
};

Result<Groups> groups_of(const Json& v, const std::filesystem::path& data_dir) {
  LOOM_TRY_ASSIGN(auto shipped, RuntimeProfile::builtin("settings_fields"));
  const Json* rom_groups = json::find(shipped.values(), "groups");
  if (!rom_groups || !rom_groups->is_object()) return Error(Errc::InvalidArgument, "settings_fields: the pack has no /groups object");
  const Json& rom = *rom_groups;
  Groups out;
  std::vector<views::Overlay> overlays;
  views::Policy policy;
  auto loaded = views::load_overlays({data_dir});  // empty data_dir: none (builtin)
  auto kinds = loaded ? views::effective(data_dir, "view_kinds") : Result<views::Effective>(loaded.error());
  if (!loaded || !kinds) {
    const std::string why = (!loaded ? loaded.error() : kinds.error()).message;
    log::debug(kLog, "settings groups composed without view overlays: {}", why);
    out.issues.push_back(Json{{"code", "overlays_unreadable"}, {"layer", "views"}, {"op", ""}, {"id", ""},
                              {"note", "view overlays not applied to the settings groups (the shipped groups are shown): " + why}});
  } else {
    overlays = std::move(*loaded);
    if (const views::EffectiveEntry* def = kinds->find(kGroupsView); def && !def->disabled)
      policy = views::Policy::from_json(def->value.value("policy", Json::object()));
    // the view kinds were composed without an unreadable graph_views profile overlay: the owner reads why
    for (const auto& i : kinds->issues)
      if (json::get_string(i, "code") == "profile_overlay_unreadable") out.issues.push_back(i);
  }
  std::vector<views::Overlay> layers;
  const Json source{{"profile", "settings_fields"}, {"pointer", "/groups"}};
  if (auto mine = views::profile_overlay(kGroupsView, source, rom, data_dir, policy); !mine) {
    log::debug(kLog, "settings groups composed without the profile overlay: {}", mine.error().message);
    out.issues.push_back(Json{{"code", "profile_overlay_unreadable"}, {"layer", "overlay:profile:settings_fields"}, {"op", ""}, {"id", ""},
                              {"note", "the user's profile overlay is not applied to the settings groups: " + mine.error().message}});
  } else if (!mine->ops.empty()) {
    layers.push_back(std::move(*mine));
  }
  layers.insert(layers.end(), overlays.begin(), overlays.end());
  const views::Effective eff = views::compose(kGroupsView, rom, layers, policy);
  for (const auto& e : eff.entries) {
    Json g = e.value;
    g["origin"] = e.origin;
    g["provenance"] = e.provenance;
    if (e.disabled) g["disabled"] = true;
    out.object[e.id] = std::move(g);
  }
  const Json* overlaid = json::find(v, "groups");
  for (const auto& h : eff.hidden) {
    const std::string id = json::get_string(h, "id");
    const Json* entry = rom.contains(id) ? &rom.at(id) : (overlaid && overlaid->is_object() ? json::find(*overlaid, id) : nullptr);
    Json g = entry ? *entry : Json::object();
    g["hidden"] = true;
    g["hidden_by"] = h.value("by", "");
    const Json provenance = h.value("provenance", Json::array());
    g["origin"] = provenance.is_array() && !provenance.empty() ? json::get_string(provenance[0], "layer", "rom") : std::string("rom");
    g["provenance"] = provenance;
    out.object[id] = std::move(g);
  }
  for (const auto& i : eff.issues) out.issues.push_back(i);
  return out;
}

// Membership a user overlay patches per key (`placement`: field key or <profile><pointer> -> {group, subgroup?}): moves the entry on
// top of human.group/subgroup and keeps the preset as human.placed_from, so moving one field never copies the fields array. An entry
// without a group, or one that names no field, is an issue in the same list as the view overlays' (never dropped silently).
struct Placement {
  const Json* map = nullptr;
  std::set<std::string> used;
  Json issues = Json::array();

  void apply(Json& entry, const std::string& ref) {
    const Json* p = map && map->is_object() ? json::find(*map, ref) : nullptr;
    if (!p) return;
    used.insert(ref);
    const std::string group = p->is_object() ? json::get_string(*p, "group") : "";
    Json* h = entry.contains("human") && entry["human"].is_object() ? &entry["human"] : nullptr;
    if (group.empty() || !h) {
      issues.push_back(Json{{"code", group.empty() ? "placement_without_group" : "placement_without_human"}, {"layer", "placement"},
                            {"op", ""}, {"id", ref}, {"note", group.empty() ? "a placement needs a group" : "the entry has no human layer to place"}});
      return;
    }
    Json from{{"group", h->value("group", "")}};
    if (const Json* sub = json::find(*h, "subgroup")) from["subgroup"] = *sub;
    (*h)["group"] = group;
    if (const std::string sub = json::get_string(*p, "subgroup"); !sub.empty()) (*h)["subgroup"] = sub;
    else h->erase("subgroup");  // the old subgroup belonged to the old group
    (*h)["placed_from"] = std::move(from);
  }

  void unused() {
    if (!map || !map->is_object()) return;
    for (auto it = map->begin(); it != map->end(); ++it)
      if (!used.count(it.key()))
        issues.push_back(Json{{"code", "placement_unknown"}, {"layer", "placement"}, {"op", ""}, {"id", it.key()},
                              {"note", "no field or profile value with this key"}});
  }
};

// {pl, en}, both non-empty: every human text is a sentence in both languages.
bool text_pair(const Json* t) {
  return t && t->is_object() && !json::get_string(*t, "pl").empty() && !json::get_string(*t, "en").empty();
}

bool lists(const Json* arr, const std::string& id) {
  if (!arr || !arr->is_array()) return false;
  for (const auto& x : *arr) if (x.is_string() && x.get<std::string>() == id) return true;
  return false;
}
}  // namespace

Result<Json> settings_fields(const std::filesystem::path& data_dir) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("settings_fields", data_dir));
  const Json& v = profile.values();
  // The edit rules are general rules over (target, action) in the one rule store (action_rules);
  // a field is an element of class `element_class`, "edit" the action, the installation the scope.
  LOOM_TRY_ASSIGN(auto engine, actions::Engine::load(data_dir));
  LOOM_TRY_ASSIGN(auto scope, engine.parse_scope(Json(v.value("rule_scope", ""))));
  const std::string rule_action = v.value("rule_action", "");
  Json fields = Json::array();
  const Json& cfg = config_defaults();
  const Json& lcfg = loom_config_defaults();
  Placement placement;
  placement.map = json::find(v, "placement");
  std::map<std::string, std::optional<Json>> preset_profiles;  // preset_from: domain -> effective values (read once per call)
  for (const auto& f : v.at("fields")) {
    const std::string scope_name = f.value("scope", ""), key = f.value("key", "");
    Json o = f;  // `human`, `status`, `values` and the rest of the entry pass through unchanged
    resolve_values_from(o, data_dir);
    placement.apply(o, key);
    o.erase("has_default");
    const bool has_default = f.value("has_default", false);
    o["default"] = has_default ? "present" : "absent";
    if (scope_name == "config" && has_default) {
      if (cfg.contains(key)) o["default_value"] = cfg.at(key);
      else if (lcfg.contains(key)) o["default_value"] = lcfg.at(key);
    }
    // A field whose reader takes its preset from a runtime profile (preset_from) shows that preset, read live from the effective
    // profile (preset_value / preset_source), never copied into the pack; a pointer that names nothing is a listed problem.
    if (const Json* from = json::find(f, "preset_from"); from && from->is_object() && !o.contains("default_value")) {
      const std::string domain = json::get_string(*from, "profile"), pointer = json::get_string(*from, "pointer");
      auto it = preset_profiles.find(domain);
      if (it == preset_profiles.end()) {
        auto prof = RuntimeProfile::load(domain, data_dir);
        if (!prof) log::write(log::Level::Debug, std::string(kLog), "preset profile " + domain + " not readable: " + prof.error().message);
        it = preset_profiles.emplace(domain, prof ? std::optional<Json>(prof->values()) : std::nullopt).first;
      }
      const Json* at = it->second ? json::find_pointer(*it->second, pointer) : nullptr;
      if (at) {
        o["preset_value"] = *at;
        o["preset_source"] = "profile:" + domain + pointer;
      } else {
        o["preset_problem"] = (it->second ? "pointer_absent: " : "profile_unavailable: ") + domain + pointer;
      }
    }
    actions::Element element;
    element.cls = v.value("element_class", "");
    element.attrs = Json{{"field_scope", scope_name}, {"key", key}};
    LOOM_TRY_ASSIGN(auto decision, engine.decide(element, rule_action, scope));
    const std::string& effect = decision.effect;  // "allow" when no rule matches (R43)
    const Json* e = find_effect(v.at("effects"), effect);
    o["edit"] = e ? e->value("edit", effect) : effect;  // open string: unknown effects pass through
    o["rule"] = Json{{"action", rule_action}, {"effect", effect}};
    fields.push_back(std::move(o));
  }
  Json out{{"schema", "loom.settings_fields/1"}, {"edit_rules", v.at("effects")}, {"fields", fields}};
  // Additive (M1-D0): the human groups of the fields and the runtime-profile values shown as fields.
  // Grouping is data: an overlay may regroup, rename or hide; nothing about it lives in code.
  LOOM_TRY_ASSIGN(auto groups, groups_of(v, data_dir));
  Json profile_values = profile_values_of(v, data_dir);
  for (auto& p : profile_values) placement.apply(p, p.value("profile", "") + p.value("pointer", ""));
  placement.unused();
  for (auto& i : placement.issues) groups.issues.push_back(std::move(i));
  out["groups"] = std::move(groups.object);
  if (!groups.issues.empty()) out["groups_issues"] = std::move(groups.issues);
  out["level_evidence"] = v.value("level_evidence", Json::object());
  out["profile_values"] = std::move(profile_values);
  return out;
}

std::vector<std::string> settings_fields_problems(const Json& doc, const std::filesystem::path& data_dir) {
  std::vector<std::string> out;
  std::set<std::string> described;
  // The levels a field may be set at are the chain of graph/scopes.pack (the effective `scopes` view of the rules engine).
  std::set<std::string> chain;
  // The flows a field names are actions of the catalogue (actions.pack with the user's overlay): a renamed action never leaves a dangling
  // flow on a field card.
  std::set<std::string> action_ids;
  if (auto engine = actions::Engine::load(data_dir)) {
    for (const auto& level : engine->scope_chain()) chain.insert(json::get_string(level, "id"));
    if (chain.empty()) out.push_back("scope chain empty: levels cannot be checked");
    if (engine->catalogue().is_object())
      for (auto it = engine->catalogue().begin(); it != engine->catalogue().end(); ++it) action_ids.insert(it.key());
    if (action_ids.empty()) out.push_back("action catalogue empty: human.flows cannot be checked");
  } else {
    out.push_back("scope chain unavailable: " + engine.error().message);
  }
  // The model roles a field may be the value of are the roles of ports.pack (with the user's overlay); a role the ports do not
  // know yet must say so (`role_status`), so the chip never dangles.
  std::set<std::string> roles;
  if (auto ports = RuntimeProfile::load("ports", data_dir)) {
    if (const Json* r = json::find(ports->values(), "roles"); r && r->is_object())
      for (auto it = r->begin(); it != r->end(); ++it) roles.insert(it.key());
    if (roles.empty()) out.push_back("ports.pack has no roles: human.role cannot be checked");
  } else {
    out.push_back("ports profile unavailable: " + ports.error().message);
  }
  // Evidence for the levels project/conversation (a chain-resolved reader), turn (a per-request wire option) and installation (a
  // writer of the installation layer: the runtime-profile preset or a user rule at that scope): data of the pack.
  const Json* evidence = json::find(doc, "level_evidence");
  const Json* chain_keys = evidence ? json::find(*evidence, "chain") : nullptr;
  const Json* turn_keys = evidence ? json::find(*evidence, "turn") : nullptr;
  const Json* installation_keys = evidence ? json::find(*evidence, "installation") : nullptr;
  if (!chain_keys || !turn_keys || !installation_keys) out.push_back("no level_evidence (chain, turn, installation)");
  const Json* groups = json::find(doc, "groups");
  std::map<std::string, int> per_group;  // group id -> fields + profile values in it
  if (!groups || !groups->is_object()) {
    out.push_back("no groups");
  } else {
    for (auto it = groups->begin(); it != groups->end(); ++it) {
      per_group[it.key()] = 0;
      if (!text_pair(json::find(*it, "label"))) out.push_back("group without label pl+en: " + it.key());
      if (!text_pair(json::find(*it, "why"))) out.push_back("group without why pl+en: " + it.key());
    }
  }
  if (const Json* issues = json::find(doc, "groups_issues"); issues && issues->is_array())
    for (const auto& i : *issues) out.push_back("groups overlay issue: " + i.dump());
  auto check_human = [&](const Json& entry, const std::string& who, const std::string& evidence_key, bool profile_value) {
    const Json* h = json::find(entry, "human");
    if (!h || !h->is_object()) { out.push_back("no human: " + who); return; }
    for (const char* k : {"label", "why", "effect"})
      if (!text_pair(json::find(*h, k))) out.push_back("human." + std::string(k) + " needs pl and en: " + who);
    const std::string group = json::get_string(*h, "group");
    if (group.empty()) out.push_back("no group: " + who);
    else if (!per_group.count(group)) out.push_back("unknown group '" + group + "': " + who);
    else {
      ++per_group[group];
      if (const std::string sub = json::get_string(*h, "subgroup"); !sub.empty()) {
        bool found = false;
        if (const Json* subs = json::find(groups->at(group), "subgroups"); subs && subs->is_array())
          for (const auto& s : *subs) found = found || json::get_string(s, "id") == sub;
        if (!found) out.push_back("unknown subgroup '" + sub + "' of group '" + group + "': " + who);
      }
    }
    // Empty levels = settable nowhere: only a read_only field (edit rule `deny`, which carries its reason) may say so.
    const Json* rule = json::find(entry, "rule");
    const bool read_only = rule && json::get_string(*rule, "effect") == "deny";
    const Json* levels = json::find(*h, "levels");
    if (!levels || !levels->is_array()) out.push_back("no levels: " + who);
    else if (levels->empty() && !read_only) out.push_back("no levels (only a read_only field is settable nowhere): " + who);
    else
      for (const auto& l : *levels) {
        const std::string id = l.is_string() ? l.get<std::string>() : "";
        if (!chain.empty() && !chain.count(id)) out.push_back("level outside the scope chain '" + id + "': " + who);
        // A level is a promise that a reader honours the value there; the evidence is data (level_evidence), never a guess.
        if ((id == "project" || id == "conversation") && chain_keys && !lists(chain_keys, evidence_key))
          out.push_back("level '" + id + "' without a chain-resolved reader (level_evidence.chain): " + who);
        if (id == "turn" && turn_keys && !lists(turn_keys, evidence_key))
          out.push_back("level 'turn' without a per-request option (level_evidence.turn): " + who);
        if (id == "installation" && installation_keys && !lists(installation_keys, evidence_key))
          out.push_back("level 'installation' without a writer (level_evidence.installation): " + who);
        // `person` is config.json / secrets.json; a profile value lives in its profile, never in config (§3.5)
        if (id == "person" && profile_value)
          out.push_back("level 'person' on a profile value (its value lives in the profile, never in config.json): " + who);
      }
    if (const Json* flows = json::find(*h, "flows"); flows && !action_ids.empty()) {
      if (!flows->is_array()) out.push_back("human.flows must be a list of action ids: " + who);
      else
        for (const auto& fl : *flows)
          if (!fl.is_string() || !action_ids.count(fl.get<std::string>())) out.push_back("unknown flow " + fl.dump() + ": " + who);
    }
    if (const std::string role = json::get_string(*h, "role");
        !role.empty() && !roles.empty() && !roles.count(role) && json::get_string(*h, "role_status").empty())
      out.push_back("role '" + role + "' unknown to ports.pack and without role_status: " + who);
    // One source (M1-D1): a role ports.pack knows carries its status there (roles.<id>.status), never a second copy on the field.
    if (const std::string role = json::get_string(*h, "role"); !role.empty() && roles.count(role) && h->contains("role_status"))
      out.push_back("role_status on role '" + role + "', which ports.pack knows (its status lives in ports.pack): " + who);
    // A field whose key nothing reads yet says so in data (status.planned with its reason), never in the present tense alone.
    if (const Json* status = json::find(entry, "status")) {
      if (!status->is_object() || json::get_string(*status, "state").empty()) out.push_back("status without state: " + who);
      if (!text_pair(status->is_object() ? json::find(*status, "reason") : nullptr)) out.push_back("status without reason pl+en: " + who);
      // a planned field says what the app does today, so a preset never reads as behaviour (or protection) that is not there yet
      if (status->is_object() && json::get_string(*status, "state") == "planned" && !text_pair(json::find(*status, "today")))
        out.push_back("planned status without today pl+en: " + who);
    }
  };
  for (const auto& f : doc.at("fields")) {
    const std::string scope = f.value("scope", ""), key = f.value("key", "");
    if (key.empty() || (scope != "config" && scope != "secret")) { out.push_back("bad field entry"); continue; }
    if (!described.insert(scope + ":" + key).second) out.push_back("duplicate " + scope + ":" + key);
    if (f.value("edit", "").empty()) out.push_back("no edit rule: " + key);
    if (f.value("description", "").empty()) out.push_back("no description: " + key);
    // the pack's schema requires only key and scope (a hand-written overlay entry still loads); what an entry lacks is listed here
    if (!f.contains("type") || !f["type"].is_string()) out.push_back("no type: " + key);
    if (f.value("group", "").empty()) out.push_back("no technical group: " + key);
    if (f.contains("preset_problem")) out.push_back("preset_from does not resolve: " + key + " (" + json::get_string(f, "preset_problem") + ")");
    const bool has_default = scope == "config" && (config_defaults().contains(key) || loom_config_defaults().contains(key));
    const std::string declared = f.value("default", "");
    if (declared != "present" && declared != "absent") out.push_back("default must be present|absent: " + key);
    else if ((declared == "present") != has_default) out.push_back("default mismatch: " + key);
    if (has_default && !f.contains("default_value")) out.push_back("default value missing: " + key);
    if (scope == "secret" && (f.contains("default_value") || !f.value("sensitive", false)))
      out.push_back("secret must be sensitive without value: " + key);
    check_human(f, scope + ":" + key, key, false);
  }
  if (const Json* pv = json::find(doc, "profile_values"); pv && pv->is_array())
    for (const auto& p : *pv) {
      const std::string ref = p.value("profile", "") + p.value("pointer", "");
      const std::string who = "profile value " + ref;
      if (p.value("profile", "").empty() || p.value("pointer", "").empty()) out.push_back("profile value without profile or pointer");
      check_human(p, who, ref, true);
      if (p.contains("problem")) out.push_back(who + ": " + p.at("problem").dump());
      else if (!p.contains("value")) out.push_back(who + ": no value");
    }
  // Every group holds something, or says why it is empty (an overlay may hide fields; the reason stays visible).
  for (const auto& [id, n] : per_group)
    if (n == 0 && !text_pair(json::find(groups->at(id), "empty_reason"))) out.push_back("empty group without empty_reason: " + id);
  for (const Json* d : {&config_defaults(), &loom_config_defaults()})
    for (auto it = d->begin(); it != d->end(); ++it)
      if (!described.count("config:" + it.key())) out.push_back("undescribed default: " + it.key());
  return out;
}

std::string settings_edit_rule(std::string_view scope, std::string_view key) {
  auto doc = settings_fields();
  if (!doc) return "";
  for (const auto& f : doc->at("fields"))
    if (f.value("scope", "") == scope && f.value("key", "") == key) return f.value("edit", "");
  return "";
}

Result<Json> check_declared_value(const Json& field, const Json& value) {
  const Json* values = json::find(field, "values");
  if (value.is_null() || !values || !values->is_array() || field.value("values_open", false)) return Json(nullptr);
  const Json* match = nullptr;
  for (const auto& entry : *values)
    if (const Json* v = json::find(entry, "value"); v && *v == value) { match = &entry; break; }
  if (!match) {
    Json listed = Json::array();
    std::string allowed;
    for (const auto& entry : *values) {
      listed.push_back(entry.value("value", Json()));
      allowed += (allowed.empty() ? "" : ", ") + json::dump(entry.value("value", Json()));
    }
    if (json::get_string(field, "on_unlisted") == "refuse")
      return Error(Errc::InvalidArgument, "the value " + json::dump(value) + " is not one of the setting's values: " + allowed);
    return Json{{"value", value}, {"status", "unlisted"}, {"values", listed},
                {"message", "the value " + json::dump(value) + " is not one of the setting's values (" + allowed +
                                "); stored, its reader falls back"}};
  }
  const std::string status = json::get_string(*match, "status");
  if (status.empty() || status == "available") return Json(nullptr);
  const Json reason = match->value("reason", Json());
  const std::string why = reason.is_string() ? reason.get<std::string>() : json::get_string(reason, "en");
  const std::string by = json::get_string(*match, "by");  // the work item that adds its reader (technical; the reason is plain words)
  const std::string head = "the value " + json::dump(value) + " is " + status + (by.empty() ? "" : " (comes with " + by + ")") + ", not available yet";
  if (json::get_string(*match, "on_set") == "refuse") return Error(Errc::InvalidArgument, head + (why.empty() ? "" : ": " + why));
  Json warning{{"value", value}, {"status", status}, {"message", head + "; stored" + (why.empty() ? "" : ": " + why)}};
  if (!by.empty()) warning["by"] = by;
  if (!reason.is_null()) warning["reason"] = reason;
  return warning;
}

Result<Json> check_config_patch(const Json& patch, const std::filesystem::path& data_dir) {
  Json warnings = Json::array();
  if (!patch.is_object() || patch.empty()) return warnings;
  auto doc = settings_fields(data_dir);
  if (!doc) {  // metadata unavailable: never block writes
    log::debug(kLog, "config write checked without metadata: {}", doc.error().message);
    return warnings;
  }
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    for (const auto& f : doc->at("fields")) {
      if (f.value("scope", "") != "config" || f.value("key", "") != it.key()) continue;
      if (f.at("rule").value("effect", "") == "deny")
        return Error(Errc::InvalidArgument, "config key '" + it.key() + "' is read_only");
      LOOM_TRY_ASSIGN(Json warning, check_declared_value(f, it.value()));
      if (!warning.is_null()) {
        warning["key"] = it.key();
        warnings.push_back(std::move(warning));
      }
      break;
    }
  }
  return warnings;
}

Status check_config_write(std::string_view key, const std::filesystem::path& data_dir) {
  auto doc = settings_fields(data_dir);
  if (!doc) return {};  // metadata unavailable: never block writes
  for (const auto& f : doc->at("fields")) {
    if (f.value("scope", "") != "config" || f.value("key", "") != key) continue;
    // Only the effect "deny" (label read_only) is enforced by the generic write path.
    if (f.at("rule").value("effect", "") == "deny")
      return Error(Errc::InvalidArgument, "config key '" + std::string(key) + "' is read_only");
  }
  return {};
}

}  // namespace loom
