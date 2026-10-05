#pragma once

#include <string_view>

#include "loom/importer.h"

namespace loom {

// Complete effective values only: missing/null/unknown fields are errors.
// No default merge occurs here, including after a layer disables a field.
Result<ImportPresetValues> import_preset_from_values(const Json& values);

// Validate the entire projection before changing any of the five fields.
// All remaining caller options and callbacks retain their previous values.
Status apply_import_preset_values(ImportOptions& options, const Json& values);

// Exact immutable bytes of the compiled "import" or "import_audit" resource.
// The returned view remains valid for the lifetime of the process.
Result<std::string_view> compiled_import_preset_source(std::string_view resource_id);

// Source identity/version/hash/raw bytes and validated import values for a
// common layer consumer. This API neither discovers nor stores user layers.
Result<Json> inspect_import_preset();

}  // namespace loom
