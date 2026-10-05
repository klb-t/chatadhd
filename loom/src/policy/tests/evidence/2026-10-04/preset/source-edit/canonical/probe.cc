// Generic source-edit probe: no policy values are embedded in this test.
// Compile against the actual kernel after regenerating the embedded preset,
// then pass a fresh temporary data directory as the sole argument.
#include "loom/config.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
template <class T>
T take(loom::Result<T> result, const std::string& operation) {
  if (!result) throw std::runtime_error(operation + ": " + result.error().to_string());
  return std::move(result).value();
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2 || std::string(argv[1]).empty())
      throw std::runtime_error("usage: preset_paths_probe TEMP_DATA_ROOT");
    const std::filesystem::path data_root(argv[1]);
    std::filesystem::create_directories(data_root);
    loom::Config config(data_root / "config.json");
    const bool before_exists = std::filesystem::exists(config.path());
    const auto before = before_exists ? take(loom::fsutil::read_file(config.path()), "read existing initial config")
                                      : std::string();
    auto preset = take(loom::usage_policy_preset(), "read checked embedded preset");
    auto settings = take(loom::usage_policy_settings(config), "read settings");
    auto effective = take(loom::effective_usage_policy_options(config), "read effective options");
    auto policy = take(loom::UsagePolicy::open(data_root / "probe-policy.sqlite"), "open default policy");
    auto inspected = take(policy->inspect("source-edit-probe"), "inspect default policy");
    loom::Json paths{{"config_get", config.get("loom_usage_policy")},
                     {"loom_config_defaults", loom::loom_config_defaults().at("loom_usage_policy")},
                     {"effective", effective}, {"settings", settings.at("effective")},
                     {"default_open", inspected.at("options")}};
    bool agree = true;
    const auto canonical_preset = loom::json::canonical(preset);
    for (const auto& value : paths)
      agree = agree && loom::json::canonical(value) == canonical_preset;
    agree = agree && loom::json::canonical(loom::usage_policy_defaults()) == canonical_preset;
    const bool after_exists = std::filesystem::exists(config.path());
    const auto after = after_exists ? take(loom::fsutil::read_file(config.path()), "read existing final config")
                                    : std::string();
    const bool stored = config.contains("loom_usage_policy");
    const bool persisted = after_exists && take(loom::json::parse(after), "parse final config").contains("loom_usage_policy");
    const bool unchanged = before_exists == after_exists && (!before_exists || before == after);

    // Synthetic caller overlay: values derive from whatever preset was built,
    // so the same probe verifies a deliberately edited source document.
    loom::Json overlay{{"growth_factor", preset.at("growth_factor").get<double>() + 1.0},
                       {"initial_baselines", {{"probe-overlay", {{"probe:units", preset.at("growth_factor")}}}}},
                       {"probe_extension", {{"caller", "isolated-source-edit-proof"}}}};
    if (auto valid = loom::validate_usage_policy_options(overlay); !valid)
      throw std::runtime_error("validate synthetic overlay: " + valid.error().to_string());
    config.set("loom_usage_policy", overlay);
    if (auto saved = config.save(); !saved)
      throw std::runtime_error("explicit overlay fixture save: " + saved.error().to_string());
    const auto overlay_bytes = take(loom::fsutil::read_file(config.path()), "read explicitly saved overlay");
    loom::Config restarted(config.path());
    auto restarted_effective = take(loom::effective_usage_policy_options(restarted), "read restarted effective overlay");
    auto restarted_settings = take(loom::usage_policy_settings(restarted), "read restarted settings overlay");
    loom::Json expected = preset;
    for (auto field = overlay.begin(); field != overlay.end(); ++field) expected[field.key()] = field.value();
    auto overridden_policy = take(loom::UsagePolicy::open(data_root / "probe-overlay-policy.sqlite", restarted_effective),
                                  "open policy with restarted effective overlay");
    auto overlay_inspection = take(overridden_policy->inspect("probe-overlay"), "inspect overlay policy");
    const bool overlay_agrees = loom::json::canonical(restarted_effective) == loom::json::canonical(expected) &&
                                loom::json::canonical(restarted_settings.at("effective")) == loom::json::canonical(expected) &&
                                loom::json::canonical(overlay_inspection.at("options")) == loom::json::canonical(expected);
    const bool shallow = restarted_effective.at("initial_baselines") == overlay.at("initial_baselines");
    const bool override_preserved = restarted.get("loom_usage_policy") == overlay &&
                                    restarted_settings.at("stored_override") == overlay &&
                                    take(loom::json::parse(overlay_bytes), "parse saved partial overlay").at("loom_usage_policy") == overlay;
    const bool restart_unchanged = take(loom::fsutil::read_file(config.path()), "read config after restart") == overlay_bytes;
    const loom::Json output{
        {"paths", paths}, {"checked_preset", preset},
        {"compatibility_view", loom::usage_policy_defaults()}, {"all_paths_equal", agree},
        {"provenance", {{"preset_source", settings.at("preset_source")},
                        {"preset_document", settings.at("preset_document")},
                        {"hashes", settings.at("hashes")}}},
        {"stored_override_present", stored}, {"preset_written_to_config", persisted},
        {"config_file_exists_before", before_exists}, {"config_file_exists_after_reads", after_exists},
        {"config_file_unchanged", unchanged},
        {"overlay", {{"override", overlay}, {"expected_effective", expected},
                     {"config_get_after_restart", restarted.get("loom_usage_policy")},
                     {"effective_after_restart", restarted_effective},
                     {"settings_effective_after_restart", restarted_settings.at("effective")},
                     {"opened_options", overlay_inspection.at("options")},
                     {"all_effective_paths_equal", overlay_agrees},
                     {"shallow_initial_baselines_replaced", shallow},
                     {"raw_override_preserved", override_preserved},
                     {"config_file_unchanged_on_restart", restart_unchanged}}},
        {"execution", "local_cpp_config_and_sqlite_only"}};
    std::cout << output.dump(2) << '\n';
    return agree && !before_exists && !after_exists && !stored && !persisted && unchanged &&
                   overlay_agrees && shallow && override_preserved && restart_unchanged ? 0 : 1;
  } catch (const std::exception& problem) {
    std::cerr << "preset_paths_probe: " << problem.what() << '\n';
    return 1;
  }
}
