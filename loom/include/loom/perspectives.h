// loom/perspectives.h — edge kinds as graph data, and perspectives as named selections over them (P24, b, c).
//
// Edge kinds (effective view `edge_kinds`): each has a meaning {en, pl}, a direction (directed | symmetric), an inverse kind, a parent kind
// (sub-kinds inherit membership in what their parent belongs to: a perspective that selects `dependency` gets imports, requires ...) and the
// perspectives it belongs to. The basic kinds are the ROM (data/runtime/graph_views.pack); others come from memory overlays. A link type
// found in the data that no kind describes is NOT refused: it is emergent and appears in the `unclassified` bucket of classify().
//
// A perspective (effective view `perspectives`) is a named selection: {label, relations: [edge kind ids, expanded to sub-kinds],
// direction: out | in | both, hops: depth (null = unbounded), node_filter: {kinds?, exclude_kinds?, label_contains?}, render: hint for the
// client {layout, orientation, edge_style, ...}, source?: a mechanism that computes the nodes (scope_chain = "dziedziczenie")}.
// It reuses the shape of graph_perspectives (`graph.perspective.traversal` = {relations, direction, hops}); each listed perspective also
// carries that `traversal` layer so the existing web layers read it unchanged. Perspectives are shown side by side: render_many().
#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "loom/effective_view.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Config;
class Database;
}

namespace loom::views {

class EdgeKinds {
 public:
  static Result<EdgeKinds> open(const std::filesystem::path& root, const std::vector<std::filesystem::path>& extra_dirs = {});

  const Effective& view() const { return view_; }
  // Canonical id of a kind name (alias resolved) or "" when no kind describes it.
  std::string canonical(const std::string& name) const;
  // The kind and all its sub-kinds that are not disabled (names that are not kinds expand to themselves: emergent kinds work in selections).
  std::set<std::string> expand(const std::string& name) const;
  std::string inverse_of(const std::string& name) const;
  // The kind's ancestors, nearest first.
  std::vector<std::string> ancestors(const std::string& name) const;
  // Perspectives the kind belongs to: its own list, its ancestors' lists, and perspectives whose relations select it (or an ancestor).
  std::set<std::string> perspectives_of(const std::string& name, const Effective& perspectives) const;
  // {id, entry, origin, provenance, ancestors, children, inverse, perspectives}
  Json describe(const std::string& name, const Effective& perspectives) const;
  // used: link type -> count. {known:[{type,kind,count,perspectives}], unclassified:[{type,count}]}
  Json classify(const std::map<std::string, std::size_t>& used, const Effective& perspectives) const;

 private:
  Effective view_;
};

struct RenderContext {
  Database* db = nullptr;             // the graph (nodes, links)
  const Config* cfg = nullptr;        // for scope-chain perspectives
  std::filesystem::path root;         // data dir
};

class Perspectives {
 public:
  static Result<Perspectives> open(const std::filesystem::path& root, const std::vector<std::filesystem::path>& extra_dirs = {});

  const EdgeKinds& kinds() const { return kinds_; }
  const Effective& view() const { return view_; }
  // Edge kinds a perspective selects (relations expanded + kinds that declare membership), sorted.
  Result<std::set<std::string>> edge_kinds_of(const std::string& id) const;
  // [{id, label, relations, edge_kinds, direction, hops, node_filter, render, source?, traversal, origin, provenance, disabled?}]
  Json list() const;
  // Applies a perspective to the graph. request: {seeds?: [node ids], hops?, direction?, limit?, key?, conversation_id?, units?}
  // -> {schema, perspective, edge_kinds, nodes, edges, unclassified?} ; scope_chain perspectives need request.key.
  Result<Json> render(const RenderContext& ctx, const std::string& id, const Json& request) const;
  // Several perspectives over the same request, side by side: {schema, perspectives: {id: result}}
  Result<Json> render_many(const RenderContext& ctx, const std::vector<std::string>& ids, const Json& request) const;

 private:
  EdgeKinds kinds_;
  Effective view_;
};

// Link types in use: {type: count} over all links of the graph.
Result<std::map<std::string, std::size_t>> link_types_in_use(Database& db);

}  // namespace loom::views
