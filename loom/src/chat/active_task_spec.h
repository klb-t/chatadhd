// Internal deterministic ActiveTaskSpec projection compiler. No new public ABI.
#pragma once

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::chat {

// Validate the supplied loom.active_task_spec/1 projection, then return a copy
// with compiled_instruction regenerated from active/contested statements. The
// incoming compiled text is never trusted as executable instruction content.
// This checks projection structure, not semantic fidelity or source identity.
// The caller binds conversation/source messages and retains the original input.
Result<Json> compile_active_task_spec(const Json& spec);

}  // namespace loom::chat
