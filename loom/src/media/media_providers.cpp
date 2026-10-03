// Port of engine/providers.py: ASR (Groq Whisper, Google Speech) and OCR
// (ocr.space) providers behind a common ProviderManager-equivalent.
#include "loom/media_providers.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"

namespace loom {

namespace fs = std::filesystem;
using OptStr = std::optional<std::string>;

namespace {
constexpr std::string_view kLog = "loom.media";

std::string lower_ext(const fs::path& p) {
  std::string ext = p.extension().string();
  if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

// Python: `line.strip()` then keep non-empty lines.
std::string strip_ascii(std::string_view s) {
  std::size_t b = 0, e = s.size();
  auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; };
  while (b < e && is_ws(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && is_ws(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

std::vector<std::string> split_lines(std::string_view text) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      out.push_back(std::string(text.substr(start, i - start)));
      start = i + 1;
    }
  }
  return out;
}

std::string http_error_message(const net::HttpResponse& resp) {
  std::string msg = "HTTP " + std::to_string(resp.status);
  if (!resp.body.empty()) {
    msg += ": ";
    msg += resp.body.size() > 500 ? resp.body.substr(0, 500) : resp.body;
  }
  return msg;
}

}  // namespace

// ── Result JSON ───────────────────────────────────────────────────────

Json AsrResult::to_json() const {
  return Json{{"text", text},
              {"confidence", confidence},
              {"alternatives", alternatives},
              {"language", language ? Json(*language) : Json(nullptr)},
              {"duration", duration ? Json(*duration) : Json(nullptr)}};
}

Json OcrResult::to_json() const {
  return Json{{"text", text}, {"confidence", confidence}, {"lines", lines}, {"language", language ? Json(*language) : Json(nullptr)}};
}

// ── GroqAsr ──────────────────────────────────────────────────────────

GroqAsr::GroqAsr(std::string api_key, net::HttpTransport& http, std::string model)
    : key_(std::move(api_key)), http_(http), model_(std::move(model)) {}

Result<AsrResult> GroqAsr::transcribe_bytes(std::string_view audio, std::string_view format, const OptStr& language) {
  static const std::map<std::string, std::string, std::less<>> kFormatMap = {
      {"mp3", "mp3"}, {"wav", "wav"}, {"m4a", "mp4"}, {"ogg", "ogg"}, {"webm", "webm"}};
  std::string fmt;
  if (auto it = kFormatMap.find(std::string(format)); it != kFormatMap.end()) fmt = it->second;
  else fmt = "wav";

  std::vector<net::MultipartPart> parts;
  parts.push_back({"file", "audio." + fmt, "audio/" + fmt, std::string(audio)});
  parts.push_back({"model", "", "", model_});
  parts.push_back({"response_format", "", "", "verbose_json"});
  if (language && !language->empty()) parts.push_back({"language", "", "", *language});
  net::MultipartBody body = net::build_multipart(parts);

  net::HttpRequest req;
  req.method = "POST";
  req.url = "https://api.groq.com/openai/v1/audio/transcriptions";
  req.headers = {{"Authorization", "Bearer " + key_}, {"Content-Type", body.content_type}};
  req.body = std::move(body.body);
  req.timeout_ms = 60000;

  auto resp = http_.send(req);
  if (!resp) return Error(Errc::Network, "Groq ASR error: " + resp.error().message);
  if (!resp->ok()) {
    std::string msg = http_error_message(*resp);
    return Error(Errc::Http, "Groq ASR error: " + msg);
  }
  auto j = resp->json();
  if (!j) return Error(Errc::Parse, "Groq ASR: invalid JSON response");

  AsrResult out;
  out.text = strip_ascii(json::get_string(*j, "text"));
  out.confidence = 0.9;
  if (const Json* segs = json::find(*j, "segments"); segs && segs->is_array()) {
    std::size_t n = 0;
    for (const auto& seg : *segs) {
      if (n++ >= 5) break;
      Json alt{{"text", strip_ascii(json::get_string(seg, "text"))},
               {"confidence", json::get_number(seg, "avg_logprob", 0.0)},
               {"start", seg.contains("start") ? seg.at("start") : Json(nullptr)},
               {"end", seg.contains("end") ? seg.at("end") : Json(nullptr)}};
      out.alternatives.push_back(std::move(alt));
    }
  }
  if (auto lang = json::get_opt_string(*j, "language")) out.language = *lang;
  if (const Json* d = json::find(*j, "duration"); d && d->is_number()) out.duration = d->get<double>();
  return out;
}

Result<AsrResult> GroqAsr::transcribe(const fs::path& audio_path, const OptStr& language) {
  auto data = fsutil::read_file(audio_path);
  if (!data) return data.error();
  return transcribe_bytes(*data, lower_ext(audio_path), language);
}

// ── GoogleSpeechAsr ──────────────────────────────────────────────────

GoogleSpeechAsr::GoogleSpeechAsr(std::string api_key, net::HttpTransport& http) : key_(std::move(api_key)), http_(http) {}

Result<AsrResult> GoogleSpeechAsr::transcribe_bytes(std::string_view audio, std::string_view format, const OptStr& language) {
  static const std::map<std::string, std::string, std::less<>> kEncodingMap = {
      {"wav", "LINEAR16"}, {"mp3", "MP3"}, {"ogg", "OGG_OPUS"}, {"flac", "FLAC"}, {"webm", "WEBM_OPUS"}};
  std::string encoding = "LINEAR16";
  if (auto it = kEncodingMap.find(std::string(format)); it != kEncodingMap.end()) encoding = it->second;

  Json payload{{"config",
               Json{{"encoding", encoding},
                    {"languageCode", language && !language->empty() ? *language : std::string("en-US")},
                    {"maxAlternatives", 5},
                    {"enableWordConfidence", true},
                    {"enableWordTimeOffsets", true}}},
              {"audio", Json{{"content", base64::encode(audio)}}}};

  net::HttpRequest req;
  req.method = "POST";
  req.url = net::with_query("https://speech.googleapis.com/v1/speech:recognize", {{"key", key_}});
  req.headers = {{"Content-Type", "application/json"}};
  req.body = json::dump(payload);
  req.timeout_ms = 60000;

  auto resp = http_.send(req);
  if (!resp) return Error(Errc::Network, "Google Speech error: " + resp.error().message);
  if (!resp->ok()) {
    std::string msg = http_error_message(*resp);
    return Error(Errc::Http, "Google Speech error: " + msg);
  }
  auto j = resp->json();
  if (!j) return Error(Errc::Parse, "Google Speech: invalid JSON response");

  AsrResult out;
  out.language = language;
  if (const Json* results = json::find(*j, "results"); results && results->is_array() && !results->empty()) {
    const Json* alts = json::find((*results)[0], "alternatives");
    if (alts && alts->is_array()) {
      int rank = 0;
      for (const auto& alt : *alts) {
        std::string text = json::get_string(alt, "transcript");
        double conf = json::get_number(alt, "confidence", 0.0);
        out.alternatives.push_back(Json{{"text", text}, {"confidence", conf}, {"rank", ++rank}});
        if (rank == 1) {
          out.text = text;
          out.confidence = conf;
        }
      }
    }
  }
  return out;
}

Result<AsrResult> GoogleSpeechAsr::transcribe(const fs::path& audio_path, const OptStr& language) {
  auto data = fsutil::read_file(audio_path);
  if (!data) return data.error();
  return transcribe_bytes(*data, lower_ext(audio_path), language);
}

// ── OcrSpaceProvider ─────────────────────────────────────────────────

OcrSpaceProvider::OcrSpaceProvider(std::string api_key, net::HttpTransport& http, int engine)
    : key_(std::move(api_key)), http_(http), engine_(engine) {}

Result<OcrResult> OcrSpaceProvider::recognize_bytes(std::string_view image, std::string_view format, const OptStr& language) {
  static const std::map<std::string, std::string, std::less<>> kLangMap = {
      {"en", "eng"}, {"pl", "pol"}, {"de", "ger"}, {"fr", "fre"}, {"es", "spa"}, {"it", "ita"},
      {"nl", "dut"}, {"pt", "por"}, {"ru", "rus"}, {"zh", "chs"}, {"ja", "jpn"}, {"ko", "kor"}};
  std::string lang_code = "eng";
  if (language) {
    if (auto it = kLangMap.find(*language); it != kLangMap.end()) lang_code = it->second;
  }

  std::string b64 = base64::encode(image);
  std::vector<std::pair<std::string, std::string>> fields = {
      {"apikey", key_},
      {"base64Image", "data:image/" + std::string(format) + ";base64," + b64},
      {"language", lang_code},
      {"OCREngine", std::to_string(engine_)},
      {"isTable", "True"},
      {"scale", "True"},
  };

  net::HttpRequest req;
  req.method = "POST";
  req.url = "https://api.ocr.space/parse/image";
  req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
  req.body = net::form_urlencode(fields);
  req.timeout_ms = 60000;

  auto resp = http_.send(req);
  if (!resp) return Error(Errc::Network, "OCR.space request error: " + resp.error().message);
  if (!resp->ok()) {
    std::string msg = http_error_message(*resp);
    return Error(Errc::Http, "OCR.space request error: " + msg);
  }
  auto j = resp->json();
  if (!j) return Error(Errc::Parse, "OCR.space: invalid JSON response");

  const Json* errored = json::find(*j, "IsErroredOnProcessing");
  if (errored && json::truthy(*errored)) {
    std::string err = "Unknown error";
    if (const Json* em = json::find(*j, "ErrorMessage")) {
      if (em->is_array() && !em->empty() && (*em)[0].is_string()) err = (*em)[0].get<std::string>();
      else if (em->is_string()) err = em->get<std::string>();
    }
    return Error(Errc::Http, "OCR.space error: " + err);
  }

  const Json* parsed = json::find(*j, "ParsedResults");
  if (!parsed || !parsed->is_array() || parsed->empty()) {
    OcrResult empty;
    empty.language = language;
    return empty;
  }
  const Json& first = (*parsed)[0];
  std::string text = json::get_string(first, "ParsedText");

  OcrResult out;
  for (auto& line : split_lines(text)) {
    std::string s = strip_ascii(line);
    if (!s.empty()) out.lines.push_back(std::move(s));
  }
  std::string joined;
  for (std::size_t i = 0; i < out.lines.size(); ++i) {
    if (i) joined += '\n';
    joined += out.lines[i];
  }
  out.text = joined;
  out.language = language;

  // Python averages a list of per-word 0.9 placeholders (ocr.space gives no
  // real per-word confidence) or falls back to 0.9 if the list is empty, so
  // the result is 0.9 whenever TextOverlay is present at all.
  const Json* overlay = json::find(first, "TextOverlay");
  if (overlay && json::truthy(*overlay)) {
    out.confidence = 0.9;
  } else {
    out.confidence = !out.text.empty() ? 0.85 : 0.0;
  }
  return out;
}

Result<OcrResult> OcrSpaceProvider::recognize(const fs::path& image_path, const OptStr& language) {
  auto data = fsutil::read_file(image_path);
  if (!data) return data.error();
  return recognize_bytes(*data, lower_ext(image_path), language);
}

// ── MediaProviders ───────────────────────────────────────────────────

MediaProviders::MediaProviders(const Secrets& secrets, net::HttpTransport& http) : secrets_(secrets), http_(http) { refresh(); }

void MediaProviders::refresh() {
  std::vector<std::shared_ptr<AsrProvider>> asr;
  std::vector<std::shared_ptr<OcrProvider>> ocr;

  if (std::string key = secrets_.get_string("groq_api_key"); !key.empty()) {
    asr.push_back(std::make_shared<GroqAsr>(key, http_));
    log::info(kLog, "Groq ASR initialized");
  }
  if (std::string key = secrets_.get_string("google_speech_api_key"); !key.empty()) {
    asr.push_back(std::make_shared<GoogleSpeechAsr>(key, http_));
    log::info(kLog, "Google Speech ASR initialized");
  }
  if (std::string key = secrets_.get_string("ocr_space_api_key"); !key.empty()) {
    ocr.push_back(std::make_shared<OcrSpaceProvider>(key, http_));
    log::info(kLog, "OCR.space initialized");
  }

  std::lock_guard lk(mu_);
  asr_ = std::move(asr);
  ocr_ = std::move(ocr);
}

void MediaProviders::add_asr_provider(std::shared_ptr<AsrProvider> p) {
  std::lock_guard lk(mu_);
  extra_asr_.push_back(std::move(p));
}

void MediaProviders::add_ocr_provider(std::shared_ptr<OcrProvider> p) {
  std::lock_guard lk(mu_);
  extra_ocr_.push_back(std::move(p));
}

std::vector<std::string> MediaProviders::get_asr_providers() const {
  std::lock_guard lk(mu_);
  std::vector<std::string> names;
  for (const auto& p : asr_) names.push_back(p->name());
  for (const auto& p : extra_asr_) names.push_back(p->name());
  return names;
}

std::vector<std::string> MediaProviders::get_ocr_providers() const {
  std::lock_guard lk(mu_);
  std::vector<std::string> names;
  for (const auto& p : ocr_) names.push_back(p->name());
  for (const auto& p : extra_ocr_) names.push_back(p->name());
  return names;
}

namespace {
template <class Provider, class ResultT, class Fn>
Result<ResultT> try_providers(const std::vector<std::shared_ptr<Provider>>& primary,
                              const std::vector<std::shared_ptr<Provider>>& extra, const OptStr& provider,
                              std::string_view unconfigured_msg, std::string_view all_failed_msg, Fn&& call) {
  if (primary.empty() && extra.empty()) return Error(Errc::Unavailable, std::string(unconfigured_msg));
  if (provider) {
    for (const auto& p : primary) {
      if (p->name() == *provider) return call(*p);
    }
    for (const auto& p : extra) {
      if (p->name() == *provider) return call(*p);
    }
    return Error(Errc::NotFound, "provider '" + *provider + "' not available");
  }
  for (const auto& p : primary) {
    auto r = call(*p);
    if (r) return r;
    log::warn(kLog, "provider {} failed: {}", p->name(), r.error().message);
  }
  for (const auto& p : extra) {
    auto r = call(*p);
    if (r) return r;
    log::warn(kLog, "provider {} failed: {}", p->name(), r.error().message);
  }
  return Error(Errc::Unavailable, std::string(all_failed_msg));
}
}  // namespace

Result<AsrResult> MediaProviders::transcribe(const fs::path& audio_path, const OptStr& provider, const OptStr& language) {
  std::vector<std::shared_ptr<AsrProvider>> primary, extra;
  {
    std::lock_guard lk(mu_);
    primary = asr_;
    extra = extra_asr_;
  }
  return try_providers<AsrProvider, AsrResult>(
      primary, extra, provider, "No ASR providers configured. Add groq_api_key or google_speech_api_key to secrets.",
      "All ASR providers failed", [&](AsrProvider& p) { return p.transcribe(audio_path, language); });
}

Result<AsrResult> MediaProviders::transcribe_bytes(std::string_view audio, std::string_view format,
                                                   const OptStr& provider, const OptStr& language) {
  std::vector<std::shared_ptr<AsrProvider>> primary, extra;
  {
    std::lock_guard lk(mu_);
    primary = asr_;
    extra = extra_asr_;
  }
  return try_providers<AsrProvider, AsrResult>(
      primary, extra, provider, "No ASR providers configured. Add groq_api_key or google_speech_api_key to secrets.",
      "All ASR providers failed", [&](AsrProvider& p) { return p.transcribe_bytes(audio, format, language); });
}

Result<OcrResult> MediaProviders::ocr(const fs::path& image_path, const OptStr& provider, const OptStr& language) {
  std::vector<std::shared_ptr<OcrProvider>> primary, extra;
  {
    std::lock_guard lk(mu_);
    primary = ocr_;
    extra = extra_ocr_;
  }
  return try_providers<OcrProvider, OcrResult>(primary, extra, provider,
                                               "No OCR providers configured. Add ocr_space_api_key to secrets.",
                                               "All OCR providers failed",
                                               [&](OcrProvider& p) { return p.recognize(image_path, language); });
}

Result<OcrResult> MediaProviders::ocr_bytes(std::string_view image, std::string_view format, const OptStr& provider,
                                            const OptStr& language) {
  std::vector<std::shared_ptr<OcrProvider>> primary, extra;
  {
    std::lock_guard lk(mu_);
    primary = ocr_;
    extra = extra_ocr_;
  }
  return try_providers<OcrProvider, OcrResult>(primary, extra, provider,
                                               "No OCR providers configured. Add ocr_space_api_key to secrets.",
                                               "All OCR providers failed",
                                               [&](OcrProvider& p) { return p.recognize_bytes(image, format, language); });
}

Json MediaProviders::status() const {
  return Json{{"asr", Json{{"available", get_asr_providers()}, {"configured", !get_asr_providers().empty()}}},
              {"ocr", Json{{"available", get_ocr_providers()}, {"configured", !get_ocr_providers().empty()}}}};
}

}  // namespace loom
