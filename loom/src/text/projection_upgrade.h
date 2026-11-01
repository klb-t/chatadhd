#pragma once
#include "loom/result.h"
#include "loom/util/json.h"
namespace loom { class Runtime; }
namespace loom::text::sources {
// Explicit operations on an existing immutable projection; never rewrites selectors.
Result<Json> projection_upgrade(Runtime&, const Json&);
}
