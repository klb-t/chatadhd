// Edge kinds and perspectives as graph data. See include/loom/perspectives.h.
#include "loom/perspectives.h"

#include <algorithm>
#include <deque>
#include <unordered_map>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/scope_resolver.h"

namespace loom::views {
namespace {

std::vector<std::string> strings_of(const Json& v) {
  std::vector<std::string> out;
  if (v.is_array())
    for (const auto& s : v)
      if (s.is_string()) out.push_back(s.get<std::string>());
  return out;
}

}  // namespace

// ── edge kinds ──────────────────────────────────────────────────────────────

Result<EdgeKinds> EdgeKinds::open(const std::filesystem::path& root, const std::vector<std::filesystem::path>& extra_dirs) {
  EdgeKinds k;
  LOOM_TRY_ASSIGN(k.view_, effective(root, "edge_kinds", extra_dirs));
  return k;
}

std::string EdgeKinds::canonical(const std::string& name) const {
  const EffectiveEntry* e = view_.find(name);
  return e ? e->id : std::string();
}

std::set<std::string> EdgeKinds::expand(const std::string& name) const {
  std::set<std::string> out;
  const std::string root = canonical(name);
  if (root.empty()) {
    out.insert(name);  // an emergent kind selects itself
    return out;
  }
  std::deque<std::string> todo{root};
  while (!todo.empty()) {
    const std::string cur = todo.front();
    todo.pop_front();
    const EffectiveEntry* self = view_.find(cur);
    if (!self || self->disabled || !out.insert(cur).second) continue;
    for (const auto& e : view_.entries)
      if (!e.disabled && canonical(json::get_string(e.value, "parent")) == cur) todo.push_back(e.id);
  }
  return out;
}

std::string EdgeKinds::inverse_of(const std::string& name) const {
  const EffectiveEntry* e = view_.find(name);
  return e ? canonical(json::get_string(e->value, "inverse")) : std::string();
}

std::vector<std::string> EdgeKinds::ancestors(const std::string& name) const {
  std::vector<std::string> out;
  std::set<std::string> seen;
  const EffectiveEntry* e = view_.find(name);
  while (e) {
    const std::string parent = canonical(json::get_string(e->value, "parent"));
    if (parent.empty() || !seen.insert(parent).second) break;  // a cycle ends the chain; it is reported by describe()
    out.push_back(parent);
    e = view_.find(parent);
  }
  return out;
}

std::set<std::string> EdgeKinds::perspectives_of(const std::string& name, const Effective& perspectives) const {
  std::set<std::string> out;
  const std::string self = canonical(name);
  std::vector<std::string> line{self};
  for (const auto& a : ancestors(name)) line.push_back(a);
  for (const auto& id : line) {
    const EffectiveEntry* e = view_.find(id);
    if (!e) continue;
    for (const auto& p : strings_of(e->value.value("perspectives", Json::array()))) out.insert(p);
  }
  for (const auto* p : perspectives.active())
    for (const auto& rel : strings_of(p->value.value("relations", Json::array()))) {
      const auto sel = expand(rel);
      if (sel.count(self.empty() ? name : self)) out.insert(p->id);
    }
  return out;
}

Json EdgeKinds::describe(const std::string& name, const Effective& perspectives) const {
  const EffectiveEntry* e = view_.find(name);
  if (!e) return Json{{"id", name}, {"known", false}};
  Json o = e->to_json();
  o["known"] = true;
  o["inverse"] = inverse_of(name);
  const auto anc = ancestors(name);
  o["ancestors"] = anc;
  Json children = Json::array();
  for (const auto& c : view_.entries)
    if (canonical(json::get_string(c.value, "parent")) == e->id) children.push_back(c.id);
  o["children"] = children;
  Json issues = Json::array();
  const std::string parent = json::get_string(e->value, "parent");
  if (!parent.empty() && canonical(parent).empty()) issues.push_back(Json{{"code", "parent_unknown"}, {"parent", parent}});
  if (!anc.empty() && std::find(anc.begin(), anc.end(), e->id) != anc.end()) issues.push_back(Json{{"code", "parent_cycle"}});
  const std::string inv = json::get_string(e->value, "inverse");
  if (!inv.empty() && canonical(inv).empty()) issues.push_back(Json{{"code", "inverse_unknown"}, {"inverse", inv}});
  if (!issues.empty()) o["issues"] = issues;
  Json ps = Json::array();
  for (const auto& p : perspectives_of(name, perspectives)) ps.push_back(p);
  o["perspectives"] = ps;
  return o;
}

Json EdgeKinds::classify(const std::map<std::string, std::size_t>& used, const Effective& perspectives) const {
  Json known = Json::array(), unclassified = Json::array();
  for (const auto& [type, count] : used) {
    const std::string kind = canonical(type);
    if (kind.empty()) {
      unclassified.push_back(Json{{"type", type}, {"count", count}});
    } else {
      Json ps = Json::array();
      for (const auto& p : perspectives_of(kind, perspectives)) ps.push_back(p);
      known.push_back(Json{{"type", type}, {"kind", kind}, {"count", count}, {"perspectives", ps}});
    }
  }
  return Json{{"schema", "loom.edge_kind_classification/1"}, {"known", known}, {"unclassified", unclassified}};
}

Result<std::map<std::string, std::size_t>> link_types_in_use(Database& db) {
  LOOM_TRY_ASSIGN(auto links, db.get_links());
  std::map<std::string, std::size_t> out;
  for (const auto& l : links) ++out[l.link_type];
  return out;
}

// ── perspectives ────────────────────────────────────────────────────────────

Result<Perspectives> Perspectives::open(const std::filesystem::path& root, const std::vector<std::filesystem::path>& extra_dirs) {
  Perspectives p;
  LOOM_TRY_ASSIGN(p.kinds_, EdgeKinds::open(root, extra_dirs));
  LOOM_TRY_ASSIGN(p.view_, effective(root, "perspectives", extra_dirs));
  return p;
}

Result<std::set<std::string>> Perspectives::edge_kinds_of(const std::string& id) const {
  const EffectiveEntry* p = view_.find(id);
  if (!p) return Error(Errc::NotFound, "no such perspective: " + id);
  std::set<std::string> out;
  for (const auto& rel : strings_of(p->value.value("relations", Json::array())))
    for (const auto& k : kinds_.expand(rel)) out.insert(k);
  for (const auto& e : kinds_.view().entries) {
    if (e.disabled) continue;
    for (const auto& pid : strings_of(e.value.value("perspectives", Json::array())))
      if (pid == p->id) for (const auto& k : kinds_.expand(e.id)) out.insert(k);
  }
  return out;
}

Json Perspectives::list() const {
  Json out = Json::array();
  for (const auto& e : view_.entries) {
    Json o = e.to_json();
    std::set<std::string> kinds;
    if (auto ks = edge_kinds_of(e.id)) kinds = *ks;
    o["edge_kinds"] = Json::array();
    for (const auto& k : kinds) o["edge_kinds"].push_back(k);
    // the shape of graph.perspective.traversal, read unchanged by the web layers
    o["traversal"] = Json{{"relations", o["edge_kinds"]}, {"direction", e.value.value("direction", "out")}, {"hops", e.value.value("hops", Json(nullptr))}};
    out.push_back(std::move(o));
  }
  return out;
}

namespace {

bool node_passes(const Json& filter, const Node& n) {
  const auto kinds = strings_of(filter.value("kinds", Json::array()));
  const auto excluded = strings_of(filter.value("exclude_kinds", Json::array()));
  if (!kinds.empty() && std::find(kinds.begin(), kinds.end(), n.kind) == kinds.end()) return false;
  if (std::find(excluded.begin(), excluded.end(), n.kind) != excluded.end()) return false;
  const std::string needle = json::get_string(filter, "label_contains");
  return needle.empty() || n.label.find(needle) != std::string::npos;
}

}  // namespace

Result<Json> Perspectives::render(const RenderContext& ctx, const std::string& id, const Json& request) const {
  const EffectiveEntry* p = view_.find(id);
  if (!p) return Error(Errc::NotFound, "no such perspective: " + id);
  if (p->disabled) return Error(Errc::Unavailable, "perspective " + p->id + " is disabled");
  const std::string source = json::get_string(p->value, "source");
  if (source == "scope_chain") {
    const std::string key = json::get_string(request, "key");
    if (key.empty()) return Error(Errc::InvalidArgument, "perspective " + p->id + " needs request.key (the setting to trace)");
    LOOM_TRY_ASSIGN(auto resolver, scopes::Resolver::open(ctx.root, ctx.cfg, ctx.db));
    scopes::Binding b;
    b.key = key;
    b.conv_id = json::get_string(request, "conversation_id");
    if (request.contains("units")) b.units = request["units"];
    if (request.contains("installation")) b.installation = request["installation"];
    if (request.contains("unit_values")) b.unit_values = request["unit_values"];
    LOOM_TRY_ASSIGN(auto out, resolver.inheritance(b));
    out["perspective"] = p->id;
    out["render"] = p->value.value("render", Json::object());
    return out;
  }
  if (!source.empty()) return Error(Errc::Unsupported, "perspective " + p->id + ": unknown source mechanism '" + source + "'");
  if (!ctx.db) return Error(Errc::InvalidArgument, "rendering a perspective needs a graph");
  LOOM_TRY_ASSIGN(auto kinds, edge_kinds_of(p->id));
  std::string direction = request.contains("direction") ? json::get_string(request, "direction", "out") : json::get_string(p->value, "direction", "out");
  Json hops_j = request.contains("hops") ? request["hops"] : p->value.value("hops", Json(nullptr));
  const bool bounded = hops_j.is_number();
  const std::int64_t hops = bounded ? hops_j.get<std::int64_t>() : 0;
  const Json filter = p->value.value("node_filter", Json::object());

  LOOM_TRY_ASSIGN(auto links, ctx.db->get_links());
  std::vector<const Link*> chosen;
  for (const auto& l : links)
    if (kinds.count(l.link_type)) chosen.push_back(&l);

  std::vector<std::string> seeds = strings_of(request.value("seeds", Json::array()));
  std::set<std::string> reached(seeds.begin(), seeds.end());
  std::set<const Link*> kept;
  if (seeds.empty()) {
    for (const auto* l : chosen) { reached.insert(l->src); reached.insert(l->dst); kept.insert(l); }
  } else {
    std::deque<std::pair<std::string, std::int64_t>> todo;
    for (const auto& s : seeds) todo.push_back({s, 0});
    std::set<std::string> seen(seeds.begin(), seeds.end());
    while (!todo.empty()) {
      auto [cur, depth] = todo.front();
      todo.pop_front();
      if (bounded && depth >= hops) continue;
      for (const auto* l : chosen) {
        const bool out_edge = l->src == cur && (direction == "out" || direction == "both");
        const bool in_edge = l->dst == cur && (direction == "in" || direction == "both");
        if (!out_edge && !in_edge) continue;
        kept.insert(l);
        const std::string& next = out_edge ? l->dst : l->src;
        reached.insert(next);
        if (seen.insert(next).second) todo.push_back({next, depth + 1});
      }
    }
  }
  std::unordered_map<std::string, std::optional<Node>> nodes;
  auto node_of = [&](const std::string& nid) -> Result<const Node*> {
    auto it = nodes.find(nid);
    if (it == nodes.end()) {
      LOOM_TRY_ASSIGN(auto n, ctx.db->get_node(nid));
      it = nodes.emplace(nid, std::move(n)).first;
    }
    return it->second ? &*it->second : nullptr;
  };
  Json nj = Json::array(), ej = Json::array();
  std::set<std::string> shown;
  const std::set<std::string> seed_set(seeds.begin(), seeds.end());
  const std::int64_t limit = request.contains("limit") && request["limit"].is_number() ? request["limit"].get<std::int64_t>() : -1;
  bool truncated = false;
  for (const auto& nid : reached) {
    LOOM_TRY_ASSIGN(const Node* n, node_of(nid));
    if (!n) continue;
    if (!seed_set.count(nid) && !node_passes(filter, *n)) continue;
    if (limit >= 0 && static_cast<std::int64_t>(nj.size()) >= limit) { truncated = true; break; }
    nj.push_back(Json{{"id", n->id}, {"kind", n->kind}, {"label", n->label}});
    shown.insert(nid);
  }
  for (const auto* l : chosen) {
    if (!kept.count(l) || !shown.count(l->src) || !shown.count(l->dst)) continue;
    Json e{{"id", l->id}, {"src", l->src}, {"dst", l->dst}, {"type", l->link_type}, {"kind", kinds_.canonical(l->link_type)}, {"weight", l->weight}};
    const std::string inv = kinds_.inverse_of(l->link_type);
    if (!inv.empty()) e["inverse"] = inv;
    ej.push_back(std::move(e));
  }
  Json ks = Json::array();
  for (const auto& k : kinds) ks.push_back(k);
  Json out{{"schema", "loom.perspective_render/1"}, {"perspective", p->id}, {"label", p->value.value("label", Json(p->id))},
           {"edge_kinds", ks}, {"direction", direction}, {"hops", hops_j}, {"render", p->value.value("render", Json::object())},
           {"nodes", nj}, {"edges", ej}};
  if (truncated) out["truncated"] = true;
  return out;
}

Result<Json> Perspectives::render_many(const RenderContext& ctx, const std::vector<std::string>& ids, const Json& request) const {
  Json all = Json::object();
  for (const auto& id : ids) {
    LOOM_TRY_ASSIGN(auto r, render(ctx, id, request));
    all[id] = std::move(r);
  }
  return Json{{"schema", "loom.perspectives_side_by_side/1"}, {"perspectives", all}};
}

}  // namespace loom::views
