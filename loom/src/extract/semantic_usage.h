// Optional adapter to thread 2's shared ledger; no local policy substitute.
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "loom/runtime.h"

#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#define LOOM_EXTRACT_HAS_USAGE_POLICY 1
#else
#define LOOM_EXTRACT_HAS_USAGE_POLICY 0
#endif

namespace loom::extract::detail {

// The caller retains dispatch state and first-response provenance. In
// particular, an unresolved reservation is never another execution grant.
// A missing dependency is reported as unavailable, without an admission
// receipt or a fabricated rolling baseline. The compatibility preset may
// preserve the pre-W2 execution path, but never calls that path guarded.
class SemanticUsage {
 public:
  static Result<std::unique_ptr<SemanticUsage>> open(Runtime& rt, bool require_policy = false) {
    const Json configured_required = rt.config().get("loom_semantic_usage_policy_required", false);
    if (!configured_required.is_boolean())
      return Error(Errc::InvalidArgument, "loom_semantic_usage_policy_required must be boolean");
    auto adapter = std::unique_ptr<SemanticUsage>(
        new SemanticUsage(require_policy || configured_required.get<bool>()));
#if LOOM_EXTRACT_HAS_USAGE_POLICY
    LOOM_TRY_ASSIGN(auto options, effective_usage_policy_options(rt.config()));
    LOOM_TRY_ASSIGN(auto policy, UsagePolicy::open(rt.paths().root / "usage-policy.sqlite", options));
    adapter->policy_ = std::move(policy);
#endif
    return adapter;
  }

  SemanticUsage(const SemanticUsage&) = delete;
  SemanticUsage& operator=(const SemanticUsage&) = delete;

  bool available() const noexcept {
#if LOOM_EXTRACT_HAS_USAGE_POLICY
    return static_cast<bool>(policy_);
#else
    return false;
#endif
  }

  bool required() const noexcept { return required_; }

  Result<Json> preview(const Json& estimate) {
#if LOOM_EXTRACT_HAS_USAGE_POLICY
    return policy_->preview(estimate);
#else
    return unavailable("preview", Json{{"estimate", estimate}}, false);
#endif
  }

  // With W2, callers require status=allowed AND authorized=true. Without W2,
  // legacy_execution_allowed is a separate compatibility choice, not an
  // authorization from a usage policy. Explicitly required policy blocks it.
  Result<Json> request(const Json& estimate) {
#if LOOM_EXTRACT_HAS_USAGE_POLICY
    return policy_->request(estimate);
#else
    return unavailable("request", Json{{"estimate", estimate}}, !required_);
#endif
  }

  Result<Json> complete(std::string_view operation_id, const Json& actual) {
#if LOOM_EXTRACT_HAS_USAGE_POLICY
    return policy_->complete(operation_id, actual);
#else
    return unavailable("complete", Json{{"operation_id", operation_id}, {"actual", actual}}, false);
#endif
  }

  // Cancel before dispatch, or after an explicit recovery decision. Transport
  // failure after a paid dispatch does not establish zero use: complete known
  // quantities and retain unknown reservations instead of cancelling them.
  Result<Json> cancel(std::string_view operation_id, std::string_view reason) {
#if LOOM_EXTRACT_HAS_USAGE_POLICY
    return policy_->cancel(operation_id, reason);
#else
    return unavailable("cancel", Json{{"operation_id", operation_id}, {"reason", reason}}, false);
#endif
  }

 private:
  explicit SemanticUsage(bool required) : required_(required) {}

  Json unavailable(std::string_view action, Json input, bool legacy_allowed) const {
    return Json{{"schema", "loom.semantic_usage_capability/1"},
                {"action", action},
                {"available", false},
                {"status", "unavailable"},
                {"reason", "usage_policy_dependency_missing"},
                {"guard_applied", false},
                {"authorized", false},
                {"receipt_id", nullptr},
                {"recorded", false},
                {"policy_required", required_},
                {"legacy_execution_allowed", legacy_allowed},
                {"input", std::move(input)}};
  }

  bool required_ = false;
#if LOOM_EXTRACT_HAS_USAGE_POLICY
  std::unique_ptr<UsagePolicy> policy_;
#endif
};

}  // namespace loom::extract::detail

#undef LOOM_EXTRACT_HAS_USAGE_POLICY
