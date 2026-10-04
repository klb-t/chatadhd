#include "loom/config.h"
#include "loom/usage_policy.h"
#include "loom/util/sha256.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace loom {
namespace {
#include "../policy/usage_policy_preset.inc"

const std::string& preset_text() {
  static const std::string bytes = [] {
    std::string result;
    for (auto chunk : kUsagePolicyPresetChunks) result.append(chunk);
    return result;
  }();
  return bytes;
}

const Result<Json>& builtin_preset() {
  static const Result<Json> preset = []() -> Result<Json> {
    LOOM_TRY_ASSIGN(auto data, json::parse(preset_text()));
    LOOM_TRY(validate_usage_policy_options(data));
    for (const auto* field : {"schema", "growth_factor", "baseline_window", "ledger_busy_timeout_ms",
                              "include_reservations", "initial_baselines"}) {
      if (!data.contains(field))
        return Error(Errc::InvalidArgument, std::string("missing usage preset field: ") + field);
    }
    return data;
  }();
  return preset;
}

Result<Json> apply_override(const Json& preset, const Json& override) {
  LOOM_TRY(validate_usage_policy_options(override));
  Json effective = preset;
  for (auto it = override.begin(); it != override.end(); ++it) effective[it.key()] = it.value();
  return effective;
}

std::string policy_hash(const Json& value) { return Sha256::hex(json::canonical(value)); }
}  // namespace

const Json& usage_policy_defaults() {
  const auto& preset = builtin_preset();
  if (!preset) throw std::runtime_error(preset.error().to_string());
  return *preset;
}

Result<Json> usage_policy_preset() { return builtin_preset(); }

Status validate_usage_policy_options(const Json& options) {
  if (!options.is_object()) return Error(Errc::InvalidArgument, "usage policy must be an object");
  if (options.contains("schema") && (!options["schema"].is_string() || options["schema"] != "loom.usage_policy/1"))
    return Error(Errc::Unsupported, "unsupported usage policy schema");
  if (options.contains("growth_factor")) {
    const auto& v = options["growth_factor"];
    if (!v.is_number() || !std::isfinite(v.get<double>()) || v.get<double>() <= 1)
      return Error(Errc::InvalidArgument, "growth_factor must be finite and greater than one");
  }
  if (options.contains("baseline_window") && !options["baseline_window"].is_null()) {
    const auto& v = options["baseline_window"];
    if (!v.is_number_integer() || (v.is_number_unsigned() &&
        v.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) ||
        v.get<std::int64_t>() <= 0)
      return Error(Errc::InvalidArgument, "baseline_window must be a positive int64 or null");
  }
  if (options.contains("include_reservations") && !options["include_reservations"].is_boolean())
    return Error(Errc::InvalidArgument, "include_reservations must be boolean");
  if (options.contains("ledger_busy_timeout_ms")) {
    const auto& v = options["ledger_busy_timeout_ms"];
    if (!v.is_number_integer() || (v.is_number_unsigned() &&
        v.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) ||
        v.get<std::int64_t>() < 0 || v.get<std::int64_t>() > std::numeric_limits<int>::max())
      return Error(Errc::InvalidArgument, "ledger_busy_timeout_ms must fit the SQLite nonnegative int timeout");
  }
  if (options.contains("initial_baselines")) {
    const auto& seeds = options["initial_baselines"];
    if (!seeds.is_object()) return Error(Errc::InvalidArgument, "initial_baselines must be an object");
    for (auto cohort = seeds.begin(); cohort != seeds.end(); ++cohort) {
      if (cohort.key().empty() || !cohort->is_object())
        return Error(Errc::InvalidArgument, "baseline cohorts must be named objects");
      for (auto v = cohort->begin(); v != cohort->end(); ++v) {
        if (v.key().empty() || (!v->is_null() && (!v->is_number() ||
            !std::isfinite(v->get<double>()) || v->get<double>() < 0)))
          return Error(Errc::InvalidArgument, "baseline quantities must be finite nonnegative numbers or null");
      }
    }
  }
  return {};
}

Result<Json> effective_usage_policy_options(const Config& config) {
  // Execution callers only need this setting, not a copy of unrelated config.
  LOOM_TRY_ASSIGN(auto preset, usage_policy_preset());
  return apply_override(preset,
                        config.JsonStore::get("loom_usage_policy", Json::object()));
}

Result<Json> usage_policy_settings(const Config& config, const Json* proposed_override) {
  // Read once so the reported source, value and hashes cannot describe
  // different concurrent config revisions.
  const auto stored = config.all();
  const auto* override = json::find(stored, "loom_usage_policy");
  LOOM_TRY_ASSIGN(auto preset, usage_policy_preset());
  LOOM_TRY_ASSIGN(auto effective, apply_override(preset, override ? *override : Json::object()));
  Json snapshot{{"preset", preset}, {"stored_override", override ? *override : Json(nullptr)},
                {"effective", effective}, {"source", override ? "configured" : "preset"},
                {"preset_source", "embedded_data"},
                {"preset_document", Json{{"path", kUsagePolicyPresetSource},
                  {"schema", "loom.usage_policy/1"}, {"encoding", "utf8_json"},
                  {"source_sha256", Sha256::hex(preset_text())}}},
                {"override_semantics", "replace_top_level_fields"},
                {"hashes", Json{{"algorithm", "sha256"}, {"representation", "loom.canonical_json"},
                  {"preset", policy_hash(preset)},
                  {"stored_override", override ? Json(policy_hash(*override)) : Json(nullptr)},
                  {"effective", policy_hash(effective)}}}};
  if (proposed_override) {
    LOOM_TRY_ASSIGN(auto proposed, apply_override(preset, *proposed_override));
    snapshot["preview"] = Json{{"override", *proposed_override}, {"effective", proposed},
      {"hashes", Json{{"override", policy_hash(*proposed_override)}, {"effective", policy_hash(proposed)}}},
      {"effective_changed", json::canonical(effective) != json::canonical(proposed)}, {"persisted", false}};
  }
  return snapshot;
}
}  // namespace loom
