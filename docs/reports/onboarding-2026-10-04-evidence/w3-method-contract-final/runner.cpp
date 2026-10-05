#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "method_registry.h"
#include "loom/db.h"
#include "loom/onboarding.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

using namespace loom;
static const std::filesystem::path evidence = "/tmp/onboarding-verification-2026-10-04/w3-method-contract-final";
void save(const char* name, const Json& data) {
  std::ofstream output(evidence / name);
  output << json::dump(data, 2) << '\n';
}
template<class T> T checked(Result<T> result) {
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(result).value();
}
int main() {
  try {
    auto pack = checked(onboarding::builtin_pack());
    auto scenario = checked(onboarding::builtin_scenario());
    auto layers = checked(onboarding::DefaultLayers::create(pack));
    Json defaults{{"privacy", checked(layers.resolve("onboarding.privacy"))["value"]},
        {"settings", checked(layers.resolve("onboarding.settings"))["value"]}};
    auto profile = checked(onboarding::ProfileSession::create(scenario, defaults));
    auto projection = checked(onboarding::project_graph(pack, scenario, profile.snapshot(), layers.snapshot(), "offline-contract-user"));
    const auto& methods = projection.at("method_profile");
    save("input_method_profile.json", methods);
    fsutil::TempDir directory;
    auto database = checked(Database::open(directory.path() / "method-registry.db"));
    context::MethodRegistry registry(*database);
    auto loaded = checked(registry.load(methods));
    save("registry_snapshot.json", loaded);
    for (const auto& source : loaded.at("definition_source_status"))
      if (source.at("status") != "exact_bytes_recorded") throw std::runtime_error("W3 did not recognize exact definition captures");
    auto absent = checked(registry.resolve(loaded, Json::object(), Json::object()));
    save("unavailable_resolution.json", absent);
    if (absent.at("leaves").size() != 1 || absent.at("leaves")[0].at("available") != false)
      throw std::runtime_error("unknown capability must stay visible and unavailable");
    Json capabilities{{"execution", Json{{"onboarding_interview", Json{{"available", true}, {"host", "offline-validation-declaration"}}}}}};
    auto advertised = checked(registry.resolve(loaded, Json::object(), capabilities));
    save("advertised_resolution.json", advertised);
    if (advertised.at("leaves").size() != 1 || advertised.at("leaves")[0].at("available") != true)
      throw std::runtime_error("declared host capability did not resolve");
    const auto& leaf = advertised.at("leaves")[0];
    if (leaf.at("execution_capability") != "onboarding_interview" ||
        leaf.at("effective_parameters") != scenario.at("graph_method").at("parameters") ||
        leaf.at("parameter_layers") != pack.at("method_selection").at("parameter_layers"))
      throw std::runtime_error("W3 method resolution differs from actual W12 data");
    Json tampered = methods;
    tampered["sources"][0]["text_sha256"] = std::string(64, '0');
    const auto rejected = registry.load(tampered);
    if (rejected || rejected.error().code != Errc::Conflict) throw std::runtime_error("source drift was not rejected by W3");
    Json multiple_pack = pack;
    Json alternative = scenario.at("graph_method");
    alternative["revision"] = alternative.at("revision").get<std::int64_t>() + 1;
    alternative["parameters"]["synthetic_setting"] = 1;
    multiple_pack["methods"] = Json::array({alternative});
    multiple_pack["revision"] = multiple_pack.at("revision").get<std::int64_t>() + 1;
    auto multiple_layers = checked(onboarding::DefaultLayers::create(multiple_pack, layers.snapshot()));
    auto multiple_graph = checked(onboarding::project_graph(multiple_pack, scenario, profile.snapshot(), multiple_layers.snapshot(), "offline-contract-user"));
    const auto& multiple_methods = multiple_graph.at("method_profile");
    save("multiversion_method_profile.json", multiple_methods);
    auto multiple_loaded = checked(registry.load(multiple_methods));
    auto multiple_resolution = checked(registry.resolve(multiple_loaded, Json::object(), capabilities));
    save("multiversion_resolution.json", multiple_resolution);
    if (multiple_resolution.at("leaves").size() != 2 || multiple_methods.at("bindings").size() != 2)
      throw std::runtime_error("W3 did not retain both same-method version declarations");
    const auto& versions = multiple_methods.at("bindings");
    if (versions[0].at("method_id") != versions[1].at("method_id") ||
        versions[0].at("method_version_id") == versions[1].at("method_version_id"))
      throw std::runtime_error("same-method identities/versions collapsed");
    bool configured_parameter = false;
    for (const auto& item : multiple_resolution.at("leaves")) {
      if (item.at("available") != true) throw std::runtime_error("same-method version incorrectly unavailable");
      if (item.at("effective_parameters").contains("synthetic_setting")) {
        configured_parameter = true;
        if (item.at("effective_parameters").at("synthetic_setting") != 1)
          throw std::runtime_error("same-method effective parameter source changed");
      }
    }
    if (!configured_parameter) throw std::runtime_error("method's alternative parameters were not resolved");
    Json native_receipt_profile = profile.snapshot();
    native_receipt_profile["candidates"]["offline-result"] = Json{{"field", "work.projects"},
      {"review", "pending"}, {"provenance", "model_inferred"}, {"value", "synthetic proposition"}};
    native_receipt_profile["method_executions"] = Json::array({Json{{"run_id", "e_offline_saved_receipt_fixture"},
      {"method_profile", methods}, {"method_version_id", methods.at("bindings")[0].at("method_version_id")},
      {"request_token", "synthetic-receipt-request"}, {"response_sha256", Sha256::hex("synthetic received DTO")},
      {"response_hash_scope", "canonical_received_reply"}, {"provider", "offline_fixture"},
      {"result_candidate_ids", Json::array({"offline-result"})}}});
    auto receipt_graph = checked(onboarding::project_graph(pack, scenario, native_receipt_profile, layers.snapshot(), "offline-contract-user"));
    save("native_saved_receipt_fixture_projection.json", receipt_graph);
    auto receipt_loaded = checked(registry.load(receipt_graph.at("method_profile")));
    auto receipt_resolution = checked(registry.resolve(receipt_loaded, Json::object(), capabilities));
    if (receipt_resolution.at("leaves").size() != 1) throw std::runtime_error("native saved receipt altered available declarations");
    int runs = 0, result_edges = 0;
    for (const auto& entity : receipt_graph.at("entities"))
      if (entity.at("kind") == pack.at("vocabulary").at("kinds").at("run")) ++runs;
    for (const auto& claim : receipt_graph.at("claims"))
      if (claim.at("predicate") == pack.at("vocabulary").at("predicates").at("produced_in_run") ||
          claim.at("predicate") == pack.at("vocabulary").at("predicates").at("produced_by_method_version")) ++result_edges;
    if (runs != 1 || result_edges != 2) throw std::runtime_error("native saved receipt lost real graph provenance links");
    save("verification.json", Json{{"status", "passed"}, {"method_registry_load", true},
      {"absent_host_method_visible", true}, {"absent_host_available", false}, {"advertised_host_available", true},
      {"parameter_precedence", leaf.at("parameter_layers")}, {"native_entities", methods.at("entities").size()},
      {"native_claims", methods.at("claims").size()}, {"native_sources", methods.at("sources").size()},
      {"tampered_source_rejected", true}, {"same_method_multiversion_load_resolve", true},
      {"same_method_multiversion_count", 2}, {"native_saved_receipt_fixture_projected", true},
      {"native_saved_receipt_result_edges", result_edges}, {"provider_calls", 0}, {"actual_method_executions", 0},
      {"scope", "W12 declaration -> actual W3 load/resolve; execution not tested"}});
    std::cout << "PASS: actual W3 load/resolve accepted W12 definitions; missing capability unavailable, declared capability available; zero provider calls.\n";
    return 0;
  } catch (const std::exception& error) {
    save("error.json", Json{{"error", error.what()}, {"provider_calls", 0}});
    std::cerr << error.what() << '\n';
    return 1;
  }
}
