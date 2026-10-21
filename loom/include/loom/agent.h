// loom/agent.h — the agent: use_model + the actions of the catalogue as tools, under the rules of the interpreter
// (PRZEGLAD_STRAZNIK_SRODOWISKA §3.2; the loop itself is tool_loop.h).
//
//   loom_invoke("agent", "run" | "tools" | "formats", request)
//
//   run     {prompt | messages | conversation_id, element?, scope?, model?, provider?/route?, parameters?, actions?,
//            policy?, max_steps?, on_proposal?, lang?, usage_confirmation?, ref?, trace?}
//           A model turn is a use_model call (kind "agent.loop") admitted by the shared usage guard; a tool call is
//           ActionRunner::run - the rules, the x10 guard, the spending limits, the receipts and the graph trace of
//           any other run. A call the policy does not let run is stored as a proposal (`propose`): nothing executes.
//           Answer: loom.agent_loop/1 {status, text, turns, steps, proposals, usage, messages, loop_node, settings}.
//           `resume` with a `conversation_id` also posts the answer as a message of that conversation (chat_route::post_agent_answer;
//           the format of any other action result; the answer gets `message_id`, or `message_error`); `post_answer: true | false`
//           decides either way (preset: true for a resume that names a conversation).
//           Trace in the graph: an `action_run` node per loop; each call is an `action_run` node `part_of` it and
//           so is the run node of the action the interpreter made.
//   tools   the same request without calling a model: the tools that would be offered, the decision of the policy
//           for each (run | propose | deny), the settings and where each value came from, the model's tool format.
//   formats the formats of the profile and which one the model gets.
#pragma once

#include <string_view>

#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::agent {

inline constexpr std::string_view kStepNodeSchema = "loom.agent_step/1";

Result<Json> run(Runtime& rt, const Json& request, const CancelToken* cancel = nullptr);
Result<Json> tools(Runtime& rt, const Json& request);
Result<Json> formats(Runtime& rt, const Json& request);
Result<Json> invoke(Runtime& rt, std::string_view action, const Json& request);

}  // namespace loom::agent
