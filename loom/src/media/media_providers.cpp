// Port of engine/providers.py: ASR (Groq Whisper, Google Speech) and OCR
// (ocr.space) providers behind a common ProviderManager-equivalent.
#include "loom/media_providers.h"
#include "loom/port_compose.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "media/media_call.h"

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

Result<RuntimeProfile> media_profile(const std::optional<RuntimeProfile>& supplied) {
  if (supplied && supplied->domain() != "media") return Error(Errc::InvalidArgument, "expected media runtime profile");
  LOOM_TRY_ASSIGN(auto builtin, RuntimeProfile::builtin("media"));
  return supplied ? builtin.with_values(supplied->values()) : Result<RuntimeProfile>(std::move(builtin));
}

std::string selected_id(const Result<RuntimeProfile>& profile, std::string id, std::string_view adapter) {
  if (!id.empty() || !profile) return id;
  return profile->values().at("defaults_by_adapter").at(std::string(adapter)).get<std::string>();
}

Result<Json> provider_settings(const Result<RuntimeProfile>& profile, const std::string& id,
                               std::string_view adapter, std::initializer_list<const char*> required) {
  LOOM_TRY(profile);
  const auto& providers = profile->values().at("providers");
  if (!providers.contains(id)) return Error(Errc::InvalidArgument, "unknown media descriptor: " + id);
  const auto& settings = providers.at(id);
  if (json::get_string(settings, "adapter") != adapter)
    return Error(Errc::InvalidArgument, "media descriptor uses a different adapter: " + id);
  for (const char* field : required)
    if (!settings.contains(field)) return Error(Errc::InvalidArgument, "media descriptor missing field: " + std::string(field));
  return settings;
}

std::string descriptor_name(const Result<RuntimeProfile>& profile, const std::string& id) {
  if (!profile) return "";
  const auto& providers = profile->values().at("providers");
  return providers.contains(id) ? json::get_string(providers.at(id), "name") : "";
}

net::Headers descriptor_headers(const Json& settings) {
  net::Headers out;
  for (auto it = settings.at("headers").begin(); it != settings.at("headers").end(); ++it)
    out.push_back({it.key(), it.value().get<std::string>()});
  return out;
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

GroqAsr::GroqAsr(std::string api_key, net::HttpTransport& http, OptStr model,
                   std::optional<RuntimeProfile> profile, std::string profile_id,
                   std::shared_ptr<MediaCallContext> call)
    : key_(std::move(api_key)), http_(http), call_(std::move(call)), profile_(media_profile(profile)),
      profile_id_(selected_id(profile_, std::move(profile_id), "groq_whisper")) {
  if (model) model_ = *model;
  else if (profile_ && profile_->values().at("providers").contains(profile_id_))
    model_ = json::get_string(profile_->values().at("providers").at(profile_id_), "model");
}

std::string GroqAsr::name() const { return descriptor_name(profile_, profile_id_); }

Result<AsrResult> GroqAsr::transcribe_bytes(std::string_view audio, std::string_view format, const OptStr& language) {
  LOOM_TRY_ASSIGN(auto settings, provider_settings(profile_, profile_id_, "groq_whisper",
      {"format_map", "default_format", "model", "response_format", "max_segments", "placeholder_confidence"}));
  const auto& formats = settings.at("format_map");
  const std::string fmt = formats.contains(std::string(format)) ? formats.at(std::string(format)).get<std::string>()
                                                              : settings.at("default_format").get<std::string>();

  // The one model action: the adapter of the OpenAI transcription family builds the
  // multipart body (file, model, then these parameters in order) and the auth header.
  media_detail::WireCall call;
  call.action = "media.asr";
  call.manifest = "groq";
  call.adapter = "openai_transcriptions";
  call.endpoint = settings.at("endpoint").get<std::string>();
  call.credential = key_;
  call.headers = descriptor_headers(settings);
  call.model = model_;
  call.input.kind = "audio";
  call.input.mime = "audio/" + fmt;
  call.input.bytes = std::string(audio);
  call.parameters = Json::object();
  call.parameters["response_format"] = settings.at("response_format").get<std::string>();
  if (language && !language->empty()) call.parameters["language"] = *language;
  call.timeout_ms = settings.at("timeout_ms").get<int>();

  auto resp = media_detail::send(http_, call_, call, "Groq ASR error",
                                 profile_->values().at("error_body_chars").get<std::size_t>());
  if (!resp) return resp.error();
  auto j = resp->json();
  if (!j) return Error(Errc::Parse, "Groq ASR: invalid JSON response");

  AsrResult out;
  out.text = strip_ascii(json::get_string(*j, "text"));
  out.confidence = settings.at("placeholder_confidence").get<double>();
  if (const Json* segs = json::find(*j, "segments"); segs && segs->is_array()) {
    std::size_t n = 0;
    for (const auto& seg : *segs) {
      if (n++ >= settings.at("max_segments").get<std::size_t>()) break;
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

GoogleSpeechAsr::GoogleSpeechAsr(std::string api_key, net::HttpTransport& http,
                                   std::optional<RuntimeProfile> profile, std::string profile_id,
                                   std::shared_ptr<MediaCallContext> call)
    : key_(std::move(api_key)), http_(http), call_(std::move(call)), profile_(media_profile(profile)),
      profile_id_(selected_id(profile_, std::move(profile_id), "google_speech")) {}

std::string GoogleSpeechAsr::name() const { return descriptor_name(profile_, profile_id_); }

Result<AsrResult> GoogleSpeechAsr::transcribe_bytes(std::string_view audio, std::string_view format, const OptStr& language) {
  LOOM_TRY_ASSIGN(auto settings, provider_settings(profile_, profile_id_, "google_speech",
      {"encoding_map", "default_encoding", "default_language", "max_alternatives", "word_confidence", "word_time_offsets"}));
  const auto& encodings = settings.at("encoding_map");
  const std::string encoding = encodings.contains(std::string(format)) ? encodings.at(std::string(format)).get<std::string>()
                                                                    : settings.at("default_encoding").get<std::string>();

  media_detail::WireCall call;
  call.action = "media.asr";
  call.manifest = "google_speech";
  call.adapter = "google_speech_v1";
  call.endpoint = settings.at("endpoint").get<std::string>();
  call.credential = key_;
  call.headers = descriptor_headers(settings);
  call.model = descriptor_name(profile_, profile_id_);
  call.input.kind = "audio";
  call.input.mime = "audio/" + std::string(format);
  call.input.bytes = std::string(audio);
  call.parameters = Json{{"config",
                          Json{{"encoding", encoding},
                               {"languageCode", language && !language->empty() ? *language : settings.at("default_language").get<std::string>()},
                               {"maxAlternatives", settings.at("max_alternatives")},
                               {"enableWordConfidence", settings.at("word_confidence")},
                               {"enableWordTimeOffsets", settings.at("word_time_offsets")}}}};
  call.timeout_ms = settings.at("timeout_ms").get<int>();

  auto resp = media_detail::send(http_, call_, call, "Google Speech error",
                                 profile_->values().at("error_body_chars").get<std::size_t>());
  if (!resp) return resp.error();
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

OcrSpaceProvider::OcrSpaceProvider(std::string api_key, net::HttpTransport& http, std::optional<int> engine,
                                     std::optional<RuntimeProfile> profile, std::string profile_id,
                                     std::shared_ptr<MediaCallContext> call)
    : key_(std::move(api_key)), http_(http), call_(std::move(call)), engine_(0), profile_(media_profile(profile)),
      profile_id_(selected_id(profile_, std::move(profile_id), "ocr_space")) {
  if (engine) engine_ = *engine;
  else if (profile_ && profile_->values().at("providers").contains(profile_id_))
    engine_ = profile_->values().at("providers").at(profile_id_).value("engine", 0);
}

std::string OcrSpaceProvider::name() const { return descriptor_name(profile_, profile_id_); }

Result<OcrResult> OcrSpaceProvider::recognize_bytes(std::string_view image, std::string_view format, const OptStr& language) {
  LOOM_TRY_ASSIGN(auto settings, provider_settings(profile_, profile_id_, "ocr_space",
      {"language_map", "default_language", "engine", "is_table", "scale", "overlay_placeholder_confidence", "text_placeholder_confidence"}));
  const auto& languages = settings.at("language_map");
  const std::string lang_code = language && languages.contains(*language) ? languages.at(*language).get<std::string>()
                                                                       : settings.at("default_language").get<std::string>();

  media_detail::WireCall call;
  call.action = "media.ocr";
  call.manifest = "ocr_space";
  call.adapter = "ocr_space_form";
  call.endpoint = settings.at("endpoint").get<std::string>();
  call.credential = key_;
  call.headers = descriptor_headers(settings);
  call.model = descriptor_name(profile_, profile_id_);
  call.input.kind = "image";
  call.input.mime = "image/" + std::string(format);
  call.input.bytes = std::string(image);
  call.parameters = Json{{"language", lang_code},
                         {"OCREngine", std::to_string(engine_)},
                         {"isTable", settings.at("is_table").get<std::string>()},
                         {"scale", settings.at("scale").get<std::string>()}};
  call.timeout_ms = settings.at("timeout_ms").get<int>();

  auto resp = media_detail::send(http_, call_, call, "OCR.space request error",
                                 profile_->values().at("error_body_chars").get<std::size_t>());
  if (!resp) return resp.error();
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
    out.confidence = settings.at("overlay_placeholder_confidence").get<double>();
  } else {
    out.confidence = !out.text.empty() ? settings.at("text_placeholder_confidence").get<double>() : 0.0;
  }
  return out;
}

Result<OcrResult> OcrSpaceProvider::recognize(const fs::path& image_path, const OptStr& language) {
  auto data = fsutil::read_file(image_path);
  if (!data) return data.error();
  return recognize_bytes(*data, lower_ext(image_path), language);
}

// ── MediaProviders ───────────────────────────────────────────────────

// ── Adapter registries ───────────────────────────────────────────────

AsrAdapterRegistry& asr_adapters() {
  static AsrAdapterRegistry r = [] {
    AsrAdapterRegistry x("adapter", "ASR adapter");
    x.add({"groq_whisper", "implemented",
           [](const std::string& id, const Json&, const MediaAdapterEnv& e) -> Result<std::unique_ptr<AsrProvider>> {
             return std::unique_ptr<AsrProvider>(std::make_unique<GroqAsr>(e.key, *e.http, OptStr{}, *e.profile, id, e.call));
           }});
    x.add({"google_speech", "implemented",
           [](const std::string& id, const Json&, const MediaAdapterEnv& e) -> Result<std::unique_ptr<AsrProvider>> {
             return std::unique_ptr<AsrProvider>(std::make_unique<GoogleSpeechAsr>(e.key, *e.http, *e.profile, id, e.call));
           }});
    return x;
  }();
  return r;
}

OcrAdapterRegistry& ocr_adapters() {
  static OcrAdapterRegistry r = [] {
    OcrAdapterRegistry x("adapter", "OCR adapter");
    x.add({"ocr_space", "implemented",
           [](const std::string& id, const Json&, const MediaAdapterEnv& e) -> Result<std::unique_ptr<OcrProvider>> {
             return std::unique_ptr<OcrProvider>(
                 std::make_unique<OcrSpaceProvider>(e.key, *e.http, std::optional<int>{}, *e.profile, id, e.call));
           }});
    return x;
  }();
  return r;
}

// ── MediaProviders ───────────────────────────────────────────────────

MediaProviders::MediaProviders(const Secrets& secrets, net::HttpTransport& http)
    : secrets_(secrets), http_(http), profile_(RuntimeProfile::load("media", secrets.path().parent_path())),
      call_(std::make_shared<MediaCallContext>()) {
  call_->config_path = secrets.path().parent_path() / "config.json";
  refresh();
}

void MediaProviders::set_provenance_sink(std::function<void(const Json&)> sink) {
  std::lock_guard lk(call_->mu);
  call_->on_provenance = std::move(sink);
}

Result<Json> MediaProviders::runtime_profile() const {
  std::lock_guard lk(mu_);
  LOOM_TRY(profile_);
  return profile_->inspection();
}

namespace {
// One descriptor of an order list -> provider (nullptr: skipped, no key stored).
template <class Port, class Registry>
Result<std::shared_ptr<Port>> instantiate(const Registry& registry, const Json& providers, const std::string& id,
                                          const char* what, const Secrets& secrets, MediaAdapterEnv env) {
  if (!providers.contains(id)) return Error(Errc::InvalidArgument, std::string("unknown ") + what + " descriptor: " + id);
  const auto& p = providers.at(id);
  const std::string adapter = json::get_string(p, "adapter");
  if (!registry.contains(adapter)) return Error(Errc::Unsupported, registry.unknown_message(adapter, id));
  env.key = secrets.get_string(p.at("secret_key").get<std::string>());
  if (env.key.empty()) return std::shared_ptr<Port>();
  LOOM_TRY_ASSIGN(auto made, registry.create_named(adapter, id, p, env));
  return std::shared_ptr<Port>(std::move(made));
}
}  // namespace

void MediaProviders::refresh() {
  auto profile = RuntimeProfile::load("media", secrets_.path().parent_path());
  std::vector<std::shared_ptr<AsrProvider>> asr;
  std::vector<std::shared_ptr<OcrProvider>> ocr;
  if (profile) {
    MediaAdapterEnv env;
    env.http = &http_;
    env.profile = &*profile;
    env.call = call_;
    const auto& values = profile->values();
    const auto& providers = values.at("providers");
    Status st = ok_status();
    for (const auto& item : values.at("asr_order")) {
      auto made = instantiate<AsrProvider>(asr_adapters(), providers, item.get<std::string>(), "ASR", secrets_, env);
      if (!made) { st = made.error(); break; }
      if (*made) asr.push_back(*made);
    }
    if (st) for (const auto& item : values.at("ocr_order")) {
      auto made = instantiate<OcrProvider>(ocr_adapters(), providers, item.get<std::string>(), "OCR", secrets_, env);
      if (!made) { st = made.error(); break; }
      if (*made) ocr.push_back(*made);
    }
    if (!st) profile = st.error();
  }
  if (!profile) { asr.clear(); ocr.clear(); }
  std::lock_guard lk(mu_);
  profile_ = std::move(profile);
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
  std::vector<std::shared_ptr<Provider>> order(primary);
  order.insert(order.end(), extra.begin(), extra.end());
  return compose::fallback<ResultT>(
      order, [&](const std::shared_ptr<Provider>& p) -> std::optional<Result<ResultT>> { return call(*p); },
      Error(Errc::Unavailable, std::string(all_failed_msg)),
      [](const std::shared_ptr<Provider>& p, const Error& e, Error&) {
        log::warn(kLog, "provider {} failed: {}", p->name(), e.message);
      });
}
}  // namespace

Result<AsrResult> MediaProviders::transcribe(const fs::path& audio_path, const OptStr& provider, const OptStr& language) {
  std::vector<std::shared_ptr<AsrProvider>> primary, extra;
  {
    std::lock_guard lk(mu_);
    LOOM_TRY(profile_);
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
    LOOM_TRY(profile_);
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
    LOOM_TRY(profile_);
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
    LOOM_TRY(profile_);
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
