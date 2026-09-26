// extract.h — STUB: knowledge-wave. The extract+resolve area replaces this
// file (and may add more under src/extract/).
#include "loom/extract.h"

#include "loom/knowledge.h"
#include "stub.h"

namespace loom::extract {

Json Detection::to_json() const { return Json{{"artifact_type", artifact_type}, {"score", score}, {"reasons", reasons}}; }
Json ClassifiedItem::to_json() const { return Json{{"stub", "ClassifiedItem"}}; }  // STUB: knowledge-wave
Json Extraction::to_json() const { return Json{{"stub", "Extraction"}}; }          // STUB: knowledge-wave

Extractor::Extractor(std::shared_ptr<const kb::Pack> pack) : pack_(std::move(pack)) {}
Result<std::vector<Detection>> Extractor::detect(const UnitContent&) const {
  (void)pack_;
  return LOOM_KNOWLEDGE_STUB("Extractor::detect");  // STUB: knowledge-wave
}
Result<std::vector<model::Observation>> Extractor::segment(const model::ArtifactType&, const UnitContent&) const {
  return LOOM_KNOWLEDGE_STUB("Extractor::segment");  // STUB: knowledge-wave
}
Result<Extraction> Extractor::extract(const model::ArtifactType&, const UnitContent&,
                                      const std::vector<model::Observation>&) const {
  return LOOM_KNOWLEDGE_STUB("Extractor::extract");  // STUB: knowledge-wave
}
Result<Extraction> Extractor::process(const UnitContent&) const { return LOOM_KNOWLEDGE_STUB("Extractor::process"); }  // STUB: knowledge-wave
Result<std::vector<ClassifiedItem>> Extractor::classify_items(const model::ProjectKind&, const std::vector<model::Facet>&,
                                                              const std::vector<model::Observation>&) const {
  return LOOM_KNOWLEDGE_STUB("Extractor::classify_items");  // STUB: knowledge-wave
}

Result<Json> run_stage(knowledge::StageContext&) { return LOOM_KNOWLEDGE_STUB("knowledge.extract"); }  // STUB: knowledge-wave

}  // namespace loom::extract
