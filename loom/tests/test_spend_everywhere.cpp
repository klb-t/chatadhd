// P18 1: the spending limits of the user (setting usage_spend_limits) apply to EVERY model use, not only to action runs. The central admission of
// the usage ledger (UsagePolicy, through effective_usage_policy_options) reads them, so a plain chat send, the routing model call, the background
// analysis and an action's model call obey the same limits with the same on_exceed: confirm holds with a receipt like the x10 hold, deny refuses
// with the reason, notify admits and tells. No limit is the preset: nothing changes for a user who has set none.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/loom.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/usage_policy.h"
#include "loom/usage_spend.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 7}}}};
}

Json limit(const char* period, double amount, const char* on_exceed) {
  return Json{{"period", period}, {"amount", amount}, {"currency", "USD"}, {"on_exceed", on_exceed}};
}

// A completed operation of the ledger that reported `usd` (now): what the providers charged for something earlier in the period.
void spend_usd(const std::filesystem::path& dir, double usd, const std::string& op = "earlier") {
  auto policy = unwrap(UsagePolicy::open(dir / "usage-policy.sqlite", Json::object()));
  Json estimate{{"operation_id", op}, {"baseline_key", "chat.send|test|m"}, {"model", "test/model"}, {"resources", Json{{"output_tokens", nullptr}, {"cost_usd", nullptr}}}};
  unwrap(policy->request(estimate));
  unwrap(policy->complete(op, Json{{"resources", Json{{"requests", 1}, {"output_tokens", 120}, {"cost_usd", usd}}}, {"provenance", "provider_reported"}}));
}

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
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("auto_title", false);
    rt->config().set("model_registry_autorefresh", "off");
    unwrap(rt->config().save());
  }
  void limits(const Json& list) {
    rt->config().set("usage_spend_limits", list);
    unwrap(rt->config().save());
  }
  void script(const std::string& content = "hi") {
    transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content)));
  }
  std::size_t model_calls() {
    std::size_t n = 0;
    for (const auto& r : transport->requests())
      if (r.method == "POST" && r.url.find("/chat/completions") != std::string::npos) ++n;
    return n;
  }
  ChatOptions plain() {
    ChatOptions o;
    o.stream = false;
    return o;
  }
  static ChatOptions with_answer(ChatOptions o, const Json& pending, bool approved) {
    o.usage_confirmation = Json{{"approved", approved}, {"receipt_id", pending.at("receipt_id")}, {"operation_id", pending.at("operation_id")}, {"ref", "test"}};
    return o;
  }
};

// The C ABI with a scripted platform transport (as tests/test_chat_route_issue.cpp): the chunks of the stream are what the web client reads.
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
  explicit Ctx(const std::string& method, const Json& limits) {
    std::string opts = Json{{"data_dir", dir.path().string()}, {"start_workers", false}}.dump();
    const char* err = nullptr;
    ctx = loom_init_ex(opts.c_str(), &err);
    if (err) loom_free_string(err);
    REQUIRE(ctx != nullptr);
    REQUIRE(loom_set_http_transport(ctx, Wire::send, &wire) == LOOM_OK);
    REQUIRE(loom_set_secret(ctx, "api_key", "sk-test-key") == LOOM_OK);
    REQUIRE(loom_set_config_json(ctx, Json{{"base_url", "https://api.test"}, {"default_model", "test/model"}, {"auto_title", false},
                                           {"model_registry_autorefresh", "off"}, {"intent_routing_method", method}, {"usage_spend_limits", limits}}.dump().c_str()) == LOOM_OK);
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
  std::size_t chat_posts() const {
    std::size_t n = 0;
    for (const auto& r : wire.requests)
      if (r.value("url", "").find("/chat/completions") != std::string::npos) ++n;
    return n;
  }
};

}  // namespace

TEST_SUITE("spend_everywhere") {
  TEST_CASE("no limit is the preset: the options, the decision and the request are as they were") {
    Fixture f;
    // the options of the policy are the embedded preset, nothing is added for a user without limits
    const Json options = unwrap(effective_usage_policy_options(f.rt->config()));
    CHECK(options == unwrap(usage_policy_preset()));
    CHECK_FALSE(options.contains("spend_limits"));
    f.limits(Json::array());
    CHECK(unwrap(effective_usage_policy_options(f.rt->config())) == unwrap(usage_policy_preset()));
    // a chat send is admitted and the decision of the ledger has none of the new keys
    f.script("hello");
    auto r = f.rt->chat().send("one ordinary message", f.plain());
    REQUIRE(r);
    CHECK_FALSE(r->usage_pending.has_value());
    CHECK(r->usage_notices.is_null());
    CHECK_FALSE(r->to_json().contains("usage_notices"));
    auto policy = unwrap(UsagePolicy::open(f.td.path() / "usage-policy.sqlite", options));
    const Json decision = unwrap(policy->preview(Json{{"operation_id", "p1"}, {"baseline_key", "chat.send|x|y"}, {"model", "y"}, {"resources", Json{{"requests", 1}}}}));
    for (const char* key : {"limits", "limit_report", "notices", "reason_codes"}) CHECK_FALSE(decision.contains(key));
    CHECK(decision.at("status") == "allowed");
  }

  TEST_CASE("chat send over a monthly limit with confirm: held with a receipt and its own reason, then confirmed and sent once") {
    Fixture f;
    f.limits(Json::array({limit("month", 1.0, "confirm")}));
    spend_usd(f.td.path(), 2.0);
    f.script("sent after the yes");
    auto held = f.rt->chat().send("hello there", f.plain());
    REQUIRE(held);
    REQUIRE(held->usage_pending.has_value());
    const Json pending = *held->usage_pending;
    CHECK(pending.at("status") == "requires_confirmation");
    CHECK(pending.at("sent") == false);
    CHECK(pending.at("reason_codes") == Json::array({"spend_limit"}));
    REQUIRE(pending.at("limits").size() == 1);
    const Json& l = pending.at("limits")[0];
    CHECK(l.at("period") == "month");
    CHECK(l.at("on_exceed") == "confirm");
    CHECK(l.at("code") == "spend_limit_already_over");
    CHECK(l.at("reason").at("en").get<std::string>().find("monthly spending limit of 1 USD is already used up") != std::string::npos);
    CHECK(l.at("reason").at("pl").get<std::string>().find("miesięczny") != std::string::npos);
    CHECK(l.at("spent_usd") == 2.0);
    CHECK(f.model_calls() == 0);
    CHECK(held->assistant_message_id.empty());

    // a no: nothing is sent either
    auto no = f.rt->chat().send("hello there", Fixture::with_answer(f.plain(), pending, false));
    REQUIRE(no);
    REQUIRE(no->usage_pending.has_value());
    CHECK(no->usage_pending->at("status") == "denied");
    CHECK(f.model_calls() == 0);

    // a yes (a fresh hold, then its receipt): sent exactly once
    auto again = f.rt->chat().send("hello there", f.plain());
    REQUIRE(again);
    REQUIRE(again->usage_pending.has_value());
    auto sent = f.rt->chat().send("hello there", Fixture::with_answer(f.plain(), *again->usage_pending, true));
    REQUIRE(sent);
    CHECK_FALSE(sent->usage_pending.has_value());
    CHECK(sent->text == "sent after the yes");
    CHECK(f.model_calls() == 1);
  }

  TEST_CASE("the receipt binds the decision, not the numbers of the moment: more spend before the yes does not void it") {
    Fixture f;
    f.limits(Json::array({limit("month", 1.0, "confirm")}));
    spend_usd(f.td.path(), 2.0);
    f.script("ok");
    auto held = f.rt->chat().send("hello", f.plain());
    REQUIRE(held);
    REQUIRE(held->usage_pending.has_value());
    spend_usd(f.td.path(), 0.5, "another");
    auto sent = f.rt->chat().send("hello", Fixture::with_answer(f.plain(), *held->usage_pending, true));
    REQUIRE(sent);
    CHECK_FALSE(sent->usage_pending.has_value());
    CHECK(f.model_calls() == 1);
  }

  TEST_CASE("deny refuses with the reason, notify sends and tells, and a limit that is not passed leaves the send alone") {
    {
      Fixture f;
      f.limits(Json::array({limit("month", 1.0, "deny")}));
      spend_usd(f.td.path(), 2.0);
      auto refused = f.rt->chat().send("hello", f.plain());
      REQUIRE(refused);
      REQUIRE(refused->usage_pending.has_value());
      const Json& p = *refused->usage_pending;
      CHECK(p.at("status") == "denied");
      CHECK(p.at("limits")[0].at("on_exceed") == "deny");
      // the reason says what was set and how to change it
      CHECK(p.at("limits")[0].at("reason").at("en").get<std::string>().find("You set this limit to refuse") != std::string::npos);
      CHECK(p.at("limits")[0].at("reason").at("pl").get<std::string>().find("odmawiać") != std::string::npos);
      CHECK(f.model_calls() == 0);
    }
    {
      Fixture f;
      f.limits(Json::array({limit("month", 1.0, "notify")}));
      spend_usd(f.td.path(), 2.0);
      f.script("went through");
      auto told = f.rt->chat().send("hello", f.plain());
      REQUIRE(told);
      CHECK_FALSE(told->usage_pending.has_value());
      CHECK(told->text == "went through");
      REQUIRE(told->usage_notices.is_array());
      REQUIRE(told->usage_notices.size() == 1);
      CHECK(told->usage_notices[0].at("code") == "spend_limit_notice");
      CHECK(told->usage_notices[0].at("text").at("en").get<std::string>().rfind("Heads up:", 0) == 0);
      CHECK(told->usage_notices[0].at("text").at("pl").get<std::string>().rfind("Uwaga:", 0) == 0);
      // kept with the answer
      auto stored = unwrap(f.rt->db().get_msg(told->assistant_message_id));
      REQUIRE(stored.has_value());
      CHECK(stored->metadata.at("usage_notices")[0].at("period") == "month");
      CHECK(f.model_calls() == 1);
    }
    {
      Fixture f;
      f.limits(Json::array({limit("month", 100.0, "deny")}));
      spend_usd(f.td.path(), 2.0);
      f.script("fine");
      auto ok = f.rt->chat().send("hello", f.plain());
      REQUIRE(ok);
      CHECK_FALSE(ok->usage_pending.has_value());
      CHECK(ok->usage_notices.is_null());
      CHECK(f.model_calls() == 1);
    }
  }

  TEST_CASE("every period counts: the day, the week and the month are separate limits, the nearest passed one holds") {
    Fixture f;
    f.limits(Json::array({limit("month", 100.0, "confirm"), limit("day", 1.0, "deny")}));
    spend_usd(f.td.path(), 2.0);
    auto refused = f.rt->chat().send("hello", f.plain());
    REQUIRE(refused);
    REQUIRE(refused->usage_pending.has_value());
    CHECK(refused->usage_pending->at("status") == "denied");
    REQUIRE(refused->usage_pending->at("limits").size() == 1);
    CHECK(refused->usage_pending->at("limits")[0].at("period") == "day");
  }

  TEST_CASE("an admission that is no model use is never stopped by a limit; one that knows its cost counts it") {
    Fixture f;
    f.limits(Json::array({limit("month", 1.0, "deny")}));
    spend_usd(f.td.path(), 2.0);
    auto policy = unwrap(UsagePolicy::open(f.td.path() / "usage-policy.sqlite", unwrap(effective_usage_policy_options(f.rt->config()))));
    // importing a file: bytes only, no model
    const Json import = unwrap(policy->request(Json{{"operation_id", "import_1"}, {"baseline_key", "import|archive"}, {"resources", Json{{"source_bytes", 1000}}}}));
    CHECK(import.at("status") == "allowed");
    CHECK_FALSE(import.contains("limits"));
    // a model call is refused (the period is used up)
    const Json call = unwrap(policy->request(Json{{"operation_id", "call_1"}, {"baseline_key", "k|m"}, {"model", "m"}, {"resources", Json{{"requests", 1}}}}));
    CHECK(call.at("status") == "denied");
    CHECK(call.at("authorized") == false);
    CHECK(call.at("remaining").empty());
    CHECK(call.at("reason_codes") == Json::array({"spend_limit"}));
    // the same refused operation id stays refused (idempotent), a new one is asked again
    CHECK(unwrap(policy->request(Json{{"operation_id", "call_1"}, {"baseline_key", "k|m"}, {"model", "m"}, {"resources", Json{{"requests", 1}}}})).at("status") == "denied");
  }

  TEST_CASE("a call that knows its cost: spent + the estimate is what is compared with the limit") {
    Fixture f;
    f.limits(Json::array({limit("week", 1.0, "confirm")}));
    spend_usd(f.td.path(), 0.8);
    auto policy = unwrap(UsagePolicy::open(f.td.path() / "usage-policy.sqlite", unwrap(effective_usage_policy_options(f.rt->config()))));
    const Json small = unwrap(policy->request(Json{{"operation_id", "small"}, {"baseline_key", "k|m"}, {"model", "m"}, {"resources", Json{{"requests", 1}, {"cost_usd", 0.1}}}}));
    CHECK(small.at("status") == "allowed");
    const Json big = unwrap(policy->request(Json{{"operation_id", "big"}, {"baseline_key", "k|m"}, {"model", "m"}, {"resources", Json{{"requests", 1}, {"cost_usd", 0.5}}}}));
    CHECK(big.at("status") == "requires_confirmation");
    const Json& row = big.at("limit_report")[0];
    CHECK(row.at("code") == "spend_limit_would_exceed");
    CHECK(row.at("estimate_usd") == 0.5);
    CHECK(row.at("already_over") == false);
    CHECK(row.at("reason").at("en").get<std::string>().find("This request (about 0.50 USD)") != std::string::npos);
    // the receipt confirms and the call is then admitted
    const Json yes = unwrap(policy->confirm("big", big.at("receipt_id").get<std::string>(), true, "test"));
    CHECK(yes.at("status") == "allowed");
    CHECK(yes.at("authorized") == true);
    // a record read back from the ledger still verifies (the receipt covers the limits)
    CHECK(unwrap(policy->inspect("k|m")).dump().find("spend_limit") != std::string::npos);
  }

  TEST_CASE("the stream says it: a refusal and a hold are usage_pending chunks with the reason, a notice comes before the end") {
    {
      Ctx c("heuristic", Json::array({limit("month", 1.0, "deny")}));
      spend_usd(c.dir.path(), 2.0);
      Chunks chunks;
      c.chat(Json{{"message", "hello there"}, {"stream", false}}, chunks);
      const Json* pending = chunks.first("usage_pending");
      REQUIRE(pending != nullptr);
      CHECK(pending->at("usage_pending").at("status") == "denied");
      CHECK(pending->at("usage_pending").at("limits")[0].at("reason").at("en").get<std::string>().find("spending limit") != std::string::npos);
      CHECK(chunks.first("done") == nullptr);
      CHECK(c.chat_posts() == 0);
    }
    {
      Ctx c("heuristic", Json::array({limit("month", 1.0, "notify")}));
      spend_usd(c.dir.path(), 2.0);
      c.wire.replies.push_back({200, reply("through").dump()});
      Chunks chunks;
      c.chat(Json{{"message", "hello there"}, {"stream", false}}, chunks);
      const auto types = chunks.types();
      REQUIRE(chunks.first("usage_notice") != nullptr);
      REQUIRE(chunks.first("done") != nullptr);
      CHECK(chunks.first("usage_notice")->at("notices")[0].at("code") == "spend_limit_notice");
      std::size_t notice_at = 0, done_at = 0;
      for (std::size_t i = 0; i < types.size(); ++i) {
        if (types[i] == "usage_notice") notice_at = i;
        if (types[i] == "done") done_at = i;
      }
      CHECK(notice_at < done_at);
      CHECK(c.chat_posts() == 1);
    }
  }

  TEST_CASE("the routing model call obeys the limit: deny falls back to the local method with the reason, confirm holds with a receipt") {
    {
      Ctx c("model", Json::array({limit("month", 1.0, "deny")}));
      spend_usd(c.dir.path(), 2.0);
      Chunks chunks;
      c.chat(Json{{"message", "just a sentence"}, {"stream", false}}, chunks);
      // the routing model was not asked and the message was not sent either (the same limit refuses the chat send): both say why
      const Json* issue = chunks.first("route_issue");
      REQUIRE(issue != nullptr);
      CHECK(issue->at("issues")[0].at("code") == "model_route_failed");
      CHECK(issue->at("issues")[0].at("message").get<std::string>().find("spending limit") != std::string::npos);
      REQUIRE(chunks.first("usage_pending") != nullptr);
      CHECK(chunks.first("usage_pending")->at("usage_pending").at("status") == "denied");
      CHECK(c.chat_posts() == 0);
    }
    {
      Ctx c("model", Json::array({limit("month", 1.0, "confirm")}));
      spend_usd(c.dir.path(), 2.0);
      Chunks chunks;
      c.chat(Json{{"message", "just a sentence"}, {"stream", false}}, chunks);
      const Json* pending = chunks.first("usage_pending");
      REQUIRE(pending != nullptr);
      CHECK(pending->at("usage_pending").at("stage") == "route_intent");
      CHECK(pending->at("usage_pending").at("status") == "requires_confirmation");
      CHECK(pending->at("usage_pending").at("limits")[0].at("period") == "month");
      CHECK(c.chat_posts() == 0);
    }
  }
}
