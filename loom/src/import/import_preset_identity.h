#pragma once

#include "loom/importer.h"

namespace loom {

// Canonical effective caller values. Projection identity includes only the two
// values that change retained/inferred results; resource values permit resume.
Json import_preset_values(const ImportOptions& options);
std::string import_preset_hash(const ImportOptions& options);
std::string import_projection_hash(const ImportOptions& options);

// Compare with historical compatibility tokens in the compiled data resource,
// independently of what a future version chooses as its current default.
bool import_preset_is_legacy(const ImportOptions& options);
bool import_projection_is_legacy(const ImportOptions& options);

}  // namespace loom
