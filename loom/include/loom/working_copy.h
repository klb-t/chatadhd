// R103 B1b (BEZ_LOGOWANIA_R103 §6.5): the whole working copy of a data directory as one versioned bundle, so the person's
// history, graph, memory and settings can live on HIS side (browser store, file, own cloud) and come back exactly.
//
// What goes in is an ALLOW-LIST (never a deny-list): rows of the listed tables of chatadhd.db (preset: conversations,
// messages incl. versions/status/semantic_status, nodes and links with metadata - analysis results, pins (`pinned_on`
// links), trash marks in metadata -, message annotations, sources, provenance, relation types), memory.json and
// config.json. secrets.json, the in-memory credential overlay (keys of the pool, a Vertex access token), session
// cookies and every other file of the directory are not read at all. A config entry whose key names a secret
// (`api_key`, `*_token`, ...) or whose value carries a secret value known to this runtime is left out and named in
// `excluded`, so a key pasted into a setting by mistake never leaves either.
//
// Import restores the rows (INSERT OR REPLACE by primary key; mode "replace" first empties the listed tables), writes
// memory.json and the config, reloads both and brings the search index up to date. It never runs analysis and never
// calls a model: rows are written directly, no MSG_CREATED is emitted, semantic_status stays as it was.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Runtime;

namespace working_copy {

inline constexpr std::string_view kSchema = "loom.guest_bundle/2";

// Preset allow-list of tables (an option `tables` replaces it; a name that is not a table of the database is skipped).
const std::vector<std::string>& default_tables();

// options: {tables?: [..], include_memory?: true, include_config?: true}
// -> {schema, saved_at, producer, content:{tables:{name:{columns:[..], rows:[[..]]}}, memory, config},
//     integrity:{algorithm:"sha256", content_sha256}, excluded:{config_keys:[..]}}
Result<Json> export_bundle(Runtime& rt, const Json& options = Json::object());

// options: {mode?: "merge"|"replace" (preset "merge"), keep_config_keys?: [..] (current values win),
//           verify?: true (a missing or wrong integrity hash is refused)}
// -> {schema, restored:{tables:{name:rows}, memory_nodes, config_keys}, analysis_runs: 0}
Result<Json> import_bundle(Runtime& rt, const Json& bundle, const Json& options = Json::object());

// sha256 of the canonical dump of `content` (what `integrity.content_sha256` holds).
std::string content_sha256(const Json& content);

}  // namespace working_copy
}  // namespace loom
