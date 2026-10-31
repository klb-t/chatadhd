#include "llm/image_contract.h"
#include "loom/config.h"
#include "loom/util/fs.h"
#include <algorithm>

namespace loom::llm {
namespace {
Result<std::optional<bool>> accepts(const Json& data) {
  if (!data.is_object() || !data.contains("accepts")) return std::optional<bool>{};
  if (!data.at("accepts").is_array()) return Error(Errc::InvalidArgument, "image_capability_invalid");
  bool image = false;
  for (const auto& part : data.at("accepts")) {
    if (!part.is_string()) return Error(Errc::InvalidArgument, "image_capability_invalid");
    if (part == "image") image = true;
  }
  return std::optional<bool>(image);
}
bool contains_image(const Json& messages) {
  if (!messages.is_array()) return false;
  for (const auto& message : messages) {
    const auto* content = json::find(message, "content");
    if (!content || !content->is_array()) continue;
    for (const auto& part : *content)
      if (json::get_string(part, "type") == "image_url" || json::get_string(part, "type") == "image") return true;
  }
  return false;
}
}
Result<std::optional<bool>> catalog_accepts_images(const Services& services, const ModelSpec& model) {
  LOOM_TRY_ASSIGN(auto explicit_value, accepts(model.capabilities));
  if (explicit_value) return explicit_value;
  const Json configured = services.config.get("model_capabilities");
  if (configured.is_object() && configured.contains(model.model)) {
    LOOM_TRY_ASSIGN(auto value, accepts(configured.at(model.model)));
    if (value) return value;
  }
  const auto path = services.config.path().parent_path() / "models.json";
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    if (ec) return Error(Errc::Io, "image_catalog_unreadable");
    return std::optional<bool>{};
  }
  LOOM_TRY_ASSIGN(auto raw, fsutil::read_file(path));
  LOOM_TRY_ASSIGN(auto catalog, json::parse(raw));
  if (catalog.is_object()) {
    if (catalog.contains("data")) catalog = Json(catalog.at("data"));
    else if (catalog.contains("models")) catalog = Json(catalog.at("models"));
  }
  if (!catalog.is_array()) return Error(Errc::Parse, "image_catalog_invalid");
  for (const auto& entry : catalog) {
    if (json::get_string(entry, "id") != model.model) continue;
    if (entry.contains("capabilities")) {
      LOOM_TRY_ASSIGN(auto value, accepts(entry.at("capabilities")));
      if (value) return value;
    }
    const auto* architecture = json::find(entry, "architecture");
    if (architecture && architecture->is_object() && architecture->contains("input_modalities"))
      return accepts(Json{{"accepts", architecture->at("input_modalities")}});
    return std::optional<bool>{};
  }
  return std::optional<bool>{};
}
Status check_image_contract(const Services& services, const ModelSpec& model, const Adapter& adapter,
                             const CallSpec& spec) {
  const auto intrinsic = adapter.capabilities();
  for (const auto& part : spec.parts)
    if (part.kind == "image" && std::find(intrinsic.accepts.begin(), intrinsic.accepts.end(), "image") == intrinsic.accepts.end())
      return Error(Errc::Unsupported, "vision_adapter_typed_image_unavailable");
  Json wire = spec.body;
  const bool raw = !spec.body_bytes.empty();
  if (raw) {
    auto parsed = json::parse(spec.body_bytes);
    if (!parsed) {
      // Opaque media/embedding operations retain their contracts; an opaque
      // message payload cannot be certified safe using a different description.
      if (!spec.messages.empty() || !spec.batch_requests.empty() ||
          (wire.is_object() && (wire.contains("messages") || wire.contains("requests"))))
        return Error(Errc::InvalidArgument, "vision_message_payload_uninspectable");
      return ok_status();
    }
    wire = std::move(*parsed);
  }
  // body_bytes wins in the transport even if body/messages describe something
  // different. Inspect it for every adapter, including adapters without vision.
  const Json messages = wire.is_object() && wire.contains("messages") ? wire.at("messages") :
      ((raw || wire.is_object()) ? Json::array() : spec.messages);
  const Json batches = wire.is_object() && wire.contains("requests") ? wire.at("requests") :
      ((raw || wire.is_object()) ? Json::array() : spec.batch_requests);
  std::vector<Json> groups{messages};
  if (batches.is_array()) for (const auto& batch : batches) {
    if (batch.contains("params") && batch.at("params").contains("messages"))
      groups.push_back(batch.at("params").at("messages"));
  }
  bool image = false;
  for (const auto& group : groups) image = image || contains_image(group);
  if (!image) return ok_status();
  if (!adapter.accepts_message_images()) return Error(Errc::Unsupported, "vision_adapter_image_contract_unavailable");
  if (wire.is_object() && wire.contains("model") && json::get_string(wire, "model") != model.model)
    return Error(Errc::InvalidArgument, "vision_model_payload_mismatch");
  for (const auto& group : groups) for (const auto& message : group) {
    const auto* content = json::find(message, "content");
    if (!content || !content->is_array()) continue;
    for (const auto& part : *content) {
      const auto type = json::get_string(part, "type");
      if (type == "image") return Error(Errc::Unsupported, "vision_image_part_format");
      if (type == "image_url" && (!part.contains("image_url") || !part.at("image_url").is_object() ||
          json::get_string(part.at("image_url"), "url").empty()))
        return Error(Errc::InvalidArgument, "vision_image_url_missing");
    }
  }
  LOOM_TRY_ASSIGN(auto capable, catalog_accepts_images(services, model));
  if (!capable) return Error(Errc::Unavailable, "vision_model_capability_unknown");
  if (!*capable) return Error(Errc::Unsupported, "vision_model_image_input_unsupported");
  return ok_status();
}
}
