// Immutable data-defined runtime recipes. No provider/domain-name dispatch.
// KB data retain their existing pack contract; runtime .pack documents use
// a declarative value schema and a partial per-domain user overlay.
#pragma once

#include <filesystem>
#include <memory>
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

  const Json& values() const noexcept { return *values_; }
  const Json& definition() const noexcept { return *definition_; }
  const Json& defaults() const { return definition_->at("defaults"); }
  const Json& value_schema() const { return definition_->at("value_schema"); }
  // Preset construction sources, not user-layer origins. Raw byte identity
  // is deliberately separate from the historical definition/values hash.
  const Json& source_provenance() const noexcept { return source_provenance_; }
  const std::string& hash() const noexcept { return hash_; }
  const std::string& domain() const noexcept { return domain_; }
  bool is_builtin() const noexcept;
  // Full definition, defaults, effective values, schema and preset sources.
  // is_builtin expresses value equality, never user override intent.
  Json inspection() const;
  static std::vector<std::string> domains();

 private:
  // Immutable after construction and shared by copies (copy = two reference counts, not a deep copy of a document that
  // can be hundreds of KB: the embedded presets are copied on every load()). Nothing writes through these pointers, so
  // copies held by different runtimes and threads read the same tree concurrently (const nlohmann reads only); a new
  // value set (overlay, overrides, patch) always builds a new tree. Never null.
  static std::shared_ptr<const Json> null_json();
  std::shared_ptr<const Json> definition_ = null_json();
  std::shared_ptr<const Json> values_ = null_json();
  std::string domain_;
  std::string hash_;
  Json source_provenance_ = Json::array();
};

// Universal, inert template substitution. {{name}} refers to an object
// member; {{/path/to/value}} uses RFC 6901. Missing values are errors;
// substitutions are not reparsed and never execute code.
Result<std::string> render_profile_template(std::string_view text, const Json& variables);

}  // namespace loom
