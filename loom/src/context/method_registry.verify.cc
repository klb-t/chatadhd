// Source-private, offline native graph regressions. No production test glob.
#include <doctest/doctest.h>

#include "method_registry.h"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>

#include "loom/db.h"
#include "loom/graph_packet_store.h"
#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

#ifdef LOOM_METHOD_REGISTRY_W4
#include "packet.h"
#endif

namespace {
using namespace loom;
template<class T> T must(Result<T> r) {
  if (!r) FAIL("native fixture/API failure: " << r.error().to_string());
  return std::move(r).value();
}
std::string h(const Json& j) { return Sha256::hex(json::canonical(j)); }
const char* known = "2026-10-04T14:00:00Z";
Json origin() { return Json{{"kind", "user"}, {"actor", "synthetic-owner"}, {"model", nullptr}, {"recipe_sha256", nullptr}, {"response_sha256", nullptr}}; }
Json vocab() {
  Json kinds = Json::object(), predicates = Json::object();
  for (const char* r : {"method", "method_version", "parameter_set_version", "prompt_version", "recipe_version", "preset_version", "combination_version", "run", "model_identity", "compiler_transform"})
    kinds[r] = "synthetic.kind/" + std::string(r);
  for (const char* r : {"version_of", "uses_parameter_set", "uses_combination", "uses_recipe", "uses_prompt", "uses_preset", "includes_method", "requests_method_version", "produced_in_run", "produced_by_method_version", "projected_by_compiler"})
    predicates[r] = "synthetic.relation/" + std::string(r);
  return Json{{"kinds", kinds}, {"predicates", predicates}};
}
Json entity(const std::string& id, const char* role, Json attrs = Json::object()) {
  model::Entity e;
  e.id = id; e.canonical_key = id; e.label = id; e.kind = vocab()["kinds"][role];
  e.evidence = model::EvidenceClass::User; e.origin = model::Origin::User;
  e.attrs = std::move(attrs); e.first_seen = known; e.last_seen = known;
  return e.to_json();
}
Json version(const std::string& id, const char* role, const Json& d) {
  return entity(id, role, Json{{"definition", d}, {"definition_sha256", h(d)}});
}
Json observation(const std::string& id, const std::string& bytes) {
  model::Observation o;
  o.id = id; o.unit = "synthetic_unit"; o.kind = model::ObservationKind::Field; o.text = bytes;
  o.locator.source = "sha256:" + Sha256::hex(bytes); o.locator.byte_start = 0; o.locator.byte_len = bytes.size();
  return Json{{"observation", o.to_json()}, {"known_at", known}, {"text_sha256", Sha256::hex(bytes)}};
}
Json claim(const std::string& a, const char* r, const std::string& b, const Json& source) {
  const auto o = must(model::Observation::from_json(source["observation"]));
  model::Claim c;
  c.subject = a; c.predicate = vocab()["predicates"][r]; c.object = b;
  c.qualifiers.extra = Json{{"confidence_scope", "structure_only"}, {"content_truth", "not_established"}};
  c.assessment.evidence = model::EvidenceClass::Derived; c.assessment.origin = model::Origin::System;
  c.assessment.confidence = 1;
  c.assessment.derivation = model::Derivation{"synthetic.native_projection", 1, "", 0};
  c.assessment.support.push_back(model::Support{o.id, o.locator, o.text, "synthetic.native_projection", 1});
  c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  REQUIRE(c.validate());
  return c.to_json();
}
Json lexical_profile() {
  const Json d{{"method_key", "synthetic/arbitrary-method"}, {"execution_capability", "synthetic/native"},
      {"parameters", Json{{"threshold", 0.7}, {"nested", Json{{"method", true}}}}}};
  const auto source = observation("synthetic_source", json::canonical(d));
  return Json{{"vocabulary", vocab()},
      {"entities", Json::array({entity("synthetic_method", "method"), version("synthetic_version", "method_version", d)})},
      {"claims", Json::array({claim("synthetic_version", "version_of", "synthetic_method", source)})},
      {"sources", Json::array({source})},
      {"selection", Json{{"members", Json::array({Json{{"method_version_id", "synthetic_version"}}})},
          {"parameter_layers", Json::array({"method", "member", "selection", "user"})}}}};
}
Json capabilities() {
  return Json{{"execution", Json{{"synthetic/native", Json{{"available", true}, {"implementation", "offline synthetic executor"}}}}},
      {"fusion", Json{{"synthetic/sum", Json{{"available", true}}}, {"ordered_single_member", Json{{"available", true}}}}}};
}
void add_combination(Json& p, const std::string& id, const Json& members, Json parameters = Json::object()) {
  Json d{{"members", members}, {"parameters", parameters}, {"fusion", Json{{"operation", "synthetic/sum"}}}};
  p["entities"].push_back(version(id, "combination_version", d));
  auto source = observation("source_" + id, json::canonical(d));
  p["sources"].push_back(source);
  for (const auto& m : members) {
    const auto target = m.contains("method_version_id") ? m["method_version_id"] : m["combination_version_id"];
    const auto c = claim(id, "includes_method", target.get<std::string>(), source);
    bool found = false;
    for (const auto& existing : p["claims"]) if (existing["id"] == c["id"]) found = true;
    if (!found) p["claims"].push_back(c);
  }
}
// Test-only packet head fixture for the main native store. Production adapter
// always delegates codec/history construction to the injected W4 operation.
Json head_fixture(const Json& profile) {
  Json p{{"schema", "loom.graph_packet/1"}, {"definitions", Json::array()}, {"entities", profile["entities"]},
      {"claims", profile["claims"]}, {"sources", profile["sources"]}, {"task", Json::object()},
      {"provenance", Json::object()}, {"history", Json::array()}};
  for (const char* col : {"definitions", "entities", "claims", "sources"}) {
    p["provenance"][col] = Json::object();
    for (const auto& row : p[col]) {
      const auto id = std::string_view(col) == "sources" ? row["observation"]["id"] : row["id"];
      p["provenance"][col][id.get<std::string>()] = Json{{"known_at", known}, {"origin", origin()}, {"record_sha256", h(row)}};
    }
  }
  p["packet_id"] = h(p);
  return p;
}
Json acceptance(const Json& packet, const std::string& target) {
  Json selection = Json::object(), expected = Json::object();
  for (const char* col : {"entities", "claims", "sources"}) {
    selection[col] = Json::array(); expected[col] = Json::object();
    for (const auto& row : packet[col]) {
      const auto id = std::string_view(col) == "sources" ? row["observation"]["id"] : row["id"];
      selection[col].push_back(id); expected[col][id.get<std::string>()] = nullptr;
    }
  }
  return Json{{"operation", "accept"}, {"target", target}, {"packet", packet}, {"selection", selection},
      {"expected_rows", expected}, {"explicitly_accepted", true}};
}
struct Fixture {
  fsutil::TempDir dir;
  std::unique_ptr<Database> db = must(Database::open(dir.path() / "synthetic.sqlite"));
  context::MethodRegistry registry{*db};
};
Json first_leaf(Fixture& f, const Json& p, Json overrides = Json::object(), Json caps = capabilities()) {
  auto s = must(f.registry.load(p));
  return must(f.registry.resolve(s, overrides, caps))["leaves"][0];
}
}

TEST_CASE("method registry requires caller data and reports unsupported execution without fake metadata") {
  Fixture f;
  auto missing = f.registry.load(Json::object());
  REQUIRE_FALSE(missing); CHECK(missing.error().code == Errc::Unavailable);
  auto p = lexical_profile();
  p.erase("selection");
  auto snapshot = must(f.registry.load(p));
  auto no_selection = f.registry.resolve(snapshot, Json::object(), capabilities());
  REQUIRE_FALSE(no_selection); CHECK(no_selection.error().code == Errc::Unavailable);
  auto leaf = first_leaf(f, lexical_profile(), Json::object(), Json::object());
  CHECK_FALSE(leaf["available"].get<bool>());
  CHECK(leaf["execution_capability"] == "synthetic/native");
  CHECK(leaf["recipe"].is_null()); CHECK(leaf["prompt"].is_null()); CHECK(leaf["preset"].is_null());
  CHECK(leaf["unavailable_reasons"].size() == 1);
  CHECK_FALSE(f.db->conn().has_table("loom_kb_runs"));
  auto p_with_fusion = lexical_profile();
  p_with_fusion["selection"]["fusion"] = Json{{"operation", "unsupported/inherited"}};
  auto original = first_leaf(f, p_with_fusion);
  CHECK_FALSE(original["available"].get<bool>());
  auto cleared = first_leaf(f, p_with_fusion, Json{{"fusion", nullptr}});
  CHECK(cleared["available"].get<bool>()); CHECK(cleared["fusions"].empty());
}

TEST_CASE("method registry retains arbitrary signed weights repeated paths and caller parameter ordering") {
  Fixture f;
  auto p = lexical_profile();
  p["selection"]["parameters"] = Json{{"threshold", 0.2}, {"nested", Json{{"selection", true}}}};
  p["selection"]["user_overrides"] = Json{{"threshold", -0.4}};
  p["selection"]["members"] = Json::array();
  for (int n = 0; n < 65; ++n) p["selection"]["members"].push_back(Json{{"method_version_id", "synthetic_version"}, {"weight", -2.5}, {"parameters", Json{{"threshold", 0.3}}}});
  const auto s = must(f.registry.load(p));
  const auto result = must(f.registry.resolve(s, Json::object(), capabilities()));
  REQUIRE(result["leaves"].size() == 65);
  CHECK(result["leaves"][0]["weight"] == -2.5);
  CHECK(result["leaves"][0]["effective_parameters"]["threshold"] == -0.4);
  CHECK(result["leaves"][0]["effective_parameters"]["nested"] == Json{{"method", true}, {"selection", true}});
  CHECK(result["leaves"][64]["path"][0]["member_index"] == 64);
  auto reversed = must(f.registry.resolve(s, Json{{"parameter_layers", Json::array({"user", "selection", "member", "method"})}}, capabilities()));
  CHECK(reversed["leaves"][0]["effective_parameters"]["threshold"] == 0.7);
  auto broken = f.registry.resolve(s, Json{{"parameter_layers", Json::array({"missing_data_layer"})}}, capabilities());
  REQUIRE_FALSE(broken); CHECK(broken.error().code == Errc::Unavailable);
  auto bad_weight = p;
  bad_weight["selection"]["members"][0]["weight"] = std::numeric_limits<double>::infinity();
  CHECK_FALSE(f.registry.load(bad_weight));
}

TEST_CASE("method registry expands native combination DAGs and detects cycles and missing edges") {
  Fixture f;
  auto p = lexical_profile();
  const auto child = Json{{"method_version_id", "synthetic_version"}, {"weight", -3.0}};
  add_combination(p, "combo_inner", Json::array({child, child}), Json{{"threshold", 0.1}});
  const auto inner = Json{{"combination_version_id", "combo_inner"}, {"weight", 2.0}};
  add_combination(p, "combo_outer", Json::array({inner, inner}), Json{{"threshold", 0.9}});
  p["selection"]["members"] = Json::array({Json{{"combination_version_id", "combo_outer"}, {"weight", 4.0}}});
  p["selection"]["parameter_layers"] = Json::array({"method", "combination"});
  const auto s = must(f.registry.load(p));
  const auto r = must(f.registry.resolve(s, Json::object(), capabilities()));
  REQUIRE(r["leaves"].size() == 4);
  CHECK(r["leaves"][0]["weight"] == -24.0);
  CHECK(r["leaves"][0]["effective_parameters"]["threshold"] == 0.1);
  CHECK(r["leaves"][0]["path"][1]["member_index"] == 0);
  CHECK(r["leaves"][2]["path"][1]["member_index"] == 1);
  CHECK(r["leaves"][0]["fusions"].size() == 2);
  const auto unsupported = must(f.registry.resolve(s, Json::object(), Json{{"execution", capabilities()["execution"]}}));
  CHECK_FALSE(unsupported["leaves"][0]["available"].get<bool>());
  auto missing_edge = p; missing_edge["claims"].erase(missing_edge["claims"].end() - 1);
  CHECK_FALSE(f.registry.resolve(must(f.registry.load(missing_edge)), Json::object(), capabilities()));
  auto cyclic = lexical_profile();
  add_combination(cyclic, "cycle", Json::array({Json{{"combination_version_id", "cycle"}}}));
  cyclic["selection"]["members"] = Json::array({Json{{"combination_version_id", "cycle"}}});
  auto cycle = f.registry.resolve(must(f.registry.load(cyclic)), Json::object(), capabilities());
  REQUIRE_FALSE(cycle); CHECK(cycle.error().message.find("cyclic") != std::string::npos);
}

TEST_CASE("method registry verifies immutable version bytes and labels missing definition observations") {
  Fixture f;
  auto p = lexical_profile();
  const auto s = must(f.registry.load(p));
  CHECK(s["definition_source_status"]["synthetic_version"]["status"] == "exact_bytes_recorded");
  const std::string raw = "Exact\r\nżółć🙂\n";
  p["entities"].push_back(entity("raw_prompt", "prompt_version", Json{{"text", raw}, {"text_sha256", Sha256::hex(raw)}, {"encoding", "utf-8"}}));
  auto gap = must(f.registry.load(p));
  CHECK(gap["definition_source_status"]["raw_prompt"]["status"] == "exact_definition_source_unavailable");
  p["sources"].push_back(observation("raw_source", raw));
  CHECK(must(f.registry.load(p))["definition_source_status"]["raw_prompt"]["source_ids"] == Json::array({"raw_source"}));
  p["entities"][1]["attrs"]["definition"]["parameters"]["threshold"] = 0.999;
  auto corrupt = f.registry.load(p);
  REQUIRE_FALSE(corrupt); CHECK(corrupt.error().code == Errc::Conflict);
}

TEST_CASE("method registry reads real accepted rows and blocks receipt drift before dispatch") {
  Fixture f;
  const auto p = lexical_profile();
  const auto accepted = must(f.registry.accept(acceptance(head_fixture(p), "synthetic-registry")));
  const auto receipt_id = accepted["receipt"]["id"].get<std::string>();
  const auto run = accepted["receipt"]["run_id"].get<std::string>();
  Json only_data{{"vocabulary", p["vocabulary"]}, {"selection", p["selection"]}};
  const auto s = must(f.registry.load(only_data, {receipt_id}));
  CHECK(s["entities"]["synthetic_version"] == p["entities"][1]);
  CHECK(s["receipts"][0]["run_id"] == run);
  const auto leaf = must(f.registry.resolve(s, Json::object(), capabilities()))["leaves"][0];
  auto changed = must(model::Entity::from_json(p["entities"][1]));
  changed.attrs["definition"]["parameters"]["threshold"] = 0.01;
  changed.attrs["definition_sha256"] = h(changed.attrs["definition"]);
  REQUIRE(kb::KnowledgeStore(*f.db).put_entities(run, {changed}));
  auto drift = f.registry.load(only_data, {receipt_id});
  REQUIRE_FALSE(drift); CHECK(drift.error().code == Errc::Conflict);
  bool invoked = false;
  const context::MethodPacketOperation never = [&](const Json&) -> Result<Json> { invoked = true; return Json::object(); };
  auto dispatch = f.registry.prepare(s, leaf, Json{{"run_id", "attempt"}, {"origin", origin()}, {"known_at", known}}, never);
  CHECK_FALSE(dispatch); CHECK_FALSE(invoked);
}

TEST_CASE("method registry preserves owner rejection and cannot rewrite version identity through acceptance") {
  Fixture f;
  const auto p = lexical_profile();
  const auto packet = head_fixture(p);
  const auto accepted = must(f.registry.accept(acceptance(packet, "synthetic-registry")));
  const auto run = accepted["receipt"]["run_id"].get<std::string>();
  auto annotated = p;
  annotated["entities"][1]["attrs"]["annotation"] = Json{{"owner_note", "synthetic mutable metadata"}};
  auto annotation_request = acceptance(head_fixture(annotated), "synthetic-registry");
  for (const char* col : {"entities", "claims", "sources"}) annotation_request["expected_rows"][col] = accepted["receipt"]["stored_row_sha256"][col];
  const auto annotation_accepted = must(f.registry.accept(annotation_request));
  CHECK(must(f.registry.load(p, {annotation_accepted["receipt"]["id"]}))["entities"]["synthetic_version"]["attrs"]["annotation"] == annotated["entities"][1]["attrs"]["annotation"]);
  auto revised = p;
  revised["entities"][1]["attrs"]["definition"]["parameters"]["threshold"] = 3.0;
  revised["entities"][1]["attrs"]["definition_sha256"] = h(revised["entities"][1]["attrs"]["definition"]);
  auto change = acceptance(head_fixture(revised), "synthetic-registry");
  for (const char* col : {"entities", "claims", "sources"}) change["expected_rows"][col] = annotation_accepted["receipt"]["stored_row_sha256"][col];
  auto reject_version_reuse = f.registry.accept(change);
  REQUIRE_FALSE(reject_version_reuse); CHECK(reject_version_reuse.error().code == Errc::Conflict);
  revised["entities"][1]["attrs"] = Json::object();
  change["packet"] = head_fixture(revised);
  auto reject_stripping = f.registry.accept(change);
  REQUIRE_FALSE(reject_stripping); CHECK(reject_stripping.error().code == Errc::Conflict);
  auto c = must(model::Claim::from_json(p["claims"][0]));
  c.assessment.status = model::ClaimStatus::Rejected;
  REQUIRE(kb::KnowledgeStore(*f.db).put_claims(run, {c}));
  auto stale = f.registry.load(Json{{"vocabulary", vocab()}}, {accepted["receipt"]["id"]});
  REQUIRE_FALSE(stale); CHECK(stale.error().code == Errc::Conflict);
  auto owner_profile = p;
  owner_profile["claims"][0] = c.to_json();
  auto owner_snapshot = must(f.registry.load(owner_profile));
  auto unavailable_method = f.registry.resolve(owner_snapshot, Json::object(), capabilities());
  REQUIRE_FALSE(unavailable_method); CHECK(unavailable_method.error().code == Errc::Unavailable);
}

TEST_CASE("method registry preserves arbitrary parameter data and native subspans but rejects missing claim dependencies") {
  Fixture f;
  auto p = lexical_profile();
  p["selection"]["user_overrides"] = Json::array({"open_data", 7, nullptr});
  auto leaf = first_leaf(f, p);
  CHECK(leaf["effective_parameters"] == p["selection"]["user_overrides"]);
  const std::string raw = "prefix Exact żółć🙂 suffix";
  auto o = observation("spanned_source", raw);
  p["sources"] = Json::array({o});
  auto c = claim("synthetic_version", "version_of", "synthetic_method", o);
  auto& support = c["assessment"]["basis"]["support"][0];
  const std::string quote = "Exact żółć🙂";
  support["quote"] = quote;
  support["locator"]["byte_start"] = raw.find(quote);
  support["locator"]["byte_len"] = quote.size();
  p["claims"] = Json::array({c});
  CHECK(f.registry.load(p));
  p["claims"][0]["assessment"]["counter"]["claims"] = Json::array({"absent_claim"});
  CHECK_FALSE(f.registry.load(p));
}

TEST_CASE("method registry reads exact native parameter-set versions and checks method bindings") {
  Fixture f;
  auto p = lexical_profile();
  Json definition{{"effective_parameters", p["entities"][1]["attrs"]["definition"]["parameters"]}, {"user_overrides", Json::object()}};
  const auto raw_source = observation("parameter_source", json::canonical(definition));
  p["entities"].push_back(version("parameter_version", "parameter_set_version", definition));
  p["sources"].push_back(raw_source);
  p["claims"].push_back(claim("synthetic_version", "uses_parameter_set", "parameter_version", raw_source));
  auto& method = p["entities"][1]["attrs"];
  method["definition"]["parameter_set_sha256"] = h(definition);
  method["definition_sha256"] = h(method["definition"]);
  p["selection"]["parameter_layers"] = Json::array({"parameter_set", "user"});
  const auto s = must(f.registry.load(p));
  const auto leaf = must(f.registry.resolve(s, Json::object(), capabilities()))["leaves"][0];
  CHECK(leaf["parameter_set"]["id"] == "parameter_version");
  CHECK(leaf["effective_parameters"] == definition["effective_parameters"]);
  CHECK(s["definition_source_status"]["parameter_version"]["status"] == "exact_bytes_recorded");
  auto missing_edge = p; missing_edge["claims"].erase(missing_edge["claims"].end() - 1);
  CHECK_FALSE(f.registry.resolve(must(f.registry.load(missing_edge)), Json::object(), capabilities()));
  auto mismatch = p;
  mismatch["entities"].back()["attrs"]["definition"]["effective_parameters"]["threshold"] = 0.123;
  const auto sha = h(mismatch["entities"].back()["attrs"]["definition"]);
  mismatch["entities"].back()["attrs"]["definition_sha256"] = sha;
  mismatch["entities"][1]["attrs"]["definition"]["parameter_set_sha256"] = sha;
  mismatch["entities"][1]["attrs"]["definition_sha256"] = h(mismatch["entities"][1]["attrs"]["definition"]);
  auto contradictory = f.registry.resolve(must(f.registry.load(mismatch)), Json::object(), capabilities());
  REQUIRE_FALSE(contradictory); CHECK(contradictory.error().code == Errc::Conflict);
}

#ifdef LOOM_METHOD_REGISTRY_W4
namespace {
context::MethodPacketOperation native_operation() { return [](const Json& request) { return packet::execute(request); }; }
Json run_context(const std::string& id) {
  return Json{{"run_id", id}, {"origin", origin()}, {"known_at", known}, {"input_sha256", nullptr},
      {"measurements", Json{{"provider_calls", 0}, {"money_usd", 0}, {"accuracy", nullptr}}}};
}
Json read_w4_fixture() {
  const char* path = std::getenv("METHOD_REGISTRY_W4_FIXTURE");
  REQUIRE_MESSAGE(path != nullptr, "Pinned W4 fixture path required in cross-lane test mode");
  std::ifstream file(path);
  REQUIRE(file.good());
  Json fixture; file >> fixture; return fixture;
}
}

TEST_CASE("method registry prepares native lexical runs without fabricated model or recipe") {
  Fixture f;
  auto p = lexical_profile();
  const auto s = must(f.registry.load(p));
  const auto leaf = must(f.registry.resolve(s, Json::object(), capabilities()))["leaves"][0];
  const auto prepared = must(f.registry.prepare(s, leaf, run_context("native_lexical_attempt"), native_operation()));
  REQUIRE(packet::execute(Json{{"operation", "validate"}, {"packet", prepared["packet"]}}));
  CHECK(prepared["effective_recipe"].is_null());
  CHECK_FALSE(prepared["manifest"]["bindings"].contains("model_identity_id"));
  CHECK_FALSE(prepared["manifest"]["bindings"].contains("compiler_transform_id"));
  CHECK(prepared["manifest"]["trace"]["measurements"]["accuracy"].is_null());
  const auto& manifest = prepared["manifest"];
  const auto pid = manifest["bindings"]["parameter_set_version_id"].get<std::string>();
  Json parameters = nullptr;
  for (const auto& e : prepared["packet"]["entities"]) if (e["id"] == pid) parameters = e;
  REQUIRE(parameters.is_object());
  CHECK(parameters["attrs"]["definition"]["effective_parameters"] == leaf["effective_parameters"]);
  CHECK(parameters["attrs"]["definition"]["user_overrides"] == leaf["user_overrides"]);
  CHECK(manifest["definition_hashes"]["parameter_set"] == parameters["attrs"]["definition_sha256"]);
  for (const auto& subject : {manifest["bindings"]["method_version_id"], manifest["bindings"]["run_id"]}) {
    bool found = false;
    for (const auto& c : prepared["packet"]["claims"]) if (c["subject"] == subject && c["predicate"] == vocab()["predicates"]["uses_parameter_set"] && c["object"] == pid) found = true;
    CHECK(found);
  }
  CHECK(manifest["definition_records"]["parameter_set_version_id"] == parameters["attrs"]);
  CHECK_FALSE(manifest["definition_records"].contains("run_id"));
  bool captured_records = false;
  for (const auto& source : prepared["packet"]["sources"]) if (source["observation"]["text"] == json::canonical(manifest["definition_records"])) captured_records = true;
  CHECK(captured_records);
  const auto accept = must(f.registry.accept(acceptance(prepared["packet"], "native-lexical")));
  auto rows = must(f.registry.load(Json{{"vocabulary", vocab()}, {"selection", p["selection"]}}, {accept["receipt"]["id"]}));
  CHECK(rows["entities"].contains("native_lexical_attempt"));
  CHECK(rows["entities"][pid]["attrs"] == parameters["attrs"]);
  auto no_roles = p; no_roles["vocabulary"]["kinds"].erase("parameter_set_version");
  const auto legacy = must(f.registry.load(no_roles));
  const auto legacy_leaf = must(f.registry.resolve(legacy, Json::object(), capabilities()))["leaves"][0];
  bool invoked = false;
  const context::MethodPacketOperation uncalled = [&](const Json& request) -> Result<Json> { invoked = true; return packet::execute(request); };
  auto missing_role = f.registry.prepare(legacy, legacy_leaf, run_context("missing_role"), uncalled);
  REQUIRE_FALSE(missing_role); CHECK(missing_role.error().code == Errc::Unavailable); CHECK_FALSE(invoked);
  auto rc = run_context("native_unknown_measurements"); rc["measurements"] = nullptr;
  CHECK(must(f.registry.prepare(s, leaf, rc, native_operation()))["manifest"]["trace"]["measurements"].is_null());
}

TEST_CASE("method registry binds post-registration request and creates changed effective hashes through pinned W4") {
  Fixture f;
  const auto fixture = read_w4_fixture();
  Json profile = fixture["base_make_request"];
  profile.erase("operation"); profile.erase("origin"); profile.erase("known_at");
  profile["vocabulary"] = fixture["contract"]["vocabulary"];
  profile["selection"] = Json{{"members", Json::array({Json{{"combination_version_id", fixture["contract"]["bindings"]["combination_version_id"]}}})},
      {"parameter_layers", Json::array({"preset", "recipe", "method", "user"})}, {"user_overrides", Json{{"temperature", 0.8}}}};
  Json caps{{"execution", Json{{"offline_saved_response", Json{{"available", true}}}}}, {"fusion", Json{{"ordered_single_member", Json{{"available", true}}}}}};
  const auto base = must(packet::execute(fixture["base_make_request"]));
  const auto accepted = must(f.registry.accept(acceptance(base, "pinned-w4-registry")));
  const auto receipt = accepted["receipt"]["id"].get<std::string>();
  const auto s = must(f.registry.load(Json{{"vocabulary", profile["vocabulary"]}, {"selection", profile["selection"]}}, {receipt}));
  CHECK(s["definition_source_status"][fixture["contract"]["bindings"]["prompt_version_id"].get<std::string>()]["status"] == "exact_definition_source_unavailable");
  const auto leaf = must(f.registry.resolve(s, Json::object(), caps))["leaves"][0];
  auto rc = run_context("registry_w4_attempt");
  rc["base_packet"] = base;
  rc["recipe_overrides"] = Json{{"request", Json{{"messages", Json::array({Json{{"role", "user"}, {"content", "placeholder"}}})}, {"packet_id", "placeholder"}, {"model", "placeholder"}}},
      {"parameter_bindings", Json::array({Json{{"target", Json::array({"runtime_control"})}, {"source", Json::array({"actual_control"})}},
          Json{{"target", Json::array({"model"})}, {"source", Json::array({"requested_model"})}}})},
      {"request_bindings", Json::array({Json{{"target", Json::array({"packet_id"})}, {"source", Json::array({"base_packet_sha256"})}},
          Json{{"target", Json::array({"model"})}, {"source", Json::array({"effective_parameters", "model"})}},
          Json{{"target", Json::array({"messages", 0, "content"})}, {"source", Json::array({"input_text"})}}})}};
  rc["input_text"] = "Exact żółć🙂 primary reply";
  rc["actual_control"] = "synthetic-control-A";
  rc["requested_model"] = "synthetic/test-model";
  const auto prepared = must(f.registry.prepare(s, leaf, rc, native_operation()));
  CHECK(prepared["manifest"]["definition_hashes"]["recipe"] != fixture["contract"]["definition_hashes"]["recipe"]);
  CHECK(prepared["manifest"]["definition_hashes"]["method_version"] != fixture["contract"]["definition_hashes"]["method_version"]);
  CHECK(prepared["effective_recipe"]["parameters"]["temperature"] == 0.8);
  CHECK(prepared["effective_recipe"]["parameters"]["runtime_control"] == "synthetic-control-A");
  CHECK(prepared["effective_recipe"]["request"]["packet_id"] == prepared["packet"]["packet_id"]);
  CHECK(prepared["manifest"]["trace"]["requested_model"] == prepared["effective_recipe"]["request"]["model"]);
  CHECK(prepared["effective_recipe"]["request"]["messages"][0]["content"] == rc["input_text"]);
  CHECK(prepared["manifest"]["trace"]["recipe_request_sha256"] == h(prepared["effective_recipe"]["request"]));
  CHECK(prepared["manifest"]["bindings"]["combination_version_id"] != fixture["contract"]["bindings"]["combination_version_id"]);
  bool effective_member_edge = false;
  for (const auto& c : prepared["packet"]["claims"]) if (c["subject"] == prepared["manifest"]["bindings"]["combination_version_id"] &&
      c["object"] == prepared["manifest"]["bindings"]["method_version_id"] && c["predicate"] == profile["vocabulary"]["predicates"]["includes_method"])
    effective_member_edge = true;
  CHECK(effective_member_edge);
  bool run_combination_edge = false;
  for (const auto& c : prepared["packet"]["claims"]) if (c["subject"] == prepared["manifest"]["bindings"]["run_id"] &&
      c["object"] == prepared["manifest"]["bindings"]["combination_version_id"] && c["predicate"] == profile["vocabulary"]["predicates"]["uses_combination"])
    run_combination_edge = true;
  CHECK(run_combination_edge);
  auto rc2 = rc; rc2["effective_parameters"] = Json{{"temperature", 0.9}, {"reasoning", nullptr}}; rc2["run_id"] = "registry_w4_attempt_2";
  const auto second = must(f.registry.prepare(s, leaf, rc2, native_operation()));
  CHECK(second["manifest"]["definition_hashes"]["recipe"] != prepared["manifest"]["definition_hashes"]["recipe"]);
  CHECK(second["manifest"]["definition_hashes"]["method_version"] != prepared["manifest"]["definition_hashes"]["method_version"]);
  CHECK(second["manifest"]["bindings"]["parameter_set_version_id"] != prepared["manifest"]["bindings"]["parameter_set_version_id"]);
  auto bound_control = rc; bound_control["actual_control"] = "synthetic-control-B"; bound_control["run_id"] = "registry_bound_control";
  const auto third = must(f.registry.prepare(s, leaf, bound_control, native_operation()));
  CHECK(third["manifest"]["definition_hashes"]["recipe"] != prepared["manifest"]["definition_hashes"]["recipe"]);
  CHECK(third["manifest"]["definition_hashes"]["method_version"] != prepared["manifest"]["definition_hashes"]["method_version"]);
  auto another_model = rc; another_model["requested_model"] = "synthetic/other-model"; another_model["run_id"] = "registry_model_changed";
  CHECK_FALSE(f.registry.prepare(s, leaf, another_model, native_operation()));
  another_model["recipe_overrides"]["model"] = "synthetic/other-model";
  const auto changed_model = must(f.registry.prepare(s, leaf, another_model, native_operation()));
  CHECK(changed_model["manifest"]["definition_hashes"]["recipe"] != prepared["manifest"]["definition_hashes"]["recipe"]);
  CHECK(changed_model["manifest"]["definition_hashes"]["method_version"] != prepared["manifest"]["definition_hashes"]["method_version"]);
  CHECK(changed_model["manifest"]["bindings"]["parameter_set_version_id"] != prepared["manifest"]["bindings"]["parameter_set_version_id"]);
  another_model["recipe_overrides"]["model"] = nullptr; another_model["run_id"] = "registry_model_final_binding";
  const auto final_binding = must(f.registry.prepare(s, leaf, another_model, native_operation()));
  CHECK(final_binding["effective_recipe"]["request"]["packet_id"] == final_binding["packet"]["packet_id"]);
  for (const auto& e : final_binding["packet"]["entities"]) if (e["id"] == "registry_model_final_binding")
    CHECK(e["attrs"]["requested_model"] == final_binding["effective_recipe"]["request"]["model"]);
  rc2["recipe_overrides"]["prompt_sha256"] = std::string(64, '0');
  CHECK_FALSE(f.registry.prepare(s, leaf, rc2, native_operation()));
}

TEST_CASE("method registry produces real result edges and immutable trace sources through pinned W4") {
  Fixture f;
  const auto fixture = read_w4_fixture();
  auto profile = fixture["base_make_request"];
  profile["vocabulary"] = fixture["contract"]["vocabulary"];
  profile["selection"] = Json{{"members", Json::array({Json{{"method_version_id", fixture["contract"]["bindings"]["method_version_id"]}}})},
      {"parameter_layers", Json::array({"preset", "recipe", "method", "user"})}};
  const auto s = must(f.registry.load(profile));
  const auto leaf = must(f.registry.resolve(s, Json::object(), Json{{"execution", Json{{"offline_saved_response", Json{{"available", true}}}}}}))["leaves"][0];
  auto rc = run_context("actual_result_attempt");
  rc["compiler_transform_id"] = fixture["contract"]["bindings"]["compiler_transform_id"];
  rc["model_identity_id"] = fixture["contract"]["bindings"]["model_identity_id"];
  const auto prepared = must(f.registry.prepare(s, leaf, rc, native_operation()));
  auto reply = fixture["reply_request"];
  reply["operation"] = "compile_reply";
  reply["packet"] = prepared["packet"];
  // Fixture reply base stamp belongs to its original packet. Patch this one
  // field in the synthetic reply, then execute the unchanged real compiler.
  auto raw_reply = Json::parse(reply["raw"].get<std::string>());
  raw_reply["base_packet_sha256"] = prepared["packet"]["packet_id"];
  reply["raw"] = json::canonical(raw_reply);
  reply["host"]["recipe_sha256"] = prepared["manifest"]["definition_hashes"]["recipe"];
  reply["host"]["model"] = "synthetic/test-model-resolved-alias";
  const auto compilation = must(packet::execute(reply));
  const auto applied = must(packet::execute(Json{{"operation", "apply_compiled_reply"}, {"packet", prepared["packet"]},
      {"compilation", compilation}, {"policy", fixture["apply_policy"]}, {"explicitly_accepted", true}}));
  Json rb{{"origin", origin()}, {"known_at", known}, {"node_ids", compilation["node_ids"]},
      {"compilation_sha256", compilation["compilation_sha256"]}, {"request", prepared["effective_recipe"]["request"]},
      {"measurements", Json{{"provider_calls", 0}, {"accuracy", nullptr}}}};
  const auto bound = must(f.registry.bind_results(applied["packet"], prepared["manifest"], rb, native_operation()));
  REQUIRE(packet::execute(Json{{"operation", "validate"}, {"packet", bound["packet"]}}));
  CHECK(bound["manifest"]["trace"]["request_bytes_sha256"] == Sha256::hex(json::canonical(rb["request"])));
  CHECK(bound["manifest"]["trace"]["requested_model"] == "synthetic/test-model");
  CHECK(bound["manifest"]["trace"]["actual_model"] == "synthetic/test-model-resolved-alias");
  CHECK(bound["manifest"]["trace"]["actual_model_identity_id"].is_null());
  CHECK(bound["manifest"]["trace"]["requested_model_identity_id"] == rc["model_identity_id"]);
  CHECK_FALSE(bound["manifest"]["definition_records"].contains("model_identity_id"));
  for (auto binding = bound["manifest"]["bindings"].begin(); binding != bound["manifest"]["bindings"].end(); ++binding) {
    if (binding.key() == "run_id" || binding.value().is_null()) continue;
    bool found = false;
    for (const auto& e : bound["packet"]["entities"]) if (e["id"] == binding.value()) {
      CHECK(bound["manifest"]["definition_records"][binding.key()] == e["attrs"]); found = true;
    }
    CHECK(found);
  }
  const auto& predicates = bound["manifest"]["vocabulary"]["predicates"];
  for (const auto& id : compilation["node_ids"]) {
    for (const char* r : {"produced_in_run", "produced_by_method_version", "projected_by_compiler"}) {
      bool found = false;
      for (const auto& c : bound["packet"]["claims"]) if (c["subject"] == id && c["predicate"] == predicates[r]) {
        CHECK(c["qualifiers"]["extra"]["confidence_scope"] == "structure_only"); found = true;
      }
      CHECK(found);
    }
  }
  const auto stored = must(f.registry.accept(acceptance(bound["packet"], "actual-result-provenance")));
  const auto reloaded = must(f.registry.load(Json{{"vocabulary", profile["vocabulary"]}}, {stored["receipt"]["id"]}));
  CHECK(reloaded["entities"]["actual_result_attempt"]["attrs"]["projection_status"] == "response_projected");
  CHECK(reloaded["entities"]["actual_result_attempt"]["attrs"]["measurements"]["accuracy"].is_null());
  rb["expected_request_sha256"] = bound["manifest"]["trace"]["request_sha256"];
  rb["request"] = Json{{"corrupted_payload", true}};
  CHECK_FALSE(f.registry.bind_results(applied["packet"], prepared["manifest"], rb, native_operation()));
  rb.erase("expected_request_sha256"); rb["request"] = prepared["effective_recipe"]["request"];
  auto mismatched = prepared["manifest"]; mismatched["trace"]["method_version_id"] = "wrong_version";
  CHECK_FALSE(f.registry.bind_results(applied["packet"], mismatched, rb, native_operation()));
  auto forged = rb; forged["model_origin"] = origin();
  CHECK_FALSE(f.registry.bind_results(applied["packet"], prepared["manifest"], forged, native_operation()));
  auto explicit_model = rb; explicit_model["expected_model"] = "synthetic/test-model";
  CHECK_FALSE(f.registry.bind_results(applied["packet"], prepared["manifest"], explicit_model, native_operation()));
  auto forged_records = prepared["manifest"];
  forged_records["definition_records"]["parameter_set_version_id"]["definition"]["effective_parameters"] = Json{{"fabricated", true}};
  CHECK_FALSE(f.registry.bind_results(applied["packet"], forged_records, rb, native_operation()));
  auto missing_parameter_edge = applied["packet"];
  for (auto c = missing_parameter_edge["claims"].begin(); c != missing_parameter_edge["claims"].end(); ++c)
    if ((*c)["subject"] == prepared["manifest"]["bindings"]["run_id"] && (*c)["predicate"] == profile["vocabulary"]["predicates"]["uses_parameter_set"]) {
      missing_parameter_edge["claims"].erase(c); break;
    }
  CHECK_FALSE(f.registry.bind_results(missing_parameter_edge, prepared["manifest"], rb, native_operation()));
}
#endif
