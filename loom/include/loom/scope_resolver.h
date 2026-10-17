// loom/scope_resolver.h — ONE resolution chain for any setting or value (P24, a).
//
//   conversation -> organizational units (project, team, branch, anything: unit kinds are data, views `unit_kinds`) -> user -> installation
//
// This extends what exists, it does not replace it: the chain of levels is graph/scopes.pack (R47, the effective `scopes` view with the user's
// graph_entry overlay), the arbitration is Engine::resolve_value (conflict rules as data), rules of dimension `value` (rules.set at any level,
// with a scope_id for the instance) take part. What this adds is the one place where the BASELINES of a setting are assembled:
//   installation  the shipped preset of the setting (Binding.installation, data)
//   person        the user's setting in config.json (Binding.person_setting, default = the key)
//   unit levels   conversations.metadata.units = {unit kind or level: unit id}; the kind maps to a chain level through `unit_kinds`
//                 (add a kind or a level by data); a unit's value is a rule of that level bound to the unit's id, or Binding.unit_values
//   conversation  conversations.metadata.settings[key]
// Every consumer (guardian settings, annotations::resolve_model, ...) calls resolve(); none reads the layers itself.
// explain: the same answer with the whole chain, which level supplied the value and which were overridden, and the "dziedziczenie"
// (inheritance) graph perspective built from it (nodes = levels, edges inherits_from / overrides).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "loom/action_rules.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Config;
class Database;
}


namespace loom::scopes {

struct Binding {
  std::string key;                   // the value's key, as rules target it ({kind: setting, key})
  Json installation;                 // the shipped preset (null = none)
  std::string installation_source;   // "profile:..." (for provenance)
  std::string person_setting;        // config key of the user's value ("" = key); null and "" count as not set
  std::string conv_id;               // "" = no conversation
  Json units = nullptr;              // {unit kind|level: id}; null = read conversations.metadata.units of conv_id
  Json unit_values = nullptr;        // {level: value} baselines of unit levels (tools, tests); rules are the persistent way
  Json conversation_value = nullptr; // null = conversations.metadata.settings[key]
};

struct Resolution {
  bool defined = false;
  Json value = nullptr;
  std::string scope;                 // level that supplied the value
  std::string scope_id;
  std::string source;
  actions::ScopedValue engine;       // the engine's full answer (considered candidates)
  Json chain = Json::array();        // [{level, label, active, scope_id?, role: supplied|overridden|inactive|none, candidates:[...]}], general first
  Json units = Json::object();       // {level: id} as resolved from the kinds
  Json ignored = Json::array();      // units that named neither a kind nor a level: [{unit, why}]
  Json to_json() const;              // {key, defined, value, scope, scope_id?, source, units, chain}
};

class Resolver {
 public:
  // `cfg` may be null (no user layer), `db` may be null (no conversation metadata). data_root: where profiles/ and views/ live.
  static Result<Resolver> open(const std::filesystem::path& data_root, const Config* cfg, Database* db);

  Result<Resolution> resolve(const Binding& binding) const;
  // The "dziedziczenie" perspective: {schema, key, value, nodes:[{id,kind:"scope_level",label,role,value?}], edges:[{src,dst,type}], supplied_by, overridden}.
  Result<Json> inheritance(const Binding& binding) const;
  const actions::Engine& engine() const { return *engine_; }
  // How many Resolvers this process has opened (a render opens one and resolves every value through it; tests check the count).
  static std::uint64_t opens();

 private:
  std::shared_ptr<actions::Engine> engine_;
  // The unit kinds (views `unit_kinds`) as read by the first resolve() (under its own lock); a Resolver is a snapshot of the rules and the kinds.
  struct UnitKinds;
  std::shared_ptr<UnitKinds> unit_kinds_;
  const Config* cfg_ = nullptr;
  Database* db_ = nullptr;
  std::filesystem::path root_;
};

// Convenience for consumers that want only the value: the resolved value, or `fallback` when nothing in the chain defines it.
// A resolver that cannot be opened is logged at debug level and the user's setting (config) decides - never a silent default.
Json value_or(const std::filesystem::path& data_root, const Config& cfg, Database* db, const Binding& binding, const Json& fallback);

}  // namespace loom::scopes
