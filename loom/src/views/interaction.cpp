// Interaction as data: effective bindings per device, conflicts, unsupported. See include/loom/interaction.h.
#include "loom/interaction.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace loom::views {
namespace {

std::vector<std::string> strs(const Json& v) {
  std::vector<std::string> out;
  if (v.is_array())
    for (const auto& s : v)
      if (s.is_string()) out.push_back(s.get<std::string>());
  return out;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// The same input must give the same key whatever the order of the lists or the case of the names.
std::string normalized_key(const std::string& device, const Json& input) {
  std::string out = device;
  std::vector<std::string> fields;
  if (input.is_object())
    for (auto it = input.begin(); it != input.end(); ++it) fields.push_back(it.key());
  std::sort(fields.begin(), fields.end());
  for (const auto& f : fields) {
    const Json& v = input[f];
    std::string text;
    if (v.is_array()) {
      std::vector<std::string> items = strs(v);
      for (auto& i : items) i = lower(i);
      std::sort(items.begin(), items.end());
      if (items.empty()) continue;
      for (const auto& i : items) text += (text.empty() ? "" : "+") + i;
    } else if (v.is_string()) {
      text = lower(v.get<std::string>());
    } else {
      text = json::dump(v);
    }
    out += ";" + f + "=" + text;
  }
  return out;
}

}  // namespace

Result<Interaction> Interaction::open(const std::filesystem::path& root, const std::vector<std::filesystem::path>& extra_dirs) {
  Interaction i;
  LOOM_TRY_ASSIGN(i.bindings_, effective(root, "interaction_bindings", extra_dirs));
  LOOM_TRY_ASSIGN(i.devices_, effective(root, "interaction_devices", extra_dirs));
  LOOM_TRY_ASSIGN(i.actions_, effective(root, "interaction_actions", extra_dirs));
  LOOM_TRY_ASSIGN(i.rules_, effective(root, "interaction_rules", extra_dirs));
  LOOM_TRY_ASSIGN(i.declarations_, effective(root, "view_declarations", extra_dirs));
  return i;
}

Json Interaction::resolve(const Json& request) const {
  std::map<std::string, Json> messages;
  for (const auto* entry : rules_.active())
    if (json::get_string(entry->value, "kind") == "messages") {
      const Json texts = entry->value.value("texts", Json::object());
      for (auto it = texts.begin(); it != texts.end(); ++it) messages[it.key()] = it.value();
    }
  auto say = [&](const std::string& code, const Json& params) {
    Json out = Json::object();
    const auto it = messages.find(code);
    for (const char* lang : {"en", "pl"}) {
      std::string s = code;
      if (it != messages.end() && it->second.is_object()) s = json::get_string(it->second, lang, json::get_string(it->second, "en", code));
      if (params.is_object())
        for (auto p = params.begin(); p != params.end(); ++p) {
          const std::string needle = "{" + p.key() + "}";
          const std::string value = p.value().is_string() ? p.value().get<std::string>() : json::dump(p.value());
          for (std::size_t at = s.find(needle); at != std::string::npos; at = s.find(needle, at + value.size())) s.replace(at, needle.size(), value);
        }
      out[lang] = s;
    }
    return out;
  };
  auto reason = [&](const std::string& code, const std::string& level, const Json& params) {
    return Json{{"code", code}, {"level", level}, {"text", say(code, params)}, {"params", params}};
  };

  const std::string wanted = json::get_string(request, "device");
  Json issues = Json::array();
  for (const Effective* e : {&bindings_, &devices_, &actions_, &rules_})
    for (const auto& i : e->issues) issues.push_back(i);

  Json devices = Json::array();
  for (const auto& d : devices_.entries)
    devices.push_back(Json{{"id", d.id}, {"label", d.value.value("label", Json::object())}, {"implemented", json::get_bool(d.value, "implemented", false)},
                           {"inputs", d.value.value("inputs", Json::array())}, {"disabled", d.disabled}});
  if (!wanted.empty() && !devices_.find(wanted))
    issues.push_back(Json{{"code", "unknown_device_requested"}, {"text", say("unknown_device_requested", Json{{"device", wanted}})}, {"params", Json{{"device", wanted}}}});

  // The action kinds with their labels (and axis pairs): what a renderer needs to name a group of bindings, e.g. "plane" for the arrows.
  Json actions = Json::array();
  for (const auto& a : actions_.entries) {
    Json item{{"id", a.id}, {"label", a.value.value("label", Json::object())}, {"implemented", json::get_bool(a.value, "implemented", false)}, {"disabled", a.disabled}};
    if (a.value.contains("pairs")) item["pairs"] = a.value["pairs"];
    if (a.value.contains("targets")) item["targets"] = a.value["targets"];
    actions.push_back(item);
  }

  Json bindings = Json::array(), unsupported = Json::array(), disabled = Json::array();
  std::map<std::string, std::vector<std::string>> by_key;  // normalized key -> binding ids in effective order
  std::map<std::string, Json> input_of;
  std::map<std::string, std::size_t> index_of;  // binding id -> index in `bindings`

  for (const auto& e : bindings_.entries) {
    const Json& v = e.value;
    const std::string device = json::get_string(v, "device");
    if (!wanted.empty() && device != wanted) continue;
    if (e.disabled) {
      disabled.push_back(e.id);
      continue;
    }
    auto reject = [&](const std::string& code, const Json& params) {
      unsupported.push_back(Json{{"binding", e.id}, {"device", device}, {"reason", reason(code, "warning", params)}});
    };
    if (device.empty()) { reject("binding_without_device", Json::object()); continue; }
    const EffectiveEntry* dev = devices_.find(device);
    if (!dev || dev->disabled) { reject("unsupported_device", Json{{"device", device}}); continue; }
    const Json input = v.value("input", Json(nullptr));
    if (!input.is_object() || input.empty()) { reject("binding_without_input", Json::object()); continue; }
    const Json action = v.value("action", Json(nullptr));
    if (!action.is_object()) { reject("binding_without_action", Json::object()); continue; }
    const std::set<std::string> known = [&] {
      auto l = strs(dev->value.value("inputs", Json::array()));
      return std::set<std::string>(l.begin(), l.end());
    }();
    bool bad = false;
    for (auto it = input.begin(); it != input.end(); ++it)
      if (!known.count(it.key())) { reject("unsupported_input_field", Json{{"device", device}, {"field", it.key()}}); bad = true; break; }
    if (bad) continue;
    const std::string kind = json::get_string(action, "kind");
    const EffectiveEntry* act = actions_.find(kind);
    if (kind.empty() || !act || act->disabled) { reject("unsupported_action_kind", Json{{"kind", kind}}); continue; }

    Json reasons = Json::array();
    bool implemented = json::get_bool(dev->value, "implemented", false);
    if (!implemented) reasons.push_back(reason("device_not_implemented", "info", Json{{"device", device}}));
    // A device can be handled while some of its input fields are still only recorded (xr: the voice phrase): such a binding is "recorded".
    if (implemented)
      for (const auto& field : strs(dev->value.value("recorded_inputs", Json::array())))
        if (input.contains(field)) {
          implemented = false;
          reasons.push_back(reason("input_not_implemented", "info", Json{{"device", device}, {"field", field}}));
          break;
        }
    if (kind == "switch_view") {
      const std::string decl = json::get_string(action, "declaration");
      const EffectiveEntry* d = declarations_.find(decl);
      if (!d || d->disabled) reasons.push_back(reason("switch_view_unknown_declaration", "warning", Json{{"declaration", decl}}));
    }
    const std::string key = normalized_key(device, input);
    Json b{{"id", e.id}, {"label", v.value("label", Json(e.id))}, {"device", device}, {"input", input}, {"action", action}, {"key", key},
           {"origin", e.origin}, {"provenance", e.provenance}, {"status", implemented ? "active" : "recorded"}, {"reasons", reasons}};
    index_of[e.id] = bindings.size();
    bindings.push_back(b);
    by_key[key].push_back(e.id);
    input_of[key] = input;
  }

  Json conflicts = Json::array();
  for (const auto& [key, ids] : by_key) {
    if (ids.size() < 2) continue;
    const std::string winner = ids.back();
    Json id_list = Json::array();
    std::string joined;
    for (const auto& id : ids) {
      id_list.push_back(id);
      joined += (joined.empty() ? "" : ", ") + id;
    }
    for (const auto& id : ids) {
      Json& b = bindings[index_of[id]];
      b["reasons"].push_back(reason("conflict", "warning", Json{{"input", key}, {"ids", joined}, {"winner", winner}}));
      if (id != winner) {
        b["shadowed"] = true;
        b["shadowed_by"] = winner;
      }
    }
    conflicts.push_back(Json{{"key", key}, {"input", input_of[key]}, {"ids", id_list}, {"winner", winner},
                             {"message", say("conflict", Json{{"input", key}, {"ids", joined}, {"winner", winner}})}});
  }
  return Json{{"schema", "loom.view_interaction/1"}, {"device", wanted}, {"devices", devices}, {"actions", actions}, {"bindings", bindings}, {"conflicts", conflicts},
              {"disabled", disabled}, {"unsupported", unsupported}, {"issues", issues}};
}

}  // namespace loom::views
