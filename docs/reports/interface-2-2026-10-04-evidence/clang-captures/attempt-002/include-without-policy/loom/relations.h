// loom/relations.h — typed relation registry as data (MEGA MASTER 2.B, 4.3).
//
// Edge types used by links.link_type are free strings (Python accepts any);
// the registry adds meaning: inverse, symmetry, transitivity, category.
// Built-in types are seeded from an embedded JSON list (policy data); unknown
// types are accepted and auto-registered with category "custom".
#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Database;

struct RelationType {
  std::string name;
  std::string inverse;      // "" when none
  bool symmetric = false;
  bool transitive = false;
  std::string category = "custom";  // structural | semantic | provenance | temporal | dialogue | custom
  std::string description;
  Json metadata = Json::object();
  Json to_json() const;
  static Result<RelationType> from_json(const Json& j);
};

// The embedded seed list (JSON array of RelationType objects).
const Json& builtin_relation_types();

class RelationRegistry {
 public:
  explicit RelationRegistry(Database& db);

  // Inserts built-ins that are missing (never overwrites user edits).
  Status seed_builtin();
  // Upserts definitions from a JSON array (e.g. a user-provided relations.json).
  Status load(const Json& definitions);

  Result<std::optional<RelationType>> get(std::string_view name);
  Result<std::vector<RelationType>> list(std::optional<std::string_view> category = std::nullopt);
  Status upsert(const RelationType& t);
  // Accept any relation name: registers it (category "custom") if unknown.
  Result<RelationType> ensure(std::string_view name);

  // Convenience queries (unknown -> empty / false).
  std::string inverse_of(std::string_view name);
  bool is_symmetric(std::string_view name);

 private:
  Database& db_;
  std::mutex mu_;
  std::map<std::string, RelationType, std::less<>> cache_;
  bool cache_loaded_ = false;
  Status load_cache_locked();
};

}  // namespace loom
