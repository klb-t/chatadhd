// catalog links.cpp: the linking pass no longer compares every pair of units
// (that was O(N^2): ~2.5e9 pairs at 50k units). Candidate generation (LSH
// banding on MinHash lanes, an inverted index on rare terms, a time-sorted
// sweep, a project group table) must reproduce the exhaustive reference on
// archives that fit the reference scan, and must stay near-linear at scale.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>

#include "catalog/catalog_internal.h"

using namespace loom;
using namespace loom::catalog;
using namespace loom::catalog::internal;

namespace {

struct Rng {
  std::uint64_t s;
  explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
  std::uint64_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  }
  std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
  double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
};

using Key = std::tuple<int, int, std::string>;

std::set<Key> keys_of(const LinkResult& r, const std::string& type = "") {
  std::set<Key> out;
  for (const auto& l : r.links) {
    if (type.empty() || l.type == type) out.insert({l.i, l.j, l.type});
  }
  return out;
}

// A small corpus with topical clusters: units of one topic draw most of
// their tokens from the topic's word pool (so MinHash and rare-term links
// exist), plus shared filler words.
struct Corpus {
  std::vector<Sketch> sketches;
  std::vector<LinkUnit> units;
};

Corpus make_corpus(std::size_t n, std::uint64_t seed) {
  Rng rng(seed);
  Corpus c;
  c.sketches.resize(n);
  c.units.resize(n);
  const std::size_t topics = std::max<std::size_t>(4, n / 25);
  std::vector<std::string> previous;
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t topic = i % topics;
    auto draw = [&] {
      bool topical = rng.unit() < 0.7;
      std::size_t w = topical ? topic * 40 + rng.below(40) : 100000 + rng.below(300);
      return "w" + std::to_string(w);
    };
    std::vector<std::string> tokens;
    if (i % 4 == 1) {
      // A continuation of the previous unit: its token stream with 0-15 %
      // of the tokens replaced, so MinHash (word 3-shingles) Jaccard ranges
      // from ~1 down to just around the 0.4 threshold.
      double edit = 0.15 * rng.unit();
      tokens = previous;
      for (auto& t : tokens) {
        if (rng.unit() < edit) t = draw();
      }
    } else {
      for (int k = 0; k < 90; ++k) tokens.push_back(draw());
    }
    previous = tokens;
    Sketch& s = c.sketches[i];
    std::map<std::string, int> tf;
    for (const auto& t : tokens) ++tf[t];
    for (const auto& [t, n_t] : tf) s.top_terms.emplace_back(t, n_t);
    std::sort(s.top_terms.begin(), s.top_terms.end(), [](const auto& a, const auto& b) {
      return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    s.minhash = MinHash::build(tokens, 64);
    LinkUnit& u = c.units[i];
    u.sketch = &s;
    u.platform = (i % 3 == 0) ? "claude" : "chatgpt";
    u.project = (i % 17 == 0) ? "proj" + std::to_string(i % 5) : "";
    // spread over ~60 days so same_session neighbourhoods stay small
    u.time = 1.7e9 + static_cast<double>(rng.below(60 * 86400));
  }
  return c;
}

}  // namespace

TEST_SUITE("catalog_links") {
  TEST_CASE("candidate generation reproduces the exhaustive reference scan") {
    Corpus c = make_corpus(700, 7);
    LinkParams exact;
    exact.exact_max_units = 100000;
    LinkParams fast = exact;
    fast.exact_max_units = 0;
    LinkResult ref = build_links(c.units, exact);
    LinkResult got = build_links(c.units, fast);
    REQUIRE(json::get_string(ref.stats, "mode") == "exhaustive");
    REQUIRE(json::get_string(got.stats, "mode") == "candidates");

    // Exact predicates: identical link sets.
    CHECK(keys_of(got, "shared_rare") == keys_of(ref, "shared_rare"));
    CHECK(keys_of(got, "same_session") == keys_of(ref, "same_session"));
    CHECK(keys_of(got, "same_project") == keys_of(ref, "same_project"));
    // Strengths agree link by link.
    std::map<Key, double> ref_strength;
    for (const auto& l : ref.links) ref_strength[{l.i, l.j, l.type}] = l.strength;
    for (const auto& l : got.links) CHECK(ref_strength.at({l.i, l.j, l.type}) == doctest::Approx(l.strength));

    // LSH is a candidate generator: it may miss a pair near the threshold but
    // never invents one. Recall on the reference set must stay >= 0.99.
    auto ref_cont = keys_of(ref, "continuation");
    auto got_cont = keys_of(got, "continuation");
    REQUIRE(ref_cont.size() > 50);  // the corpus really contains continuation links
    for (const auto& k : got_cont) CHECK(ref_cont.count(k) == 1);
    double recall = static_cast<double>(got_cont.size()) / static_cast<double>(ref_cont.size());
    MESSAGE("continuation links: reference=" << ref_cont.size() << " candidates=" << got_cont.size() << " recall=" << recall);
    CHECK(recall >= 0.99);
    // Output is sorted and deterministic.
    LinkResult again = build_links(c.units, fast);
    CHECK(keys_of(again) == keys_of(got));
  }

  TEST_CASE("large project groups are aggregated instead of materialised as cliques") {
    Corpus c = make_corpus(300, 3);
    for (std::size_t i = 0; i < c.units.size(); ++i) c.units[i].project = i < 100 ? "big" : "";
    LinkParams p;
    p.project_max_clique = 32;
    LinkResult r = build_links(c.units, p);
    CHECK(keys_of(r, "same_project").empty());
    REQUIRE(r.project_groups.size() == 1);
    CHECK(r.project_groups[0].size() == 100);
    p.project_max_clique = 200;
    CHECK(keys_of(build_links(c.units, p), "same_project").size() == 100 * 99 / 2);
  }

  TEST_CASE("same_session keeps the nearest neighbours when a mass import shares one timestamp") {
    Corpus c = make_corpus(200, 5);
    for (auto& u : c.units) {
      u.platform = "chatgpt";
      u.time = 1.7e9;  // every unit at the same instant
    }
    LinkParams p;
    p.exact_max_units = 0;
    p.session_max_neighbours = 8;
    LinkResult r = build_links(c.units, p);
    auto sessions = keys_of(r, "same_session");
    // Each unit contributes at most 8 neighbours; the union is bounded by 200 * 8.
    CHECK(sessions.size() <= 200u * 8u);
    CHECK(sessions.size() >= 200u * 8u / 2);
  }

  TEST_CASE("50k units: near-linear candidate generation, planted near-duplicates found" * doctest::timeout(120)) {
    constexpr std::size_t kUnits = 50000;
    Rng rng(11);
    std::vector<Sketch> sketches(kUnits);
    std::vector<LinkUnit> units(kUnits);
    std::vector<std::pair<int, int>> planted;
    for (std::size_t i = 0; i < kUnits; ++i) {
      Sketch& s = sketches[i];
      s.minhash.sig.resize(64);
      if (i >= 100 && i % 100 == 0) {  // a near-copy of unit i - 57: 8 of 64 lanes differ (Jaccard 0.875)
        const Sketch& src = sketches[i - 57];
        s.minhash = src.minhash;
        for (int k = 0; k < 8; ++k) s.minhash.sig[rng.below(64)] = rng.next() | 1ULL;
        s.top_terms = src.top_terms;
        planted.emplace_back(static_cast<int>(i - 57), static_cast<int>(i));
      } else {
        for (auto& v : s.minhash.sig) v = rng.next() >> 1;
        for (int k = 0; k < 30; ++k) {
          // Zipf-ish vocabulary: most terms are common, a long tail is rare
          std::size_t r = rng.below(1000000);
          std::size_t term = r < 700000 ? rng.below(300) : rng.below(200000);
          s.top_terms.emplace_back("t" + std::to_string(term), 1 + static_cast<int>(rng.below(3)));
        }
        std::sort(s.top_terms.begin(), s.top_terms.end());
        s.top_terms.erase(std::unique(s.top_terms.begin(), s.top_terms.end(),
                                      [](const auto& a, const auto& b) { return a.first == b.first; }),
                          s.top_terms.end());
      }
      LinkUnit& u = units[i];
      u.sketch = &s;
      u.platform = (i % 2 == 0) ? "chatgpt" : "claude";
      u.time = 1.6e9 + static_cast<double>(rng.below(2 * 365 * 86400));
    }
    LinkParams p;  // default: candidates above 2000 units
    auto t0 = std::chrono::steady_clock::now();
    LinkResult r = build_links(units, p);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("50k units: " << secs << " s, " << json::dump(r.stats));
    CHECK(json::get_string(r.stats, "mode") == "candidates");
    // All-pairs would verify ~1.25e9 pairs. Candidate generation examines a
    // tiny fraction of that.
    CHECK(json::get_int(r.stats, "minhash_pairs_verified") < static_cast<std::int64_t>(kUnits) * 100);
    CHECK(json::get_int(r.stats, "rare_term_candidates") < static_cast<std::int64_t>(kUnits) * 200);
    auto cont = keys_of(r, "continuation");
    std::size_t found = 0;
    for (const auto& [a, b] : planted) found += cont.count({a, b, "continuation"});
    CHECK(found >= planted.size() * 99 / 100);
    CHECK(secs < 60.0);
  }
}
