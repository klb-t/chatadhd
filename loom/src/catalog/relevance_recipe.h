#pragma once

#include <map>
#include <string>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::catalog::internal {

// The caller supplies the relevance document from its already loaded Pack.
// There are no fallback presets here: every execution value comes from data.
struct RelevanceRecipe {
  double bias;
  std::map<std::string, double> weights;
  std::map<std::string, double> class_weights;
  std::map<std::string, std::string> channel_of;
  double bm25_k1;
  double bm25_b;
  double bm25_normalise_percentile;
  // Only effective values, with channels resolved as feature -> channel.
  // Pack/schema/description metadata is not part of this execution receipt.
  Json snapshot;
};

Result<RelevanceRecipe> load_relevance_recipe(const Json& relevance);

}  // namespace loom::catalog::internal
