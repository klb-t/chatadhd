#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "loom/result.h"

namespace loom::context::image_region {
// Internal adapter DTOs, not new persisted schemas. JSON decoding is in native.cpp.
struct Frame {
  std::string id;
  std::array<std::int64_t, 2> origin{}, extent{};
  std::array<std::string, 2> axes{}, units{};
  std::string orientation;
};
struct Region {
  std::string id, frame, text;
  std::array<std::int64_t, 2> origin{}, size{};
};
struct Source {
  std::string id, hash;
  std::int64_t size = 0;
};
struct Selection {
  std::string node_text, unit_source, locator_source, pointer;
  std::string observation_source, observation_pointer, observation_text;
  std::int64_t ordinal = -1;
  std::string original_source, original_hash;
  std::string evidence;
  bool confidence_is_null = false;
};
struct Document {
  std::string schema, status, original_hash, evidence;
  bool confidence_is_null = false;
  std::vector<Region> regions;
  std::vector<Frame> frames;
};
struct Validated {
  Region region;
  Frame frame;
  std::string text; // exact UTF-8 prefix of this region only
  std::size_t codepoints = 0;
};
// Caller decodes Document from the same bytes passed here, and verifies the
// original blob through its authorized BlobStore. No filesystem discovery,
// OCR invocation, graph expansion or independent context budget lives here.
Result<Validated> validate(const Selection&, const Source& representation,
    const Source& original, const Document&, std::string_view representation_bytes,
    std::size_t detail_chars);
} // namespace loom::context::image_region
