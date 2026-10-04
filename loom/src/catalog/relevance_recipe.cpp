#include "relevance_recipe.h"

#include <cmath>
#include <utility>

namespace loom::catalog::internal {
namespace {

Error invalid(std::string detail) {
  return Error(Errc::InvalidArgument, "catalog relevance recipe: " + std::move(detail));
}

Result<double> finite_number(const Json& object, const char* key, const std::string& path) {
  const auto* value = json::find(object, key);
  if (!value || !value->is_number()) return invalid(path + " must be a finite number");
  const double number = value->get<double>();
  if (!std::isfinite(number)) return invalid(path + " must be a finite number");
  return number;
}

Result<std::map<std::string, double>> finite_map(const Json& object, const char* key) {
  const auto* values = json::find(object, key);
  if (!values || !values->is_object()) return invalid(std::string("/") + key + " must be an object");
  std::map<std::string, double> result;
  for (auto it = values->begin(); it != values->end(); ++it) {
    if (it.key().empty()) return invalid(std::string("/") + key + " keys must be nonempty");
    LOOM_TRY_ASSIGN(double number, finite_number(*values, it.key().c_str(), std::string("/") + key + "/" + it.key()));
    result.emplace(it.key(), number);
  }
  return result;
}

}  // namespace

Result<RelevanceRecipe> load_relevance_recipe(const Json& relevance) {
  if (!relevance.is_object()) return invalid("expected an object");
  RelevanceRecipe recipe{};
  LOOM_TRY_ASSIGN(recipe.bias, finite_number(relevance, "bias", "/bias"));
  LOOM_TRY_ASSIGN(recipe.weights, finite_map(relevance, "weights"));
  LOOM_TRY_ASSIGN(recipe.class_weights, finite_map(relevance, "term_class_weights"));

  const auto* channels = json::find(relevance, "channels");
  if (!channels || !channels->is_object()) return invalid("/channels must be an object");
  for (auto it = channels->begin(); it != channels->end(); ++it) {
    if (it.key().empty()) return invalid("/channels names must be nonempty");
    if (!it.value().is_array()) return invalid("/channels/" + it.key() + " must be an array of feature names");
    for (const auto& feature : it.value()) {
      if (!feature.is_string() || feature.get_ref<const std::string&>().empty())
        return invalid("/channels/" + it.key() + " must contain nonempty feature names");
      const auto name = feature.get<std::string>();
      if (!recipe.channel_of.emplace(name, it.key()).second)
        return invalid("feature '" + name + " is assigned more than once in /channels");
    }
  }
  for (const auto& [feature, weight] : recipe.weights) {
    (void)weight;
    if (!recipe.channel_of.contains(feature))
      return invalid("weighted feature '" + feature + " has no channel in /channels");
  }

  const auto* bm25 = json::find(relevance, "bm25");
  if (!bm25 || !bm25->is_object()) return invalid("/bm25 must be an object");
  LOOM_TRY_ASSIGN(recipe.bm25_k1, finite_number(*bm25, "k1", "/bm25/k1"));
  LOOM_TRY_ASSIGN(recipe.bm25_b, finite_number(*bm25, "b", "/bm25/b"));
  LOOM_TRY_ASSIGN(recipe.bm25_normalise_percentile,
                  finite_number(*bm25, "normalise_percentile", "/bm25/normalise_percentile"));
  if (recipe.bm25_k1 < 0.0) return invalid("/bm25/k1 must be nonnegative");
  if (recipe.bm25_b < 0.0 || recipe.bm25_b > 1.0) return invalid("/bm25/b must be in [0, 1]");
  if (recipe.bm25_normalise_percentile < 0.0 || recipe.bm25_normalise_percentile > 100.0)
    return invalid("/bm25/normalise_percentile must be in [0, 100]");

  recipe.snapshot = Json{{"bias", recipe.bias}, {"weights", recipe.weights},
                         {"term_class_weights", recipe.class_weights}, {"channels", recipe.channel_of},
                         {"bm25", Json{{"k1", recipe.bm25_k1}, {"b", recipe.bm25_b},
                                        {"normalise_percentile", recipe.bm25_normalise_percentile}}}};
  return recipe;
}

}  // namespace loom::catalog::internal
