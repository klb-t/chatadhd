#pragma once
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/media_providers.h"
namespace loom {
// Materializes Source/Unit/Observation as graph nodes; no new DB schema.
// Observation quotes address the derived JSON source; original pixel regions
// are carried separately. OCR is never asserted to be bytes of the PNG.
Result<Json> store_image_representation(Database& db, BlobStore& blobs, ProvenanceStore& provenance,
                                         const std::string& source_id, const OcrResult& ocr, const Json& preset);
}
