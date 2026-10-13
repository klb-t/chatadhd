// loom/selector.h — port of core/selector.py (SelectorEngine). [OWNER: wave 2 semantic/graph]
//
// Three tiers, highest available wins:
//   1 embedding  — through an injected EmbeddingProvider (capability, not a
//                  hard dependency; Python used sentence-transformers
//                  all-MiniLM-L6-v2); cosine similarity
//   2 TF-IDF     — implemented natively, reproducing sklearn
//                  TfidfVectorizer(max_features=5000) defaults: lowercase,
//                  token pattern (?u)\b\w\w+\b, smooth_idf (idf = ln((1+n)/
//                  (1+df)) + 1), raw tf, l2 normalisation, vocabulary limited
//                  to the 5000 most frequent terms by corpus count (ties by
//                  term order as sklearn: sorted by (-count, term)); cosine
//                  similarity against the l2-normalised query vector
//   3 keyword    — Python _search_keyword: tokens = set(re.findall(r'\w+',
//                  query.lower())); score = |{t : t in text.lower()}| / |tokens|
// Deviation: Python only offered tier 2 with scikit-learn installed; Loom's
// native TF-IDF is always available, so the default tier is 2 (1 when an
// embedder is supplied).
// _rank(): stable sort by score descending (Python sorted(reverse=True) keeps
// equal scores in corpus order), take top_k, stop at the first score <= 0.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/runtime_profile.h"
#include "loom/util/json.h"

namespace loom {

struct SelectorHit {
  std::string id;
  std::string text;
  double score = 0.0;
  Json to_json() const;  // {"id","text","score"}
};

// Embedding capability (local model, remote API, on-device...).
class EmbeddingProvider {
 public:
  virtual ~EmbeddingProvider() = default;
  virtual std::string model_id() const = 0;
  virtual Result<std::vector<std::vector<float>>> embed(const std::vector<std::string>& texts) = 0;
};

// A search tier is a registry entry, not a number. Composition order is
// preference order: the first usable tier answers; an embedding tier without a
// provider (or whose provider fails) hands over to the next entry.
struct SearchTier {
  std::string name;  // registered name: "embedding" | "tfidf" | "keyword" | anything added to the registry
  SearchTier(std::string n) : name(std::move(n)) {}
  SearchTier(const char* n) : name(n) {}
};

struct SearchTierContext {
  Json settings;  // the whole selector profile values (tfidf / keyword / ranking / tiers)
  std::shared_ptr<EmbeddingProvider> embedder;
};

class SearchTierBackend {
 public:
  virtual ~SearchTierBackend() = default;
  virtual std::string_view name() const = 0;
  virtual Status index(const std::vector<std::string>& corpus) = 0;
  // True when the last index() failure was the tier's own provider failing, so the
  // engine may hand over to the next tier instead of reporting the error.
  virtual bool handover_on_failure() const { return false; }
  // One score per corpus entry; an empty vector means "no match at all".
  virtual Result<std::vector<double>> score(std::string_view query) const = 0;
};

// Returns nullptr when the tier cannot work in this context (e.g. no embedder).
using SearchTierFactory = std::function<std::unique_ptr<SearchTierBackend>(const SearchTierContext&)>;

class SearchTierRegistry {
 public:
  static SearchTierRegistry& instance();  // pre-registered: embedding, tfidf, keyword
  void add(std::string name, SearchTierFactory factory);  // replaces an entry of the same name
  std::unique_ptr<SearchTierBackend> create(std::string_view name, const SearchTierContext& ctx) const;
  bool contains(std::string_view name) const;
  std::vector<std::string> names() const;

 private:
  SearchTierRegistry();
  mutable std::mutex mu_;
  std::vector<std::pair<std::string, SearchTierFactory>> entries_;
};

class SelectorEngine {
 public:
  // Legacy numeric tier codes; they only resolve through the profile's
  // tiers.names list. Kept as aliases for existing callers, not for new code.
  static constexpr int kTierEmbedding = 1;
  static constexpr int kTierTfIdf = 2;
  static constexpr int kTierKeyword = 3;

  // Composition: ordered tiers (see SearchTier). An unknown name or a list in
  // which no tier is usable makes index()/search() report the error.
  explicit SelectorEngine(std::vector<SearchTier> tiers, std::shared_ptr<EmbeddingProvider> embedder = nullptr,
                          std::optional<RuntimeProfile> profile = std::nullopt);
  // Adapter for the numeric form: nullopt = profile preset (best available).
  // Requesting code 1 without an embedder falls back to the profile's
  // without_embedding tier (Python fell back when the model failed to load).
  // No profile means the exact historical parameters from selector.pack.
  explicit SelectorEngine(std::optional<int> legacy_tier = std::nullopt,
                          std::shared_ptr<EmbeddingProvider> embedder = nullptr,
                          std::optional<RuntimeProfile> profile = std::nullopt);
  ~SelectorEngine();
  SelectorEngine(SelectorEngine&&) noexcept;
  SelectorEngine& operator=(SelectorEngine&&) noexcept;

  // Registered name of the tier answering now ("embedding" | "tfidf" | "keyword" | ...).
  std::string active_tier() const;
  // Deprecated alias: the profile's legacy code for active_tier() (0 if it has none).
  int legacy_tier_code() const noexcept;
  // ids default to "0","1",... (Python). An embedding failure hands over to the next tier.
  Status index(std::vector<std::string> texts, std::vector<std::string> ids = {});
  std::vector<SelectorHit> search(std::string_view query, std::optional<int> top_k = std::nullopt) const;
  Result<std::vector<SelectorHit>> search_checked(std::string_view query,
                                                 std::optional<int> top_k = std::nullopt) const;
  // Rebuilds an existing index transactionally. A rejected recipe or failed
  // embedding leaves the previous recipe/index usable. Empty feature cap = 0
  // means unlimited, while tier identifiers remain algorithm capabilities.
  Status set_profile(const RuntimeProfile& profile);
  Result<Json> profile_inspection() const;
  std::size_t size() const noexcept;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace loom
