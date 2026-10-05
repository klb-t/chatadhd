// Unit tests for loom/media_providers.h (ASR: Groq, Google Speech; OCR:
// ocr.space) using ScriptedTransport to check exact request shape and
// response parsing, without any real network access.
#include <doctest/doctest.h>

#include "loom/config.h"
#include "loom/media_providers.h"
#include "loom/net/http.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
std::string find_header(const net::Headers& h, std::string_view name) { return net::header_value(h, name); }
}  // namespace

TEST_SUITE("media") {
  TEST_CASE("profile injection changes maps, transport, and compatibility priors") {
    auto builtin = unwrap(RuntimeProfile::builtin("media"));
    auto profile = unwrap(builtin.with_overrides(Json{{"providers", {
      {"groq", {{"name", "local whisper"}, {"endpoint", "https://offline.invalid/asr"}, {"timeout_ms", 7},
                {"model", "local-model"}, {"format_map", {{"custom", "opus"}}}, {"max_segments", 1},
                {"placeholder_confidence", .42}, {"headers", {{"X-Test", "yes"}}}}},
      {"google", {{"encoding_map", {{"custom", "CUSTOM"}}}, {"default_language", "pl-PL"},
                  {"max_alternatives", 12}, {"word_confidence", false}}},
      {"ocr.space", {{"language_map", {{"custom", "custom-language"}}}, {"engine", 4},
                     {"text_placeholder_confidence", .37}}}
    }}}));
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200,
      Json{{"text", "result"}, {"segments", Json::array({Json{{"text", "a"}}, Json{{"text", "b"}}})}}));
    GroqAsr groq("fake", http, {}, profile);
    auto result = unwrap(groq.transcribe_bytes("bytes", "custom", {}));
    CHECK(groq.name() == "local whisper");
    CHECK(result.confidence == .42);  // configurable placeholder, not measured confidence
    CHECK(result.alternatives.size() == 1);
    auto req = http.requests().back();
    CHECK(req.url == "https://offline.invalid/asr");
    CHECK(req.timeout_ms == 7);
    CHECK(find_header(req.headers, "X-Test") == "yes");
    CHECK(req.body.find("filename=\"audio.opus\"") != std::string::npos);
    CHECK(req.body.find("local-model") != std::string::npos);

    GoogleSpeechAsr google("fake", http, profile);
    LOOM_REQUIRE_OK(google.transcribe_bytes("bytes", "custom", {}));
    auto body = unwrap(json::parse(http.requests().back().body));
    CHECK(body["config"]["encoding"] == "CUSTOM");
    CHECK(body["config"]["languageCode"] == "pl-PL");
    CHECK(body["config"]["maxAlternatives"] == 12);
    CHECK(body["config"]["enableWordConfidence"] == false);

    http.set_fallback(net::ScriptedTransport::Reply::json(200,
      Json{{"ParsedResults", Json::array({Json{{"ParsedText", "ocr"}}})}}));
    OcrSpaceProvider ocr("fake", http, {}, profile);
    auto ocr_result = unwrap(ocr.recognize_bytes("image", "png", std::string("custom")));
    CHECK(ocr_result.confidence == .37);
    req = http.requests().back();
    CHECK(req.body.find("language=custom-language") != std::string::npos);
    CHECK(req.body.find("OCREngine=4") != std::string::npos);

    auto removed = unwrap(builtin.with_patch(Json::array({
      Json{{"op", "remove"}, {"path", "/providers/google/encoding_map/flac"}}})));
    GoogleSpeechAsr without_flac("fake", http, removed);
    LOOM_REQUIRE_OK(without_flac.transcribe_bytes("bytes", "flac", {}));
    CHECK(unwrap(json::parse(http.requests().back().body))["config"]["encoding"] == "LINEAR16");

    Json forged_definition = builtin.definition();
    forged_definition["value_schema"] = Json{{"type", "object"}};
    forged_definition["defaults"]["providers"]["google"]["max_alternatives"] = "invalid";
    GoogleSpeechAsr invalid("fake", http, unwrap(RuntimeProfile::from_definition(forged_definition)));
    const auto before = http.requests().size();
    CHECK_FALSE(invalid.transcribe_bytes("bytes", "wav", {}));
    CHECK(http.requests().size() == before);
  }

  TEST_CASE("overlay can add descriptors, reorder adapters, and disable them") {
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("local_asr_key", "fake");
    auto defaults = unwrap(RuntimeProfile::builtin("media")).values();
    Json local = defaults["providers"]["groq"];
    local["name"] = "my endpoint";
    local["endpoint"] = "https://offline.invalid/local";
    local["secret_key"] = "local_asr_key";
    Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "media"},
                 {"overrides", {{"providers", {{"local", local}}}, {"asr_order", Json::array({"local"})},
                                {"ocr_order", Json::array()}}}};
    LOOM_REQUIRE_OK(fsutil::ensure_dir(td.path() / "profiles"));
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles/media.pack", overlay.dump()));
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"text", "custom"}}));
    MediaProviders providers(secrets, http);
    CHECK(providers.get_asr_providers() == std::vector<std::string>{"my endpoint"});
    CHECK(providers.get_ocr_providers().empty());
    CHECK(unwrap(providers.transcribe_bytes("bytes", "wav")).text == "custom");
    CHECK(http.requests().back().url == "https://offline.invalid/local");
    CHECK(unwrap(providers.runtime_profile())["is_builtin"] == false);

    overlay["overrides"]["asr_order"] = Json::array();
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles/media.pack", overlay.dump()));
    providers.refresh();
    CHECK(providers.get_asr_providers().empty());
  }

  TEST_CASE("invalid overlay or adapter fails before transport") {
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("groq_api_key", "fake");
    LOOM_REQUIRE_OK(fsutil::ensure_dir(td.path() / "profiles"));
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles/media.pack", "not JSON"));
    net::ScriptedTransport http;
    MediaProviders providers(secrets, http);
    CHECK_FALSE(providers.runtime_profile());
    CHECK_FALSE(providers.transcribe_bytes("bytes", "wav"));
    CHECK(http.requests().empty());
    Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "media"},
                 {"overrides", {{"providers", {{"groq", {{"adapter", "unknown"}}}}}}}};
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles/media.pack", overlay.dump()));
    providers.refresh();
    auto result = providers.transcribe_bytes("bytes", "wav");
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::Unsupported);
    CHECK(http.requests().empty());
  }

  TEST_CASE("GroqAsr: request shape and response parsing") {
    net::ScriptedTransport http;
    Json reply{{"text", " hello world "},
              {"language", "en"},
              {"duration", 3.5},
              {"segments", Json::array({Json{{"text", " seg1 "}, {"avg_logprob", -0.1}, {"start", 0.0}, {"end", 1.0}},
                                        Json{{"text", "seg2"}, {"avg_logprob", -0.2}, {"start", 1.0}, {"end", 2.0}}})}};
    http.expect("POST", "https://api.groq.com/openai/v1/audio/transcriptions", net::ScriptedTransport::Reply::json(200, reply));

    GroqAsr groq("gk_test", http);
    AsrResult r = unwrap(groq.transcribe_bytes("RIFF....audio-bytes....", "wav", std::nullopt));
    CHECK(r.text == "hello world");
    CHECK(r.confidence == 0.9);
    CHECK(r.language == "en");
    CHECK(r.duration == 3.5);
    REQUIRE(r.alternatives.size() == 2);
    CHECK(r.alternatives[0]["text"] == "seg1");
    CHECK(r.alternatives[0]["confidence"] == -0.1);

    auto reqs = http.requests();
    REQUIRE(reqs.size() == 1);
    const auto& req = reqs[0];
    CHECK(req.method == "POST");
    CHECK(req.url == "https://api.groq.com/openai/v1/audio/transcriptions");
    CHECK(find_header(req.headers, "Authorization") == "Bearer gk_test");
    CHECK(find_header(req.headers, "Content-Type").find("multipart/form-data") == 0);
    CHECK(req.body.find("name=\"file\"; filename=\"audio.wav\"") != std::string::npos);
    CHECK(req.body.find("Content-Type: audio/wav") != std::string::npos);
    CHECK(req.body.find("name=\"model\"") != std::string::npos);
    CHECK(req.body.find("whisper-large-v3-turbo") != std::string::npos);
    CHECK(req.body.find("name=\"response_format\"") != std::string::npos);
    CHECK(req.body.find("verbose_json") != std::string::npos);
    CHECK(req.body.find("RIFF....audio-bytes....") != std::string::npos);
  }

  TEST_CASE("GroqAsr: extension -> format mapping and language field") {
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"text", "x"}}));
    GroqAsr groq("k", http);
    LOOM_REQUIRE_OK(groq.transcribe_bytes("x", "m4a", std::string("pl")));
    auto reqs = http.requests();
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].body.find("filename=\"audio.mp4\"") != std::string::npos);
    CHECK(reqs[0].body.find("name=\"language\"") != std::string::npos);
    CHECK(reqs[0].body.find("\r\n\r\npl\r\n") != std::string::npos);
  }

  TEST_CASE("GroqAsr: HTTP error surfaces as Errc::Http") {
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::text(401, "unauthorized"));
    GroqAsr groq("bad", http);
    auto r = groq.transcribe_bytes("x", "wav", std::nullopt);
    CHECK_FALSE(r);
    CHECK(r.error().code == Errc::Http);
  }

  TEST_CASE("GoogleSpeechAsr: request shape and alternatives") {
    net::ScriptedTransport http;
    Json reply{{"results", Json::array({Json{{"alternatives", Json::array({Json{{"transcript", "first"}, {"confidence", 0.95}},
                                                                            Json{{"transcript", "second"}, {"confidence", 0.4}}})}}})}};
    http.expect("POST", "https://speech.googleapis.com/v1/speech:recognize", net::ScriptedTransport::Reply::json(200, reply));

    GoogleSpeechAsr google("goog_key", http);
    AsrResult r = unwrap(google.transcribe_bytes("wavbytes", "wav", std::string("pl-PL")));
    CHECK(r.text == "first");
    CHECK(r.confidence == 0.95);
    REQUIRE(r.alternatives.size() == 2);
    CHECK(r.alternatives[1]["text"] == "second");
    CHECK(r.alternatives[1]["rank"] == 2);

    auto reqs = http.requests();
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].url == "https://speech.googleapis.com/v1/speech:recognize?key=goog_key");
    Json body = unwrap(json::parse(reqs[0].body));
    CHECK(body["config"]["encoding"] == "LINEAR16");
    CHECK(body["config"]["languageCode"] == "pl-PL");
    CHECK(body["config"]["maxAlternatives"] == 5);
    CHECK(body["config"]["enableWordConfidence"] == true);
    CHECK(body["audio"]["content"] == base64::encode("wavbytes"));
  }

  TEST_CASE("GoogleSpeechAsr: default language en-US and encoding by extension") {
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"results", Json::array()}}));
    GoogleSpeechAsr google("k", http);
    LOOM_REQUIRE_OK(google.transcribe_bytes("x", "flac", std::nullopt));
    auto reqs = http.requests();
    Json body = unwrap(json::parse(reqs[0].body));
    CHECK(body["config"]["languageCode"] == "en-US");
    CHECK(body["config"]["encoding"] == "FLAC");
  }

  TEST_CASE("OcrSpaceProvider: request shape and text parsing") {
    net::ScriptedTransport http;
    Json reply{{"IsErroredOnProcessing", false},
              {"ParsedResults", Json::array({Json{{"ParsedText", "line one\r\n \r\nline two \n"},
                                                   {"TextOverlay", Json{{"Lines", Json::array({Json{{"Words", Json::array({Json{{"WordText", "one"}}})}}})}}}}})}};
    http.expect("POST", "https://api.ocr.space/parse/image", net::ScriptedTransport::Reply::json(200, reply));

    OcrSpaceProvider ocr("ocrkey", http);
    OcrResult r = unwrap(ocr.recognize_bytes("PNGDATA", "png", std::string("pl")));
    CHECK(r.text == "line one\nline two");
    CHECK(r.confidence == 0.9);
    REQUIRE(r.lines.size() == 2);
    CHECK(r.lines[0] == "line one");
    CHECK(r.lines[1] == "line two");

    auto reqs = http.requests();
    REQUIRE(reqs.size() == 1);
    CHECK(find_header(reqs[0].headers, "Content-Type") == "application/x-www-form-urlencoded");
    CHECK(reqs[0].body.find("apikey=ocrkey") != std::string::npos);
    CHECK(reqs[0].body.find("language=pol") != std::string::npos);
    CHECK(reqs[0].body.find("OCREngine=2") != std::string::npos);
    CHECK(reqs[0].body.find("isTable=True") != std::string::npos);
    CHECK(reqs[0].body.find("scale=True") != std::string::npos);
    CHECK(reqs[0].body.find("base64Image=data%3Aimage%2Fpng%3Bbase64%2C") != std::string::npos);
  }

  TEST_CASE("OcrSpaceProvider: default language eng, error surfaces as Errc::Http") {
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(
        200, Json{{"IsErroredOnProcessing", true}, {"ErrorMessage", Json::array({"bad image"})}}));
    OcrSpaceProvider ocr("k", http);
    auto r = ocr.recognize_bytes("x", "png", std::nullopt);
    CHECK_FALSE(r);
    CHECK(r.error().code == Errc::Http);
    auto reqs = http.requests();
    CHECK(reqs[0].body.find("language=eng") != std::string::npos);
  }

  TEST_CASE("OcrSpaceProvider: empty ParsedResults -> empty OcrResult") {
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"IsErroredOnProcessing", false}, {"ParsedResults", Json::array()}}));
    OcrSpaceProvider ocr("k", http);
    OcrResult r = unwrap(ocr.recognize_bytes("x", "png", std::nullopt));
    CHECK(r.text == "");
    CHECK(r.confidence == 0.0);
    CHECK(r.lines.empty());
  }

  TEST_CASE("MediaProviders: no providers configured -> Unavailable") {
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    net::ScriptedTransport http;
    MediaProviders mp(secrets, http);
    CHECK(mp.get_asr_providers().empty());
    CHECK(mp.get_ocr_providers().empty());
    Json st = mp.status();
    CHECK(st["asr"]["configured"] == false);
    CHECK(st["ocr"]["configured"] == false);
    auto r = mp.transcribe_bytes("x", "wav");
    CHECK_FALSE(r);
    CHECK(r.error().code == Errc::Unavailable);
    auto o = mp.ocr_bytes("x", "png");
    CHECK_FALSE(o);
    CHECK(o.error().code == Errc::Unavailable);
  }

  TEST_CASE("MediaProviders: refresh() picks up secrets; groq preferred over google") {
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("groq_api_key", "gk");
    secrets.set("google_speech_api_key", "gsk");
    secrets.set("ocr_space_api_key", "ok");
    net::ScriptedTransport http;
    MediaProviders mp(secrets, http);  // constructor already calls refresh()

    CHECK(mp.get_asr_providers() == std::vector<std::string>{"groq", "google"});
    CHECK(mp.get_ocr_providers() == std::vector<std::string>{"ocr.space"});
    CHECK(mp.status()["asr"]["configured"] == true);

    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"text", "from groq"}}));
    AsrResult r = unwrap(mp.transcribe_bytes("bytes", "wav"));
    CHECK(r.text == "from groq");
    CHECK(http.requests().back().url.find("groq.com") != std::string::npos);
  }

  TEST_CASE("MediaProviders: falls back to google when groq fails") {
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("groq_api_key", "gk");
    secrets.set("google_speech_api_key", "gsk");
    net::ScriptedTransport http;
    http.expect("POST", "https://api.groq.com", net::ScriptedTransport::Reply::fail(Errc::Network, "boom"));
    http.expect("POST", "https://speech.googleapis.com",
               net::ScriptedTransport::Reply::json(200, Json{{"results", Json::array()}}));
    MediaProviders mp(secrets, http);
    AsrResult r = unwrap(mp.transcribe_bytes("bytes", "wav"));
    CHECK(r.text == "");  // empty results -> empty transcript, but no error
    CHECK(http.requests().size() == 2);
  }

  TEST_CASE("MediaProviders: explicit provider name selects that provider; unknown -> NotFound") {
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("groq_api_key", "gk");
    secrets.set("google_speech_api_key", "gsk");
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"results", Json::array()}}));
    MediaProviders mp(secrets, http);

    auto r = mp.transcribe_bytes("x", "wav", std::string("google"));
    LOOM_REQUIRE_OK(r);
    CHECK(http.requests().back().url.find("googleapis") != std::string::npos);

    auto bad = mp.transcribe_bytes("x", "wav", std::string("nope"));
    CHECK_FALSE(bad);
    CHECK(bad.error().code == Errc::NotFound);
  }

  TEST_CASE("MediaProviders: platform-injected extra provider participates") {
    struct FakeAsr final : AsrProvider {
      std::string name() const override { return "platform"; }
      Result<AsrResult> transcribe_bytes(std::string_view, std::string_view, const std::optional<std::string>&) override {
        AsrResult r;
        r.text = "on-device";
        return r;
      }
      Result<AsrResult> transcribe(const std::filesystem::path&, const std::optional<std::string>&) override {
        AsrResult r;
        r.text = "on-device";
        return r;
      }
    };
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    net::ScriptedTransport http;
    MediaProviders mp(secrets, http);
    mp.add_asr_provider(std::make_shared<FakeAsr>());
    CHECK(mp.get_asr_providers() == std::vector<std::string>{"platform"});
    AsrResult r = unwrap(mp.transcribe_bytes("x", "wav", std::string("platform")));
    CHECK(r.text == "on-device");
  }
}
