// Offline operation/ranking regressions; linked by the root's existing harness.
#include "method_channels.h"

#include <limits>

#include "doctest/doctest.h"

namespace {
using namespace loom;
using namespace loom::context;

Json descriptor(std::string operation = "sum", std::string signal = "raw_score") {
  return Json{{"operation", std::move(operation)}, {"signal", std::move(signal)}};
}
Json flat_plan(Json weights, Json operation = descriptor()) {
  Json leaves = Json::array();
  for (const auto& weight : weights) leaves.push_back(Json{{"weight", weight}, {"available", true}});
  return Json{{"selection", Json{{"fusion", operation}}}, {"leaves", leaves}};
}
Json batch(std::size_t index, Json hits, std::string status = "ok") {
  return Json{{"leaf_index", index}, {"status", std::move(status)}, {"accepted_hits", std::move(hits)}};
}
Json hit(std::string ref, double score) { return Json{{"ref", std::move(ref)}, {"score", score}}; }
std::string winner(const std::map<std::string, double>& scores) {
  std::string ref;
  double best = -std::numeric_limits<double>::infinity();
  for (const auto& [id, score] : scores) if (score > best) { ref = id; best = score; }
  return ref;
}
Json regex_parameters() {
  return Json{{"patterns", Json::array({"orchard", "bridge"})},
      {"flags", Json::array({"ECMAScript"})}, {"match", "search"}, {"semantics", "any_pattern"}};
}
Json step(std::size_t index, std::string id, double weight) {
  return Json{{"member_index", index}, {"id", std::move(id)}, {"member", Json{{"weight", weight}}}};
}
}

TEST_SUITE("method_channels_offline") {
  TEST_CASE("only actual kernel capability IDs are offered; unknown methods have no substitution") {
    const auto execution = method_channel_capabilities();
    REQUIRE(execution.contains("regex"));
    CHECK(execution["regex"]["available"] == true);
    CHECK(execution["regex"]["parameter_keys"] == Json::array({"limit", "min_score", "patterns", "flags", "match", "semantics"}));
    CHECK(execution["lexical"]["parameter_keys"] == Json::array({"limit", "min_score"}));
    CHECK(method_fusion_capabilities().contains("rrf"));
    auto unknown = method_channel("owner-new-method", Json::object(), nullptr, {});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == Errc::NotImplemented);
    CHECK_FALSE(method_channel("bm25", Json{{"k1", 1}}, nullptr, {}));
    CHECK_FALSE(method_channel("graph_pool", Json{{"membership_score", 1}}, nullptr, {}));
  }

  TEST_CASE("regex caller semantics and thresholds change which real corpus records are eligible") {
    const std::vector<RetrievalDocument> corpus{{"both", "orchard bridge"}, {"one", "orchard"}, {"neither", "melody"}};
    auto parameters = regex_parameters();
    auto any = method_channel("regex", parameters, nullptr, {});
    REQUIRE(any);
    auto any_batch = (*any)->retrieve("ignored by supplied regex", corpus, {"occurrence", 10, 0});
    REQUIRE(any_batch.status == "ok");
    CHECK(any_batch.hits.size() == 2);
    CHECK(any_batch.scored_count == 3);
    parameters["semantics"] = "all_patterns";
    auto all = method_channel("regex", parameters, nullptr, {});
    REQUIRE(all);
    auto all_batch = (*all)->retrieve("", corpus, {"occurrence", 10, 0});
    REQUIRE(all_batch.hits.size() == 1);
    CHECK(all_batch.hits[0].ref == "both");
    CHECK((*all)->retrieve("", corpus, {"occurrence", 10, 1}).hits.empty());
    parameters["semantics"] = "pattern_hits";
    auto count = method_channel("regex", parameters, nullptr, {});
    REQUIRE(count);
    auto counted = (*count)->retrieve("", corpus, {"occurrence", 1, 0});
    REQUIRE(counted.hits.size() == 1);
    CHECK(counted.hits[0].ref == "both");
    CHECK(counted.hits[0].score == 2);
    CHECK(counted.eligible_count == 2);
    CHECK(counted.truncated);
  }

  TEST_CASE("regex grammar flags and case handling are supplied data") {
    auto parameters = regex_parameters();
    parameters["patterns"] = Json::array({"orchard"});
    parameters["flags"] = Json::array();
    CHECK_FALSE(method_channel("regex", parameters, nullptr, {}));
    parameters["flags"] = Json::array({"ECMAScript", "basic"});
    CHECK_FALSE(method_channel("regex", parameters, nullptr, {}));
    parameters["flags"] = Json::array({"ECMAScript", "icase"});
    auto insensitive = method_channel("regex", parameters, nullptr, {});
    REQUIRE(insensitive);
    CHECK((*insensitive)->retrieve("", {{"uppercase", "ORCHARD"}}, {"native", 1, 0}).hits.size() == 1);
    parameters["patterns"] = Json::array({"["});
    CHECK_FALSE(method_channel("regex", parameters, nullptr, {}));
  }

  TEST_CASE("BM25 caller normalization parameter reverses actual corpus ranking") {
    auto loaded = kb::Pack::load_builtin();
    REQUIRE(loaded);
    const std::vector<RetrievalDocument> corpus{{"short", "orchard"},
        {"long", "orchard orchard orchard bridge melody copper silver crystal"}, {"other", "melody"}};
    auto unnormalized = method_channel("bm25", Json{{"k1", 1.2}, {"b", 0}}, *loaded, {});
    auto normalized = method_channel("bm25", Json{{"k1", 1.2}, {"b", 1}}, *loaded, {});
    REQUIRE(unnormalized);
    REQUIRE(normalized);
    auto first = (*unnormalized)->retrieve("orchard", corpus, {"unadjusted", 1, 0});
    auto second = (*normalized)->retrieve("orchard", corpus, {"adjusted", 1, 0});
    REQUIRE(first.status == "ok");
    REQUIRE(second.status == "ok");
    REQUIRE(first.hits.size() == 1);
    REQUIRE(second.hits.size() == 1);
    CHECK(first.hits[0].ref == "long");
    CHECK(second.hits[0].ref == "short");
    auto singular = method_channel("bm25", Json{{"k1", -1}, {"b", 0}}, *loaded, {});
    REQUIRE(singular);
    auto rejected = (*singular)->retrieve("orchard", {{"one", "orchard"}}, {"singular", 1, 0});
    CHECK(rejected.status == "error");
    CHECK(rejected.hits.empty());
  }

  TEST_CASE("graph pool measures only the actual supplied graph claim membership") {
    auto channel = method_channel("graph_pool", Json{{"scope", "graph_claim_ids"}, {"membership_score", -2}}, nullptr, {"real", "missing"});
    REQUIRE(channel);
    auto measured = (*channel)->retrieve("", {{"real", "source"}, {"outside", "source"}}, {"pool", 5, -3});
    REQUIRE(measured.hits.size() == 1);
    CHECK(measured.hits[0].ref == "real");
    CHECK(measured.hits[0].score == -2);
    CHECK(measured.scored_count == 1);
    CHECK(measured.zero_score_count == 0);
    CHECK((*channel)->retrieve("", {{"real", "source"}}, {"pool", 5, -2}).hits.empty());
  }

  TEST_CASE("weighted reciprocal channel rank reverses the winner instead of taking per-channel max") {
    const auto trace = Json::array({batch(0, Json::array({hit("x", 8), hit("y", 1)})),
        batch(1, Json::array({hit("y", 9), hit("x", 1)}))});
    auto x = fuse_method_results(flat_plan(Json::array({10, 1}), descriptor("sum", "reciprocal_rank")), trace);
    auto y = fuse_method_results(flat_plan(Json::array({1, 10}), descriptor("sum", "reciprocal_rank")), trace);
    REQUIRE(x);
    REQUIRE(y);
    CHECK(winner(*x) == "x");
    CHECK(winner(*y) == "y");
    CHECK(x->at("x") == doctest::Approx(10.5));
    CHECK(y->at("y") == doctest::Approx(10.5));
  }

  TEST_CASE("signed weights, measured zero, absent instruments and repeated occurrences remain distinct") {
    auto plan = flat_plan(Json::array({-2, 0, 4}), descriptor("sum"));
    auto result = fuse_method_results(plan, Json::array({batch(0, Json::array({hit("x", 3)})),
        batch(1, Json::array({hit("known-zero", 7)})), batch(2, Json::array({hit("phantom", 100)}), "unavailable")}));
    REQUIRE(result);
    CHECK(result->at("x") == -6);
    CHECK(result->at("known-zero") == 0);
    CHECK_FALSE(result->contains("phantom"));
    auto repeated = fuse_method_results(flat_plan(Json::array({1, 1, 1})),
        Json::array({batch(0, Json::array({hit("x", 2)})), batch(1, Json::array({hit("x", 2)})), batch(2, Json::array({hit("x", 2)}))}));
    REQUIRE(repeated);
    CHECK(repeated->at("x") == 6);
    auto average = fuse_method_results(flat_plan(Json::array({1, 1}), descriptor("mean")),
        Json::array({batch(0, Json::array({hit("x", 6)})), batch(1, Json::array(), "error")}));
    REQUIRE(average);
    CHECK(average->at("x") == 6);
  }

  TEST_CASE("nested max executes before a signed parent weight; duplicate combination occurrences stay separate") {
    Json leaves = Json::array();
    for (std::size_t occurrence = 0; occurrence < 2; ++occurrence) for (std::size_t child = 0; child < 2; ++child) {
      leaves.push_back(Json{{"available", true}, {"weight", -1},
          {"path", Json::array({step(occurrence, "same-combination", -1), step(child, "same-method", 1)})},
          {"fusions", Json::array({Json{{"combination_version_id", "same-combination"}, {"fusion", descriptor("max")}}})}});
    }
    auto result = fuse_method_results(Json{{"selection", Json{{"fusion", descriptor("sum")}}}, {"leaves", leaves}},
        Json::array({batch(0, Json::array({hit("x", 2)})), batch(1, Json::array({hit("x", 5)})),
          batch(2, Json::array({hit("x", 2)})), batch(3, Json::array({hit("x", 5)}))}));
    REQUIRE(result);
    CHECK(result->at("x") == -10);  // max(2,5)*-1, twice; never max(-2,-5).
  }

  TEST_CASE("fusion descriptors require data and preserve finite signed RRF constants") {
    auto operation = descriptor("rrf", "reciprocal_rank");
    auto trace = Json::array({batch(0, Json::array({hit("x", 8), hit("y", 2)}))});
    CHECK_FALSE(fuse_method_results(flat_plan(Json::array({1}), operation), trace));
    operation["rrf_constant"] = 0;
    auto measured = fuse_method_results(flat_plan(Json::array({1}), operation), trace);
    REQUIRE(measured);
    CHECK(measured->at("x") == 1);
    CHECK(measured->at("y") == 0.5);
    operation["rrf_constant"] = -1;
    CHECK_FALSE(fuse_method_results(flat_plan(Json::array({1}), operation), trace));
    CHECK_FALSE(fuse_method_results(flat_plan(Json::array({1}), Json("sum")), trace));
    CHECK_FALSE(fuse_method_results(flat_plan(Json::array({1}), descriptor("new-unimplemented-operation")), trace));
  }

  TEST_CASE("unavailable nested mechanisms cannot block independent available results") {
    auto plan = flat_plan(Json::array({1, 1}));
    plan["leaves"][1]["available"] = false;
    plan["leaves"][1]["path"] = Json::array({step(1, "unsupported-combination", 1), step(0, "unsupported-method", 1)});
    plan["leaves"][1]["fusions"] = Json::array({Json{{"combination_version_id", "unsupported-combination"}, {"fusion", descriptor("unknown")}}});
    auto result = fuse_method_results(plan, Json::array({batch(0, Json::array({hit("real", 3)})), batch(1, Json::array(), "unavailable")}));
    REQUIRE(result);
    CHECK(result->size() == 1);
    CHECK(result->at("real") == 3);
  }

  TEST_CASE("different path identities cannot silently merge at the same occurrence index") {
    auto plan = flat_plan(Json::array({1, 1}));
    for (std::size_t i = 0; i < 2; ++i) {
      const auto combination = "combination-" + std::to_string(i);
      plan["leaves"][i]["path"] = Json::array({step(0, combination, 1), step(i, "method", 1)});
      plan["leaves"][i]["fusions"] = Json::array({Json{{"combination_version_id", combination}, {"fusion", descriptor("sum")}}});
    }
    CHECK_FALSE(fuse_method_results(plan, Json::array({batch(0, Json::array({hit("x", 1)})), batch(1, Json::array({hit("x", 2)}))})));
    plan["leaves"][0]["path"] = Json::array({step(0, "combination-1", 1)});
    CHECK_FALSE(fuse_method_results(plan, Json::array({batch(0, Json::array({hit("x", 1)})), batch(1, Json::array({hit("x", 2)}))})));
  }

  TEST_CASE("accepted hits are authoritative; invalid or overflowing evidence is never fabricated") {
    auto record = batch(0, Json::array({hit("accepted", 2)}));
    record["hits"] = Json::array({hit("outside-threshold", 100)});
    auto result = fuse_method_results(flat_plan(Json::array({1})), Json::array({record}));
    REQUIRE(result);
    CHECK(result->contains("accepted"));
    CHECK_FALSE(result->contains("outside-threshold"));
    CHECK_FALSE(fuse_method_results(flat_plan(Json::array({1})), Json::array({record, record})));
    CHECK_FALSE(fuse_method_results(flat_plan(Json::array({1})), Json::array({batch(0, Json::array({hit("x", 1), hit("x", 2)}))})));
    CHECK_FALSE(fuse_method_results(flat_plan(Json::array({1})), Json::array({batch(0, Json::array({hit("x", std::numeric_limits<double>::infinity())}))})));
    CHECK_FALSE(fuse_method_results(flat_plan(Json::array({std::numeric_limits<double>::max()})),
        Json::array({batch(0, Json::array({hit("x", 2)}))})));
  }
}
