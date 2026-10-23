// Resolving an address against a port graph (K3-1). See include/loom/address.h.
// Every step is a flat-map over the nodes of the previous one; what points outside the graph gives an empty set and an advisory.
#include <algorithm>
#include <limits>
#include <cmath>
#include <optional>

#include "loom/address.h"

namespace loom::address {

namespace {

using dimensions::Dimension;
using dimensions::Registry;

std::string kind_of(const Json& c) { return c.is_object() && c.size() == 1 ? c.begin().key() : std::string(); }
const Json& payload(const Json& c) { return c.begin().value(); }

struct Run {
  const Graph& g;
  const Registry& reg;
  advisory::Collector& adv;
  const Syntax& text_syntax;

  std::string coord_text(const Json& coord) const {
    Address a;
    a.steps.push_back(Step{"", coord});
    return print(a, text_syntax);
  }
  std::vector<NodeId> seq(const std::string& dim, const NodeId& n) const { return g.neighbours ? g.neighbours(dim, n) : std::vector<NodeId>{}; }
  Json key(const std::string& dim, const NodeId& n) const { return g.key ? g.key(dim, n) : Json(nullptr); }
  void outside(const Json& coord, const std::string& detail) { adv.add("address_outside", Json{{"address", coord_text(coord)}, {"detail", detail}}); }
  void mismatch(const Dimension& d, const std::string& kind) { adv.add("coord_kind_mismatch", Json{{"dimension", d.id}, {"kind", kind}, {"codec", d.codec}}); }
};

long position_of(const std::vector<NodeId>& v, const NodeId& n) {
  const auto it = std::find(v.begin(), v.end(), n);
  return it == v.end() ? -1 : static_cast<long>(it - v.begin());
}

// The index that 0 means in `seq`: the dimension's origin (current = the position of the anchor).
std::optional<long> origin_index(Run& r, const Dimension& d, long anchor, std::size_t n) {
  if (d.origin == "first") return 0;
  if (d.origin == "last") return static_cast<long>(n) - 1;
  if (d.origin != "current") r.adv.add("origin_unknown", Json{{"dimension", d.id}, {"origin", d.origin}});
  if (anchor < 0) return std::nullopt;
  return anchor;
}

// nearest by a numeric key; ties go to the earlier node. Exact hits need no between mode.
std::optional<long> nearest_by_key(Run& r, const Dimension& d, const std::vector<NodeId>& seq, double target, bool* exact) {
  std::optional<long> best;
  double best_dist = 0;
  for (std::size_t i = 0; i < seq.size(); ++i) {
    const auto k = r.reg.numeric(d, r.key(d.id, seq[i]));
    if (!k) continue;
    const double dist = std::fabs(*k - target);
    if (!best || dist < best_dist) {
      best = static_cast<long>(i);
      best_dist = dist;
    }
  }
  if (!best) r.adv.add("key_missing", Json{{"dimension", d.id}});
  *exact = best && best_dist == 0;
  return best;
}

void between_check(Run& r, const Dimension& d) {
  const std::string mode = d.between_mode();
  if (mode != "nearest" && !r.reg.between_available(mode)) r.adv.add("between_unavailable", Json{{"dimension", d.id}, {"mode", mode}});
}

// Coordinates are open values (R83): any int64 or any double is accepted. Arithmetic on them saturates instead of
// overflowing (UBSan in CI 283, 2026-10-23: INT64_MAX + offset); a saturated index lies outside the sequence and
// yields the same `address_outside` advisory as any other far position. NaN points nowhere (outside too).
long sat_add(long a, long b) {
  long out = 0;
  if (__builtin_add_overflow(a, b, &out)) return b > 0 ? std::numeric_limits<long>::max() : std::numeric_limits<long>::min();
  return out;
}
long long_of(std::int64_t v) {
  if (v > static_cast<std::int64_t>(std::numeric_limits<long>::max())) return std::numeric_limits<long>::max();
  if (v < static_cast<std::int64_t>(std::numeric_limits<long>::min())) return std::numeric_limits<long>::min();
  return static_cast<long>(v);
}
long long_of(double x) {
  if (std::isnan(x)) return std::numeric_limits<long>::max();
  const double hi = static_cast<double>(std::numeric_limits<long>::max());  // 2^63 as a double; anything at or past it saturates
  if (x >= hi) return std::numeric_limits<long>::max();
  if (x <= -hi) return std::numeric_limits<long>::min();
  return static_cast<long>(x);
}

// The index a coordinate means in `seq` (not bounds-checked), or nullopt with an advisory.
std::optional<long> index_of(Run& r, const Dimension& d, const Json& coord, const std::vector<NodeId>& seq, long anchor) {
  const std::string kind = kind_of(coord);
  const Json& v = payload(coord);
  const long n = static_cast<long>(seq.size());
  auto offset_from_origin = [&](long off) -> std::optional<long> {
    const auto o = origin_index(r, d, anchor, seq.size());
    if (!o) {
      r.outside(coord, "the current node is not in this sequence");
      return std::nullopt;
    }
    return sat_add(*o, off);
  };
  if (kind == "int") return offset_from_origin(long_of(v.get<std::int64_t>()));
  if (kind == "double") {
    const double x = v.get<double>();
    if (d.effective_codec == "double") {
      bool exact = false;
      const auto at = nearest_by_key(r, d, seq, x, &exact);
      if (at && !exact) between_check(r, d);
      return at;
    }
    if (d.effective_codec == "integer" || d.effective_codec == "levels") {
      if (x == std::floor(x)) return offset_from_origin(long_of(x));
      between_check(r, d);
      const auto o = origin_index(r, d, anchor, seq.size());
      if (!o) {
        r.outside(coord, "the current node is not in this sequence");
        return std::nullopt;
      }
      return long_of(std::ceil(static_cast<double>(*o) + x - 0.5));
    }
    r.mismatch(d, kind);
    return std::nullopt;
  }
  if (kind == "datetime") {
    if (d.effective_codec != "datetime") {
      r.mismatch(d, kind);
      return std::nullopt;
    }
    const auto t = dimensions::datetime_epoch(v.get<std::string>());
    if (!t) {
      r.mismatch(d, kind);
      return std::nullopt;
    }
    bool exact = false;
    const auto at = nearest_by_key(r, d, seq, *t, &exact);
    if (at && !exact) between_check(r, d);
    return at;
  }
  if (kind == "word") {
    const std::string w = v.get<std::string>();
    const Json* op = r.reg.word(d, w);
    if (!op || !op->is_object()) {
      r.adv.add("word_unknown", Json{{"dimension", d.id}, {"word", w}});
      return std::nullopt;
    }
    if (const Json* e = json::find(*op, "end")) return e->get<std::string>() == "first" ? 0L : n - 1;
    if (const Json* off = json::find(*op, "offset"); off && off->is_number_integer()) return offset_from_origin(off->get<std::int64_t>());
    if (const Json* ex = json::find(*op, "extremum"); ex && ex->is_string()) {
      const bool want_max = ex->get<std::string>() != "min";
      std::optional<long> best;
      Json best_key;
      for (long i = 0; i < n; ++i) {
        const Json k = r.key(d.id, seq[static_cast<std::size_t>(i)]);
        if (k.is_null()) continue;
        if (!best) {
          best = i;
          best_key = k;
          continue;
        }
        const auto c = r.reg.compare(d, k, best_key);
        if (c && (want_max ? *c > 0 : *c < 0)) {
          best = i;
          best_key = k;
        }
      }
      if (best) return best;
      r.adv.add("key_missing", Json{{"dimension", d.id}});
      return want_max ? n - 1 : 0L;
    }
    if (json::find(*op, "all")) {
      r.mismatch(d, "word:all");
      return std::nullopt;
    }
    r.adv.add("word_unknown", Json{{"dimension", d.id}, {"word", w}});
    return std::nullopt;
  }
  if (kind == "name" || (!kind.empty() && kind != "range" && kind != "levels" && kind != "pred")) {
    if (d.effective_codec != "name" && kind == "name") {
      r.mismatch(d, kind);
      return std::nullopt;
    }
    for (long i = 0; i < n; ++i) {
      const Json k = r.key(d.id, seq[static_cast<std::size_t>(i)]);
      if (k.is_string() && k.get<std::string>() == v.get<std::string>()) return i;
    }
    r.outside(coord, "no node of that name");
    return std::nullopt;
  }
  r.mismatch(d, kind);
  return std::nullopt;
}

std::vector<NodeId> select(Run& r, const Dimension& d, const Json& coord, const std::vector<NodeId>& seq, long anchor) {
  const std::string kind = kind_of(coord);
  if (kind == "pred") {
    r.adv.add("predicate_not_evaluated", Json{{"dimension", d.id}, {"pred", payload(coord).get<std::string>()}});
    return {};
  }
  if (seq.empty()) {
    r.outside(coord, "nothing lies on this dimension at that level");
    return {};
  }
  const long n = static_cast<long>(seq.size());
  if (kind == "word") {
    const Json* op = r.reg.word(d, payload(coord).get<std::string>());
    if (op && op->is_object() && json::find(*op, "all")) return seq;
  }
  if (kind == "range") {
    const auto lo = index_of(r, d, payload(coord)[0], seq, anchor);
    const auto hi = index_of(r, d, payload(coord)[1], seq, anchor);
    if (!lo || !hi) return {};
    if ((*lo < 0 && *hi < 0) || (*lo >= n && *hi >= n)) {
      r.outside(coord, "the whole range lies outside 0.." + std::to_string(n - 1));
      return {};
    }
    const long a = std::clamp(*lo, 0L, n - 1), b = std::clamp(*hi, 0L, n - 1);
    if (a != *lo || b != *hi) r.outside(coord, "the range was cut to the existing nodes 0.." + std::to_string(n - 1));
    std::vector<NodeId> out;
    for (long i = a;; i += (a <= b ? 1 : -1)) {
      out.push_back(seq[static_cast<std::size_t>(i)]);
      if (i == b) break;
    }
    return out;
  }
  const auto idx = index_of(r, d, coord, seq, anchor);
  if (!idx) return {};
  if (*idx < 0 || *idx >= n) {
    r.outside(coord, "position " + std::to_string(*idx) + " is outside 0.." + std::to_string(n - 1));
    return {};
  }
  return {seq[static_cast<std::size_t>(*idx)]};
}

void add_unique(std::vector<NodeId>& into, const std::vector<NodeId>& add) {
  for (const auto& n : add)
    if (std::find(into.begin(), into.end(), n) == into.end()) into.push_back(n);
}

std::vector<NodeId> resolve_step(Run& r, const Dimension& d, const Json& coord, const NodeId& anchor) {
  const std::string kind = kind_of(coord);
  std::vector<Json> parts;
  if (kind == "levels") {
    if (d.effective_codec != "levels") {
      r.mismatch(d, kind);
      return {};
    }
    for (const auto& p : payload(coord)) parts.push_back(p);
  } else if (d.effective_codec == "levels") {
    parts.push_back(coord);
  } else {
    const auto s = r.seq(d.id, anchor);
    if (s.empty() && r.g.neighbours) r.adv.add("anchor_off_dimension", Json{{"dimension", d.id}, {"node", anchor}});
    long a = position_of(s, anchor);
    if (a < 0 && r.g.lineage) {  // the anchor may sit on this dimension under another node
      const auto chain = r.g.lineage(d.id, anchor);
      if (!chain.empty()) a = position_of(s, chain.back());
    }
    return select(r, d, coord, s, a);
  }
  // Hierarchy: each level picks inside the nodes chosen by the one before it.
  const std::vector<NodeId> chain = r.g.lineage ? r.g.lineage(d.id, anchor) : std::vector<NodeId>{};
  if (chain.empty()) {
    r.adv.add("anchor_off_dimension", Json{{"dimension", d.id}, {"node", anchor}});
    return {};
  }
  if (!d.levels.empty() && parts.size() > d.levels.size())
    r.adv.add("levels_deeper_than_dimension", Json{{"dimension", d.id}, {"depth", parts.size()}, {"declared", d.levels.size()}});
  const auto top = r.seq(d.id, chain[0]);
  std::vector<NodeId> current = select(r, d, parts[0], top, position_of(top, chain[0]));
  for (std::size_t k = 1; k < parts.size(); ++k) {
    std::vector<NodeId> next;
    for (const auto& node : current) {
      const auto kids = r.g.children ? r.g.children(d.id, node) : std::vector<NodeId>{};
      long a = -1;
      if (k < chain.size() && chain[k - 1] == node) a = position_of(kids, chain[k]);
      else if (!kids.empty()) a = d.off_lineage_origin == "first" ? 0 : static_cast<long>(kids.size()) - 1;
      add_unique(next, select(r, d, parts[k], kids, a));
    }
    current = std::move(next);
  }
  return current;
}

}  // namespace

Result<Resolution> resolve(const Address& address, const NodeId& anchor, const Graph& graph, const dimensions::Registry& registry) {
  advisory::Collector adv(&registry.catalogue());
  Resolution out;
  const Syntax& text_syntax = Syntaxes::builtin().default_syntax();
  Run run{graph, registry, adv, text_syntax};
  std::vector<NodeId> current{anchor};
  if (address.steps.empty()) adv.add("address_empty");
  std::size_t index = 0;
  for (const auto& step : address.steps) {
    std::vector<NodeId> next;
    std::string shown = step.dim;
    for (const auto& node : current) {
      std::string dim = step.dim;
      if (dim.empty()) dim = graph.default_dimension ? graph.default_dimension(node) : std::string();
      if (dim.empty()) dim = registry.default_dimension();
      shown = dim;
      const Dimension* d = registry.find(dim);
      if (!d) {
        adv.add("dimension_unknown", Json{{"dimension", dim}});
        continue;
      }
      if (!d->codec_known) adv.add("codec_unknown", Json{{"dimension", d->id}, {"codec", d->codec}});
      add_unique(next, resolve_step(run, *d, step.coord, node));
    }
    out.trace.push_back(Json{{"step", index++}, {"dim", shown}, {"coord", step.coord}, {"in", current.size()}, {"out", next.size()}});
    current = std::move(next);
  }
  out.nodes = std::move(current);
  out.advisories = adv.items();
  return out;
}

}  // namespace loom::address
