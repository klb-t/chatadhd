#include "loom/port_profile.h"

#include "loom/runtime_profile.h"

namespace loom {
using actions::Baseline;

namespace {
Json roles_of(const RuntimeProfile& p) {
  const Json* r = json::find(p.values(), "roles");
  return r && r->is_object() ? *r : Json::object();
}
}  // namespace

// Exact role first, then the longest "prefix.*" pattern.
Json PortProfile::entry_for(const Json& roles, const std::string& role) {
  if (const Json* e = json::find(roles, role); e && e->is_object()) return *e;
  std::string best;
  Json found = nullptr;
  for (auto it = roles.begin(); it != roles.end(); ++it) {
    const std::string& k = it.key();
    if (k.size() < 2 || k.compare(k.size() - 2, 2, ".*") != 0) continue;
    const std::string prefix = k.substr(0, k.size() - 1);  // keeps the dot
    if (role.compare(0, prefix.size(), prefix) == 0 && prefix.size() > best.size() && it.value().is_object()) {
      best = prefix;
      found = it.value();
    }
  }
  return found;
}

Result<PortProfile> PortProfile::load(const std::filesystem::path& data_dir) {
  PortProfile p;
  LOOM_TRY_ASSIGN(auto engine, actions::Engine::load(data_dir));
  p.engine_ = std::make_shared<const actions::Engine>(std::move(engine));
  LOOM_TRY_ASSIGN(auto builtin, RuntimeProfile::builtin("ports"));
  p.layers_.push_back({"installation", "profile:ports (shipped preset)", "data/runtime/ports.pack", roles_of(builtin)});
  if (!data_dir.empty()) {
    LOOM_TRY_ASSIGN(auto user, RuntimeProfile::load("ports", data_dir));
    // The person layer holds only what the overlay changed.
    Json changed = Json::object();
    const Json base = roles_of(builtin);
    const Json eff = roles_of(user);
    for (auto it = eff.begin(); it != eff.end(); ++it) {
      const Json* b = json::find(base, it.key());
      if (!b || *b != it.value()) changed[it.key()] = it.value();
    }
    p.layers_.push_back({"person", "profile:ports (user overlay)", (data_dir / kUserFile).string(), std::move(changed)});
  } else {
    p.layers_.push_back({"person", "profile:ports (user overlay)", kUserFile, Json::object()});
  }
  return p;
}

Result<Json> PortProfile::run(const std::string& role, const Json& conversation, const std::string& conversation_id,
                              const Json& settings, bool explain) const {
  if (role.empty()) return Error(Errc::InvalidArgument, "role is required");
  const std::string key = "ports." + role;
  std::vector<Baseline> baselines;
  Json layers = Json::array();
  int order = 0;
  auto add = [&](const std::string& level, const std::string& source, const std::string& file, const Json& roles,
                 const std::string& scope_id) {
    Json e = entry_for(roles, role);
    Json layer{{"scope", level}, {"source", source}, {"defined", !e.is_null()}};
    if (!file.empty()) layer["file"] = file;
    if (!scope_id.empty()) layer["scope_id"] = scope_id;
    if (!e.is_null()) {
      layer["entry"] = e;
      baselines.emplace_back(level, e, source, order++, scope_id);
    }
    layers.push_back(std::move(layer));
  };
  for (const auto& l : layers_) add(l.level, l.source, l.file, l.roles, "");
  add("conversation", "conversation metadata.ports",
      conversation_id.empty() ? std::string("conversations.metadata.ports") : "conversations/" + conversation_id,
      conversation.is_object() ? conversation : Json::object(), conversation_id);

  Json scope_json = conversation_id.empty() ? Json("conversation") : Json{{"conversation", conversation_id}};
  LOOM_TRY_ASSIGN(auto scope, engine_->parse_scope(scope_json));
  LOOM_TRY_ASSIGN(auto sv, engine_->resolve_value(key, scope, baselines));
  Json out{{"role", role}, {"defined", sv.defined}};
  if (sv.defined) {
    Json entry = sv.value;
    Json params = entry.contains("params") && entry["params"].is_object() ? entry["params"] : Json::object();
    const std::string setting = json::get_string(entry, "setting");
    if (!setting.empty() && !params.contains("model")) {
      const std::string model = json::get_string(settings, setting);
      if (!model.empty()) params["model"] = model;
    }
    out["entry"] = entry;
    out["params"] = params;
    if (entry.contains("adapter")) out["adapter"] = entry["adapter"];
    if (entry.contains("compose")) out["compose"] = entry["compose"];
    out["scope"] = sv.scope;
    if (!sv.scope_id.empty()) out["scope_id"] = sv.scope_id;
    out["source"] = sv.source == "rule" ? "rule:" + sv.rule : sv.source;
    for (const auto& l : layers) {
      if (l["scope"] == sv.scope && l["defined"].get<bool>()) {
        if (l.contains("file")) out["file"] = l["file"];
        break;
      }
    }
  }
  if (explain) {
    for (auto& l : layers) l["winner"] = sv.defined && l["scope"] == sv.scope && l["defined"].get<bool>();
    out["layers"] = std::move(layers);
    out["engine"] = sv.to_json();
    out["considered"] = sv.considered;
  }
  return out;
}

Result<Json> PortProfile::resolve(const std::string& role, const Json& conversation, const std::string& conversation_id,
                                  const Json& settings) const {
  return run(role, conversation, conversation_id, settings, false);
}
Result<Json> PortProfile::explain(const std::string& role, const Json& conversation, const std::string& conversation_id,
                                  const Json& settings) const {
  return run(role, conversation, conversation_id, settings, true);
}

Json PortProfile::roles() const {
  Json names = Json::array();
  std::vector<std::string> seen;
  for (const auto& l : layers_)
    for (auto it = l.roles.begin(); it != l.roles.end(); ++it)
      if (std::find(seen.begin(), seen.end(), it.key()) == seen.end()) {
        seen.push_back(it.key());
        names.push_back(it.key());
      }
  return names;
}

}  // namespace loom
