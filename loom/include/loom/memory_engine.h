// loom/memory_engine.h — port of engine/memory_engine.py.  [OWNER: wave 2 semantic/graph]
//
// Hierarchical memory tree persisted to memory.json (shared with Python):
//   file = JSON array of asdict(MemoryNode) in insertion order, written with
//   json.dumps(indent=2, ensure_ascii=False) atomically (memory.tmp -> rename).
//   Loading ignores unknown fields; a corrupt file logs and yields an empty tree.
// Behaviour to preserve:
//   add_node(content, parent_id, node_type="text", metadata, tags):
//     id = 12 hex chars (no prefix); depth = parent.depth + 1 when the parent
//     exists else 0; for node_type "text" tags += analyzer.extract_topics(
//     content, threshold=1) (after the given tags); created = utcnow iso + "Z";
//     saves.
//   update_node(nid, **kwargs): unknown nid -> warning (Loom: NotFound);
//     only existing attribute names are set; saves.
//   delete_node(nid, recursive=False): recursive deletes descendants first;
//     non-recursive leaves children orphaned (Python behaviour); saves.
//   get_children(parent_id=None): sorted by (node_type != "folder", created).
//   get_active_context(max_chars=16000): depth-first from roots
//     (get_children(None)); skip inactive nodes (and their subtree) and stop
//     adding once char_count > max_chars; line formats:
//       folder: "{indent}[{content}]{w}{tags}"
//       file:   "{indent}FILE: {content} ({metadata.path}){w}"
//       dir:    "{indent}DIR: {content} ({metadata.path}){w}"
//       text:   "{indent}{content}{w}{tags}"
//     indent = "  " * level, w = " [w:{weight:.1f}]" when weight != 1.0,
//     tags = " #" + " #".join(tags) when tags; joined with "\n".
//   search(query, limit=10): case-insensitive substring over content, in
//     insertion order.
//   get_graph_data(): {"nodes":[{"id","label":content[:25],"type":"memory",
//     "node_type","active","weight","tags"}], "edges":[{"src":parent,"dst":id,
//     "type":"child","weight":1.0}]}
// Thread-safe (internal mutex).
#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/runtime_profile.h"
#include "loom/util/json.h"

namespace loom {

class SemanticAnalyzer;

struct MemoryNode {
  std::string id;
  std::string content;
  std::optional<std::string> parent_id;
  std::string node_type = "text";  // text | folder | file | dir
  bool active = true;
  int depth = 0;
  double weight = 1.0;
  std::vector<std::string> tags;
  std::string created;
  Json metadata = Json::object();

  // asdict() field order: id, content, parent_id, node_type, active, depth,
  // weight, tags, created, metadata.
  Json to_json() const;
  static Result<MemoryNode> from_json(const Json& j);  // unknown keys ignored; id+content required
};

class MemoryEngine {
 public:
  // Recipe: built-in memory profile, then <memory parent>/profiles/memory.pack.
  // An invalid existing overlay is retained as profile_status() error. Checked
  // operations use Result; old value-returning wrappers throw its original
  // message rather than inventing an empty context. Raw get_all/get_node still
  // expose retained source records when rendering is unavailable.
  explicit MemoryEngine(std::filesystem::path path, const SemanticAnalyzer* analyzer = nullptr);

  Result<std::string> add_node(std::string_view content, std::optional<std::string> parent_id = std::nullopt,
                               std::optional<std::string_view> node_type = std::nullopt, Json metadata = Json::object(),
                               std::vector<std::string> tags = {});
  // `fields`: any subset of MemoryNode keys except "id" (unknown keys ignored,
  // as Python's hasattr() check).
  Status update_node(std::string_view nid, const Json& fields);
  Status delete_node(std::string_view nid, bool recursive = false);
  std::optional<MemoryNode> get_node(std::string_view nid) const;
  std::vector<MemoryNode> get_children(std::optional<std::string_view> parent_id = std::nullopt) const;
  Result<std::vector<MemoryNode>> get_children_checked(std::optional<std::string_view> parent_id = std::nullopt) const;
  std::vector<MemoryNode> get_all() const;
  std::string get_active_context(std::optional<std::size_t> max_chars = std::nullopt) const;
  Result<std::string> get_active_context_checked(std::optional<std::size_t> max_chars = std::nullopt) const;
  std::vector<MemoryNode> search(std::string_view query, std::optional<int> limit = std::nullopt) const;
  Result<std::vector<MemoryNode>> search_checked(std::string_view query, std::optional<int> limit = std::nullopt) const;
  Json get_graph_data() const;
  Result<Json> get_graph_data_checked() const;
  Result<Json> profile_inspection() const;
  Status profile_status() const;

  Status reload();  // re-read memory.json (e.g. after the Python app changed it)
  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  Status save_locked() const;
  void load_locked();
  // mu_ must already be held.
  std::vector<MemoryNode> children_locked(std::optional<std::string_view> parent_id) const;

  std::filesystem::path path_;
  const SemanticAnalyzer* analyzer_;
  mutable std::mutex mu_;
  std::vector<MemoryNode> nodes_;  // insertion order (Python dict order)
  std::optional<RuntimeProfile> profile_;
  std::optional<Error> profile_error_;
};

}  // namespace loom
