#include "loom/onboarding_layers.h"

#include <set>
#include <string>

#include "loom/util/sha256.h"

namespace loom::onboarding {
namespace {

Error invalid(std::string message) {
  return Error(Errc::InvalidArgument, "default layers: " + std::move(message));
}
Error conflict(std::string message) {
  return Error(Errc::Conflict, "default layers: " + std::move(message));
}

bool nonempty_string(const Json& object, const char* key) {
  return object.contains(key) && object[key].is_string() && !object[key].get_ref<const std::string&>().empty();
}
bool positive_revision(const Json& object, const char* key) {
  if (!object.contains(key) || !object[key].is_number_integer()) return false;
  return object[key].is_number_unsigned() ? object[key].get<std::uint64_t>() > 0
                                        : object[key].get<std::int64_t>() > 0;
}
bool mode_valid(const Json& mode) { return mode == "proposal" || mode == "direct"; }

Status validate_pack(const Json& pack) {
  if (!pack.is_object() || pack.value("schema", Json{}) != "loom.default_layers_pack/1" ||
      !nonempty_string(pack, "pack_id") || !positive_revision(pack, "revision") ||
      !pack.contains("entries") || !pack["entries"].is_array())
    return invalid("pack needs schema, pack_id, positive revision and entries array");
  if (!pack.contains("policy") || !pack["policy"].is_object() ||
      !pack["policy"].contains("excluded_area_new_defaults") ||
      !mode_valid(pack["policy"]["excluded_area_new_defaults"]))
    return invalid("pack policy must declare excluded_area_new_defaults: proposal or direct");
  std::set<std::string> ids, keys;
  for (const auto& entry : pack["entries"]) {
    if (!entry.is_object() || !nonempty_string(entry, "id") || !nonempty_string(entry, "key") ||
        !nonempty_string(entry, "area") || !positive_revision(entry, "revision") || !entry.contains("value"))
      return invalid("every entry needs stable id, key, area, positive revision and value");
    if (!ids.insert(entry["id"].get<std::string>()).second || !keys.insert(entry["key"].get<std::string>()).second)
      return conflict("duplicate default id or key");
  }
  return ok_status();
}

Result<Json> initialise(const Json& original) {
  if (!original.is_object()) return invalid("persistent state must be an object");
  Json state = original;
  if (state.contains("schema") && state["schema"] != "loom.default_layers_state/1")
    return Error(Errc::Unsupported, "default layers: unsupported state version; state was not modified");
  state["schema"] = "loom.default_layers_state/1";
  for (const char* member : {"catalog", "overrides", "disabled", "exclusions", "areas", "proposals"}) {
    if (!state.contains(member)) state[member] = Json::object();
    if (!state[member].is_object()) return invalid(std::string(member) + " must be an object");
  }
  if (!state.contains("history")) state["history"] = Json::array();
  if (!state["history"].is_array()) return invalid("history must be an array");
  std::set<std::string> known_keys;
  for (const auto& [id, record] : state["catalog"].items()) {
    if (!record.is_object() || !record.contains("entry") || !record["entry"].is_object())
      return invalid("catalog record is malformed");
    const auto& entry = record["entry"];
    if (!nonempty_string(entry, "id") || entry["id"] != id || !nonempty_string(entry, "key") ||
        !nonempty_string(entry, "area") || !positive_revision(entry, "revision") || !entry.contains("value"))
      return invalid("catalog identity is malformed");
    if (!known_keys.insert(entry["key"].get<std::string>()).second)
      return conflict("catalog contains colliding stable keys");
  }
  for (const auto& [key, record] : state["overrides"].items())
    if (key.empty() || !record.is_object() || !record.contains("value")) return invalid("override needs value");
  for (const auto& [area, record] : state["areas"].items())
    if (area.empty() || !record.is_object() || !record.contains("new_defaults_mode") ||
        !mode_valid(record["new_defaults_mode"])) return invalid("excluded area needs a valid mode");
  for (const char* map : {"disabled", "exclusions", "proposals"}) {
    for (const auto& [id, record] : state[map].items()) {
      if (!record.is_object() || !state["catalog"].contains(id) || !nonempty_string(record, "key") ||
          record["key"] != state["catalog"][id]["entry"]["key"])
        return invalid(std::string(map) + " record has no matching stable identity");
      if (std::string_view(map) == "exclusions" &&
          (!nonempty_string(record, "area") || record["area"] != state["catalog"][id]["entry"]["area"]))
        return invalid("exclusion area disagrees with stable identity");
    }
  }
  if (state.contains("pack_revision") && !positive_revision(state, "pack_revision"))
    return invalid("pack_revision must be positive");
  return state;
}

Result<Json> migrate(const Json& pack, const Json& original) {
  LOOM_TRY(validate_pack(pack));
  LOOM_TRY_ASSIGN(auto state, initialise(original));
  if (state.contains("pack_id") && state["pack_id"] != pack["pack_id"])
    return conflict("cannot change the pack identity of existing user state");
  const std::string fingerprint = Sha256::hex(json::canonical(pack));
  if (state.contains("pack_revision")) {
    if (pack["revision"] < state["pack_revision"]) return conflict("pack revision cannot move backwards");
    if (pack["revision"] == state["pack_revision"] && state.contains("pack_fingerprint") &&
        state["pack_fingerprint"] != fingerprint) return conflict("pack content changed without a revision increment");
  }
  // Keep every historical identity so a removed id/key cannot be recycled to
  // escape a permanent exclusion. Unknown source/graph metadata are retained.
  for (const auto& entry : pack["entries"]) {
    const std::string id = entry["id"], key = entry["key"], area = entry["area"];
    const bool is_new = !state["catalog"].contains(id);
    for (const auto& [other_id, record] : state["catalog"].items())
      if (other_id != id && record["entry"]["key"] == key)
        return conflict("stable key reused by another id: " + key);
    if (!is_new) {
      const auto& old = state["catalog"][id]["entry"];
      if (old["key"] != key || old["area"] != area) return conflict("stable id changed its key or area: " + id);
      if (entry["revision"] < old["revision"]) return conflict("entry revision cannot move backwards: " + id);
      if (entry["revision"] == old["revision"] && json::canonical(entry) != json::canonical(old))
        return conflict("entry content changed without a revision increment: " + id);
    }
    if (is_new) {
      state["catalog"][id] = Json{{"entry", entry}, {"first_pack_revision", pack["revision"]}};
      if (state["areas"].contains(area) && state["areas"][area]["new_defaults_mode"] == "proposal")
        state["proposals"][id] = Json{{"key", key}, {"area", area}, {"created_pack_revision", pack["revision"]}};
    } else state["catalog"][id]["entry"] = entry;
  }
  state["pack_id"] = pack["pack_id"];
  state["pack_revision"] = pack["revision"];
  state["pack_fingerprint"] = fingerprint;
  return state;
}

const Json* active_entry(const Json& pack, std::string_view key) {
  for (const auto& entry : pack["entries"]) if (entry["key"] == key) return &entry;
  return nullptr;
}
const Json* historical_entry(const Json& state, std::string_view key) {
  for (const auto& [id, record] : state["catalog"].items()) {
    (void)id;
    if (record["entry"]["key"] == key) return &record["entry"];
  }
  return nullptr;
}
}  // namespace

Result<DefaultLayers> DefaultLayers::create(const Json& pack, const Json& state) {
  try {
    LOOM_TRY_ASSIGN(auto migrated, migrate(pack, state));
    DefaultLayers result;
    result.pack_ = pack;
    result.state_ = std::move(migrated);
    return result;
  } catch (const Json::exception& e) { return invalid(e.what()); }
}

Result<Json> DefaultLayers::resolve(std::string_view key) const {
  if (key.empty()) return invalid("key must not be empty");
  try {
    const auto* entry = active_entry(pack_, key);
    const auto* historical = entry ? entry : historical_entry(state_, key);
    Json result{{"key", key}, {"status", "missing"}, {"layer", "none"},
                {"explanation", "No current default or user value exists for this key."}};
    if (historical) {
      const std::string id = (*historical)["id"];
      result["id"] = id;
      result["area"] = (*historical)["area"];
      result["revision"] = (*historical)["revision"];
      result["entity"] = *historical;
      result["entity"].erase("value");
      if (state_["exclusions"].contains(id)) {
        result["status"] = "excluded"; result["layer"] = "user_exclusion";
        result["explanation"] = "A durable user exclusion blocks this stable id across pack updates.";
        result["source"] = state_["exclusions"][id];
        return result;
      }
      if (state_["disabled"].contains(id)) {
        result["status"] = "disabled"; result["layer"] = "user_disabled";
        result["explanation"] = "The user retained this default but disabled its use.";
        result["source"] = state_["disabled"][id];
        return result;
      }
    }
    const std::string string_key(key);
    if (state_["overrides"].contains(string_key)) {
      result["status"] = "effective"; result["layer"] = "user";
      result["value"] = state_["overrides"][string_key]["value"];
      result["source"] = state_["overrides"][string_key];
      result["explanation"] = "An explicit user value overrides the built-in graph layer.";
    } else if (entry) {
      const std::string id = (*entry)["id"];
      if (state_["proposals"].contains(id)) {
        result["status"] = "proposal"; result["layer"] = "pack_proposal";
        result["explanation"] = "This new default touches an excluded area and awaits user acceptance.";
        result["source"] = state_["proposals"][id];
      } else {
        result["status"] = "effective"; result["layer"] = "builtin";
        result["value"] = (*entry)["value"];
        result["source"] = Json{{"pack_id", pack_["pack_id"]}, {"pack_revision", pack_["revision"]},
                                {"entry_id", id}, {"entry_revision", (*entry)["revision"]}};
        result["explanation"] = "The built-in graph default applies because no user suppression or override exists.";
      }
    }
    return result;
  } catch (const Json::exception& e) { return invalid(e.what()); }
}

Result<Json> DefaultLayers::dispatch(const Json& action) const {
  try {
    if (!action.is_object() || !nonempty_string(action, "op")) return invalid("action needs an op");
    const std::string op = action["op"];
    Json state = state_;
    if (op == "set_area_mode") {
      if (!nonempty_string(action, "area") || !action.contains("mode") || !mode_valid(action["mode"]))
        return invalid("set_area_mode needs area and proposal/direct mode");
      const std::string area = action["area"];
      if (!state["areas"].contains(area)) return Error(Errc::NotFound, "default layers: area has no exclusion");
      state["areas"][area]["new_defaults_mode"] = action["mode"];
      // The explicit mode change may accept waiting NEW defaults. Durable id
      // exclusions have higher precedence and are never cleared here.
      if (action["mode"] == "direct") {
        for (auto it = state["proposals"].begin(); it != state["proposals"].end();) {
          if (it.value()["area"] == area) it = state["proposals"].erase(it); else ++it;
        }
      }
    } else {
      if (!nonempty_string(action, "key")) return invalid("action needs a key");
      const std::string key = action["key"];
      const auto* entry = historical_entry(state_, key);
      const std::string id = entry ? (*entry)["id"].get<std::string>() : "";
      if (op == "override") {
        if (!action.contains("value")) return invalid("override needs value (null is an ordinary value)");
        if (entry && state["exclusions"].contains(id)) return conflict("explicit reenable is required before override");
        Json record = action; record.erase("op"); record.erase("key");
        state["overrides"][key] = std::move(record);
        if (entry) state["proposals"].erase(id);
      } else if (op == "clear_override") {
        state["overrides"].erase(key);
      } else {
        if (!entry) return Error(Errc::NotFound, "default layers: no default identity for " + key);
        const std::string area = (*entry)["area"];
        if (op == "disable") state["disabled"][id] = Json{{"key", key}, {"pack_revision", pack_["revision"]}};
        else if (op == "exclude") {
          state["exclusions"][id] = Json{{"key", key}, {"area", area}, {"pack_revision", pack_["revision"]}};
          state["overrides"].erase(key);
          state["proposals"].erase(id);
          if (!state["areas"].contains(area))
            state["areas"][area] = Json{{"new_defaults_mode", pack_["policy"]["excluded_area_new_defaults"]},
                                       {"excluded_at_pack_revision", pack_["revision"]}};
        } else if (op == "reenable") {
          state["exclusions"].erase(id); state["disabled"].erase(id); state["proposals"].erase(id);
        } else if (op == "accept_proposal") {
          if (state["exclusions"].contains(id)) return conflict("accepting a proposal cannot clear permanent exclusion");
          if (!state["proposals"].contains(id)) return Error(Errc::NotFound, "default layers: no proposal for " + key);
          state["proposals"].erase(id);
        } else return invalid("unknown action: " + op);
      }
    }
    // History records the action and optional caller-provided provenance/time.
    // Exclusion history contains the exclusion instruction, not a removed value.
    state["history"].push_back(action);
    return state;
  } catch (const Json::exception& e) { return invalid(e.what()); }
}

Result<Json> DefaultLayers::update_pack(const Json& pack) const {
  try { return migrate(pack, state_); }
  catch (const Json::exception& e) { return invalid(e.what()); }
}

}  // namespace loom::onboarding
