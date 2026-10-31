#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "loom/util/json.h"
#include "loom/result.h"

namespace loom::chat {

struct DocumentText {
  std::string text;
  int pages = 0;
  std::vector<int> pages_without_text;  // one-based source page numbers
};

// Local derived text only. The source document is never changed or uploaded by the reader.
// Error messages are diagnostic keys, rendered by the attachment recipe at the chat boundary.
Result<DocumentText> read_document_text(const std::filesystem::path& path, const Json& reader);

}  // namespace loom::chat
