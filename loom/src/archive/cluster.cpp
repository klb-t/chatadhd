// Deterministic Louvain community detection (Blondel et al. 2008) for theme
// clustering. Nodes are visited in index order, candidate communities in
// ascending id order, and a node only moves for a strictly positive gain, so
// the same graph always yields the same partition.
#include <algorithm>
#include <map>

#include "archive/archive_internal.h"

namespace loom::archive {

namespace {
using Adj = std::vector<std::map<int, double>>;  // ordered neighbours

// One level of local moving. Returns true if any node moved.
bool one_level(const Adj& adj, std::vector<int>& comm) {
  const int n = static_cast<int>(adj.size());
  std::vector<double> k(n, 0.0);
  double m2 = 0.0;
  for (int i = 0; i < n; ++i) {
    for (const auto& [j, w] : adj[i]) k[i] += w;
    m2 += k[i];
  }
  if (m2 <= 0) return false;
  std::vector<double> tot(n, 0.0);
  for (int i = 0; i < n; ++i) tot[comm[i]] += k[i];
  bool improved = false;
  for (int pass = 0; pass < 100; ++pass) {
    bool moved = false;
    for (int i = 0; i < n; ++i) {
      int old = comm[i];
      std::map<int, double> wc;  // community -> weight from i (ascending ids)
      for (const auto& [j, w] : adj[i]) {
        if (j == i) continue;
        wc[comm[j]] += w;
      }
      tot[old] -= k[i];
      double stay = (wc.count(old) ? wc[old] : 0.0) - tot[old] * k[i] / m2;
      int best = old;
      double best_gain = stay;
      for (const auto& [c, w] : wc) {
        if (c == old) continue;
        double gain = w - tot[c] * k[i] / m2;
        if (gain > best_gain + 1e-12) {
          best = c;
          best_gain = gain;
        }
      }
      comm[i] = best;
      tot[best] += k[i];
      if (best != old) moved = true;
    }
    if (!moved) break;
    improved = true;
  }
  return improved;
}
}  // namespace

std::vector<int> louvain(int n, const std::vector<std::tuple<int, int, double>>& edges, int max_levels) {
  std::vector<int> result(static_cast<std::size_t>(std::max(0, n)));
  for (int i = 0; i < n; ++i) result[i] = i;
  if (n <= 0) return result;
  Adj adj(n);
  for (const auto& [a, b, w] : edges) {
    if (a < 0 || b < 0 || a >= n || b >= n || w <= 0) continue;
    if (a == b) {
      adj[a][a] += w;
    } else {
      adj[a][b] += w;
      adj[b][a] += w;
    }
  }
  for (int level = 0; level < max_levels; ++level) {
    const int m = static_cast<int>(adj.size());
    std::vector<int> comm(m);
    for (int i = 0; i < m; ++i) comm[i] = i;
    if (!one_level(adj, comm)) break;
    // renumber by first appearance
    std::map<int, int> ren;
    for (int i = 0; i < m; ++i) {
      if (!ren.count(comm[i])) {
        int next = static_cast<int>(ren.size());
        ren[comm[i]] = next;
      }
    }
    for (int& c : comm) c = ren[c];
    for (int& r : result) r = comm[r];
    const int k = static_cast<int>(ren.size());
    if (k == m) break;
    Adj next(k);
    for (int i = 0; i < m; ++i) {
      for (const auto& [j, w] : adj[i]) next[comm[i]][comm[j]] += w;
    }
    adj = std::move(next);
  }
  // final renumbering by first appearance over original nodes
  std::map<int, int> ren;
  for (int& r : result) {
    if (!ren.count(r)) {
      int next = static_cast<int>(ren.size());
      ren[r] = next;
    }
    r = ren[r];
  }
  return result;
}

}  // namespace loom::archive
