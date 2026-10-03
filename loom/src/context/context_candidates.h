#pragma once

#include <map>

#include "loom/context_engine.h"

namespace loom::context {
struct ContextCandidates {
  std::vector<model::Claim> claims;
  std::map<std::string, Json> factors;
  Json trace = Json::object();
};
ContextCandidates gather_context_candidates(kb::KnowledgeStore& store, const std::string& run,
    std::shared_ptr<const kb::Pack> pack, const ContextRequest& request,
    const std::vector<model::EvidenceClass>& admitted_evidence,
    const std::vector<std::string>& graph_claims,
    const std::map<std::string, std::shared_ptr<CandidateChannel>>& installed);
}
