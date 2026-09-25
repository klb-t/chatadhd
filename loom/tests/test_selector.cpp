#include <doctest/doctest.h>

#include "loom/selector.h"

using namespace loom;

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
}
