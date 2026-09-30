#pragma once

#include <map>

#include "loom/context_engine.h"

namespace loom::context {
struct ContextEvidence {
  std::vector<model::Claim> claims;
  std::vector<model::Observation> observations;
  std::map<std::string, std::vector<std::string>> counter_for;
  Json requested = Json::array();
  Json counters = Json::object();
};

// Retrieve explicit claim anchors and one layer of recorded counter links.
// Initial refs are directly retrieved claims, before selection/budgeting.
ContextEvidence gather_context_evidence(kb::KnowledgeStore& store, const std::string& run,
    const ContextRequest& request, const std::vector<std::string>& initial_claims,
    const std::vector<model::EvidenceClass>& admitted_evidence);

// Change candidate statuses into selected/over_budget only after selection.
void finalize_context_evidence(ContextEvidence& evidence, const std::vector<model::ContextItem>& selected);
}
