#pragma once

#include "loom/result.h"
#include "loom/util/json.h"
#include <string>

namespace loom { class Runtime; }
namespace loom::text::sources::docx {
// Internal bridge. No document-supplied metadata chooses executable paths,
// coordinate semantics or validation rules.
struct Prepared {
  Json extraction, reader, derivation;
  std::string script;
};
Status validate(const Json& extraction, const std::string& original_hash, std::int64_t original_size);
Result<Prepared> prepare(Runtime& rt, const std::string& hash);
}  // namespace loom::text::sources::docx
