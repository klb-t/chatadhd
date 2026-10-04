#pragma once

#include "context_candidates.h"
#include "method_registry.h"

namespace loom {
class Runtime;
namespace context {
struct MethodExecutionState;

// Execute the graph-selected methods against the exact corpus gathered by the
// existing selector. Missing configuration keeps the legacy path unchanged.
// Definition/preset/precedence data are supplied by the caller or its pack.
struct MethodCandidates {
  bool enabled = false;
  ContextCandidates candidates;
  Json plan = Json::object();
  Json settings = Json::object();
  Json graph = Json::object();
  std::map<std::string, double> scores;
  std::shared_ptr<MethodExecutionState> execution_state;
};

Result<MethodCandidates> gather_method_candidates(Runtime& runtime,
    kb::KnowledgeStore& store, const std::string& run,
    std::shared_ptr<const kb::Pack> pack, const ContextRequest& request,
    const std::vector<model::EvidenceClass>& admitted_evidence,
    const std::vector<std::string>& graph_claims,
    const std::map<std::string, std::shared_ptr<CandidateChannel>>& installed,
    const Json& execution);

// Explicit arithmetic choice for graph candidates outside method results.
// No default blending or diversity policy is invented in the engine.
Result<double> blend_method_score(double legacy_score,
    const std::optional<double>& measured_score, const Json& settings);
Result<double> method_diversity_score(double score, std::size_t prior_count,
                                    const Json& settings);

// Optional result_methods.{fusion,selection} are caller graph selection
// overlays, each resolving one actual executor version. No stage definition
// or vocabulary is fabricated when those graph data are absent. Prepare the
// selection graph before arithmetic/budgeting; complete it only from the
// materialized items/drops. Dependency queries are dynamic, so the complete
// selection input hash stays unknown; the captured preselection input has a
// separate exact hash and an explicit scope.
Status prepare_method_selection(MethodCandidates& methods, const Json& input,
                                const Json& consumed_parameters);
Status finalize_method_selection(MethodCandidates& methods, model::ContextSet& selected);

}  // namespace context
}  // namespace loom
