// Immutable data-defined runtime recipes. No provider/domain-name dispatch.
// KB data retain their existing pack contract; runtime .pack documents use
// a declarative value schema and a partial per-domain user overlay.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class RuntimeProfile {
 public:
  static Result<RuntimeProfile> builtin(std::string_view domain);
  // <data_dir>/profiles/<domain>.pack, then one-call overrides. Objects
  // merge recursively; arrays/scalars replace. Missing file = defaults;
  // unreadable, malformed or invalid existing files return an error.
  static Result<RuntimeProfile> load(std::string_view domain, const std::filesystem::path& data_dir = {},
                                     const Json& overrides = Json::object());
  static Result<RuntimeProfile> from_definition(const Json& definition,
                                                const Json& overrides = Json::object());
  Result<RuntimeProfile> with_overrides(const Json& overrides) const;
  // Exact replacement against this descriptor; also used by consumers to
  // validate injected values without resurrecting removed dictionary keys.
  Result<RuntimeProfile> with_values(const Json& values) const;
  Result<RuntimeProfile> with_patch(const Json& patch) const;
  Result<RuntimeProfile> with_overlay(const Json& document) const;

  const Json& values() const noexcept { return values_; }
  const Json& definition() const noexcept { return definition_; }
  const Json& value_schema() const { return definition_.at("value_schema"); }
  const std::string& hash() const noexcept { return hash_; }
  const std::string& domain() const noexcept { return domain_; }
  bool is_builtin() const noexcept;
  // Full effective values and schema, with a content-derived version hash.
  Json inspection() const;
  static std::vector<std::string> domains();

 private:
  Json definition_;
  Json values_;
  std::string domain_;
  std::string hash_;
};

// Universal, inert template substitution. {{name}} refers to an object
// member; {{/path/to/value}} uses RFC 6901. Missing values are errors;
// substitutions are not reparsed and never execute code.
Result<std::string> render_profile_template(std::string_view text, const Json& variables);

}  // namespace loom
