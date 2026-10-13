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
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/adapter_registry.h"
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

// Where the calls of the media providers are admitted and recorded.  Every
// ASR / OCR request is the one model action (loom/use_model.h): the usage guard
// admits it before a byte leaves (x10 growth asks the owner) and its
// "loom.model_use/1" record goes to `on_provenance`.
//   config_path   settings + ledger directory of the data dir; empty (a provider
//                 made by hand, e.g. in a test): no ledger exists, the call is
//                 not guarded.
struct MediaCallContext {
  std::filesystem::path config_path;
  std::mutex mu;  // guards on_provenance
  std::function<void(const Json&)> on_provenance;
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
          std::optional<RuntimeProfile> profile = {}, std::string profile_id = "",
          std::shared_ptr<MediaCallContext> call = nullptr);
  std::string name() const override;
  Result<AsrResult> transcribe_bytes(std::string_view audio, std::string_view format,
                                     const std::optional<std::string>& language) override;
  Result<AsrResult> transcribe(const std::filesystem::path& audio_path,
                               const std::optional<std::string>& language) override;

 private:
  std::string key_;
  net::HttpTransport& http_;
  std::shared_ptr<MediaCallContext> call_;
  std::string model_;
  Result<RuntimeProfile> profile_;
  std::string profile_id_;
};

class GoogleSpeechAsr final : public AsrProvider {
 public:
  GoogleSpeechAsr(std::string api_key, net::HttpTransport& http,
                  std::optional<RuntimeProfile> profile = {}, std::string profile_id = "",
                  std::shared_ptr<MediaCallContext> call = nullptr);
  std::string name() const override;
  Result<AsrResult> transcribe_bytes(std::string_view audio, std::string_view format,
                                     const std::optional<std::string>& language) override;
  Result<AsrResult> transcribe(const std::filesystem::path& audio_path,
                               const std::optional<std::string>& language) override;

 private:
  std::string key_;
  net::HttpTransport& http_;
  std::shared_ptr<MediaCallContext> call_;
  Result<RuntimeProfile> profile_;
  std::string profile_id_;
};

class OcrSpaceProvider final : public OcrProvider {
 public:
  OcrSpaceProvider(std::string api_key, net::HttpTransport& http, std::optional<int> engine = {},
                   std::optional<RuntimeProfile> profile = {}, std::string profile_id = "",
                   std::shared_ptr<MediaCallContext> call = nullptr);
  std::string name() const override;
  Result<OcrResult> recognize_bytes(std::string_view image, std::string_view format,
                                    const std::optional<std::string>& language) override;
  Result<OcrResult> recognize(const std::filesystem::path& image_path,
                              const std::optional<std::string>& language) override;

 private:
  std::string key_;
  net::HttpTransport& http_;
  std::shared_ptr<MediaCallContext> call_;
  int engine_;
  Result<RuntimeProfile> profile_;
  std::string profile_id_;
};

// ── Adapter registries ───────────────────────────────────────────────
// A media descriptor ("providers" of the media profile) names its adapter in the
// key "adapter"; these registries turn the name into a provider.  A new ASR / OCR
// service is one add() (plus a descriptor), no edit to MediaProviders.  Register
// before MediaProviders::refresh(); the registries are not locked.
struct MediaAdapterEnv {
  std::string key;                           // the credential of the descriptor's secret
  net::HttpTransport* http = nullptr;
  const RuntimeProfile* profile = nullptr;   // effective media profile
  std::shared_ptr<MediaCallContext> call;    // admission + provenance of the calls
};
using AsrAdapterRegistry = AdapterRegistry<AsrProvider, MediaAdapterEnv>;
using OcrAdapterRegistry = AdapterRegistry<OcrProvider, MediaAdapterEnv>;
AsrAdapterRegistry& asr_adapters();  // builtin: groq_whisper, google_speech
OcrAdapterRegistry& ocr_adapters();  // builtin: ocr_space

// Python ProviderManager.
class MediaProviders {
 public:
  MediaProviders(const Secrets& secrets, net::HttpTransport& http);
  // Separate inspection keeps legacy result/status JSON byte-compatible.
  Result<Json> runtime_profile() const;
  // Re-reads the secrets (call after loom_set_secret). Python built the
  // provider list once in __init__.
  void refresh();
  // Receives the "loom.model_use/1" record (no secrets) of every ASR / OCR call.
  void set_provenance_sink(std::function<void(const Json&)> sink);
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
  std::shared_ptr<MediaCallContext> call_;
  mutable std::mutex mu_;
  std::vector<std::shared_ptr<AsrProvider>> asr_;  // preference order
  std::vector<std::shared_ptr<OcrProvider>> ocr_;
  std::vector<std::shared_ptr<AsrProvider>> extra_asr_;
  std::vector<std::shared_ptr<OcrProvider>> extra_ocr_;
};

}  // namespace loom
