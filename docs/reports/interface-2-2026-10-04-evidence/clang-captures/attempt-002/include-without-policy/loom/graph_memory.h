// loom/graph_memory.h — port of engine/graph_memory.py + the ContextSelector
// facade behind loom_select_context.                       [OWNER: wave 2 semantic/graph]
//
// GraphMemorySelector.select_context(text, analysis, max_nodes, depth,
// current_conv_id) — behaviour to preserve:
//   max_n = max_nodes or config.graph_memory_max_nodes (20); max_d = depth or
//   config.graph_memory_depth (2)  (0 counts as "not given", Python `or`)
//   seeds = extract_seed_labels(text, analysis): from analysis entities
//     (name, len > 1) + topics (label), else regex entities (len(text) > 1) +
//     regex topics
//   seed ids: find_node(label) and find_node(label.lower(), kind="topic")
//   BFS (queue of (id, depth), visited set) while queue and
//     len(message_ids) < max_n: ids starting with "m_" are collected unless
//     the message belongs to current_conv_id; expand neighbours (get_links)
//     while depth < max_d
//   output: "Relevant context from past conversations:\n" + "\n---\n".join(
//     "[{conv_title[:30]} | {role}]: {text[:500]}")  ("?" if conv missing);
//     "" when no seeds / no matches.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Database;
class Config;
class SemanticAnalyzer;
class MemoryEngine;

struct GraphSelectOptions {
  std::optional<Json> analysis;  // pre-computed unified analysis
  std::optional<int> max_nodes;
  std::optional<int> depth;
  std::optional<std::string> current_conv_id;
};

class GraphMemorySelector {
 public:
  GraphMemorySelector(Database& db, const Config& cfg, const SemanticAnalyzer& regex);

  std::string select_context(std::string_view text, const GraphSelectOptions& opts = {});
  std::vector<std::string> extract_seed_labels(std::string_view text, const std::optional<Json>& analysis) const;

  // Loom: BFS subgraph around `seed_ids` (nodes or messages) up to `depth`
  // hops, capped at max_nodes. Output shape matches Database::get_graph_data
  // ({"nodes":[{"id","label","type","kind",...}], "edges":[{"src","dst",
  // "type","weight"}]}) plus "depth" per node. Backs loom_expand_graph.
  Result<Json> expand(const std::vector<std::string>& seed_ids, int depth, int max_nodes = 500);

 private:
  Database& db_;
  const Config& cfg_;
  const SemanticAnalyzer& regex_;
};

// ── ContextSet (MEGA MASTER 4.4) ────────────────────────────────────
// Builds the context for one model call from several sources under a token
// budget. Selection policy v1 (data-driven weights may come later):
//   1. memory tree (MemoryEngine::get_active_context) when include_memory
//   2. graph neighbourhood (GraphMemorySelector) with `depth`
//   3. full-text hits (Database::search_messages) when include_search
// Items are deduplicated by id, ordered by source then score, and appended
// until the token estimate reaches max_tokens (truncated=true if cut).
struct ContextRequest {
  std::string text;
  int depth = 0;          // 0 = config.graph_memory_depth
  int max_tokens = 4000;  // budget for prompt_text
  std::optional<std::string> conv_id;  // current conversation (excluded from graph hits)
  bool include_memory = true;
  bool include_graph = true;
  bool include_search = true;
  static Result<ContextRequest> from_json(const Json& j);
};

struct ContextItem {
  std::string id;
  std::string kind;    // "memory" | "message" | "search"
  std::string source;  // "memory_tree" | "graph" | "fts"
  std::string text;
  double score = 0.0;
  Json metadata = Json::object();
  Json to_json() const;
};

struct ContextSet {
  std::vector<ContextItem> items;
  std::string prompt_text;  // ready to inject as a system message
  int token_estimate = 0;
  bool truncated = false;
  Json to_json() const;  // {"items":[...],"prompt_text","token_estimate","truncated"}
};

class ContextSelector {
 public:
  ContextSelector(Database& db, const Config& cfg, GraphMemorySelector& graph, MemoryEngine* memory);
  Result<ContextSet> select(const ContextRequest& req);
  // Policy: ceil(code points / 4).
  static int estimate_tokens(std::string_view text);

 private:
  Database& db_;
  const Config& cfg_;
  GraphMemorySelector& graph_;
  MemoryEngine* memory_;
};

}  // namespace loom
