#include "chat/image_input.h"
#include "import/image_representation.h"
#include "chat/secret_filter.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include <algorithm>

namespace loom::chat {
Result<Json> image_pipeline(std::string_view prompt, const Json& preset, std::string_view goal) {
  std::string selected(goal);
  std::string basis = goal.empty() ? "preset" : "user_override";
  if (selected.empty()) {
    selected = prompt.empty() ? "no_prompt" : "default";
    const std::string lower = utf8::to_lower(prompt);
    std::size_t best = 0;
    for (auto it = preset.at("goal_cues").begin(); it != preset.at("goal_cues").end(); ++it)
      for (const auto& cue : it.value()) {
        const std::string value = utf8::to_lower(cue.get<std::string>());
        if (!value.empty() && value.size() > best && lower.find(value) != std::string::npos) {
          selected = it.key(); best = value.size(); basis = "cue_match";
        }
      }
  }
  const auto& routes = preset.at("routes");
  if (!routes.contains(selected)) return Error(Errc::InvalidArgument, "image_goal_unknown");
  const auto& steps = routes.at(selected);
  for (const auto& step : steps)
    if (step != "local_ocr" && step != "vision") return Error(Errc::Unsupported, "image_step_unknown");
  return Json{{"goal", selected}, {"basis", basis}, {"steps", steps}};
}
Result<PreparedImage> prepare_image_attachment(MediaProviders& media,
                               const std::filesystem::path& path, const std::filesystem::path& shown,
                               std::string_view prompt, const Json& preset, std::string_view goal) {
  LOOM_TRY_ASSIGN(auto plan, image_pipeline(prompt, preset, goal));
  LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(path));
  std::string ext = utf8::to_lower(shown.extension().string());
  if (!ext.empty()) ext.erase(0, 1);
  std::string mime = "image/" + (ext == "jpg" ? "jpeg" : ext);
  PreparedImage out;
  out.bytes = bytes; out.mime = mime; out.name = shown.filename().string(); out.preset = preset;
  out.note = plan;
  const auto hash = Sha256::hex(bytes);
  out.note["source_sha256"] = hash;
  for (const auto& step : plan.at("steps")) {
    if (step == "local_ocr") {
      auto result = media.ocr_local_bytes(bytes, ext, preset.at("local").at("name").get<std::string>());
      if (!result) {
        OcrResult absent;
        absent.representation = Json{{"schema", "loom.ocr_regions/1"}, {"status", "unavailable"},
            {"reason", "ocr_provider_unavailable"}, {"evidence_class", "absent"}, {"text", ""}, {"lines", Json::array()},
            {"source_sha256", hash}, {"regions", Json::array()}, {"frames", Json::array()},
            {"method", {{"id", preset.at("local").at("name")}, {"version", nullptr}}}, {"confidence", nullptr}};
        result = std::move(absent);
      }
      out.has_ocr = true;
      out.ocr = std::move(*result);
      out.note["ocr"] = Json{{"status", out.ocr.representation.at("status")},
          {"reason", out.ocr.representation.at("reason")}, {"method", out.ocr.representation.at("method")}};
      const auto& messages = preset.at("messages").at(preset.at("language").get<std::string>());
      const auto status = json::get_string(out.ocr.representation, "status");
      const std::string body = status == "recognized"
          ? messages.at("context_prefix").get<std::string>() + out.ocr.text
          : messages.at(status).get<std::string>();
      out.parts.push_back(Json{{"type", "text"}, {"text", body}});
    } else if (step == "vision") {
      out.note["vision"] = "requires_model_admission";
      out.parts.push_back(Json{{"type", "image_url"},
          {"image_url", {{"url", "data:" + mime + ";base64," + base64::encode(bytes)}}}});
    }
  }
  return out;
}
Result<Json> persist_image_attachment(Database& db, BlobStore& blobs, ProvenanceStore& prov,
                                      const PreparedImage& image) {
  LOOM_TRY_ASSIGN(auto blob, blobs.put(image.bytes, image.mime));
  SourceRecord source;
  source.id = "src_image_" + blob.hash;
  source.kind = image.preset.at("source_kinds").at("original").get<std::string>();
  source.uri = image.name; source.title = image.name;
  source.blob_hash = blob.hash; source.size = blob.size; source.mime = image.mime;
  source.format = "screenshot"; source.parser = "loom.image_input"; source.parser_version = "1";
  {
    auto lock = db.lock();
    LOOM_TRY_ASSIGN(auto prior, prov.get_source(source.id));
    if (!prior) { LOOM_TRY(prov.add_source(source)); }
  }
  if (image.has_ocr) return store_image_representation(db, blobs, prov, source.id, image.ocr, image.preset);
  return Json{{"source_id", source.id}, {"source_sha256", blob.hash}};
}
Result<Json> image_secret_gate(const Json& recipe, const std::string& mode, const Json& store_values,
    const Json& confirmation, const Json& messages, const PreparedImages& images) {
  if (!recipe.at("attachments").value("scan_text_files", true)) return Json(nullptr);
  std::vector<secrets::Source> sources;
  Json snapshots = Json::array();
  for (const auto& image : images) {
    if (!image.has_ocr) continue;
    sources.push_back({"file", image.name, image.ocr.text});
    snapshots.push_back(Json{{"source_sha256", Sha256::hex(image.bytes)},
                             {"ocr_sha256", Sha256::hex(image.ocr.representation.dump())},
                             {"parts_sha256", Sha256::hex(image.parts.dump())}});
  }
  if (sources.empty()) return Json(nullptr);
  const auto binding = Sha256::hex(Json{{"question", messages}, {"images", snapshots}}.dump());
  const bool confirmed = json::get_bool(confirmation, "approved", false) &&
      json::get_string(confirmation, "image_payload_sha256") == binding;
  LOOM_TRY_ASSIGN(auto pending, secrets::gate(recipe, mode, sources, store_values, confirmed));
  if (!pending.is_null()) {
    pending["image_payload_sha256"] = binding;
    pending["resend"] = "repeat the same request with secret_confirmation {approved:true, image_payload_sha256}";
  }
  return pending;
}

}
