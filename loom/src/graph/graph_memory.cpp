// OWNER: wave 2 semantic/graph. Stubs except the token estimate policy.
#include "loom/graph_memory.h"

#include "loom/util/utf8.h"
#include "stub.h"

namespace loom {

GraphMemorySelector::GraphMemorySelector(Database& db, const Config& cfg, const SemanticAnalyzer& regex)
    : db_(db), cfg_(cfg), regex_(regex) {}

std::string GraphMemorySelector::select_context(std::string_view, const GraphSelectOptions&) {
  return {};  // STUB: wave2
}

std::vector<std::string> GraphMemorySelector::extract_seed_labels(std::string_view, const std::optional<Json>&) const {
  return {};  // STUB: wave2
}

Result<Json> GraphMemorySelector::expand(const std::vector<std::string>&, int, int) {
  return LOOM_NOT_IMPLEMENTED("GraphMemorySelector::expand");  // STUB: wave2
}

Result<ContextRequest> ContextRequest::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "context request must be an object");
  ContextRequest r;
  r.text = json::get_string(j, "text");
  r.depth = static_cast<int>(json::get_int(j, "depth", 0));
  r.max_tokens = static_cast<int>(json::get_int(j, "max_tokens", 4000));
  r.conv_id = json::get_opt_string(j, "conv_id");
  r.include_memory = json::get_bool(j, "include_memory", true);
  r.include_graph = json::get_bool(j, "include_graph", true);
  r.include_search = json::get_bool(j, "include_search", true);
  return r;
}

Json ContextItem::to_json() const {
  return Json{{"id", id},     {"kind", kind},   {"source", source},
              {"text", text}, {"score", score}, {"metadata", metadata}};
}

Json ContextSet::to_json() const {
  Json arr = Json::array();
  for (const auto& i : items) arr.push_back(i.to_json());
  return Json{{"items", arr}, {"prompt_text", prompt_text}, {"token_estimate", token_estimate}, {"truncated", truncated}};
}

ContextSelector::ContextSelector(Database& db, const Config& cfg, GraphMemorySelector& graph, MemoryEngine* memory)
    : db_(db), cfg_(cfg), graph_(graph), memory_(memory) {}

Result<ContextSet> ContextSelector::select(const ContextRequest&) {
  return LOOM_NOT_IMPLEMENTED("ContextSelector::select");  // STUB: wave2
}

int ContextSelector::estimate_tokens(std::string_view text) {
  return static_cast<int>((utf8::length(text) + 3) / 4);
}

}  // namespace loom
