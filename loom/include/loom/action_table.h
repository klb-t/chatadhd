// Action catalogue reader. The data is the one catalogue data/runtime/actions.pack (schema
// loom.action_catalogue/1, keyed by id). loom_invoke lists it as ("actions","list"); the server
// registers its routes from action_table() (entries that carry "http").
#pragma once
#include <filesystem>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

// {"catalogue_schema","actions":[{id,kind,status,title,description,inputs[part kinds],outputs,requires[],
//   effects{reads_only,writes,sends_off_device,costs_money,irreversible,touches_private_data},
//   invocable entries also: port,action,labels{pl},params[{name,type,required,description{en,pl},has_default,
//   default?,edit}],http{verb,path,query?}}]}
const Json& action_catalogue();

// The same list with the user overlay <data_root>/profiles/actions.pack applied (edited entries, added entries,
// entries with "hidden": true omitted). loom_invoke("actions","list") uses this. The server's route table
// (action_table) stays derived from the builtin catalogue at startup: routes are code wiring, not user data.
Result<Json> action_catalogue(const std::filesystem::path& data_root);

// action_catalogue(data_root) as a ready C-ABI answer: the list, or {"error":{"code","message"}} when the overlay is broken.
Json action_list(const std::filesystem::path& data_root);

// Route rows derived from the catalogue: {"port","action","verb","path","query":[{"name","always"}]}.
const Json& action_table();

}  // namespace loom
