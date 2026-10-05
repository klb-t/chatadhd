// Synthetic native selector regressions. W4 cases execute its actual packet
// codec; no paid service, owner archive, private fixture or preset catalogue.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <set>

#include "context_execution.h"
#include "method_execution.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

namespace {
using namespace loom;
using namespace loom::context;
template<class T> T execution_must(Result<T> result) {
  if (!result) FAIL("synthetic selector fixture failed: " << result.error().to_string());
  return std::move(result).value();
}
#ifdef LOOM_METHOD_REGISTRY_W4
constexpr const char* execution_known = "2026-10-04T14:00:00Z";
Json execution_origin() {
  return Json{{"kind", "system"}, {"actor", "synthetic-native-instrument"},
      {"model", nullptr}, {"recipe_sha256", nullptr}, {"response_sha256", nullptr}};
}
std::string execution_hash(const Json& value) { return Sha256::hex(json::canonical(value)); }
#endif

struct ExecutionFixture {
  fsutil::TempDir directory;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  std::shared_ptr<const kb::Pack> pack;
  std::unique_ptr<ContextEngine> engine;
  ContextRequest request;
  const std::string x = "synthetic_claim_x", y = "synthetic_claim_y";

  ExecutionFixture() {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = transport;
    runtime = execution_must(Runtime::open(options));
    runtime->config().set("semantic_analysis", false);
    pack = execution_must(runtime->knowledge().pack());
    auto& store = runtime->knowledge().store();
    request.run = execution_must(store.begin_run(pack->hash(), Json{{"fixture", "synthetic_methods"}})).id;
    std::vector<model::Entity> entities;
    std::vector<model::Claim> claims;
    for (const auto& [suffix, text] : std::vector<std::pair<std::string, std::string>>{
        {"x", "orchard orchard bridge"}, {"y", "orchard bridge bridge."}}) {
      model::Entity entity;
      entity.id = "synthetic_entity_" + suffix;
      entity.kind = "component";
      entity.label = "Synthetic " + suffix;
      entity.canonical_key = entity.id;
      entity.evidence = model::EvidenceClass::User;
      entity.origin = model::Origin::User;
      entities.push_back(entity);
      model::Claim claim;
      claim.id = suffix == "x" ? x : y;
      claim.subject = entity.id;
      claim.predicate = "records";
      claim.value = text;
      claim.assessment.evidence = model::EvidenceClass::User;
      claim.assessment.origin = model::Origin::User;
      claim.assessment.confidence = 1;
      claims.push_back(claim);
    }
    REQUIRE(store.put_entities(request.run, entities));
    REQUIRE(store.put_claims(request.run, claims));
    REQUIRE(store.finish_run(request.run, "done", Json::object()));
    request.text = "verify synthetic selector ranking";
    request.goal_type = "verify_claim";
    request.budget_tokens = 2000;
    request.relation_hops = 0;
    engine = std::make_unique<ContextEngine>(*runtime, store, pack);
  }
  model::ContextSet select(const Json& settings) {
    ContextExecutionScope scope(Json{{"method_registry", settings}});
    return execution_must(engine->select(request));
  }
  int one_item_budget(const model::ContextSet& full) const {
    REQUIRE(full.items.size() == 2);
    const int largest = std::max(full.items[0].tokens, full.items[1].tokens);
    REQUIRE(full.items[0].tokens + full.items[1].tokens > largest);
    return largest;
  }
};

#ifdef LOOM_METHOD_REGISTRY_W4
Json execution_vocabulary() {
  Json kinds = Json::object(), predicates = Json::object();
  for (const auto* name : {"method", "method_version", "prompt_version", "recipe_version", "preset_version",
      "combination_version", "parameter_set_version", "run", "model_identity", "compiler_transform", "result"})
    kinds[name] = "synthetic.kind/" + std::string(name);
  for (const auto* name : {"version_of", "uses_recipe", "uses_prompt", "uses_preset", "includes_method",
      "uses_parameter_set", "uses_combination", "requests_method_version", "produced_in_run",
      "produced_by_method_version", "projected_by_compiler"})
    predicates[name] = "synthetic.relation/" + std::string(name);
  return Json{{"kinds", kinds}, {"predicates", predicates}};
}
Json execution_entity(std::string id, const std::string& role, Json attrs = Json::object()) {
  model::Entity entity;
  entity.id = std::move(id);
  entity.canonical_key = entity.id;
  entity.kind = execution_vocabulary()["kinds"][role];
  entity.label = entity.id;
  entity.evidence = model::EvidenceClass::User;
  entity.origin = model::Origin::User;
  entity.attrs = std::move(attrs);
  entity.first_seen = execution_known;
  entity.last_seen = execution_known;
  return entity.to_json();
}
void execution_definition_method(Json& profile, const std::string& suffix, const Json& definition) {
  const auto bytes = json::canonical(definition);
  model::Observation observation;
  observation.id = "synthetic_definition_" + suffix;
  observation.unit = "synthetic-method-fixture";
  observation.kind = model::ObservationKind::Field;
  observation.text = bytes;
  observation.locator.source = "sha256:" + Sha256::hex(bytes);
  observation.locator.byte_start = 0;
  observation.locator.byte_len = bytes.size();
  profile["sources"].push_back(Json{{"observation", observation.to_json()}, {"known_at", execution_known},
      {"text_sha256", Sha256::hex(bytes)}});
  const auto identity = "synthetic_method_" + suffix;
  const auto version = "synthetic_version_" + suffix;
  profile["entities"].push_back(execution_entity(identity, "method"));
  profile["entities"].push_back(execution_entity(version, "method_version",
      Json{{"definition", definition}, {"definition_sha256", execution_hash(definition)}}));
  model::Claim claim;
  claim.subject = version;
  claim.predicate = execution_vocabulary()["predicates"]["version_of"];
  claim.object = identity;
  claim.qualifiers.extra = Json{{"confidence_scope", "structure_only"}, {"content_truth", "not_established"}};
  claim.assessment.evidence = model::EvidenceClass::Derived;
  claim.assessment.origin = model::Origin::System;
  claim.assessment.confidence = 1;
  claim.assessment.derivation = model::Derivation{"synthetic.native_projection", 1, "", 0};
  claim.assessment.support.push_back(model::Support{observation.id, observation.locator, bytes, "synthetic.native_projection", 1});
  claim.id = model::Claim::make_id(claim.subject, claim.predicate, claim.object, claim.value, claim.qualifiers);
  profile["claims"].push_back(claim.to_json());
}
void execution_method(Json& profile, const std::string& suffix, const std::string& pattern) {
  const Json parameters{{"patterns", Json::array({pattern})}, {"flags", Json::array({"ECMAScript"})},
      {"match", "search"}, {"semantics", "match_count"}, {"limit", 100}, {"min_score", 0}};
  execution_definition_method(profile, suffix, Json{{"execution_capability", "regex"}, {"parameters", parameters}});
}
Json execution_settings(double first_weight = 10, double second_weight = 1) {
  Json profile{{"vocabulary", execution_vocabulary()}, {"entities", Json::array()},
      {"claims", Json::array()}, {"sources", Json::array()}};
  execution_method(profile, "orchard", "orchard");
  execution_method(profile, "bridge", "bridge");
  profile["selection"] = Json{{"members", Json::array({
      Json{{"method_version_id", "synthetic_version_orchard"}, {"weight", first_weight}},
      Json{{"method_version_id", "synthetic_version_bridge"}, {"weight", second_weight}}})},
      {"parameter_layers", Json::array({"method", "member", "selection", "user"})},
      {"fusion", Json{{"operation", "sum"}, {"signal", "raw_score"}}}};
  return Json{{"enabled", true}, {"profile", profile}, {"graph_blend_operation", "method_only"},
      {"diversity", Json{{"operation", "identity"}}},
      {"run_context", Json{{"origin", execution_origin()}, {"known_at", execution_known}}}};
}
Json execution_result_settings(double first_weight, double second_weight) {
  Json settings = execution_settings(first_weight, second_weight);
  for (const auto* stage : {"fusion", "selection"}) {
    execution_definition_method(settings["profile"], stage,
        Json{{"execution_capability", "context_" + std::string(stage)}, {"parameters", Json::object()}});
    settings["result_methods"][stage] = Json{{"members", Json::array({
        Json{{"method_version_id", "synthetic_version_" + std::string(stage)}, {"weight", 1}}})},
        {"parameter_layers", Json::array({"method", "member", "selection", "user"})},
        {"parameters", Json::object()}, {"user_overrides", Json::object()}};
  }
  return settings;
}
const Json& execution_packet_entity(const Json& packet, const Json& id) {
  for (const auto& entity : packet["entities"]) if (entity["id"] == id) return entity;
  FAIL("synthetic graph entity is missing");
  return packet; // only reached if the test framework continues after FAIL
}
bool execution_graph_edge(const Json& packet, const Json& subject, const char* role, const Json& object) {
  for (const auto& claim : packet["claims"])
    if (claim["subject"] == subject && claim["predicate"] == execution_vocabulary()["predicates"][role] &&
        claim["object"] == object) return true;
  return false;
}
#endif
}

TEST_CASE("method execution disabled keeps actual legacy selector output unchanged") {
  ExecutionFixture fixture;
  fixture.request.targets = {"synthetic_entity_x", "synthetic_entity_y"};
  fixture.request.relation_hops = 1;
  const auto before = execution_must(fixture.engine->select(fixture.request));
  const auto after = fixture.select(Json{{"enabled", false}, {"profile", "disabled malformed profile intentionally unused"}});
  CHECK(after.to_json() == before.to_json());
  CHECK(fixture.transport->requests().empty());
}

#ifdef LOOM_METHOD_REGISTRY_W4
TEST_CASE("method execution actual ContextEngine winner flips with caller combination weights under one-item budget") {
  ExecutionFixture fixture;
  const auto full = fixture.select(execution_settings());
  fixture.request.budget_tokens = fixture.one_item_budget(full);
  const auto x = fixture.select(execution_settings(10, 1));
  const auto y = fixture.select(execution_settings(1, 10));
  REQUIRE(x.items.size() == 1);
  REQUIRE(y.items.size() == 1);
  CHECK(x.items[0].ref == fixture.x);
  CHECK(y.items[0].ref == fixture.y);
  CHECK(x.used_tokens <= x.budget_tokens);
  CHECK(y.used_tokens <= y.budget_tokens);
  CHECK_FALSE(x.goal.params["method_registry"]["result_graph"]["canonical_store_written"].get<bool>());
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("method execution overlay threshold is consumed before candidate admission and changes effective hashes") {
  ExecutionFixture fixture;
  auto settings = execution_settings(1, 1);
  settings["profile"]["selection"]["members"].erase(1);
  const auto before = fixture.select(settings);
  REQUIRE(before.items.size() == 2);
  settings["selection"] = Json{{"user_overrides", Json{{"min_score", 1}}}};
  const auto after = fixture.select(settings);
  REQUIRE(after.items.size() == 1);
  CHECK(after.items[0].ref == fixture.x);
  const auto& channels = after.goal.params["candidate_retrieval"]["channels"];
  REQUIRE(channels.size() == 1);
  CHECK(channels[0]["request"]["min_score"] == 1);
  CHECK(channels[0]["eligible_count"] == 1);
  CHECK(after.goal.params["method_registry"]["resolution"]["resolution_sha256"] !=
      before.goal.params["method_registry"]["resolution"]["resolution_sha256"]);
  CHECK(channels[0]["method_graph"]["manifest"]["definition_hashes"]["method_version"] !=
      before.goal.params["candidate_retrieval"]["channels"][0]["method_graph"]["manifest"]["definition_hashes"]["method_version"]);
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("method execution signed repeated methods survive graph discovery and charge each claim once") {
  ExecutionFixture fixture;
  fixture.request.targets = {"synthetic_entity_x", "synthetic_entity_y"};
  fixture.request.relation_hops = 1;
  auto settings = execution_settings(-1, 1);
  auto member = settings["profile"]["selection"]["members"][0];
  settings["profile"]["selection"]["members"] = Json::array({member, member});
  const auto full = fixture.select(settings);
  REQUIRE(full.items.size() == 2);
  std::set<std::string> refs;
  for (const auto& item : full.items) {
    CHECK(refs.insert(item.ref).second);
    CHECK(item.score < 0);
    CHECK(item.factors["method_selection"]["measured"] == (item.ref == fixture.x ? -4 : -2));
    CHECK(item.factors["authority"] == 1);
  }
  CHECK(full.items[0].ref == fixture.y);
  fixture.request.budget_tokens = fixture.one_item_budget(full);
  const auto limited = fixture.select(settings);
  REQUIRE(limited.items.size() == 1);
  CHECK(limited.items[0].ref == fixture.y);
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("method execution real result graph has result-to-run and result-to-method Claims without fabricated model binding") {
  ExecutionFixture fixture;
  const auto selected = fixture.select(execution_settings());
  const auto& graph = selected.goal.params["method_registry"]["result_graph"];
  REQUIRE(graph["packet"].is_object());
  const auto vocabulary = execution_vocabulary();
  std::set<std::string> result_ids;
  for (const auto& entity : graph["packet"]["entities"])
    if (entity["kind"] == vocabulary["kinds"]["result"]) result_ids.insert(entity["id"].get<std::string>());
  REQUIRE_FALSE(result_ids.empty());
  for (const auto& id : result_ids) {
    bool run_edge = false, method_edge = false;
    for (const auto& claim : graph["packet"]["claims"]) if (claim["subject"] == id) {
      if (claim["predicate"] == vocabulary["predicates"]["produced_in_run"]) run_edge = true;
      if (claim["predicate"] == vocabulary["predicates"]["produced_by_method_version"]) method_edge = true;
    }
    CHECK(run_edge);
    CHECK(method_edge);
    CHECK(id != fixture.x);
    CHECK(id != fixture.y);
  }
  for (const auto& [id, record] : graph["records"].items()) {
    (void)id;
    CHECK_FALSE(record["manifest"]["bindings"].contains("model_identity_id"));
    CHECK_FALSE(record["manifest"]["bindings"].contains("compiler_transform_id"));
  }
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("method execution caller graph stages produce real fusion and final selection artifacts with exact signed ordered parameters") {
  ExecutionFixture fixture;
  auto settings = execution_result_settings(-3, 1);
  const auto full = fixture.select(settings);
  fixture.request.budget_tokens = fixture.one_item_budget(full);
  const auto selected = fixture.select(settings);
  REQUIRE(selected.items.size() == 1);
  REQUIRE(selected.dropped.size() == 1);
  CHECK(selected.items[0].ref == fixture.y);
  CHECK(selected.dropped[0].ref == fixture.x);
  const auto& graph = selected.goal.params["method_registry"]["result_graph"];
  const auto& packet = graph["packet"];
  const auto& stages = graph["stages"];
  for (const auto* name : {"fusion", "selection"}) {
    const auto& stage = stages[name];
    REQUIRE(stage["status"] == "candidate");
    const auto& bindings = stage["manifest"]["bindings"];
    REQUIRE(bindings.contains("parameter_set_version_id"));
    const auto& params = execution_packet_entity(packet, bindings["parameter_set_version_id"]);
    CHECK(params["attrs"]["definition"]["effective_parameters"] == stage["parameters"]);
    CHECK(params["attrs"]["definition_sha256"] == execution_hash(params["attrs"]["definition"]));
    CHECK(execution_graph_edge(packet, bindings["method_version_id"], "uses_parameter_set", params["id"]));
    CHECK(execution_graph_edge(packet, bindings["run_id"], "uses_parameter_set", params["id"]));
    REQUIRE(stage["result_entity_ids"].size() == 3); // batch and the two actual decisions/measurements
    for (const auto& id : stage["result_entity_ids"]) {
      CHECK(id != fixture.x);
      CHECK(id != fixture.y);
      CHECK(execution_graph_edge(packet, id, "produced_in_run", bindings["run_id"]));
      CHECK(execution_graph_edge(packet, id, "produced_by_method_version", bindings["method_version_id"]));
    }
    CHECK_FALSE(bindings.contains("model_identity_id"));
    CHECK_FALSE(bindings.contains("compiler_transform_id"));
  }
  const auto& fusion = stages["fusion"];
  CHECK(fusion["parameters"]["ordered_composition"][0]["weight"] == -3);
  CHECK(fusion["parameters"]["ordered_composition"][1]["weight"] == 1);
  CHECK(fusion["parameters"]["fusion"] == (Json{{"operation", "sum"}, {"signal", "raw_score"}}));
  CHECK(execution_packet_entity(packet, fusion["measurement_nodes"][fixture.x])["attrs"]["measurement"] == -5);
  CHECK(execution_packet_entity(packet, fusion["measurement_nodes"][fixture.y])["attrs"]["measurement"] == -1);
  CHECK(fusion["complete_input_sha256"].is_string());
  const auto& selection = stages["selection"];
  CHECK(selection["complete_input_sha256"].is_null());
  CHECK(selection["captured_input_sha256"].is_string());
  CHECK(selection["parameters"]["effective_budget_tokens"] == fixture.request.budget_tokens);
  CHECK(selection["parameters"]["graph_blend_operation"] == "method_only");
  CHECK(selection["parameters"]["diversity"] == (Json{{"operation", "identity"}}));
  CHECK(execution_packet_entity(packet, selection["measurement_nodes"][fixture.y])["attrs"]["measurement"]["decision"] == "included");
  CHECK(execution_packet_entity(packet, selection["measurement_nodes"][fixture.x])["attrs"]["measurement"]["decision"] == "budget_dropped");
  const auto first = settings["profile"]["selection"]["members"][0];
  settings["profile"]["selection"]["members"][0] = settings["profile"]["selection"]["members"][1];
  settings["profile"]["selection"]["members"][1] = first;
  const auto reversed = fixture.select(settings);
  CHECK(reversed.items[0].ref == fixture.y);
  const auto& reversed_fusion = reversed.goal.params["method_registry"]["result_graph"]["stages"]["fusion"];
  CHECK(reversed_fusion["parameters"]["ordered_composition"][0]["weight"] == 1);
  CHECK(reversed_fusion["parameters"]["ordered_composition"][1]["weight"] == -3);
  CHECK(reversed_fusion["manifest"]["definition_hashes"]["method_version"] != fusion["manifest"]["definition_hashes"]["method_version"]);
  CHECK_FALSE(graph["canonical_store_written"].get<bool>());
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("method execution does not fabricate graph result stage versions when caller data are absent") {
  ExecutionFixture fixture;
  auto settings = execution_settings(10, 1);
  settings["result_methods"] = Json{{"fusion", Json::object()}};
  const auto selected = fixture.select(settings);
  REQUIRE(selected.items.size() == 2);
  CHECK(selected.items[0].ref == fixture.x);
  const auto& stages = selected.goal.params["method_registry"]["result_graph"]["stages"];
  for (const auto* name : {"fusion", "selection"}) {
    CHECK(stages[name]["status"] == "unavailable");
    CHECK(stages[name]["reason"] == "result_method_graph_version_not_configured");
    CHECK_FALSE(stages[name].contains("manifest"));
    CHECK_FALSE(stages[name].contains("result_entity_ids"));
  }
  settings = execution_result_settings(10, 1);
  settings["result_methods"]["fusion"]["parameters"] = Json{{"unconsumed_knob", 42}};
  ContextExecutionScope scope(Json{{"method_registry", settings}});
  const auto invalid = fixture.engine->select(fixture.request);
  REQUIRE_FALSE(invalid);
  CHECK(invalid.error().code == Errc::InvalidArgument);
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("method execution rejects unexecuted result-stage composition instead of silently overwriting supplied data") {
  ExecutionFixture fixture;
  auto settings = execution_result_settings(10, 1);
  settings["result_methods"]["fusion"]["fusion"] = Json{{"operation", "min"}, {"signal", "raw_score"}};
  {
    ContextExecutionScope scope(Json{{"method_registry", settings}});
    const auto rejected = fixture.engine->select(fixture.request);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    CHECK(rejected.error().message.find("additional outer fusion") != std::string::npos);
  }
  settings["result_methods"]["fusion"]["fusion"] = nullptr;
  const auto selected = fixture.select(settings);
  REQUIRE(selected.items.size() == 2);
  CHECK(selected.items[0].ref == fixture.x);
  CHECK(selected.goal.params["method_registry"]["result_graph"]["stages"]["fusion"]["status"] == "candidate");
  CHECK(fixture.transport->requests().empty());
}
#endif
