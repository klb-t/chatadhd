#pragma once

#include "../docx_projection.h"
#include "loom/model.h"
#include "loom/provenance.h"
#include <filesystem>
namespace loom { struct Node; }

namespace loom::text::sources::pdf {
// Internal adapter only; the shared projection envelope has no DOCX semantics.
using Prepared = docx::Prepared;
Result<Prepared> prepare(Runtime& rt, const std::string& hash);
Status verify_node(const Node&, const std::filesystem::path& original);
Status validate(const Json& extraction, const std::string& original_hash, std::int64_t original_size);
Result<std::string> record_source(Database& db, const std::string& title, const Prepared& document);
Status validate_derivation(Database& db, const SourceRecord& source, const Json& metadata);
Status replay(Runtime& rt, const Json& extraction, const Json& derivation);
// Add page and inferred chapter Units; existing source_parts owns observations,
// occurrences, genesis and their receipts. Called inside its transaction.
Result<Json> materialize_structure(Runtime& rt, const Json& result, const Prepared& document,
                                   const Json& recipe, bool recognized_blocks);
}  // namespace loom::text::sources::pdf
