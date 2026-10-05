#pragma once

#include "loom/context_engine.h"

namespace loom::context {

// A synchronous native execution capability, never reconstructed from preview
// ContextRequest JSON. Nesting and exceptions restore the previous capability;
// each scope owns its options and its shared goal-typing attempt counter.
// goal_typing.enabled AND calls_authorized must be true to send anything.
// Defaults are presets, not maximums. Supported integer widths remain those
// of the existing transport/GoalTypingBudget. max_requests is shared across
// plan projections; this does not introduce retries of a failed response.
class ContextExecutionScope {
 public:
  explicit ContextExecutionScope(Json options);
  ~ContextExecutionScope();
  ContextExecutionScope(const ContextExecutionScope&) = delete;
  ContextExecutionScope& operator=(const ContextExecutionScope&) = delete;
  const Json& options() const { return options_; }
  int goal_typing_requests() const { return goal_typing_requests_; }
  void note_goal_typing_request() { ++goal_typing_requests_; }
  void record_usage_decision(Json receipt) { usage_decisions_.push_back(std::move(receipt)); }
  const Json& usage_decisions() const { return usage_decisions_; }

 private:
  Json options_;
  ContextExecutionScope* previous_;
  int goal_typing_requests_ = 0;
  Json usage_decisions_ = Json::array();
};

ContextExecutionScope* current_context_execution_scope();
Status validate_context_execution_options(const Json& options);

// This helper preserves the original typed goal (classification confidence,
// first-response source references, fallback and usage receipt) in selection.
// It does not call build() again. Ordinary previews have no execution scope.
Result<Json> build_context_with_execution(ContextEngine& engine, const ContextRequest& request,
                                         Runtime& runtime, const Json& options);

}  // namespace loom::context
