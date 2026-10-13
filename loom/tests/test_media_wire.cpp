// Golden request bytes of the media providers (ASR: Groq, Google Speech; OCR:
// ocr.space): method, URL, header order, body bytes, timeout.  Pinned before
// the providers moved onto the adapter registry and the one model action, so
// the move is provably byte-for-byte.  The multipart boundary is random; it is
// replaced by BOUNDARY.
#include <doctest/doctest.h>

#include "loom/media_providers.h"
#include "loom/net/http.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json wire(const net::HttpRequest& r) {
  Json j = loom::test::request_json(r);
  std::string boundary;
  for (const auto& [name, value] : r.headers) {
    const auto at = value.find("boundary=");
    if (name == "Content-Type" && at != std::string::npos) boundary = value.substr(at + 9);
  }
  if (boundary.empty()) return j;
  auto swap = [&](std::string s) {
    for (auto p = s.find(boundary); p != std::string::npos; p = s.find(boundary, p + 8)) s.replace(p, boundary.size(), "BOUNDARY");
    return s;
  };
  j["body"] = swap(r.body);
  for (auto& h : j["headers"]) h[1] = swap(h[1].get<std::string>());
  return j;
}

}  // namespace

TEST_SUITE("media wire") {
  TEST_CASE("golden: Groq ASR request bytes are pinned") {
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"text", "x"}}));
    GroqAsr groq("gk_test", http);
    LOOM_REQUIRE_OK(groq.transcribe_bytes("AUDIO", "m4a", std::string("pl")));
    auto reqs = http.requests();
    REQUIRE(reqs.size() == 1);
    const Json expected{
        {"method", "POST"},
        {"url", "https://api.groq.com/openai/v1/audio/transcriptions"},
        {"headers", Json::array({Json::array({"Authorization", "Bearer gk_test"}),
                                 Json::array({"Content-Type", "multipart/form-data; boundary=BOUNDARY"})})},
        {"body", "--BOUNDARY\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.mp4\"\r\n"
                 "Content-Type: audio/mp4\r\n\r\nAUDIO\r\n"
                 "--BOUNDARY\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-large-v3-turbo\r\n"
                 "--BOUNDARY\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\nverbose_json\r\n"
                 "--BOUNDARY\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\npl\r\n"
                 "--BOUNDARY--\r\n"},
        {"timeout_ms", 60000},
        {"stream", false}};
    CHECK(wire(reqs[0]) == expected);
  }

  TEST_CASE("golden: Google Speech request bytes are pinned") {
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"results", Json::array()}}));
    GoogleSpeechAsr google("goog_key", http);
    LOOM_REQUIRE_OK(google.transcribe_bytes("AUDIO", "flac", std::string("pl-PL")));
    auto reqs = http.requests();
    REQUIRE(reqs.size() == 1);
    const Json expected{
        {"method", "POST"},
        {"url", "https://speech.googleapis.com/v1/speech:recognize?key=goog_key"},
        {"headers", Json::array({Json::array({"Content-Type", "application/json"})})},
        {"body", "{\"config\":{\"encoding\":\"FLAC\",\"languageCode\":\"pl-PL\",\"maxAlternatives\":5,"
                 "\"enableWordConfidence\":true,\"enableWordTimeOffsets\":true},\"audio\":{\"content\":\"QVVESU8=\"}}"},
        {"timeout_ms", 60000},
        {"stream", false}};
    CHECK(wire(reqs[0]) == expected);
  }

  TEST_CASE("golden: OCR.space request bytes are pinned") {
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"IsErroredOnProcessing", false}, {"ParsedResults", Json::array()}}));
    OcrSpaceProvider ocr("ocrkey", http);
    LOOM_REQUIRE_OK(ocr.recognize_bytes("PNGDATA", "png", std::string("pl")));
    auto reqs = http.requests();
    REQUIRE(reqs.size() == 1);
    const Json expected{
        {"method", "POST"},
        {"url", "https://api.ocr.space/parse/image"},
        {"headers", Json::array({Json::array({"Content-Type", "application/x-www-form-urlencoded"})})},
        {"body", "apikey=ocrkey&base64Image=data%3Aimage%2Fpng%3Bbase64%2CUE5HREFUQQ%3D%3D&language=pol&OCREngine=2"
                 "&isTable=True&scale=True"},
        {"timeout_ms", 60000},
        {"stream", false}};
    CHECK(wire(reqs[0]) == expected);
  }
}
