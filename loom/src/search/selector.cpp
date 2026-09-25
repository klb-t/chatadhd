// OWNER: wave 2 semantic/graph. Stub.
#include "loom/selector.h"

#include "stub.h"

namespace loom {

Json SelectorHit::to_json() const { return Json{{"id", id}, {"text", text}, {"score", score}}; }

struct SelectorEngine::Impl {
  int tier = kTierKeyword;
  std::shared_ptr<EmbeddingProvider> embedder;
  std::vector<std::string> corpus;
  std::vector<std::string> ids;
};

SelectorEngine::SelectorEngine(std::optional<int> tier, std::shared_ptr<EmbeddingProvider> embedder)
    : impl_(std::make_unique<Impl>()) {
  // STUB: wave2 - tier selection (1 with embedder, else 2 native TF-IDF).
  impl_->tier = tier.value_or(kTierKeyword);
  impl_->embedder = std::move(embedder);
}
SelectorEngine::~SelectorEngine() = default;
SelectorEngine::SelectorEngine(SelectorEngine&&) noexcept = default;
SelectorEngine& SelectorEngine::operator=(SelectorEngine&&) noexcept = default;

int SelectorEngine::tier() const noexcept { return impl_ ? impl_->tier : kTierKeyword; }

Status SelectorEngine::index(std::vector<std::string> texts, std::vector<std::string> ids) {
  impl_->corpus = std::move(texts);  // STUB: wave2 (no index built)
  impl_->ids = std::move(ids);
  return {};
}

std::vector<SelectorHit> SelectorEngine::search(std::string_view, int) const {
  return {};  // STUB: wave2
}

std::size_t SelectorEngine::size() const noexcept { return impl_ ? impl_->corpus.size() : 0; }

}  // namespace loom
