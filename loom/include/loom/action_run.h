// loom/action_run.h — running actions: the interpreter of composite actions (GRAF_HUB_KOD_SILNIK §2, §5, §9).
//
// One kind of entity: an action is atomic (an executor does it) or composite (steps of other actions,
// as data in data/runtime/actions.pack). The interpreter runs a composite's steps, or a chain that
// propose_chain found, with the same machinery:
//
//   * steps run in order; a step may carry `variants` (run several alternatives; mode `all` returns every
//     result, `first_success` stops at the first that works) and a `when` condition on the traits of the
//     element or on the results of earlier steps (trait / step status / output kind / argument / not /
//     all / any);
//   * typed parts (llm::Part: text, json, image, audio, video, embedding, file_ref) flow between steps:
//     a step takes `inputs: [{"from": "input" | <step id>, "variant"?}]` (default: the previous step's
//     output, the element's content for the first) and its `with` may use {"$arg": name} and
//     {"$text": <step id>} placeholders;
//   * every action of the plan goes through the rules (Engine::decide, plus the rules of the names an
//     action lists in `gate_as`): deny stops the run before anything is executed, with the stated
//     reason and initiator; confirm pauses the run with a receipt (never silently); the usage guard's
//     x10 hold pauses it with the guard's receipt. A paused run is durable: it is stored through the
//     RunStore and resumed by `confirm` after any amount of time or a restart;
//   * every step leaves a record (loom.action_step/1: action, element, decision, hashes of inputs and
//     outputs, the provenance of the model calls it made); the whole run is a document
//     (loom.action_run/1) you can read back;
//   * a run can be cancelled: between steps, and inside a model call through the cancel token.
//
// Mechanism vs data: the interpreter knows how to run steps and what the conditions mean; which actions
// have which steps, prompts, models and variants is data. Executors (the code behind atomic actions)
// are registered per action id by the caller, so the interpreter is testable without a runtime.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loom/action_rules.h"
#include "loom/result.h"
#include "loom/use_model.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {
class EventLog;
}

namespace loom::actions {

inline constexpr std::string_view kRunSchema = "loom.action_run/1";
inline constexpr std::string_view kStepSchema = "loom.action_step/1";
inline constexpr std::string_view kPendingSchema = "loom.action_pending/1";

// ── Executors: the code behind atomic actions ───────────────────────

// What an executor is given for one step (one variant of it).
struct StepCall {
  std::string run_id;
  std::string step_id;
  std::string action;          // the atomic action to carry out
  std::string root_action;     // the action the run was started for (cohort / provenance)
  std::string variant;         // label of the variant ("" = none)
  Json with = Json::object();  // placeholders substituted
  std::vector<llm::Part> inputs;
  Json element = Json::object();
  Json args = Json::object();
  Json usage_confirmation = Json::object();  // set when resuming this step after the usage guard's hold
  const CancelToken* cancel = nullptr;
};

struct StepOutcome {
  enum class State { Ok, Failed, Held, Cancelled, Unavailable };
  State state = State::Failed;
  std::vector<llm::Part> parts;  // typed outputs
  Json data = nullptr;           // structured result, when the action has one
  Json provenance = Json::array();  // records of the calls made (loom.model_use/1 ...), no secrets
  Json hold = nullptr;           // Held: {"schema":"loom.action_usage_pending/1", status, operation_id, receipt_id, ...}
  std::optional<Error> error;    // Failed / Unavailable / Cancelled
};

using Executor = std::function<StepOutcome(const StepCall&)>;

class ExecutorTable {
 public:
  void add(std::string action, Executor executor) { by_action_[std::move(action)] = std::move(executor); }
  // Executor for catalogue entries that carry port + action (storage.*, actions.*): `fallback` handles them.
  void set_port_executor(Executor executor) { port_ = std::move(executor); }
  const Executor* find(std::string_view action, const Json& entry) const;

 private:
  std::map<std::string, Executor, std::less<>> by_action_;
  Executor port_;
};

// ── Run storage ─────────────────────────────────────────────────────

class RunStore {
 public:
  virtual ~RunStore() = default;
  virtual Status save(const Json& run) = 0;                       // a checkpoint of the whole document
  virtual Result<std::optional<Json>> load(std::string_view run_id) = 0;
  // Latest document of each run, newest first. filter: {"status"?, "conv_id"?, "limit"?}.
  virtual Result<std::vector<Json>> list(const Json& filter) = 0;
};
std::unique_ptr<RunStore> make_memory_run_store();
// Checkpoints are append-only events of type "action.run" (subject = run id): the audit trail and the
// durable state in one place.
std::unique_ptr<RunStore> make_event_log_run_store(EventLog& log);

// ── The interpreter ─────────────────────────────────────────────────

struct RunRequest {
  std::string action;            // catalogue action to run ...
  Json chain = nullptr;          // ... or a chain: [{"action", "with"?}, ...] / a propose_chain chain object
  Json element = Json::object(); // the resolved element {id, class, traits, attrs, source?}
  Json scope = nullptr;          // rules scope (level id / {level: instance})
  Json args = Json::object();    // arguments of the action ({"$arg": name} in the steps)
  Json context = Json::object(); // who asked and where (conv_id, message_id, ref, text ...): stored, not interpreted
  bool propose = false;          // store the plan and wait for the user's go; execute nothing
  bool confirmed = false;        // the caller accepts the rule confirmations of this plan up front
  std::string ref;               // who/what confirms (web, cli, chat:<message id> ...)
  std::string run_id;            // optional: assigned by the caller (to register a cancel token first)
  static Result<RunRequest> from_json(const Json& j);
};

struct InterpreterDeps {
  const Engine* engine = nullptr;
  const ExecutorTable* executors = nullptr;
  RunStore* store = nullptr;
  // Content of the element as typed parts (the `input` of the first step).
  std::function<Result<std::vector<llm::Part>>(const Json& element)> element_content;
  // Receives each step record (loom.action_step/1) as it is written: the audit trail.
  std::function<void(const Json& record)> on_step;
  Json settings = Json::object();  // runtime profile action_run (max_depth, ...)
};

class Interpreter {
 public:
  explicit Interpreter(InterpreterDeps deps) : deps_(std::move(deps)) {}

  // Plan, gate, and (unless proposing or paused) execute. The returned run document tells where it
  // stands: completed | failed | denied | proposed | awaiting_confirmation | awaiting_usage | cancelled.
  Result<Json> start(const RunRequest& request, const CancelToken* cancel = nullptr);

  // Answer the pending question of a run: {"approved": bool, "receipt_id": str, "ref"?: str}.
  // The receipt must be the pending one (a stale or invented receipt is refused).
  Result<Json> confirm(std::string_view run_id, const Json& answer, const CancelToken* cancel = nullptr);

  // Stop a run: a paused / proposed run becomes `cancelled`; a running one stops at the next step
  // boundary (or inside a model call, through its token).
  Result<Json> cancel(std::string_view run_id, std::string_view ref = "");

  Result<Json> get(std::string_view run_id) const;

  // The plan of a request without storing or running anything: steps, gates, cost estimate, effects.
  Result<Json> preview(const RunRequest& request) const;

 private:
  struct Claim;
  Result<Json> plan_of(const RunRequest& request) const;
  Result<Json> gate_plan(const Json& plan, const Json& element, const Json& scope,
                         const std::set<std::string>& base_traits) const;
  Result<std::set<std::string>> element_traits(const Json& element, const Json& scope) const;
  Result<Json> drive(Json run, const CancelToken* cancel, const Json& usage_confirmation);
  Result<Json> run_step(Json& run, std::size_t index, const CancelToken* cancel, const Json& usage_confirmation);
  Result<std::vector<llm::Part>> inputs_of(const Json& run, const Json& step) const;

  InterpreterDeps deps_;
  mutable std::mutex mu_;
  std::set<std::string, std::less<>> claimed_;  // runs being executed right now
};

// The one-line summary of a run for answers and conversation messages.
Json run_summary(const Json& run);

}  // namespace loom::actions
