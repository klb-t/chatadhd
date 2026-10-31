#pragma once
#include <string>
#include <string_view>
#include "loom/result.h"
#include "loom/util/json.h"
namespace loom { class Database; }
namespace loom::chat {
// Register only an already stored upload whose blob hash matches the extraction.
// Repeated extraction/reopen reuses the same immutable raw source.
Result<std::string> record_docx_source(Database& db, std::string_view attachment,
                                       std::string_view title, const Json& extraction,
                                       const Json& reader);
}
