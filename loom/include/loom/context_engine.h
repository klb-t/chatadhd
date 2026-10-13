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

#include <cstddef>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/kb.h"
#include "loom/context_retrieval.h"
#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Runtime;
class RuntimeProfile;

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
  // Independent R28 controls. Radius follows existing subject/object claim
  // links in either direction; it neither merges entities nor infers relevance.
  // Zero disables exploratory goal-edge retrieval. Stable/project candidates
  // and required-premise closure are independent of this radius.
  int relation_hops = 1;
  // No override => the goal type's per-role policy. More detail may fit fewer
  // items under the same budget; it never changes the graph candidate radius.
  std::optional<model::Resolution> detail_resolution;
  // Caller-supplied retrieval projection of an existing product plan. This
  // does not compile instructions or infer the current ActiveTaskSpec.
  Json plan = nullptr;
  // Explicit claim anchors are independent of the exploratory entity radius.
  std::vector<std::string> claim_targets;
  // Follow recorded counter links and report absent/unavailable/budgeted
  // counter-evidence. A missing link never proves there is no contradiction.
  bool include_counter_evidence = false;
  // Additional candidate instruments operate before selection, independently
  // of graph reach and display detail. Built-in tfidf/lexical are offline;
  // unknown capabilities are reported unavailable, never silently scored zero.
  std::vector<CandidateChannelRequest> candidate_channels;
  int candidate_scan_limit = 10000;
  bool lexical_shadow = false;
  static Result<ContextRequest> from_json(const Json& j);
  Json to_json() const;
};

// Native, content-free compatibility proof. This is not a read/egress grant.
struct SourceContextPreflight {
  std::string binding_sha256;
  Json receipt = Json::object();
};

// Request-scoped observations prepared by the Runtime source resolver. Their
// locators address an actual in-memory mapping document, not a guessed pointer
// into the original archive. source_ref retains that original source identity.
// Neither this input nor the resulting ContextSet/full trace is a durable DTO.
struct SourceContextObservation {
  model::Observation observation;
  std::string message_ref;
  std::optional<std::string> parent_ref;
  std::string source_version;
  std::string message_status;
  Json source_ref = Json::object();
  Json read_receipt_ref = Json::object();
};

struct SourceContextInput {
  std::vector<SourceContextObservation> observations;
  // loom.source_context_acquisition/1: requested/sources/omissions arrays.
  // Selection retains hashes only: one for stable declared identity/outcomes,
  // another for the actual access receipts and additional acquisition metadata.
  Json acquisition = Json::object();
  std::string preflight_sha256;
};

// A separate native caller may authorize ONE goal-typing model attempt.
// Installed credentials/model settings do not authorize it. All limits are
// explicit; zero requests keeps the call offline. The caller owns the wider
// session/monetary budget and must retain the returned goal's model-attempt
// trace. First-response bytes are raw sources in the owner's existing source /
// blob store (may contain sensitive provider echoes); diagnostic traces carry
// references, never response text or request headers. This capability is not
// parsed from ContextRequest JSON and is never used by select(), build(), or the
// context-preview C ABI/HTTP API.
struct GoalTypingBudget {
  int max_requests = 0;                 // 0 (offline) or 1
  std::size_t max_input_bytes = 0;      // complete classification prompt, <= 256000
  int max_output_tokens = 0;           // <= 4096
  int timeout_ms = 0;                  // <= 60000
  std::size_t max_response_bytes = 0;  // received body, <= 256000; overflow aborts
};

class ContextEngine {
 public:
  ContextEngine(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack);
  // An explicit native capability injection. The native caller is responsible
  // for authorization/resource limits of external implementations. Request JSON
  // cannot create a provider, discover credentials or authorize network calls.
  void set_candidate_channel(std::string id, std::shared_ptr<CandidateChannel> channel) {
    candidate_channels_[std::move(id)] = std::move(channel);
  }

  // Read-only/offline typing: cue classifier or an explicitly forced type.
  Result<model::Goal> type_goal(const ContextRequest& req);
  // Optional model instrument, requiring separate native authorization/limits.
  // Low-confidence cue typing may use at most one request; failures retain the
  // cue result and explain the fallback in goal.params.external_goal_typing.
  // Confidence is heuristic or provider-self-reported, not calibrated accuracy.
  // Persistence failure preserves the cue + attempted/spent trace, never an
  // implicit retry or a model result whose first response cannot be verified.
  Result<model::Goal> type_goal_with_model(const ContextRequest& req, const GoalTypingBudget& budget);
  Result<model::ContextSet> select(const ContextRequest& req);
  // Source-only, offline preparation. Runtime resolves R40 and obtains local
  // read authority before supplying observations; core does neither source I/O
  // nor implicit knowledge-run lookup. Invoke preflight before reading bytes.
  // All additional/injected candidate channels and payload-capturing method
  // execution are explicitly unavailable in this increment. select repeats
  // preflight and rejects a changed binding before consuming the observations.
  Result<SourceContextPreflight> preflight_source_context(
      const ContextRequest& req, const RuntimeProfile& source_profile) const;
  Result<model::ContextSet> select(const ContextRequest& req,
      const SourceContextInput& input, const RuntimeProfile& source_profile);
  // Prompt text in band order with evidence/origin markers.
  Result<std::string> render(const model::ContextSet& set);
  // Sections-as-data (LOOM_CONCEPTUAL_MODEL idea, prompt_compiler.py):
  // {"goal":{...},"budget_tokens","used_tokens","sections":[{"band","items":
  // [{"ref","ref_kind","resolution","score","factors","why","required_by",
  // "tokens","text"}]}],"dropped":[...]}. Same data render() turns into text;
  // useful for a UI / CLI / test to show why each item is there without
  // re-parsing the rendered prompt.
  Json trace(const model::ContextSet& set) const;
  // Integration hook: type_goal + select + render in one call, for a caller
  // (ChatEngine, the CLI, the server) that wants a ready prompt without
  // touching ContextSet directly. ADDITIVE: chat_engine.h's own context
  // path (memory + GraphMemorySelector) is untouched; a caller opts into
  // this by calling ContextEngine::build() itself.
  // -> {"goal":{...},"context_set":{...},"prompt":"..."}
  Result<Json> build(const ContextRequest& req);
  // Token estimate of a text (code points / 4, at least 1 for non-empty).
  static int estimate_tokens(std::string_view text) noexcept;

 private:
  Result<model::Goal> type_goal_impl(const ContextRequest& req, const GoalTypingBudget* budget,
      const Json* cue_options_snapshot = nullptr);
  Result<model::ContextSet> select_impl(const ContextRequest& req,
      const SourceContextInput* source_input, const RuntimeProfile* source_profile,
      const Json* source_goal_cues = nullptr);
  Runtime& rt_;
  kb::KnowledgeStore& store_;
  std::shared_ptr<const kb::Pack> pack_;
  std::map<std::string, std::shared_ptr<CandidateChannel>> candidate_channels_;
};

}  // namespace context
}  // namespace loom
