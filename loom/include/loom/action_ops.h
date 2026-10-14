// loom/action_ops.h — the operations of the application as actions the conversation can reach (P15, R54).
//
// Everything the user can do from a panel is an action of the catalogue (runtime/actions.pack) with phrases in both languages, so it can be
// said in the conversation at any level of abstraction: search, the graph around a name, the memory, conversations and messages, import and
// export, storage backends, the model or provider of a job, usage and its growth limit, the background work, the log, the state of the
// application, and the two answers that make the rest discoverable: what can be done here (help.can_do) and how to do something without
// doing it (help.how_to). Each is a thin executor over the engine that already does the work (the same calls the C ABI makes); what it says
// back is written from texts in runtime/chat_ops.pack, in the language of the run. Nothing here is a limit: every number (how many results,
// which words stand for a period or a role) is that profile's preset, and the user's overlay <data>/profiles/chat_ops.pack changes it.
//
// The executors never take a credential: storage.connect writes the part of a backend that is not secret into the overlay
// profiles/storage_security.pack and says which secrets to enter in Settings; the rule deny:settings.set:secret stays as it is.
#pragma once

#include <string>
#include <string_view>

#include "loom/action_run.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::actions {

// Registers the executor of every operation below under its action id.
void install_chat_ops(ExecutorTable& table, Runtime& rt);

// The ids of the actions install_chat_ops registers (for the coverage audit and the tests).
const std::vector<std::string>& chat_op_ids();

}  // namespace loom::actions
