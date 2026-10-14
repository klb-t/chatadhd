// loom/chat_route.h — everything from the conversation window (R54, GRAF_HUB_KOD_SILNIK §8).
//
// Every chat input goes through input.route_intent before it is sent:
//   message  -> continues on the existing chat path; nothing about the request changes (same bytes);
//   command  -> resolved against the action catalogue and the rules (actions.for on the referenced
//               element; free text -> candidate actions with scores; a missing trait -> a chain from
//               propose_chain) and answered IN the conversation as a proposal: what will run, on what, the
//               cost estimate, what needs confirmation. The user's "yes" (or the explicit confirm call)
//               runs it through the interpreter (action_run.h); "no" drops it. A setting can let a plan
//               that only reads, or any plan, run at once; the rules and the x10 usage confirmation
//               apply either way.
// The method is a setting (config intent_routing_method, preset in runtime/intent_routing.pack):
// heuristic (local, free), model (one guarded use_model call) or off. Data: runtime/intent_routing.pack
// (lexicon, patterns, confirmation words, texts in pl and en) plus `phrases` on the catalogue's actions.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "loom/chat_engine.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::chat_route {

// chat.route: {"text", "conv_id"?, "method"?, "lang"?, "route_usage_confirmation"?}. Classifies one input and,
// for a command, resolves it and previews the plan; nothing is stored or executed.
//   {"schema":"loom.chat_route/1","kind":"message"|"command"|"pending","method","lang","confidence"?,
//    "command"?: {source, matched, score, action | chain, args, element, feasible, reason?, plan?, gates...},
//    "usage_pending"?, "issues": [...]}
// kind "pending": the model method's own call is held by the usage guard (answer with
// route_usage_confirmation on the identical request).
Result<Json> route(Runtime& rt, const Json& request);

// The sentence of a rejected proposal, to be sent to the model as an ordinary message (setting intent_routing_on_reject:
// offer | send_as_message | drop). `text` is the user's sentence as it stands in the conversation; `history_skip` is the
// exchange of the proposal (the sentence as the command it was read as, the proposal, the answers, the replies): the caller
// sets it in ChatOptions::history_skip, so the request is exactly the one an ordinary message would have made.
struct Resend {
  std::string text;
  std::vector<std::string> history_skip;
  std::string proposal_message_id;
  std::string source_message_id;
};

struct Outcome {
  bool handled = false;       // false: a message; the caller continues on the chat path with the request untouched
                              // (with `resend`: the chat path sends resend->text, see Resend)
  std::optional<Resend> resend;
  std::string conv_id;
  std::string user_message_id;
  std::string assistant_message_id;
  Json chunk;                 // final stream chunk: action_proposal | action_result | usage_pending
  Json result;                // the JSON the C ABI returns for this input
  // What went wrong in routing without stopping it (the same `issues` as chat.route): a model route that failed and the local method
  // decided. A message that goes on to the model with such an issue must say so in its stream (capi_chat.cpp emits the event
  // `route_issue`); nothing is lost either way. Empty array: nothing to say.
  Json issues = Json::array();
};

// actions.confirm as the clients' transports (HTTP, JNI) call it: answers the run and, when a proposal of a conversation
// waits for exactly this run, settles that proposal as the typed "yes" or "no" does (its status, the reply message, the
// offer to send the rejected sentence as a message). The result is the run plus {"proposal_message_id","proposal_status",
// "message_id"?,"offer"?,"resend"?}; without a waiting proposal it is the plain run. "resend": the setting asks the
// client to send the sentence now - the client does it with a chat request carrying send_as_message.
Result<Json> confirm(Runtime& rt, const Json& request);

// actions.client_result as the clients' transports call it: answers the client step a run is paused on and, when the run belongs to a
// conversation, tells how it went there - the message with the result (or the next question of the run), the question of the earlier
// message closed, the proposal settled. The result is the run plus {"message_id"?}.
Result<Json> client_result(Runtime& rt, const Json& request);

// actions.cancel the same way: a paused run is cancelled and the conversation that showed its question closes it.
Result<Json> cancel(Runtime& rt, const Json& request);

// A rule written from a conversation was switched off (or switched on again, `disabled` false): the proposal that wrote it says so
// (written.undone, status undone / executed). A rule that did not come from a conversation has no message to tell.
void note_rule_switched(Runtime& rt, const std::string& rule_id, bool disabled);

// A definition of the user's own (action, sequence, group) was taken back: the proposal in the conversation that wrote it says so
// (status "undone", written.undone). `origin` is the removed entry's origin ({conv_id, node, ...}); no conversation, nothing to mark.
void note_removed(Runtime& rt, const Json& origin);

// The background analysis is held by the x10 guard (events::kSemanticUsagePending): the conversation whose message was being analysed shows the
// guard's question as a message with the receipt ("the background analysis wants about N times more than usual - continue?"), and "yes" / "no" as the
// last thing said answers it through the worker's usage confirmation, as POST /api/semantic/usage-confirm does. A question of an older receipt is
// closed as out of date. Without a conversation to show it in (the messages are gone) nothing is posted; the status of the worker still has it.
void note_semantic_hold(Runtime& rt, const Json& pending);
// The question was answered, by whichever surface (events::kSemanticUsageAnswered): the message that asks it records the answer and stops asking.
void note_semantic_answer(Runtime& rt, const Json& answer);

// The chat window's entry. `request` is the client's request object (its explicit chat features make an
// input a message); `emit_start(conv_id, user_message_id)` is called once the user's message is stored.
Result<Outcome> handle(Runtime& rt, const std::string& text, const ChatOptions& opts, const Json& request,
                       const std::function<void(const std::string&, const std::string&)>& emit_start);

}  // namespace loom::chat_route
