// Optional, bounded model proposals over located knowledge observations.
// Results enter the existing candidate queue, never the canonical claim set.
#pragma once

#include <string_view>
#include <vector>

#include "loom/model.h"
#include "loom/result.h"

namespace loom {
class Runtime;
namespace knowledge { struct StageContext; }
namespace extract {

inline constexpr std::string_view kKnowledgeSemanticVersion = "1";

// Non-secret runtime identity for knowledge run/task fingerprints. Does not
// include API key bytes or a hash of them; availability is only a boolean.
// extract_params is the extract stage's params, including optional semantic.
Json semantic_fingerprint(Runtime& rt, const Json& extract_params, std::string_view llm_mode);

// ctx.params._semantic_identity, when present, must equal the current identity.
// Snapshot configuration once; group by unit/source/member/branch and preserve
// observation order. No HTTP request occurs while a Database lock is held.
// Returns explicit status, counts, rejection reasons, candidate_ids and a
// deterministic output hash. Network/model failures preserve existing claims.
Result<Json> propose_semantics(knowledge::StageContext& ctx,
                               const std::vector<model::Observation>& observations,
                               const std::vector<model::Entity>& entities,
                               const std::vector<model::Claim>& claims);

}  // namespace extract
}  // namespace loom
