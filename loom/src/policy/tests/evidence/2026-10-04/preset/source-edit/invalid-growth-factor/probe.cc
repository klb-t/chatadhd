// Native malformed-default probe. Compile with an isolated regenerated preset
// object ahead of the real kernel archive; no replacement API implementation.
#include "loom/config.h"
#include "loom/usage_policy.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
template <class T>
loom::Json result_diagnostic(const loom::Result<T>& result) {
  if (result) return loom::Json{{"ok", true}};
  return loom::Json{{"ok", false}, {"code", loom::errc_name(result.error().code)},
                    {"message", result.error().message}};
}

template <class F>
loom::Json exception_diagnostic(F operation) {
  try {
    operation();
    return loom::Json{{"threw", false}};
  } catch (const std::exception& problem) {
    return loom::Json{{"threw", true}, {"message", problem.what()}};
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2 || std::string(argv[1]).empty())
      throw std::runtime_error("usage: negative_preset_probe TEMP_DATA_ROOT");
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    loom::Config config(root / "config.json");
    const auto ledger_parent = root / "ledger-must-not-be-created";
    const auto ledger = ledger_parent / "policy.sqlite";
    const auto preset = loom::usage_policy_preset();
    const auto effective = loom::effective_usage_policy_options(config);
    const auto settings = loom::usage_policy_settings(config);
    const auto opened = loom::UsagePolicy::open(ledger);
    loom::Json results{{"checked_preset", result_diagnostic(preset)},
                       {"effective", result_diagnostic(effective)},
                       {"settings", result_diagnostic(settings)},
                       {"default_open", result_diagnostic(opened)}};
    loom::Json legacy{{"compatibility_view", exception_diagnostic([] { (void)loom::usage_policy_defaults(); })},
                      {"loom_config_defaults", exception_diagnostic([] { (void)loom::loom_config_defaults(); })},
                      {"config_get", exception_diagnostic([&] { (void)config.get("loom_usage_policy"); })}};
    bool failed_closed = true;
    for (const auto& result : results)
      failed_closed = failed_closed && !result.at("ok").get<bool>() &&
                      result.at("code") == "invalid_argument";
    for (const auto& result : legacy)
      failed_closed = failed_closed && result.at("threw").get<bool>();
    const bool ledger_created = std::filesystem::exists(ledger);
    const bool ledger_parent_created = std::filesystem::exists(ledger_parent);
    const bool config_created = std::filesystem::exists(config.path());
    failed_closed = failed_closed && !ledger_created && !ledger_parent_created && !config_created;
    std::cout << loom::Json{{"results", results}, {"legacy", legacy},
                           {"ledger_created", ledger_created}, {"ledger_parent_created", ledger_parent_created},
                           {"config_created", config_created}, {"failed_closed", failed_closed},
                           {"execution", "local_cpp_config_only"}}.dump(2) << '\n';
    return failed_closed ? 0 : 1;
  } catch (const std::exception& problem) {
    std::cerr << "negative_preset_probe: " << problem.what() << '\n';
    return 1;
  }
}
