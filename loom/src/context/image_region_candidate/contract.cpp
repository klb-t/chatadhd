#include "contract.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include <algorithm>

namespace loom::context::image_region {
namespace {
bool hash(std::string_view value) {
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
Error reject(const char* reason) { return Error(Errc::Conflict, reason); }
}
Result<Validated> validate(const Selection& s, const Source& rep, const Source& original,
    const Document& doc, std::string_view bytes, std::size_t detail_chars) {
  if (rep.id.empty() || original.id.empty() || rep.id == original.id ||
      !hash(rep.hash) || !hash(original.hash) || rep.size < 0 || original.size < 0 ||
      static_cast<std::uint64_t>(rep.size) != bytes.size() || Sha256::hex(bytes) != rep.hash)
    return reject("ocr_region_representation_integrity");
  if (s.unit_source != rep.id || s.locator_source != rep.id || s.observation_source != rep.id ||
      s.original_source != original.id || s.original_hash != original.hash || doc.original_hash != original.hash)
    return reject("ocr_region_source_mismatch");
  if (doc.schema != "loom.ocr_regions/1" || doc.status != "recognized" ||
      doc.evidence != "inferred" || s.evidence != "inferred" ||
      !doc.confidence_is_null || !s.confidence_is_null)
    return reject("ocr_region_evidence_mismatch");
  if (s.ordinal < 0 || static_cast<std::uint64_t>(s.ordinal) >= doc.regions.size())
    return reject("ocr_region_pointer");
  const auto pointer = "/regions/" + std::to_string(s.ordinal);
  if (s.pointer != pointer || s.observation_pointer != pointer + "/text")
    return reject("ocr_region_pointer");
  const auto& region = doc.regions[static_cast<std::size_t>(s.ordinal)];
  if (region.id.empty() || region.text.empty() || !utf8::is_valid(region.text) ||
      region.text != s.observation_text || region.text != s.node_text)
    return reject("ocr_region_text_mismatch");
  if (std::count_if(doc.regions.begin(), doc.regions.end(), [&](const Region& r) {
        return r.id == region.id;
      }) != 1)
    return reject("ocr_region_duplicate_id");
  const Frame* selected_frame = nullptr;
  for (const auto& f : doc.frames) if (f.id == region.frame) {
    if (selected_frame) return reject("ocr_region_ambiguous_frame");
    selected_frame = &f;
  }
  if (!selected_frame || region.frame.empty()) return reject("ocr_region_missing_frame");
  const auto& frame = *selected_frame;
  // This adapter consumes the existing 2D pixel/top-left OCR contract only.
  // Other frames need their own coordinate adapter, not guessed conversions.
  if (frame.axes != std::array<std::string, 2>{"x", "y"} ||
      frame.units != std::array<std::string, 2>{"px", "px"} ||
      frame.origin != std::array<std::int64_t, 2>{0, 0} || frame.orientation != "top_left")
    return reject("ocr_region_frame_coordinates");
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const auto start = region.origin[axis], size = region.size[axis], extent = frame.extent[axis];
    // Subtract before comparing: start + size could overflow.
    if (extent <= 0 || start < 0 || size <= 0 || size > extent || start > extent - size)
      return reject("ocr_region_bounds");
  }
  const auto text = detail_chars == 0 ? region.text : std::string(utf8::prefix(region.text, detail_chars));
  return Validated{region, frame, text, utf8::length(text)};
}
} // namespace loom::context::image_region
