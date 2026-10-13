// Action table: the single static description of actions reachable through loom_invoke(port, action, json),
// with the HTTP route each one is served on. The server registers routes from it, loom_invoke("core",
// "actions") returns it. Adding an action = one row here (+ its handler); defaults are presets, not rules.
#pragma once
#include "loom/util/json.h"

namespace loom {

// Rows: {"port","action","verb":"GET|POST","path","summary","input":{...notes},"query":[{"name","always"}]}
// GET rows build the request object from the named query parameters ("always": sent even when absent,
// as ""); POST rows take the JSON body as the request object.
const Json& action_table();

}  // namespace loom
