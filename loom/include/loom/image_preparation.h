#pragma once
#include "loom/media_providers.h"

namespace loom::chat {
// Transient snapshot only. Never put this object in a trace/log: bytes and OCR
// are private input. Preparing it cannot mutate a Source/Blob/graph store.
struct PreparedImage {
  std::string bytes, mime, name;
  OcrResult ocr;
  bool has_ocr = false;
  Json preset = Json::object();
  Json parts = Json::array();
  Json note = Json::object();
};
using PreparedImages = std::vector<PreparedImage>;
}
