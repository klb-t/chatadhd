#pragma once

#include <string>
#include <string_view>

#include "loom/util/json.h"

namespace loom::onboarding {

// Generated from the canonical onboarding presentation pack. Callers may
// replace this catalog through the same identity-based default layers.
Result<Json> builtin_presentation();
Status validate_presentation(const Json& catalog);
// Inert {{parameter}} interpolation: no expressions, HTML or execution.
// An empty locale chooses the catalog's explicit default_locale.
Result<std::string> presentation_text(const Json& catalog, std::string_view id,
                                     const Json& parameters = Json::object(),
                                     std::string_view locale = {});

}  // namespace loom::onboarding
