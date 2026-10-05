// loom/materialize.h — products of the knowledge layer: the
// self-description, dossiers per instance, the backlog of violations and
// gaps, extrapolated specifications, and preference checks on products
// (LOOM_CONCEPTUAL_MODEL §5, §6.6; R2, R6, R11; I8). Area:
// context+materialize. STATUS: contract + stubs ("// STUB: knowledge-wave",
// Errc::NotImplemented).
//
// ── Semantics ───────────────────────────────────────────────────────
// * Every product lists the claims and principles it depends on
//   (model::Product) and the checks it passed; it is regenerated when they
//   change (the input hash covers them). A preference violation is a failing
//   check, not a style note (I8).
// * Rendering never upgrades evidence: observed, derived, inferred (with its
//   expected property and check state), extrapolated (proposal, basis),
//   absent (what would fill it) and user are rendered with the markers of
//   policy/evidence_encoding.json, origin model_knowledge visibly distinct
//   (I2, I3). Extrapolated values appear only in "Proposals" sections.
// * Deterministic: same run + same pack + same judgements -> byte-identical
//   files (no clocks, sorted with explicit tie-breaks).
//
//   self_description   SELF.md: projects (kind, facets, status, versions per branch with lost/restored
//                      oscillation), components, principles (level/form, seed vs discovered, phrasings with
//                      sources), operators, decisions and forks, open questions
//   dossier            one instance: slot table (value, evidence, origin, confidence, expected property,
//                      check state, sources), coverage, conflicts, analogies
//   backlog            principle/preference check violations (rules/checks.json), required slots absent,
//                      contested claims, violated expected properties, lost features
//   extrapolated_spec  a specification skeleton for an instance's missing parts, from extrapolation
//                      rules and operators (proposals only), with a patch plan against the repository
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "loom/kb.h"
#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Runtime;
namespace knowledge {
struct StageContext;
}

namespace materialize {

// {"kind","title","markdown","json":{...},"product":{model Product}}
struct Rendered {
  std::string kind;           // self_description | dossier | backlog | extrapolated_spec
  std::string title;
  std::string markdown;
  Json data = Json::object();
  model::Product product;
  Json to_json() const;
};

class Materializer {
 public:
  Materializer(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack);
  Materializer(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack, Json overrides);

  Result<Rendered> self_description(std::string_view run);
  Result<Rendered> dossier(std::string_view run, std::string_view instance_id);
  Result<Rendered> backlog(std::string_view run);
  Result<Rendered> extrapolated_spec(std::string_view run, std::string_view instance_id);
  // Runs the checks of the applicable principles and preferences (their
  // `checks`, rules/checks.json detectors) over a product's files.
  Result<std::vector<model::ProductCheck>> check_preferences(const model::Product& product,
                                                             const std::vector<std::string>& files);

 private:
  Runtime& rt_;
  kb::KnowledgeStore& store_;
  std::shared_ptr<const kb::Pack> pack_;
  Json overrides_;
};

// knowledge.materialize stage: SELF.md, one dossier per instance, backlog,
// extrapolated specs -> BlobStore + loom_artifacts (+ config.out_dir), products
// -> KnowledgeStore. -> {"output","stats":{...},"artifacts":[...]}
Result<Json> run_stage(knowledge::StageContext& ctx);

}  // namespace materialize
}  // namespace loom
