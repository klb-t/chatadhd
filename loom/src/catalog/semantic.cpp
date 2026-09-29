// catalog_internal.h: SemanticSpace — offline vector-space evidence for the
// catalog scorer (R25/R27: vector similarity is a first-class retrieval
// channel next to the lexical one; neither gates the other).
//
// Two spaces over the sketch's stemmed terms (see catalog_internal.h):
//   word  : (1 + ln tf) * idf
//   gram  : character n-grams of "_term_" (1 + ln tf_gram) * idf_gram
// idf = ln((1 + N) / (1 + df)) + 1, df counted over units (top-K terms).
// A unit vector is rebuilt from its sketch whenever it is needed; nothing
// per-unit is retained, so memory is bounded by the vocabularies.
#include <algorithm>
#include <cmath>

#include "catalog_internal.h"
#include "loom/util/utf8.h"

namespace loom::catalog::internal {

namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

double sublinear(double tf) { return tf > 0 ? 1.0 + std::log(tf) : 0.0; }

void normalise(std::vector<std::pair<std::uint32_t, double>>& v) {
  double n = 0.0;
  for (const auto& [id, w] : v) n += w * w;
  if (n <= 0.0) return;
  n = std::sqrt(n);
  for (auto& e : v) e.second /= n;
}

}  // namespace

double percentile_of(std::vector<double> values, double pct) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  double q = std::clamp(pct / 100.0, 0.0, 1.0) * static_cast<double>(values.size() - 1);
  return values[static_cast<std::size_t>(std::floor(q + 0.5))];
}

void SemanticSpace::grams_of(std::string_view term, std::vector<std::uint64_t>& out) const {
  out.clear();
  std::u32string cps = utf8::decode(term);
  cps.insert(cps.begin(), U'_');
  cps.push_back(U'_');
  const std::size_t n = static_cast<std::size_t>(std::max(2, ngram_));
  if (cps.size() <= n) {
    std::uint64_t h = kFnvOffset;
    for (char32_t c : cps) h = (h ^ static_cast<std::uint64_t>(c)) * kFnvPrime;
    out.push_back(h);
    return;
  }
  for (std::size_t i = 0; i + n <= cps.size(); ++i) {
    std::uint64_t h = kFnvOffset;
    for (std::size_t k = 0; k < n; ++k) h = (h ^ static_cast<std::uint64_t>(cps[i + k])) * kFnvPrime;
    out.push_back(h);
  }
}

SemanticSpace::SemanticSpace(const std::vector<const Sketch*>& sketches, int ngram)
    : sketches_(sketches), ngram_(std::max(2, ngram)) {
  std::vector<std::uint32_t> word_df, gram_df;
  std::vector<std::uint64_t> grams;
  std::vector<std::uint32_t> seen_grams;
  for (const Sketch* sk : sketches_) {
    seen_grams.clear();
    if (!sk) continue;
    for (const auto& [term, tf] : sk->top_terms) {
      auto [it, fresh] = word_ids_.emplace(term, static_cast<std::uint32_t>(word_df.size()));
      if (fresh) word_df.push_back(0);
      ++word_df[it->second];  // top_terms holds each term once
      grams_of(term, grams);
      for (std::uint64_t g : grams) {
        auto [git, gfresh] = gram_ids_.emplace(g, static_cast<std::uint32_t>(gram_df.size()));
        if (gfresh) gram_df.push_back(0);
        seen_grams.push_back(git->second);
      }
    }
    std::sort(seen_grams.begin(), seen_grams.end());
    seen_grams.erase(std::unique(seen_grams.begin(), seen_grams.end()), seen_grams.end());
    for (std::uint32_t id : seen_grams) ++gram_df[id];
  }
  const double n = static_cast<double>(sketches_.size());
  word_idf_.resize(word_df.size());
  for (std::size_t i = 0; i < word_df.size(); ++i) word_idf_[i] = std::log((1.0 + n) / (1.0 + word_df[i])) + 1.0;
  gram_idf_.resize(gram_df.size());
  for (std::size_t i = 0; i < gram_df.size(); ++i) gram_idf_[i] = std::log((1.0 + n) / (1.0 + gram_df[i])) + 1.0;
}

SemanticProfile SemanticSpace::new_profile() const {
  SemanticProfile p;
  p.word.assign(word_idf_.size(), 0.0);
  p.gram.assign(gram_idf_.size(), 0.0);
  return p;
}

SemanticSpace::UnitVec SemanticSpace::unit_vec(std::size_t unit) const {
  UnitVec v;
  const Sketch* sk = unit < sketches_.size() ? sketches_[unit] : nullptr;
  if (!sk) return v;
  std::vector<std::uint64_t> grams;
  std::vector<std::pair<std::uint32_t, double>> gram_tf;
  for (const auto& [term, tf] : sk->top_terms) {
    auto wit = word_ids_.find(term);
    if (wit != word_ids_.end()) v.word.emplace_back(wit->second, sublinear(tf) * word_idf_[wit->second]);
    grams_of(term, grams);
    for (std::uint64_t g : grams) {
      auto git = gram_ids_.find(g);
      if (git != gram_ids_.end()) gram_tf.emplace_back(git->second, static_cast<double>(tf));
    }
  }
  std::sort(gram_tf.begin(), gram_tf.end(),
            [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second < b.second; });
  for (std::size_t i = 0; i < gram_tf.size();) {
    std::size_t j = i;
    double tf = 0.0;
    while (j < gram_tf.size() && gram_tf[j].first == gram_tf[i].first) tf += gram_tf[j++].second;
    v.gram.emplace_back(gram_tf[i].first, sublinear(tf) * gram_idf_[gram_tf[i].first]);
    i = j;
  }
  normalise(v.word);
  normalise(v.gram);
  return v;
}

void SemanticSpace::add_unit(SemanticProfile& p, std::size_t unit, double weight) const {
  if (weight <= 0.0) return;
  UnitVec v = unit_vec(unit);
  for (const auto& [id, w] : v.word) p.word[id] += weight * w;
  for (const auto& [id, w] : v.gram) p.gram[id] += weight * w;
  p.mass += weight;
  ++p.units;
}

void SemanticSpace::add_terms(SemanticProfile& p, const std::vector<std::pair<std::string, double>>& terms,
                              double weight) const {
  if (weight <= 0.0 || terms.empty()) return;
  std::vector<std::pair<std::uint32_t, double>> word;
  std::vector<std::pair<std::uint32_t, double>> gram_raw;
  std::vector<std::uint64_t> grams;
  for (const auto& [key, w] : terms) {
    if (key.empty() || w <= 0.0) continue;
    auto wit = word_ids_.find(key);
    if (wit != word_ids_.end()) word.emplace_back(wit->second, w * word_idf_[wit->second]);
    grams_of(key, grams);
    for (std::uint64_t g : grams) {
      auto git = gram_ids_.find(g);
      if (git != gram_ids_.end()) gram_raw.emplace_back(git->second, w * gram_idf_[git->second]);
    }
  }
  auto merge = [](std::vector<std::pair<std::uint32_t, double>>& v) {
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second < b.second; });
    std::vector<std::pair<std::uint32_t, double>> out;
    for (const auto& e : v) {
      if (!out.empty() && out.back().first == e.first) out.back().second += e.second;
      else out.push_back(e);
    }
    v = std::move(out);
  };
  merge(word);
  merge(gram_raw);
  normalise(word);
  normalise(gram_raw);
  for (const auto& [id, w] : word) p.word[id] += weight * w;
  for (const auto& [id, w] : gram_raw) p.gram[id] += weight * w;
  p.mass += weight;
}

void SemanticSpace::finish(SemanticProfile& p) const {
  p.word_sq = 0.0;
  p.gram_sq = 0.0;
  for (double x : p.word) p.word_sq += x * x;
  for (double x : p.gram) p.gram_sq += x * x;
}

ChannelCosine SemanticSpace::cosine(std::size_t unit, const SemanticProfile& p, double own_weight) const {
  UnitVec v = unit_vec(unit);
  auto one = [&](const std::vector<std::pair<std::uint32_t, double>>& vec, const std::vector<double>& prof,
                 double prof_sq) {
    if (vec.empty()) return 0.0;
    double dot = 0.0;
    for (const auto& [id, w] : vec) dot += w * prof[id];
    double ow = own_weight > 0.0 ? own_weight : 0.0;
    double sq = prof_sq;
    if (ow > 0.0) {
      // (P - ow v): v is unit length, so |P - ow v|^2 = |P|^2 - 2 ow (v.P) + ow^2.
      sq = prof_sq - 2.0 * ow * dot + ow * ow;
      dot -= ow;
    }
    if (sq <= 1e-12) return 0.0;
    return std::max(0.0, dot / std::sqrt(sq));
  };
  return ChannelCosine{one(v.word, p.word, p.word_sq), one(v.gram, p.gram, p.gram_sq)};
}

double SemanticSpace::similarity(std::size_t a, std::size_t b) const {
  UnitVec va = unit_vec(a), vb = unit_vec(b);
  auto dot = [](const std::vector<std::pair<std::uint32_t, double>>& x, const std::vector<std::pair<std::uint32_t, double>>& y) {
    // both are sorted by id only for grams; words follow top_terms order, so use a map-free
    // merge on sorted copies.
    std::vector<std::pair<std::uint32_t, double>> sx = x, sy = y;
    auto cmp = [](const auto& l, const auto& r) { return l.first < r.first; };
    std::sort(sx.begin(), sx.end(), cmp);
    std::sort(sy.begin(), sy.end(), cmp);
    double s = 0.0;
    std::size_t i = 0, j = 0;
    while (i < sx.size() && j < sy.size()) {
      if (sx[i].first == sy[j].first) s += sx[i++].second * sy[j++].second;
      else if (sx[i].first < sy[j].first) ++i;
      else ++j;
    }
    return s;
  };
  return 0.5 * (dot(va.word, vb.word) + dot(va.gram, vb.gram));
}

}  // namespace loom::catalog::internal
