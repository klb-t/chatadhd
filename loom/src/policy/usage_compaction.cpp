// Settings and entry points of the usage ledger's compaction (see usage_policy.h).
// The fold itself is UsagePolicy::compact; the preset is the runtime profile
// "usage_compaction", so nothing here carries a default of its own.
#include <cmath>
#include <set>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/runtime_profile.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"

namespace loom {
namespace {

constexpr const char* kLedgerFile = "usage-policy.sqlite";
constexpr const char* kLog = "loom.usage_compaction";

std::filesystem::path ledger_path(const std::filesystem::path& root) { return root / kLedgerFile; }

}  // namespace

Status validate_usage_compaction_settings(const Json& settings) {
  if (!settings.is_object()) return Error(Errc::InvalidArgument, "compaction settings must be an object");
  for (const char* field : {"mode", "older_than_days", "statuses", "fold_samples_beyond_window", "reclaim_space", "auto_interval_hours"})
    if (!settings.contains(field)) return Error(Errc::InvalidArgument, std::string("compaction settings need ") + field);
  const auto& mode = settings["mode"];
  if (!mode.is_string() || (mode != "off" && mode != "manual" && mode != "auto"))
    return Error(Errc::InvalidArgument, "compaction mode must be off, manual or auto");
  for (const char* field : {"older_than_days", "auto_interval_hours"}) {
    const auto& v = settings[field];
    if (!v.is_number() || !std::isfinite(v.get<double>()) || v.get<double>() < 0)
      return Error(Errc::InvalidArgument, std::string(field) + " must be a finite nonnegative number");
  }
  if (!settings["statuses"].is_array()) return Error(Errc::InvalidArgument, "statuses must be an array");
  for (const auto& status : settings["statuses"]) {
    // Only terminal operations that carry no pending decision can ever be folded.
    if (!status.is_string() || (status != "completed" && status != "cancelled"))
      return Error(Errc::InvalidArgument, "only completed and cancelled operations can be folded");
  }
  for (const char* field : {"fold_samples_beyond_window", "reclaim_space"})
    if (!settings[field].is_boolean()) return Error(Errc::InvalidArgument, std::string(field) + " must be boolean");
  return {};
}

Result<Json> usage_compaction_settings(const Config& config, const std::filesystem::path& data_root,
                                       const Json* override_settings) {
  // preset (profile defaults + user overlay file) < config key < explicit override
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("usage_compaction", data_root));
  Json effective = profile.values();
  const auto replace_top_level = [&effective](const Json& layer) {
    for (auto it = layer.begin(); it != layer.end(); ++it) effective[it.key()] = it.value();
  };
  const Json configured = config.get("usage_ledger_compaction", Json::object());
  if (configured.is_object()) replace_top_level(configured);
  else if (!configured.is_null()) return Error(Errc::InvalidArgument, "usage_ledger_compaction must be an object");
  if (override_settings) {
    if (!override_settings->is_object()) return Error(Errc::InvalidArgument, "override must be an object");
    replace_top_level(*override_settings);
  }
  LOOM_TRY(validate_usage_compaction_settings(effective));
  return effective;
}

Result<Json> compact_usage_ledger(const Config& config, const std::filesystem::path& data_root, const Json& command) {
  if (!command.is_object()) return Error(Errc::InvalidArgument, "compaction command must be an object");
  const Json* override_settings = json::find(command, "override");
  LOOM_TRY_ASSIGN(auto settings, usage_compaction_settings(config, data_root, override_settings));
  const bool dry_run = json::get_bool(command, "dry_run");
  const std::string as_of = json::get_string(command, "as_of");
  Json head{{"schema", "loom.usage_compaction/1"}, {"settings", settings}, {"ledger_path", ledger_path(data_root).string()}};
  if (settings["mode"] == "off") {
    head["status"] = "off";
    return head;
  }
  if (!std::filesystem::exists(ledger_path(data_root))) {
    head["status"] = "no_ledger";  // nothing has been admitted yet; a compaction never creates a ledger
    return head;
  }
  LOOM_TRY_ASSIGN(auto options, effective_usage_policy_options(config));
  LOOM_TRY_ASSIGN(auto policy, UsagePolicy::open(ledger_path(data_root), options));
  LOOM_TRY_ASSIGN(auto report, policy->compact(settings, dry_run, as_of));
  report["status"] = dry_run ? "dry_run" : "compacted";
  report["ledger_path"] = ledger_path(data_root).string();
  return report;
}

Result<Json> auto_compact_usage_ledger(const Config& config, const std::filesystem::path& data_root) {
  LOOM_TRY_ASSIGN(auto settings, usage_compaction_settings(config, data_root));
  Json skipped{{"schema", "loom.usage_compaction/1"}, {"status", "skipped"}, {"settings", settings}};
  if (settings["mode"] != "auto") {
    skipped["reason"] = "mode";
    return skipped;
  }
  if (!std::filesystem::exists(ledger_path(data_root))) {
    skipped["reason"] = "no_ledger";
    return skipped;
  }
  LOOM_TRY_ASSIGN(auto options, effective_usage_policy_options(config));
  LOOM_TRY_ASSIGN(auto policy, UsagePolicy::open(ledger_path(data_root), options));
  LOOM_TRY_ASSIGN(bool due, policy->compaction_due(settings["auto_interval_hours"].get<double>()));
  if (!due) {
    skipped["reason"] = "not_due";
    return skipped;
  }
  LOOM_TRY_ASSIGN(auto report, policy->compact(settings, false, {}));
  report["status"] = "compacted";
  log::info(kLog, "usage ledger compacted: {} operations and {} samples folded, {} -> {} bytes",
            report["folded_operations"].get<std::int64_t>(), report["folded_samples"].get<std::int64_t>(),
            report["bytes_before"].get<std::uint64_t>(), report["bytes_after"].get<std::uint64_t>());
  return report;
}

}  // namespace loom
