#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "context/runtime_preset.h"

namespace loom::context {
#include "context/runtime_presets.inc"
}

namespace loom::chat {

struct ReasoningRecipe {
  context::RuntimePresetSnapshot snapshot;
  std::vector<std::string> thinking_indicators;
  std::vector<std::string> budget_model_markers;
  Json budget_by_effort;
  Json unknown_effort_budget;
  std::string adaptive_effort;
  std::string verbosity_effort;
  std::string verbosity_value;
};

inline Result<ReasoningRecipe> reasoning_recipe(const Json& options = Json::object()) {
  LOOM_TRY_ASSIGN(auto snapshot, context::resolve_runtime_preset(context::builtin_chat_reasoning_definition(), options));
  const auto& values = snapshot.values;
  const auto invalid = [](std::string_view key) {
    return Error(Errc::InvalidArgument, "chat reasoning preset field invalid: " + std::string(key));
  };
  // These are consumed field names, not a generic schema interpreter. Before
  // W11 integration the inspection explicitly reports native-only validation.
  const std::vector<std::string> fields{"thinking_indicators", "budget_model_markers", "budget_by_effort",
      "unknown_effort_budget", "adaptive_effort", "verbosity_effort", "verbosity_value"};
  if (values.size() != fields.size()) return invalid("exact consumed field set");
  for (const auto& field : fields) if (!values.contains(field)) return invalid(field);
  for (const auto* field : {"thinking_indicators", "budget_model_markers"}) {
    if (!values[field].is_array()) return invalid(field);
    for (const auto& marker : values[field]) if (!marker.is_string()) return invalid(field);
  }
  const auto budget_valid = [](const Json& value) {
    return value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>() >= 0);
  };
  if (!values["budget_by_effort"].is_object()) return invalid("budget_by_effort");
  for (const auto& [effort, budget] : values["budget_by_effort"].items()) {
    (void)effort;
    if (!budget_valid(budget)) return invalid("budget_by_effort");
  }
  if (!budget_valid(values["unknown_effort_budget"])) return invalid("unknown_effort_budget");
  for (const auto* field : {"adaptive_effort", "verbosity_effort", "verbosity_value"})
    if (!values[field].is_string()) return invalid(field);
  ReasoningRecipe result;
  result.thinking_indicators = values["thinking_indicators"].get<std::vector<std::string>>();
  result.budget_model_markers = values["budget_model_markers"].get<std::vector<std::string>>();
  result.budget_by_effort = values["budget_by_effort"];
  result.unknown_effort_budget = values["unknown_effort_budget"];
  result.adaptive_effort = values["adaptive_effort"].get<std::string>();
  result.verbosity_effort = values["verbosity_effort"].get<std::string>();
  result.verbosity_value = values["verbosity_value"].get<std::string>();
  result.snapshot = std::move(snapshot);
  return result;
}

inline void apply_reasoning_recipe(Json& payload, std::string_view model,
                                   const std::optional<std::string>& effort, const ReasoningRecipe& recipe) {
  std::string lower(model);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto matches = [](std::string_view value, const std::vector<std::string>& markers) {
    return std::any_of(markers.begin(), markers.end(), [&](const std::string& marker) {
      return value.find(marker) != std::string_view::npos;
    });
  };
  if (!matches(lower, recipe.thinking_indicators)) return;
  Json reasoning{{"enabled", true}};
  if (matches(model, recipe.budget_model_markers)) {
    if (effort && *effort == recipe.verbosity_effort) {
      payload["verbosity"] = recipe.verbosity_value;
    } else if (effort && *effort != recipe.adaptive_effort) {
      const auto found = recipe.budget_by_effort.find(*effort);
      reasoning["max_tokens"] = found == recipe.budget_by_effort.end() ? recipe.unknown_effort_budget : *found;
    }
  } else if (effort) {
    reasoning["effort"] = *effort;
  }
  payload["reasoning"] = std::move(reasoning);
}

}  // namespace loom::chat
