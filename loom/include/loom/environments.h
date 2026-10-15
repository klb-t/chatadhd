// loom/environments.h — environments, capabilities and accesses as nodes of the knowledge graph
// (PRZEGLAD_STRAZNIK_SRODOWISKA §3.1, S2/R32).
//
//   environment  node  `environment:<kind>:<id>`            class runtime_environment | client_environment |
//                                                            directory_environment | storage_environment |
//                                                            service_environment
//   capability   node  `capability:<kind>:<id>:<what>`      class capability  --provided_by--> port
//                                                            (metadata.element_class = the class of the thing that does it)
//   access       node  `access:<kind>:<id>:<how>`           class access      --stored_in--> secret_vault
//   vault        node  `vault:secrets`                      class secret_vault (names the file, never reads it)
//
//   environment --has_capability--> capability --requires_access--> access
//   environment --accessed_via--> access
//   storage environment --hosted_on--> runtime / directory environment
//
// Where the data comes from (nothing here is a closed list; the shapes are data/graph/environments.pack, in the
// graph_entry profile under /environments, with the owner's overlay on top):
//   runtimes     data/graph/devices.pack platforms (web, android, server, cli) + the device availability per platform
//   ports        the adapter registries (asr, ocr, storage, batch), written through port_graph
//   directories  the data directory always, any other directory when asked about (a probe of the operating system)
//   storages     the `storages` of the storage_security profile that are configured
//   services     the `services` of the pack (an account at a provider) and the credentials declared in settings_fields
//
// An access holds a REFERENCE to the secrets vault (a key name and whether the key is set), never the bytes: the
// code asks Secrets::has() and Secrets::keys() and nothing else, and every node, link and answer goes through
// port_graph::redact(). A test plants canary secrets and looks for them in every table and answer.
//
// The question "what can I do on my phone / in this directory?" (can_do) is answered FROM THE GRAPH: the environment
// is found among the nodes, its capabilities and accesses are read through its links, and for the class of the
// environment and the class each capability points at the action engine (actions.for) says which actions of the
// catalogue fit, with their effect flags and whether the rules allow, ask or deny.
#pragma once

#include <string>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::environments {

inline constexpr const char* kEnvironmentSchema = "loom.environment/1";
inline constexpr const char* kCapabilitySchema = "loom.capability/1";
inline constexpr const char* kAccessSchema = "loom.access/1";
inline constexpr const char* kSyncSchema = "loom.environments_sync/1";
inline constexpr const char* kListSchema = "loom.environments_list/1";
inline constexpr const char* kCanDoSchema = "loom.environment_can_do/1";

// Brings the nodes up to date from their sources. request: {path?: "<directory>"} adds that directory as an
// environment. Returns {schema, environments:[{id, node, kind, class, label, capabilities, accesses}], removed}.
Result<Json> sync(Runtime& rt, const Json& request = Json::object());

// The environments in the graph: {schema, environments:[{id, node, kind, class, label, current, capabilities:[...],
// accesses:[...]}]}. request: {sync?: bool} (default: the setting environments_auto_sync).
Result<Json> list(Runtime& rt, const Json& request = Json::object());

// What can be done in one environment. request: {environment?: "<node id | platform | alias | label>", path?:
// "<directory>", lang?: "en"|"pl", include_unmatched?: bool, sync?: bool}. Without environment and path: the runtime
// this process is. Answer: {schema, environment, capabilities, accesses, actions:[{action, title, implementation,
// status, effects, via:[...], needs_access?}], counts, issues}.
Result<Json> can_do(Runtime& rt, const Json& request = Json::object());

}  // namespace loom::environments
