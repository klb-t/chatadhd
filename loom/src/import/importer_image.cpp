#include "loom/importer.h"
#include "loom/media_providers.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "import/image_representation.h"

namespace loom {
Result<std::vector<Conversation>> ConversationImporter::screenshot_body(const std::filesystem::path& path,
                                                                        const ImportOptions& opts) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("image_input", db_.path().parent_path()));
  const auto& preset = profile.values();
  // Legacy role reconstruction is an explicit interpretation choice, never a
  // consequence of OCR text length or a document containing 'Assistant:'.
  if (preset.at("interpretation") == "conversation") return legacy_screenshot_body(path, opts);
  const auto declared = current_source_ && !current_source_->source_filename.empty()
      ? std::filesystem::path(current_source_->source_filename) : path;
  std::string format = utf8::to_lower(declared.extension().string());
  if (!format.empty()) format.erase(0, 1);
  LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(path));
  Result<OcrResult> result = media_
      ? media_->ocr_local_bytes(bytes, format, preset.at("local").at("name").get<std::string>())
      : Result<OcrResult>(Error(Errc::Unavailable, "ocr_provider_unavailable"));
  OcrResult ocr;
  if (result) ocr = std::move(*result);
  else {
    ocr.representation = Json{{"schema", "loom.ocr_regions/1"}, {"status", "unavailable"},
        {"reason", "ocr_provider_unavailable"}, {"evidence_class", "absent"}, {"text", ""}, {"lines", Json::array()},
        {"source_sha256", Sha256::hex(bytes)}, {"regions", Json::array()}, {"frames", Json::array()},
        {"method", {{"id", preset.at("local").at("name")}, {"version", nullptr}}}, {"confidence", nullptr}};
  }
  Json projection = ocr.representation;
  if (blobs_ && prov_ && current_source_ && !current_source_->source_id.empty()) {
    LOOM_TRY_ASSIGN(projection, store_image_representation(db_, *blobs_, *prov_, current_source_->source_id, ocr, preset));
  }
  const auto status = json::get_string(ocr.representation, "status");
  const auto& messages = preset.at("messages").at(preset.at("language").get<std::string>());
  BatchMessage message;
  message.role = preset.at("document_role").get<std::string>();
  // User is the transport carrier of an attached document, not its author.
  message.text = status == "recognized" ? ocr.text : messages.at(status).get<std::string>();
  message.metadata = Json{{"image_representation", projection}, {"source_content", true},
                          {"role_basis", "attachment_carrier"}, {"speaker", nullptr}};
  message.attachments.push_back(current_source_ && !current_source_->blob_hash.empty()
      ? "blob:" + current_source_->blob_hash + "/" + declared.filename().string() : path.string());
  LOOM_TRY_ASSIGN(auto conversation, finish_import(opts.title.value_or(declared.filename().string()),
                                                   {std::move(message)}, "screenshot_document"));
  return std::vector<Conversation>{std::move(conversation)};
}
}
