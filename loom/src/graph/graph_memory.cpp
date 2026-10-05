// OWNER: wave 2 semantic/graph. Port of engine/graph_memory.py
// (GraphMemorySelector) plus the Loom-native expand()/ContextSelector.
#include "loom/graph_memory.h"

#include <algorithm>
#include <limits>
#include <deque>
#include <set>
#include <stdexcept>

#include "loom/config.h"
#include "loom/runtime_profile.h"
#include "loom/log.h"
#include "loom/db.h"
#include "loom/memory_engine.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/utf8.h"

namespace loom {

GraphMemorySelector::GraphMemorySelector(Database& db, const Config& cfg, const SemanticAnalyzer& regex)
    : db_(db), cfg_(cfg), regex_(regex) {}

std::vector<std::string> GraphMemorySelector::extract_seed_labels(std::string_view text,
                                                                  const std::optional<Json>& analysis) const {
  auto result = extract_seed_labels_checked(text, analysis);
  if (!result) { log::error("loom.graph_memory", "seed profile rejected: {}", result.error().message); return {}; }
  return *result;
}

Result<std::vector<std::string>> GraphMemorySelector::extract_seed_labels_checked(std::string_view text,
                                                                                const std::optional<Json>& analysis,
                                                                                const Json& overrides) const {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_memory", cfg_.path().parent_path(), overrides));
  LOOM_TRY_ASSIGN(auto semantic_profile, RuntimeProfile::load("semantic_analyzer", cfg_.path().parent_path()));
  std::size_t minimum = profile.values().at("seed_min_label_codepoints").get<std::size_t>();
  std::vector<std::string> labels;
  if (analysis) {
    if (const Json* ents = json::find(*analysis, "entities"); ents && ents->is_array()) {
      for (const auto& ent : *ents) {
        std::string name = ent.is_object() ? json::get_string(ent, "name", "")
                                           : (ent.is_string() ? ent.get<std::string>() : "");
        if (!name.empty() && utf8::length(name) >= minimum) labels.push_back(name);
      }
    }
    if (const Json* tops = json::find(*analysis, "topics"); tops && tops->is_array()) {
      for (const auto& t : *tops) {
        std::string label = t.is_object() ? json::get_string(t, "label", "") : (t.is_string() ? t.get<std::string>() : "");
        if (!label.empty()) labels.push_back(label);
      }
    }
  } else {
    std::unique_ptr<SemanticAnalyzer> effective;
    const SemanticAnalyzer* analyzer = &regex_;
    if (!semantic_profile.is_builtin()) {
      LOOM_TRY_ASSIGN(effective, SemanticAnalyzer::create_with_profile(semantic_profile));
      analyzer = effective.get();
    }
    Analysis result = analyzer->analyse(text);
    for (const auto& ent : result.entities) {
      if (utf8::length(ent.text) >= minimum) labels.push_back(ent.text);
    }
    for (const auto& t : result.topics) labels.push_back(t);
  }
  return labels;
}

std::string GraphMemorySelector::select_context(std::string_view text, const GraphSelectOptions& opts) {
  auto result = select_context_checked(text, opts);
  if (!result) { log::error("loom.graph_memory", "context profile rejected: {}", result.error().message); return ""; }
  return *result;
}

Result<std::string> GraphMemorySelector::select_context_checked(std::string_view text, const GraphSelectOptions& opts,
                                                               const Json& overrides) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_memory", cfg_.path().parent_path(), overrides));
  const Json& policy = profile.values();
  const bool zero_as_default = policy.at("zero_as_default").get<bool>();
  auto config_integer = [&](std::string_view key, const Json& fallback) -> Result<int> {
    const Json value = cfg_.get(key, fallback);
    if (!value.is_number_integer()) return Error(Errc::InvalidArgument, "graph setting " + std::string(key) + " must be an integer");
    if (value.is_number_unsigned()) {
      const auto number = value.get<std::uint64_t>();
      if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        return Error(Errc::InvalidArgument, "graph setting " + std::string(key) + " exceeds the native integer representation");
      return static_cast<int>(number);
    }
    const auto number = value.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
      return Error(Errc::InvalidArgument, "graph setting " + std::string(key) + " exceeds the native integer representation");
    return static_cast<int>(number);
  };
  int max_n, max_d;
  if (opts.max_nodes && (!zero_as_default || *opts.max_nodes != 0)) max_n = *opts.max_nodes;
  else { LOOM_TRY_ASSIGN(max_n, config_integer("graph_memory_max_nodes", policy.at("default_max_nodes"))); }
  if (opts.depth && (!zero_as_default || *opts.depth != 0)) max_d = *opts.depth;
  else { LOOM_TRY_ASSIGN(max_d, config_integer("graph_memory_depth", policy.at("default_depth"))); }

  LOOM_TRY_ASSIGN(auto seed_labels, extract_seed_labels_checked(text, opts.analysis, overrides));
  if (seed_labels.empty()) return "";

  std::vector<std::string> seed_ids_order;
  std::set<std::string> seed_ids_set;
  auto add_seed = [&](const std::string& id) {
    if (seed_ids_set.insert(id).second) seed_ids_order.push_back(id);
  };
  for (const auto& label : seed_labels) {
    auto n1 = db_.find_node(label);
    if (n1 && *n1) add_seed((*n1)->id);
    auto n2 = db_.find_node(utf8::to_lower(label), policy.at("topic_kind").get<std::string>());
    if (n2 && *n2) add_seed((*n2)->id);
  }
  if (seed_ids_order.empty()) return "";

  std::set<std::string> visited;
  std::vector<std::string> message_ids;
  std::deque<std::pair<std::string, int>> queue;
  for (const auto& s : seed_ids_order) queue.push_back({s, 0});

  while (!queue.empty() && (max_n == 0 || static_cast<int>(message_ids.size()) < max_n)) {
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
  std::size_t cap = max_n == 0 ? message_ids.size() : std::min(message_ids.size(), static_cast<std::size_t>(max_n));
  for (std::size_t i = 0; i < cap; ++i) {
    auto m = db_.get_msg(message_ids[i]);
    if (!m || !*m) continue;
    std::string conv_title = policy.at("missing_title").get<std::string>();
    auto title_length = policy.at("title_codepoints").get<std::size_t>();
    auto snippet_length = policy.at("snippet_codepoints").get<std::size_t>();
    auto c = db_.get_conv((*m)->conv_id);
    if (c && *c) conv_title = title_length == 0 ? (*c)->title : std::string(utf8::prefix((*c)->title, title_length));
    std::string snippet = snippet_length == 0 ? (*m)->text : std::string(utf8::prefix((*m)->text, snippet_length));
    LOOM_TRY_ASSIGN(auto row, render_profile_template(policy.at("row").get<std::string>(),
                                                    Json{{"title", conv_title}, {"role", (*m)->role}, {"text", snippet}}));
    parts.push_back(std::move(row));
  }
  if (parts.empty()) return "";

  std::string out = policy.at("header").get<std::string>();
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += policy.at("separator").get<std::string>();
    out += parts[i];
  }
  return out;
}

Result<Json> GraphMemorySelector::expand(const std::vector<std::string>& seed_ids, int depth) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_memory", cfg_.path().parent_path()));
  return expand(seed_ids, depth, profile.values().at("default_expand_max_nodes").get<int>());
}

Result<Json> GraphMemorySelector::expand(const std::vector<std::string>& seed_ids, int depth, int max_nodes) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_memory", cfg_.path().parent_path()));
  const std::size_t label_length = profile.values().at("graph_label_codepoints").get<std::size_t>();
  std::set<std::string> visited;
  std::deque<std::pair<std::string, int>> queue;
  for (const auto& s : seed_ids) queue.push_back({s, 0});

  Json nodes_arr = Json::array();
  Json edges_arr = Json::array();
  std::set<std::string> edge_ids_seen;

  while (!queue.empty() && (max_nodes == 0 || static_cast<int>(visited.size()) < max_nodes)) {
    auto [id, d] = queue.front();
    queue.pop_front();
    if (visited.count(id)) continue;

    Json node_entry;
    if (id.rfind("m_", 0) == 0) {
      auto m = db_.get_msg(id);
      if (!m) return Error(m.error());
      if (!*m) continue;
      node_entry = Json{{"id", id},
                        {"label", label_length == 0 ? (*m)->text : std::string(utf8::prefix((*m)->text, label_length))},
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
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::builtin("graph_memory"));
  return from_json_with_profile(j, profile);
}

Result<ContextRequest> ContextRequest::from_json_with_profile(const Json& j, const RuntimeProfile& profile) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "context request must be an object");
  if (profile.domain() != "graph_memory") return Error(Errc::InvalidArgument, "expected graph_memory profile");
  LOOM_TRY_ASSIGN(auto descriptor, RuntimeProfile::builtin("graph_memory"));
  LOOM_TRY(descriptor.with_values(profile.values()));
  const Json& defaults = profile.values().at("context");
  ContextRequest r;
  r.text = json::get_string(j, "text");
  r.depth = static_cast<int>(json::get_int(j, "depth", 0));
  r.max_tokens = static_cast<int>(json::get_int(j, "max_tokens", defaults.at("max_tokens").get<std::int64_t>()));
  r.conv_id = json::get_opt_string(j, "conv_id");
  r.include_memory = json::get_bool(j, "include_memory", defaults.at("include_memory").get<bool>());
  r.include_graph = json::get_bool(j, "include_graph", defaults.at("include_graph").get<bool>());
  r.include_search = json::get_bool(j, "include_search", defaults.at("include_search").get<bool>());
  r.provided_fields = Json::object();
  for (const auto& field : {"depth", "max_tokens", "include_memory", "include_graph", "include_search"})
    if (j.contains(field)) r.provided_fields[field] = true;
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
  auto profile = RuntimeProfile::builtin("graph_memory");
  if (!profile) throw std::logic_error(profile.error().message);
  auto divisor = profile->values().at("context").at("codepoints_per_token").get<std::size_t>();
  return static_cast<int>((utf8::length(text) + divisor - 1) / divisor);
}

Result<ContextSet> ContextSelector::select(const ContextRequest& req) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_memory", cfg_.path().parent_path()));
  const Json& policy = profile.values().at("context");
  auto supplied = [&](std::string_view field) {
    return req.provided_fields.is_null() || req.provided_fields.contains(std::string(field));
  };
  const bool zero_as_default = policy.at("zero_as_default").get<bool>();
  if (supplied("max_tokens") && req.max_tokens < 0 && !zero_as_default)
    return Error(Errc::InvalidArgument, "context max_tokens must be nonnegative when zero_as_default is disabled");
  auto fail_channel = [&](std::string_view channel) {
    return policy.at("failure_policy").at(std::string(channel)) == "error";
  };
  const bool include_memory = supplied("include_memory") ? req.include_memory : policy.at("include_memory").get<bool>();
  const bool include_graph = supplied("include_graph") ? req.include_graph : policy.at("include_graph").get<bool>();
  const bool include_search = supplied("include_search") ? req.include_search : policy.at("include_search").get<bool>();
  std::vector<ContextItem> candidates;

  if (include_memory && memory_) {
    auto mem_ctx = memory_->get_active_context_checked();
    if (!mem_ctx) {
      if (fail_channel("memory")) return mem_ctx.error();
    } else if (!mem_ctx->empty()) {
      candidates.push_back(ContextItem{"memory", "memory", "memory_tree", *mem_ctx, policy.at("memory_score").get<double>(), Json::object()});
    }
  }

  if (include_graph) {
    GraphSelectOptions opts;
    if (req.depth > 0 || (supplied("depth") && !profile.values().at("zero_as_default").get<bool>())) opts.depth = req.depth;
    opts.current_conv_id = req.conv_id;
    auto g = graph_.select_context_checked(req.text, opts);
    if (!g) {
      if (fail_channel("graph")) return g.error();
    } else if (!g->empty()) {
      candidates.push_back(ContextItem{"graph", "message", "graph", *g, policy.at("graph_score").get<double>(), Json::object()});
    }
  }

  if (include_search && !req.text.empty()) {
    SearchOptions sopts;
    sopts.limit = policy.at("search_limit").get<int>();
    auto sr = db_.search_messages(req.text, sopts);
    if (!sr) {
      if (fail_channel("search")) return sr.error();
    } else {
      for (const auto& hit : sr->hits) {
        if (req.conv_id && hit.message.conv_id == *req.conv_id) continue;
        candidates.push_back(ContextItem{hit.message.id, "search", "fts", hit.message.text, hit.score, Json::object()});
      }
    }
  }

  const auto& source_order = policy.at("source_order");
  auto rank = [&](const ContextItem& item) {
    std::string source = item.source == "memory_tree" ? "memory" : item.source == "fts" ? "search" : item.source;
    auto found = std::find(source_order.begin(), source_order.end(), Json(source));
    return std::distance(source_order.begin(), found);
  };
  std::stable_sort(candidates.begin(), candidates.end(), [&](const ContextItem& a, const ContextItem& b) { return rank(a) < rank(b); });
  std::vector<ContextItem> deduped;
  std::set<std::string> seen;
  for (auto& it : candidates) {
    if (!seen.insert(it.id).second) continue;
    deduped.push_back(std::move(it));
  }

  ContextSet result;
  int budget = supplied("max_tokens") && (req.max_tokens > 0 || (!zero_as_default && req.max_tokens == 0))
                   ? req.max_tokens
                   : policy.at("max_tokens").get<int>();
  int used = 0;
  std::string prompt;
  for (auto& it : deduped) {
    auto divisor = policy.at("codepoints_per_token").get<std::size_t>();
    int t = static_cast<int>((utf8::length(it.text) + divisor - 1) / divisor);
    if (budget > 0 && used + t > budget) {
      result.truncated = true;
      if (policy.at("oversized_candidate") == "skip") continue;
      break;
    }
    if (!prompt.empty()) prompt += policy.at("separator").get<std::string>();
    prompt += it.text;
    used += t;
    result.items.push_back(it);
  }
  result.prompt_text = prompt;
  result.token_estimate = used;
  return result;
}

}  // namespace loom
