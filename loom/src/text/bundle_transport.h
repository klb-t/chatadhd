#pragma once
#include "loom/result.h"
#include "loom/util/json.h"
namespace loom { class Runtime; }
namespace loom::text::sources { Result<Json> bundle_transport(Runtime&, const Json&); }
