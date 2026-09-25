// OWNER: wave 2 crypto/media/github. Stub.
#include "loom/media_providers.h"

#include "stub.h"

namespace loom {

namespace fs = std::filesystem;
using OptStr = std::optional<std::string>;

Json AsrResult::to_json() const {
  return Json{{"text", text},
              {"confidence", confidence},
              {"alternatives", alternatives},
              {"language", language ? Json(*language) : Json(nullptr)},
              {"duration", duration ? Json(*duration) : Json(nullptr)}};
}

Json OcrResult::to_json() const {
  return Json{{"text", text},
              {"confidence", confidence},
              {"lines", lines},
              {"language", language ? Json(*language) : Json(nullptr)}};
}

GroqAsr::GroqAsr(std::string api_key, net::HttpTransport& http, std::string model)
    : key_(std::move(api_key)), http_(http), model_(std::move(model)) {}
Result<AsrResult> GroqAsr::transcribe_bytes(std::string_view, std::string_view, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("GroqAsr::transcribe_bytes");  // STUB: wave2
}
Result<AsrResult> GroqAsr::transcribe(const fs::path&, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("GroqAsr::transcribe");  // STUB: wave2
}

GoogleSpeechAsr::GoogleSpeechAsr(std::string api_key, net::HttpTransport& http) : key_(std::move(api_key)), http_(http) {}
Result<AsrResult> GoogleSpeechAsr::transcribe_bytes(std::string_view, std::string_view, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("GoogleSpeechAsr::transcribe_bytes");  // STUB: wave2
}
Result<AsrResult> GoogleSpeechAsr::transcribe(const fs::path&, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("GoogleSpeechAsr::transcribe");  // STUB: wave2
}

OcrSpaceProvider::OcrSpaceProvider(std::string api_key, net::HttpTransport& http, int engine)
    : key_(std::move(api_key)), http_(http), engine_(engine) {}
Result<OcrResult> OcrSpaceProvider::recognize_bytes(std::string_view, std::string_view, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("OcrSpaceProvider::recognize_bytes");  // STUB: wave2
}
Result<OcrResult> OcrSpaceProvider::recognize(const fs::path&, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("OcrSpaceProvider::recognize");  // STUB: wave2
}

MediaProviders::MediaProviders(const Secrets& secrets, net::HttpTransport& http) : secrets_(secrets), http_(http) {}
void MediaProviders::refresh() {}  // STUB: wave2
void MediaProviders::add_asr_provider(std::shared_ptr<AsrProvider> p) {
  std::lock_guard lk(mu_);
  extra_asr_.push_back(std::move(p));
}
void MediaProviders::add_ocr_provider(std::shared_ptr<OcrProvider> p) {
  std::lock_guard lk(mu_);
  extra_ocr_.push_back(std::move(p));
}
std::vector<std::string> MediaProviders::get_asr_providers() const { return {}; }  // STUB: wave2
Result<AsrResult> MediaProviders::transcribe(const fs::path&, const OptStr&, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("MediaProviders::transcribe");  // STUB: wave2
}
Result<AsrResult> MediaProviders::transcribe_bytes(std::string_view, std::string_view, const OptStr&, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("MediaProviders::transcribe_bytes");  // STUB: wave2
}
std::vector<std::string> MediaProviders::get_ocr_providers() const { return {}; }  // STUB: wave2
Result<OcrResult> MediaProviders::ocr(const fs::path&, const OptStr&, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("MediaProviders::ocr");  // STUB: wave2
}
Result<OcrResult> MediaProviders::ocr_bytes(std::string_view, std::string_view, const OptStr&, const OptStr&) {
  return LOOM_NOT_IMPLEMENTED("MediaProviders::ocr_bytes");  // STUB: wave2
}
Json MediaProviders::status() const {
  return Json{{"asr", Json{{"available", Json::array()}, {"configured", false}}},
              {"ocr", Json{{"available", Json::array()}, {"configured", false}}}};  // STUB: wave2
}

}  // namespace loom
