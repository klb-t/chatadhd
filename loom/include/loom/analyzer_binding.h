#pragma once

namespace loom {

// Dependency binding, not a separate configuration resolver. Both strategies
// validate and apply the current user overlay through RuntimeProfile.
enum class AnalyzerBinding {
  ConstructorDefault,  // Builtin values use the explicitly supplied analyzer.
  RuntimeProfile,      // Builtin values also use the currently resolved recipe.
};

}  // namespace loom
