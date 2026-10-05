// OWNER: wave 2 semantic/graph. Port of core/selector.py (SelectorEngine):
// keyword tier (exact), native TF-IDF tier reproducing sklearn
// TfidfVectorizer defaults, and an embedding tier via an injected provider.
#include "loom/selector.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom {

namespace {
Status validate_embeddings(const std::vector<std::vector<float>>& vectors, std::size_t expected_count,
                           std::optional<std::size_t> expected_dimension = {}) {
  if (vectors.size() != expected_count)
    return Error(Errc::InvalidArgument, "embedding provider returned a different document count");
  if (vectors.empty()) return {};
  const auto dimension = expected_dimension.value_or(vectors.front().size());
  if (!dimension) return Error(Errc::InvalidArgument, "embedding provider returned an empty vector");
  for (const auto& vector : vectors) {
    if (vector.size() != dimension)
      return Error(Errc::InvalidArgument, "embedding provider returned inconsistent vector dimensions");
    for (float value : vector)
      if (!std::isfinite(value)) return Error(Errc::InvalidArgument, "embedding provider returned a nonfinite coordinate");
  }
  return {};
}

// Maximal runs of Unicode "word" code points (sklearn's `(?u)\b\w\w+\b` when
// min_len=2, Python's `\w+` for the keyword tier when min_len=1).
std::vector<std::string> word_runs(std::string_view text, std::size_t min_len) {
  std::vector<std::string> out;
  std::u32string t = utf8::decode(text);
  std::size_t i = 0;
  while (i < t.size()) {
    if (!unicode::is_word(t[i])) {
      i++;
      continue;
    }
    std::size_t j = i;
    while (j < t.size() && unicode::is_word(t[j])) j++;
    if (j - i >= min_len) out.push_back(utf8::encode(t.substr(i, j - i)));
    i = j;
  }
  return out;
}

std::vector<std::string> tokenize(std::string_view text, const Json& recipe) {
  std::string normalized = recipe.at("lowercase").get<bool>() ? utf8::to_lower(text) : std::string(text);
  return word_runs(normalized, recipe.at("min_token_length").get<std::size_t>());
}

double sparse_dot(const std::vector<std::pair<int, double>>& a, const std::vector<std::pair<int, double>>& b) {
  double sum = 0.0;
  std::size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i].first == b[j].first) {
      sum += a[i].second * b[j].second;
      i++;
      j++;
    } else if (a[i].first < b[j].first) {
      i++;
    } else {
      j++;
    }
  }
  return sum;
}

void l2_normalize(std::vector<std::pair<int, double>>& v) {
  double norm2 = 0.0;
  for (const auto& [c, val] : v) norm2 += val * val;
  if (norm2 <= 0.0) return;
  double norm = std::sqrt(norm2);
  for (auto& [c, val] : v) val /= norm;
}

double cosine_dense(const std::vector<float>& a, const std::vector<float>& b) {
  double dot = 0.0, na = 0.0, nb = 0.0;
  std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    double x = a[i], y = b[i];
    dot += x * y;
    na += x * x;
    nb += y * y;
  }
  if (na <= 0.0 || nb <= 0.0) return 0.0;
  return dot / (std::sqrt(na) * std::sqrt(nb));
}

}  // namespace

Json SelectorHit::to_json() const { return Json{{"id", id}, {"text", text}, {"score", score}}; }

struct SelectorEngine::Impl {
  int tier = SelectorEngine::kTierKeyword;
  std::optional<int> requested_tier;
  std::shared_ptr<EmbeddingProvider> embedder;
  std::vector<std::string> corpus;
  std::vector<std::string> ids;
  std::optional<RuntimeProfile> profile;
  std::optional<Error> profile_error;

  const Json& settings() const { return profile->values(); }

  // Tier 2 (TF-IDF), sklearn TfidfVectorizer(max_features=5000) semantics.
  std::vector<std::string> vocab;  // alphabetical, final (post max_features) order
  std::unordered_map<std::string, int> vocab_index;
  std::vector<double> idf;
  std::vector<std::vector<std::pair<int, double>>> doc_vecs;  // sparse, l2-normalized

  // Tier 1 (embedding).
  std::vector<std::vector<float>> embeddings;

  std::vector<SelectorHit> rank(const std::vector<double>& scores, int top_k) const {
    std::vector<int> idx(scores.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return scores[a] > scores[b]; });
    std::vector<SelectorHit> out;
    for (int i : idx) {
      if (static_cast<int>(out.size()) >= top_k) break;
      const auto& ranking = settings().at("ranking");
      const auto score = scores[static_cast<std::size_t>(i)];
      const auto minimum_score = ranking.at("minimum_score").get<double>();
      if (ranking.at("threshold_inclusive").get<bool>() ? score < minimum_score : score <= minimum_score) break;
      out.push_back(SelectorHit{ids[static_cast<std::size_t>(i)], corpus[static_cast<std::size_t>(i)],
                                scores[static_cast<std::size_t>(i)]});
    }
    return out;
  }

  void build_tfidf() {
    vocab.clear();
    vocab_index.clear();
    idf.clear();
    doc_vecs.clear();
    if (corpus.empty()) return;

    std::vector<std::vector<std::string>> doc_tokens(corpus.size());
    std::unordered_map<std::string, long long> total_count;
    std::unordered_map<std::string, int> doc_freq;
    for (std::size_t d = 0; d < corpus.size(); ++d) {
      doc_tokens[d] = tokenize(corpus[d], settings().at("tfidf"));
      std::unordered_set<std::string> seen;
      for (const auto& t : doc_tokens[d]) {
        total_count[t]++;
        if (seen.insert(t).second) doc_freq[t]++;
      }
    }

    std::vector<std::string> terms;
    terms.reserve(total_count.size());
    for (const auto& [t, c] : total_count) terms.push_back(t);
    const auto max_features = settings().at("tfidf").at("max_features").get<std::size_t>();
    if (max_features && terms.size() > max_features) {
      std::sort(terms.begin(), terms.end(), [&](const std::string& a, const std::string& b) {
        long long ca = total_count[a], cb = total_count[b];
        if (ca != cb) return ca > cb;
        return a < b;
      });
      terms.resize(max_features);
    }
    std::sort(terms.begin(), terms.end());  // sklearn re-alphabetizes the kept vocabulary
    vocab = terms;
    for (std::size_t i = 0; i < vocab.size(); ++i) vocab_index[vocab[i]] = static_cast<int>(i);

    double n_docs = static_cast<double>(corpus.size());
    idf.assign(vocab.size(), 0.0);
    for (std::size_t i = 0; i < vocab.size(); ++i) {
      int df = doc_freq[vocab[i]];
      const auto& recipe = settings().at("tfidf");
      const auto smoothing = recipe.at("idf_smoothing").get<double>();
      idf[i] = std::log((smoothing + n_docs) / (smoothing + static_cast<double>(df))) + recipe.at("idf_offset").get<double>();
    }

    doc_vecs.assign(corpus.size(), {});
    for (std::size_t d = 0; d < corpus.size(); ++d) {
      std::unordered_map<int, double> counts;
      for (const auto& t : doc_tokens[d]) {
        auto it = vocab_index.find(t);
        if (it == vocab_index.end()) continue;
        counts[it->second] += 1.0;
      }
      std::vector<std::pair<int, double>> vec;
      vec.reserve(counts.size());
      for (const auto& [col, tf] : counts) {
        const auto scaled_tf = settings().at("tfidf").at("sublinear_tf").get<bool>() ? 1.0 + std::log(tf) : tf;
        vec.push_back({col, scaled_tf * idf[static_cast<std::size_t>(col)]});
      }
      std::sort(vec.begin(), vec.end());
      if (settings().at("tfidf").at("normalize").get<bool>()) l2_normalize(vec);
      doc_vecs[d] = std::move(vec);
    }
  }

  std::vector<std::pair<int, double>> tfidf_query_vec(std::string_view query) const {
    std::unordered_map<int, double> counts;
    for (const auto& t : tokenize(query, settings().at("tfidf"))) {
      auto it = vocab_index.find(t);
      if (it == vocab_index.end()) continue;
      counts[it->second] += 1.0;
    }
    std::vector<std::pair<int, double>> vec;
    vec.reserve(counts.size());
    for (const auto& [col, tf] : counts) {
        const auto scaled_tf = settings().at("tfidf").at("sublinear_tf").get<bool>() ? 1.0 + std::log(tf) : tf;
        vec.push_back({col, scaled_tf * idf[static_cast<std::size_t>(col)]});
      }
    std::sort(vec.begin(), vec.end());
    if (settings().at("tfidf").at("normalize").get<bool>()) l2_normalize(vec);
    return vec;
  }
};

SelectorEngine::SelectorEngine(std::optional<int> tier, std::shared_ptr<EmbeddingProvider> embedder,
                               std::optional<RuntimeProfile> requested_profile)
    : impl_(std::make_unique<Impl>()) {
  auto profile = RuntimeProfile::builtin("selector");
  if (!profile) { impl_->profile_error = profile.error(); return; }
  if (requested_profile) {
    if (requested_profile->domain() != "selector") {
      impl_->profile_error = Error(Errc::InvalidArgument, "expected selector profile");
      return;
    }
    profile = profile->with_values(requested_profile->values());
    if (!profile) { impl_->profile_error = profile.error(); return; }
  }
  impl_->profile = std::move(*profile);
  impl_->requested_tier = tier;
  const auto& tiers = impl_->settings().at("tiers");
  int t;
  if (tier) {
    t = *tier;
    if (t == kTierEmbedding && !embedder) t = tiers.at("without_embedding").get<int>();  // Python: model failed to load -> tier 2
  } else {
    t = tiers.at(embedder ? "with_embedding" : "without_embedding").get<int>();  // Loom: native TF-IDF always available
  }
  impl_->tier = t;
  impl_->embedder = std::move(embedder);
  if (t == kTierEmbedding && !impl_->embedder)
    impl_->profile_error = Error(Errc::Unavailable, "configured embedding tier has no embedding provider");
}
SelectorEngine::~SelectorEngine() = default;
SelectorEngine::SelectorEngine(SelectorEngine&&) noexcept = default;
SelectorEngine& SelectorEngine::operator=(SelectorEngine&&) noexcept = default;

int SelectorEngine::tier() const noexcept { return impl_ ? impl_->tier : kTierKeyword; }

Status SelectorEngine::index(std::vector<std::string> texts, std::vector<std::string> ids) {
  if (impl_->profile_error) return *impl_->profile_error;
  if (texts.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return Error(Errc::InvalidArgument, "selector corpus exceeds native index representation");
  if (!ids.empty() && ids.size() != texts.size())
    return Error(Errc::InvalidArgument, "selector ids must have one entry per document");
  auto staged = std::make_unique<Impl>(*impl_);
  staged->corpus = std::move(texts);
  if (!ids.empty()) {
    staged->ids = std::move(ids);
  } else {
    staged->ids.clear();
    staged->ids.reserve(staged->corpus.size());
    for (std::size_t i = 0; i < staged->corpus.size(); ++i) staged->ids.push_back(std::to_string(i));
  }
  staged->vocab.clear();
  staged->vocab_index.clear();
  staged->idf.clear();
  staged->doc_vecs.clear();
  staged->embeddings.clear();

  if (staged->corpus.empty()) { impl_ = std::move(staged); return ok_status(); }

  if (staged->tier == kTierEmbedding && staged->embedder) {
    auto r = staged->embedder->embed(staged->corpus);
    if (r) {
      LOOM_TRY(validate_embeddings(*r, staged->corpus.size()));
      staged->embeddings = std::move(r).value();
      impl_ = std::move(staged);
      return ok_status();
    }
    staged->tier = staged->settings().at("tiers").at("embedding_failure").get<int>();
    if (staged->tier == kTierEmbedding) return r.error();
  }
  if (staged->tier == kTierTfIdf) staged->build_tfidf();
  impl_ = std::move(staged);
  return ok_status();
}

Result<std::vector<SelectorHit>> SelectorEngine::search_checked(std::string_view query, std::optional<int> requested_top_k) const {
  if (impl_->profile_error) return *impl_->profile_error;
  const auto top_k = requested_top_k.value_or(impl_->settings().at("ranking").at("top_k").get<int>());
  if (impl_->corpus.empty()) return std::vector<SelectorHit>{};
  if (impl_->tier == kTierEmbedding && impl_->embedder) {
    auto r = impl_->embedder->embed({std::string(query)});
    if (!r) return r.error();
    LOOM_TRY(validate_embeddings(*r, 1, impl_->embeddings.front().size()));
    const auto& qv = (*r)[0];
    std::vector<double> scores;
    scores.reserve(impl_->embeddings.size());
    for (const auto& dv : impl_->embeddings) scores.push_back(cosine_dense(qv, dv));
    return impl_->rank(scores, top_k);
  }
  if (impl_->tier == kTierTfIdf) {
    auto qvec = impl_->tfidf_query_vec(query);
    std::vector<double> scores;
    scores.reserve(impl_->doc_vecs.size());
    for (const auto& dv : impl_->doc_vecs) scores.push_back(sparse_dot(qvec, dv));
    return impl_->rank(scores, top_k);
  }
  // Tier 3: keyword.
  auto qtokens_vec = tokenize(query, impl_->settings().at("keyword"));
  std::set<std::string> qtokens(qtokens_vec.begin(), qtokens_vec.end());
  if (qtokens.empty()) return std::vector<SelectorHit>{};
  std::vector<double> scores;
  scores.reserve(impl_->corpus.size());
  for (const auto& text : impl_->corpus) {
    std::string text_lower = impl_->settings().at("keyword").at("lowercase").get<bool>() ? utf8::to_lower(text) : text;
    int hits = 0;
    for (const auto& t : qtokens) {
      if (text_lower.find(t) != std::string::npos) hits++;
    }
    scores.push_back(static_cast<double>(hits) / static_cast<double>(qtokens.size()));
  }
  return impl_->rank(scores, top_k);
}


std::vector<SelectorHit> SelectorEngine::search(std::string_view query, std::optional<int> top_k) const {
  auto result = search_checked(query, top_k);
  if (!result) {
    if (impl_->profile_error) throw std::runtime_error(result.error().to_string());
    // Legacy embedding-query failures were empty results. The checked entry
    // point exposes the provider error without changing that default API.
    return {};
  }
  return std::move(*result);
}

Result<Json> SelectorEngine::profile_inspection() const {
  if (impl_->profile_error) return *impl_->profile_error;
  return impl_->profile->inspection();
}

Status SelectorEngine::set_profile(const RuntimeProfile& profile) {
  if (profile.domain() != "selector") return Error(Errc::InvalidArgument, "expected selector profile");
  // Revalidate against the supported consumer descriptor, even when the input
  // was built from a caller-supplied definition with a permissive schema.
  LOOM_TRY_ASSIGN(auto builtin, RuntimeProfile::builtin("selector"));
  LOOM_TRY_ASSIGN(auto checked, builtin.with_values(profile.values()));
  auto staged = std::make_unique<Impl>(*impl_);
  staged->profile = std::move(checked);
  staged->profile_error.reset();
  const auto& tiers = staged->settings().at("tiers");
  staged->tier = staged->requested_tier.value_or(tiers.at(staged->embedder ? "with_embedding" : "without_embedding").get<int>());
  if (staged->tier == kTierEmbedding && !staged->embedder)
    staged->tier = tiers.at("without_embedding").get<int>();
  if (staged->tier == kTierEmbedding && !staged->embedder)
    return Error(Errc::Unavailable, "configured embedding tier has no embedding provider");
  if (staged->tier == kTierEmbedding && staged->embedder && !staged->corpus.empty()) {
    auto embeddings = staged->embedder->embed(staged->corpus);
    if (!embeddings) return embeddings.error();
    LOOM_TRY(validate_embeddings(*embeddings, staged->corpus.size()));
    staged->embeddings = std::move(*embeddings);
  } else if (staged->tier == kTierTfIdf) {
    staged->build_tfidf();
  }
  impl_ = std::move(staged);
  return {};
}

std::size_t SelectorEngine::size() const noexcept { return impl_ ? impl_->corpus.size() : 0; }

}  // namespace loom
