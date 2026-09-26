// catalog.h — STUB: knowledge-wave. The catalog area replaces this file (and
// may add more files under src/catalog/). Every body below returns
// Errc::NotImplemented or an empty value and is marked "STUB: knowledge-wave".
#include "loom/catalog.h"

#include "loom/knowledge.h"
#include "stub.h"

namespace loom::catalog {

namespace {
Json stub_json(std::string_view what) { return Json{{"stub", std::string(what)}}; }  // STUB: knowledge-wave
}  // namespace

SketchParams SketchParams::mobile() {  // STUB: knowledge-wave
  SketchParams p;
  p.top_k = 64;
  p.minhash = 32;
  return p;
}
Json SketchParams::to_json() const { return stub_json("SketchParams"); }  // STUB: knowledge-wave
Result<SketchParams> SketchParams::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("SketchParams::from_json"); }  // STUB: knowledge-wave
Result<ScanConfig> ScanConfig::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("ScanConfig::from_json"); }  // STUB: knowledge-wave
Json ScanConfig::to_json() const { return stub_json("ScanConfig"); }  // STUB: knowledge-wave
Json CatalogUnit::to_json() const { return stub_json("CatalogUnit"); }  // STUB: knowledge-wave
Result<CatalogUnit> CatalogUnit::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("CatalogUnit::from_json"); }  // STUB: knowledge-wave
Json SelfProfile::to_json() const { return stub_json("SelfProfile"); }  // STUB: knowledge-wave
Result<SelfProfile> SelfProfile::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("SelfProfile::from_json"); }  // STUB: knowledge-wave
Result<ProfileConfig> ProfileConfig::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("ProfileConfig::from_json"); }  // STUB: knowledge-wave
Json ProfileConfig::to_json() const { return stub_json("ProfileConfig"); }  // STUB: knowledge-wave
Result<ScoreConfig> ScoreConfig::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("ScoreConfig::from_json"); }  // STUB: knowledge-wave
Json ScoreConfig::to_json() const { return stub_json("ScoreConfig"); }  // STUB: knowledge-wave
Json UnitScore::to_json() const { return stub_json("UnitScore"); }  // STUB: knowledge-wave
Result<UnitScore> UnitScore::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("UnitScore::from_json"); }  // STUB: knowledge-wave
Result<Override> Override::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("Override::from_json"); }  // STUB: knowledge-wave
Json Override::to_json() const { return stub_json("Override"); }  // STUB: knowledge-wave
Json Decision::to_json() const { return stub_json("Decision"); }  // STUB: knowledge-wave
Result<Decision> Decision::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("Decision::from_json"); }  // STUB: knowledge-wave
Result<UnitQuery> UnitQuery::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("UnitQuery::from_json"); }  // STUB: knowledge-wave
Json UnitQuery::to_json() const { return stub_json("UnitQuery"); }  // STUB: knowledge-wave
Result<ImportOptions> ImportOptions::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("ImportOptions::from_json"); }  // STUB: knowledge-wave
Json ImportOptions::to_json() const { return stub_json("ImportOptions"); }  // STUB: knowledge-wave

Catalog::Catalog(Runtime& rt, std::shared_ptr<const kb::Pack> pack) : rt_(rt), pack_(std::move(pack)) {}
Status Catalog::ensure_schema(Database&) { return LOOM_KNOWLEDGE_STUB("Catalog::ensure_schema"); }  // STUB: knowledge-wave
Result<Json> Catalog::scan(const ScanConfig&, const ProgressFn&, const CancelToken*) {
  (void)rt_;
  (void)pack_;
  return LOOM_KNOWLEDGE_STUB("Catalog::scan");  // STUB: knowledge-wave
}
Result<SelfProfile> Catalog::build_profile(const ProfileConfig&) { return LOOM_KNOWLEDGE_STUB("Catalog::build_profile"); }  // STUB: knowledge-wave
Result<Json> Catalog::score(const ScoreConfig&, const ProgressFn&, const CancelToken*) {
  return LOOM_KNOWLEDGE_STUB("Catalog::score");  // STUB: knowledge-wave
}
Result<std::vector<Decision>> Catalog::select(std::string_view) { return LOOM_KNOWLEDGE_STUB("Catalog::select"); }  // STUB: knowledge-wave
Status Catalog::set_override(const Override&) { return LOOM_KNOWLEDGE_STUB("Catalog::set_override"); }  // STUB: knowledge-wave
Result<std::vector<CatalogUnit>> Catalog::query(const UnitQuery&) { return LOOM_KNOWLEDGE_STUB("Catalog::query"); }  // STUB: knowledge-wave
Result<Json> Catalog::preview(std::string_view) { return LOOM_KNOWLEDGE_STUB("Catalog::preview"); }  // STUB: knowledge-wave
Result<std::string> Catalog::read_unit(std::string_view) { return LOOM_KNOWLEDGE_STUB("Catalog::read_unit"); }  // STUB: knowledge-wave
Result<Json> Catalog::import_selected(const ImportOptions&, const ProgressFn&, const CancelToken*) {
  return LOOM_KNOWLEDGE_STUB("Catalog::import_selected");  // STUB: knowledge-wave
}
Status Catalog::add_profile_terms(const Json&) { return LOOM_KNOWLEDGE_STUB("Catalog::add_profile_terms"); }  // STUB: knowledge-wave
Result<Json> Catalog::status() { return LOOM_KNOWLEDGE_STUB("Catalog::status"); }  // STUB: knowledge-wave

Result<Json> run_stage(knowledge::StageContext&) { return LOOM_KNOWLEDGE_STUB("knowledge.catalog"); }  // STUB: knowledge-wave

}  // namespace loom::catalog
