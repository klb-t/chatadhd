// Action catalogue reader. The data lives in data/runtime/actions_storage.pack (schema
// loom.action_catalogue/1 entries; TODO(S2-R3): merge into actions.pack after P7 lands). loom_invoke
// lists it as ("actions","list"); the server registers its routes from action_table().
#pragma once
#include "loom/util/json.h"

namespace loom {

// {"catalogue_schema","actions":[{id,port,action,kind,status,description{en,pl},inputs[{name,type,required,
//   description,has_default,default?,edit}],outputs,required_traits,effects[],http{verb,path,query?}}]}
const Json& action_catalogue();

// Route rows derived from the catalogue: {"port","action","verb","path","query":[{"name","always"}]}.
const Json& action_table();

}  // namespace loom
