#pragma once
#include "loom/media_providers.h"

namespace loom {
// Local process adapter, injected through the existing MediaProviders port.
// Settings are the local member of runtime/image_input.pack.
class LocalOcrProvider final : public OcrProvider {
 public:
  explicit LocalOcrProvider(Json settings) : settings_(std::move(settings)) {}
  std::string name() const override;
  bool runs_locally() const noexcept override { return true; }
  Result<OcrResult> recognize_bytes(std::string_view bytes, std::string_view format,
                                    const std::optional<std::string>& language) override;
  Result<OcrResult> recognize(const std::filesystem::path& path,
                             const std::optional<std::string>& language) override;
 private:
  Json settings_;
};
Result<OcrResult> decode_local_ocr(const Json& document, std::string_view source_hash);
}  // namespace loom
