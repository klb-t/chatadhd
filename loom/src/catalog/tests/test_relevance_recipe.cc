#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include "../relevance_recipe.h"

using namespace loom;
using namespace loom::catalog::internal;

namespace {

Json tracked_relevance() {
  const auto source = std::filesystem::path(__FILE__).parent_path() / "../../../data/policy/relevance.json";
  std::ifstream input(source);
  if (!input) {
    FAIL("cannot read tracked relevance data: " << source.string());
    return Json();
  }
  Json result;
  input >> result;
  return result;
}

RelevanceRecipe take(Result<RelevanceRecipe> result) {
  if (!result) {
    FAIL("unexpected decoder error: " << result.error().message);
    return RelevanceRecipe{};
  }
  return std::move(result).value();
}

void check_rejected(const Json& input, const std::string& detail) {
  const auto result = load_relevance_recipe(input);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().code == Errc::InvalidArgument);
  CHECK(result.error().message.find(detail) != std::string::npos);
}

}  // namespace

TEST_CASE("tracked relevance values are decoded exactly without fallback presets") {
  const auto data = tracked_relevance();
  const auto recipe = take(load_relevance_recipe(data));
  CHECK(recipe.bias == data.at("bias").get<double>());
  CHECK(json::canonical(Json(recipe.weights)) == json::canonical(data.at("weights")));
  CHECK(json::canonical(Json(recipe.class_weights)) == json::canonical(data.at("term_class_weights")));
  CHECK(recipe.bm25_k1 == data.at("bm25").at("k1").get<double>());
  CHECK(recipe.bm25_b == data.at("bm25").at("b").get<double>());
  CHECK(recipe.bm25_normalise_percentile == data.at("bm25").at("normalise_percentile").get<double>());
  CHECK(recipe.bias == -3.0);
  CHECK(recipe.bm25_k1 == 1.2);
  CHECK(recipe.bm25_b == 0.75);
  CHECK(recipe.bm25_normalise_percentile == 99.0);
  for (auto it = data.at("channels").begin(); it != data.at("channels").end(); ++it)
    for (const auto& feature : it.value()) CHECK(recipe.channel_of.at(feature.get<std::string>()) == it.key());
  CHECK(json::canonical(recipe.snapshot.at("weights")) == json::canonical(data.at("weights")));
  CHECK(json::canonical(recipe.snapshot.at("term_class_weights")) == json::canonical(data.at("term_class_weights")));
  CHECK(recipe.snapshot.at("channels") == Json(recipe.channel_of));
  CHECK(recipe.snapshot.at("bm25") == data.at("bm25"));
  CHECK(recipe.snapshot.size() == 5);
  CHECK_FALSE(recipe.snapshot.contains("schema"));
  CHECK_FALSE(recipe.snapshot.contains("description"));
  CHECK(json::canonical(take(load_relevance_recipe(data)).snapshot) == json::canonical(recipe.snapshot));
}

TEST_CASE("required relevance fields fail explicitly instead of restoring literals") {
  const auto base = tracked_relevance();
  for (const auto* key : {"bias", "weights", "term_class_weights", "channels", "bm25"}) {
    INFO("missing key=" << key);
    auto changed = base;
    changed.erase(key);
    check_rejected(changed, key);
  }
  for (const auto* key : {"k1", "b", "normalise_percentile"}) {
    INFO("missing BM25 key=" << key);
    auto changed = base;
    changed["bm25"].erase(key);
    check_rejected(changed, key);
  }
  check_rejected(Json(), "object");
  check_rejected(Json::array(), "object");
}

TEST_CASE("relevance decoder rejects malformed and nonfinite values") {
  const auto base = tracked_relevance();
  for (const auto* key : {"weights", "term_class_weights", "channels", "bm25"}) {
    INFO("malformed object=" << key);
    auto changed = base;
    changed[key] = Json::array();
    check_rejected(changed, key);
  }
  const Json malformed[] = {Json(true), Json("1"), Json(nullptr), Json::array(),
                            Json(std::numeric_limits<double>::infinity()),
                            Json(-std::numeric_limits<double>::infinity()),
                            Json(std::numeric_limits<double>::quiet_NaN())};
  for (const auto& value : malformed) {
    for (const auto& path : {Json::json_pointer("/bias"), Json::json_pointer("/weights/id_hits"),
                            Json::json_pointer("/term_class_weights/alias"), Json::json_pointer("/bm25/k1"),
                            Json::json_pointer("/bm25/b"), Json::json_pointer("/bm25/normalise_percentile")}) {
      INFO("invalid numeric path=" << path.to_string());
      auto changed = base;
      changed[path] = value;
      check_rejected(changed, path.to_string());
    }
  }
}

TEST_CASE("BM25 parameter checks enforce mathematical domains without extra ceilings") {
  const auto base = tracked_relevance();
  for (double k1 : {-0.01, -1.0}) {
    auto changed = base;
    changed["bm25"]["k1"] = k1;
    check_rejected(changed, "/bm25/k1");
  }
  {
    auto changed = base;
    changed["bm25"]["k1"] = 0.0;
    CHECK(take(load_relevance_recipe(changed)).bm25_k1 == 0.0);
  }
  for (double b : {-0.01, 1.01}) {
    auto changed = base;
    changed["bm25"]["b"] = b;
    check_rejected(changed, "/bm25/b");
  }
  for (double percentile : {-0.01, 100.01}) {
    auto changed = base;
    changed["bm25"]["normalise_percentile"] = percentile;
    check_rejected(changed, "/bm25/normalise_percentile");
  }
  for (double b : {0.0, 1.0}) {
    auto changed = base;
    changed["bm25"]["b"] = b;
    CHECK(take(load_relevance_recipe(changed)).bm25_b == b);
  }
  for (double percentile : {0.0, 100.0}) {
    auto changed = base;
    changed["bm25"]["normalise_percentile"] = percentile;
    CHECK(take(load_relevance_recipe(changed)).bm25_normalise_percentile == percentile);
  }
}

TEST_CASE("open channel names stay explicit and weighted features cannot be ambiguous") {
  const auto base = tracked_relevance();
  auto changed = base;
  changed["channels"]["owner_channel"] = Json::array({"owner_feature"});
  changed["weights"]["owner_feature"] = 0.0;
  CHECK(take(load_relevance_recipe(changed)).channel_of.at("owner_feature") == "owner_channel");
  changed["channels"]["owner_channel"] = Json::array({"id_hits"});
  check_rejected(changed, "more than once");
  changed = base;
  changed["channels"]["lexical"].erase(changed["channels"]["lexical"].begin());
  check_rejected(changed, "no channel");
  changed = base;
  changed["channels"]["lexical"] = "id_hits";
  check_rejected(changed, "array");
  changed = base;
  changed["channels"]["lexical"].push_back(1);
  check_rejected(changed, "nonempty feature");
  changed = base;
  changed["channels"]["lexical"].push_back("");
  check_rejected(changed, "nonempty feature");
}

TEST_CASE("data edits change effective weights and receipts while metadata edits do not") {
  const auto base = tracked_relevance();
  const auto original = take(load_relevance_recipe(base));
  auto changed = base;
  changed["weights"]["id_hits"] = 0.0;
  changed["term_class_weights"]["expansion"] = 2.5;
  changed["bm25"]["normalise_percentile"] = 60.0;
  const auto edited = take(load_relevance_recipe(changed));
  CHECK(edited.weights.at("id_hits") == 0.0);
  CHECK(edited.class_weights.at("expansion") == 2.5);
  CHECK(edited.bm25_normalise_percentile == 60.0);
  CHECK(edited.bias + edited.weights.at("id_hits") * 3.0 != original.bias + original.weights.at("id_hits") * 3.0);
  CHECK(json::canonical(edited.snapshot) != json::canonical(original.snapshot));
  changed = base;
  changed["description"] = "An owner annotation that does not change execution values.";
  CHECK(json::canonical(take(load_relevance_recipe(changed)).snapshot) == json::canonical(original.snapshot));
}

TEST_CASE("finite settings have no arbitrary bias weight or BM25 k1 cap") {
  auto changed = tracked_relevance();
  const double high = std::numeric_limits<double>::max();
  changed["bias"] = high;
  changed["weights"]["id_hits"] = -high;
  changed["weights"]["title"] = 0.0;
  changed["term_class_weights"]["alias"] = high;
  changed["term_class_weights"]["concept"] = -high;
  changed["bm25"]["k1"] = high;
  const auto recipe = take(load_relevance_recipe(changed));
  CHECK(recipe.bias == high);
  CHECK(recipe.weights.at("id_hits") == -high);
  CHECK(recipe.weights.at("title") == 0.0);
  CHECK(recipe.class_weights.at("alias") == high);
  CHECK(recipe.class_weights.at("concept") == -high);
  CHECK(recipe.bm25_k1 == high);
  CHECK(recipe.snapshot["bias"] == high);
}
