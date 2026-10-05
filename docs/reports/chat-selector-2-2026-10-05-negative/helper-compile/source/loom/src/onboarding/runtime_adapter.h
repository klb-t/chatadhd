#pragma once

#include "loom/onboarding_layers.h"

namespace loom::onboarding {

// Returns RuntimeProfile inspection(), including checked effective values,
// schema and content-derived hash; reports Unavailable before W11 integration.
Result<Json> runtime_profile_values(const Json& definition, const DefaultLayers& layers,
                                    const Json& bindings);

}  // namespace loom::onboarding
