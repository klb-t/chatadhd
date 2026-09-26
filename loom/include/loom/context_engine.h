// loom/context_engine.h — goal-directed context selection (R12, R13;
// LOOM_CONCEPTUAL_MODEL §4). Area: context+materialize. STATUS: contract +
// stubs ("// STUB: knowledge-wave", Errc::NotImplemented). ADDITIVE: the
// legacy ContextSelector / GraphMemorySelector (Python parity) stay as they
// are; this engine works on the knowledge layer.
//
// ── Semantics ───────────────────────────────────────────────────────
// 1. type_goal(): the prompt/task is typed by a goal type
//    (goals/goal_types.json: cues + targets + the project's kind), with a
//    confidence; the owner may force the type.
// 2. select(): candidates = entities, claims, principles (incl. preferences),
//    decisions and observations reachable from the goal's targets through
//    the roles the goal type lists. Score = relevance x authority (origin
//    order) x freshness x confidence, with diversity (no near-duplicates);
//    evidence classes outside the goal type's list never enter; closes over
//    required dependencies (a claim's premises and the principle it rests on
//    come with it, `required_by` says who pulled them in); each item gets
//    the goal type's resolution for its role (label | summary | full | raw)
//    and a `why`. Budget: tokens = code points / 4 (Loom convention),
//    split over the bands by the goal type; nothing exceeds the budget;
//    what was considered but dropped is listed with its reason.
// 3. Bands in prompt order: stable (constitution, invariants, core
//    preferences: identical across calls -> provider prefix caches help for
//    free) -> project (slow-changing project model) -> goal (goal-specific
//    tail). Selection is the primary saving; caching never constrains it.
// 4. render(): the ContextSet as prompt text, band by band, every item with
//    its evidence/origin marker (policy/evidence_encoding.json markdown).
//    Summaries are extractive by default (derived, with provenance).
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

class Runtime;

namespace context {

// {"text","targets":[entity ids],"project","budget_tokens","goal_type"?,"run"?,"lang"?}
struct ContextRequest {
  std::string text;                      // the prompt / task
  std::vector<std::string> targets;      // entity ids the caller already knows are involved
  std::string project;                   // project entity ("" = derive from targets / text)
  int budget_tokens = 4000;
  std::optional<std::string> goal_type;  // force a goal type
  std::string run;                       // knowledge run ("" = latest done run)
  std::string lang;                      // rendering language ("" = the prompt's)
  static Result<ContextRequest> from_json(const Json& j);
  Json to_json() const;
};

class ContextEngine {
 public:
  ContextEngine(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack);

  Result<model::Goal> type_goal(const ContextRequest& req);
  Result<model::ContextSet> select(const ContextRequest& req);
  // Prompt text in band order with evidence/origin markers.
  Result<std::string> render(const model::ContextSet& set);
  // Token estimate of a text (code points / 4, at least 1 for non-empty).
  static int estimate_tokens(std::string_view text) noexcept;

 private:
  Runtime& rt_;
  kb::KnowledgeStore& store_;
  std::shared_ptr<const kb::Pack> pack_;
};

}  // namespace context
}  // namespace loom
