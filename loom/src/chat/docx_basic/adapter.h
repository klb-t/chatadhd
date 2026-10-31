#pragma once

#include <filesystem>
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::chat {
// Internal adapter; no new public C ABI. Paths/limits come from trusted reader
// configuration, never from the document. Supported on Linux with glibc >= 2.34.
Result<Json> read_docx_basic(const std::filesystem::path& path, const Json& reader);
}
