#pragma once

#include "loom/context_engine.h"

namespace loom::context {
Status parse_context_extensions(const Json& value, ContextRequest& request);
void serialize_context_extensions(Json& value, const ContextRequest& request);
Status validate_context_extensions(const ContextRequest& request);
}
