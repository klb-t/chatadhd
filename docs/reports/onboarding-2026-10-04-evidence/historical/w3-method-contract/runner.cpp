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

using namespace loom;
static const std::filesystem::path evidence = "/tmp/onboarding-verification-2026-10-04/w3-method-contract";
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
    save("verification.json", Json{{"status", "passed"}, {"method_registry_load", true},
      {"absent_host_method_visible", true}, {"absent_host_available", false}, {"advertised_host_available", true},
      {"parameter_precedence", leaf.at("parameter_layers")}, {"native_entities", methods.at("entities").size()},
      {"native_claims", methods.at("claims").size()}, {"native_sources", methods.at("sources").size()},
      {"tampered_source_rejected", true}, {"provider_calls", 0}, {"actual_method_executions", 0},
      {"scope", "W12 declaration -> actual W3 load/resolve; execution not tested"}});
    std::cout << "PASS: actual W3 load/resolve accepted W12 definitions; missing capability unavailable, declared capability available; zero provider calls.\n";
    return 0;
  } catch (const std::exception& error) {
    save("error.json", Json{{"error", error.what()}, {"provider_calls", 0}});
    std::cerr << error.what() << '\n';
    return 1;
  }
}
