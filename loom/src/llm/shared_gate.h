// The shared usage guard (chat::ModelUsageGuard) as a use-model UsageGate.
// Source-private: call sites reach it through llm::make_usage_gate(), except
// the graph reply path, which lends its own guard and settles it itself.
#pragma once

#include <memory>

#include "chat/graph_reply.h"
#include "loom/use_model.h"

namespace loom::llm {

class SharedUsageGate final : public UsageGate {
 public:
  // Owns a guard over the ledger in `root`.
  SharedUsageGate(const std::filesystem::path& root, const Config& config, bool require_scope, Json estimate,
                  Json confirmation, Json declared);
  // Borrows a guard whose lifecycle the caller keeps (settle_inline=false: the
  // caller settles it after its own post-processing).
  SharedUsageGate(chat::ModelUsageGuard& guard, Json estimate, Json confirmation, bool settle_inline);

  Result<GateDecision> admit(const net::HttpRequest& request, const Json& provenance) override;
  void settle(const GateSettlement& settlement) override;

 private:
  std::unique_ptr<chat::ModelUsageGuard> owned_;
  chat::ModelUsageGuard* guard_ = nullptr;
  Json estimate_, confirmation_, declared_ = Json::object();
  bool settle_inline_ = true;
};

}  // namespace loom::llm
