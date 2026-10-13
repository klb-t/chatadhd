#include "llm/shared_gate.h"

#include "loom/log.h"

namespace loom::llm {

SharedUsageGate::SharedUsageGate(const std::filesystem::path& root, const Config& config, bool require_scope,
                                 Json estimate, Json confirmation, Json declared)
    : owned_(std::make_unique<chat::ModelUsageGuard>(root, config, require_scope)),
      guard_(owned_.get()),
      estimate_(std::move(estimate)),
      confirmation_(std::move(confirmation)),
      declared_(std::move(declared)) {}

SharedUsageGate::SharedUsageGate(chat::ModelUsageGuard& guard, Json estimate, Json confirmation, bool settle_inline)
    : guard_(&guard),
      estimate_(std::move(estimate)),
      confirmation_(std::move(confirmation)),
      settle_inline_(settle_inline) {}

Result<GateDecision> SharedUsageGate::admit(const net::HttpRequest& request, const Json& provenance) {
  Json estimate = estimate_;
  if (estimate.is_object() && !estimate.contains("model_use")) estimate["model_use"] = provenance;
  auto admitted = guard_->admit(request, estimate, confirmation_);
  if (!admitted) return admitted.error();
  GateDecision d;
  d.dispatch = admitted->value("dispatch_authorized", false);
  d.status = json::get_string(*admitted, "status", "unavailable");
  d.decision = *admitted;
  d.decisions = guard_->decisions();
  return d;
}

void SharedUsageGate::settle(const GateSettlement& s) {
  if (!settle_inline_) return;
  auto settled = guard_->settle(s.attempted, s.received_bytes, s.usage, declared_);
  if (!settled) log::warn("loom.use_model", "usage settlement failed: {}", settled.error().to_string());
}

}  // namespace loom::llm
