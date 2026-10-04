#pragma once

#include <string_view>

#include "loom/context_engine.h"

namespace loom {
class Config;
class Database;
class GraphMemorySelector;
class MemoryEngine;

namespace context {

// Read-only compilation over the already-selected knowledge material and the
// legacy memory/message stores. Output prompt uses one final rendered budget,
// including headings and omission markers; knowledge_result is retained intact.
// Memory is budgeted per node. Identical knowledge representations deduplicate;
// alternative views of one record retain their independent resolution/projection.
// Typed knowledge diagnostics are rendered once whenever knowledge is selected,
// and count toward that same budget. Unsupported custom ContextSet shapes get
// an explicit diagnostics_mapping_status in the knowledge channel report.
// Options are caller policy, not product ceilings:
//   budget_tokens (default request; 0 empty), include_knowledge (true),
//   source_priority ([memory,knowledge,legacy_graph], within each band),
//   section_labels ({stable,project,goal}), memory_max_chars (16000; 0 all),
//   legacy_relation_hops (request.relation_hops; 0 seed nodes only),
//   legacy_max_messages (config.graph_memory_max_nodes; 0 all),
//   legacy_max_visited (0 all), legacy_detail_chars (0 full; exact source slice),
//   legacy_include_inactive (false), legacy_include_current_conversation (false),
//   legacy_seed_ids ([]), analysis (optional).
// Reach and source detail remain independent. No source row is rewritten and
// stored/model/user statements are never silently promoted to established truth.
Result<Json> compile_unified_context(Database& db, const Config& cfg,
    GraphMemorySelector* graph, MemoryEngine* memory, const ContextRequest& request,
    Json knowledge_result, std::string_view conv_id, bool include_memory,
    bool include_graph, const Json& options = Json::object());

}  // namespace context
}  // namespace loom
