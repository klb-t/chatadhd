#pragma once

#include <cmath>
#include <algorithm>
#include <limits>
#include <map>
#include "context_execution.h"
#include "context_usage_claim.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"

// Thread 2 provides this dependency independently. The production path fails
// closed until its durable policy is integrated; no mock ledger is substituted.
#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#define LOOM_CONTEXT_HAS_USAGE_POLICY 1
#else
#define LOOM_CONTEXT_HAS_USAGE_POLICY 0
#endif

namespace loom::context::detail {

inline Json goal_typing_options(Runtime& rt) {
  const auto configured = rt.config().get("context_execution", Json::object());
  const auto* scope = current_context_execution_scope();
  const auto* source = scope ? &scope->options() : &configured;
  const auto* typing = json::find(*source, "goal_typing");
  return typing && typing->is_object() ? *typing : Json::object();
}

inline Result<GoalTypingBudget> execution_goal_budget(Runtime& rt) {
  const auto options = goal_typing_options(rt);
  LOOM_TRY(validate_context_execution_options(Json{{"goal_typing", options}}));
  GoalTypingBudget budget;
  budget.max_requests = options.value("max_requests", 1);
  if (const auto* scope = current_context_execution_scope())
    budget.max_requests = std::max(0, budget.max_requests - scope->goal_typing_requests());
  // Historical values are editable presets, never upper bounds on this path.
  budget.max_input_bytes = options.value("max_input_bytes", std::size_t(256000));
  budget.max_output_tokens = options.value("max_output_tokens", 4096);
  budget.timeout_ms = options.value("timeout_ms", 60000);
  budget.max_response_bytes = options.value("max_response_bytes", std::size_t(256000));
  return budget;
}

class GoalTypingUsage {
 public:
  bool admit(Runtime& rt, const Json& options, const net::HttpRequest& request,
             std::size_t input_bytes, const GoalTypingBudget& budget, Json& attempt) {
    // Preserve the old bounded native adapter and its regression sentinels.
    // Production admission is activated only by a separate execution scope.
    if (!current_context_execution_scope()) return true;
#if LOOM_CONTEXT_HAS_USAGE_POLICY
    const Json usage = options.value("usage", Json::object());
    const auto model = json::get_string(attempt, "model");
    const std::string body_hash = Sha256::hex(request.body);
    // A paused receipt binds the complete transport, including its account.
    // Keep header ordering/duplicates; only the opaque digest is persisted.
    Json headers = Json::array();
    for (const auto& [name, value] : request.headers) headers.push_back(Json::array({name, value}));
    const auto request_identity = Sha256::hex(json::canonical(Json{{"method", request.method}, {"url", request.url},
        {"headers", headers}, {"body", request.body}, {"timeout_ms", request.timeout_ms}, {"stream", request.stream}}));
    operation_ = usage.contains("resume_operation_id") ? usage["resume_operation_id"].get<std::string>() : usage.contains("operation_id")
        ? usage["operation_id"].get<std::string>() + "/" + body_hash
        : "usage_goal_" + random_hex(32);
    auto settings = effective_usage_policy_options(rt.config());
    if (!settings) return failure(attempt, "usage_policy_error", settings.error().code);
    auto opened = UsagePolicy::open(rt.paths().root / "usage-policy.sqlite", *settings);
    if (!opened) return failure(attempt, "usage_policy_error", opened.error().code);
    policy_ = std::move(*opened);
    Json estimate{{"operation_id", operation_},
        {"baseline_key", usage.value("baseline_key", "context.goal_typing/" + model)},
        {"resources", Json{{"requests", 1}, {"input_bytes", input_bytes},
            {"response_bytes", options.value("estimated_response_bytes", Json(nullptr))},
            {"output_tokens", options.value("estimated_output_tokens", Json(nullptr))},
            {"cost_usd", options.value("estimated_cost_usd", Json(nullptr))}}},
        {"instrument", "loom.context.goal_typing"}, {"model", model}, {"request_sha256", body_hash},
        {"request_identity_sha256", request_identity},
        {"execution_budget", Json{{"max_requests", budget.max_requests}, {"max_input_bytes", budget.max_input_bytes},
            {"max_response_bytes", budget.max_response_bytes}, {"max_output_tokens", budget.max_output_tokens},
            {"timeout_ms", budget.timeout_ms}}},
        {"estimate_basis", "caller_expected_usage_or_unknown_and_exact_input_bytes"}};
    auto decision = policy_->request(estimate);
    if (!decision) return failure(attempt, "usage_policy_error", decision.error().code);
    current_context_execution_scope()->record_usage_decision(*decision);
    if (const auto* confirmation = json::find(usage, "confirmation"); confirmation &&
        json::get_string(*decision, "status") == "requires_confirmation") {
      decision = policy_->confirm(operation_, json::get_string(*confirmation, "receipt_id"),
          (*confirmation)["approved"].get<bool>(), json::get_string(*confirmation, "ref"));
      if (!decision) return failure(attempt, "usage_confirmation_error", decision.error().code);
      current_context_execution_scope()->record_usage_decision(*decision);
    }
    attempt["usage_policy"] = *decision;
    // Reusing a completed/partly measured ID must not spend a second time.
    const auto* actual = json::find(*decision, "actual");
    if (actual && actual->is_object() && actual->contains("requests")) {
      attempt["status"] = "operation_already_attempted";
      return false;
    }
    if (!decision->value("authorized", false)) {
      attempt["status"] = decision->value("status", "usage_policy_error");
      return false;
    }
    auto claim = claim_context_usage_operation(rt.paths().root, *decision);
    if (!claim) return failure(attempt, "execution_claim_error", claim.error().code);
    attempt["execution_claim"] = *claim;
    current_context_execution_scope()->record_usage_decision(*claim);
    if (!claim->value("claimed", false)) {
      attempt["status"] = "operation_already_started";
      return false;
    }
    return true;
#else
    (void)rt; (void)options; (void)request; (void)input_bytes; (void)budget;
    attempt["usage_policy"] = Json{{"status", "unavailable"}, {"dependency", "thread_2"}};
    attempt["status"] = "usage_policy_unavailable";
    return false;
#endif
  }

  void complete(std::size_t input_bytes, std::size_t response_bytes, const Json* body, Json& attempt) {
#if LOOM_CONTEXT_HAS_USAGE_POLICY
    if (!policy_) return;
    auto measured = policy_->complete(operation_, Json{{"resources", Json{{"requests", 1},
        {"input_bytes", input_bytes}, {"response_bytes", response_bytes}}}, {"provenance", "instrument_measured"}});
    if (!measured) { failure(attempt, "usage_completion_error", measured.error().code); return; }
    attempt["usage_policy"] = *measured;
    current_context_execution_scope()->record_usage_decision(*measured);
    Json reported{{"output_tokens", nullptr}, {"cost_usd", nullptr}};
    if (body) {
      if (const auto* usage = json::find(*body, "usage"); usage && usage->is_object()) {
        for (const auto& [target, source] : std::map<std::string, std::string>{{"output_tokens", "completion_tokens"}, {"cost_usd", "cost"}}) {
          if (const auto* amount = json::find(*usage, source); amount && amount->is_number() &&
              std::isfinite(amount->get<double>()) && amount->get<double>() >= 0) reported[target] = *amount;
        }
      }
    }
    auto updated = policy_->complete(operation_, Json{{"resources", reported}, {"provenance", "provider_reported"}});
    if (!updated) { failure(attempt, "usage_completion_error", updated.error().code); return; }
    attempt["usage_policy"] = *updated;
    current_context_execution_scope()->record_usage_decision(*updated);
#else
    (void)input_bytes; (void)response_bytes; (void)body; (void)attempt;
#endif
  }

 private:
  bool failure(Json& attempt, std::string status, Errc code) {
    attempt["status"] = std::move(status);
    attempt["usage_error"] = std::string(errc_name(code));
    return false;
  }
#if LOOM_CONTEXT_HAS_USAGE_POLICY
  std::unique_ptr<UsagePolicy> policy_;
  std::string operation_;
#endif
};

}  // namespace loom::context::detail
