#pragma once

#include <filesystem>
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::chat {
// Internal adapter; no new public C ABI. Paths/limits come from trusted reader
// configuration, never from the document. Supported on Linux with glibc >= 2.34.
// Shared bounded subprocess. Configuration/arguments must be installation-owned.
Result<Json> read_local_json(const std::filesystem::path& path, const Json& reader, const Json& arguments);
// Host-created structured input; same bounded subprocess, no document path.
Result<Json> read_local_json_bytes(std::string_view bytes, const Json& reader, const Json& arguments);
Result<Json> read_docx_basic(const std::filesystem::path& path, const Json& reader);
}
