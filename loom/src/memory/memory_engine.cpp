// OWNER: wave 2 semantic/graph. Stub.
#include "loom/memory_engine.h"

#include "stub.h"

namespace loom {

Json MemoryNode::to_json() const {
  return Json{{"id", id},
              {"content", content},
              {"parent_id", parent_id ? Json(*parent_id) : Json(nullptr)},
              {"node_type", node_type},
              {"active", active},
              {"depth", depth},
              {"weight", weight},
              {"tags", tags},
              {"created", created},
              {"metadata", metadata}};
}

Result<MemoryNode> MemoryNode::from_json(const Json&) {
  return LOOM_NOT_IMPLEMENTED("MemoryNode::from_json");  // STUB: wave2
}

MemoryEngine::MemoryEngine(std::filesystem::path path, const SemanticAnalyzer* analyzer)
    : path_(std::move(path)), analyzer_(analyzer) {
  // STUB: wave2 - load memory.json
}

Result<std::string> MemoryEngine::add_node(std::string_view, std::optional<std::string>, std::string_view, Json,
                                           std::vector<std::string>) {
  return LOOM_NOT_IMPLEMENTED("MemoryEngine::add_node");  // STUB: wave2
}

Status MemoryEngine::update_node(std::string_view, const Json&) {
  return LOOM_NOT_IMPLEMENTED("MemoryEngine::update_node");  // STUB: wave2
}

Status MemoryEngine::delete_node(std::string_view, bool) {
  return LOOM_NOT_IMPLEMENTED("MemoryEngine::delete_node");  // STUB: wave2
}

std::optional<MemoryNode> MemoryEngine::get_node(std::string_view) const { return std::nullopt; }  // STUB: wave2
std::vector<MemoryNode> MemoryEngine::get_children(std::optional<std::string_view>) const { return {}; }  // STUB: wave2
std::vector<MemoryNode> MemoryEngine::get_all() const { return {}; }  // STUB: wave2
std::string MemoryEngine::get_active_context(std::size_t) const { return {}; }  // STUB: wave2
std::vector<MemoryNode> MemoryEngine::search(std::string_view, int) const { return {}; }  // STUB: wave2
Json MemoryEngine::get_graph_data() const {
  return Json{{"nodes", Json::array()}, {"edges", Json::array()}};  // STUB: wave2
}
Status MemoryEngine::reload() { return LOOM_NOT_IMPLEMENTED("MemoryEngine::reload"); }  // STUB: wave2
Status MemoryEngine::save_locked() const { return LOOM_NOT_IMPLEMENTED("MemoryEngine::save"); }  // STUB: wave2
void MemoryEngine::load_locked() {}  // STUB: wave2

}  // namespace loom
