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
class RuntimeProfile;
class SemanticAnalyzer;
class MemoryEngine;

struct GraphSelectOptions {
  std::optional<Json> analysis;  // pre-computed unified analysis
  std::optional<int> max_nodes;
  std::optional<int> depth;
  std::optional<std::string> current_conv_id;
};

// One message the graph selection put into the context (M1-K1c2 B): which one, how it reads in the text, and how the search reached it.
// `row` is the exact piece of the text that came from this message (the separator before it is not part of it).
struct GraphElement {
  std::string id;       // message id (m_...)
  std::string conv_id;
  std::string role;
  std::string title;    // the conversation title as it stands in the text (clipped by the profile)
  std::string row;      // the rendered row of this message
  std::string via;      // the id of the node the search came from ("" for a seed)
  int depth = 0;        // hops from the nearest seed
};

// The text select_context_checked returns (byte for byte) plus what it was made of: header + [separator] row + [separator] row ...
struct GraphContextDetail {
  std::string text;
  std::string header;
  std::string separator;
  std::vector<GraphElement> elements;  // in the order of the rows in `text`
};

class GraphMemorySelector {
 public:
  GraphMemorySelector(Database& db, const Config& cfg, const SemanticAnalyzer& regex);

  std::string select_context(std::string_view text, const GraphSelectOptions& opts = {});
  std::vector<std::string> extract_seed_labels(std::string_view text, const std::optional<Json>& analysis) const;
  Result<std::vector<std::string>> extract_seed_labels_checked(std::string_view text, const std::optional<Json>& analysis,
                                                              const Json& overrides = Json::object()) const;
  Result<std::string> select_context_checked(std::string_view text, const GraphSelectOptions& opts = {},
                                            const Json& overrides = Json::object());
  // The same selection, the same text (select_context_checked is this function's `text`), and the messages it was made of, for the receipt.
  Result<GraphContextDetail> select_context_detailed(std::string_view text, const GraphSelectOptions& opts = {},
                                                     const Json& overrides = Json::object());

  // Loom: BFS subgraph around `seed_ids` (nodes or messages) up to `depth`
  // hops, capped at max_nodes. Output shape matches Database::get_graph_data
  // ({"nodes":[{"id","label","type","kind",...}], "edges":[{"src","dst",
  // "type","weight"}]}) plus "depth" per node. Backs loom_expand_graph.
  Result<Json> expand(const std::vector<std::string>& seed_ids, int depth);
  Result<Json> expand(const std::vector<std::string>& seed_ids, int depth, int max_nodes);

 private:
  Result<std::string> select_impl(std::string_view text, const GraphSelectOptions& opts, const Json& overrides, GraphContextDetail* detail);

  Database& db_;
  const Config& cfg_;
  const SemanticAnalyzer& regex_;
};

// ── ContextSet (MEGA MASTER 4.4) ────────────────────────────────────
// Builds the context for one model call from several sources under a token
// budget. The graph_memory profile selects channel defaults, source order,
// scores, token estimator, oversized-candidate and channel failure policies.
// Items are deduplicated by id and retain order within each source.
struct ContextRequest {
  std::string text;
  int depth = 0;          // Legacy preset: 0 uses config.graph_memory_depth.
  int max_tokens = 4000;  // Legacy C++ preset; omitted JSON uses the active profile.
  std::optional<std::string> conv_id;  // current conversation (excluded from graph hits)
  bool include_memory = true;
  bool include_graph = true;
  bool include_search = true;
  // Both JSON parsers remember only field presence, never source text.
  // null denotes a directly constructed C++ request with explicit values.
  Json provided_fields = nullptr;
  static Result<ContextRequest> from_json(const Json& j);
  static Result<ContextRequest> from_json_with_profile(const Json& j, const RuntimeProfile& profile);
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
  // Uses the builtin graph_memory estimator preset. select() uses its active profile.
  static int estimate_tokens(std::string_view text);
  // The divisor behind estimate_tokens (builtin graph_memory preset): tokens = ceil(codepoints / this).
  static std::size_t codepoints_per_token();

 private:
  Database& db_;
  const Config& cfg_;
  GraphMemorySelector& graph_;
  MemoryEngine* memory_;
};

}  // namespace loom
