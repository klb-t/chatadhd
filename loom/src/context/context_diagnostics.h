#pragma once

#include "loom/model.h"

namespace loom::context {
// Render coverage limitations as data alongside material. These are item
// selection diagnostics, not a judgement that a proposition is true/false.
std::string render_context_diagnostics(const model::ContextSet& set);
}
