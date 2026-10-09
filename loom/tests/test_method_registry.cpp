#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "context/method_registry.h"
#include "loom/graph_packet_store.h"
#include "loom/model.h"
#include "loom/util/sha256.h"
#include "packet/packet.h"
#include "test_helpers.h"
#include "util/json_value.h"

using namespace loom;

namespace {
const char* known = "2026-10-09T00:00:00Z";
Json origin() {
  return Json{{"kind", "user"}, {"actor", "synthetic-method-test"}, {"model", nullptr},
      {"recipe_sha256", nullptr}, {"response_sha256", nullptr}};
}
Json profile() {
  Json kinds = Json::object(), predicates = Json::object();
  for (const char* role : {"method", "method_version", "parameter_set_version", "run"})
    kinds[role] = std::string("fixture.") + role;
  for (const char* role : {"version_of", "uses_parameter_set", "requests_method_version",
                          "produced_in_run", "produced_by_method_version"})
    predicates[role] = std::string("fixture.") + role;
  Json definition{{"execution_capability", "fixture.local"},
      {"parameters", Json{{"nested", Json{{"z", 1}, {"a", 2}}}, {"ordered", Json::array({1, 2})}}}};
  model::Entity method;
  method.id = "fixture.method";
  method.canonical_key = method.id;
  method.label = "Synthetic method";
  method.kind = kinds["method"];
  method.evidence = model::EvidenceClass::User;
  method.origin = model::Origin::User;
  auto version = method;
  version.id = "fixture.version";
  version.canonical_key = version.id;
  version.kind = kinds["method_version"];
  version.attrs = Json{{"definition", definition}, {"definition_sha256", Sha256::hex(json::canonical(definition))}};
  model::Observation observation;
  observation.id = "fixture.source";
  observation.unit = "fixture.unit";
  observation.kind = model::ObservationKind::Field;
  observation.text = json::canonical(definition);
  observation.locator.source = "sha256:" + Sha256::hex(observation.text);
  model::Claim relation;
  relation.id = "fixture.relation";
  relation.subject = version.id;
  relation.predicate = predicates["version_of"];
  relation.object = method.id;
  relation.assessment.evidence = model::EvidenceClass::User;
  relation.assessment.origin = model::Origin::User;
  relation.assessment.confidence = 1;
  relation.assessment.support.push_back(model::Support{observation.id, observation.locator,
      observation.text, "fixture", 1});
  return Json{{"vocabulary", Json{{"kinds", kinds}, {"predicates", predicates}}},
      {"entities", Json::array({method.to_json(), version.to_json()})},
      {"claims", Json::array({relation.to_json()})},
      {"sources", Json::array({Json{{"observation", observation.to_json()}, {"known_at", known},
                                  {"text_sha256", Sha256::hex(observation.text)}}})},
      {"selection", Json{{"members", Json::array({Json{{"method_version_id", version.id}}})},
                          {"parameter_layers", Json::array({"method", "member", "selection", "user"})}}}};
}
Json sorted(const Json& value) { return test::unwrap(json::parse(json::canonical(value))); }
Json acceptance(const Json& data) {
  auto packet = test::unwrap(packet::execute(Json{{"operation", "make"}, {"origin", origin()},
      {"known_at", known}, {"entities", data["entities"]}, {"claims", data["claims"]}, {"sources", data["sources"]}}));
  REQUIRE_FALSE(packet.contains("error"));
  Json selection = Json::object(), expected = Json::object();
  for (const char* collection : {"entities", "claims", "sources"}) {
    selection[collection] = Json::array();
    expected[collection] = Json::object();
    for (const auto& row : packet[collection]) {
      const auto id = std::string_view(collection) == "sources" ? row["observation"]["id"] : row["id"];
      selection[collection].push_back(id);
      expected[collection][id.get<std::string>()] = nullptr;
    }
  }
  return Json{{"operation", "accept"}, {"packet", packet}, {"target", "synthetic-method-test"},
      {"selection", selection}, {"expected_rows", expected}, {"explicitly_accepted", true}};
}
}

TEST_CASE("MethodRegistry accepts equivalent object ordering without rewriting source identity") {
  fsutil::TempDir temp;
  auto db = test::open_db(temp.path() / "methods.db");
  context::MethodRegistry registry(*db);
  const auto data = profile();
  const auto baseline = test::unwrap(registry.load(data));
  for (const char* collection : {"entities", "claims", "sources"}) {
    auto permutation = data;
    permutation[collection] = sorted(data[collection]);
    const auto loaded = test::unwrap(registry.load(permutation));
    CHECK(loaded["snapshot_sha256"] == baseline["snapshot_sha256"]);
    CHECK(loaded["profile"].dump() == permutation.dump());
  }
  auto permutation = sorted(data);
  auto snapshot = test::unwrap(registry.load(permutation));
  CHECK(snapshot["snapshot_sha256"] == baseline["snapshot_sha256"]);
  // Duplicated equivalent records must not turn object ordering into a conflict.
  for (const char* collection : {"entities", "claims", "sources"})
    permutation[collection].push_back(data[collection][0]);
  LOOM_REQUIRE_OK(registry.load(permutation));
}

TEST_CASE("MethodRegistry and GraphPacketStore agree after accepted DTO restoration") {
  fsutil::TempDir temp;
  auto db = test::open_db(temp.path() / "methods.db");
  auto data = sorted(profile());
  data["entities"][0]["confidence"] = 1;  // Exact native 1 -> 1.0 is lossless.
  data["entities"][0]["attrs"]["exact"] = Json::array({UINT64_MAX, INT64_MIN, UINT64_C(9007199254740993)});
  const auto request = acceptance(data);
  std::string receipt_id;
  {
    kb::GraphPacketStore store(*db);
    const auto accepted = test::unwrap(store.execute(request));
    receipt_id = accepted["receipt"]["id"];
    CHECK(accepted["receipt"]["packet"].dump() == request["packet"].dump());
    context::MethodRegistry registry(*db);
    const auto snapshot = test::unwrap(registry.load(data, {receipt_id}));
    CHECK(snapshot["entities"]["fixture.method"]["attrs"]["exact"] == data["entities"][0]["attrs"]["exact"]);
    const auto resolution = test::unwrap(registry.resolve(snapshot, Json::object(), Json::object()));
    REQUIRE(resolution["leaves"].size() == 1);
    CHECK(resolution["leaves"][0]["available"] == false);
    CHECK_FALSE(resolution["leaves"][0]["unavailable_reasons"].empty());
    int dispatches = 0;
    auto denied = registry.prepare(snapshot, resolution["leaves"][0], Json::object(),
        [&](const Json& command) { ++dispatches; return packet::execute(command); });
    REQUIRE_FALSE(denied);
    CHECK(denied.error().code == Errc::Unavailable);
    CHECK(dispatches == 0);
  }
  db.reset();
  db = test::open_db(temp.path() / "methods.db");
  kb::GraphPacketStore store(*db);
  const auto read = test::unwrap(store.execute(Json{{"operation", "read"}, {"receipt_id", receipt_id}}));
  CHECK(read["row_drift"]["matches"] == true);
  CHECK(read["receipt"]["packet"].dump() == request["packet"].dump());
  context::MethodRegistry restored(*db);
  LOOM_REQUIRE_OK(restored.load(data, {receipt_id}));
}

TEST_CASE("MethodRegistry rejects lossy DTOs and changed identities after order normalization") {
  fsutil::TempDir temp;
  auto db = test::open_db(temp.path() / "methods.db");
  context::MethodRegistry registry(*db);
  const auto data = sorted(profile());
  for (const char* collection : {"entities", "claims", "sources"}) {
    auto changed = data;
    auto& native = std::string_view(collection) == "sources" ? changed[collection][0]["observation"] : changed[collection][0];
    native["unknown_field"] = "must not disappear";
    CHECK_FALSE(registry.load(changed));
  }
  for (const auto& number : {Json(UINT64_C(9007199254740993)), Json(INT64_C(-9007199254740993)), Json(UINT64_MAX)}) {
    auto changed = data;
    changed["sources"][0]["observation"]["locator"]["time_start"] = number;
    CHECK_FALSE(registry.load(changed));
  }
  for (const auto ordinal : {UINT64_C(2147483648), UINT64_C(9223372036854775808), UINT64_MAX}) {
    auto changed = data;
    changed["sources"][0]["observation"]["ordinal"] = ordinal;
    CHECK_FALSE(registry.load(changed));
  }
  auto changed = data;
  auto duplicate = data["entities"][0];
  duplicate["attrs"]["sequence"] = Json::array({"first", "second"});
  changed["entities"][0] = duplicate;
  duplicate["attrs"]["sequence"] = Json::array({"second", "first"});
  changed["entities"].push_back(duplicate);
  CHECK_FALSE(registry.load(changed));
  changed = data;
  changed["sources"][0]["text_sha256"] = std::string(64, '0');
  CHECK_FALSE(registry.load(changed));
  changed = data;
  changed["entities"][1]["attrs"]["definition"]["parameters"]["ordered"] = Json::array({2, 1});
  CHECK_FALSE(registry.load(changed));
}

TEST_CASE("Native DTO value comparison preserves exact numbers and scalar and array distinctions") {
  using json::equivalent;
  CHECK(equivalent(Json{{"b", 1}, {"a", Json::array({Json{{"z", 2}, {"a", 3}}})}},
                   Json{{"a", Json::array({Json{{"a", 3}, {"z", 2}}})}, {"b", 1.0}}));
  CHECK(equivalent(Json(INT64_MAX), Json(INT64_MAX)));
  CHECK(equivalent(Json(UINT64_MAX), Json(UINT64_MAX)));
  CHECK(equivalent(Json(INT64_MIN), Json(-std::ldexp(1.0, 63))));
  CHECK(equivalent(Json(0), Json(-0.0)));
  CHECK_FALSE(equivalent(Json(UINT64_MAX), Json(std::ldexp(1.0, 64))));
  CHECK_FALSE(equivalent(Json(INT64_MAX), Json(std::ldexp(1.0, 63))));
  CHECK_FALSE(equivalent(Json(UINT64_MAX), Json(-1)));
  CHECK_FALSE(equivalent(Json(UINT64_C(9007199254740993)), Json(9007199254740992.0)));
  CHECK_FALSE(equivalent(Json(INT64_C(-9007199254740993)), Json(-9007199254740992.0)));
  CHECK_FALSE(equivalent(Json(1), Json(1.5)));
  CHECK_FALSE(equivalent(Json(1), Json(true)));
  CHECK_FALSE(equivalent(Json(1), Json("1")));
  CHECK_FALSE(equivalent(Json::array({1, 2}), Json::array({2, 1})));
  CHECK_FALSE(equivalent(Json{{"a", 1}}, Json{{"a", 1}, {"b", nullptr}}));
  CHECK_FALSE(equivalent(Json(0), Json(std::numeric_limits<double>::infinity())));
}

TEST_CASE("MethodRegistry prepared parameter and result bindings ignore only object order") {
  fsutil::TempDir temp;
  auto db = test::open_db(temp.path() / "methods.db");
  context::MethodRegistry registry(*db);
  const auto snapshot = test::unwrap(registry.load(sorted(profile())));
  const auto resolution = test::unwrap(registry.resolve(snapshot, Json::object(),
      Json{{"execution", Json{{"fixture.local", Json{{"available", true}}}}}}));
  REQUIRE(resolution["leaves"][0]["available"] == true);
  const auto prepared = test::unwrap(registry.prepare(snapshot, resolution["leaves"][0],
      Json{{"run_id", "fixture.run"}, {"origin", origin()}, {"known_at", known}}, packet::execute));
  auto manifest = prepared["manifest"];
  manifest["trace"]["effective_parameters"] = sorted(manifest["trace"]["effective_parameters"]);
  manifest["definition_records"] = sorted(manifest["definition_records"]);
  const Json bindings{{"origin", origin()}, {"known_at", known},
      {"result_entity_ids", Json::array({"fixture.method"})}};
  const auto bound = test::unwrap(registry.bind_results(prepared["packet"], manifest, bindings, packet::execute));
  CHECK(bound["manifest"]["trace"]["result_bindings"][0]["result_entity_id"] == "fixture.method");
  auto changed = manifest;
  changed["trace"]["effective_parameters"]["ordered"] = Json::array({2, 1});
  CHECK_FALSE(registry.bind_results(prepared["packet"], changed, bindings, packet::execute));
  changed = manifest;
  changed["definition_records"]["method_version_id"]["unknown_field"] = true;
  CHECK_FALSE(registry.bind_results(prepared["packet"], changed, bindings, packet::execute));
  auto unavailable = bindings;
  unavailable["availability"] = "unavailable";
  unavailable["input_sha256"] = nullptr;
  const auto failed = test::unwrap(registry.bind_results(prepared["packet"], manifest, unavailable, packet::execute));
  CHECK(failed["manifest"]["trace"]["availability"] == "unavailable");
  unavailable["input_sha256"] = std::string(64, '0');
  CHECK_FALSE(registry.bind_results(prepared["packet"], manifest, unavailable, packet::execute));
  unavailable["input_sha256"] = nullptr;
  unavailable["availability"] = false;
  CHECK_FALSE(registry.bind_results(prepared["packet"], manifest, unavailable, packet::execute));

}
