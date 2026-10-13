// Wire shapes of the media API families, behind the one model action.  The
// adapters only know how a typed input part (audio / image bytes) and the
// request's ordered parameters become bytes; which provider, key, address and
// parameter values apply is data resolved by the caller and use_model.cpp.
#include "llm/media_adapters.h"

#include "loom/util/base64.h"

namespace loom::llm {

namespace {

constexpr int kDefaultTimeoutMs = 120000;

std::string join(const std::string& base, const std::string& path) {
  if (path.find("://") != std::string::npos) return path;
  if (path.empty()) return base;
  return base + (path.front() == '/' ? path : "/" + path);
}

// "audio/mp4" -> "mp4".
std::string subtype(std::string_view mime) {
  const auto slash = mime.find('/');
  return std::string(slash == std::string_view::npos ? mime : mime.substr(slash + 1));
}

const Part* first_part(const CallSpec& spec, std::string_view kind) {
  for (const auto& p : spec.parts)
    if (p.kind == kind) return &p;
  return nullptr;
}

// Shared by the three families: the address, the timeout and the response rules.
class MediaAdapter : public Adapter {
 public:
  MediaAdapter(std::string family, std::string input_kind, std::string default_path)
      : family_(std::move(family)), input_(std::move(input_kind)), path_(std::move(default_path)) {}
  std::string_view family() const noexcept override { return family_; }
  Capabilities capabilities() const override { return {{input_}, {"text"}, {}}; }
  std::string default_endpoint(Operation, const Json&) const override { return path_; }
  bool success(Operation, int http_status) const noexcept override { return http_status >= 200 && http_status < 300; }
  std::unique_ptr<StreamDecoder> stream_decoder(const StreamHooks&, const CancelToken*) const override { return nullptr; }

  Result<net::HttpRequest> build(const Route& route, Operation op, const CallSpec& spec) const override {
    if (op != Operation::Complete) return Error(Errc::Unsupported, "adapter " + family_ + " has no batch operations");
    const Part* in = first_part(spec, input_);
    if (!in) return Error(Errc::InvalidArgument, "adapter " + family_ + " needs a " + input_ + " input");
    net::HttpRequest req;
    req.method = "POST";
    req.url = join(route.base_url, route.endpoint.empty() ? path_ : route.endpoint);
    LOOM_TRY(fill(route, spec, *in, req));
    req.timeout_ms = spec.timeout_ms.value_or(kDefaultTimeoutMs);
    return req;
  }

 protected:
  virtual Status fill(const Route& route, const CallSpec& spec, const Part& in, net::HttpRequest& req) const = 0;
  static void add_defaults(const Route& route, net::HttpRequest& req) {
    for (const auto& h : route.default_headers) req.headers.push_back(h);
  }

 private:
  std::string family_, input_, path_;
};

class OpenAiTranscriptions final : public MediaAdapter {
 public:
  OpenAiTranscriptions() : MediaAdapter("openai_transcriptions", "audio", "/audio/transcriptions") {}

  Result<Parsed> parse(std::string_view body) const override {
    auto j = json::parse(body);
    if (!j || !j->is_object()) return Error(Errc::Parse, "transcription response is not a JSON object");
    Parsed out;
    out.text = json::get_string(*j, "text");
    return out;
  }

 protected:
  Status fill(const Route& route, const CallSpec& spec, const Part& in, net::HttpRequest& req) const override {
    const std::string fmt = subtype(in.mime);
    std::vector<net::MultipartPart> parts;
    parts.push_back({"file", "audio." + fmt, in.mime, in.bytes});
    parts.push_back({"model", "", "", spec.model});
    for (auto it = spec.parameters.begin(); it != spec.parameters.end(); ++it)
      parts.push_back({it.key(), "", "", it.value().is_string() ? it.value().get<std::string>() : json::dump(it.value())});
    net::MultipartBody body = net::build_multipart(parts);
    LOOM_TRY(apply_auth(route, req));
    req.headers.emplace_back("Content-Type", body.content_type);
    add_defaults(route, req);
    req.body = std::move(body.body);
    return ok_status();
  }
};

class GoogleSpeech final : public MediaAdapter {
 public:
  GoogleSpeech() : MediaAdapter("google_speech_v1", "audio", "/v1/speech:recognize") {}

  Result<Parsed> parse(std::string_view body) const override {
    auto j = json::parse(body);
    if (!j || !j->is_object()) return Error(Errc::Parse, "speech response is not a JSON object");
    Parsed out;
    const Json* results = json::find(*j, "results");
    if (results && results->is_array() && !results->empty()) {
      const Json* alts = json::find((*results)[0], "alternatives");
      if (alts && alts->is_array() && !alts->empty()) out.text = json::get_string((*alts)[0], "transcript");
    }
    return out;
  }

 protected:
  Status fill(const Route& route, const CallSpec& spec, const Part& in, net::HttpRequest& req) const override {
    Json payload = spec.parameters.is_object() ? spec.parameters : Json::object();
    payload["audio"] = Json{{"content", base64::encode(in.bytes)}};
    add_defaults(route, req);
    LOOM_TRY(apply_auth(route, req));  // the key rides in the query
    req.body = json::dump(payload);
    return ok_status();
  }
};

class OcrSpaceForm final : public MediaAdapter {
 public:
  OcrSpaceForm() : MediaAdapter("ocr_space_form", "image", "/parse/image") {}

  Result<Parsed> parse(std::string_view body) const override {
    auto j = json::parse(body);
    if (!j || !j->is_object()) return Error(Errc::Parse, "recognition response is not a JSON object");
    Parsed out;
    const Json* parsed = json::find(*j, "ParsedResults");
    if (parsed && parsed->is_array() && !parsed->empty()) out.text = json::get_string((*parsed)[0], "ParsedText");
    return out;
  }

 protected:
  Status fill(const Route& route, const CallSpec& spec, const Part& in, net::HttpRequest& req) const override {
    std::vector<std::pair<std::string, std::string>> fields;
    // The route's scheme "form:<field>" names the field the key travels in (first).
    if (route.auth_scheme.rfind("form:", 0) == 0 && !route.credential.empty())
      fields.emplace_back(route.auth_scheme.substr(5), route.credential);
    fields.emplace_back("base64Image", "data:" + in.mime + ";base64," + base64::encode(in.bytes));
    for (auto it = spec.parameters.begin(); it != spec.parameters.end(); ++it)
      fields.emplace_back(it.key(), it.value().is_string() ? it.value().get<std::string>() : json::dump(it.value()));
    add_defaults(route, req);
    req.body = net::form_urlencode(fields);
    return ok_status();
  }
};

}  // namespace

std::unique_ptr<Adapter> make_openai_transcriptions_adapter() { return std::make_unique<OpenAiTranscriptions>(); }
std::unique_ptr<Adapter> make_google_speech_adapter() { return std::make_unique<GoogleSpeech>(); }
std::unique_ptr<Adapter> make_ocr_space_form_adapter() { return std::make_unique<OcrSpaceForm>(); }

}  // namespace loom::llm
