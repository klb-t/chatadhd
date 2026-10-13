// Typed parts and capability descriptions of the use-model action.
#include <algorithm>

#include "loom/use_model.h"

namespace loom::llm {

bool known_part_kind(std::string_view kind) noexcept {
  return std::find(std::begin(kPartKinds), std::end(kPartKinds), kind) != std::end(kPartKinds);
}

Json Part::to_json() const {
  Json out{{"kind", kind}};
  if (!text.empty()) out["text"] = text;
  if (!value.is_null()) out["value"] = value;
  if (!blob_hash.empty()) out["blob_hash"] = blob_hash;
  if (!url.empty()) out["url"] = url;
  if (!mime.empty()) out["mime"] = mime;
  if (!node_id.empty()) out["node_id"] = node_id;
  return out;
}

Result<Part> Part::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "a part must be an object");
  Part p;
  p.kind = json::get_string(j, "kind");
  if (!known_part_kind(p.kind)) return Error(Errc::InvalidArgument, "unknown part kind '" + p.kind + "'");
  p.text = json::get_string(j, "text");
  if (const Json* v = json::find(j, "value")) p.value = *v;
  p.blob_hash = json::get_string(j, "blob_hash");
  p.url = json::get_string(j, "url");
  p.mime = json::get_string(j, "mime");
  p.node_id = json::get_string(j, "node_id");
  return p;
}

Json Capabilities::to_json() const {
  return Json{{"accepts", accepts}, {"produces", produces}, {"modes", modes}};
}

}  // namespace loom::llm
