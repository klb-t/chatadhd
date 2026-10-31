#pragma once

#include <string_view>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom { class Runtime; class Database; struct Node; }
namespace loom::text::sources {

// Explicit local import of an existing client blob. source_key is caller identity,
// hash is an immutable version; neither a filename nor a normalized fingerprint
// establishes identity. request: hash, source_key, name, format; optional language,
// level, pack (segmentation overlay), previous_source_id (explicit version edge).
// Returns source/unit/version/occurrence graph references. No model or file parser.
Result<Json> materialize(Runtime& rt, const Json& request);

// Read one occurrence against its immutable original, checking hash and byte range.
// request: node_id. The ordinary client blob raw/read path reads the full original.
Result<Json> read(Runtime& rt, const Json& request);

// Metadata/content integrity for the graph selector (no blob access or writes).
Status validate_occurrence(Database& db, const Node& node);

inline constexpr std::string_view kOccurrenceSchema = "loom.source_text_occurrence/1";

}  // namespace loom::text::sources
