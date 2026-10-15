// loom/tool_loop.h — the tool loop of use_model (the agent; PRZEGLAD_STRAZNIK_SRODOWISKA §3.2, step 7).
//
// The model is offered the actions of the catalogue that fit the elements in context as TOOLS. It asks for one, the
// interpreter carries it out, the result goes back to the model, and so on until the model answers without a tool or a
// bound is reached. This layer knows models and tool dialects; it knows nothing of how an action is executed:
//
//   ToolHost    the only door to the interpreter. The runtime-bound host (agent.h) is the ActionRunner's public API
//               (run / propose), so the rules, the x10 guard, the spending limits, the receipts and the graph trace of
//               every run apply unchanged. Nothing here runs an executor, opens a database or spends a byte on its own.
//   policy      data (runtime profile `tool_loop`, setting tool_loop_policy): per call `run`, `propose` or `deny`,
//               decided from the effect flags of the catalogue entry; a proposal runs nothing.
//   bound       the number of model turns (setting tool_loop_max_steps, 0 = no limit).
//   dialect     how a model is offered tools and how its call and the result travel: openai_tools (OpenAI / OpenRouter),
//               anthropic_tool_use, and `text` (a marked JSON call in the prompt) for a model without native tools.
//               Which one a model gets is data: config model_capabilities[model].tools, the provider manifest's
//               metadata capabilities.tools, the profile's model_rules, format_by_adapter, default_format.
//
// Every model turn is an ordinary use_model call (kind "agent.loop"): the shared usage guard admits it, so a held turn
// ends the loop with status "held" and the guard's own pending record.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "loom/result.h"
#include "loom/use_model.h"
#include "loom/util/json.h"

namespace loom::llm::tools {

inline constexpr std::string_view kLoopSchema = "loom.agent_loop/1";

// One action offered to the model.
struct Tool {
  std::string name;         // the wire name (allowed characters only, unique)
  std::string action;       // the catalogue id
  std::string description;
  Json parameters = Json::object();  // JSON schema of the arguments
  Json effects = Json::object();     // the entry's effect flags (what the policy decides on)
  bool takes_element = false;        // the call may name another element than the loop's
};

// The resolved settings of one loop (see resolve_settings).
struct LoopSettings {
  std::string policy_id;
  Json rules = Json::array();        // the policy: [{when?, mode}]
  long max_steps = 0;                // 0 = no limit
  std::string on_proposal = "stop";  // stop | continue
  long result_max_chars = 0;         // 0 = no cut
  std::string format;                // dialect id of the model (profile `formats`)
  std::string format_kind;           // openai_tools | anthropic_tool_use | text | none
  Json format_data = Json::object();
  Json messages = Json::object();    // localized notices
  std::string lang = "en";
  std::string kind = "agent.loop";
  Json sources = Json::object();     // where each value came from: {policy, max_steps, ...: request | setting | profile}
};

// What ToolHost::call answers: the interpreter's run document, reduced to what the loop needs.
struct HostResult {
  std::string status;    // completed | proposed | denied | awaiting_usage | awaiting_confirmation | failed | ...
  std::string run_id;
  std::string text;      // the text of the result (completed)
  std::string reason;    // why not (denied / failed)
  Json run = nullptr;    // the whole run document
};

// The door to the interpreter.
class ToolHost {
 public:
  virtual ~ToolHost() = default;
  // `propose`: store the plan and wait for the user - execute nothing.
  virtual Result<HostResult> call(const Tool& tool, const Json& arguments, const std::string& element, bool propose) = 0;
};

// How one call ended, as the loop reports it.
struct StepRecord {
  std::size_t turn = 0;        // the model turn that asked for it (1-based)
  std::string tool_call_id;
  std::string tool;            // the wire name
  std::string action;          // the catalogue id ("" = unknown tool)
  Json arguments = Json::object();
  std::string mode;            // run | propose | deny | invalid
  std::string status;          // the run's status, or unknown_tool | bad_arguments
  std::string run_id;
  Json to_json() const;
};

struct LoopHooks {
  std::function<void(const StepRecord&, const HostResult*)> on_step;   // after each call
  std::function<void(std::size_t turn, const VariantResult&)> on_turn;  // after each model turn
};

struct LoopOutcome {
  // finished | step_limit | needs_confirmation | held | failed | cancelled
  std::string status;
  std::string text;                  // the model's last text
  std::size_t turns = 0;             // model turns taken
  std::vector<StepRecord> steps;
  Json proposals = Json::array();    // [{action, arguments, run_id, tool_call_id}]
  Json usage = Json::object();       // numeric usage fields summed over the turns
  Json hold = nullptr;               // held: the guard's pending record
  std::optional<Error> error;
  Json messages = Json::array();     // the transcript (canonical form), to continue from
  Json to_json() const;
};

// ── Data ────────────────────────────────────────────────────────────

// The profile `tool_loop` with the overlay applied.
Result<Json> load_profile(const Services& services);

// The format of a model (a key of the profile's `formats`): see the layers in the header comment.
Result<std::string> resolve_format(const Services& services, const Json& profile, const Route& route, const ModelSpec& model);

// Settings of a loop: the request's own values (`policy`: id or inline {rules}; `max_steps`; `on_proposal`) over the
// settings (tool_loop_policy: id or inline policy, tool_loop_max_steps, tool_loop_on_proposal) over the profile.
Result<LoopSettings> resolve_settings(const Services& services, const Json& profile, const Route& route,
                                      const ModelSpec& model, const Json& request);

// The decision of the policy for one catalogue entry's effects: run | propose | deny.
std::string decide_mode(const Json& rules, const std::string& action, const Json& effects);

// The tools for a list of catalogue ids (entries come from the catalogue): wire names are unique and valid for the
// provider; `lang` picks the description.
std::vector<Tool> make_tools(const Json& profile, const Json& catalogue, const std::vector<std::string>& action_ids,
                             const std::string& lang);

// ── The loop ────────────────────────────────────────────────────────

// `base` carries the model(s), the route, the messages / prompt / context, the parameters and usage settings of every
// turn; the first model of `base.models` (or the settings' model) is used. Streaming is not used: a turn is a buffered call.
Result<LoopOutcome> run_loop(const Services& services, ModelRequest base, const std::vector<Tool>& tools,
                             ToolHost& host, const LoopSettings& settings, const std::string& element,
                             const LoopHooks& hooks = {});

// The wire form of the tools in a dialect (what goes into the request's parameters) - exposed for previews and tests.
Json wire_tools(const LoopSettings& settings, const std::vector<Tool>& tools);

}  // namespace loom::llm::tools
