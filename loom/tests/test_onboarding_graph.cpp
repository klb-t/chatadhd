#include <doctest/doctest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/onboarding.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::unwrap;

namespace {
struct GraphFixture {
  Json pack = unwrap(builtin_pack());
  Json scenario = unwrap(builtin_scenario());
  DefaultLayers layers = unwrap(DefaultLayers::create(pack));
  Json profile;
  GraphFixture() {
    Json defaults;
    defaults["privacy"] = unwrap(layers.resolve("onboarding.privacy"))["value"];
    defaults["settings"] = unwrap(layers.resolve("onboarding.settings"))["value"];
    profile = unwrap(ProfileSession::create(scenario, defaults)).snapshot();
  }
  Json project() const { return unwrap(project_graph(pack, scenario, profile, layers.snapshot(), "synthetic-user")); }
};
const Json* entity_with(const Json& graph, std::string_view kind, std::string_view attr, std::string_view value) {
  for (const auto& entity : graph["entities"])
    if (entity["kind"] == kind && json::get_string(entity["attrs"], attr) == value) return &entity;
  return nullptr;
}
Json action(DefaultLayers& layers, const Json& action) {
  auto state = unwrap(layers.dispatch(action));
  layers = unwrap(DefaultLayers::create(layers.pack(), state));
  return state;
}
}  // namespace

TEST_SUITE("onboarding native graph projection") {
  TEST_CASE("new user has native types defaults profile and declared methods without fictitious personal projects or execution") {
    GraphFixture fixture;
    const Json graph = fixture.project();
    REQUIRE_FALSE(graph["entities"].empty());
    REQUIRE_FALSE(graph["claims"].empty());
    REQUIRE_FALSE(graph["observations"].empty());
    std::set<std::string> entities, observations;
    for (const auto& row : graph["entities"]) {
      auto entity = unwrap(model::Entity::from_json(row));
      CHECK(entities.insert(entity.id).second);
      CHECK(entity.kind != "project");
      CHECK(entity.kind != "run");
    }
    for (const auto& row : graph["observations"]) {
      auto observation = unwrap(model::Observation::from_json(row));
      CHECK(observations.insert(observation.id).second);
      CHECK(observation.locator.source == "sha256:" + Sha256::hex(observation.text));
      CHECK(observation.locator.byte_len == static_cast<std::int64_t>(observation.text.size()));
      CHECK(observation.attrs["capture_scope"] == "record_bytes_only");
    }
    for (const auto& row : graph["claims"]) {
      const auto claim = unwrap(model::Claim::from_json(row));
      LOOM_REQUIRE_OK(claim.validate());
      CHECK(entities.contains(claim.subject));
      if (!claim.object.empty()) CHECK(entities.contains(claim.object));
      for (const auto& support : claim.assessment.support) CHECK(observations.contains(support.observation));
    }
    const auto field = entity_with(graph, fixture.pack["vocabulary"]["kinds"]["field"].get<std::string>(), "field_id", "work.projects");
    REQUIRE(field);
    CHECK((*field)["attrs"]["status"] == "unknown");
    CHECK((*field)["attrs"]["value"].is_null());
    bool self_probe = false, type_definition = false;
    for (const auto& entity : graph["entities"]) {
      if (json::get_string(entity["attrs"], "path") == "profiles/self.json") {
        self_probe = true;
        CHECK(entity["attrs"]["role"] == "application_probe");
        CHECK(entity["attrs"]["user_personal_fact"] == false);
      }
      if (entity["kind"] == fixture.pack["vocabulary"]["kinds"]["type"]) type_definition = true;
    }
    CHECK(self_probe);
    CHECK(type_definition);
    REQUIRE(graph["method_profile"]["bindings"].size() == 1);
    CHECK_FALSE(graph["method_profile"].contains("trace"));
    CHECK_FALSE(graph["method_profile"].contains("manifest"));
    REQUIRE(graph["method_profile"]["selection"]["members"].size() == 1);
  }

  TEST_CASE("native rows persist in existing KnowledgeStore and reproduce after read") {
    GraphFixture fixture;
    const Json graph = fixture.project();
    fsutil::TempDir temporary;
    auto db = loom::test::open_db(temporary.path() / "projection.db");
    kb::KnowledgeStore store(*db);
    const auto run = unwrap(store.begin_run("synthetic-pack", Json{{"owner", "synthetic-user"}}));
    std::vector<model::Entity> entities;
    std::vector<model::Claim> claims;
    std::vector<model::Observation> observations;
    for (const auto& row : graph["entities"]) entities.push_back(unwrap(model::Entity::from_json(row)));
    for (const auto& row : graph["claims"]) claims.push_back(unwrap(model::Claim::from_json(row)));
    for (const auto& row : graph["observations"]) observations.push_back(unwrap(model::Observation::from_json(row)));
    LOOM_REQUIRE_OK(store.put_observations(run.id, observations));
    LOOM_REQUIRE_OK(store.put_entities(run.id, entities));
    LOOM_REQUIRE_OK(store.put_claims(run.id, claims));
    for (const auto& entity : entities) {
      auto read = unwrap(store.get_entity(run.id, entity.id)); REQUIRE(read); CHECK(read->to_json() == entity.to_json());
    }
    for (const auto& claim : claims) {
      auto read = unwrap(store.get_claim(run.id, claim.id)); REQUIRE(read); CHECK(read->to_json() == claim.to_json());
    }
    CHECK(json::canonical(fixture.project()) == json::canonical(graph));
  }

  TEST_CASE("multiple versions share method identity while preserving exact version bindings") {
    GraphFixture fixture;
    Json alternative = fixture.scenario["graph_method"];
    alternative["revision"] = alternative["revision"].get<std::int64_t>() + 1;
    alternative["parameters"]["synthetic_setting"] = 1;
    fixture.pack["methods"] = Json::array({alternative});
    fixture.pack["revision"] = fixture.pack["revision"].get<std::int64_t>() + 1;
    const auto graph = fixture.project();
    const auto& bindings = graph["method_profile"]["bindings"];
    REQUIRE(bindings.size() == 2);
    CHECK(bindings[0]["method_id"] == bindings[1]["method_id"]);
    CHECK(bindings[0]["method_version_id"] != bindings[1]["method_version_id"]);
    CHECK(graph["method_profile"]["selection"]["members"].size() == 2);
    std::size_t method_edges = 0, version_edges = 0;
    for (const auto& claim : graph["claims"]) {
      if (claim["predicate"] == fixture.pack["vocabulary"]["predicates"]["has_method"]) ++method_edges;
      if (claim["predicate"] == fixture.pack["vocabulary"]["predicates"]["uses_method_version"]) ++version_edges;
    }
    CHECK(method_edges == 1);
    CHECK(version_edges == 2);
    CHECK(json::canonical(fixture.project()) == json::canonical(graph));
  }

  TEST_CASE("immutable method definitions retain exact bytes hashes parameters and real version edges") {
    GraphFixture fixture;
    const auto graph = fixture.project();
    const auto& method = graph["method_profile"];
    const auto& binding = method["bindings"][0];
    std::map<std::string, Json> rows;
    std::set<std::string> captured;
    for (const auto& source : method["sources"]) {
      const std::string bytes = source["observation"]["text"];
      CHECK(source["text_sha256"] == Sha256::hex(bytes));
      captured.insert(bytes);
    }
    for (const auto& row : method["entities"]) {
      rows[row["id"].get<std::string>()] = row;
      const auto& attrs = row["attrs"];
      if (attrs.contains("definition_sha256")) {
        const auto bytes = json::canonical(attrs["definition"]);
        CHECK(attrs["definition_sha256"] == Sha256::hex(bytes));
        CHECK(captured.contains(bytes));
      }
      if (attrs.contains("text_sha256")) {
        const auto bytes = attrs["text"].get<std::string>();
        CHECK(attrs["text_sha256"] == Sha256::hex(bytes));
        CHECK(captured.contains(bytes));
      }
    }
    auto has_edge = [&](const char* from, const char* predicate, const char* to) {
      for (const auto& claim : method["claims"])
        if (claim["subject"] == binding[from] && claim["predicate"] == fixture.pack["vocabulary"]["predicates"][predicate] && claim["object"] == binding[to]) return true;
      return false;
    };
    CHECK(has_edge("method_version_id", "version_of", "method_id"));
    CHECK(has_edge("method_version_id", "uses_recipe", "recipe_version_id"));
    CHECK(has_edge("recipe_version_id", "uses_prompt", "prompt_version_id"));
    CHECK(has_edge("method_version_id", "uses_parameter_set", "parameter_set_version_id"));
    const auto& version = rows.at(binding["method_version_id"].get<std::string>());
    CHECK(version["attrs"]["definition"]["recipe_sha256"] == rows.at(binding["recipe_version_id"].get<std::string>())["attrs"]["definition_sha256"]);
    CHECK(version["attrs"]["definition"]["parameter_set_sha256"] == rows.at(binding["parameter_set_version_id"].get<std::string>())["attrs"]["definition_sha256"]);
    auto changed = fixture;
    changed.scenario["graph_method"]["prompt"]["text"] = "An edited exact synthetic prompt.";
    auto edited = changed.project();
    CHECK(edited["method_profile"]["bindings"][0]["method_id"] == binding["method_id"]);
    CHECK(edited["method_profile"]["bindings"][0]["method_version_id"] != binding["method_version_id"]);
    CHECK(edited["method_profile"]["bindings"][0]["prompt_version_id"] != binding["prompt_version_id"]);
  }

  TEST_CASE("model inferred information stays unverified while direct and form statements use user evidence") {
    GraphFixture fixture;
    for (const auto& [id, provenance] : std::map<std::string, std::string>{{"identity.description", "model_inferred"}, {"work.projects", "user_stated"}, {"communication.style", "form"}}) {
      auto& field = fixture.profile["fields"][id];
      field["status"] = "known"; field["value"] = "synthetic-value";
      field["provenance"] = provenance; field["review"] = "confirmed";
    }
    const auto graph = fixture.project();
    for (const auto& [id, provenance] : std::map<std::string, std::string>{{"identity.description", "model_inferred"}, {"work.projects", "user_stated"}, {"communication.style", "form"}}) {
      const auto field = entity_with(graph, fixture.pack["vocabulary"]["kinds"]["field"].get<std::string>(), "field_id", id);
      REQUIRE(field);
      CHECK((*field)["attrs"]["provenance"] == provenance);
      const bool inferred = provenance == "model_inferred";
      CHECK((*field)["origin"] == (inferred ? "model_knowledge" : "user"));
      CHECK((*field)["evidence_class"] == (inferred ? "inferred" : "user"));
      int value_claims = 0;
      for (const auto& claim : graph["claims"]) if (claim["subject"] == (*field)["id"] && claim["predicate"] == fixture.pack["vocabulary"]["predicates"]["has_value"]) {
        ++value_claims;
        CHECK(claim["assessment"]["evidence_class"] == "user");
        CHECK(claim["assessment"]["origin"] == "user");
      }
      CHECK(value_claims == (inferred ? 0 : 1));
      if (inferred) CHECK((*field)["attrs"]["content_verification"] == "unverified");
    }
  }

  TEST_CASE("declined and never have no generated questions and old discarded bytes never return through snapshot captures") {
    GraphFixture fixture;
    fixture.profile["fields"]["identity.description"]["status"] = "declined";
    fixture.profile["fields"]["work.projects"]["status"] = "never";
    fixture.profile["history"] = Json::array({Json{{"value", "DISCARDED-SYNTHETIC-CONTENT"}}});
    fixture.profile["session"]["old_summary"] = "DISCARDED-SYNTHETIC-CONTENT";
    fixture.profile["candidates"]["old"] = Json{{"id", "old"}, {"review", "rejected"}, {"value", "DISCARDED-SYNTHETIC-CONTENT"}};
    const auto graph = fixture.project();
    CHECK(json::canonical(graph).find("DISCARDED-SYNTHETIC-CONTENT") == std::string::npos);
    for (const auto* id : {"identity.description", "work.projects"}) {
      const auto field = entity_with(graph, fixture.pack["vocabulary"]["kinds"]["field"].get<std::string>(), "field_id", id);
      REQUIRE(field);
      for (const auto& claim : graph["claims"]) if (claim["subject"] == (*field)["id"] && claim["assessment"]["evidence_class"] == "absent")
        CHECK(claim["assessment"]["open"]["questions"].empty());
    }
  }

  TEST_CASE("permanent exclusions suppress values application probe and method registration after pack upgrade") {
    GraphFixture fixture;
    action(fixture.layers, Json{{"op", "exclude"}, {"key", "profile.self"}});
    action(fixture.layers, Json{{"op", "exclude"}, {"key", "preference.style"}});
    action(fixture.layers, Json{{"op", "exclude"}, {"key", "method.onboarding"}});
    fixture.pack["revision"] = fixture.pack["revision"].get<std::int64_t>() + 1;
    fixture.layers = unwrap(DefaultLayers::create(fixture.pack, fixture.layers.snapshot()));
    const auto graph = fixture.project();
    CHECK(graph["method_profile"]["entities"].empty());
    CHECK(graph["method_profile"]["selection"]["members"].empty());
    for (const auto& entity : graph["entities"]) {
      CHECK(json::get_string(entity["attrs"], "path") != "profiles/self.json");
      if (entity["kind"] == fixture.pack["vocabulary"]["kinds"]["default"] && json::get_string(entity["attrs"], "key") == "preference.style") {
        CHECK(entity["attrs"]["status"] == "excluded");
        CHECK_FALSE(entity["attrs"].contains("value"));
      }
    }
  }

  TEST_CASE("actual host capture creates result provenance edges without turning receipt into content truth") {
    GraphFixture fixture;
    const auto prepared = fixture.project();
    const auto& method = prepared["method_profile"];
    const std::string version = method["bindings"][0]["method_version_id"];
    fixture.profile["candidates"]["captured-result"] = Json{{"field", "work.projects"}, {"value", "synthetic model proposition"},
        {"review", "pending"}, {"provenance", "model_inferred"}, {"time", "2000-01-01T00:00:00Z"}};
    fixture.profile["method_executions"] = Json::array({Json{{"run_id", "e_synthetic_actual_capture"},
        {"method_profile", method}, {"method_version_id", version}, {"request_token", "synthetic-request-token"},
        {"response_sha256", Sha256::hex("synthetic exact captured response bytes")}, {"provider", "offline-fixture"},
        {"known_at", "2000-01-01T00:00:00Z"}, {"result_candidate_ids", Json::array({"captured-result"})}}});
    const auto graph = fixture.project();
    const Json* result = nullptr;
    int runs = 0;
    for (const auto& entity : graph["entities"]) {
      if (json::get_string(entity["attrs"], "role") == "pending_candidate") result = &entity;
      if (entity["kind"] == fixture.pack["vocabulary"]["kinds"]["run"]) {
        ++runs; CHECK(entity["id"] == "e_synthetic_actual_capture");
        CHECK(entity["attrs"]["content_truth"] == "not_established");
        CHECK_FALSE(entity["attrs"].contains("measurement_status"));
        CHECK_FALSE(entity["attrs"].contains("quality"));
      }
    }
    REQUIRE(result);
    CHECK(runs == 1);
    bool produced_in_run = false, produced_by = false, requested_version = false;
    for (const auto& claim : graph["claims"]) {
      if (claim["subject"] == (*result)["id"] && claim["predicate"] == fixture.pack["vocabulary"]["predicates"]["produced_in_run"]) {
        produced_in_run = true; CHECK(claim["object"] == "e_synthetic_actual_capture");
      }
      if (claim["subject"] == (*result)["id"] && claim["predicate"] == fixture.pack["vocabulary"]["predicates"]["produced_by_method_version"]) {
        produced_by = true; CHECK(claim["object"] == version);
      }
      if (claim["subject"] == "e_synthetic_actual_capture" && claim["predicate"] == fixture.pack["vocabulary"]["predicates"]["requests_method_version"]) {
        requested_version = true; CHECK(claim["object"] == version);
      }
    }
    CHECK(produced_in_run); CHECK(produced_by); CHECK(requested_version);
    CHECK((*result)["evidence_class"] == "inferred");
    auto tampered = fixture;
    tampered.profile["method_executions"][0]["method_profile"]["sources"][0]["text_sha256"] = std::string(64, '0');
    const auto rejected = project_graph(tampered.pack, tampered.scenario, tampered.profile, tampered.layers.snapshot(), "synthetic-user");
    REQUIRE_FALSE(rejected); CHECK(rejected.error().code == Errc::Conflict);
    fixture.profile["candidates"].erase("captured-result");
    auto cleared = fixture.project();
    for (const auto& entity : cleared["entities"]) CHECK(entity["kind"] != fixture.pack["vocabulary"]["kinds"]["run"]);
  }

  TEST_CASE("automatic inferred default overrides retain model origin rather than becoming user statements") {
    GraphFixture fixture;
    action(fixture.layers, Json{{"op", "override"}, {"key", "preference.length"}, {"value", "synthetic inferred length"},
        {"provenance", "model_inferred"}, {"review", "accepted_automatically"}});
    const auto graph = fixture.project();
    const Json* effective = nullptr;
    for (const auto& entity : graph["entities"])
      if (entity["kind"] == fixture.pack["vocabulary"]["kinds"]["default"] && json::get_string(entity["attrs"], "key") == "preference.length") effective = &entity;
    REQUIRE(effective);
    CHECK((*effective)["origin"] == "model_knowledge");
    CHECK((*effective)["evidence_class"] == "inferred");
    CHECK((*effective)["attrs"]["content_verification"] == "unverified");
    CHECK((*effective)["attrs"]["value"] == "synthetic inferred length");
    for (const auto& claim : graph["claims"]) if (claim["subject"] == (*effective)["id"])
      CHECK(claim["predicate"] != fixture.pack["vocabulary"]["predicates"]["has_value"]);
  }

  TEST_CASE("a user prompt override changes concrete definitions and its exclusion disables registration") {
    GraphFixture fixture;
    action(fixture.layers, Json{{"op", "override"}, {"key", "prompt.onboarding"}, {"value", "Synthetic user supplied prompt"}});
    auto graph = fixture.project();
    bool changed_prompt = false;
    for (const auto& entity : graph["method_profile"]["entities"])
      if (entity["kind"] == fixture.pack["vocabulary"]["kinds"]["prompt_version"]) {
        changed_prompt = true;
        CHECK(entity["attrs"]["text"] == "Synthetic user supplied prompt");
        CHECK(entity["origin"] == "system");
      }
    CHECK(changed_prompt);
    action(fixture.layers, Json{{"op", "exclude"}, {"key", "prompt.onboarding"}});
    graph = fixture.project();
    CHECK(graph["method_profile"]["entities"].empty());
    CHECK(graph["method_profile"]["selection"]["members"].empty());
  }

  TEST_CASE("vocabulary absence and contradictory immutable hashes are explicit errors") {
    GraphFixture fixture;
    fixture.pack["vocabulary"]["kinds"].erase("field");
    fixture.pack["revision"] = fixture.pack["revision"].get<std::int64_t>() + 1;
    const auto unavailable = project_graph(fixture.pack, fixture.scenario, fixture.profile, fixture.layers.snapshot(), "synthetic-user");
    REQUIRE_FALSE(unavailable);
    CHECK(unavailable.error().code == Errc::Unavailable);
    GraphFixture contradictory;
    contradictory.scenario["graph_method"]["recipe"]["prompt_sha256"] = std::string(64, '0');
    const auto drift = project_graph(contradictory.pack, contradictory.scenario, contradictory.profile, contradictory.layers.snapshot(), "synthetic-user");
    REQUIRE_FALSE(drift);
    CHECK(drift.error().code == Errc::Conflict);
  }
}
