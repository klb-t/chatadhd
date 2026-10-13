// Rules and actions the user writes by saying so: executors of rules.set, rules.disable, rules.list, actions.define,
// actions.remove, groups.define, groups.remove. Only user overlays are written (see action_overlay.h).
#include "loom/action_overlay.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <set>

#include "loom/action_rules.h"
#include "loom/runtime_profile.h"
#include "loom/settings_fields.h"
#include "loom/util/fs.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::actions {

namespace fs = std::filesystem;

namespace {

std::mutex& overlay_mutex() {
  static std::mutex mu;
  return mu;
}

Error invalid(const std::string& message) { return Error(Errc::InvalidArgument, message); }

}  // namespace

// ── OverlayStore ────────────────────────────────────────────────────

fs::path OverlayStore::file(std::string_view domain) const { return root_ / "profiles" / (std::string(domain) + ".pack"); }

Result<Json> OverlayStore::document(std::string_view domain) const {
  const fs::path path = file(domain);
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", std::string(domain)}, {"overrides", Json::object()}};
  }
  LOOM_TRY_ASSIGN(std::string bytes, fsutil::read_file(path));
  if (!utf8::is_valid(bytes)) return Error(Errc::Parse, "the overlay " + path.string() + " is not valid UTF-8");
  LOOM_TRY_ASSIGN(Json doc, json::parse(bytes));
  if (!doc.is_object()) return Error(Errc::Parse, "the overlay " + path.string() + " is not an object");
  if (!doc.contains("overrides") || !doc["overrides"].is_object()) doc["overrides"] = Json::object();
  return doc;
}

Status OverlayStore::write(std::string_view domain, const Json& document) const {
  // Valid or not at all: the overlay must load against the profile it overlays.
  LOOM_TRY_ASSIGN(auto base, RuntimeProfile::builtin(domain));
  LOOM_TRY_ASSIGN(auto merged, base.with_overlay(document));
  (void)merged;
  LOOM_TRY(fsutil::ensure_dir(root_ / "profiles"));
  return fsutil::atomic_write(file(domain), json::dump(document, 2));
}

Result<Json> OverlayStore::entry(std::string_view domain, const std::vector<std::string>& path) const {
  std::lock_guard lk(overlay_mutex());
  LOOM_TRY_ASSIGN(Json doc, document(domain));
  const Json* at = &doc["overrides"];
  for (const auto& key : path) {
    at = at->is_object() ? json::find(*at, key) : nullptr;
    if (!at) return Json(nullptr);
  }
  return *at;
}

Status OverlayStore::put(std::string_view domain, const std::vector<std::string>& path, const Json& value) const {
  if (path.empty()) return invalid("an overlay entry needs a path");
  std::lock_guard lk(overlay_mutex());
  LOOM_TRY_ASSIGN(Json doc, document(domain));
  Json* at = &doc["overrides"];
  for (std::size_t i = 0; i + 1 < path.size(); ++i) {
    if (!at->contains(path[i]) || !(*at)[path[i]].is_object()) (*at)[path[i]] = Json::object();
    at = &(*at)[path[i]];
  }
  (*at)[path.back()] = value;
  return write(domain, doc);
}

Result<bool> OverlayStore::remove(std::string_view domain, const std::vector<std::string>& path) const {
  if (path.empty()) return invalid("an overlay entry needs a path");
  std::lock_guard lk(overlay_mutex());
  LOOM_TRY_ASSIGN(Json doc, document(domain));
  std::vector<Json*> chain{&doc["overrides"]};
  for (std::size_t i = 0; i + 1 < path.size(); ++i) {
    Json* next = chain.back()->is_object() ? const_cast<Json*>(json::find(*chain.back(), path[i])) : nullptr;
    if (!next) return false;
    chain.push_back(next);
  }
  if (!chain.back()->is_object() || !chain.back()->contains(path.back())) return false;
  chain.back()->erase(path.back());
  for (std::size_t i = chain.size() - 1; i > 0; --i)  // the parents this leaves empty go too
    if (chain[i]->is_object() && chain[i]->empty()) chain[i - 1]->erase(path[i - 1]);
  LOOM_TRY(write(domain, doc));
  return true;
}

// ── rules ───────────────────────────────────────────────────────────

namespace {

std::string text_of(const Json& v, const char* lang = "en") {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_object()) return json::get_string(v, lang, json::get_string(v, "en"));
  return std::string();
}

std::string slug(std::string s) {
  for (char& c : s)
    if (c == ' ' || c == '/' || c == '\\') c = '_';
  return s;
}

// "Locked by <initiator>: <reason>" for refusals.
std::string locked_message(const Json& pin, const std::string& what) {
  std::string out = what + ": the rule '" + json::get_string(pin, "rule") + "' is pinned";
  if (const std::string who = json::get_string(pin, "initiator"); !who.empty()) out += " by " + who;
  if (const Json* r = json::find(pin, "reason")) out += " (" + text_of(*r) + ")";
  return out;
}

// The scoped key a words-level setting name stands for; the second is how it was found.
struct KeyChoice {
  std::string key;
  bool consumer_known = false;
  std::string setting;  // the user's setting that is the person-level baseline, when there is one
  std::string field;
};

Result<KeyChoice> choose_key(const Json& values, const std::string& said, const fs::path& root) {
  KeyChoice out;
  const Json keys = values.value("scoped_values", Json::object()).value("keys", Json::object());
  if (const Json* known = json::find(keys, said)) {
    out.key = said;
    out.consumer_known = true;
    out.setting = json::get_string(*known, "setting");
    out.field = json::get_string(*known, "field");
    return out;
  }
  for (auto it = keys.begin(); it != keys.end(); ++it)
    if (json::get_string(it.value(), "setting") == said && json::get_string(it.value(), "field").empty()) {
      out.key = it.key();
      out.consumer_known = true;
      out.setting = said;
      return out;
    }
  if (auto fields = settings_fields(root)) {
    if (const Json* list = json::find(*fields, "fields"); list && list->is_array())
      for (const auto& f : *list)
        if (json::get_string(f, "key") == said) {
          out.key = said;
          out.setting = said;
          return out;  // a setting of the application: written, but no consumer reads it per scope yet
        }
  }
  return Error(Errc::NotFound, "no setting '" + said + "' (a scoped value is a setting of the application)");
}

Json coerce_value(const Json& given, const std::string& setting, const fs::path& root) {
  if (!given.is_string()) return given;
  const std::string text = given.get<std::string>();
  std::string type;
  if (auto fields = settings_fields(root))
    if (const Json* list = json::find(*fields, "fields"); list && list->is_array())
      for (const auto& f : *list)
        if (json::get_string(f, "key") == setting) type = json::get_string(f, "type");
  if (type == "string") return given;
  if (text == "true" || text == "false" || text == "null" || (!text.empty() && (std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '-' || text[0] == '{' || text[0] == '[')))
    return json::parse_or(text, given);
  return given;
}

}  // namespace

namespace {

struct RuleDraft {
  Json rule;
  Json note = Json::object();
  std::string dimension;
  std::optional<Json> pin;
  bool replaced = false;
};

Result<RuleDraft> draft_rule(const fs::path& root, const Json& request) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("action_rules", root));
  LOOM_TRY_ASSIGN(auto engine, Engine::load(root));
  const Json& values = profile.values();
  const Json cfg = values.value("user_rules", Json::object());
  std::string scope = json::get_string(request, "scope", json::get_string(cfg, "default_scope", "person"));
  const std::string level = engine.level_name(scope);
  if (level.empty()) return invalid("unknown scope '" + scope + "' (a level of the scope chain: installation, person, project, conversation ...)");
  std::string scope_id = json::get_string(request, "scope_id");
  if (scope_id.empty() && level == "conversation") {
    scope_id = json::get_string(request, "conv_id");
    if (scope_id.empty()) scope_id = json::get_string(request.value("context", Json::object()), "conv_id");
  }
  const auto instance_levels = [&] {
    std::set<std::string> s;
    for (const auto& l : cfg.value("instance_levels", Json::array()))
      if (l.is_string()) s.insert(l.get<std::string>());
    return s;
  }();
  if (instance_levels.count(level) && scope_id.empty()) return invalid("a rule at the level '" + level + "' belongs to one instance of it: give scope_id");

  // What the rule says.
  std::string effect = json::get_string(request, "effect");
  const bool has_value = request.contains("value");
  if (effect.empty() && has_value) effect = "set_value";
  if (effect.empty()) return invalid("effect is required (set_value, confirm, deny, allow, require_confirmation ...)");
  const Json effects_table = values.value("effects", Json::object());
  const Json decisions_table = values.value("decisions", Json::object());
  const Json* effect_row = json::find(effects_table, effect);
  if (!effect_row) return invalid("unknown effect '" + effect + "'");
  const std::string decision = json::get_string(*effect_row, "decision", "allow");
  const Json* decision_row = json::find(decisions_table, decision);
  const std::string dimension = decision_row ? json::get_string(*decision_row, "dimension", "gate") : std::string("gate");

  Json rule = Json::object();
  Json note = Json::object();
  std::string subject;
  if (dimension == "value") {
    const std::string said = json::get_string(request, "key", json::get_string(request.value("target", Json::object()), "key"));
    if (said.empty()) return invalid("key is required (the setting whose value the rule sets)");
    if (!has_value) return invalid("value is required");
    LOOM_TRY_ASSIGN(KeyChoice choice, choose_key(values, said, root));
    rule["target"] = Json{{"kind", "setting"}, {"key", choice.key}};
    rule["value"] = coerce_value(request["value"], choice.setting.empty() ? choice.key : choice.setting, root);
    if (!choice.consumer_known) note["consumer"] = "nothing reads this value per scope yet";
    subject = choice.key;
  } else {
    Json target = request.contains("target") && request["target"].is_object() ? request["target"] : Json{{"kind", "any"}};
    rule["target"] = target;
    Json action = request.value("action", Json(nullptr));
    if (action.is_string() && !action.get<std::string>().empty() && action.get<std::string>() != "*") {
      const Json* catalogue = nullptr;
      LOOM_TRY_ASSIGN(auto actions_profile, RuntimeProfile::load("actions", root));
      catalogue = json::find(actions_profile.values(), "actions");
      if (!catalogue || !json::find(*catalogue, action.get<std::string>()))
        return Error(Errc::NotFound, "no action '" + action.get<std::string>() + "' in the catalogue");
    }
    if (!action.is_null()) rule["action"] = action;
    subject = (action.is_string() ? action.get<std::string>() : action.is_object() ? json::dump(action) : std::string("*"));
    const std::string kind = json::get_string(target, "kind", "any");
    if (kind != "any") subject += ":" + kind + "." + json::get_string(target, "id");
  }
  rule["effect"] = effect;
  rule["scope"] = level;
  if (!scope_id.empty()) rule["scope_id"] = scope_id;
  Json reason = request.value("reason", Json(nullptr));
  if (reason.is_null() || (reason.is_string() && reason.get<std::string>().empty())) reason = cfg.value("default_reason", Json("Set by you."));
  rule["reason"] = reason;
  rule["initiator"] = json::get_string(request, "initiator", json::get_string(cfg, "default_initiator", "user"));
  if (json::get_bool(request, "locked", false)) {
    rule["locked"] = true;
    rule["conflict"] = "locked_by";
  }
  std::string id = json::get_string(request, "id");
  if (id.empty())
    id = slug(json::get_string(cfg, "id_prefix", "user") + ":" + level + (scope_id.empty() ? "" : ":" + scope_id) + ":" + dimension + ":" + subject);
  rule["id"] = id;
  if (const Json* prov = json::find(request, "provenance"); prov && prov->is_object()) rule["provenance"] = *prov;
  rule["created"] = timeutil::utc_now_iso();

  RuleDraft draft;
  draft.rule = rule;
  draft.note = note;
  draft.dimension = dimension;
  // A pinned rule wins whatever is written below it: it is refused, with its reason, before anything is written.
  draft.pin = engine.locked_against(rule);
  OverlayStore overlay(root);
  LOOM_TRY_ASSIGN(Json previous, overlay.entry("action_rules", {"rules", id}));
  draft.replaced = !previous.is_null();
  return draft;
}

}  // namespace

Result<Json> rules_set(const fs::path& root, const Json& request) {
  LOOM_TRY_ASSIGN(RuleDraft draft, draft_rule(root, request));
  if (draft.pin) return Error(Errc::Conflict, locked_message(*draft.pin, "this rule cannot be written"));
  const bool dry_run = json::get_bool(request, "dry_run", false);
  const std::string id = json::get_string(draft.rule, "id");
  Json out{{"schema", "loom.rule_written/1"}, {"id", id}, {"rule", draft.rule}, {"replaced", draft.replaced}, {"written", !dry_run},
           {"overlay", "profiles/action_rules.pack"}, {"dimension", draft.dimension}};
  if (!draft.note.empty()) out["note"] = draft.note;
  if (dry_run) return out;
  LOOM_TRY(OverlayStore(root).put("action_rules", {"rules", id}, draft.rule));
  return out;
}

Result<Json> rules_preview(const fs::path& root, std::string_view action, const Json& request) {
  if (action == "rules.set") {
    LOOM_TRY_ASSIGN(RuleDraft draft, draft_rule(root, request));
    Json out{{"ok", !draft.pin}, {"rule", draft.rule}, {"replaced", draft.replaced}};
    if (draft.pin) out["pin"] = *draft.pin;
    if (!draft.note.empty()) out["note"] = draft.note;
    return out;
  }
  if (action == "rules.disable") {
    const std::string id = json::get_string(request, "id");
    if (id.empty()) return invalid("id is required (the rule to switch off)");
    LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("action_rules", root));
    const Json all_rules = profile.values().value("rules", Json::object());
    const Json* rule = json::find(all_rules, id);
    if (!rule) return Error(Errc::NotFound, "no rule '" + id + "'");
    Json out{{"ok", true}, {"rule", *rule}};
    if (!json::get_bool(request, "restore", false) && json::get_bool(*rule, "locked", false)) {
      out["ok"] = false;
      Json pin{{"rule", id}, {"initiator", json::get_string(*rule, "initiator")}};
      if (const Json* r = json::find(*rule, "reason")) pin["reason"] = *r;
      out["pin"] = pin;
    }
    return out;
  }
  return Json{{"ok", true}};
}

Result<Json> rules_disable(const fs::path& root, const Json& request) {
  const std::string id = json::get_string(request, "id");
  if (id.empty()) return invalid("id is required (the rule to switch off)");
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("action_rules", root));
  const Json rules = profile.values().value("rules", Json::object());
  const Json* rule = json::find(rules, id);
  if (!rule) return Error(Errc::NotFound, "no rule '" + id + "'");
  const bool restore = json::get_bool(request, "restore", false);
  if (!restore && json::get_bool(*rule, "locked", false)) {
    Json pin{{"rule", id}, {"initiator", json::get_string(*rule, "initiator")}};
    if (const Json* r = json::find(*rule, "reason")) pin["reason"] = *r;
    return Error(Errc::Conflict, locked_message(pin, "this rule cannot be switched off"));
  }
  const bool dry_run = json::get_bool(request, "dry_run", false);
  OverlayStore overlay(root);
  Json out{{"schema", "loom.rule_disabled/1"}, {"id", id}, {"disabled", !restore}, {"written", !dry_run}};
  if (dry_run) return out;
  if (restore) {
    LOOM_TRY_ASSIGN(bool removed, overlay.remove("action_rules", {"rules", id, "disabled"}));
    out["changed"] = removed;
  } else {
    LOOM_TRY(overlay.put("action_rules", {"rules", id, "disabled"}, true));
    out["changed"] = !json::get_bool(*rule, "disabled", false);
  }
  return out;
}

Result<Json> rules_list(const fs::path& root, const Json& request) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("action_rules", root));
  LOOM_TRY_ASSIGN(auto shipped, RuntimeProfile::builtin("action_rules"));
  LOOM_TRY_ASSIGN(auto engine, Engine::load(root));
  const Json shipped_rules = shipped.values().value("rules", Json::object());
  const std::string want_scope = json::get_string(request, "scope");
  const std::string want_level = want_scope.empty() ? std::string() : engine.level_name(want_scope);
  if (!want_scope.empty() && want_level.empty()) return invalid("unknown scope '" + want_scope + "'");
  const std::string want_id = json::get_string(request, "scope_id");
  const bool user_only = json::get_bool(request, "user_only", false);
  const bool include_disabled = json::get_bool(request, "include_disabled", true);
  OverlayStore overlay(root);
  Json rules = Json::array();
  const Json all = profile.values().value("rules", Json::object());
  for (auto it = all.begin(); it != all.end(); ++it) {
    Json r = it.value();
    const bool in_shipped = json::find(shipped_rules, it.key()) != nullptr;
    LOOM_TRY_ASSIGN(Json mine, overlay.entry("action_rules", {"rules", it.key()}));
    const std::string source = in_shipped ? (mine.is_null() ? "shipped" : "shipped+overlay") : "overlay";
    if (user_only && source == "shipped") continue;
    const std::string level = engine.level_name(json::get_string(r, "scope"));
    if (!want_level.empty() && level != want_level) continue;
    if (!want_id.empty() && json::get_string(r, "scope_id") != want_id) continue;
    const bool disabled = json::get_bool(r, "disabled", false);
    if (disabled && !include_disabled) continue;
    r["id"] = it.key();
    r["source"] = source;
    r["disabled"] = disabled;
    rules.push_back(std::move(r));
  }
  return Json{{"schema", "loom.rules_list/1"}, {"rules", rules}};
}

// ── the emergent catalogue (R55) ────────────────────────────────────

namespace {

bool valid_name(const std::string& id) {
  if (id.empty() || id.size() > 64) return false;
  if (!(std::islower(static_cast<unsigned char>(id[0])) || std::isdigit(static_cast<unsigned char>(id[0])))) return false;
  return std::all_of(id.begin(), id.end(), [](char c) {
    return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-';
  });
}

Json provenance_of(const Json& request) {
  Json out = Json::object();
  if (const Json* p = json::find(request, "provenance"); p && p->is_object()) out = *p;
  return out;
}

Json origin_of(const Json& request) {
  Json origin = provenance_of(request);
  origin["kind"] = "emergent";
  origin["defined"] = timeutil::utc_now_iso();
  return origin;
}

}  // namespace

Result<Json> actions_define(const fs::path& root, const Json& request) {
  const std::string id = json::get_string(request, "id");
  if (!valid_name(id)) return invalid("id must be a name of a-z, 0-9, _ . - (got '" + id + "')");
  const bool dry_run = json::get_bool(request, "dry_run", false);
  const std::string instruction = json::get_string(request, "instruction");
  const Json* steps_in = json::find(request, "steps");
  const bool has_steps = steps_in && steps_in->is_array() && !steps_in->empty();
  if (instruction.empty() == !has_steps) return invalid("give either instruction (one model step) or steps (actions of the catalogue), not both and not neither");

  LOOM_TRY_ASSIGN(auto catalogue, RuntimeProfile::load("actions", root));
  LOOM_TRY_ASSIGN(auto shipped, RuntimeProfile::builtin("actions"));
  LOOM_TRY_ASSIGN(auto routing, RuntimeProfile::load("intent_routing", root));
  const Json actions = catalogue.values().value("actions", Json::object());
  const Json shipped_actions = shipped.values().value("actions", Json::object());
  if (json::find(shipped_actions, id)) return Error(Errc::Conflict, "an action named '" + id + "' ships with the application; choose another name");
  OverlayStore overlay(root);
  LOOM_TRY_ASSIGN(Json mine, overlay.entry("actions", {"actions", id}));
  const bool replaces = !mine.is_null();
  if (replaces && json::get_string(mine.value("origin", Json::object()), "kind") != "emergent")
    return Error(Errc::Conflict, "'" + id + "' is an entry you wrote by hand; it is not replaced from a conversation");
  const Json emergent = routing.values().value("emergent", Json::object());
  const Json cfg = emergent.value("definition", Json::object());

  Json entry = Json::object();
  entry["id"] = id;
  entry["kind"] = "composite";
  entry["status"] = "implemented";
  entry["title"] = json::get_string(request, "title", id);
  const std::string lang = json::get_string(request, "lang");
  if (!lang.empty() && lang != "en") entry["labels"] = Json{{lang, entry["title"]}};
  Json effects{{"reads_only", true}, {"writes", false}, {"sends_off_device", true}, {"costs_money", true}, {"irreversible", false}, {"touches_private_data", true}};
  if (!instruction.empty()) {
    entry["description"] = "Defined from your own words in a conversation: " + instruction;
    Json with = Json::object();
    with["system"] = Json{{"$localized", cfg.value("system", Json::object())}};
    with["prompt"] = instruction;
    with["parameters"] = cfg.value("parameters", Json::object());
    entry["steps"] = Json::array({Json{{"action", "use_model"}, {"with", with}}});
    entry["inputs"] = Json::array({"text"});
    entry["outputs"] = Json::array({"text"});
    entry["requires"] = request.value("requires", Json::array({"has_text"}));
    entry["produces"] = Json::array({"has_text"});
    entry["content_op"] = "none";
    entry["cost"] = json::get_number(cfg, "cost", 1.0);
    entry["confidence"] = json::get_number(cfg, "confidence", 0.6);
  } else {
    Json steps = Json::array();
    std::string described;
    double cost = 0, confidence = 1;
    Json first, last;
    effects = Json{{"reads_only", true}, {"writes", false}, {"sends_off_device", false}, {"costs_money", false}, {"irreversible", false}, {"touches_private_data", false}};
    for (const auto& st : *steps_in) {
      const std::string action = st.is_string() ? st.get<std::string>() : json::get_string(st, "action");
      const Json* used = json::find(actions, action);
      if (!used) return Error(Errc::NotFound, "no action '" + action + "' in the catalogue");
      if (json::get_string(*used, "status") != "implemented") return invalid("the action '" + action + "' is only declared, not implemented: it cannot be a step yet");
      steps.push_back(Json{{"action", action}});
      described += (described.empty() ? "" : " -> ") + action;
      cost += json::get_number(*used, "cost", 1.0);
      confidence *= json::get_number(*used, "confidence", 1.0);
      if (first.is_null()) first = *used;
      last = *used;
      effects["reads_only"] = effects.value("reads_only", true) && json::get_bool(used->value("effects", Json::object()), "reads_only", true);
      for (const auto& flag : {"writes", "sends_off_device", "costs_money", "irreversible", "touches_private_data"})
        effects[flag] = effects.value(flag, false) || json::get_bool(used->value("effects", Json::object()), flag, false);
    }
    entry["description"] = "A chain you kept repeating, saved as one action: " + described + ".";
    entry["steps"] = steps;
    entry["inputs"] = first.value("inputs", Json::array());
    entry["outputs"] = last.value("outputs", Json::array());
    entry["requires"] = request.contains("requires") ? request["requires"] : first.value("requires", Json::array());
    entry["produces"] = last.value("produces", Json::array());
    entry["content_op"] = json::get_string(last, "content_op", "none");
    entry["cost"] = cost;
    entry["confidence"] = confidence;
  }
  entry["form_op"] = "none";
  entry["effects"] = effects;
  entry["consumer"] = "";
  entry["origin"] = origin_of(request);

  Json phrases = Json::object();
  if (const Json* p = json::find(request, "phrases"); p && p->is_object())
    for (auto it = p->begin(); it != p->end(); ++it) {
      Json list = Json::array();
      if (it.value().is_array())
        for (const auto& x : it.value())
          if (x.is_string() && !x.get<std::string>().empty()) list.push_back(x);
      if (!list.empty()) phrases[it.key()] = list;
    }

  Json out{{"schema", "loom.action_defined/1"}, {"id", id}, {"entry", entry}, {"phrases", phrases}, {"replaced", replaces}, {"written", !dry_run},
           {"overlays", Json::array({"profiles/actions.pack"})}};
  if (!phrases.empty()) out["overlays"].push_back("profiles/intent_routing.pack");
  if (dry_run) return out;
  LOOM_TRY(overlay.put("actions", {"actions", id}, entry));
  if (!phrases.empty()) {
    if (auto st = overlay.put("intent_routing", {"heuristic", "phrases", id}, phrases); !st) {
      (void)overlay.remove("actions", {"actions", id});  // both or neither
      return st.error();
    }
  }
  return out;
}

Result<Json> actions_remove(const fs::path& root, const Json& request) {
  const std::string id = json::get_string(request, "id");
  if (id.empty()) return invalid("id is required (the action to remove)");
  const bool dry_run = json::get_bool(request, "dry_run", false);
  LOOM_TRY_ASSIGN(auto shipped, RuntimeProfile::builtin("actions"));
  OverlayStore overlay(root);
  LOOM_TRY_ASSIGN(Json mine, overlay.entry("actions", {"actions", id}));
  if (mine.is_null()) {
    if (json::find(shipped.values().value("actions", Json::object()), id))
      return Error(Errc::Conflict, "'" + id + "' ships with the application and cannot be removed; switch it off with an overlay (status planned)");
    return Error(Errc::NotFound, "you have no action '" + id + "'");
  }
  LOOM_TRY_ASSIGN(Json all_mine, overlay.entry("actions", {"actions"}));
  for (auto it = all_mine.begin(); it != all_mine.end(); ++it) {
    if (it.key() == id) continue;
    for (const auto& st : it.value().value("steps", Json::array()))
      if (json::get_string(st, "action") == id) return Error(Errc::Conflict, "the action '" + it.key() + "' of yours uses '" + id + "'; remove that one first");
  }
  Json out{{"schema", "loom.action_removed/1"}, {"id", id}, {"origin", mine.value("origin", Json::object())}, {"removed", true}, {"written", !dry_run}};
  if (dry_run) return out;
  LOOM_TRY(overlay.remove("actions", {"actions", id}));
  LOOM_TRY(overlay.remove("intent_routing", {"heuristic", "phrases", id}));
  return out;
}

Result<Json> groups_define(const fs::path& root, const Json& request) {
  const std::string id = json::get_string(request, "id");
  if (!valid_name(id)) return invalid("id must be a name of a-z, 0-9, _ . - (got '" + id + "')");
  const bool dry_run = json::get_bool(request, "dry_run", false);
  const Json* traits_in = json::find(request, "traits");
  if (!traits_in || !traits_in->is_array() || traits_in->empty()) return invalid("traits is required (the traits of the bundle)");
  LOOM_TRY_ASSIGN(auto entry_profile, RuntimeProfile::load("graph_entry", root));
  LOOM_TRY_ASSIGN(auto shipped, RuntimeProfile::builtin("graph_entry"));
  const Json ontology = entry_profile.values().value("ontology", Json::object());
  const Json known_traits = ontology.value("traits", Json::object());
  const Json known_classes = ontology.value("classes", Json::object());
  Json traits = Json::array();
  for (const auto& t : *traits_in) {
    const std::string name = t.is_string() ? t.get<std::string>() : std::string();
    if (name.empty() || (!json::find(known_traits, name) && !json::find(known_classes, name))) return invalid("'" + name + "' is no trait or class of the ontology");
    traits.push_back(name);
  }
  if (json::find(shipped.values().value("ontology", Json::object()).value("groups", Json::object()), id))
    return Error(Errc::Conflict, "a group named '" + id + "' ships with the application; choose another name");
  OverlayStore overlay(root);
  LOOM_TRY_ASSIGN(Json mine, overlay.entry("graph_entry", {"ontology", "groups", id}));
  const bool replaces = !mine.is_null();
  if (replaces && json::get_string(mine.value("origin", Json::object()), "kind") != "emergent")
    return Error(Errc::Conflict, "'" + id + "' is a group you wrote by hand; it is not replaced from a conversation");
  Json group = Json::object();
  group["label"] = request.contains("label") && request["label"].is_object() ? request["label"] : Json{{"en", id}};
  group["traits"] = traits;
  group["element_match"] = json::get_string(request, "element_match", "all");
  group["origin"] = origin_of(request);
  Json out{{"schema", "loom.group_defined/1"}, {"id", id}, {"group", group}, {"replaced", replaces}, {"written", !dry_run}, {"overlays", Json::array({"profiles/graph_entry.pack"})}};
  if (dry_run) return out;
  LOOM_TRY(overlay.put("graph_entry", {"ontology", "groups", id}, group));
  return out;
}

Result<Json> groups_remove(const fs::path& root, const Json& request) {
  const std::string id = json::get_string(request, "id");
  if (id.empty()) return invalid("id is required (the group to remove)");
  const bool dry_run = json::get_bool(request, "dry_run", false);
  LOOM_TRY_ASSIGN(auto shipped, RuntimeProfile::builtin("graph_entry"));
  OverlayStore overlay(root);
  LOOM_TRY_ASSIGN(Json mine, overlay.entry("graph_entry", {"ontology", "groups", id}));
  if (mine.is_null()) {
    if (json::find(shipped.values().value("ontology", Json::object()).value("groups", Json::object()), id))
      return Error(Errc::Conflict, "the group '" + id + "' ships with the application and cannot be removed");
    return Error(Errc::NotFound, "you have no group '" + id + "'");
  }
  Json out{{"schema", "loom.group_removed/1"}, {"id", id}, {"origin", mine.value("origin", Json::object())}, {"removed", true}, {"written", !dry_run}};
  if (dry_run) return out;
  LOOM_TRY(overlay.remove("graph_entry", {"ontology", "groups", id}));
  return out;
}

}  // namespace loom::actions
