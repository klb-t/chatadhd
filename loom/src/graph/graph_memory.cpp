// OWNER: wave 2 semantic/graph. Port of engine/graph_memory.py
// (GraphMemorySelector) plus the Loom-native expand()/ContextSelector.
#include "loom/graph_memory.h"

#include <algorithm>
#include <deque>
#include <set>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/memory_engine.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/utf8.h"

namespace loom {

GraphMemorySelector::GraphMemorySelector(Database& db, const Config& cfg, const SemanticAnalyzer& regex)
    : db_(db), cfg_(cfg), regex_(regex) {}

std::vector<std::string> GraphMemorySelector::extract_seed_labels(std::string_view text,
                                                                  const std::optional<Json>& analysis) const {
  std::vector<std::string> labels;
  if (analysis) {
    if (const Json* ents = json::find(*analysis, "entities"); ents && ents->is_array()) {
      for (const auto& ent : *ents) {
        std::string name = ent.is_object() ? json::get_string(ent, "name", "")
                                           : (ent.is_string() ? ent.get<std::string>() : "");
        if (!name.empty() && utf8::length(name) > 1) labels.push_back(name);
      }
    }
    if (const Json* tops = json::find(*analysis, "topics"); tops && tops->is_array()) {
      for (const auto& t : *tops) {
        std::string label = t.is_object() ? json::get_string(t, "label", "") : (t.is_string() ? t.get<std::string>() : "");
        if (!label.empty()) labels.push_back(label);
      }
    }
  } else {
    Analysis result = regex_.analyse(text);
    for (const auto& ent : result.entities) {
      if (utf8::length(ent.text) > 1) labels.push_back(ent.text);
    }
    for (const auto& t : result.topics) labels.push_back(t);
  }
  return labels;
}

std::string GraphMemorySelector::select_context(std::string_view text, const GraphSelectOptions& opts) {
  int max_n = (opts.max_nodes && *opts.max_nodes != 0) ? *opts.max_nodes
                                                       : static_cast<int>(cfg_.get("graph_memory_max_nodes", 20).get<std::int64_t>());
  int max_d = (opts.depth && *opts.depth != 0) ? *opts.depth
                                               : static_cast<int>(cfg_.get("graph_memory_depth", 2).get<std::int64_t>());

  auto seed_labels = extract_seed_labels(text, opts.analysis);
  if (seed_labels.empty()) return "";

  std::vector<std::string> seed_ids_order;
  std::set<std::string> seed_ids_set;
  auto add_seed = [&](const std::string& id) {
    if (seed_ids_set.insert(id).second) seed_ids_order.push_back(id);
  };
  for (const auto& label : seed_labels) {
    auto n1 = db_.find_node(label);
    if (n1 && *n1) add_seed((*n1)->id);
    auto n2 = db_.find_node(utf8::to_lower(label), "topic");
    if (n2 && *n2) add_seed((*n2)->id);
  }
  if (seed_ids_order.empty()) return "";

  std::set<std::string> visited;
  std::vector<std::string> message_ids;
  std::deque<std::pair<std::string, int>> queue;
  for (const auto& s : seed_ids_order) queue.push_back({s, 0});

  while (!queue.empty() && static_cast<int>(message_ids.size()) < max_n) {
    auto [node_id, d] = queue.front();
    queue.pop_front();
    if (visited.count(node_id)) continue;
    visited.insert(node_id);

    if (node_id.rfind("m_", 0) == 0) {
      auto m = db_.get_msg(node_id);
      if (m && *m) {
        bool same_conv = opts.current_conv_id.has_value() && (*m)->conv_id == *opts.current_conv_id;
        if (!same_conv) message_ids.push_back(node_id);
      }
    }

    if (d < max_d) {
      auto links = db_.get_links(node_id);
      if (links) {
        for (const auto& l : *links) {
          std::string neighbor = (l.src == node_id) ? l.dst : l.src;
          if (!visited.count(neighbor)) queue.push_back({neighbor, d + 1});
        }
      }
    }
  }

  if (message_ids.empty()) return "";

  std::vector<std::string> parts;
  std::size_t cap = std::min(message_ids.size(), static_cast<std::size_t>(max_n));
  for (std::size_t i = 0; i < cap; ++i) {
    auto m = db_.get_msg(message_ids[i]);
    if (!m || !*m) continue;
    std::string conv_title = "?";
    auto c = db_.get_conv((*m)->conv_id);
    if (c && *c) conv_title = std::string(utf8::prefix((*c)->title, 30));
    std::string snippet = std::string(utf8::prefix((*m)->text, 500));
    parts.push_back("[" + conv_title + " | " + (*m)->role + "]: " + snippet);
  }
  if (parts.empty()) return "";

  std::string out = "Relevant context from past conversations:\n";
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += "\n---\n";
    out += parts[i];
  }
  return out;
}

Result<Json> GraphMemorySelector::expand(const std::vector<std::string>& seed_ids, int depth, int max_nodes) {
  std::set<std::string> visited;
  std::deque<std::pair<std::string, int>> queue;
  for (const auto& s : seed_ids) queue.push_back({s, 0});

  Json nodes_arr = Json::array();
  Json edges_arr = Json::array();
  std::set<std::string> edge_ids_seen;

  while (!queue.empty() && static_cast<int>(visited.size()) < max_nodes) {
    auto [id, d] = queue.front();
    queue.pop_front();
    if (visited.count(id)) continue;

    Json node_entry;
    if (id.rfind("m_", 0) == 0) {
      auto m = db_.get_msg(id);
      if (!m) return Error(m.error());
      if (!*m) continue;
      node_entry = Json{{"id", id},
                        {"label", std::string(utf8::prefix((*m)->text, 40))},
                        {"type", "message"},
                        {"kind", "message"},
                        {"depth", d}};
    } else {
      auto n = db_.get_node(id);
      if (!n) return Error(n.error());
      if (!*n) continue;
      node_entry = (*n)->to_json();
      node_entry["type"] = (*n)->kind;
      node_entry["depth"] = d;
    }
    visited.insert(id);
    nodes_arr.push_back(node_entry);

    if (d < depth) {
      auto links = db_.get_links(id);
      if (!links) return Error(links.error());
      for (const auto& l : *links) {
        if (edge_ids_seen.insert(l.id).second) edges_arr.push_back(l.to_json());
        std::string neighbor = (l.src == id) ? l.dst : l.src;
        if (!visited.count(neighbor)) queue.push_back({neighbor, d + 1});
      }
    }
  }

  return Json{{"nodes", nodes_arr}, {"edges", edges_arr}};
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

int ContextSelector::estimate_tokens(std::string_view text) {
  return static_cast<int>((utf8::length(text) + 3) / 4);
}

Result<ContextSet> ContextSelector::select(const ContextRequest& req) {
  std::vector<ContextItem> candidates;

  if (req.include_memory && memory_) {
    std::string mem_ctx = memory_->get_active_context();
    if (!mem_ctx.empty()) {
      candidates.push_back(ContextItem{"memory", "memory", "memory_tree", mem_ctx, 1.0, Json::object()});
    }
  }

  if (req.include_graph) {
    GraphSelectOptions opts;
    if (req.depth > 0) opts.depth = req.depth;
    opts.current_conv_id = req.conv_id;
    std::string g = graph_.select_context(req.text, opts);
    if (!g.empty()) {
      candidates.push_back(ContextItem{"graph", "message", "graph", g, 0.8, Json::object()});
    }
  }

  if (req.include_search && !req.text.empty()) {
    SearchOptions sopts;
    sopts.limit = 10;
    auto sr = db_.search_messages(req.text, sopts);
    if (sr) {
      for (const auto& hit : sr->hits) {
        if (req.conv_id && hit.message.conv_id == *req.conv_id) continue;
        candidates.push_back(ContextItem{hit.message.id, "search", "fts", hit.message.text, hit.score, Json::object()});
      }
    }
  }

  std::vector<ContextItem> deduped;
  std::set<std::string> seen;
  for (auto& it : candidates) {
    if (!seen.insert(it.id).second) continue;
    deduped.push_back(std::move(it));
  }

  ContextSet result;
  int budget = req.max_tokens > 0 ? req.max_tokens : 4000;
  int used = 0;
  std::string prompt;
  for (auto& it : deduped) {
    int t = estimate_tokens(it.text);
    if (used + t > budget) {
      result.truncated = true;
      break;
    }
    if (!prompt.empty()) prompt += "\n\n";
    prompt += it.text;
    used += t;
    result.items.push_back(it);
  }
  result.prompt_text = prompt;
  result.token_estimate = used;
  return result;
}

}  // namespace loom
