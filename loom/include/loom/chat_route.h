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

#include <string>

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

struct Outcome {
  bool handled = false;       // false: a message; the caller continues on the chat path with the request untouched
  std::string conv_id;
  std::string user_message_id;
  std::string assistant_message_id;
  Json chunk;                 // final stream chunk: action_proposal | action_result | usage_pending
  Json result;                // the JSON the C ABI returns for this input
};

// The chat window's entry. `request` is the client's request object (its explicit chat features make an
// input a message); `emit_start(conv_id, user_message_id)` is called once the user's message is stored.
Result<Outcome> handle(Runtime& rt, const std::string& text, const ChatOptions& opts, const Json& request,
                       const std::function<void(const std::string&, const std::string&)>& emit_start);

}  // namespace loom::chat_route
