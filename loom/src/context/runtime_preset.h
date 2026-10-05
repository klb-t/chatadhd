#pragma once

#include <string>

#include "loom/result.h"
#include "loom/util/json.h"
#include "loom/util/sha256.h"

#if __has_include("loom/runtime_profile.h")
#include "loom/runtime_profile.h"
#endif
#if __has_include("loom/onboarding_layers.h")
#include "loom/onboarding_layers.h"
#endif

namespace loom::context {

// A binding to canonical data, not another profile loader. No filesystem
// overlays, schema interpreter, recursive merge or exclusion resolver lives
// here. Consumers validate the fields their native operations actually use.
struct RuntimePresetSnapshot {
  Json values;
  std::string hash;
  Json details;
  Json inspection() const { return details; }
};

inline Result<RuntimePresetSnapshot> resolve_runtime_preset(const Json& definition, const Json& options) {
  try {
    if (!definition.is_object() || definition.value("schema", Json()) != "loom.runtime_profile/1" ||
        !definition.contains("defaults") || !definition["defaults"].is_object() ||
        !definition.contains("value_schema") || !definition["value_schema"].is_object() ||
        json::get_string(definition, "domain").empty() ||
        !definition.contains("revision") || !definition["revision"].is_number_integer() ||
        (!definition["revision"].is_number_unsigned() && definition["revision"].get<std::int64_t>() < 1) ||
        (definition["revision"].is_number_unsigned() && definition["revision"].get<std::uint64_t>() == 0))
      return Error(Errc::InvalidArgument, "runtime preset descriptor invalid");
    if (!options.is_object()) return Error(Errc::InvalidArgument, "runtime preset options must be an object");
    for (const auto& [key, value] : options.items()) {
      (void)value;
      if (key != "effective_values" && key != "layer_snapshot")
        return Error(Errc::InvalidArgument, "unknown runtime preset option: " + key);
    }
    if (options.contains("effective_values") && options.contains("layer_snapshot"))
      return Error(Errc::InvalidArgument, "runtime preset values and layers are mutually exclusive");

    Json values = definition["defaults"];
    Json resolutions = Json::object();
    std::string basis = "native_consumed_fields";
    if (options.contains("layer_snapshot")) {
#if __has_include("loom/runtime_profile.h") && __has_include("loom/onboarding_layers.h")
      const auto& supplied = options["layer_snapshot"];
      if (!supplied.is_object() || !supplied.contains("pack") || !supplied.contains("state") ||
          !supplied.contains("bindings") || !supplied["bindings"].is_object())
        return Error(Errc::InvalidArgument, "runtime layer snapshot needs pack, state and bindings");
      // Every consumed top-level setting must be bound. The shared adapter
      // starts with defaults; partial bindings would resurrect an exclusion.
      for (const auto& [key, ignored] : definition["defaults"].items()) {
        (void)ignored;
        std::string pointer = "/";
        for (char c : key) pointer += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
        if (!supplied["bindings"].contains(pointer))
          return Error(Errc::InvalidArgument, "runtime layer bindings must cover each preset setting");
      }
      LOOM_TRY_ASSIGN(auto layers, onboarding::DefaultLayers::create(supplied["pack"], supplied["state"]));
      LOOM_TRY_ASSIGN(auto checked, onboarding::runtime_profile_values(definition, layers, supplied["bindings"]));
      values = checked.at("values");
      for (const auto& [pointer, key] : supplied["bindings"].items()) {
        LOOM_TRY_ASSIGN(auto resolution, layers.resolve(key.get<std::string>()));
        resolutions[pointer] = std::move(resolution);
      }
      basis = "runtime_profile_value_schema";
#else
      return Error(Errc::Unavailable, "runtime preset layers require threads 11 and 12");
#endif
    } else if (options.contains("effective_values")) {
      if (!options["effective_values"].is_object())
        return Error(Errc::InvalidArgument, "runtime preset effective values must be an object");
      // Exact replacement; missing fields never inherit descriptor defaults.
      values = options["effective_values"];
    }
#if __has_include("loom/runtime_profile.h")
    LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::from_definition(definition));
    LOOM_TRY_ASSIGN(auto checked, profile.with_values(values));
    values = checked.values();
    basis = "runtime_profile_value_schema";
#endif
    RuntimePresetSnapshot result;
    result.values = values;
    result.hash = Sha256::hex(json::canonical(Json{{"definition", definition}, {"values", values}}));
    result.details = Json{{"schema", "loom.runtime_profile_effective/1"}, {"domain", definition.at("domain")},
        {"revision", definition.at("revision")}, {"hash", result.hash},
        {"is_builtin", values == definition["defaults"]}, {"values", values},
        {"value_schema", definition["value_schema"]}, {"validation_basis", basis}};
    result.details["source"] = options.contains("layer_snapshot") ? "graph_layers" :
        options.contains("effective_values") ? "caller_effective_values" : "builtin";
    if (!resolutions.empty()) result.details["layer_resolutions"] = std::move(resolutions);
    return result;
  } catch (const Json::exception& e) {
    return Error(Errc::InvalidArgument, std::string("runtime preset: ") + e.what());
  }
}

}  // namespace loom::context
