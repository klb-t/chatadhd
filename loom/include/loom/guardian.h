// loom/guardian.h — the context guardian (P21; docs/architecture/STRAZNIK_I_SRODOWISKA_2026-10-14.md).
//
// A secretary, not a gatekeeper. Beside the raw list of messages it builds the most economical and precise REPRESENTATION of the
// conversation and learns from the user's corrections. It never alters a message and never decides what is sent to the model: that is
// the context selector's job (select() below is what the selector reads; setting chat_representation, preset raw = request bytes unchanged).
//
// One cycle, all of it data (runtime profile `guardian`, overlays, the settings guardian_method / guardian_promotion / guardian_model /
// guardian_levels / chat_representation):
//   message -> signal (kinds: course_correction, refinement, proposal_rejection; each recognised by heuristic, model or structure)
//           -> occurrence (graph node, edge `derived_from` to the evidence message)
//           -> hypothesis (graph node: a claim with a scope and a weight; edges `supports` from occurrences, `contradicts` between
//              hypotheses, `except` from exception nodes stated by the user or inferred)
//           -> proposal (a message of the conversation, a run of the action guardian.promote waiting for the user's yes; or an
//              immediate rule with a notice, by the setting guardian_promotion)
//           -> rule in the user overlay profiles/guardian.pack (reversible) and, for a run of clarifications, a compiled representation.
// The lossy levels L0..L3 are derived nodes (`guardian_level`) built from one shared layer of segment annotations (span -> category);
// the guardian writes the cheap categories (fillers, repetitions, retractions), any other producer (the semantic analyzer) may write
// the rest, and a message the analyzer has already annotated is not sent to a model a second time.
#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "loom/event_bus.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
class Database;
class Config;
}  // namespace loom

namespace loom::guardian {

// What the context selector takes from the guardian for one conversation. Empty = the raw messages, as always.
struct Selection {
  std::string preset;                                  // chat_representation as resolved for the conversation ("raw" when nothing is chosen)
  std::optional<Json> spec;                            // loom.active_task_spec/1 for ChatOptions::active_task_spec (level L3)
  Json bindings = Json::object();                      // event id -> {message_id, text_sha256}
  std::string history = "replace_refinement";          // or "append" (preset mixed)
  std::map<std::string, std::string> substitutions;    // message id -> its text at the chosen level (L1, L2)
  std::map<std::string, std::string> levels;           // message id -> the level it was substituted at ("L1", "L2"); the receipt names it
  std::string rules;                                   // the standing rules about the person that may go with this request (normalized text only), "" = none
  std::vector<std::string> skip;                       // messages of another topic thread (setting chat_thread_filter), left out of this request only
  Json notes = Json::array();                          // what was left raw and why (a changed message, no goal ...)
  bool empty() const { return !spec && substitutions.empty() && skip.empty() && rules.empty(); }
};

// The selector's reading. Cheap when nothing is chosen (no rule, no representation, setting raw). Errors never stop a send: the caller
// uses the raw messages.
// `representation` (M1-K1a): a preset named by the chosen selector profile (chat_context.pack selector_profiles[].representation); it replaces the
// answer of the scope chain for chat_representation, because choosing a profile is choosing that bundle. "" = the chain decides, as always.
Result<Selection> select(Database& db, const Config& cfg, const std::string& conv_id, const std::string& representation = "");

// Has the user refused this proposal (by its title) so often in this conversation that the router should not offer it again? The consumer of the
// do_not_propose rules the guardian records from rejected proposals (data: kinds.proposal_rejection.suppress_at).
bool do_not_propose(Database& db, const Config& cfg, const std::string& conv_id, const std::string& proposal_title);

// Names of the actions the guardian serves (runtime/actions.pack, port actions).
bool is_guardian_action(std::string_view action);

class Guardian {
 public:
  explicit Guardian(Runtime& rt);
  ~Guardian();
  Guardian(const Guardian&) = delete;
  Guardian& operator=(const Guardian&) = delete;

  // Subscribes to message:created. stop() unsubscribes and joins the model worker.
  void start();
  void stop();
  // Waits until every queued model reading has been done (tests, shutdown).
  void drain();

  // guardian.observe | status | promote | undo | compile | levels (request objects described at the catalogue entries).
  Result<Json> invoke(std::string_view action, const Json& request);

  // A proposal of the conversation was answered no: the signal proposal_rejection (and, for the guardian's own proposal, the
  // hypothesis is marked rejected so it is not asked again until its weight doubles). `proposal` is metadata.action_proposal.
  void note_rejection(const std::string& conv_id, const std::string& proposal_message_id, const Json& proposal, const std::string& answer_message_id);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace loom::guardian
