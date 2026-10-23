// A mock graph for the address tests (K3-1): the ports of address::Graph over small forests, one per dimension.
// Every dimension of a registry gets a forest over the same node ids u0..u<n-1>, so any node is an anchor on any dimension.
#pragma once

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "loom/address.h"
#include "loom/dimensions.h"

namespace k3mock {

struct Node {
  std::string parent;
  std::vector<std::string> kids;
  loom::Json key;
};
struct Forest {
  std::vector<std::string> roots;
  std::map<std::string, Node> nodes;
};

struct Mock {
  std::map<std::string, Forest> forests;
  std::map<std::string, std::map<std::string, std::string>> home;  // dim -> a node that is not in the forest -> the node that stands for it

  std::string place(const std::string& dim, const std::string& node) const {
    const auto f = forests.find(dim);
    if (f == forests.end()) return "";
    if (f->second.nodes.count(node)) return node;
    const auto h = home.find(dim);
    if (h != home.end()) {
      const auto m = h->second.find(node);
      if (m != h->second.end()) return m->second;
    }
    return "";
  }

  loom::address::Graph ports() const {
    loom::address::Graph g;
    g.neighbours = [this](const std::string& dim, const std::string& node) {
      const std::string p = place(dim, node);
      if (p.empty()) return std::vector<std::string>{};
      const Forest& f = forests.at(dim);
      const Node& n = f.nodes.at(p);
      return n.parent.empty() ? f.roots : f.nodes.at(n.parent).kids;
    };
    g.children = [this](const std::string& dim, const std::string& node) {
      const std::string p = place(dim, node);
      return p.empty() ? std::vector<std::string>{} : forests.at(dim).nodes.at(p).kids;
    };
    g.lineage = [this](const std::string& dim, const std::string& node) {
      std::vector<std::string> chain;
      std::string p = place(dim, node);
      if (p.empty()) return chain;
      const Forest& f = forests.at(dim);
      for (; !p.empty(); p = f.nodes.at(p).parent) chain.insert(chain.begin(), p);
      return chain;
    };
    g.key = [this](const std::string& dim, const std::string& node) {
      const std::string p = place(dim, node);
      return p.empty() ? loom::Json(nullptr) : forests.at(dim).nodes.at(p).key;
    };
    return g;
  }

  // roots in order, then each node's kids: built from a tree description {id, key, kids}.
  void add_root(const std::string& dim, const std::string& id, loom::Json key) {
    forests[dim].roots.push_back(id);
    forests[dim].nodes[id] = Node{"", {}, std::move(key)};
  }
  void add_kid(const std::string& dim, const std::string& parent, const std::string& id, loom::Json key) {
    forests[dim].nodes[parent].kids.push_back(id);
    forests[dim].nodes[id] = Node{parent, {}, std::move(key)};
  }

  static std::string uid(int i) { return "u" + std::to_string(i); }

  static loom::Json flat_key(const loom::dimensions::Dimension& d, int i) {
    char buf[64];
    if (d.effective_codec == "integer") return i;
    if (d.effective_codec == "double") return i * 0.25;
    if (d.effective_codec == "datetime") {
      std::snprintf(buf, sizeof buf, "2026-03-%02dT%02d:00Z", 1 + i / 24, i % 24);
      return std::string(buf);
    }
    if (loom::json::get_string(d.order, "by") == "list" && d.order.contains("list") && !d.order["list"].empty())
      return d.order["list"][static_cast<std::size_t>(i) % d.order["list"].size()];
    std::snprintf(buf, sizeof buf, "k%02d", i);
    return std::string(buf);
  }

  // One forest per dimension of the registry over n nodes. Flat dimensions: all nodes as roots in a rotated order.
  // Levels dimensions: a tree (3 roots, 3 kids each, as deep as the levels) filled breadth first.
  static Mock build(const loom::dimensions::Registry& reg, int n) {
    Mock m;
    int shift = 0;
    for (const auto& id : reg.ids()) {
      const auto& d = *reg.find(id);
      ++shift;
      if (d.effective_codec == "levels" && !d.levels.empty()) {
        int next = 0;
        std::vector<std::string> frontier;
        for (int r = 0; r < 3 && next < n; ++r) {
          const std::string u = uid(next++);
          m.add_root(id, u, r);
          frontier.push_back(u);
        }
        for (std::size_t depth = 1; depth < d.levels.size(); ++depth) {
          std::vector<std::string> deeper;
          for (const auto& p : frontier)
            for (int k = 0; k < 3 && next < n; ++k) {
              const std::string u = uid(next++);
              m.add_kid(id, p, u, k);
              deeper.push_back(u);
            }
          frontier = deeper;
        }
      } else {
        for (int i = 0; i < n; ++i) m.add_root(id, uid((i + shift * 5) % n), flat_key(d, i));
      }
    }
    return m;
  }
};

}  // namespace k3mock
