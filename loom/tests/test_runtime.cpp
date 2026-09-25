#include <doctest/doctest.h>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/net/http.h"
#include "loom/relations.h"
#include "loom/runtime.h"
#include "loom/selector.h"
#include "loom/semantic_analyzer.h"
#include "loom/tasks.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("runtime") {
  TEST_CASE("open wires every subsystem; shutdown is idempotent") {
    fsutil::TempDir td;
    RuntimeOptions o;
    o.data_dir = (td.path() / "data").string();
    o.start_workers = true;
    auto rt = unwrap(Runtime::open(o));
    CHECK(rt->paths().root == td.path() / "data");
    CHECK(std::filesystem::exists(td.path() / "data" / ".chatadhd_data"));
    CHECK(unwrap(rt->db().schema_version()) == 4);
    CHECK(rt->bus().handler_count(events::kMsgCreated) == 1);  // GraphEngine
    CHECK(rt->bus().handler_count(events::kImportDone) == 1);  // SemanticWorker wake
    CHECK(unwrap(rt->relations().get("supersedes")).has_value());
    CHECK(rt->tasks().running());
    CHECK(rt->config().get("theme") == "dark");
    CHECK(!rt->analyzer().rules().entity_patterns.empty());
    CHECK(rt->analyzer().rules().entity_patterns.size() == 11);
    CHECK(rt->analyzer().rules().topics.size() == 7);

    // Tasks run on the runtime's worker pool.
    rt->tasks().register_handler("echo", [](TaskContext& ctx) -> Status {
      ctx.set_result(ctx.params());
      return {};
    });
    std::string id = unwrap(rt->tasks().submit("echo", Json{{"x", 1}}));
    for (int i = 0; i < 300 && unwrap(rt->tasks().get(id))->status != "done"; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(unwrap(rt->tasks().get(id))->status == "done");

    Json info = rt->info();
    CHECK(info["data_dir"] == (td.path() / "data").string());
    CHECK(info["paths"]["db"] == (td.path() / "data" / "chatadhd.db").string());

    rt->shutdown();
    rt->shutdown();
    CHECK(!rt->tasks().running());
  }

  TEST_CASE("HTTP transport can be swapped at runtime") {
    fsutil::TempDir td;
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    auto scripted = std::make_shared<net::ScriptedTransport>();
    scripted->expect("GET", "https://example.test/", net::ScriptedTransport::Reply::json(200, Json{{"ok", true}}));
    o.http = scripted;
    auto rt = unwrap(Runtime::open(o));
    net::HttpRequest req;
    req.url = "https://example.test/ping";
    auto resp = unwrap(rt->http().send(req));
    CHECK(resp.status == 200);
    CHECK(unwrap(resp.json())["ok"] == true);
    CHECK(rt->http().name() == "scripted");
    rt->set_http_transport(nullptr);
    CHECK(rt->http().name() != "scripted");
    CHECK(!rt->tasks().running());
  }

  TEST_CASE("options from JSON") {
    auto o = unwrap(RuntimeOptions::from_json(Json{{"data_dir", "/x"}, {"start_workers", false}, {"log_level", "warning"}}));
    CHECK(*o.data_dir == "/x");
    CHECK(!o.start_workers);
    CHECK(*o.log_level == log::Level::Warning);
    CHECK(!RuntimeOptions::from_json(Json{{"nope", 1}}));
    CHECK(!RuntimeOptions::from_json(Json{{"log_level", "loud"}}));
    CHECK(RuntimeOptions::from_json(Json(nullptr)));
    log::set_level(log::Level::Info);
  }

  TEST_CASE("ScriptedTransport streaming, fallback and helpers") {
    net::ScriptedTransport t;
    t.expect("POST", "https://api.test/chat", net::ScriptedTransport::Reply::sse({"{\"a\":1}", "[DONE]"}));
    std::string streamed;
    int status = 0;
    net::StreamSink sink;
    sink.on_headers = [&](int s, const net::Headers&) {
      status = s;
      return true;
    };
    sink.on_data = [&](std::string_view c) {
      streamed += c;
      return true;
    };
    net::HttpRequest req;
    req.method = "POST";
    req.url = "https://api.test/chat/completions";
    auto r = unwrap(t.send(req, &sink));
    CHECK(status == 200);
    CHECK(r.body.empty());
    CHECK(streamed == "data: {\"a\":1}\n\ndata: [DONE]\n\n");
    CHECK(!t.send(req));  // no expectation left, no fallback
    t.set_fallback(net::ScriptedTransport::Reply::fail(Errc::Timeout, "slow"));
    auto f = t.send(req);
    REQUIRE(!f);
    CHECK(f.error().code == Errc::Timeout);
    CHECK(t.requests().size() == 3);

    CancelToken tok;
    tok.cancel();
    t.expect("", "https://", net::ScriptedTransport::Reply::text(200, "x"));
    CHECK(t.send(req, nullptr, &tok).error().code == Errc::Cancelled);

    auto u = unwrap(net::parse_url("https://user@api.example.com:8443/v1/models?x=1#frag"));
    CHECK(u.scheme == "https");
    CHECK(u.host == "api.example.com");
    CHECK(u.port == 8443);
    CHECK(u.target == "/v1/models?x=1");
    CHECK(unwrap(net::parse_url("http://h")).target == "/");
    CHECK(unwrap(net::parse_url("http://h")).port == 80);
    CHECK(!net::parse_url("nohost"));
    CHECK(net::url_encode("a b/ż") == "a%20b%2F%C5%BC");
    CHECK(net::form_urlencode({{"k", "a b"}, {"x", "1&2"}}) == "k=a+b&x=1%262");
    CHECK(net::with_query("https://x/y", {{"key", "v"}}) == "https://x/y?key=v");
    CHECK(net::with_query("https://x/y?a=1", {{"key", "v"}}) == "https://x/y?a=1&key=v");
    auto mp = net::build_multipart({{"model", "", "", "whisper"}, {"file", "audio.wav", "audio/wav", "RIFF"}});
    CHECK(mp.content_type.rfind("multipart/form-data; boundary=", 0) == 0);
    CHECK(mp.body.find("name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\nRIFF\r\n") !=
          std::string::npos);
    auto hr = unwrap(net::HttpRequest::from_json(req.to_json()));
    CHECK(hr.url == req.url);
    CHECK(hr.method == "POST");
    CHECK(net::header_value({{"Content-Type", "x"}}, "content-type") == "x");
  }
}
