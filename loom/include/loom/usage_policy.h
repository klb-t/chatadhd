// Shared execution policy for context, packets and import (thread 2).
// Defaults are presets. Resource names and comparison cohorts are caller data.
#pragma once

#include "loom/loom.h"

#ifdef __cplusplus
#include <filesystem>
#include <memory>
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Config;

// Versioned preset and validation, also used by configuration/C ABI callers.
// No resource ceiling is imposed. Known amounts are finite nonnegative JSON
// numbers; null means unknown, never zero. Unknown names/extensions survive.
const Json& usage_policy_defaults();
Status validate_usage_policy_options(const Json& options);
Result<Json> effective_usage_policy_options(const Config& config);

// Durable local ledger, independent of core/knowledge schema migrations.
// All admission/lifecycle writes serialize through a SQLite transaction;
// reservations survive restart and must be explicitly completed/cancelled.
class UsagePolicy {
 public:
  static Result<std::unique_ptr<UsagePolicy>> open(
      const std::filesystem::path& ledger_path,
      const Json& options = usage_policy_defaults());
  ~UsagePolicy();
  UsagePolicy(const UsagePolicy&) = delete;
  UsagePolicy& operator=(const UsagePolicy&) = delete;

  // estimate = {operation_id, baseline_key, resources:{name:number|null},
  //             ...caller provenance/extensions}. baseline_key names comparable
  // operations, e.g. task/provider/model/unit; it is not an automatic grouping.
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
  // Only measured/reported quantities train the rolling baseline. Unknown
  // dimensions keep their reservation until resolved by a later complete or
  // explicit cancel; cancellation never invents a zero measurement.
  Result<Json> complete(std::string_view operation_id, const Json& actual);
  Result<Json> cancel(std::string_view operation_id, std::string_view reason);
  Result<Json> inspect(std::string_view baseline_key);

 private:
  struct Impl;
  explicit UsagePolicy(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
}  // namespace loom

extern "C" {
#endif

// JSON command API for the future settings/confirmation screen and non-C++
// clients. Returns malloc-owned JSON (loom_free_string), including normal
// requires_confirmation receipts; errors use the existing C ABI envelope.
// Actions: settings, preview, request, confirm, complete, cancel, inspect.
LOOM_API const char* loom_usage_policy_json(LoomContext* ctx, const char* command_json);

#ifdef __cplusplus
}  // extern "C"
#endif
