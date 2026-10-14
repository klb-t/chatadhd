// P14 3d: a model route that failed (the local method decided) is not only visible in chat.route: the chat stream says so with an event
// `route_issue` before the answer, and the input goes on exactly as it would have (a message still reaches the model; R54 loses nothing).
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/loom.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json completion(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 7}}}};
}

// The C ABI with a scripted platform transport: replies in order, the requests kept.
struct Wire {
  struct Reply {
    int status = 200;
    std::string body;
  };
  std::vector<Reply> replies;
  std::size_t next = 0;
  std::vector<Json> requests;
  static int send(const char* request_json, LoomHttpResponse* resp, void* ud) {
    auto* self = static_cast<Wire*>(ud);
    self->requests.push_back(Json::parse(request_json));
    const Reply r = self->next < self->replies.size() ? self->replies[self->next++] : Reply{500, "{}"};
    if (loom_http_response_begin(resp, r.status, R"({"Content-Type":"application/json"})") != LOOM_OK) return LOOM_OK;
    loom_http_response_write(resp, r.body.data(), r.body.size());
    return LOOM_OK;
  }
};

struct Chunks {
  std::vector<Json> events;
  static void collect(const char* raw, int, void* ud) { static_cast<Chunks*>(ud)->events.push_back(Json::parse(raw)); }
  std::vector<std::string> types() const {
    std::vector<std::string> out;
    for (const auto& e : events) out.push_back(e.value("type", ""));
    return out;
  }
  const Json* first(const std::string& type) const {
    for (const auto& e : events)
      if (e.value("type", "") == type) return &e;
    return nullptr;
  }
};

struct Ctx {
  fsutil::TempDir dir;
  LoomContext* ctx = nullptr;
  Wire wire;
  explicit Ctx(const std::string& method) {
    std::string opts = Json{{"data_dir", dir.path().string()}, {"start_workers", false}}.dump();
    const char* err = nullptr;
    ctx = loom_init_ex(opts.c_str(), &err);
    if (err) loom_free_string(err);
    REQUIRE(ctx != nullptr);
    REQUIRE(loom_set_http_transport(ctx, Wire::send, &wire) == LOOM_OK);
    REQUIRE(loom_set_secret(ctx, "api_key", "sk-test-key") == LOOM_OK);
    REQUIRE(loom_set_config_json(ctx, Json{{"base_url", "https://api.test"}, {"default_model", "test/model"}, {"intent_routing_method", method}}.dump().c_str()) == LOOM_OK);
  }
  ~Ctx() {
    if (ctx) loom_shutdown(ctx);
  }
  Json chat(const Json& request, Chunks& chunks) {
    const char* raw = loom_chat_ex(ctx, request.dump().c_str(), Chunks::collect, &chunks);
    REQUIRE(raw != nullptr);
    Json out = Json::parse(raw);
    loom_free_string(raw);
    return out;
  }
};

}  // namespace

TEST_SUITE("chat_route_issue") {
  TEST_CASE("a failed model route is an event in the chat stream; the message still goes to the model, with the bytes of an ordinary one") {
    Ctx routed("model");
    routed.wire.replies = {{500, R"({"error":"boom"})"}, {200, completion("Hi there.").dump()}};
    Chunks stream;
    const Json result = routed.chat(Json{{"message", "Hello there"}, {"stream", false}}, stream);
    INFO(Json(stream.events).dump());
    REQUIRE(result.value("error", Json(nullptr)).is_null());
    // the event comes first and names what happened; the stream then ends as any chat stream
    REQUIRE(stream.events.size() >= 3);
    CHECK(stream.events[0].at("type") == "route_issue");
    CHECK(stream.events[0].at("issues").at(0).at("code") == "model_route_failed");
    CHECK(stream.events[0].at("issues").at(0).at("fallback") == "heuristic");
    CHECK(stream.events[0].at("issues").at(0).at("message").is_string());
    CHECK(stream.events[0].at("request_id").is_string());
    CHECK(stream.events.back().at("type") == "done");
    CHECK(stream.events.back().at("request_id") == stream.events[0].at("request_id"));
    CHECK(result.at("route_issues").at(0).at("code") == "model_route_failed");
    // the routing call failed, the message was sent: two requests, the second is the chat
    REQUIRE(routed.wire.requests.size() == 2);
    CHECK(routed.wire.requests[1].at("url") == "https://api.test/chat/completions");
    // the very request an ordinary message makes (heuristic routing: no routing call, nothing to report)
    Ctx plain("heuristic");
    plain.wire.replies = {{200, completion("Hi there.").dump()}};
    Chunks plain_stream;
    (void)plain.chat(Json{{"message", "Hello there"}, {"stream", false}}, plain_stream);
    REQUIRE(plain.wire.requests.size() == 1);
    CHECK(plain.wire.requests[0].at("body") == routed.wire.requests[1].at("body"));
    CHECK(plain_stream.first("route_issue") == nullptr);
    // the model said "message": no problem, no event
    Ctx fine("model");
    fine.wire.replies = {{200, completion(R"({"kind":"message","action":null,"reference":null,"args":{},"confidence":0.9})").dump()},
                         {200, completion("Hi there.").dump()}};
    Chunks fine_stream;
    (void)fine.chat(Json{{"message", "Hello there"}, {"stream", false}}, fine_stream);
    CHECK(fine_stream.first("route_issue") == nullptr);
    CHECK(fine.wire.requests.size() == 2);
  }

  TEST_CASE("the same when the local method found a command: the event is said, the proposal is the answer") {
    fsutil::TempDir td;
    auto transport = std::make_shared<net::ScriptedTransport>();
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    auto rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("intent_routing_method", "model");
    rt->config().set("model_registry_autorefresh", "off");
    const std::string conv = unwrap(rt->db().create_conv("Planning")).id;
    NewMessage m;
    m.conv_id = conv;
    m.role = "user";
    m.text = "We should ship on Friday.";
    unwrap(rt->db().create_msg(m));
    transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(500, Json{{"error", "boom"}}));
    ChatOptions opts;
    opts.conv_id = conv;
    opts.stream = false;
    const auto out = unwrap(chat_route::handle(*rt, "Summarise this conversation", opts, Json{{"message", "Summarise this conversation"}}, nullptr));
    REQUIRE(out.handled);
    REQUIRE(out.issues.size() == 1);
    CHECK(out.issues.at(0).at("code") == "model_route_failed");
    // a message carries it too, and an input with nothing wrong carries nothing
    transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(500, Json{{"error", "boom"}}));
    const auto message = unwrap(chat_route::handle(*rt, "Hello there", opts, Json{{"message", "Hello there"}}, nullptr));
    CHECK_FALSE(message.handled);
    REQUIRE(message.issues.size() == 1);
    CHECK(message.issues.at(0).at("code") == "model_route_failed");
    rt->config().set("intent_routing_method", "heuristic");
    CHECK(unwrap(chat_route::handle(*rt, "Hello there", opts, Json{{"message", "Hello there"}}, nullptr)).issues.empty());
  }
}
