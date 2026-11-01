#pragma once
#include "loom/result.h"
#include "loom/util/json.h"
namespace loom { class Runtime; }
namespace loom::text::sources::video_host {
Result<Json> materialize(Runtime&, const Json&);
Result<Json> read(Runtime&, const Json&);
}
