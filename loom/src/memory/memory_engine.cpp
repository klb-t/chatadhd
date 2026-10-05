// OWNER: wave 2 semantic/graph. Exact port of engine/memory_engine.py.
#include "loom/memory_engine.h"

#include <algorithm>
#include <format>
#include <functional>
#include <stdexcept>

#include "loom/log.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/fs.h"
#include "loom/util/ids.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom {

namespace {
constexpr std::string_view kLog = "loom.memory";
}

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

Result<MemoryNode> MemoryNode::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "memory node must be a JSON object");
  const Json* idv = json::find(j, "id");
  const Json* cv = json::find(j, "content");
  if (!idv || !idv->is_string()) return Error(Errc::InvalidArgument, "memory node requires 'id'");
  if (!cv || !cv->is_string()) return Error(Errc::InvalidArgument, "memory node requires 'content'");
  MemoryNode n;
  n.id = idv->get<std::string>();
  n.content = cv->get<std::string>();
  n.parent_id = json::get_opt_string(j, "parent_id");
  n.node_type = json::get_string(j, "node_type", "text");
  n.active = json::get_bool(j, "active", true);
  n.depth = static_cast<int>(json::get_int(j, "depth", 0));
  n.weight = json::get_number(j, "weight", 1.0);
  if (const Json* t = json::find(j, "tags"); t && t->is_array()) {
    for (const auto& x : *t) {
      if (x.is_string()) n.tags.push_back(x.get<std::string>());
    }
  }
  n.created = json::get_string(j, "created", "");
  if (n.created.empty()) n.created = timeutil::utc_now_iso();  // Python MemoryNode.__post_init__
  if (const Json* m = json::find(j, "metadata"); m && m->is_object()) n.metadata = *m;
  return n;
}

MemoryEngine::MemoryEngine(std::filesystem::path path, const SemanticAnalyzer* analyzer)
    : path_(std::move(path)), analyzer_(analyzer) {
  auto profile = RuntimeProfile::load("memory", path_.parent_path());
  if (profile) profile_ = std::move(*profile);
  else profile_error_ = profile.error();
  std::lock_guard<std::mutex> lock(mu_);
  load_locked();
}

// ── Persistence ─────────────────────────────────────────────────────────

void MemoryEngine::load_locked() {
  nodes_.clear();
  std::error_code ec;
  if (!std::filesystem::exists(path_, ec)) return;
  auto text = fsutil::read_file(path_);
  if (!text) {
    log::error(kLog, "failed to read {}: {}", path_.string(), text.error().message);
    return;
  }
  auto parsed = json::parse(*text);
  if (!parsed || !parsed->is_array()) {
    log::error(kLog, "failed to parse {} as a JSON array", path_.string());
    return;
  }
  std::vector<MemoryNode> loaded;
  loaded.reserve(parsed->size());
  for (const auto& item : *parsed) {
    auto n = MemoryNode::from_json(item);
    if (!n) {
      // Python: a single bad node raises inside _load()'s try/except and
      // aborts the whole load, leaving the cache empty.
      log::error(kLog, "failed to load memory node from {}: {}", path_.string(), n.error().message);
      nodes_.clear();
      return;
    }
    loaded.push_back(std::move(n).value());
  }
  nodes_ = std::move(loaded);
  log::info(kLog, "Loaded {} memory nodes from {}", nodes_.size(), path_.filename().string());
}

Status MemoryEngine::save_locked() const {
  Json arr = Json::array();
  for (const auto& n : nodes_) arr.push_back(n.to_json());
  json::DumpOptions opts;
  opts.indent = 2;
  opts.ensure_ascii = false;
  std::string text = json::py_dumps(arr, opts);
  fsutil::AtomicWriteOptions wopts;
  wopts.fsync = true;
  wopts.owner_only = false;
  auto st = fsutil::atomic_write(path_, text, wopts);
  if (!st) log::error(kLog, "failed to save {}: {}", path_.string(), st.error().message);
  return st;
}

Status MemoryEngine::reload() {
  std::lock_guard<std::mutex> lock(mu_);
  auto profile = RuntimeProfile::load("memory", path_.parent_path());
  if (!profile) { profile_error_ = profile.error(); return profile.error(); }
  profile_ = std::move(*profile);
  profile_error_.reset();
  load_locked();
  return ok_status();
}

// ── Internal lookups (mu_ already held) ────────────────────────────────

namespace {
bool parent_eq(const std::optional<std::string>& a, std::optional<std::string_view> b) {
  if (!a && !b) return true;
  if (!a || !b) return false;
  return *a == *b;
}
}  // namespace

// ── CRUD ────────────────────────────────────────────────────────────────

Result<std::string> MemoryEngine::add_node(std::string_view content, std::optional<std::string> parent_id,
                                           std::optional<std::string_view> node_type, Json metadata,
                                           std::vector<std::string> tags) {
  std::lock_guard<std::mutex> lock(mu_);
  if (profile_error_) return *profile_error_;
  const auto& values = profile_->values();
  const auto resolved_type = node_type ? std::string(*node_type) : values.at("default_node_type").get<std::string>();
  std::string nid = random_hex(values.at("id_chars").get<std::size_t>());
  int depth = 0;
  if (parent_id) {
    for (const auto& n : nodes_) {
      if (n.id == *parent_id) {
        depth = n.depth + 1;
        break;
      }
    }
  }

  std::vector<std::string> auto_tags;
  const auto& auto_types = values.at("auto_tag_node_types");
  if (analyzer_ && std::find(auto_types.begin(), auto_types.end(), Json(resolved_type)) != auto_types.end()) {
    auto_tags = analyzer_->extract_topics(content, values.at("auto_tag_threshold").get<int>());
  }

  MemoryNode node;
  node.id = nid;
  node.content = std::string(content);
  node.parent_id = std::move(parent_id);
  node.node_type = resolved_type;
  node.weight = values.at("default_weight").get<double>();
  node.depth = depth;
  node.metadata = std::move(metadata);
  node.tags = std::move(tags);
  for (auto& t : auto_tags) node.tags.push_back(std::move(t));
  node.created = timeutil::utc_now_iso();
  nodes_.push_back(node);
  auto st = save_locked();
  if (!st) return Error(st.error());
  log::debug(kLog, "Added memory node {} (type={}, depth={})", nid, node.node_type, depth);
  return nid;
}

Status MemoryEngine::update_node(std::string_view nid, const Json& fields) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const MemoryNode& n) { return n.id == nid; });
  if (it == nodes_.end()) {
    log::warn(kLog, "update_node: {} not found", std::string(nid));
    return Error(Errc::NotFound, "memory node not found: " + std::string(nid));
  }
  if (!fields.is_object()) return Error(Errc::InvalidArgument, "update fields must be a JSON object");
  for (auto entry = fields.begin(); entry != fields.end(); ++entry) {
    const std::string& k = entry.key();
    const Json& v = entry.value();
    if (k == "content" && v.is_string()) it->content = v.get<std::string>();
    else if (k == "parent_id") it->parent_id = v.is_string() ? std::optional<std::string>(v.get<std::string>()) : std::nullopt;
    else if (k == "node_type" && v.is_string()) it->node_type = v.get<std::string>();
    else if (k == "active" && v.is_boolean()) it->active = v.get<bool>();
    else if (k == "depth" && v.is_number()) it->depth = v.get<int>();
    else if (k == "weight" && v.is_number()) it->weight = v.get<double>();
    else if (k == "tags" && v.is_array()) {
      std::vector<std::string> tags;
      for (const auto& x : v) {
        if (x.is_string()) tags.push_back(x.get<std::string>());
      }
      it->tags = std::move(tags);
    } else if (k == "created" && v.is_string()) it->created = v.get<std::string>();
    else if (k == "metadata" && v.is_object()) it->metadata = v;
    // Unknown field names are ignored (Python hasattr() check).
  }
  return save_locked();
}

Status MemoryEngine::delete_node(std::string_view nid, bool recursive) {
  std::lock_guard<std::mutex> lock(mu_);
  bool found = std::any_of(nodes_.begin(), nodes_.end(), [&](const MemoryNode& n) { return n.id == nid; });
  if (!found) return ok_status();  // Python: silently returns, no save

  if (recursive) {
    std::function<void(const std::string&)> del_rec = [&](const std::string& id) {
      std::vector<std::string> kids;
      for (const auto& n : nodes_) {
        if (n.parent_id && *n.parent_id == id) kids.push_back(n.id);
      }
      for (const auto& k : kids) del_rec(k);
      nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const MemoryNode& n) { return n.id == id; }),
                  nodes_.end());
    };
    std::vector<std::string> children;
    for (const auto& n : nodes_) {
      if (n.parent_id && *n.parent_id == nid) children.push_back(n.id);
    }
    for (const auto& cid : children) del_rec(cid);
  }
  nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const MemoryNode& n) { return n.id == nid; }),
              nodes_.end());
  auto st = save_locked();
  log::debug(kLog, "Deleted memory node {} (recursive={})", std::string(nid), recursive);
  return st;
}

std::optional<MemoryNode> MemoryEngine::get_node(std::string_view nid) const {
  std::lock_guard<std::mutex> lock(mu_);
  for (const auto& n : nodes_) {
    if (n.id == nid) return n;
  }
  return std::nullopt;
}

Result<std::vector<MemoryNode>> MemoryEngine::get_children_checked(std::optional<std::string_view> parent_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (profile_error_) return *profile_error_;
  return children_locked(parent_id);
}
std::vector<MemoryNode> MemoryEngine::get_children(std::optional<std::string_view> parent_id) const {
  auto result = get_children_checked(parent_id);
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(*result);
}

std::vector<MemoryNode> MemoryEngine::get_all() const {
  std::lock_guard<std::mutex> lock(mu_);
  return nodes_;
}

// ── Context building ───────────────────────────────────────────────────

Result<std::string> MemoryEngine::get_active_context_checked(std::optional<std::size_t> requested_max_chars) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (profile_error_) return *profile_error_;
  const auto& values = profile_->values();
  const auto max_chars = requested_max_chars.value_or(values.at("context_max_chars").get<std::size_t>());
  std::vector<std::string> lines;
  std::size_t char_count = 0;
  std::optional<Error> render_error;
  std::function<void(const MemoryNode&, int)> walk = [&](const MemoryNode& node, int indent) {
    if (render_error || !node.active || char_count > max_chars) return;
    std::string prefix;
    for (int i = 0; i < indent; ++i) prefix += values.at("indent").get_ref<const std::string&>();
    std::string weight;
    if (node.weight != values.at("default_weight").get<double>()) {
      const auto formatted = std::format("{:.{}f}", node.weight, values.at("weight_precision").get<int>());
      auto rendered = render_profile_template(values.at("weight_template").get_ref<const std::string&>(), Json{{"weight", formatted}});
      if (!rendered) { render_error = rendered.error(); return; }
      weight = std::move(*rendered);
    }
    std::string tags;
    for (std::size_t i = 0; i < node.tags.size(); ++i) {
      tags += values.at(i ? "tag_separator" : "tag_prefix").get_ref<const std::string&>();
      tags += node.tags[i];
    }
    Json vars = node.to_json();
    vars["indent"] = prefix;
    vars["weight"] = weight;
    vars["tags"] = tags;
    vars["path"] = json::get_string(node.metadata, "path", "");
    const auto& renderers = values.at("renderers");
    const auto* selected = json::find(renderers, node.node_type);
    const auto& renderer = selected ? *selected : values.at("default_renderer");
    auto rendered = render_profile_template(renderer.get_ref<const std::string&>(), vars);
    if (!rendered) { render_error = rendered.error(); return; }
    char_count += utf8::length(*rendered);
    lines.push_back(std::move(*rendered));
    for (const auto& child : children_locked(node.id)) walk(child, indent + 1);
  };
  for (const auto& root : children_locked(std::nullopt)) walk(root, 0);
  if (render_error) return *render_error;
  std::string out;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i) out += values.at("line_separator").get_ref<const std::string&>();
    out += lines[i];
  }
  return out;
}

std::string MemoryEngine::get_active_context(std::optional<std::size_t> max_chars) const {
  auto result = get_active_context_checked(max_chars);
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(*result);
}

std::vector<MemoryNode> MemoryEngine::children_locked(std::optional<std::string_view> parent_id) const {
  std::vector<MemoryNode> out;
  for (const auto& n : nodes_) {
    if (parent_eq(n.parent_id, parent_id)) out.push_back(n);
  }
  const auto& values = profile_->values();
  const auto priority = [&](const MemoryNode& node) {
    const auto* found = json::find(values.at("sort_priorities"), node.node_type);
    return found ? found->get<int>() : values.at("default_sort_priority").get<int>();
  };
  std::stable_sort(out.begin(), out.end(), [&](const MemoryNode& a, const MemoryNode& b) {
    const auto ap = priority(a), bp = priority(b);
    if (ap != bp) return ap < bp;
    return a.created < b.created;
  });
  return out;
}

// ── Search ──────────────────────────────────────────────────────────────

Result<std::vector<MemoryNode>> MemoryEngine::search_checked(std::string_view query, std::optional<int> requested_limit) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (profile_error_) return *profile_error_;
  const auto limit = requested_limit.value_or(profile_->values().at("search_limit").get<int>());
  std::string q = utf8::to_lower(query);
  std::vector<MemoryNode> results;
  for (const auto& n : nodes_) {
    std::string content_lower = utf8::to_lower(n.content);
    if (content_lower.find(q) != std::string::npos) {
      results.push_back(n);
      if (static_cast<int>(results.size()) >= limit) break;
    }
  }
  return results;
}

// ── Graph data ──────────────────────────────────────────────────────────

Result<Json> MemoryEngine::get_graph_data_checked() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (profile_error_) return *profile_error_;
  const auto& values = profile_->values();
  Json nodes = Json::array();
  Json edges = Json::array();
  for (const auto& n : nodes_) {
    nodes.push_back(Json{{"id", n.id},
                         {"label", std::string(utf8::prefix(n.content, values.at("graph_label_chars").get<std::size_t>()))},
                         {"type", values.at("graph_node_type")},
                         {"node_type", n.node_type},
                         {"active", n.active},
                         {"weight", n.weight},
                         {"tags", n.tags}});
    if (n.parent_id) {
      edges.push_back(Json{{"src", *n.parent_id}, {"dst", n.id}, {"type", values.at("graph_child_relation")}, {"weight", values.at("graph_child_weight")}});
    }
  }
  return Json{{"nodes", nodes}, {"edges", edges}};
}

std::vector<MemoryNode> MemoryEngine::search(std::string_view query, std::optional<int> limit) const {
  auto result = search_checked(query, limit);
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(*result);
}
Json MemoryEngine::get_graph_data() const {
  auto result = get_graph_data_checked();
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(*result);
}
Result<Json> MemoryEngine::profile_inspection() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (profile_error_) return *profile_error_;
  return profile_->inspection();
}
Status MemoryEngine::profile_status() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (profile_error_) return *profile_error_;
  return {};
}

}  // namespace loom
