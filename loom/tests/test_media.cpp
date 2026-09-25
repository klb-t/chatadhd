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
