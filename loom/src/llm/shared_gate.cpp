#include "llm/shared_gate.h"

#include "loom/log.h"

namespace loom::llm {

SharedUsageGate::SharedUsageGate(const std::filesystem::path& root, const Config& config, bool require_scope,
                                 Json estimate, Json confirmation)
    : owned_(std::make_unique<chat::ModelUsageGuard>(root, config, require_scope)),
      guard_(owned_.get()),
      estimate_(std::move(estimate)),
      confirmation_(std::move(confirmation)) {}

SharedUsageGate::SharedUsageGate(chat::ModelUsageGuard& guard, Json estimate, Json confirmation, bool settle_inline)
    : guard_(&guard),
      estimate_(std::move(estimate)),
      confirmation_(std::move(confirmation)),
      settle_inline_(settle_inline) {}

Result<GateDecision> SharedUsageGate::admit(const net::HttpRequest& request, const Json& provenance) {
  (void)provenance;  // caller-supplied estimates are bound to receipts as given; derived ones already carry it
  auto admitted = guard_->admit(request, estimate_, confirmation_);
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
  auto settled = guard_->settle(s.attempted, s.received_bytes, s.usage, s.declared);
  if (!settled) log::warn("loom.use_model", "usage settlement failed: {}", settled.error().to_string());
}

}  // namespace loom::llm
