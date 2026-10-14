// loom/action_runner.h — the interpreter bound to a Runtime: the entry behind loom_invoke("actions", run |
// confirm | cancel | status | preview), the CLI `loom actions run`, and the chat router (R54).
//
// What it adds to the bare interpreter (action_run.h): the element of a request is resolved from a bare
// id (action_element.h); the executors are the real ones (use_model through llm::use_model and the usage
// guard, the invoke ports of the catalogue, the settings actions); runs are stored as events of the
// runtime's event log, so a paused run survives a restart; every step record is appended to the log;
// running runs have a cancel token so another thread (or request) can stop them.
#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

#include "loom/action_index.h"
#include "loom/action_run.h"
#include "loom/client_devices.h"
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

  // {action | chain, element?, scope?, args?, propose?, confirmed?, receipt_id?, ref?, context?}. An element that is a
  // bare id is resolved; an action may name the argument that is its element (`element_from_arg`).
  // `confirmed:true` skips the rules' confirmations only with `receipt_id` = the `plan_receipt` that preview() (or a proposal, `propose:true`)
  // gave for exactly this plan; a bare `confirmed:true` returns the proposal (status "proposed", `confirmation_refused`) instead of running.
  // The rules' denials and the usage guard (x10) still apply after the receipt.
  // `cancel` is an optional external token (the CLI's); the runner registers its own otherwise.
  Result<Json> run(const Json& request, const CancelToken* cancel = nullptr);
  // {run_id, approved, receipt_id, ref?}
  Result<Json> confirm(const Json& request, const CancelToken* cancel = nullptr);
  // {run_id, receipt_id, state, parts?, data?, error?, ref?}: the client's answer to the client_step a run is paused on
  // (client_devices.h). Media in `parts` come as data_base64 (stored in the blob store, with a graph node) or as the hash of a
  // blob that is already stored; text as text. The run continues with the next step.
  Result<Json> client_result(const Json& request, const CancelToken* cancel = nullptr);
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

  // The arguments with the ones the caller left out filled from the settings that the action's params name
  // (`params: [{name, setting}]` in the catalogue): the caller's word comes first, an unset setting binds nothing.
  Json bind_params(const Json& entry, Json args) const;

  // The clients (client_devices.h): client.declare {client_id?, client?, capabilities} -> the stored declaration;
  // client.capabilities {client_id?} -> the declaration, or the ids of the clients known. The bytes a client brings or takes are
  // client.blob_put | blob_path | blob_begin | blob_append | blob_commit | blob_read (client_blobs.h), reached through invoke().
  Result<Json> client_declare(const Json& request);
  Result<Json> client_capabilities(const Json& request);
  // The declaration the kernel holds for a client (null when unknown or expired) and what it changes in the catalogue
  // ({action id: patch}, empty for an unknown client): what the router needs to see the catalogue as that client does.
  Json client_declaration(std::string_view client_id);
  Json client_patches(std::string_view client_id);

  // The run store (for the chat router, which keeps the id of the run it proposed in the message).
  RunStore& store();
  // The incremental index of completed runs behind the detection of repeated chains and of the classes an action is used on
  // (action_index.h): detection after a command reads what changed since the last time, not the log.
  SequenceIndex& sequence_index();

 private:
  struct Loaded;
  Result<Loaded> load(std::string_view client_id = "");
  Result<Json> write_overlay(std::string_view action, const Json& request);
  Result<Json> run_gated(std::string_view action, const Json& request);
  Result<RunRequest> prepare(const Loaded& loaded, const Json& request) const;
  Interpreter make_interpreter(const Loaded& loaded) const;
  void install_executors(ExecutorTable& table);
  // The model registry is empty on a fresh data directory, so no price is known. When an estimate needs one, the provider's public model
  // list is fetched once (setting model_registry_autorefresh, profile action_run: estimate.registry_refresh). Returns why the registry is
  // still empty after the attempt (empty: it was not needed, not allowed, or it worked).
  std::string fill_registry_for_estimate(const Json& run_settings);

  Runtime& rt_;
  std::unique_ptr<RunStore> store_;
  SequenceIndex index_;
  ClientRegistry clients_;
  std::mutex mu_;
  std::map<std::string, CancelToken, std::less<>> live_;  // runs executing right now
  bool registry_tried_ = false;                           // the automatic fetch of the model list (guarded by mu_)
  std::chrono::steady_clock::time_point registry_tried_at_;
  std::string registry_failure_;
};

}  // namespace loom::actions
