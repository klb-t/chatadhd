#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "loom/image_preparation.h"
#include "loom/model.h"
#include "loom/provenance.h"
#include "loom/text_genesis.h"

namespace loom::video {

// Borrowed only during make_source_plan. Names are host display labels, never
// input paths. The caller pins receipt bytes captured from its trusted helper.
struct FrameBytes {
  std::string_view png;
  std::string_view display_name;
};
struct SourcePlanInput {
  std::string_view original_bytes, original_name, original_mime;
  std::span<const FrameBytes> frames;
  std::string_view receipt_bytes, receipt_sha256;
};

struct SourceSnapshot {
  SourceRecord source;
  std::string bytes;  // Exactly the validated bytes to put through existing BlobStore.
};
struct FrameSourcePlan {
  SourceSnapshot pixels;                 // Derived PNG Source, not an original screenshot.
  model::Unit unit;                     // Occurrence in immutable receipt JSON.
  model::Observation observation;       // Exact hash field; no OCR/pixel-text claim.
  std::vector<ProvenanceRecord> provenance;
  text::genesis::VersionText genesis;    // One independent metadata occurrence, not a movie revision.
  chat::PreparedImage image;            // No OCR, provider dispatch, or egress admission.
};
struct SourcePlan {
  SourceSnapshot original, receipt;
  model::Unit original_unit, receipt_unit;
  ProvenanceRecord receipt_provenance;
  Json method;
  std::vector<FrameSourcePlan> frames;
};

// Pure, all-or-error: no paths, database, services, callbacks or partial plan.
// Policy is trusted host data (policy.json preset); receipt limits cannot raise
// host admission limits. All returned byte carriers own their snapshots.
Result<SourcePlan> make_source_plan(const SourcePlanInput& input, const Json& policy);

}  // namespace loom::video
