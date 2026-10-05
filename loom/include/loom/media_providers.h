// loom/media_providers.h — port of engine/providers.py (ASR/OCR).
//                                                           [OWNER: wave 2 crypto/media/github]
// Providers (same endpoints, parameters and parsing as Python):
//   GroqAsr         POST https://api.groq.com/openai/v1/audio/transcriptions
//                   multipart file "audio.<fmt>" (audio/<fmt>), model
//                   (default whisper-large-v3-turbo), response_format
//                   verbose_json, language?; Bearer; timeout 60; confidence
//                   0.9; alternatives = first 5 segments {text, confidence:
//                   avg_logprob, start, end}; ext map mp3 wav m4a->mp4 ogg webm
//   GoogleSpeechAsr POST https://speech.googleapis.com/v1/speech:recognize?key=
//                   JSON config {encoding (wav LINEAR16, mp3 MP3, ogg OGG_OPUS,
//                   flac FLAC, webm WEBM_OPUS; default LINEAR16), languageCode
//                   (default en-US), maxAlternatives 5, enableWordConfidence,
//                   enableWordTimeOffsets}, audio.content base64
//   OcrSpace        POST https://api.ocr.space/parse/image form {apikey,
//                   base64Image "data:image/<fmt>;base64,...", language
//                   (en->eng, pl->pol, ...; default eng), OCREngine 2, isTable
//                   True, scale True}; IsErroredOnProcessing -> error; lines =
//                   non-empty stripped lines; confidence rules as Python
// ProviderManager: providers configured from secrets groq_api_key,
//   google_speech_api_key, ocr_space_api_key; transcribe() tries groq then
//   google unless a provider is named; ocr() tries providers in insertion
//   order. Errors: none configured -> Errc::Unavailable ("No ASR providers
//   configured. Add groq_api_key or google_speech_api_key to secrets."),
//   unknown provider -> Errc::NotFound, all failed -> Errc::Unavailable
//   ("All ASR providers failed"), HTTP/transport -> Errc::Http/Network.
#pragma once

#include <filesystem>
#include <map>
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

class Secrets;
namespace net {
class HttpTransport;
}

struct AsrResult {
  std::string text;
  double confidence = 0.0;
  Json alternatives = Json::array();  // [{text, confidence, ...}]
  std::optional<std::string> language;
  std::optional<double> duration;
  Json to_json() const;
};

struct OcrResult {
  std::string text;
  double confidence = 0.0;
  std::vector<std::string> lines;
  std::optional<std::string> language;
  Json to_json() const;
};

class AsrProvider {
 public:
  virtual ~AsrProvider() = default;
  virtual std::string name() const = 0;
  virtual Result<AsrResult> transcribe_bytes(std::string_view audio, std::string_view format,
                                             const std::optional<std::string>& language) = 0;
  // Reads the file; format from the extension (provider-specific mapping).
  virtual Result<AsrResult> transcribe(const std::filesystem::path& audio_path,
                                       const std::optional<std::string>& language) = 0;
};

class OcrProvider {
 public:
  virtual ~OcrProvider() = default;
  virtual std::string name() const = 0;
  virtual Result<OcrResult> recognize_bytes(std::string_view image, std::string_view format,
                                            const std::optional<std::string>& language) = 0;
  virtual Result<OcrResult> recognize(const std::filesystem::path& image_path,
                                      const std::optional<std::string>& language) = 0;
};

class GroqAsr final : public AsrProvider {
 public:
  GroqAsr(std::string api_key, net::HttpTransport& http, std::optional<std::string> model = {},
          std::optional<RuntimeProfile> profile = {}, std::string profile_id = "");
  std::string name() const override;
  Result<AsrResult> transcribe_bytes(std::string_view audio, std::string_view format,
                                     const std::optional<std::string>& language) override;
  Result<AsrResult> transcribe(const std::filesystem::path& audio_path,
                               const std::optional<std::string>& language) override;

 private:
  std::string key_;
  net::HttpTransport& http_;
  std::string model_;
  Result<RuntimeProfile> profile_;
  std::string profile_id_;
};

class GoogleSpeechAsr final : public AsrProvider {
 public:
  GoogleSpeechAsr(std::string api_key, net::HttpTransport& http,
                  std::optional<RuntimeProfile> profile = {}, std::string profile_id = "");
  std::string name() const override;
  Result<AsrResult> transcribe_bytes(std::string_view audio, std::string_view format,
                                     const std::optional<std::string>& language) override;
  Result<AsrResult> transcribe(const std::filesystem::path& audio_path,
                               const std::optional<std::string>& language) override;

 private:
  std::string key_;
  net::HttpTransport& http_;
  Result<RuntimeProfile> profile_;
  std::string profile_id_;
};

class OcrSpaceProvider final : public OcrProvider {
 public:
  OcrSpaceProvider(std::string api_key, net::HttpTransport& http, std::optional<int> engine = {},
                   std::optional<RuntimeProfile> profile = {}, std::string profile_id = "");
  std::string name() const override;
  Result<OcrResult> recognize_bytes(std::string_view image, std::string_view format,
                                    const std::optional<std::string>& language) override;
  Result<OcrResult> recognize(const std::filesystem::path& image_path,
                              const std::optional<std::string>& language) override;

 private:
  std::string key_;
  net::HttpTransport& http_;
  int engine_;
  Result<RuntimeProfile> profile_;
  std::string profile_id_;
};

// Python ProviderManager.
class MediaProviders {
 public:
  MediaProviders(const Secrets& secrets, net::HttpTransport& http);
  // Separate inspection keeps legacy result/status JSON byte-compatible.
  Result<Json> runtime_profile() const;
  // Re-reads the secrets (call after loom_set_secret). Python built the
  // provider list once in __init__.
  void refresh();
  // Platform/on-device providers (e.g. Android SpeechRecognizer) can be added.
  void add_asr_provider(std::shared_ptr<AsrProvider> p);
  void add_ocr_provider(std::shared_ptr<OcrProvider> p);

  std::vector<std::string> get_asr_providers() const;
  Result<AsrResult> transcribe(const std::filesystem::path& audio_path, const std::optional<std::string>& provider = {},
                               const std::optional<std::string>& language = {});
  Result<AsrResult> transcribe_bytes(std::string_view audio, std::string_view format = "wav",
                                     const std::optional<std::string>& provider = {},
                                     const std::optional<std::string>& language = {});
  std::vector<std::string> get_ocr_providers() const;
  Result<OcrResult> ocr(const std::filesystem::path& image_path, const std::optional<std::string>& provider = {},
                        const std::optional<std::string>& language = {});
  Result<OcrResult> ocr_bytes(std::string_view image, std::string_view format = "png",
                              const std::optional<std::string>& provider = {},
                              const std::optional<std::string>& language = {});
  // {"asr":{"available":[...],"configured":bool},"ocr":{...}}
  Json status() const;

 private:
  const Secrets& secrets_;
  net::HttpTransport& http_;
  Result<RuntimeProfile> profile_;
  mutable std::mutex mu_;
  std::vector<std::shared_ptr<AsrProvider>> asr_;  // preference order
  std::vector<std::shared_ptr<OcrProvider>> ocr_;
  std::vector<std::shared_ptr<AsrProvider>> extra_asr_;
  std::vector<std::shared_ptr<OcrProvider>> extra_ocr_;
};

}  // namespace loom
