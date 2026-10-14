// Profile `ports` (B-MOD-03): role -> adapter + parameters, resolved through the scope chain of the action
// engine (loom/action_rules.h, Engine::resolve_value — reused, not copied):
//
//   installation  the shipped preset (data/runtime/ports.pack)
//   person        <data_dir>/profiles/ports.pack (the user's overlay)
//   conversation  conversations.metadata.ports[role]
//
// The most specific scope wins unless the data's conflict rules say otherwise; rules of dimension `value`
// targeting the setting "ports.<role>" take part. explain() answers where the choice came from.
// Read-only: nothing here changes what the runtime uses (the consumers adopt the role one by one).
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "loom/action_rules.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class PortProfile {
 public:
  // data_dir may be empty (shipped data only).
  static Result<PortProfile> load(const std::filesystem::path& data_dir = {});

  // `conversation`: the conversation's metadata.ports object ({role: entry}) and its id (may be empty).
  // `settings`: config values for entries with "setting" (params.model comes from config when not set).
  // Result: {role, defined, entry, adapter?, compose?, params, scope, scope_id?, source, file?}.
  Result<Json> resolve(const std::string& role, const Json& conversation = Json::object(),
                       const std::string& conversation_id = {}, const Json& settings = Json::object()) const;
  // resolve() + layers: [{scope, source, file?, defined, entry?, winner}] and the engine's considered list.
  Result<Json> explain(const std::string& role, const Json& conversation = Json::object(),
                       const std::string& conversation_id = {}, const Json& settings = Json::object()) const;
  // Every role the preset or the user's file defines, resolved at the person level.
  Json roles() const;

  static constexpr const char* kUserFile = "profiles/ports.pack";

 private:
  struct Layer {
    std::string level, source, file;
    Json roles = Json::object();
  };
  std::shared_ptr<const actions::Engine> engine_;
  std::vector<Layer> layers_;  // installation, person
  static Json entry_for(const Json& roles, const std::string& role);
  Result<Json> run(const std::string& role, const Json& conversation, const std::string& conversation_id,
                   const Json& settings, bool explain) const;
};

}  // namespace loom
