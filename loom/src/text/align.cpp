// Generalised Myers and the version alignment (R102 G1). See loom/text_fragments.h.
#include <algorithm>
#include <map>
#include <unordered_map>

#include "loom/text_fragments.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::text {

namespace {

EditOp op(std::string kind, std::size_t a, std::size_t b) {
  EditOp o;
  o.kind = std::move(kind);
  o.a = a;
  o.b = b;
  return o;
}

}  // namespace

// Myers O((N+M)D) with the trace kept per step (slice [-d, d] of V), then a backtrack. Common prefix and suffix are
// taken first, as lineage.cpp did for lines.
std::vector<EditOp> myers_script(const std::vector<int>& a, const std::vector<int>& b) {
  std::vector<EditOp> out;
  std::size_t p = 0;
  while (p < a.size() && p < b.size() && a[p] == b[p]) ++p;
  std::size_t s = 0;
  while (s < a.size() - p && s < b.size() - p && a[a.size() - 1 - s] == b[b.size() - 1 - s]) ++s;
  for (std::size_t i = 0; i < p; ++i) out.push_back(op("equal", i, i));
  const long n = static_cast<long>(a.size() - p - s), m = static_cast<long>(b.size() - p - s);
  const long max = n + m;
  std::vector<EditOp> mid;
  if (max > 0) {
    auto A = [&](long i) { return a[p + static_cast<std::size_t>(i)]; };
    auto B = [&](long j) { return b[p + static_cast<std::size_t>(j)]; };
    const long off = max + 1;
    std::vector<long> v(static_cast<std::size_t>(2 * max + 3), 0);
    std::vector<std::vector<long>> trace;  // trace[d][k + d] = V[k] before step d
    long found = -1;
    for (long d = 0; d <= max && found < 0; ++d) {
      trace.emplace_back(v.begin() + (off - d), v.begin() + (off + d + 1));
      for (long k = -d; k <= d; k += 2) {
        const auto ki = static_cast<std::size_t>(k + off);
        long x = (k == -d || (k != d && v[ki - 1] < v[ki + 1])) ? v[ki + 1] : v[ki - 1] + 1;
        long y = x - k;
        while (x < n && y < m && A(x) == B(y)) {
          ++x;
          ++y;
        }
        v[ki] = x;
        if (x >= n && y >= m) {
          found = d;
          break;
        }
      }
    }
    long x = n, y = m;
    for (long d = found; d >= 0; --d) {
      const std::vector<long>& t = trace[static_cast<std::size_t>(d)];
      auto V = [&](long k) { return t[static_cast<std::size_t>(k + d)]; };
      const long k = x - y;
      long pk, px;
      if (d == 0) {
        pk = 0;
        px = 0;
      } else {
        pk = (k == -d || (k != d && V(k - 1) < V(k + 1))) ? k + 1 : k - 1;
        px = V(pk);
      }
      const long py = px - pk;
      // the snake after the edit of step d
      const long sx = d == 0 ? 0 : (pk == k + 1 ? px : px + 1);
      while (x > sx) {
        --x;
        --y;
        mid.push_back(op("equal", p + static_cast<std::size_t>(x), p + static_cast<std::size_t>(y)));
      }
      if (d > 0) {
        if (pk == k + 1) mid.push_back(op("insert", kNone, p + static_cast<std::size_t>(py)));
        else mid.push_back(op("delete", p + static_cast<std::size_t>(px), kNone));
      }
      x = px;
      y = py;
    }
    std::reverse(mid.begin(), mid.end());
  }
  out.insert(out.end(), mid.begin(), mid.end());
  for (std::size_t i = 0; i < s; ++i) out.push_back(op("equal", a.size() - s + i, b.size() - s + i));
  return out;
}

// Distance only: the same forward pass of Myers, one V of 2(N+M)+3 longs and no trace (O(N+M) memory; the script above keeps
// the trace for its backtrack, ~D^2 longs). The shortest edit script has D = inserts + deletes steps, which is what the loop finds.
std::size_t myers_distance(const std::vector<int>& a, const std::vector<int>& b) {
  std::size_t p = 0;
  while (p < a.size() && p < b.size() && a[p] == b[p]) ++p;
  std::size_t s = 0;
  while (s < a.size() - p && s < b.size() - p && a[a.size() - 1 - s] == b[b.size() - 1 - s]) ++s;
  const long n = static_cast<long>(a.size() - p - s), m = static_cast<long>(b.size() - p - s);
  const long max = n + m;
  if (max == 0) return 0;
  auto A = [&](long i) { return a[p + static_cast<std::size_t>(i)]; };
  auto B = [&](long j) { return b[p + static_cast<std::size_t>(j)]; };
  const long off = max + 1;
  std::vector<long> v(static_cast<std::size_t>(2 * max + 3), 0);
  for (long d = 0; d <= max; ++d) {
    for (long k = -d; k <= d; k += 2) {
      const auto ki = static_cast<std::size_t>(k + off);
      long x = (k == -d || (k != d && v[ki - 1] < v[ki + 1])) ? v[ki + 1] : v[ki - 1] + 1;
      long y = x - k;
      while (x < n && y < m && A(x) == B(y)) {
        ++x;
        ++y;
      }
      v[ki] = x;
      if (x >= n && y >= m) return static_cast<std::size_t>(d);
    }
  }
  return static_cast<std::size_t>(max);
}

namespace {

struct Interner {
  std::unordered_map<std::string, int> ids;
  int id(const std::string& s) { return ids.emplace(s, static_cast<int>(ids.size())).first->second; }
};

std::vector<int> word_keys(const Segmenter& seg, std::string_view x, const std::string& level, Interner& in,
                           Status& st) {
  std::vector<int> out;
  auto r = seg.segment(x, level);
  if (!r) {
    st = r.error();
    return out;
  }
  for (const auto& f : r->fragments) {
    std::string k = seg.normalize(x.substr(f.begin, f.end - f.begin));
    if (!k.empty()) out.push_back(in.id(k));
  }
  return out;
}

double dice_bigrams(const std::u32string& x, const std::u32string& y) {
  if (x.size() < 2 || y.size() < 2) return x == y ? 1.0 : 0.0;
  std::map<std::u32string, long> bx;
  for (std::size_t i = 0; i + 1 < x.size(); ++i) ++bx[x.substr(i, 2)];
  long common = 0;
  for (std::size_t i = 0; i + 1 < y.size(); ++i) {
    auto it = bx.find(y.substr(i, 2));
    if (it != bx.end() && it->second > 0) {
      --it->second;
      ++common;
    }
  }
  return 2.0 * static_cast<double>(common) / static_cast<double>(x.size() + y.size() - 2);
}

double lcs_ratio(const std::vector<int>& x, const std::vector<int>& y) {
  if (x.empty() && y.empty()) return 1.0;
  const std::size_t d = myers_distance(x, y);
  const double lcs = static_cast<double>(x.size() + y.size() - d) / 2.0;
  return 2.0 * lcs / static_cast<double>(x.size() + y.size());
}

const std::vector<std::string>& known_similarity() {
  static const std::vector<std::string> k{"words", "chars"};
  return k;
}

Status check_method(std::string_view m) {
  for (const auto& k : known_similarity())
    if (k == m) return {};
  return Error(Errc::Unsupported, "text alignment: similarity method \"" + std::string(m) +
                                      "\" is not available in this build (known: words, chars; vector and model "
                                      "variants come in a later package)");
}

}  // namespace

Result<double> similarity(const Segmenter& seg, std::string_view method, std::string_view x, std::string_view y) {
  if (auto st = check_method(method); !st) return st.error();
  if (method == "chars") return dice_bigrams(utf8::decode(seg.normalize(x)), utf8::decode(seg.normalize(y)));
  Interner in;
  Status st;
  const std::string level = seg.pack().value("alignment", Json::object()).value("similarity_level", std::string("word"));
  const auto kx = word_keys(seg, x, level, in, st);
  const auto ky = word_keys(seg, y, level, in, st);
  if (!st) return st.error();
  return lcs_ratio(kx, ky);
}

Result<AlignOptions> AlignOptions::from_pack(const Json& pack) {
  AlignOptions o;
  const Json al = pack.is_object() ? pack.value("alignment", Json::object()) : Json::object();
  if (!al.is_object()) return Error(Errc::InvalidArgument, "text pack: alignment must be an object");
  try {
    o.similarity = al.value("similarity", o.similarity);
    o.replace_threshold = al.value("replace_threshold", o.replace_threshold);
    o.move_threshold = al.value("move_threshold", o.move_threshold);
    o.inner_level = al.value("inner_level", o.inner_level);
  } catch (const std::exception& e) {
    return Error(Errc::InvalidArgument, std::string("text pack: alignment: ") + e.what());
  }
  return o;
}

std::size_t Alignment::count(std::string_view kind) const {
  return static_cast<std::size_t>(std::count_if(ops.begin(), ops.end(), [&](const EditOp& o) { return o.kind == kind; }));
}

Json EditOp::to_json() const {
  Json j{{"kind", kind}};
  j["a"] = a == kNone ? Json(nullptr) : Json(a);
  j["b"] = b == kNone ? Json(nullptr) : Json(b);
  if (similarity != 1.0) j["similarity"] = similarity;
  if (text) j["text"] = *text;
  if (!inner.empty()) {
    Json in = Json::array();
    for (const auto& o : inner) in.push_back(o.to_json());
    j["inner"] = in;
  }
  return j;
}

Json Alignment::to_json() const {
  Json o = Json::array();
  for (const auto& x : ops) o.push_back(x.to_json());
  return Json{{"ops", o}};
}

Result<Alignment> align_units(const Segmenter& seg, const std::vector<std::string>& a, const std::vector<std::string>& b,
                              const AlignOptions& opt) {
  const bool pair_replace = opt.replace_threshold <= 1.0;
  const bool pair_move = opt.move_threshold <= 1.0;
  if (pair_replace || (pair_move && opt.move_threshold < 1.0))
    if (auto st = check_method(opt.similarity); !st) return st.error();
  Interner in;
  std::vector<std::string> na, nb;
  std::vector<int> ka, kb;
  for (const auto& x : a) {
    na.push_back(seg.normalize(x));
    ka.push_back(in.id(na.back()));
  }
  for (const auto& x : b) {
    nb.push_back(seg.normalize(x));
    kb.push_back(in.id(nb.back()));
  }
  const std::vector<EditOp> base = myers_script(ka, kb);

  // pairing: pair_of_b[j] = {i, similarity, kind}
  struct Pair {
    std::size_t a = kNone;
    double sim = 0;
    std::string kind;
  };
  std::vector<Pair> pair_of_b(b.size());
  std::vector<bool> a_paired(a.size(), false);
  std::map<std::pair<std::size_t, std::size_t>, double> sim_cache;
  Status err;
  auto sim = [&](std::size_t i, std::size_t j) -> double {
    auto key = std::make_pair(i, j);
    auto it = sim_cache.find(key);
    if (it != sim_cache.end()) return it->second;
    double v = 0;
    if (ka[i] == kb[j]) {
      v = 1.0;
    } else {
      auto r = similarity(seg, opt.similarity, a[i], b[j]);
      if (!r) err = r.error();
      else v = *r;
    }
    sim_cache.emplace(key, v);
    return v;
  };

  if (pair_move) {  // exact moves first: an identical fingerprint is the strongest evidence
    std::vector<std::size_t> D, I;
    for (const auto& o : base) {
      if (o.kind == "delete" && !a_paired[o.a]) D.push_back(o.a);
      if (o.kind == "insert" && pair_of_b[o.b].a == kNone) I.push_back(o.b);
    }
    // exact: same fingerprint, first free delete in a order
    std::unordered_map<int, std::vector<std::size_t>> free_by_key;
    for (auto it = D.rbegin(); it != D.rend(); ++it) free_by_key[ka[*it]].push_back(*it);
    for (std::size_t j : I) {
      auto f = free_by_key.find(kb[j]);
      if (f != free_by_key.end() && !f->second.empty()) {
        const std::size_t i = f->second.back();
        f->second.pop_back();
        pair_of_b[j] = Pair{i, 1.0, "move"};
        a_paired[i] = true;
      }
    }
  }

  if (pair_replace) {
    // each gap between equal anchors: a non-crossing pairing of its deletes and inserts maximising the summed
    // similarity among pairs at or above the threshold (DP over |D|·|I|)
    std::size_t g = 0;
    while (g < base.size()) {
      if (base[g].kind == "equal") {
        ++g;
        continue;
      }
      std::vector<std::size_t> D, I;
      while (g < base.size() && base[g].kind != "equal") {
        if (base[g].kind == "delete") {
          if (!a_paired[base[g].a]) D.push_back(base[g].a);
        } else if (pair_of_b[base[g].b].a == kNone) {
          I.push_back(base[g].b);
        }
        ++g;
      }
      if (D.empty() || I.empty()) continue;
      const std::size_t nd = D.size(), ni = I.size();
      std::vector<double> best((nd + 1) * (ni + 1), 0.0);
      auto at = [&](std::size_t x, std::size_t y) -> double& { return best[x * (ni + 1) + y]; };
      for (std::size_t x = nd; x-- > 0;) {
        for (std::size_t y = ni; y-- > 0;) {
          double v = std::max(at(x + 1, y), at(x, y + 1));
          const double s = sim(D[x], I[y]);
          if (s >= opt.replace_threshold) v = std::max(v, s + at(x + 1, y + 1));
          at(x, y) = v;
        }
      }
      if (!err) return err.error();
      std::size_t x = 0, y = 0;
      while (x < nd && y < ni) {
        const double s = sim(D[x], I[y]);
        if (s >= opt.replace_threshold && at(x, y) == s + at(x + 1, y + 1)) {
          pair_of_b[I[y]] = Pair{D[x], s, "replace"};
          a_paired[D[x]] = true;
          ++x;
          ++y;
        } else if (at(x, y) == at(x + 1, y)) {
          ++x;
        } else {
          ++y;
        }
      }
    }
  }

  if (pair_move && opt.move_threshold < 1.0) {
    std::vector<std::size_t> D, I_left;
    for (const auto& o : base) {
      if (o.kind == "delete" && !a_paired[o.a]) D.push_back(o.a);
      if (o.kind == "insert" && pair_of_b[o.b].a == kNone) I_left.push_back(o.b);
    }
    {
      struct Cand {
        double s;
        std::size_t i, j;
      };
      std::vector<Cand> c;
      for (std::size_t i : D) {
        if (a_paired[i]) continue;
        for (std::size_t j : I_left) {
          const double s = sim(i, j);
          if (s >= opt.move_threshold) c.push_back({s, i, j});
        }
      }
      if (!err) return err.error();
      std::sort(c.begin(), c.end(), [](const Cand& x, const Cand& y) {
        if (x.s != y.s) return x.s > y.s;
        if (x.i != y.i) return x.i < y.i;
        return x.j < y.j;
      });
      for (const auto& x : c) {
        if (a_paired[x.i] || pair_of_b[x.j].a != kNone) continue;
        pair_of_b[x.j] = Pair{x.i, x.s, "move"};
        a_paired[x.i] = true;
      }
    }
  }

  AlignOptions inner_opt = opt;
  inner_opt.replace_threshold = 2.0;
  inner_opt.move_threshold = 2.0;
  inner_opt.inner_level.clear();
  Alignment out;
  for (const auto& o : base) {
    if (o.kind == "equal") {
      EditOp e = o;
      if (a[o.a] != b[o.b]) e.text = b[o.b];
      out.ops.push_back(std::move(e));
    } else if (o.kind == "delete") {
      if (!a_paired[o.a]) out.ops.push_back(o);
    } else {
      const Pair& pr = pair_of_b[o.b];
      if (pr.a == kNone) {
        EditOp e = o;
        e.text = b[o.b];
        out.ops.push_back(std::move(e));
        continue;
      }
      EditOp e = op(pr.kind, pr.a, o.b);
      e.similarity = pr.sim;
      if (a[pr.a] != b[o.b]) {
        e.text = b[o.b];
        if (!opt.inner_level.empty()) {
          auto sa = seg.segment(a[pr.a], opt.inner_level);
          if (!sa) return sa.error();
          auto sb = seg.segment(b[o.b], opt.inner_level);
          if (!sb) return sb.error();
          auto inner = align_units(seg, fragment_texts(a[pr.a], *sa), fragment_texts(b[o.b], *sb), inner_opt);
          if (!inner) return inner.error();
          e.inner = std::move(inner->ops);
        }
      }
      out.ops.push_back(std::move(e));
    }
  }
  return out;
}

Result<Alignment> align_texts(const Segmenter& seg, std::string_view a, std::string_view b, std::string_view level,
                              const AlignOptions& opt) {
  auto sa = seg.segment(a, level);
  if (!sa) return sa.error();
  auto sb = seg.segment(b, level);
  if (!sb) return sb.error();
  return align_units(seg, fragment_texts(a, *sa), fragment_texts(b, *sb), opt);
}

Result<std::vector<std::string>> apply(const Alignment& al, const std::vector<std::string>& a) {
  std::size_t nb = 0;
  for (const auto& o : al.ops)
    if (o.b != kNone) nb = std::max(nb, o.b + 1);
  std::vector<std::string> out(nb);
  std::vector<bool> seen_b(nb, false), seen_a(a.size(), false);
  for (const auto& o : al.ops) {
    if (o.a != kNone) {
      if (o.a >= a.size()) return Error(Errc::InvalidArgument, "apply: a unit index past the end of a");
      if (seen_a[o.a]) return Error(Errc::InvalidArgument, "apply: an a unit is covered twice");
      seen_a[o.a] = true;
    }
    if (o.b == kNone) continue;
    if (seen_b[o.b]) return Error(Errc::InvalidArgument, "apply: a b unit is produced twice");
    seen_b[o.b] = true;
    if (o.text) out[o.b] = *o.text;
    else if (o.a != kNone) out[o.b] = a[o.a];
    else return Error(Errc::InvalidArgument, "apply: an op without a text and without an a-side");
  }
  if (std::find(seen_b.begin(), seen_b.end(), false) != seen_b.end())
    return Error(Errc::InvalidArgument, "apply: the script leaves a b unit unproduced");
  if (std::find(seen_a.begin(), seen_a.end(), false) != seen_a.end())
    return Error(Errc::InvalidArgument, "apply: the script leaves an a unit unaccounted for");
  return out;
}

}  // namespace loom::text
