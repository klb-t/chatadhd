// loom/storage_command.h — JSON command surface of the storage plugins, shared by the C ABI
// (reached through the existing loom_github_sync JSON entry as "storage.*" actions, so libloom
// exports nothing new), the HTTP server and the CLI.
#pragma once

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
namespace storage {

// request: {"action": "storage.status|probe|kinds|placement|locate|copy|move|backup", ...}
//   placement {category?}            -> placement of one or all categories
//   locate {hash}                    -> where the graph says an object is held
//   copy|move {category,to,delete_source?,dry_run?}   (delete_source only with move)
//   backup {to, what:"database"}
// Secrets are never part of any result.
bool is_storage_action(std::string_view action) noexcept;
Result<Json> run_command(Runtime& rt, const Json& request);

}  // namespace storage
}  // namespace loom
