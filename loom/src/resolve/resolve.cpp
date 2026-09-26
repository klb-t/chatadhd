// resolve.h — STUB: knowledge-wave. The extract+resolve area replaces this
// file (and may add more under src/resolve/).
#include "loom/resolve.h"

#include "loom/knowledge.h"
#include "stub.h"

namespace loom::resolve {

Json MergeDecision::to_json() const { return Json{{"stub", "MergeDecision"}}; }  // STUB: knowledge-wave
Json ResolveResult::to_json() const { return Json{{"stub", "ResolveResult"}}; }  // STUB: knowledge-wave
Json Revision::to_json() const { return Json{{"stub", "Revision"}}; }            // STUB: knowledge-wave
Result<Revision> Revision::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("Revision::from_json"); }  // STUB: knowledge-wave
Json LineageResult::to_json() const { return Json{{"stub", "LineageResult"}}; }  // STUB: knowledge-wave
Json Conflict::to_json() const { return Json{{"stub", "Conflict"}}; }            // STUB: knowledge-wave

Resolver::Resolver(std::shared_ptr<const kb::Pack> pack) : pack_(std::move(pack)) {}
Result<ResolveResult> Resolver::resolve(const std::vector<model::Entity>&, const std::vector<model::Observation>&) const {
  (void)pack_;
  return LOOM_KNOWLEDGE_STUB("Resolver::resolve");  // STUB: knowledge-wave
}
std::vector<model::Claim> Resolver::apply_remap(const std::vector<model::Claim>& claims, const ResolveResult&) const {
  return claims;  // STUB: knowledge-wave
}

Result<LineageResult> code_lineage(const Revision&, const std::vector<Revision>&) {
  return LOOM_KNOWLEDGE_STUB("code_lineage");  // STUB: knowledge-wave
}
Result<std::vector<model::Claim>> lineage_claims(const LineageResult&, std::string_view) {
  return LOOM_KNOWLEDGE_STUB("lineage_claims");  // STUB: knowledge-wave
}
Status calibrate(const kb::Pack&, std::vector<model::Claim>&) { return LOOM_KNOWLEDGE_STUB("calibrate"); }  // STUB: knowledge-wave
Result<std::vector<Conflict>> detect_conflicts(std::vector<model::Claim>&) {
  return LOOM_KNOWLEDGE_STUB("detect_conflicts");  // STUB: knowledge-wave
}

Result<Json> run_resolve_stage(knowledge::StageContext&) { return LOOM_KNOWLEDGE_STUB("knowledge.resolve"); }  // STUB: knowledge-wave
Result<Json> run_assess_stage(knowledge::StageContext&) { return LOOM_KNOWLEDGE_STUB("knowledge.assess"); }    // STUB: knowledge-wave

}  // namespace loom::resolve
