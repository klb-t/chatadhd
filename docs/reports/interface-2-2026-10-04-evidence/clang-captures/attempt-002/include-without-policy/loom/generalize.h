// loom/generalize.h — from the epistemic state to the generator: paradigm
// matching via roles and morphisms, principle discovery and typing, operator
// mining, competing models, inference with Expected Properties,
// extrapolation, predictions and cross-domain transfer
// (LOOM_CONCEPTUAL_MODEL §2.4–§2.5, §3, §5, §6.5). Area: generalize.
// STATUS: implemented (src/generalize/). Data this area reads beyond the
// model: lexicons/cues.json classes "normative", "paradigm.*" and — when
// present — "generalize.generalization", "principle.level.{value,epistemic}",
// "principle.form.{invariant,default,meta,conflict_resolution}" (built-in
// defaults, tuned on synthetic_dev, are used while the pack lacks them);
// thresholds.json "paradigm", "principles" (+ optional "min_cue_score"),
// "status" and optional "operators" {merge_similarity 0.14,
// apply_similarity 0.12, solution_similarity 0.1, situation_similarity
// 0.12}. A decision deliberately kept open (no chosen alternative, so no
// Decision record) is read from its `decides` claim with value
// {"kept_open": [alternatives]}. Value ops / predicates that need the
// codebase digest or version records (codebase_field, order_chain,
// available_on, ...) are skipped or stay pending, never guessed (I9).
//
// ── Semantics every function must keep (I2, I3, I6, I7) ────────────
// * Matching = partial homomorphism of a paradigm's pattern into the
//   evidence graph, anchored on a subject, with constraint propagation (no
//   general subgraph isomorphism). A domain kind's slot is filled by claims
//   whose predicate is the kind's relation (or found by its bindings);
//   anything unfilled becomes an `absent` claim with Open.fill_query (what
//   would fill it), never a silent gap. Instances are partial by default.
// * Inference: a rule (pack operator, stratum 1) or a transfer morphism
//   produces an `inferred` claim WITH its Expected Property (check state
//   pending), derivation (operator id + version, morphism, depth),
//   premises and alternatives. Premises are never extrapolated/absent;
//   transfer depth is 1 (the KnowledgeStore enforces both on write).
//   evaluate_property() re-checks pending properties as new observations
//   arrive: holds (may be promoted to derived) | violated (contested).
// * Extrapolation: stratum-2 rules produce `extrapolated` proposals, capped
//   by thresholds.paradigm.extrapolation_cap, never premises.
// * model_knowledge never outranks the owner's sources (authority order);
//   priors come through the run's PriorFilter (temporal holdout).
// * Principle discovery: normative statements and generalizations
//   (extract) are clustered by phrase key; clusters matching a seed's
//   phrasings raise that seed's support (evidence_for), others become
//   discovered candidates; typing assigns level, form, scope, protects,
//   derived_from. Seeds that match nothing stay "seed, not found in corpus".
// * Operator mining: for each decision (in time order), the situation
//   features at decision time (open questions, active principles, roles
//   touched, what was new) -> the chosen solution class; recurring pairs
//   become candidate operators with success/failure counts; predictions for
//   a holdout cut are evaluated against what happened after it (§7.1).
// * Competing models are kept side by side and scored; never merged.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/kb.h"
#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
namespace knowledge {
struct StageContext;
}

namespace generalize {

inline constexpr std::string_view kGeneralizerVersion = "1";

// A read-only view of one run's epistemic state (what every function works on).
struct Evidence {
  std::vector<model::Entity> entities;
  std::vector<model::Claim> claims;
  std::vector<model::Observation> observations;
  std::vector<model::Decision> decisions;
  std::vector<model::Area> areas;
  std::vector<model::Principle> principles;   // seeds (after the PriorFilter) + candidates from extract
  // Loads the run from the store (deterministic order).
  static Result<Evidence> load(kb::KnowledgeStore& store, std::string_view run);
};

// {"instance":{...},"claims":[slot claims incl. absent],"score","reasons":[...]}
struct Match {
  model::Instance instance;
  std::vector<model::Claim> claims;
  double score = 0.0;
  Json reasons = Json::array();
  Json to_json() const;
};

class ParadigmMatcher {
 public:
  explicit ParadigmMatcher(std::shared_ptr<const kb::Pack> pack);
  // Project-kind instances (with applicable facets) for every candidate subject.
  Result<std::vector<Match>> match_projects(const Evidence& ev) const;
  // Artifact-type instances for units (brainstorm, specification, ...).
  Result<std::vector<Match>> match_artifacts(const Evidence& ev) const;
  // Cross-subject similarity of filled instances -> analogous_to claims.
  Result<std::vector<model::Claim>> analogies(const std::vector<Match>& matches) const;

 private:
  std::shared_ptr<const kb::Pack> pack_;
};

// Principle discovery + typing.
struct PrincipleReport {
  std::vector<model::Principle> principles;   // seeds with updated evidence_for + discovered candidates
  Json seed_status = Json::object();          // {"p.x": "found|seed_only"}
  Json to_json() const;
};
Result<PrincipleReport> discover_principles(const kb::Pack& pack, const Evidence& ev, const model::PriorFilter& priors);
// Fills level, form, scope, protects, derived_from of a discovered candidate.
Status type_principle(const kb::Pack& pack, model::Principle& p, const Evidence& ev);

// Operator mining from the decision sequence.
Result<std::vector<model::Operator>> mine_operators(const kb::Pack& pack, const Evidence& ev, const model::PriorFilter& priors);

// Competing models of the same observations (§2.5), scored.
Result<std::vector<model::Model>> build_models(const Evidence& ev, const std::vector<model::Principle>& principles);

// Inference: rules of strata 0-1 over matched instances (fixpoint per
// stratum, rules in id order), plus transfer morphisms (depth 1).
Result<std::vector<model::Claim>> infer(const kb::Pack& pack, const Evidence& ev, std::vector<Match>& matches);
Result<std::vector<model::Claim>> transfer(const kb::Pack& pack, const std::vector<Match>& matches);
// Stratum-2 extrapolations (capped, never premises).
Result<std::vector<model::Claim>> extrapolate(const kb::Pack& pack, const Evidence& ev, const std::vector<Match>& matches);
// Re-checks an Expected Property against the current evidence.
Result<model::CheckState> evaluate_property(const kb::ExpectedProperty& ep, const model::Claim& claim, const Evidence& ev);

// Predictions: in situation D the owner will choose solution class E.
Result<std::vector<model::Prediction>> predict(const std::vector<model::Operator>& operators, const Evidence& ev,
                                               std::string_view cut);
// Scores predictions made at `cut` against the evidence after it (§7.1).
Result<std::vector<model::Prediction>> evaluate_predictions(std::vector<model::Prediction> predictions,
                                                            const Evidence& after_cut);

// knowledge.generalize stage: match -> infer -> transfer -> extrapolate ->
// principles -> operators -> models -> predictions (for config.prior_cut),
// written to the store. Params: {"max_depth"?:1, "predict"?:true}.
// -> {"output","stats":{"instances","inferred","extrapolated","transferred","principles","operators","models","predictions"}}
Result<Json> run_stage(knowledge::StageContext& ctx);

}  // namespace generalize
}  // namespace loom
