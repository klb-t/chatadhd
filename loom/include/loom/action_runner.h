// loom/action_runner.h — the interpreter bound to a Runtime: the entry behind loom_invoke("actions", run |
// confirm | cancel | status | preview), the CLI `loom actions run`, and the chat router (R54).
//
// What it adds to the bare interpreter (action_run.h): the element of a request is resolved from a bare
// id (action_element.h); the executors are the real ones (use_model through llm::use_model and the usage
// guard, the invoke ports of the catalogue, the settings actions); runs are stored as events of the
// runtime's event log, so a paused run survives a restart; every step record is appended to the log;
// running runs have a cancel token so another thread (or request) can stop them.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

#include "loom/action_run.h"
#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::actions {

class ActionRunner {
 public:
  explicit ActionRunner(Runtime& rt);
  ~ActionRunner();
  ActionRunner(const ActionRunner&) = delete;
  ActionRunner& operator=(const ActionRunner&) = delete;

  // {action | chain, element?, scope?, args?, propose?, confirmed?, ref?, context?}. An element that is a
  // bare id is resolved; an action may name the argument that is its element (`element_from_arg`).
  // `cancel` is an optional external token (the CLI's); the runner registers its own otherwise.
  Result<Json> run(const Json& request, const CancelToken* cancel = nullptr);
  // {run_id, approved, receipt_id, ref?}
  Result<Json> confirm(const Json& request, const CancelToken* cancel = nullptr);
  // {run_id, ref?}: a paused run is cancelled; a running one is told to stop (answer: status "cancelling").
  Result<Json> cancel(const Json& request);
  // {run_id} -> the run document; {list: true, status?, conv_id?, limit?} -> {"runs":[summaries]}.
  Result<Json> status(const Json& request);
  // Like run, but nothing is stored or executed: plan, gates, cost, what needs confirmation.
  Result<Json> preview(const Json& request);

  // loom_invoke port "actions": run | confirm | cancel | status | preview, and the matching queries
  // (for | explain | propose_chain) of action_rules.h.
  Result<Json> invoke(std::string_view action, const Json& request);
  // Any invoke port that an atomic action can name in the catalogue (actions, storage).
  Result<Json> invoke_port(std::string_view port, std::string_view action, const Json& request);

  // The run store (for the chat router, which keeps the id of the run it proposed in the message).
  RunStore& store();

 private:
  struct Loaded;
  Result<Loaded> load() const;
  Result<RunRequest> prepare(const Loaded& loaded, const Json& request) const;
  Interpreter make_interpreter(const Loaded& loaded) const;
  void install_executors(ExecutorTable& table) const;

  Runtime& rt_;
  std::unique_ptr<RunStore> store_;
  std::mutex mu_;
  std::map<std::string, CancelToken, std::less<>> live_;  // runs executing right now
};

}  // namespace loom::actions
