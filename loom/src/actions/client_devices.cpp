// The declarations of clients and what they change in the catalogue (see loom/client_devices.h). Data: runtime profile client_devices.
#include "loom/client_devices.h"

#include <algorithm>
#include <chrono>

#include "loom/re/regex.h"
#include "loom/util/ids.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::actions {

namespace {

const Json& object_or_empty(const Json& parent, const char* key) {
  static const Json empty = Json::object();
  const Json* v = json::find(parent, key);
  return v && v->is_object() ? *v : empty;
}

std::string iso_of(std::int64_t ms) { return timeutil::format_iso_utc(timeutil::Clock::time_point(std::chrono::milliseconds(ms))); }

// The catalogue entries a capability makes runnable.
Json actions_of_capability(const Json& catalogue, const std::string& capability) {
  Json ids = Json::array();
  if (!catalogue.is_object()) return ids;
  for (auto it = catalogue.begin(); it != catalogue.end(); ++it)
    if (client_capability(it.value()) == capability) ids.push_back(it.key());
  return ids;
}

std::string known_states(const Json& permissions) {
  std::string out;
  for (auto it = permissions.begin(); it != permissions.end(); ++it) out += (out.empty() ? "" : ", ") + it.key();
  return out;
}

}  // namespace

std::string client_capability(const Json& entry) {
  const Json* c = json::find(entry, "client");
  return c && c->is_object() ? json::get_string(*c, "capability") : std::string();
}

void ClientRegistry::set_clock(std::function<std::int64_t()> now_ms) {
  std::lock_guard lk(mu_);
  clock_ = std::move(now_ms);
}

std::int64_t ClientRegistry::now_ms() const { return clock_ ? clock_() : timeutil::unix_millis(); }

Result<Json> ClientRegistry::declare(const Json& request, const Json& profile, const Json& catalogue) {
  if (!request.is_object()) return Error(Errc::InvalidArgument, "the declaration must be an object {client_id?, client?, capabilities}");
  const Json* given = json::find(request, "capabilities");
  if (!given || !(given->is_object() || given->is_array()))
    return Error(Errc::InvalidArgument, "capabilities is required: {capability: \"granted\" | \"prompt\" | \"denied\"} or a list of names");
  const Json& permissions = object_or_empty(profile, "permissions");
  const Json& aliases = object_or_empty(profile, "aliases");
  const std::string fallback = json::get_string(profile, "default_permission", "granted");

  // The id: chosen by the client (it must look like one) or made here.
  const Json& id_rule = object_or_empty(profile, "client_id");
  std::string id = json::get_string(request, "client_id");
  if (id.empty()) {
    id = gen_id(json::get_string(id_rule, "prefix", "cl_"));
  } else if (const std::string pattern = json::get_string(id_rule, "pattern"); !pattern.empty()) {
    LOOM_TRY_ASSIGN(auto re, re::Regex::compile(pattern));
    if (!re.search_utf8(id)) return Error(Errc::InvalidArgument, "client_id '" + id + "' does not match " + pattern);
  }

  // Each capability with its permission state; an alias stands for the capabilities it names.
  Json caps = Json::object();
  Json refused = Json::object();
  std::string failure;
  auto add = [&](const std::string& name, const Json& spec) {
    std::string permission = fallback;
    Json described = Json::object();
    if (spec.is_string()) permission = spec.get<std::string>();
    else if (spec.is_boolean()) permission = spec.get<bool>() ? fallback : "denied";
    else if (spec.is_object()) {
      permission = json::get_string(spec, "permission", fallback);
      for (const char* k : {"via", "detail"})
        if (const std::string v = json::get_string(spec, k); !v.empty()) described[k] = v;
    }
    const Json* state = json::find(permissions, permission);
    if (!state) {
      failure = "capability '" + name + "': unknown permission '" + permission + "' (known: " + known_states(permissions) + ")";
      return;
    }
    const bool runnable = json::get_bool(*state, "runnable", false);
    described["permission"] = permission;
    described["runnable"] = runnable;
    described["actions"] = actions_of_capability(catalogue, name);
    if (!runnable) refused[name] = permission;
    caps[name] = std::move(described);
  };
  auto expand = [&](const std::string& name, const Json& spec) {
    if (const Json* alias = json::find(aliases, name); alias && alias->is_array()) {
      for (const auto& target : *alias)
        if (target.is_string()) add(target.get<std::string>(), spec);
      return;
    }
    add(name, spec);
  };
  if (given->is_array()) {
    for (const auto& name : *given) {
      if (!name.is_string() || name.get<std::string>().empty()) return Error(Errc::InvalidArgument, "a capability name must be a non-empty string");
      expand(name.get<std::string>(), Json(fallback));
    }
  } else {
    for (auto it = given->begin(); it != given->end(); ++it) expand(it.key(), it.value());
  }
  if (!failure.empty()) return Error(Errc::InvalidArgument, failure);

  Json runnable_actions = Json::array();
  for (auto it = caps.begin(); it != caps.end(); ++it)
    if (json::get_bool(it.value(), "runnable", false))
      for (const auto& a : it.value().at("actions"))
        if (std::find(runnable_actions.begin(), runnable_actions.end(), a) == runnable_actions.end()) runnable_actions.push_back(a);

  Json client = Json::object();
  if (const Json* c = json::find(request, "client"); c && c->is_object())
    for (const char* k : {"kind", "platform", "label"})
      if (const std::string v = json::get_string(*c, k); !v.empty()) client[k] = v;

  std::lock_guard lk(mu_);
  const std::int64_t now = now_ms();
  const Json* ttl = json::find(profile, "ttl_seconds");
  Entry entry;
  if (ttl && ttl->is_number_integer()) entry.expires_ms = now + ttl->get<std::int64_t>() * 1000;
  Json declaration = Json::object();
  declaration["schema"] = std::string(kClientSchema);
  declaration["client_id"] = id;
  declaration["client"] = client;
  declaration["declared"] = iso_of(now);
  if (entry.expires_ms >= 0) declaration["expires"] = iso_of(entry.expires_ms);
  declaration["capabilities"] = caps;
  declaration["runnable_actions"] = runnable_actions;
  declaration["refused"] = refused;
  entry.declaration = declaration;
  for (auto it = clients_.begin(); it != clients_.end();)  // the ones that expired are forgotten here
    it = it->second.expires_ms >= 0 && it->second.expires_ms <= now ? clients_.erase(it) : std::next(it);
  clients_[id] = std::move(entry);
  return declaration;
}

Json ClientRegistry::find(std::string_view client_id) const {
  if (client_id.empty()) return Json(nullptr);
  std::lock_guard lk(mu_);
  auto it = clients_.find(client_id);
  if (it == clients_.end()) return Json(nullptr);
  if (it->second.expires_ms >= 0 && it->second.expires_ms <= now_ms()) return Json(nullptr);
  return it->second.declaration;
}

Json ClientRegistry::list() const {
  Json out = Json::array();
  std::lock_guard lk(mu_);
  const std::int64_t now = now_ms();
  for (const auto& [id, entry] : clients_) {
    if (entry.expires_ms >= 0 && entry.expires_ms <= now) continue;
    Json row{{"client_id", id}, {"client", entry.declaration.value("client", Json::object())}, {"declared", entry.declaration.value("declared", "")}};
    if (const Json* e = json::find(entry.declaration, "expires")) row["expires"] = *e;
    out.push_back(std::move(row));
  }
  return out;
}

Json client_patches(const Json& catalogue, const Json& declaration, const Json& profile) {
  Json patches = Json::object();
  if (!declaration.is_object() || !catalogue.is_object()) return patches;
  const Json& caps = object_or_empty(declaration, "capabilities");
  const Json& texts = object_or_empty(profile, "texts");
  const Json& permissions = object_or_empty(profile, "permissions");
  for (auto it = catalogue.begin(); it != catalogue.end(); ++it) {
    const std::string capability = client_capability(it.value());
    if (capability.empty()) continue;
    const Json* declared = json::find(caps, capability);
    if (!declared) continue;  // this client did not declare it: the action stays planned, with its own reason
    const std::string permission = json::get_string(*declared, "permission");
    Json patch = Json::object();
    patch["client_permission"] = permission;
    if (json::get_bool(*declared, "runnable", false)) {
      const bool asks = [&] {
        const Json* state = json::find(permissions, permission);
        return state && json::get_bool(*state, "asks", false);
      }();
      patch["status"] = "implemented";
      patch["runs_on"] = "client";
      if (const Json* note = json::find(texts, asks ? "will_ask" : "runnable")) patch["runnable_note"] = *note;
    } else if (const Json* reason = json::find(texts, "denied")) {
      patch["planned_reason"] = *reason;
    }
    patches[it.key()] = std::move(patch);
  }
  return patches;
}

Json client_step_spec(const Json& entry, std::string_view run_id, std::string_view step_id, const Json& with, const Json& inputs) {
  const Json client = entry.value("client", Json::object());
  Json spec = Json::object();
  spec["schema"] = std::string(kClientStepSchema);
  spec["run_id"] = std::string(run_id);
  spec["step"] = std::string(step_id);
  spec["action"] = json::get_string(entry, "id");
  spec["capability"] = json::get_string(client, "capability");
  if (const std::string permission = json::get_string(client, "permission"); !permission.empty()) spec["permission"] = permission;
  spec["title"] = json::get_string(entry, "title");
  if (const Json* labels = json::find(entry, "labels")) spec["labels"] = *labels;
  if (const Json* prompt = json::find(client, "prompt")) spec["prompt"] = *prompt;
  spec["expects"] = client.value("expects", Json::array());
  spec["with"] = with.is_object() ? with : Json::object();
  spec["inputs"] = inputs.is_array() ? inputs : Json::array();
  return spec;
}

}  // namespace loom::actions
