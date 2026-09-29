// catalog_internal.h: build_links — the linking pass of Catalog::score
// (proposal_scale.md §5.5) without an all-pairs scan.
//
// Link types (unchanged semantics):
//   same_project  both units carry the same non-empty project_ext_id
//   continuation  MinHash Jaccard >= link_minhash_jaccard
//   shared_rare   same platform and >= link_shared_rare_terms rare terms in
//                 both top-K term sets ("rare": idf above the corpus floor)
//   same_session  same platform, timestamps within link_same_session_hours
//
// Two modes with the same output on archives up to `exact_max_units`:
//   * exhaustive (n <= exact_max_units): every pair, the reference semantics;
//   * candidate generation (larger archives): LSH banding on the MinHash
//     lanes for continuation, an inverted index on rare terms for
//     shared_rare, a time-sorted sweep for same_session, and a group table
//     for same_project. Candidates are always verified with the exact
//     predicate, so a candidate path can miss a link (LSH recall at the
//     threshold is >= 99.6 % with 2-lane bands) but never invent one.
// Pathological fan-out is bounded (mass imports sharing one timestamp,
// Claude projects with thousands of chats): same_session keeps the
// `session_max_neighbours` nearest units per unit and same_project groups
// above `project_max_clique` are returned as groups so the caller can
// aggregate instead of materialising a clique.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

#include "catalog_internal.h"

namespace loom::catalog::internal {

namespace {

// Exact predicate of the reference implementation: agree / valid lanes.
double jaccard_of(const MinHash& a, const MinHash& b) { return a.jaccard(b); }

std::uint64_t pair_key(int i, int j) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)) << 32) | static_cast<std::uint32_t>(j); }

int type_rank(const std::string& t) {
  if (t == "same_project") return 0;
  if (t == "continuation") return 1;
  if (t == "shared_rare") return 2;
  return 3;
}

}  // namespace

LinkResult build_links(const std::vector<LinkUnit>& units, const LinkParams& params) {
  LinkResult out;
  const int n = static_cast<int>(units.size());
  const bool exhaustive = static_cast<std::size_t>(n) <= params.exact_max_units;
  std::int64_t verified_pairs = 0, lsh_candidates = 0, rare_candidates = 0;

  // ── rare terms per unit (ids, sorted) ─────────────────────────────────
  // idf of the reference implementation: ln(1 + (N - df + .5) / (df + .5)),
  // rare when idf > ln(N / 20) (0 for N <= 20).
  std::unordered_map<std::string, int> term_df;
  for (const auto& u : units) {
    if (!u.sketch) continue;
    for (const auto& [t, tf] : u.sketch->top_terms) ++term_df[t];
  }
  const double rare_floor = n > 20 ? std::log(static_cast<double>(n) / 20.0) : 0.0;
  std::unordered_map<std::string, int> rare_id;
  std::vector<std::vector<int>> rare_terms(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const Sketch* sk = units[static_cast<std::size_t>(i)].sketch;
    if (!sk) continue;
    for (const auto& [t, tf] : sk->top_terms) {
      double df = term_df[t];
      double idf = std::log(1.0 + (static_cast<double>(n) - df + 0.5) / (df + 0.5));
      if (idf > rare_floor) {
        auto [it, fresh] = rare_id.emplace(t, static_cast<int>(rare_id.size()));
        rare_terms[static_cast<std::size_t>(i)].push_back(it->second);
      }
    }
    std::sort(rare_terms[static_cast<std::size_t>(i)].begin(), rare_terms[static_cast<std::size_t>(i)].end());
  }
  auto shared_rare_count = [&](int a, int b) {
    const auto& x = rare_terms[static_cast<std::size_t>(a)];
    const auto& y = rare_terms[static_cast<std::size_t>(b)];
    int c = 0;
    std::size_t p = 0, q = 0;
    while (p < x.size() && q < y.size()) {
      if (x[p] == y[q]) { ++c; ++p; ++q; }
      else if (x[p] < y[q]) ++p;
      else ++q;
    }
    return c;
  };

  auto add_continuation = [&](int i, int j) {
    ++verified_pairs;
    double jac = jaccard_of(units[static_cast<std::size_t>(i)].sketch->minhash, units[static_cast<std::size_t>(j)].sketch->minhash);
    if (jac >= params.min_jaccard) out.links.push_back({i, j, "continuation", jac});
  };
  auto add_shared_rare = [&](int i, int j, int count) {
    const auto& a = units[static_cast<std::size_t>(i)];
    const auto& b = units[static_cast<std::size_t>(j)];
    if (a.platform != b.platform) return;
    if (count >= params.shared_rare_min) out.links.push_back({i, j, "shared_rare", std::min(1.0, count / 10.0)});
  };
  auto session_strength = [&](int i, int j, double* strength) {
    const auto& a = units[static_cast<std::size_t>(i)];
    const auto& b = units[static_cast<std::size_t>(j)];
    if (a.platform != b.platform || !a.time || !b.time) return false;
    double hours = std::abs(*a.time - *b.time) / 3600.0;
    if (hours > params.session_hours) return false;
    *strength = 1.0 - hours / (params.session_hours + 1.0);
    return true;
  };

  // ── same_project: group table ─────────────────────────────────────────
  {
    std::unordered_map<std::string, std::vector<int>> groups;
    for (int i = 0; i < n; ++i) {
      const auto& p = units[static_cast<std::size_t>(i)].project;
      if (!p.empty()) groups[p].push_back(i);
    }
    std::vector<const std::vector<int>*> ordered;
    for (const auto& [k, v] : groups) {
      if (v.size() >= 2) ordered.push_back(&v);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->front() < b->front(); });
    for (const auto* g : ordered) {
      if (static_cast<int>(g->size()) > params.project_max_clique) {
        out.project_groups.push_back(*g);
        continue;
      }
      for (std::size_t x = 0; x < g->size(); ++x) {
        for (std::size_t y = x + 1; y < g->size(); ++y) out.links.push_back({(*g)[x], (*g)[y], "same_project", 1.0});
      }
    }
  }

  if (exhaustive) {
    for (int i = 0; i < n; ++i) {
      const auto& a = units[static_cast<std::size_t>(i)];
      for (int j = i + 1; j < n; ++j) {
        const auto& b = units[static_cast<std::size_t>(j)];
        if (a.sketch && b.sketch) add_continuation(i, j);
        if (a.platform == b.platform) add_shared_rare(i, j, shared_rare_count(i, j));
        double s = 0.0;
        if (session_strength(i, j, &s)) out.links.push_back({i, j, "same_session", s});
      }
    }
  } else {
    // continuation: LSH banding over the MinHash lanes.
    const std::size_t lanes = [&] {
      for (const auto& u : units) {
        if (u.sketch && !u.sketch->minhash.sig.empty()) return u.sketch->minhash.sig.size();
      }
      return static_cast<std::size_t>(0);
    }();
    const std::size_t rows = static_cast<std::size_t>(std::max(1, params.lsh_rows));
    if (lanes >= rows) {
      std::vector<std::uint64_t> candidates;
      const std::size_t bands = lanes / rows;
      for (std::size_t band = 0; band < bands; ++band) {
        std::unordered_map<std::uint64_t, std::vector<int>> buckets;
        for (int i = 0; i < n; ++i) {
          const Sketch* sk = units[static_cast<std::size_t>(i)].sketch;
          if (!sk || sk->minhash.sig.size() != lanes) continue;
          std::uint64_t h = 1469598103934665603ULL;
          bool empty = true;
          for (std::size_t r = 0; r < rows; ++r) {
            std::uint64_t v = sk->minhash.sig[band * rows + r];
            if (v != UINT64_MAX) empty = false;
            h = (h ^ v) * 1099511628211ULL;
            h ^= h >> 29;
          }
          if (!empty) buckets[h].push_back(i);
        }
        std::vector<const std::vector<int>*> ordered;
        for (const auto& [k, v] : buckets) {
          if (v.size() >= 2) ordered.push_back(&v);
        }
        for (const auto* bucket : ordered) {
          const auto& v = *bucket;  // ascending unit index
          const std::size_t window = static_cast<std::size_t>(std::max(2, params.lsh_max_bucket));
          for (std::size_t x = 0; x < v.size(); ++x) {
            std::size_t stop = v.size() <= window ? v.size() : std::min(v.size(), x + 1 + window);
            for (std::size_t y = x + 1; y < stop; ++y) candidates.push_back(pair_key(v[x], v[y]));
          }
        }
      }
      std::sort(candidates.begin(), candidates.end());
      candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
      lsh_candidates = static_cast<std::int64_t>(candidates.size());
      for (std::uint64_t k : candidates) add_continuation(static_cast<int>(k >> 32), static_cast<int>(k & 0xffffffffu));
    }

    // shared_rare: inverted index on rare terms (posting lists are short by
    // construction: a rare term occurs in about <= 20 units).
    {
      std::vector<std::vector<int>> postings(rare_id.size());
      for (int i = 0; i < n; ++i) {
        for (int t : rare_terms[static_cast<std::size_t>(i)]) postings[static_cast<std::size_t>(t)].push_back(i);
      }
      std::vector<int> cand;
      for (int i = 0; i < n; ++i) {
        cand.clear();
        for (int t : rare_terms[static_cast<std::size_t>(i)]) {
          const auto& p = postings[static_cast<std::size_t>(t)];
          for (auto it = std::upper_bound(p.begin(), p.end(), i); it != p.end(); ++it) cand.push_back(*it);
        }
        std::sort(cand.begin(), cand.end());
        for (std::size_t x = 0; x < cand.size();) {
          std::size_t y = x;
          while (y < cand.size() && cand[y] == cand[x]) ++y;
          ++rare_candidates;
          add_shared_rare(i, cand[x], static_cast<int>(y - x));
          x = y;
        }
      }
    }

    // same_session: per platform, sorted by time; nearest `cap` neighbours.
    {
      std::unordered_map<std::string, std::vector<int>> by_platform;
      for (int i = 0; i < n; ++i) {
        if (units[static_cast<std::size_t>(i)].time) by_platform[units[static_cast<std::size_t>(i)].platform].push_back(i);
      }
      std::vector<std::string> names;
      for (const auto& [k, v] : by_platform) names.push_back(k);
      std::sort(names.begin(), names.end());
      std::vector<std::uint64_t> keys;
      for (const auto& name : names) {
        auto& v = by_platform[name];
        std::stable_sort(v.begin(), v.end(), [&](int a, int b) {
          double ta = *units[static_cast<std::size_t>(a)].time, tb = *units[static_cast<std::size_t>(b)].time;
          return ta != tb ? ta < tb : a < b;
        });
        const int cap = std::max(1, params.session_max_neighbours);
        for (std::size_t x = 0; x < v.size(); ++x) {
          // walk outwards, nearest first, both directions
          std::ptrdiff_t l = static_cast<std::ptrdiff_t>(x) - 1, r = static_cast<std::ptrdiff_t>(x) + 1;
          int taken = 0;
          const double tx = *units[static_cast<std::size_t>(v[x])].time;
          while (taken < cap) {
            double dl = l >= 0 ? std::abs(tx - *units[static_cast<std::size_t>(v[static_cast<std::size_t>(l)])].time) : 1e300;
            double dr = r < static_cast<std::ptrdiff_t>(v.size())
                            ? std::abs(*units[static_cast<std::size_t>(v[static_cast<std::size_t>(r)])].time - tx) : 1e300;
            if (dl > params.session_hours * 3600.0 && dr > params.session_hours * 3600.0) break;
            int other;
            if (dl <= dr) other = v[static_cast<std::size_t>(l--)];
            else other = v[static_cast<std::size_t>(r++)];
            ++taken;
            keys.push_back(pair_key(std::min(v[x], other), std::max(v[x], other)));
          }
        }
      }
      std::sort(keys.begin(), keys.end());
      keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
      for (std::uint64_t k : keys) {
        int i = static_cast<int>(k >> 32), j = static_cast<int>(k & 0xffffffffu);
        double s = 0.0;
        if (session_strength(i, j, &s)) out.links.push_back({i, j, "same_session", s});
      }
    }
  }

  std::sort(out.links.begin(), out.links.end(), [](const Link& a, const Link& b) {
    if (a.i != b.i) return a.i < b.i;
    if (a.j != b.j) return a.j < b.j;
    return type_rank(a.type) < type_rank(b.type);
  });
  out.stats = Json{{"mode", exhaustive ? "exhaustive" : "candidates"},
                   {"units", n},
                   {"links", static_cast<std::int64_t>(out.links.size())},
                   {"project_groups_aggregated", static_cast<std::int64_t>(out.project_groups.size())},
                   {"minhash_pairs_verified", verified_pairs},
                   {"lsh_candidates", lsh_candidates},
                   {"rare_term_candidates", rare_candidates}};
  return out;
}

}  // namespace loom::catalog::internal
