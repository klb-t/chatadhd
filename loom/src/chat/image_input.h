#pragma once
#include "loom/image_preparation.h"
#include "loom/provenance.h"
#include "loom/db.h"
namespace loom::chat {
Result<Json> image_pipeline(std::string_view prompt, const Json& preset, std::string_view goal = "");
// Preparation has no storage handles. OCR runs locally against captured bytes.
Result<PreparedImage> prepare_image_attachment(MediaProviders& media,
    const std::filesystem::path& path, const std::filesystem::path& shown,
    std::string_view prompt, const Json& preset, std::string_view goal = "");
// Explicit commit of the very snapshot inspected by the gates and sent.
Result<Json> persist_image_attachment(Database& db, BlobStore& blobs, ProvenanceStore& prov,
                                      const PreparedImage& image);
// Additional check of newly introduced OCR, using the existing secret policy.
// Consent is bound to the current question and selected image/OCR payload snapshots.
Result<Json> image_secret_gate(const Json& recipe, const std::string& mode, const Json& store_values,
    const Json& confirmation, const Json& messages, const PreparedImages& images);
}
