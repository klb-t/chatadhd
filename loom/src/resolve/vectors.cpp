// Vector layer (resolve.h): TF-IDF over normalizer keys (offline default)
// and a provider-backed (multimodal) embedding space with a content-hash +
// model-id cache. Deterministic.
#include <algorithm>
#include <cmath>
#include <map>

#include "loom/resolve.h"
#include "loom/selector.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::resolve {

double cosine(const SparseVec& a, const SparseVec& b) {
  const SparseVec& s = a.size() <= b.size() ? a : b;
  const SparseVec& l = a.size() <= b.size() ? b : a;
  double dot = 0, na = 0, nb = 0;
  for (const auto& [k, v] : s) {
    if (auto it = l.find(k); it != l.end()) dot += v * it->second;
  }
  for (const auto& [k, v] : a) na += v * v;
  for (const auto& [k, v] : b) nb += v * v;
  return na > 0 && nb > 0 ? dot / std::sqrt(na * nb) : 0.0;
}

namespace {

void l2(SparseVec& v) {
  double n = 0;
  for (const auto& [k, x] : v) n += x * x;
  if (n <= 0) return;
  n = std::sqrt(n);
  for (auto& [k, x] : v) x /= n;
}

class TfidfSpace : public VectorSpace {
 public:
  explicit TfidfSpace(std::shared_ptr<const kb::Pack> pack) : pack_(std::move(pack)), norm_(*pack_) {}
  std::string method() const override { return "tfidf"; }
  std::vector<std::string> modalities() const override { return {"text"}; }
  Status fit(const std::vector<EmbedInput>& corpus) override {
    df_.clear();
    n_ = 0;
    for (const auto& in : corpus) {
      if (in.modality != "text") continue;
      ++n_;
      std::map<std::string, int> tf;
      terms(in.text, tf);
      for (const auto& [k, c] : tf) ++df_[k];
    }
    return {};
  }
  Result<std::vector<SparseVec>> vectors(const std::vector<EmbedInput>& inputs) override {
    std::vector<SparseVec> out;
    out.reserve(inputs.size());
    for (const auto& in : inputs) {
      SparseVec v;
      if (in.modality == "text") {
        std::map<std::string, int> tf;
        terms(in.text, tf);
        for (const auto& [k, c] : tf) {
          auto it = df_.find(k);
          double df = it == df_.end() ? 0.0 : it->second;
          v[k] = c * (std::log((1.0 + n_) / (1.0 + df)) + 1.0);
        }
        l2(v);
      }
      out.push_back(std::move(v));
    }
    return out;
  }

 private:
  std::shared_ptr<const kb::Pack> pack_;
  kb::Normalizer norm_;
  std::map<std::string, int> df_;
  int n_ = 0;
  // Glossary-mapped phrase keys (PL and EN terms share one key), per
  // sentence so a glossary phrase never spans two of them.
  void terms(std::string_view text, std::map<std::string, int>& tf) const {
    std::size_t pos = 0;
    while (pos <= text.size()) {
      std::size_t e = text.find_first_of(".\n!?;", pos);
      if (e == std::string_view::npos) e = text.size();
      std::string key = norm_.phrase_key(text.substr(pos, e - pos), true);
      std::size_t s = 0;
      while (s < key.size()) {
        std::size_t sp = key.find(' ', s);
        if (sp == std::string::npos) sp = key.size();
        std::string k = key.substr(s, sp - s);
        if (utf8::length(k) >= 3) ++tf[k];
        s = sp + 1;
      }
      if (e >= text.size()) break;
      pos = e + 1;
    }
  }
};

class TextProviderEmbedder : public MultimodalEmbedder {
 public:
  explicit TextProviderEmbedder(std::shared_ptr<EmbeddingProvider> p) : p_(std::move(p)) {}
  std::string model_id() const override { return p_->model_id(); }
  std::vector<std::string> modalities() const override { return {"text"}; }
  Result<std::vector<std::vector<float>>> embed(const std::vector<EmbedInput>& inputs) override {
    std::vector<std::string> texts;
    for (const auto& in : inputs) texts.push_back(in.text);
    return p_->embed(texts);
  }

 private:
  std::shared_ptr<EmbeddingProvider> p_;
};

class EmbeddingSpace : public VectorSpace {
 public:
  EmbeddingSpace(std::shared_ptr<MultimodalEmbedder> e, std::shared_ptr<EmbeddingCache> c)
      : e_(std::move(e)), cache_(c ? std::move(c) : std::make_shared<MemoryEmbeddingCache>()) {}
  std::string method() const override { return "embedding:" + e_->model_id(); }
  std::vector<std::string> modalities() const override { return e_->modalities(); }
  Status fit(const std::vector<EmbedInput>&) override { return {}; }
  Result<std::vector<SparseVec>> vectors(const std::vector<EmbedInput>& inputs) override {
    const auto mods = e_->modalities();
    const std::string model = e_->model_id();
    std::vector<SparseVec> out(inputs.size());
    std::vector<std::size_t> miss;
    std::vector<EmbedInput> todo;
    std::vector<std::string> hashes(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      const auto& in = inputs[i];
      if (std::find(mods.begin(), mods.end(), in.modality) == mods.end()) continue;  // unsupported: empty vector
      hashes[i] = in.content_hash.empty() ? Sha256::hex(in.modality + '\x1f' + in.text + '\x1f' + in.blob) : in.content_hash;
      if (auto v = cache_->get(hashes[i], model)) {
        out[i] = dense(*v);
      } else {
        miss.push_back(i);
        todo.push_back(in);
      }
    }
    if (!todo.empty()) {
      LOOM_TRY_ASSIGN(auto vs, e_->embed(todo));
      if (vs.size() != todo.size()) return Error(Errc::Unavailable, "embedder returned a wrong number of vectors");
      for (std::size_t k = 0; k < miss.size(); ++k) {
        out[miss[k]] = dense(vs[k]);
        cache_->put(hashes[miss[k]], model, std::move(vs[k]));
      }
    }
    return out;
  }

 private:
  std::shared_ptr<MultimodalEmbedder> e_;
  std::shared_ptr<EmbeddingCache> cache_;
  static SparseVec dense(const std::vector<float>& v) {
    SparseVec s;
    for (std::size_t i = 0; i < v.size(); ++i) {
      if (v[i] != 0.0f) s["#" + std::to_string(i)] = v[i];
    }
    l2(s);
    return s;
  }
};

std::string cache_key(std::string_view hash, std::string_view model) {
  return std::string(model) + '\x1f' + std::string(hash);
}

}  // namespace

std::unique_ptr<VectorSpace> make_tfidf_space(std::shared_ptr<const kb::Pack> pack) {
  return std::make_unique<TfidfSpace>(std::move(pack));
}

std::shared_ptr<MultimodalEmbedder> embedder_from_text_provider(std::shared_ptr<EmbeddingProvider> provider) {
  if (!provider) return nullptr;
  return std::make_shared<TextProviderEmbedder>(std::move(provider));
}

std::unique_ptr<VectorSpace> make_embedding_space(std::shared_ptr<MultimodalEmbedder> embedder,
                                                  std::shared_ptr<EmbeddingCache> cache) {
  if (!embedder) return nullptr;
  return std::make_unique<EmbeddingSpace>(std::move(embedder), std::move(cache));
}

std::optional<std::vector<float>> MemoryEmbeddingCache::get(std::string_view hash, std::string_view model) const {
  auto it = m_.find(cache_key(hash, model));
  if (it == m_.end()) return std::nullopt;
  return it->second;
}
void MemoryEmbeddingCache::put(std::string_view hash, std::string_view model, std::vector<float> v) {
  m_[cache_key(hash, model)] = std::move(v);
}
Json MemoryEmbeddingCache::to_json() const {
  Json j = Json::object();
  for (const auto& [k, v] : m_) j[k] = v;
  return j;
}
MemoryEmbeddingCache MemoryEmbeddingCache::from_json(const Json& j) {
  MemoryEmbeddingCache c;
  if (!j.is_object()) return c;
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (!it.value().is_array()) continue;
    std::vector<float> v;
    for (const auto& x : it.value()) v.push_back(x.is_number() ? x.get<float>() : 0.0f);
    c.m_[it.key()] = std::move(v);
  }
  return c;
}

}  // namespace loom::resolve
