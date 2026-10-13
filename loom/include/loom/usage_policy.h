// Shared execution policy for context, packets and import (thread 2).
// Defaults are presets. Resource names and comparison cohorts are caller data.
#pragma once

#include "loom/loom.h"

#ifdef __cplusplus
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Config;

// Versioned, generated data preset and validation, also used by config/C ABI
// callers. The checked decoder is shared by every default path. The reference
// view is for legacy non-Result config interfaces; malformed compiled data
// throws there rather than silently substituting policy values.
// No resource ceiling is imposed. Known amounts are finite nonnegative JSON
// numbers; null means unknown, never zero. Unknown names/extensions survive.
const Json& usage_policy_defaults();
Result<Json> usage_policy_preset();
Status validate_usage_policy_options(const Json& options);
Result<Json> effective_usage_policy_options(const Config& config);

// Comparison-cohort name for an operation kind ("chat.send", "semantic.batch").
// Template is data: config key "usage_baseline_key_templates" {kind: template}
// with {endpoint}/{model} placeholders; absent kinds use "<kind>|{endpoint}|{model}".
std::string usage_baseline_key(const Config& config, std::string_view kind, std::string_view endpoint,
                               std::string_view model);

// One configuration snapshot: preset, stored override, effective options and
// SHA-256 of their canonical JSON (not original file bytes). An optional
// proposed override adds a read-only preview; it never saves settings or opens
// a ledger. Top-level fields replace preset fields, including whole objects.
Result<Json> usage_policy_settings(const Config& config,
                                  const Json* proposed_override = nullptr);

// ── Ledger compaction (housekeeping; not part of the receipt-bound options) ──
// Completed operations older than a preset age are folded into per-cohort
// aggregates: the operation's record and events are removed, a small
// tombstone keeps its identity and the hash of its estimate (a repeated
// operation id stays idempotent), and the measured samples of folded
// operations beyond the policy's baseline window are summed into one row per
// cohort and resource.  The rolling baseline the growth guard compares
// against is preserved exactly.  allowed / unresolved / requires_confirmation
// / denied operations, owner-confirmed ones and every receipt of those are
// never folded.
// The preset is the runtime profile "usage_compaction" (with its user overlay
// profiles/usage_compaction.pack); the config key "usage_ledger_compaction"
// replaces its top-level fields, and an explicit command's override replaces
// those.  Mode: off | manual | auto.
Status validate_usage_compaction_settings(const Json& settings);
Result<Json> usage_compaction_settings(const Config& config, const std::filesystem::path& data_root,
                                       const Json* override_settings = nullptr);
// Explicit compaction of <data_root>/usage-policy.sqlite.  command:
// {"override": {...settings}, "dry_run": bool, "as_of": "YYYY-MM-DD[ HH:MM:SS]"}.
// Mode "off" folds nothing (status "off") unless the override changes the mode.
Result<Json> compact_usage_ledger(const Config& config, const std::filesystem::path& data_root,
                                  const Json& command);
// The automatic run: only in mode "auto", at most once per auto_interval_hours,
// and never creates a ledger.  Status "skipped" says why nothing happened.
Result<Json> auto_compact_usage_ledger(const Config& config, const std::filesystem::path& data_root);

// Durable local ledger, independent of core/knowledge schema migrations.
// All admission/lifecycle writes serialize through a SQLite transaction;
// reservations survive restart and must be explicitly completed/cancelled.
class UsagePolicy {
 public:
  static Result<std::unique_ptr<UsagePolicy>> open(
      const std::filesystem::path& ledger_path,
      const Json& options = Json::object());
  ~UsagePolicy();
  UsagePolicy(const UsagePolicy&) = delete;
  UsagePolicy& operator=(const UsagePolicy&) = delete;

  // estimate = {operation_id, baseline_key, resources:{name:number|null},
  //             ...caller provenance/extensions}. baseline_key names comparable
  // operations, e.g. task/provider/model/unit; it is not an automatic grouping.
  // Graph method/version/run references and recipe/prompt hashes are opaque
  // caller extensions. The complete estimate is retained and receipt-bound;
  // this ledger does not define graph vocabulary or create provenance edges.
  // preview is read-only. request records its decision and atomically reserves
  // only when allowed. Repeat identical operation IDs are idempotent;
  // reusing an ID for a different estimate is a conflict.
  Result<Json> preview(const Json& estimate);
  Result<Json> request(const Json& estimate);

  // The receipt returned by request is required: confirmation binds to the
  // exact estimate/options/baseline/projected usage shown to the owner.
  // A changed projection requires a fresh request, not a silently broader grant.
  Result<Json> confirm(std::string_view operation_id, std::string_view receipt_id,
                       bool approved, std::string_view confirmation_ref);

  // actual = {resources:{name:number|null}, provenance:"instrument_measured"|
  //           "provider_reported"|"declared", ...extensions}.
  // Actual extensions are retained in recorded events, visible through inspect.
  // Only measured/reported quantities train the rolling baseline. Unknown
  // dimensions keep their reservation until resolved by a later complete or
  // explicit cancel; cancellation never invents a zero measurement.
  Result<Json> complete(std::string_view operation_id, const Json& actual);
  Result<Json> cancel(std::string_view operation_id, std::string_view reason);
  Result<Json> inspect(std::string_view baseline_key);

  // Folds eligible operations under `settings` (see above); dry_run reports
  // without writing.  `as_of` ("" = now) is the reference time of the age.
  Result<Json> compact(const Json& settings, bool dry_run = false, std::string_view as_of = {});
  // True when no compaction ran in the last `hours` (or none ever).
  Result<bool> compaction_due(double hours);

 private:
  struct Impl;
  explicit UsagePolicy(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
}  // namespace loom

extern "C" {
#endif

// Static-kernel JSON command API for future settings/confirmation clients.
// Shared-library export registration requires a scoped loom.h/ABI-test change;
// this lane deliberately leaves the existing shared export set unchanged.
// Returns malloc-owned JSON (loom_free_string), including normal
// requires_confirmation receipts; errors use the existing C ABI envelope.
// Actions: settings, preview_settings, preview, request, confirm, complete,
// cancel, inspect, compact, compaction_settings. preview_settings takes an
// "override" object; compact takes {"override","dry_run","as_of"} (see
// compact_usage_ledger).
const char* loom_usage_policy_json(LoomContext* ctx, const char* command_json);

#ifdef __cplusplus
}  // extern "C"
#endif
