#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../semantic_candidates.h"
#ifndef LOOM_SEMANTIC_CANDIDATES_MODULE_ONLY
#include "loom/config.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#endif

using namespace loom;
using namespace loom::catalog;
using namespace loom::catalog::internal;

namespace {

std::vector<CatalogUnit> units() {
  CatalogUnit related;
  related.unit.id = "unit-related";
  related.content_hash = "hash-related";
  related.unit.title = "Release closure";
  related.head = "Let the catch disengage.";
  CatalogUnit unrelated;
  unrelated.unit.id = "unit-unrelated";
  unrelated.content_hash = "hash-unrelated";
  unrelated.unit.title = "Weather";
  unrelated.head = "The sky is overcast.";
  CatalogUnit missing;
  missing.unit.id = "unit-missing";
  missing.content_hash = "hash-missing";
  return {related, unrelated, missing};
}

Json config() {
  return Json{{"schema", "loom.catalog_semantic_candidates/1"}, {"enabled", true},
              {"profile_input_hash", "profile-hash"}, {"channel", "dense"},
              {"model", "offline-fixture"}, {"method", "supplied_dense_cosine"},
              {"query", Json::array({1.0, 0.0})},
              {"records", Json::array({
                  Json{{"unit_id", "unit-related"}, {"content_hash", "hash-related"},
                       {"vector", Json::array({1.0, 0.0})}},
                  Json{{"unit_id", "unit-unrelated"}, {"content_hash", "hash-unrelated"},
                       {"vector", Json::array({-1.0, 0.0})}}})},
              {"policy", Json{{"bias", -3.0}, {"weight", 8.0}, {"tau_relevant", 0.7}, {"fusion", "union"}}}};
}

void check_rejected(const Json& input, const std::vector<CatalogUnit>& population = units()) {
  auto result = evaluate_semantic_candidates(input, "profile-hash", population);
  CHECK_FALSE(result.has_value());
}

#ifndef LOOM_SEMANTIC_CANDIDATES_MODULE_ONLY
template <class T>
T take(Result<T>&& value) {
  if (!value) FAIL("unexpected native error: " << value.error().message);
  return std::move(value).value();
}

Json conversation(const std::string& id, const std::string& text) {
  return Json{{"id", id}, {"title", "note"}, {"create_time", 1700000000}, {"current_node", "n"},
              {"mapping", Json{{"n", Json{{"id", "n"}, {"parent", nullptr}, {"children", Json::array()},
                  {"message", Json{{"id", "n"}, {"author", Json{{"role", "user"}}},
                      {"content", Json{{"content_type", "text"}, {"parts", Json::array({text})}}},
                      {"create_time", 1700000001}}}}}}}};
}

struct NativeFixture {
  fsutil::TempDir data;
  fsutil::TempDir source;
  std::unique_ptr<Runtime> rt;
  std::unique_ptr<Catalog> cat;
  SelfProfile profile;
  std::map<std::string, CatalogUnit> by_ext;

  explicit NativeFixture(std::optional<double> relevance_bias = std::nullopt) {
    Json archive = Json::array();
    for (const auto& [id, text] : std::vector<std::pair<std::string, std::string>>{
             {"positive", "Let the catch disengage."},
             {"lexical-trap", "The museum loom weaves wool."},
             {"negative", "Silver clouds drift overhead."},
             {"missing", "A copper fern blooms."},
             {"seed", "Nebulite Nebulite Nebulite."}})
      archive.push_back(conversation(id, text));
    const auto written = fsutil::write_file(source.path() / "conversations.json", json::dump(archive));
    REQUIRE(written.has_value());
    RuntimeOptions options;
    options.data_dir = data.path().string();
    options.start_workers = false;
    rt = take(Runtime::open(options));
    auto catalog_pack = take(rt->knowledge().pack());
    if (relevance_bias) {
      std::map<std::string, Json> documents;
      for (const auto& file : catalog_pack->files()) documents[file] = catalog_pack->file(file);
      documents["policy/relevance.json"]["bias"] = *relevance_bias;
      catalog_pack = take(kb::Pack::from_documents(std::move(documents)));
    }
    cat = std::make_unique<Catalog>(*rt, std::move(catalog_pack));
    ScanConfig scan;
    scan.sources = {source.path().string()};
    take(cat->scan(scan));
    profile = take(cat->build_profile(ProfileConfig{}));
    profile.terms = Json::array({
        Json{{"term", "Nebulite"}, {"key", "nebulite"}, {"class", "alias"}, {"project", "nebulite"}},
        Json{{"term", "loom"}, {"key", "loom"}, {"class", "alias"}, {"project", "loom"},
             {"ambiguous", true}, {"requires_context", Json{{"any", Json::array({"kernel"})}, {"min", 1}}},
             {"negative_context", Json::array({"weaves", "wool"})}}});
    profile.projects = Json::array();
    profile.input_hash = Sha256::hex(json::canonical(profile.terms));
    {
      auto lock = rt->db().lock();
      const auto updated = rt->db().conn().run("UPDATE loom_cat_profiles SET body = ? WHERE id = ?",
                                             json::dump(profile.to_json()), profile.id);
      REQUIRE(updated.has_value());
    }
    for (const auto& unit : take(cat->query(UnitQuery{}))) by_ext.emplace(unit.ext_id, unit);
    REQUIRE(by_ext.size() == 5);
  }

  Json envelope() const {
    Json value = config();
    value["profile_input_hash"] = profile.input_hash;
    value["records"] = Json::array();
    for (const auto& [id, vector] : std::vector<std::pair<std::string, Json>>{
             {"positive", Json::array({1.0, 0.0})},
             {"lexical-trap", Json::array({1.0, 0.0})},
             {"negative", Json::array({-1.0, 0.0})}}) {
      const auto& unit = by_ext.at(id);
      value["records"].push_back(Json{{"unit_id", unit.unit.id}, {"content_hash", unit.content_hash}, {"vector", vector}});
    }
    return value;
  }

  Result<Json> score() {
    ScoreConfig options;
    options.profile_id = profile.id;
    options.max_passes = 1;
    return cat->score(options);
  }

  std::map<std::string, Json> score_rows(const std::string& run_id) const {
    std::map<std::string, Json> rows;
    auto lock = rt->db().lock();
    auto statement = take(rt->db().conn().prepare(
        "SELECT u.ext_id,s.score,s.label,s.features,s.reasons,s.projects FROM loom_cat_scores s "
        "JOIN loom_cat_units u ON u.id=s.unit_id WHERE s.run_id=? ORDER BY u.ext_id"));
    statement.bind(1, run_id);
    while (take(statement.step())) {
      rows.emplace(statement.get_text(0), Json{{"score", statement.get_double(1)}, {"label", statement.get_text(2)},
                   {"features", json::parse_or(statement.get_text(3), Json::object())},
                   {"reasons", json::parse_or(statement.get_text(4), Json::array())},
                   {"projects", json::parse_or(statement.get_text(5), Json::array())}});
    }
    return rows;
  }

  std::map<std::string, Decision> decisions(const std::string& run_id) {
    std::map<std::string, Decision> result;
    for (const auto& decision : take(cat->select(run_id))) {
      for (const auto& [ext_id, unit] : by_ext)
        if (unit.unit.id == decision.unit_id) result.emplace(ext_id, decision);
    }
    return result;
  }

  Json persisted_scoring() const {
    Json snapshot = Json::object();
    auto lock = rt->db().lock();
    for (const auto& [table, ordering] : std::vector<std::pair<std::string, std::string>>{
             {"loom_cat_scores", "run_id,unit_id"}, {"loom_cat_links", "run_id,src,dst,link_type"},
             {"loom_cat_decisions", "run_id,unit_id"}}) {
      snapshot[table] = Json::array();
      auto statement = take(rt->db().conn().prepare("SELECT * FROM " + table + " ORDER BY " + ordering));
      while (take(statement.step())) {
        Json row = Json::array();
        for (int column = 0; column < statement.column_count(); ++column) row.push_back(statement.get_text(column));
        snapshot[table].push_back(std::move(row));
      }
    }
    return snapshot;
  }
};
#endif

}  // namespace

TEST_CASE("supplied semantic vectors retrieve disjoint wording and retain provenance") {
  // The query's meaning is represented by supplied test vectors: no lexical
  // token comparison, provider call or model quality measurement is involved.
  const std::string query_wording = "Open the latch";
  CHECK(query_wording.find("catch") == std::string::npos);
  auto result = evaluate_semantic_candidates(config(), "profile-hash", units());
  REQUIRE(result.has_value());
  CHECK(result->enabled);
  REQUIRE(result->hits.size() == 2);
  const auto& positive = result->hits.at("unit-related");
  const auto& negative = result->hits.at("unit-unrelated");
  CHECK(positive.cosine == doctest::Approx(1.0));
  CHECK(negative.cosine == doctest::Approx(-1.0));
  CHECK(positive.score >= result->tau_relevant);
  CHECK(negative.score < result->tau_relevant);
  CHECK(positive.evidence["score_kind"] == "retrieval_rank");
  CHECK(positive.evidence["origin"] == "supplied_vector");
  CHECK(positive.evidence["model"] == "offline-fixture");
  CHECK(positive.evidence["content_hash"] == "hash-related");
  CHECK_FALSE(positive.evidence.contains("confidence"));
  CHECK_FALSE(result->hits.contains("unit-missing"));
  CHECK(result->configuration_hash.size() == 64);
  CHECK(result->policy == config()["policy"]);
  auto repeat = evaluate_semantic_candidates(config(), "profile-hash", units());
  REQUIRE(repeat.has_value());
  CHECK(repeat->configuration_hash == result->configuration_hash);
  CHECK(repeat->hits.at("unit-related").evidence == positive.evidence);
}

TEST_CASE("disabled semantic channel ignores malformed inactive body and missing config") {
  for (const Json& input : {Json(nullptr), Json::object(),
                          Json{{"enabled", false}, {"query", "malformed"}, {"records", 17}}}) {
    auto result = evaluate_semantic_candidates(input, "unrelated-profile", {});
    REQUIRE(result.has_value());
    CHECK_FALSE(result->enabled);
    CHECK(result->hits.empty());
  }
  check_rejected(Json::array());
  check_rejected(Json{{"enabled", "false"}});
}

TEST_CASE("active semantic envelope validates schema identity shape and policy") {
  for (const char* field : {"schema", "profile_input_hash", "channel", "model", "method", "query", "records", "policy"}) {
    auto input = config();
    input.erase(field);
    check_rejected(input);
  }
  for (const char* field : {"channel", "model", "method"}) {
    auto input = config();
    input[field] = "";
    check_rejected(input);
    input[field] = 42;
    check_rejected(input);
  }
  auto input = config();
  input["schema"] = "loom.catalog_semantic_candidates/2";
  check_rejected(input);
  input = config(); input["profile_input_hash"] = "stale-profile";
  auto stale = evaluate_semantic_candidates(input, "profile-hash", units());
  REQUIRE_FALSE(stale.has_value());
  CHECK(stale.error().code == Errc::Conflict);
  input = config(); input["records"] = Json::object(); check_rejected(input);
  input = config(); input["policy"] = Json::array(); check_rejected(input);
  for (const char* field : {"bias", "weight", "tau_relevant", "fusion"}) {
    input = config(); input["policy"].erase(field); check_rejected(input);
  }
  for (const double threshold : {-0.01, 1.01}) {
    input = config(); input["policy"]["tau_relevant"] = threshold; check_rejected(input);
  }
  input = config(); input["policy"]["fusion"] = "veto"; check_rejected(input);
  for (const char* field : {"bias", "weight", "tau_relevant"}) {
    input = config(); input["policy"][field] = "0"; check_rejected(input);
    input["policy"][field] = std::numeric_limits<double>::infinity(); check_rejected(input);
  }
}

TEST_CASE("semantic vectors validate dimensions finite numeric values and nonzero norms") {
  const std::vector<Json> invalid_vectors = {
      Json(nullptr), Json::object(), Json::array(), Json::array({0.0, 0.0}),
      Json::array({"1", 0.0}), Json::array({true, 0.0}),
      Json::array({std::numeric_limits<double>::infinity(), 0.0}),
      Json::array({std::numeric_limits<double>::quiet_NaN(), 0.0})};
  for (const auto& vector : invalid_vectors) {
    auto input = config(); input["query"] = vector; check_rejected(input);
    input = config(); input["records"][0]["vector"] = vector; check_rejected(input);
  }
  auto input = config(); input["records"][0]["vector"] = Json::array({1.0}); check_rejected(input);
  input = config(); input["records"][0].erase("vector"); check_rejected(input);
}

TEST_CASE("semantic records require current exact hashes and unique known unit references") {
  auto input = config(); input["records"][0]["content_hash"] = "stale";
  auto stale = evaluate_semantic_candidates(input, "profile-hash", units());
  REQUIRE_FALSE(stale.has_value());
  CHECK(stale.error().code == Errc::Conflict);
  input = config(); input["records"][0]["unit_id"] = "unknown"; check_rejected(input);
  input = config(); input["records"].push_back(input["records"][0]); check_rejected(input);
  input = config(); input["records"][0] = nullptr; check_rejected(input);
  for (const char* field : {"unit_id", "content_hash"}) {
    input = config(); input["records"][0].erase(field); check_rejected(input);
  }
  auto duplicate_population = units(); duplicate_population.push_back(duplicate_population.front());
  check_rejected(config(), duplicate_population);
}

TEST_CASE("missing semantic records have no rank even with a high positive bias") {
  auto input = config(); input["records"] = Json::array(); input["policy"]["bias"] = 100.0;
  auto result = evaluate_semantic_candidates(input, "profile-hash", units());
  REQUIRE(result.has_value());
  CHECK(result->enabled);
  CHECK(result->hits.empty());
}

TEST_CASE("semantic cosine stays finite for extreme and subnormal finite vectors") {
  for (const double scale : {std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min()}) {
    auto input = config();
    input["query"] = Json::array({scale, scale});
    input["records"][0]["vector"] = Json::array({scale, scale});
    input["records"][1]["vector"] = Json::array({scale, -scale});
    auto result = evaluate_semantic_candidates(input, "profile-hash", units());
    REQUIRE(result.has_value());
    CHECK(result->hits.at("unit-related").cosine == doctest::Approx(1.0));
    CHECK(result->hits.at("unit-unrelated").cosine == doctest::Approx(0.0));
    CHECK(std::isfinite(result->hits.at("unit-related").score));
  }
  auto input = config(); input["policy"]["bias"] = std::numeric_limits<double>::max();
  input["policy"]["weight"] = std::numeric_limits<double>::max();
  auto result = evaluate_semantic_candidates(input, "profile-hash", units());
  REQUIRE(result.has_value());
  CHECK(result->hits.at("unit-related").score == 1.0);
  CHECK(result->hits.at("unit-unrelated").score == 0.5);
}

TEST_CASE("semantic policy preserves valid settings and fingerprints input changes") {
  auto input = config(); input["policy"]["fusion"] = "additive";
  input["policy"]["weight"] = -8.0; input["policy"]["tau_relevant"] = 1.0;
  auto first = evaluate_semantic_candidates(input, "profile-hash", units());
  REQUIRE(first.has_value());
  CHECK(first->fusion == "additive");
  CHECK(first->weight == -8.0);
  CHECK(first->hits.at("unit-related").score < first->hits.at("unit-unrelated").score);
  input["policy"]["tau_relevant"] = 0.0;
  auto second = evaluate_semantic_candidates(input, "profile-hash", units());
  REQUIRE(second.has_value());
  CHECK(second->configuration_hash != first->configuration_hash);
}

#ifndef LOOM_SEMANTIC_CANDIDATES_MODULE_ONLY
TEST_CASE("native catalog union retrieves without lexical aliases and cannot be vetoed by lexical traps") {
  NativeFixture fixture;
  const auto baseline = take(fixture.score());
  const auto baseline_run = json::get_string(baseline, "run_id");
  const auto baseline_rows = fixture.score_rows(baseline_run);
  const auto baseline_decisions = fixture.decisions(baseline_run);
  CHECK_FALSE(baseline_decisions.at("positive").selected);
  CHECK_FALSE(baseline_decisions.at("lexical-trap").selected);
  CHECK(json::get_number(baseline_rows.at("positive")["features"], "identity_alias_hits") == 0.0);
  CHECK(json::get_number(baseline_rows.at("lexical-trap")["features"], "neg_context") > 0.0);

  const auto envelope = fixture.envelope();
  fixture.rt->config().set("catalog_semantic_candidates", envelope);
  const auto scored = take(fixture.score());
  const auto run = json::get_string(scored, "run_id");
  const auto rows = fixture.score_rows(run);
  const auto selected = fixture.decisions(run);
  CHECK(run != baseline_run);
  CHECK(selected.at("positive").selected);
  CHECK(selected.at("lexical-trap").selected);
  CHECK_FALSE(selected.at("negative").selected);
  CHECK_FALSE(selected.at("missing").selected);
  for (const auto& id : {"positive", "lexical-trap"}) {
    CHECK(rows.at(id)["label"] == "relevant");
    CHECK(rows.at(id)["projects"] == baseline_rows.at(id)["projects"]);
    CHECK(json::get_number(rows.at(id)["features"], "identity_alias_hits") == 0.0);
    CHECK(rows.at(id)["features"]["semantic_score"] == baseline_rows.at(id)["features"]["semantic_score"]);
    CHECK(json::get_number(rows.at(id)["features"], "external_semantic_score") >= 0.7);
    bool found_provenance = false;
    for (const auto& reason : rows.at(id)["reasons"]) {
      if (json::get_string(reason, "feature") != "semantic_candidate") continue;
      const auto serialized = json::dump(reason);
      CHECK(serialized.find("offline-fixture") != std::string::npos);
      CHECK(serialized.find(fixture.by_ext.at(id).content_hash) != std::string::npos);
      CHECK(serialized.find("confidence") == std::string::npos);
      CHECK(reason["score_kind"] == "retrieval_rank");
      CHECK_FALSE(reason.contains("contribution"));
      const double delta = json::get_number(reason, "score_after") - json::get_number(reason, "score_before");
      CHECK(json::get_number(reason, "rank_delta") == doctest::Approx(std::round(delta * 1e4) / 1e4));
      CHECK(reason["score_before"] == baseline_rows.at(id)["score"]);
      CHECK(reason["score_after"] == rows.at(id)["score"]);
      found_provenance = true;
    }
    CHECK(found_provenance);
  }
}

TEST_CASE("native semantic channel fingerprints changed envelopes and disabling restores baseline") {
  NativeFixture fixture;
  const auto baseline_run = json::get_string(take(fixture.score()), "run_id");
  const auto baseline_rows = fixture.score_rows(baseline_run);
  const auto baseline_decisions = fixture.decisions(baseline_run);
  auto envelope = fixture.envelope();
  fixture.rt->config().set("catalog_semantic_candidates", envelope);
  const auto active_run = json::get_string(take(fixture.score()), "run_id");
  CHECK(active_run != baseline_run);
  envelope["model"] = "second-offline-model";
  fixture.rt->config().set("catalog_semantic_candidates", envelope);
  const auto changed_run = json::get_string(take(fixture.score()), "run_id");
  CHECK(changed_run != active_run);
  fixture.rt->config().set("catalog_semantic_candidates", Json{{"enabled", false}, {"query", "ignored"}});
  const auto disabled_run = json::get_string(take(fixture.score()), "run_id");
  CHECK(disabled_run == baseline_run);
  CHECK(fixture.score_rows(disabled_run) == baseline_rows);
  const auto disabled_decisions = fixture.decisions(disabled_run);
  for (const auto& [id, decision] : baseline_decisions)
    CHECK(disabled_decisions.at(id).selected == decision.selected);
}

TEST_CASE("native missing semantic records do not receive a positive bias") {
  NativeFixture fixture;
  const auto baseline_run = json::get_string(take(fixture.score()), "run_id");
  const auto baseline = fixture.score_rows(baseline_run);
  auto envelope = fixture.envelope();
  envelope["records"] = Json::array({envelope["records"][0]});
  envelope["policy"]["bias"] = 100.0;
  fixture.rt->config().set("catalog_semantic_candidates", envelope);
  const auto run = json::get_string(take(fixture.score()), "run_id");
  const auto rows = fixture.score_rows(run);
  CHECK(json::get_number(rows.at("positive"), "score") == 1.0);
  for (const auto& id : {"negative", "missing", "seed", "lexical-trap"}) {
    CHECK(rows.at(id)["score"] == baseline.at(id)["score"]);
    CHECK(rows.at(id)["label"] == baseline.at(id)["label"]);
    CHECK_FALSE(rows.at(id)["features"].contains("external_semantic_score"));
  }
}

TEST_CASE("native union and additive fusion remain separately configurable") {
  NativeFixture fixture;
  auto envelope = fixture.envelope();
  envelope["policy"]["bias"] = 0.0;
  envelope["policy"]["weight"] = 2.0;
  fixture.rt->config().set("catalog_semantic_candidates", envelope);
  const auto union_run = json::get_string(take(fixture.score()), "run_id");
  CHECK(fixture.decisions(union_run).at("positive").selected);
  CHECK(fixture.decisions(union_run).at("lexical-trap").selected);
  envelope["policy"]["fusion"] = "additive";
  fixture.rt->config().set("catalog_semantic_candidates", envelope);
  const auto additive_run = json::get_string(take(fixture.score()), "run_id");
  CHECK(additive_run != union_run);
  CHECK_FALSE(fixture.decisions(additive_run).at("positive").selected);
  CHECK_FALSE(fixture.decisions(additive_run).at("lexical-trap").selected);
  CHECK(fixture.score_rows(additive_run).at("positive")["features"]["external_semantic_score"] ==
        fixture.score_rows(union_run).at("positive")["features"]["external_semantic_score"]);
}

TEST_CASE("native configurable additive evidence can move quantized zero and one baseline ranks") {
  SUBCASE("strong positive evidence rescues a rounded-zero baseline") {
    NativeFixture fixture(-100.0);
    const auto baseline_run = json::get_string(take(fixture.score()), "run_id");
    CHECK(fixture.score_rows(baseline_run).at("positive")["score"] == 0.0);
    CHECK_FALSE(fixture.decisions(baseline_run).at("positive").selected);
    auto envelope = fixture.envelope();
    envelope["records"] = Json::array({envelope["records"][0]});
    envelope["policy"]["fusion"] = "additive";
    envelope["policy"]["bias"] = 1e10;
    envelope["policy"]["weight"] = 0.0;
    fixture.rt->config().set("catalog_semantic_candidates", envelope);
    const auto run = json::get_string(take(fixture.score()), "run_id");
    CHECK(fixture.score_rows(run).at("positive")["score"] == 1.0);
    CHECK(fixture.decisions(run).at("positive").selected);
    CHECK(fixture.score_rows(run).at("missing")["score"] == 0.0);
  }
  SUBCASE("strong negative evidence demotes a rounded-one seed") {
    NativeFixture fixture(100.0);
    const auto baseline_run = json::get_string(take(fixture.score()), "run_id");
    CHECK(fixture.score_rows(baseline_run).at("seed")["score"] == 1.0);
    CHECK(fixture.decisions(baseline_run).at("seed").selected);
    auto envelope = fixture.envelope();
    const auto& seed = fixture.by_ext.at("seed");
    envelope["records"] = Json::array({Json{{"unit_id", seed.unit.id}, {"content_hash", seed.content_hash},
                                         {"vector", Json::array({1.0, 0.0})}}});
    envelope["policy"]["fusion"] = "additive";
    envelope["policy"]["bias"] = -1e10;
    envelope["policy"]["weight"] = 0.0;
    fixture.rt->config().set("catalog_semantic_candidates", envelope);
    const auto run = json::get_string(take(fixture.score()), "run_id");
    CHECK(fixture.score_rows(run).at("seed")["score"] == 0.0);
    CHECK_FALSE(fixture.decisions(run).at("seed").selected);
    CHECK(fixture.score_rows(run).at("missing")["score"] == 1.0);
  }
}

TEST_CASE("native owner selection overrides win over semantic acceptance and rejection") {
  NativeFixture fixture;
  fixture.rt->config().set("catalog_semantic_candidates", fixture.envelope());
  const auto run = json::get_string(take(fixture.score()), "run_id");
  Override exclude;
  exclude.unit_id = fixture.by_ext.at("positive").unit.id;
  exclude.action = "exclude";
  exclude.reason = "owner rejects this unit";
  REQUIRE(fixture.cat->set_override(exclude).has_value());
  Override include;
  include.unit_id = fixture.by_ext.at("negative").unit.id;
  include.action = "include";
  include.reason = "owner includes counterevidence";
  REQUIRE(fixture.cat->set_override(include).has_value());
  auto decisions = fixture.decisions(run);
  CHECK_FALSE(decisions.at("positive").selected);
  CHECK(decisions.at("negative").selected);
  CHECK(decisions.at("positive").decided_by == "user");
  CHECK(decisions.at("negative").decided_by == "user");
  include.action = "pin";
  REQUIRE(fixture.cat->set_override(include).has_value());
  decisions = fixture.decisions(run);
  CHECK(decisions.at("negative").selected);
  CHECK(decisions.at("negative").decided_by == "user");
  fixture.rt->config().set("catalog_semantic_candidates", Json{{"enabled", false}});
  decisions = fixture.decisions(json::get_string(take(fixture.score()), "run_id"));
  CHECK_FALSE(decisions.at("positive").selected);
  CHECK(decisions.at("negative").selected);
}

TEST_CASE("native invalid semantic configuration cannot change persisted scores links or decisions") {
  NativeFixture fixture;
  const auto baseline_run = json::get_string(take(fixture.score()), "run_id");
  fixture.decisions(baseline_run);
  const auto baseline_snapshot = fixture.persisted_scoring();
  for (const auto& problem : {"stale-profile", "stale-content", "dimensions", "duplicate", "nonfinite"}) {
    auto envelope = fixture.envelope();
    const std::string kind(problem);
    if (kind == "stale-profile") envelope["profile_input_hash"] = "stale";
    if (kind == "stale-content") envelope["records"][0]["content_hash"] = "stale";
    if (kind == "dimensions") envelope["records"][0]["vector"] = Json::array({1.0});
    if (kind == "duplicate") envelope["records"].push_back(envelope["records"][0]);
    if (kind == "nonfinite") envelope["query"][0] = std::numeric_limits<double>::infinity();
    fixture.rt->config().set("catalog_semantic_candidates", envelope);
    const auto result = fixture.score();
    CHECK_FALSE(result.has_value());
    CHECK(fixture.persisted_scoring() == baseline_snapshot);
  }
}
#endif
