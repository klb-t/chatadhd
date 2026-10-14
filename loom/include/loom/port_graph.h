// loom/port_graph.h — adapters, role choices and port runs as nodes of the knowledge graph (S2/R12,
// B-MOD-09; GRAF_HUB_KOD_SILNIK §4).
//
//   port         node  `port:<name>`                      class `port`
//   adapter      node  `adapter:<port>:<name>`            class `adapter`   --implements--> port
//   role_choice  node  `role_choice:<role>:<scope>:..`    class `role_choice` --chose--> adapter(s)
//   port_run     node  `port_run:<operation id>`          class `port_run`  --produced_by--> adapter
//                                                           --derived_from--> inputs
//                                                           --part_of--> action_run (when inside a run)
//
// Written through the one graph write path (Database nodes/links tables, get_or_create_node: the label is
// the identity, so every call is idempotent). Provenance is consistent with the model_use rows: a port run
// carries the same `operation_id` as the provenance record (subject_kind "model_use", subject_id = operation
// id, or request_sha256 when there is none), so the two join on it.
//
// Secrets are references only: a secret key NAME may appear (`secret_key`, `secret_ref`), a value never. Every
// Json that enters a node goes through redact(): keys that look like credentials lose their value.
//
// The action run: the node of an action run is `run:<run_id>` of class `action_run` (src/actions/result.cpp,
// S1). A port run inside an action run needs the run id; code that executes inside a run opens an
// ActionRunScope (thread-local, additive, no S1 file touched) or sets PortRun::action_run_id. The part_of
// link is made when the run node exists; otherwise the id stays in the port run's metadata and
// reconcile_action_runs() links it once the run node has been written.
#pragma once

#include <string>
#include <vector>

#include "loom/adapter_registry.h"
#include "loom/db.h"
#include "loom/port_profile.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::port_graph {

inline constexpr const char* kAdapterSchema = "loom.port_adapter/1";
inline constexpr const char* kRoleChoiceSchema = "loom.role_choice/1";
inline constexpr const char* kPortRunSchema = "loom.port_run/1";

// Keeps references, drops secret values. Keys naming a credential (secret, token, password, api key,
// authorization, bearer, credential) keep a string value only under the reference keys `secret_key`,
// `secret_ref`, `credential_ref`; everything else becomes "[redacted]".
Json redact(const Json& value);

// The action run the current thread executes in (empty = none). RAII; nests.
class ActionRunScope {
 public:
  explicit ActionRunScope(std::string run_id);
  ~ActionRunScope();
  ActionRunScope(const ActionRunScope&) = delete;
  ActionRunScope& operator=(const ActionRunScope&) = delete;
  static std::string current();

 private:
  std::string previous_;
};

struct AdapterInfo {
  std::string name;
  std::string status = "implemented";
  Json meta = Json::object();
};

template <class Port, class Env>
std::vector<AdapterInfo> adapters_of(const AdapterRegistry<Port, Env>& registry) {
  std::vector<AdapterInfo> out;
  for (const auto& e : registry.entries()) out.push_back({e.name, e.status, e.meta});
  return out;
}

Result<std::string> port_node(Database& db, const std::string& port);
// One node per adapter + `implements` -> the port node. Returns the adapter node ids in order.
Result<std::vector<std::string>> record_adapters(Database& db, const std::string& port, const std::vector<AdapterInfo>& adapters);
Result<std::string> adapter_node(Database& db, const std::string& port, const AdapterInfo& adapter);

// `resolved` = PortProfile::resolve() output {role, defined, adapter|compose, params, scope, scope_id?, source, file?}.
// `port` defaults to the first segment of the role ("asr", "ocr", "chat" ...). An undefined role returns "".
Result<std::string> record_role_choice(Database& db, const Json& resolved, const std::string& port = {});
// Every role of the profile, resolved at person level (or in `conversation`).
Result<std::vector<std::string>> record_role_choices(Database& db, const PortProfile& profile, const Json& conversation = Json::object(),
                                                     const std::string& conversation_id = {}, const Json& settings = Json::object());

struct PortRun {
  std::string port;                    // asr | ocr | storage | llm ...
  std::string role;                    // role of the ports profile, when known
  std::string kind;                    // "media.asr", "storage.put", "ensemble" ...
  std::string adapter;                 // the adapter that answered ("" for an ensemble: see `ensemble`)
  std::string operation_id;            // joins the model_use provenance row
  std::string request_sha256;
  std::string status = "ok";           // ok | failed | degraded | held ...
  std::string error;                   // message, never a secret
  std::string model, provider;
  std::string action_run_id;           // default: ActionRunScope::current()
  std::string role_choice_node;        // optional: link `chose`-side provenance (role_choice --used_by--> run)
  std::vector<std::string> inputs;     // node ids the run read (derived_from)
  Json ensemble = nullptr;             // compose::EnsembleOutcome::to_json(): {produced_by, degraded}
  Json usage = nullptr;                // cost / tokens of the call
  Json extra = Json::object();
};
// Builds a PortRun from a "loom.model_use/1" record (what MediaProviders' provenance sink receives).
PortRun port_run_from_model_use(const Json& record);

// Creates the node, `produced_by` -> adapter node(s), `derived_from` -> inputs, `part_of` -> action_run.
// Returns the port_run node id.
Result<std::string> record_port_run(Database& db, const PortRun& run);

// Links port runs that name an action run (metadata.action_run_id) to its node when that node exists now.
// Returns how many links were made.
Result<int> reconcile_action_runs(Database& db);

}  // namespace loom::port_graph
