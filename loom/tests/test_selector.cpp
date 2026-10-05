#include <doctest/doctest.h>

#include "loom/selector.h"

using namespace loom;

namespace {
class RecipeEmbedding final : public EmbeddingProvider {
 public:
  bool fail = false;
  std::string model_id() const override { return "synthetic-recipe-fixture"; }
  Result<std::vector<std::vector<float>>> embed(const std::vector<std::string>& texts) override {
    if (fail) return Error(Errc::Unavailable, "synthetic provider failure");
    return std::vector<std::vector<float>>(texts.size(), {1.0f, 0.0f});
  }
};
}

TEST_SUITE("selector") {
  TEST_CASE("keyword tier: substring scoring, ranking, top_k, score<=0 cutoff") {
    SelectorEngine sel(SelectorEngine::kTierKeyword);
    CHECK(sel.tier() == SelectorEngine::kTierKeyword);
    std::vector<std::string> corpus = {
        "the quick brown fox",
        "jumps over the lazy dog",
        "foxes are quick and clever",
        "completely unrelated text",
    };
    CHECK(sel.index(corpus).has_value());
    CHECK(sel.size() == 4);

    auto hits = sel.search("quick fox", 10);
    REQUIRE(hits.size() >= 1);
    // doc 0 has both "quick" and "fox" -> score 1.0, should rank first.
    CHECK(hits[0].id == "0");
    CHECK(hits[0].score == doctest::Approx(1.0));
    for (const auto& h : hits) CHECK(h.score > 0.0);

    auto none = sel.search("zzzzz nonexistent", 5);
    CHECK(none.empty());
  }

  TEST_CASE("keyword tier: custom ids and top_k limiting") {
    SelectorEngine sel(SelectorEngine::kTierKeyword);
    std::vector<std::string> corpus = {"alpha beta", "alpha gamma", "alpha delta"};
    std::vector<std::string> ids = {"a", "b", "c"};
    CHECK(sel.index(corpus, ids).has_value());
    auto hits = sel.search("alpha", 2);
    CHECK(hits.size() == 2);
  }

  TEST_CASE("TF-IDF tier: exact match ranks above partial") {
    SelectorEngine sel(SelectorEngine::kTierTfIdf);
    CHECK(sel.tier() == SelectorEngine::kTierTfIdf);
    std::vector<std::string> corpus = {
        "graph memory selector implementation",
        "the graph engine builds nodes and edges",
        "completely different topic about cooking",
    };
    CHECK(sel.index(corpus).has_value());
    auto hits = sel.search("graph memory selector", 3);
    REQUIRE(!hits.empty());
    CHECK(hits[0].id == "0");
    // The unrelated doc should score lowest (likely 0, excluded).
    for (const auto& h : hits) CHECK(h.id != "2");
  }

  TEST_CASE("default tier is TF-IDF without an embedder (Loom deviation)") {
    SelectorEngine sel;
    CHECK(sel.tier() == SelectorEngine::kTierTfIdf);
  }

  TEST_CASE("empty corpus / empty query are handled") {
    SelectorEngine sel(SelectorEngine::kTierTfIdf);
    CHECK(sel.index({}).has_value());
    CHECK(sel.search("anything").empty());

    SelectorEngine sel2(SelectorEngine::kTierKeyword);
    CHECK(sel2.index({"some text"}).has_value());
    CHECK(sel2.search("").empty());
  }

  TEST_CASE("runtime recipe rebuilds vocabulary and exposes configurable cutoff") {
    auto base = RuntimeProfile::builtin("selector");
    REQUIRE(base);
    SelectorEngine sel(SelectorEngine::kTierTfIdf);
    REQUIRE(sel.index({"alpha alpha alpha", "beta gamma"}));
    REQUIRE(sel.search("beta").size() == 1);
    auto capped = base->with_overrides(Json{{"tfidf", {{"max_features", 1}}}});
    REQUIRE(capped);
    REQUIRE(sel.set_profile(*capped));
    CHECK(sel.search("beta").empty());
    auto filtered = base->with_overrides(Json{{"ranking", {{"minimum_score", 0.9}}}});
    REQUIRE(filtered);
    REQUIRE(sel.set_profile(*filtered));
    CHECK(sel.search("beta").empty());
    REQUIRE(sel.search("alpha").size() == 1);
    CHECK(sel.profile_inspection()->at("values").at("ranking").at("minimum_score") == 0.9);
  }

  TEST_CASE("feature cap can exceed old 5000-term preset without changing code") {
    std::string document;
    for (int i = 0; i < 5000; ++i) document += "term" + std::to_string(i) + " ";
    document += "zzzz";
    SelectorEngine sel(SelectorEngine::kTierTfIdf);
    REQUIRE(sel.index({document}));
    CHECK(sel.search("zzzz").empty());
    auto base = RuntimeProfile::builtin("selector");
    REQUIRE(base);
    auto unlimited = base->with_overrides(Json{{"tfidf", {{"max_features", 0}}}});
    REQUIRE(unlimited);
    REQUIRE(sel.set_profile(*unlimited));
    CHECK(sel.search("zzzz").size() == 1);
  }

  TEST_CASE("rejecting a foreign recipe retains the previous indexed result") {
    SelectorEngine sel(SelectorEngine::kTierTfIdf);
    REQUIRE(sel.index({"alpha beta", "different"}));
    const auto before = sel.search("alpha");
    auto foreign = RuntimeProfile::builtin("memory");
    REQUIRE(foreign);
    CHECK_FALSE(sel.set_profile(*foreign));
    REQUIRE(sel.search("alpha").size() == before.size());
    CHECK(sel.search("alpha")[0].to_json() == before[0].to_json());
  }

  TEST_CASE("failed embedding rebuild keeps the old recipe and stored index") {
    auto provider = std::make_shared<RecipeEmbedding>();
    SelectorEngine sel(SelectorEngine::kTierEmbedding, provider);
    REQUIRE(sel.index({"alpha", "beta"}));
    const auto before = sel.profile_inspection();
    REQUIRE(before);
    auto base = RuntimeProfile::builtin("selector");
    REQUIRE(base);
    auto updated = base->with_overrides(Json{{"ranking", {{"top_k", 1}}}});
    REQUIRE(updated);
    provider->fail = true;
    CHECK_FALSE(sel.set_profile(*updated));
    CHECK(sel.profile_inspection()->at("hash") == before->at("hash"));
    CHECK_FALSE(sel.search_checked("query"));
    CHECK(sel.search("query").empty());  // Historical wrapper behavior.
    provider->fail = false;
    CHECK(sel.search("query").size() == 2);
  }

  TEST_CASE("unavailable configured embedding tier never executes keyword under an embedding label") {
    auto base = RuntimeProfile::builtin("selector");
    REQUIRE(base);
    auto unavailable = base->with_overrides(Json{{"tiers", {{"without_embedding", 1}}}});
    REQUIRE(unavailable);
    SelectorEngine sel(std::nullopt, nullptr, *unavailable);
    auto result = sel.index({"alpha"});
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::Unavailable);
    CHECK_FALSE(sel.search_checked("alpha"));

    SelectorEngine existing;
    REQUIRE(existing.index({"original"}));
    CHECK_FALSE(existing.set_profile(*unavailable));
    REQUIRE(existing.search("original").size() == 1);
    auto forced = base->with_overrides(Json{{"tiers", {{"with_embedding", 1}, {"embedding_failure", 1}}}});
    REQUIRE(forced);
    auto provider = std::make_shared<RecipeEmbedding>();
    SelectorEngine strict(SelectorEngine::kTierEmbedding, provider, *forced);
    REQUIRE(strict.index({"first", "second"}));
    provider->fail = true;
    auto failed = strict.index({"replacement"});
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == Errc::Unavailable);
    CHECK(strict.size() == 2);
    provider->fail = false;
    CHECK(strict.search("query").size() == 2);
  }

  TEST_CASE("threshold equality inclusion is configurable for every existing method") {
    auto recipe = RuntimeProfile::builtin("selector");
    REQUIRE(recipe);
    auto strict = recipe->with_overrides(Json{{"ranking", Json{{"minimum_score", 1.0}}}});
    REQUIRE(strict);
    auto inclusive = strict->with_overrides(Json{{"ranking", Json{{"threshold_inclusive", true}}}});
    REQUIRE(inclusive);
    for (int tier : {SelectorEngine::kTierTfIdf, SelectorEngine::kTierKeyword, SelectorEngine::kTierEmbedding}) {
      auto provider = std::make_shared<RecipeEmbedding>();
      SelectorEngine engine(tier, provider, *strict);
      REQUIRE(engine.index({"alpha"}, {"one"}));
      CHECK(engine.search("alpha").empty());
      REQUIRE(engine.set_profile(*inclusive));
      auto hits = engine.search_checked("alpha");
      REQUIRE(hits);
      REQUIRE(hits->size() == 1);
      CHECK((*hits)[0].id == "one");
      CHECK((*hits)[0].score == doctest::Approx(1.0));
    }
  }
}
