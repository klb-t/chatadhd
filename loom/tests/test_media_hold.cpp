// R16: a usage-guard hold (x10 growth) of a media call (ASR / OCR) is a pause, not a provider failure: no other provider is tried,
// the reply carries the hold's receipt, a confirmed identical call sends once with identical bytes, a rejection sends nothing.
#include <doctest/doctest.h>

#include "loom/media_providers.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

const char* kGroq = "https://api.groq.com/openai/v1/audio/transcriptions";

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("groq_api_key", "gk_test");
    rt->secrets().set("google_speech_api_key", "gsk_test");
    rt->secrets().set("ocr_space_api_key", "ocrkey");
    loom::test::measured_baseline_only(rt->config());  // P17 6: this test is about the x10 hold over three tiny samples, so the floor of the baseline is off
    rt->media().refresh();
  }
  void asr_ok(int n) {
    for (int i = 0; i < n; ++i)
      transport->expect("POST", kGroq, net::ScriptedTransport::Reply::json(200, Json{{"text", "hello"}}));
  }
  // Three small recordings set the baseline; a recording 20000 times as big is x10 growth.
  void baseline() {
    asr_ok(3);
    for (int i = 0; i < 3; ++i) REQUIRE(unwrap(rt->media().transcribe_bytes("tiny", "wav")).text == "hello");
    REQUIRE(transport->requests().size() == 3);
  }
  static Json confirmation(const Json& pending, bool approved) {
    return Json{{"approved", approved}, {"receipt_id", pending.at("receipt_id")}, {"ref", "test"},
                {"operation_id", pending.at("operation_id")}};
  }
};

}  // namespace

TEST_SUITE("media hold") {
  TEST_CASE("a hold is a pause with the receipt: not an error, the next provider is not tried, nothing is sent") {
    Fixture f;
    f.baseline();
    const std::string big(60000, 'a');
    const auto reply = unwrap(f.rt->media().transcribe_bytes_resumable(big, "wav"));
    REQUIRE(reply.paused());
    CHECK(reply.pending.at("schema") == "loom.action_usage_pending/1");
    CHECK(reply.pending.at("status") == "requires_confirmation");
    CHECK(reply.pending.at("sent") == false);
    CHECK(reply.pending.at("receipt_id").is_string());
    CHECK(reply.pending.at("operation_id").is_string());
    CHECK(f.transport->requests().size() == 3);  // not sent anywhere, google included
    // the legacy form is a Paused error, still without a fallback
    auto legacy = f.rt->media().transcribe_bytes(big, "wav");
    REQUIRE_FALSE(legacy);
    CHECK(legacy.error().code == Errc::Paused);
    CHECK(f.transport->requests().size() == 3);
  }

  TEST_CASE("confirming re-runs the identical request once; only the provider that held it is tried") {
    Fixture f;
    f.baseline();
    const std::string big(60000, 'a');
    const auto held = unwrap(f.rt->media().transcribe_bytes_resumable(big, "wav"));
    REQUIRE(held.paused());
    f.asr_ok(1);
    auto dr = f.rt->media().transcribe_bytes_resumable(big, "wav", {}, {}, Fixture::confirmation(held.pending, true));
    if (!dr) MESSAGE(dr.error().message);
    const auto done = unwrap(std::move(dr));
    REQUIRE_FALSE(done.paused());
    CHECK(done.value->text == "hello");
    REQUIRE(f.transport->requests().size() == 4);
    // the same call again is a new admission, not the old confirmation
    const auto sent = f.transport->requests().back();
    CHECK(sent.url == kGroq);
    CHECK(sent.body.find(big) != std::string::npos);
    // identical bytes: the request of an unguarded provider for the same input (boundary aside) is the held request
    Secrets secrets(f.td.path() / "ref" / "secrets.json");
    secrets.set("groq_api_key", "gk_test");
    net::ScriptedTransport ref;
    ref.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"text", "hello"}}));
    MediaProviders plain(secrets, ref);
    REQUIRE(unwrap(plain.transcribe_bytes(big, "wav")).text == "hello");
    const auto want = ref.requests().back();
    CHECK(sent.body == want.body);  // the boundary is derived from the parts: byte for byte
    CHECK(sent.headers == want.headers);
    CHECK(sent.url == want.url);
    CHECK(sent.timeout_ms == want.timeout_ms);
  }

  TEST_CASE("a confirmed call that fails does not spend on another provider") {
    Fixture f;
    f.baseline();
    const std::string big(60000, 'a');
    const auto held = unwrap(f.rt->media().transcribe_bytes_resumable(big, "wav"));
    REQUIRE(held.paused());
    f.transport->expect("POST", kGroq, net::ScriptedTransport::Reply::fail(Errc::Network, "boom"));
    auto r = f.rt->media().transcribe_bytes_resumable(big, "wav", {}, {}, Fixture::confirmation(held.pending, true));
    CHECK_FALSE(r);
    CHECK(f.transport->requests().size() == 4);  // groq only, no google
  }

  TEST_CASE("rejecting ends cleanly: nothing is sent, the reply carries the refusal") {
    Fixture f;
    f.baseline();
    const std::string big(60000, 'a');
    const auto held = unwrap(f.rt->media().transcribe_bytes_resumable(big, "wav"));
    REQUIRE(held.paused());
    const auto no = unwrap(f.rt->media().transcribe_bytes_resumable(big, "wav", {}, {}, Fixture::confirmation(held.pending, false)));
    REQUIRE(no.paused());
    CHECK((no.pending.at("status") == "denied" || no.pending.at("status") == "cancelled"));  // the guard words a refusal
    CHECK(f.transport->requests().size() == 3);
  }

  TEST_CASE("the normal path is unchanged: resumable returns the value, fallback still works on failure") {
    Fixture f;
    f.asr_ok(1);
    const auto ok = unwrap(f.rt->media().transcribe_bytes_resumable("tiny", "wav"));
    REQUIRE_FALSE(ok.paused());
    CHECK(ok.value->text == "hello");
    f.transport->expect("POST", kGroq, net::ScriptedTransport::Reply::fail(Errc::Network, "boom"));
    f.transport->expect("POST", "https://speech.googleapis.com",
                        net::ScriptedTransport::Reply::json(200, Json{{"results", Json::array()}}));
    const auto fb = unwrap(f.rt->media().transcribe_bytes_resumable("tiny", "wav"));
    CHECK_FALSE(fb.paused());
    CHECK(f.transport->requests().size() == 3);
  }

  TEST_CASE("OCR holds the same way") {
    Fixture f;
    const char* ocr = "https://api.ocr.space/parse/image";
    const Json okbody{{"IsErroredOnProcessing", false},
                      {"ParsedResults", Json::array({Json{{"ParsedText", "t"}, {"TextOverlay", Json{{"Lines", Json::array()}}}}})}};
    for (int i = 0; i < 3; ++i) {
      f.transport->expect("POST", ocr, net::ScriptedTransport::Reply::json(200, okbody));
      REQUIRE(unwrap(f.rt->media().ocr_bytes("tiny", "png")).text == "t");
    }
    const std::string big(60000, 'a');
    const auto held = unwrap(f.rt->media().ocr_bytes_resumable(big, "png"));
    REQUIRE(held.paused());
    CHECK(held.pending.at("media") == "media.ocr");
    CHECK(f.transport->requests().size() == 3);
    f.transport->expect("POST", ocr, net::ScriptedTransport::Reply::json(200, okbody));
    const auto done = unwrap(f.rt->media().ocr_bytes_resumable(big, "png", {}, {}, Fixture::confirmation(held.pending, true)));
    REQUIRE_FALSE(done.paused());
    CHECK(f.transport->requests().size() == 4);
  }
}
