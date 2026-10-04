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

#include <memory>
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

class SelectorEngine {
 public:
  static constexpr int kTierEmbedding = 1;
  static constexpr int kTierTfIdf = 2;
  static constexpr int kTierKeyword = 3;

  // tier: nullopt = best available. Requesting tier 1 without an embedder
  // falls back to 2 (Python fell back when the model failed to load).
  // Optional recipe injection uses the supported selector descriptor. No
  // profile means the exact historical parameters from selector.pack.
  explicit SelectorEngine(std::optional<int> tier = std::nullopt, std::shared_ptr<EmbeddingProvider> embedder = nullptr,
                           std::optional<RuntimeProfile> profile = std::nullopt);
  ~SelectorEngine();
  SelectorEngine(SelectorEngine&&) noexcept;
  SelectorEngine& operator=(SelectorEngine&&) noexcept;

  int tier() const noexcept;
  // ids default to "0","1",... (Python). Embedding failures fall back to tier 2.
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
