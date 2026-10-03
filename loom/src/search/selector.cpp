// OWNER: wave 2 semantic/graph. Port of core/selector.py (SelectorEngine):
// keyword tier (exact), native TF-IDF tier reproducing sklearn
// TfidfVectorizer defaults, and an embedding tier via an injected provider.
#include "loom/selector.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom {

namespace {

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

std::vector<std::string> tokenize_tfidf(std::string_view text) {
  std::string lower = utf8::to_lower(text);
  return word_runs(lower, 2);
}
std::vector<std::string> tokenize_keyword(std::string_view text) {
  std::string lower = utf8::to_lower(text);
  return word_runs(lower, 1);
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
  std::shared_ptr<EmbeddingProvider> embedder;
  std::vector<std::string> corpus;
  std::vector<std::string> ids;

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
      if (scores[static_cast<std::size_t>(i)] <= 0.0) break;
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
      doc_tokens[d] = tokenize_tfidf(corpus[d]);
      std::unordered_set<std::string> seen;
      for (const auto& t : doc_tokens[d]) {
        total_count[t]++;
        if (seen.insert(t).second) doc_freq[t]++;
      }
    }

    std::vector<std::string> terms;
    terms.reserve(total_count.size());
    for (const auto& [t, c] : total_count) terms.push_back(t);
    constexpr std::size_t kMaxFeatures = 5000;
    if (terms.size() > kMaxFeatures) {
      std::sort(terms.begin(), terms.end(), [&](const std::string& a, const std::string& b) {
        long long ca = total_count[a], cb = total_count[b];
        if (ca != cb) return ca > cb;
        return a < b;
      });
      terms.resize(kMaxFeatures);
    }
    std::sort(terms.begin(), terms.end());  // sklearn re-alphabetizes the kept vocabulary
    vocab = terms;
    for (std::size_t i = 0; i < vocab.size(); ++i) vocab_index[vocab[i]] = static_cast<int>(i);

    double n_docs = static_cast<double>(corpus.size());
    idf.assign(vocab.size(), 0.0);
    for (std::size_t i = 0; i < vocab.size(); ++i) {
      int df = doc_freq[vocab[i]];
      idf[i] = std::log((1.0 + n_docs) / (1.0 + static_cast<double>(df))) + 1.0;
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
      for (const auto& [col, tf] : counts) vec.push_back({col, tf * idf[static_cast<std::size_t>(col)]});
      std::sort(vec.begin(), vec.end());
      l2_normalize(vec);
      doc_vecs[d] = std::move(vec);
    }
  }

  std::vector<std::pair<int, double>> tfidf_query_vec(std::string_view query) const {
    std::unordered_map<int, double> counts;
    for (const auto& t : tokenize_tfidf(query)) {
      auto it = vocab_index.find(t);
      if (it == vocab_index.end()) continue;
      counts[it->second] += 1.0;
    }
    std::vector<std::pair<int, double>> vec;
    vec.reserve(counts.size());
    for (const auto& [col, tf] : counts) vec.push_back({col, tf * idf[static_cast<std::size_t>(col)]});
    std::sort(vec.begin(), vec.end());
    l2_normalize(vec);
    return vec;
  }
};

SelectorEngine::SelectorEngine(std::optional<int> tier, std::shared_ptr<EmbeddingProvider> embedder)
    : impl_(std::make_unique<Impl>()) {
  int t;
  if (tier) {
    t = *tier;
    if (t == kTierEmbedding && !embedder) t = kTierTfIdf;  // Python: model failed to load -> tier 2
  } else {
    t = embedder ? kTierEmbedding : kTierTfIdf;  // Loom: native TF-IDF always available
  }
  impl_->tier = t;
  impl_->embedder = std::move(embedder);
}
SelectorEngine::~SelectorEngine() = default;
SelectorEngine::SelectorEngine(SelectorEngine&&) noexcept = default;
SelectorEngine& SelectorEngine::operator=(SelectorEngine&&) noexcept = default;

int SelectorEngine::tier() const noexcept { return impl_ ? impl_->tier : kTierKeyword; }

Status SelectorEngine::index(std::vector<std::string> texts, std::vector<std::string> ids) {
  impl_->corpus = std::move(texts);
  if (!ids.empty()) {
    impl_->ids = std::move(ids);
  } else {
    impl_->ids.clear();
    impl_->ids.reserve(impl_->corpus.size());
    for (std::size_t i = 0; i < impl_->corpus.size(); ++i) impl_->ids.push_back(std::to_string(i));
  }
  impl_->vocab.clear();
  impl_->vocab_index.clear();
  impl_->idf.clear();
  impl_->doc_vecs.clear();
  impl_->embeddings.clear();

  if (impl_->corpus.empty()) return ok_status();

  if (impl_->tier == kTierEmbedding && impl_->embedder) {
    auto r = impl_->embedder->embed(impl_->corpus);
    if (r) {
      impl_->embeddings = std::move(r).value();
      return ok_status();
    }
    impl_->tier = kTierTfIdf;  // Python: load/encode failure -> fall back
  }
  if (impl_->tier == kTierTfIdf) impl_->build_tfidf();
  return ok_status();
}

std::vector<SelectorHit> SelectorEngine::search(std::string_view query, int top_k) const {
  if (impl_->corpus.empty()) return {};
  if (impl_->tier == kTierEmbedding && impl_->embedder) {
    auto r = impl_->embedder->embed({std::string(query)});
    if (!r || r->empty()) return {};
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
  auto qtokens_vec = tokenize_keyword(query);
  std::set<std::string> qtokens(qtokens_vec.begin(), qtokens_vec.end());
  if (qtokens.empty()) return {};
  std::vector<double> scores;
  scores.reserve(impl_->corpus.size());
  for (const auto& text : impl_->corpus) {
    std::string text_lower = utf8::to_lower(text);
    int hits = 0;
    for (const auto& t : qtokens) {
      if (text_lower.find(t) != std::string::npos) hits++;
    }
    scores.push_back(static_cast<double>(hits) / static_cast<double>(qtokens.size()));
  }
  return impl_->rank(scores, top_k);
}

std::size_t SelectorEngine::size() const noexcept { return impl_ ? impl_->corpus.size() : 0; }

}  // namespace loom
