// OWNER: wave 2 semantic/graph. Port of core/selector.py (SelectorEngine):
// keyword tier (exact), native TF-IDF tier reproducing sklearn
// TfidfVectorizer defaults, and an embedding tier via an injected provider.
#include "loom/selector.h"

#include <algorithm>
#include <cmath>
#include <functional>
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


// ── Built-in tiers (registry entries) ───────────────────────────────────────

// Tier "tfidf": sklearn TfidfVectorizer(max_features=5000) semantics.
class TfIdfTier final : public SearchTierBackend {
 public:
  explicit TfIdfTier(const Json& settings) : recipe_(settings.at("tfidf")) {}
  std::string_view name() const override { return "tfidf"; }

  Status index(const std::vector<std::string>& corpus) override {
    vocab_.clear();
    vocab_index_.clear();
    idf_.clear();
    doc_vecs_.clear();
    if (corpus.empty()) return {};

    std::vector<std::vector<std::string>> doc_tokens(corpus.size());
    std::unordered_map<std::string, long long> total_count;
    std::unordered_map<std::string, int> doc_freq;
    for (std::size_t d = 0; d < corpus.size(); ++d) {
      doc_tokens[d] = tokenize(corpus[d], recipe_);
      std::unordered_set<std::string> seen;
      for (const auto& t : doc_tokens[d]) {
        total_count[t]++;
        if (seen.insert(t).second) doc_freq[t]++;
      }
    }

    std::vector<std::string> terms;
    terms.reserve(total_count.size());
    for (const auto& [t, c] : total_count) terms.push_back(t);
    const auto max_features = recipe_.at("max_features").get<std::size_t>();
    if (max_features && terms.size() > max_features) {
      std::sort(terms.begin(), terms.end(), [&](const std::string& a, const std::string& b) {
        long long ca = total_count[a], cb = total_count[b];
        if (ca != cb) return ca > cb;
        return a < b;
      });
      terms.resize(max_features);
    }
    std::sort(terms.begin(), terms.end());  // sklearn re-alphabetizes the kept vocabulary
    vocab_ = terms;
    for (std::size_t i = 0; i < vocab_.size(); ++i) vocab_index_[vocab_[i]] = static_cast<int>(i);

    double n_docs = static_cast<double>(corpus.size());
    idf_.assign(vocab_.size(), 0.0);
    for (std::size_t i = 0; i < vocab_.size(); ++i) {
      int df = doc_freq[vocab_[i]];
      const auto smoothing = recipe_.at("idf_smoothing").get<double>();
      idf_[i] = std::log((smoothing + n_docs) / (smoothing + static_cast<double>(df))) + recipe_.at("idf_offset").get<double>();
    }

    doc_vecs_.assign(corpus.size(), {});
    for (std::size_t d = 0; d < corpus.size(); ++d) doc_vecs_[d] = vectorize(doc_tokens[d]);
    return {};
  }

  Result<std::vector<double>> score(std::string_view query) const override {
    auto qvec = vectorize(tokenize(query, recipe_));
    std::vector<double> scores;
    scores.reserve(doc_vecs_.size());
    for (const auto& dv : doc_vecs_) scores.push_back(sparse_dot(qvec, dv));
    return scores;
  }

 private:
  std::vector<std::pair<int, double>> vectorize(const std::vector<std::string>& tokens) const {
    std::unordered_map<int, double> counts;
    for (const auto& t : tokens) {
      auto it = vocab_index_.find(t);
      if (it == vocab_index_.end()) continue;
      counts[it->second] += 1.0;
    }
    std::vector<std::pair<int, double>> vec;
    vec.reserve(counts.size());
    for (const auto& [col, tf] : counts) {
      const auto scaled_tf = recipe_.at("sublinear_tf").get<bool>() ? 1.0 + std::log(tf) : tf;
      vec.push_back({col, scaled_tf * idf_[static_cast<std::size_t>(col)]});
    }
    std::sort(vec.begin(), vec.end());
    if (recipe_.at("normalize").get<bool>()) l2_normalize(vec);
    return vec;
  }

  Json recipe_;
  std::vector<std::string> vocab_;  // alphabetical, final (post max_features) order
  std::unordered_map<std::string, int> vocab_index_;
  std::vector<double> idf_;
  std::vector<std::vector<std::pair<int, double>>> doc_vecs_;  // sparse, l2-normalized
};

// Tier "keyword": Python _search_keyword.
class KeywordTier final : public SearchTierBackend {
 public:
  explicit KeywordTier(const Json& settings) : recipe_(settings.at("keyword")) {}
  std::string_view name() const override { return "keyword"; }
  Status index(const std::vector<std::string>& corpus) override {
    corpus_ = corpus;
    return {};
  }
  Result<std::vector<double>> score(std::string_view query) const override {
    auto qtokens_vec = tokenize(query, recipe_);
    std::set<std::string> qtokens(qtokens_vec.begin(), qtokens_vec.end());
    if (qtokens.empty()) return std::vector<double>{};
    std::vector<double> scores;
    scores.reserve(corpus_.size());
    const bool lower = recipe_.at("lowercase").get<bool>();
    for (const auto& text : corpus_) {
      std::string text_lower = lower ? utf8::to_lower(text) : text;
      int hits = 0;
      for (const auto& t : qtokens)
        if (text_lower.find(t) != std::string::npos) hits++;
      scores.push_back(static_cast<double>(hits) / static_cast<double>(qtokens.size()));
    }
    return scores;
  }

 private:
  Json recipe_;
  std::vector<std::string> corpus_;
};

// Tier "embedding": cosine similarity over an injected provider.
class EmbeddingTier final : public SearchTierBackend {
 public:
  explicit EmbeddingTier(std::shared_ptr<EmbeddingProvider> embedder) : embedder_(std::move(embedder)) {}
  std::string_view name() const override { return "embedding"; }
  bool handover_on_failure() const override { return provider_failed_; }
  Status index(const std::vector<std::string>& corpus) override {
    provider_failed_ = false;
    auto r = embedder_->embed(corpus);
    if (!r) { provider_failed_ = true; return r.error(); }
    LOOM_TRY(validate_embeddings(*r, corpus.size()));
    embeddings_ = std::move(r).value();
    return {};
  }
  Result<std::vector<double>> score(std::string_view query) const override {
    auto r = embedder_->embed({std::string(query)});
    if (!r) return r.error();
    LOOM_TRY(validate_embeddings(*r, 1, embeddings_.front().size()));
    const auto& qv = (*r)[0];
    std::vector<double> scores;
    scores.reserve(embeddings_.size());
    for (const auto& dv : embeddings_) scores.push_back(cosine_dense(qv, dv));
    return scores;
  }

 private:
  std::shared_ptr<EmbeddingProvider> embedder_;
  std::vector<std::vector<float>> embeddings_;
  bool provider_failed_ = false;
};

}  // namespace

// ── Registry ────────────────────────────────────────────────────────────────

SearchTierRegistry::SearchTierRegistry() {
  entries_.emplace_back("embedding", [](const SearchTierContext& c) -> std::unique_ptr<SearchTierBackend> {
    if (!c.embedder) return nullptr;
    return std::make_unique<EmbeddingTier>(c.embedder);
  });
  entries_.emplace_back("tfidf", [](const SearchTierContext& c) -> std::unique_ptr<SearchTierBackend> {
    return std::make_unique<TfIdfTier>(c.settings);
  });
  entries_.emplace_back("keyword", [](const SearchTierContext& c) -> std::unique_ptr<SearchTierBackend> {
    return std::make_unique<KeywordTier>(c.settings);
  });
}

SearchTierRegistry& SearchTierRegistry::instance() {
  static SearchTierRegistry r;
  return r;
}

void SearchTierRegistry::add(std::string name, SearchTierFactory factory) {
  std::lock_guard lk(mu_);
  for (auto& e : entries_)
    if (e.first == name) { e.second = std::move(factory); return; }
  entries_.emplace_back(std::move(name), std::move(factory));
}

std::unique_ptr<SearchTierBackend> SearchTierRegistry::create(std::string_view name, const SearchTierContext& ctx) const {
  SearchTierFactory f;
  {
    std::lock_guard lk(mu_);
    for (const auto& e : entries_)
      if (e.first == name) { f = e.second; break; }
  }
  return f ? f(ctx) : nullptr;
}

bool SearchTierRegistry::contains(std::string_view name) const {
  std::lock_guard lk(mu_);
  for (const auto& e : entries_)
    if (e.first == name) return true;
  return false;
}

std::vector<std::string> SearchTierRegistry::names() const {
  std::lock_guard lk(mu_);
  std::vector<std::string> out;
  for (const auto& e : entries_) out.push_back(e.first);
  return out;
}

Json SelectorHit::to_json() const { return Json{{"id", id}, {"text", text}, {"score", score}}; }

// ── Engine ──────────────────────────────────────────────────────────────────

struct SelectorEngine::Impl {
  std::optional<std::vector<std::string>> composition;  // explicit tier list; nullopt = derived from the legacy request/profile
  std::optional<int> legacy_request;
  std::shared_ptr<EmbeddingProvider> embedder;
  std::vector<std::string> corpus;
  std::vector<std::string> ids;
  std::optional<RuntimeProfile> profile;
  std::optional<Error> profile_error;
  std::vector<std::string> chain;  // resolved preference order
  std::size_t active = 0;          // index into chain
  std::shared_ptr<const SearchTierBackend> backend;  // immutable once indexed; shared between staged copies

  const Json& settings() const { return profile->values(); }
  SearchTierContext context() const { return SearchTierContext{settings(), embedder}; }

  Result<std::string> name_for_code(int code) const {
    const auto& names = settings().at("tiers").at("names");
    if (code < 1 || static_cast<std::size_t>(code) > names.size())
      return Error(Errc::InvalidArgument, "selector tier code has no entry in tiers.names");
    return names.at(static_cast<std::size_t>(code - 1)).get<std::string>();
  }

  // The numeric request maps to the same preference lists the old tier logic produced.
  Status resolve_chain() {
    if (composition) { chain = *composition; return {}; }
    const auto& tiers = settings().at("tiers");
    const int code = legacy_request.value_or(tiers.at(embedder ? "with_embedding" : "without_embedding").get<int>());
    LOOM_TRY_ASSIGN(auto first, name_for_code(code));
    chain.clear();
    if (first == "embedding" && !embedder) {
      LOOM_TRY_ASSIGN(auto fallback, name_for_code(tiers.at("without_embedding").get<int>()));
      chain.push_back(std::move(fallback));
    } else {
      chain.push_back(first);
      if (first == "embedding") {
        LOOM_TRY_ASSIGN(auto fallback, name_for_code(tiers.at("embedding_failure").get<int>()));
        if (fallback != "embedding") chain.push_back(std::move(fallback));
      }
    }
    return {};
  }

  // First usable entry at or after `from`; creates its (unindexed) backend.
  Status activate(std::size_t from) {
    std::string last_reason = "no search tier configured";
    for (std::size_t i = from; i < chain.size(); ++i) {
      if (!SearchTierRegistry::instance().contains(chain[i])) { last_reason = "unknown search tier '" + chain[i] + "'"; continue; }
      auto b = SearchTierRegistry::instance().create(chain[i], context());
      if (!b) { last_reason = "configured " + chain[i] + " tier has no embedding provider"; continue; }
      active = i;
      backend = std::move(b);
      return {};
    }
    return Error(Errc::Unavailable, last_reason);
  }

  Status setup() {
    LOOM_TRY(resolve_chain());
    return activate(0);
  }

  // Indexes corpus on the active tier. `handover`: a provider failure moves on
  // to the next tier (index time); false returns the error (profile change).
  Status build(bool handover) {
    if (corpus.empty()) return {};
    while (true) {
      auto fresh = SearchTierRegistry::instance().create(chain[active], context());
      if (!fresh) return Error(Errc::Unavailable, "search tier " + chain[active] + " is unavailable");
      auto st = fresh->index(corpus);
      if (st) { backend = std::move(fresh); return {}; }
      if (!handover || !fresh->handover_on_failure()) return st;
      // Provider failure: next chain entry, else the profile's failure tier.
      if (active + 1 >= chain.size()) {
        auto name = name_for_code(settings().at("tiers").at("embedding_failure").get<int>());
        if (!name || *name == chain[active]) return st;
        chain.push_back(*name);
      }
      auto moved = activate(active + 1);
      if (!moved) return st;
    }
  }

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
};

namespace {
std::unique_ptr<SelectorEngine::Impl> make_impl(std::optional<std::vector<std::string>> composition,
                                                std::optional<int> legacy_request,
                                                std::shared_ptr<EmbeddingProvider> embedder,
                                                const std::optional<RuntimeProfile>& requested_profile) {
  auto impl = std::make_unique<SelectorEngine::Impl>();
  auto profile = RuntimeProfile::builtin("selector");
  if (!profile) { impl->profile_error = profile.error(); return impl; }
  if (requested_profile) {
    if (requested_profile->domain() != "selector") {
      impl->profile_error = Error(Errc::InvalidArgument, "expected selector profile");
      return impl;
    }
    profile = profile->with_values(requested_profile->values());
    if (!profile) { impl->profile_error = profile.error(); return impl; }
  }
  impl->profile = std::move(*profile);
  impl->composition = std::move(composition);
  impl->legacy_request = legacy_request;
  impl->embedder = std::move(embedder);
  if (auto st = impl->setup(); !st) impl->profile_error = st.error();
  return impl;
}
}  // namespace

SelectorEngine::SelectorEngine(std::vector<SearchTier> tiers, std::shared_ptr<EmbeddingProvider> embedder,
                               std::optional<RuntimeProfile> requested_profile) {
  std::vector<std::string> names;
  for (auto& t : tiers) names.push_back(std::move(t.name));
  impl_ = make_impl(std::move(names), std::nullopt, std::move(embedder), requested_profile);
}

SelectorEngine::SelectorEngine(std::optional<int> legacy_tier, std::shared_ptr<EmbeddingProvider> embedder,
                               std::optional<RuntimeProfile> requested_profile)
    : impl_(make_impl(std::nullopt, legacy_tier, std::move(embedder), requested_profile)) {}

SelectorEngine::~SelectorEngine() = default;
SelectorEngine::SelectorEngine(SelectorEngine&&) noexcept = default;
SelectorEngine& SelectorEngine::operator=(SelectorEngine&&) noexcept = default;

std::string SelectorEngine::active_tier() const {
  if (!impl_ || impl_->chain.empty() || impl_->active >= impl_->chain.size()) return "keyword";
  return impl_->chain[impl_->active];
}

int SelectorEngine::legacy_tier_code() const noexcept {
  if (!impl_ || !impl_->profile) return 0;
  const auto name = active_tier();
  const auto& names = impl_->settings().at("tiers").at("names");
  for (std::size_t i = 0; i < names.size(); ++i)
    if (names.at(i).get<std::string>() == name) return static_cast<int>(i) + 1;
  return 0;
}

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
  if (staged->corpus.empty()) {
    LOOM_TRY(staged->activate(staged->active));  // fresh, empty backend
    impl_ = std::move(staged);
    return ok_status();
  }
  LOOM_TRY(staged->build(true));
  impl_ = std::move(staged);
  return ok_status();
}

Result<std::vector<SelectorHit>> SelectorEngine::search_checked(std::string_view query, std::optional<int> requested_top_k) const {
  if (impl_->profile_error) return *impl_->profile_error;
  const auto top_k = requested_top_k.value_or(impl_->settings().at("ranking").at("top_k").get<int>());
  if (impl_->corpus.empty()) return std::vector<SelectorHit>{};
  LOOM_TRY_ASSIGN(auto scores, impl_->backend->score(query));
  if (scores.empty()) return std::vector<SelectorHit>{};
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
  LOOM_TRY(staged->setup());
  LOOM_TRY(staged->build(false));
  impl_ = std::move(staged);
  return {};
}

std::size_t SelectorEngine::size() const noexcept { return impl_ ? impl_->corpus.size() : 0; }

}  // namespace loom
