#include <doctest/doctest.h>

#include "loom/catalog.h"
#include "loom/graph_packet_store.h"
#include "loom/knowledge.h"
#include "loom/knowledge_store.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/tasks.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using test::unwrap;

namespace {
const char* payload = "SYNTHETIC_RESOURCE_OPERATOR_PAYLOAD";
std::string fixture() {
  return R"([{"id":"resource-operator","title":"Synthetic public fixture","current_node":"m1","mapping":{"m1":{"id":"m1","parent":null,"children":[],"message":{"id":"msg1","author":{"role":"user"},"content":{"content_type":"text","parts":["SYNTHETIC_RESOURCE_OPERATOR_PAYLOAD"]}}}}}])";
}
struct ResourceFixture {
  fsutil::TempDir source, data;
  std::filesystem::path path = source.path() / "conversations.json";
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  std::string unit_id;
  ResourceFixture() {
    LOOM_REQUIRE_OK(fsutil::write_file(path, fixture()));
    LOOM_REQUIRE_OK(fsutil::ensure_dir(data.path() / "profiles"));
    LOOM_REQUIRE_OK(fsutil::write_file(data.path() / "profiles/resource_read.pack",
        Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "resource_read"},
            {"overrides", Json{{"projection_storage", "transient"}, {"content_index", "none"}}}}.dump()));
    reopen();
    catalog::Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
    auto config = unwrap(catalog::ScanConfig::from_json(Json{{"sources", Json::array({path.string()})},
        {"retain_raw", "none"}}));
    unwrap(catalog.scan(config));
    const auto units = unwrap(catalog.query(catalog::UnitQuery{}));
    REQUIRE(units.size() == 1);
    unit_id = units[0].unit.id;
  }
  void reopen() {
    runtime.reset();
    RuntimeOptions options;
    options.data_dir = data.path().string(); options.start_workers = false; options.http = transport;
    runtime = unwrap(Runtime::open(options));
  }
  void overlay(const Json& changes) {
    LOOM_REQUIRE_OK(fsutil::ensure_dir(data.path() / "profiles"));
    LOOM_REQUIRE_OK(fsutil::write_file(data.path() / "profiles/resource_method.pack",
        Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "resource_method"}, {"overrides", changes}}.dump()));
  }
};
Json read_receipt(Runtime& runtime, const Json& output) {
  kb::GraphPacketStore store(runtime.db());
  return unwrap(store.execute(Json{{"operation", "read"}, {"receipt_id", output["method_receipt"]["receipt_id"]}}));
}
}

TEST_CASE("catalog resource operator records actual method provenance and restores metadata without transient payload") {
  ResourceFixture fixture;
  catalog::Catalog catalog(*fixture.runtime, unwrap(fixture.runtime->knowledge().pack()));
  const Json options{{"projection_storage", "transient"}, {"content_index", "none"}};
  auto& context_store = fixture.runtime->knowledge().store();
  const auto context_run = unwrap(context_store.begin_run("fixture.context", Json{{"synthetic", true}}));
  LOOM_REQUIRE_OK(context_store.finish_run(context_run.id, "done", Json{{"synthetic", true}}));
  auto output = unwrap(catalog.execute_resource(fixture.unit_id, options, true));
  REQUIRE(output["current"] == true);
  CHECK(output["last_successful"]["nodes"].dump().find(payload) != std::string::npos);
  CHECK_FALSE(unwrap(fixture.runtime->db().get_node("resource:" + fixture.unit_id)).has_value());
  const auto& manifest = output["method_manifest"];
  CHECK(manifest["schema"] == "loom.method_graph/1");
  CHECK(manifest["trace"]["schema"] == "loom.method_run_trace/1");
  CHECK(manifest["trace"]["availability"] == "ok");
  CHECK_FALSE(manifest["trace"].contains("response_sha256"));
  CHECK(json::canonical(manifest["trace"]["effective_parameters"]["read_options"]) == json::canonical(options));
  const auto exact_bytes = unwrap(catalog.read_unit(fixture.unit_id));
  CHECK(manifest["trace"]["measurements"]["unit_bytes"] == exact_bytes.size());
  CHECK(manifest["trace"]["instrumentation"]["read_scope"]["container_io"] == "not_instrumented");
  CHECK(manifest["trace"]["instrumentation"]["mapping_status"] == "recognized");
  CHECK(manifest["trace"]["instrumentation"]["source_index"] == output["last_successful"]["source_index"]);
  CHECK(manifest["trace"]["instrumentation"]["mapping_index_scope"] == output["last_successful"]["mapping_index_scope"]);
  CHECK(manifest["trace"]["instrumentation"]["resource"]["content_hash"] == Sha256::hex(exact_bytes));
  CHECK(manifest["trace"]["instrumentation"]["authorization"]["grants_egress"] == false);
  auto accepted = read_receipt(*fixture.runtime, output);
  CHECK(accepted["row_drift"]["matches"] == true);
  CHECK(accepted["receipt"]["packet"].dump().find(payload) == std::string::npos);
  auto& store = fixture.runtime->knowledge().store();
  const auto stored_run = unwrap(store.get_run(output["method_receipt"]["run_id"].get<std::string>()));
  REQUIRE(stored_run.has_value());
  CHECK(stored_run->summary["implicit_context_eligible"] == false);
  CHECK(stored_run->summary["graph_packet_receipt"] == output["method_receipt"]["receipt_id"]);
  CHECK(manifest["trace"]["effective_parameters"]["implicit_context_eligible"] == false);
  const auto implicit_runs = unwrap(store.list_context_runs(1, "done"));
  REQUIRE(implicit_runs.size() == 1);
  CHECK(implicit_runs[0].id == context_run.id);
  auto entity = unwrap(store.get_entity(output["method_receipt"]["run_id"].get<std::string>(),
      output["produced_by"]["result_entity_id"].get<std::string>()));
  REQUIRE(entity.has_value());
  CHECK(entity->attrs["current"] == true);
  CHECK(manifest["trace"]["instrumentation"]["result_metadata_sha256"] == Sha256::hex(json::canonical(entity->attrs)));
  CHECK(manifest["trace"]["instrumentation"]["result_entity_id"] == entity->id);
  bool version_edge = false, run_edge = false;
  const auto& predicates = manifest["vocabulary"]["predicates"];
  for (const auto& claim : accepted["receipt"]["packet"]["claims"]) {
    if (claim["subject"] != output["produced_by"]["result_entity_id"]) continue;
    version_edge |= claim["predicate"] == predicates["produced_by_method_version"] && claim["object"] == output["produced_by"]["method_version_id"];
    run_edge |= claim["predicate"] == predicates["produced_in_run"] && claim["object"] == output["produced_by"]["run_id"];
  }
  CHECK(version_edge);
  CHECK(run_edge);
  const auto historical = accepted["receipt"].dump();
  fixture.reopen();
  CHECK(read_receipt(*fixture.runtime, output)["receipt"].dump() == historical);
  catalog::Catalog restored(*fixture.runtime, unwrap(fixture.runtime->knowledge().pack()));
  const auto repeated = unwrap(restored.execute_resource(fixture.unit_id, options, true));
  CHECK(repeated["last_successful"]["nodes"] == output["last_successful"]["nodes"]);
  CHECK(repeated["produced_by"]["run_id"] != output["produced_by"]["run_id"]);
  CHECK(read_receipt(*fixture.runtime, output)["receipt"].dump() == historical);
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("catalog resource grant and executor availability are independent of source and descriptor data") {
  ResourceFixture fixture;
  catalog::Catalog catalog(*fixture.runtime, unwrap(fixture.runtime->knowledge().pack()));
  const auto before = unwrap(catalog.status());
  const auto denied = catalog.execute_resource("nonexistent-unit", Json::object(), false);
  REQUIRE_FALSE(denied);
  CHECK(denied.error().code == Errc::Auth);
  CHECK(unwrap(catalog.status()) == before);
  CHECK_FALSE(fixture.runtime->db().conn().has_table("loom_kb_graph_receipts"));

  auto values = unwrap(RuntimeProfile::builtin("resource_method")).values();
  auto& profile = values["profile"];
  auto& version = profile["entities"][1];
  auto& definition = version["attrs"]["definition"];
  definition["execution_capability"] = "fixture.missing_executor";
  // A capability declaration cannot manufacture host code or read permission.
  definition["available"] = true;
  definition["read_authorized"] = true;
  const auto bytes = json::canonical(definition), sha = Sha256::hex(bytes);
  const std::string version_id = "fixture.version:" + sha;
  version["id"] = version["canonical_key"] = version_id;
  version["attrs"]["definition_sha256"] = sha;
  auto& source = profile["sources"][0];
  auto& observation = source["observation"];
  observation["id"] = "fixture.source:" + sha;
  observation["text"] = bytes;
  observation["locator"]["source"] = "sha256:" + sha;
  observation["locator"]["byte_len"] = bytes.size();
  source["text_sha256"] = sha;
  auto& claim = profile["claims"][0];
  claim["subject"] = version_id;
  claim["id"] = "fixture.claim:" + sha;
  auto& support = claim["assessment"]["basis"]["support"][0];
  support["observation"] = observation["id"];
  support["locator"] = observation["locator"];
  support["quote"] = bytes;
  profile["selection"]["members"][0]["method_version_id"] = version_id;
  fixture.overlay(values);
  std::filesystem::rename(fixture.path, fixture.path.string() + ".away");
  const auto absent = catalog.execute_resource(fixture.unit_id, Json::object(), true);
  REQUIRE_FALSE(absent);
  CHECK(absent.error().code == Errc::Unavailable);
  CHECK(absent.error().message.find("executor") != std::string::npos);
  CHECK_FALSE(fixture.runtime->db().conn().has_table("loom_kb_graph_receipts"));
  CHECK_FALSE(unwrap(fixture.runtime->db().get_node("resource:" + fixture.unit_id)).has_value());
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("catalog resource method data changes output vocabulary and keeps failed refresh evidence distinct") {
  ResourceFixture fixture;
  fixture.overlay(Json{{"result_kind", "fixture:resource_result"}, {"implicit_context_eligible", true}});
  catalog::Catalog catalog(*fixture.runtime, unwrap(fixture.runtime->knowledge().pack()));
  CHECK(unwrap(RuntimeProfile::builtin("resource_read")).values()["projection_storage"] == "snapshot");
  const Json snapshot_options{{"projection_storage", "snapshot"}};
  const auto output = unwrap(catalog.execute_resource(fixture.unit_id, snapshot_options, true));
  REQUIRE(output["current"] == true);
  CHECK(output["last_successful"]["read_configuration"]["values"]["projection_storage"] == "snapshot");
  REQUIRE(unwrap(fixture.runtime->db().get_node("resource:" + fixture.unit_id)).has_value());
  auto result = unwrap(fixture.runtime->knowledge().store().get_entity(output["method_receipt"]["run_id"].get<std::string>(),
      output["produced_by"]["result_entity_id"].get<std::string>()));
  REQUIRE(result.has_value());
  CHECK(result->kind == "fixture:resource_result");
  const auto selected = unwrap(fixture.runtime->knowledge().store().list_context_runs(1, "done"));
  REQUIRE(selected.size() == 1);
  CHECK(selected[0].id == output["method_receipt"]["run_id"].get<std::string>());
  CHECK(output["method_manifest"]["trace"]["effective_parameters"]["implicit_context_eligible"] == true);
  const auto historical = read_receipt(*fixture.runtime, output)["receipt"].dump();
  std::filesystem::rename(fixture.path, fixture.path.string() + ".away");
  const auto missing = unwrap(catalog.execute_resource(fixture.unit_id, snapshot_options, true));
  CHECK(missing["status"] == "unavailable");
  CHECK(missing["current"] == false);
  CHECK(missing["method_manifest"]["trace"]["availability"] == "unavailable");
  CHECK(missing["last_successful"]["nodes"] == output["last_successful"]["nodes"]);
  const auto& attempt = missing["method_manifest"]["trace"]["instrumentation"];
  CHECK(attempt["current"] == false);
  CHECK(attempt["mapping_status"] == "not_attempted");
  CHECK(attempt["read_scope"].is_null());
  CHECK(attempt["validation"]["unit_content_hash_verified"] == false);
  CHECK(read_receipt(*fixture.runtime, missing)["receipt"]["packet"].dump().find(payload) == std::string::npos);
  CHECK(read_receipt(*fixture.runtime, output)["receipt"].dump() == historical);
  std::filesystem::rename(fixture.path.string() + ".away", fixture.path);
  const auto resumed = unwrap(catalog.execute_resource(fixture.unit_id, snapshot_options, true));
  CHECK(resumed["current"] == true);
  CHECK(read_receipt(*fixture.runtime, output)["receipt"].dump() == historical);
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("catalog resource task resumes and persists only a reference under transient policy") {
  ResourceFixture fixture;
  const Json read_options{{"projection_storage", "transient"}, {"content_index", "none"}};
  SubmitOptions attempts;
  attempts.max_attempts = 1;
  const auto denied_id = unwrap(fixture.runtime->tasks().submit("catalog.read_resource",
      Json{{"unit_id", fixture.unit_id}, {"read_options", read_options}, {"read_authorized", false}}, attempts));
  const auto denied = unwrap(fixture.runtime->tasks().run_sync(denied_id));
  CHECK(denied.status == "failed");
  CHECK_FALSE(denied.result.has_value());
  CHECK_FALSE(fixture.runtime->db().conn().has_table("loom_kb_graph_receipts"));
  const auto id = unwrap(fixture.runtime->tasks().submit("catalog.read_resource",
      Json{{"unit_id", fixture.unit_id}, {"read_options", read_options}, {"read_authorized", true}}));
  LOOM_REQUIRE_OK(fixture.runtime->tasks().pause(id));
  CHECK(unwrap(fixture.runtime->tasks().get(id))->status == "paused");
  fixture.reopen();
  REQUIRE(unwrap(fixture.runtime->tasks().get(id))->status == "paused");
  LOOM_REQUIRE_OK(fixture.runtime->tasks().resume(id));
  const auto completed = unwrap(fixture.runtime->tasks().run_sync(id));
  REQUIRE(completed.status == "done");
  REQUIRE(completed.result.has_value());
  CHECK((*completed.result)["payload_status"] == "reference_only");
  CHECK((*completed.result)["resource_ref"]["unit_id"] == fixture.unit_id);
  CHECK_FALSE(completed.result->contains("last_successful"));
  CHECK(completed.to_json().dump().find(payload) == std::string::npos);
  const auto receipt = read_receipt(*fixture.runtime, *completed.result);
  CHECK(receipt["row_drift"]["matches"] == true);
  CHECK(receipt["receipt"]["packet"].dump().find(payload) == std::string::npos);
  fixture.reopen();
  const auto restored = unwrap(fixture.runtime->tasks().get(id));
  REQUIRE(restored.has_value());
  CHECK(restored->to_json() == completed.to_json());
  CHECK(read_receipt(*fixture.runtime, *restored->result)["receipt"] == receipt["receipt"]);
  CHECK(fixture.transport->requests().empty());
  fixture.runtime.reset();
  // Inspect the actual runtime directory, including SQLite/WAL, stored queue,
  // logs and any temporary files retained under its data root after shutdown.
  for (const auto& entry : std::filesystem::recursive_directory_iterator(fixture.data.path())) {
    if (!entry.is_regular_file()) continue;
    INFO(entry.path().string());
    CHECK(unwrap(fsutil::read_file(entry.path())).find(payload) == std::string::npos);
  }
}

TEST_CASE("catalog snapshot projection and method receipt roll back together on store failure") {
  ResourceFixture fixture;
  catalog::Catalog catalog(*fixture.runtime, unwrap(fixture.runtime->knowledge().pack()));
  const auto prior = unwrap(catalog.execute_resource(fixture.unit_id, Json::object(), true));
  CHECK_FALSE(unwrap(fixture.runtime->db().get_node("resource:" + fixture.unit_id)).has_value());
  const auto historical = read_receipt(*fixture.runtime, prior)["receipt"].dump();
  const auto receipts_before = unwrap(fixture.runtime->db().conn().query_int("SELECT COUNT(*) FROM loom_kb_graph_receipts"));
  LOOM_REQUIRE_OK(fixture.runtime->db().conn().exec(
      "CREATE TRIGGER synthetic_resource_receipt_failure BEFORE INSERT ON loom_kb_graph_receipts "
      "BEGIN SELECT RAISE(ABORT, 'synthetic resource receipt storage failure'); END"));
  const auto rejected = catalog.execute_resource(fixture.unit_id, Json{{"projection_storage", "snapshot"}}, true);
  REQUIRE_FALSE(rejected);
  // SQLite RAISE(ABORT) is SQLITE_CONSTRAINT_TRIGGER (1811), whose primary
  // code SQLITE_CONSTRAINT maps to Conflict in the existing SQLite adapter.
  CHECK(rejected.error().code == Errc::Conflict);
  CHECK(rejected.error().message == "step: synthetic resource receipt storage failure");
  CHECK_FALSE(unwrap(fixture.runtime->db().get_node("resource:" + fixture.unit_id)).has_value());
  CHECK(unwrap(fixture.runtime->db().conn().query_int("SELECT COUNT(*) FROM loom_kb_graph_receipts")) == receipts_before);
  CHECK(read_receipt(*fixture.runtime, prior)["receipt"].dump() == historical);
  LOOM_REQUIRE_OK(fixture.runtime->db().conn().exec("DROP TRIGGER synthetic_resource_receipt_failure"));
  const auto resumed = unwrap(catalog.execute_resource(fixture.unit_id, Json{{"projection_storage", "snapshot"}}, true));
  CHECK(resumed["current"] == true);
  REQUIRE(unwrap(fixture.runtime->db().get_node("resource:" + fixture.unit_id)).has_value());
  CHECK(read_receipt(*fixture.runtime, prior)["receipt"].dump() == historical);
}
