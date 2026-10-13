// Action catalogue reader. The data is the one catalogue data/runtime/actions.pack (schema
// loom.action_catalogue/1, keyed by id). loom_invoke lists it as ("actions","list"); the server
// registers its routes from action_table() (entries that carry "http").
#pragma once
#include "loom/util/json.h"

namespace loom {

// {"catalogue_schema","actions":[{id,kind,status,title,description,inputs[part kinds],outputs,requires[],
//   effects{reads_only,writes,sends_off_device,costs_money,irreversible,touches_private_data},
//   invocable entries also: port,action,labels{pl},params[{name,type,required,description{en,pl},has_default,
//   default?,edit}],http{verb,path,query?}}]}
const Json& action_catalogue();

// Route rows derived from the catalogue: {"port","action","verb","path","query":[{"name","always"}]}.
const Json& action_table();

}  // namespace loom
