// materialize.h — STUB: knowledge-wave. The context+materialize area replaces
// this file (and may add more under src/materialize/).
#include "loom/materialize.h"

#include "loom/knowledge.h"
#include "stub.h"

namespace loom::materialize {

Json Rendered::to_json() const { return Json{{"stub", "Rendered"}}; }  // STUB: knowledge-wave

Materializer::Materializer(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack)
    : rt_(rt), store_(store), pack_(std::move(pack)) {}
Result<Rendered> Materializer::self_description(std::string_view) {
  (void)rt_;
  (void)store_;
  (void)pack_;
  return LOOM_KNOWLEDGE_STUB("Materializer::self_description");  // STUB: knowledge-wave
}
Result<Rendered> Materializer::dossier(std::string_view, std::string_view) { return LOOM_KNOWLEDGE_STUB("Materializer::dossier"); }  // STUB: knowledge-wave
Result<Rendered> Materializer::backlog(std::string_view) { return LOOM_KNOWLEDGE_STUB("Materializer::backlog"); }  // STUB: knowledge-wave
Result<Rendered> Materializer::extrapolated_spec(std::string_view, std::string_view) {
  return LOOM_KNOWLEDGE_STUB("Materializer::extrapolated_spec");  // STUB: knowledge-wave
}
Result<std::vector<model::ProductCheck>> Materializer::check_preferences(const model::Product&, const std::vector<std::string>&) {
  return LOOM_KNOWLEDGE_STUB("Materializer::check_preferences");  // STUB: knowledge-wave
}

Result<Json> run_stage(knowledge::StageContext&) { return LOOM_KNOWLEDGE_STUB("knowledge.materialize"); }  // STUB: knowledge-wave

}  // namespace loom::materialize
