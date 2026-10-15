// P14 2: actions.confirm of a run the usage guard holds (x10 growth) tells the conversation how it went, as the typed "tak" does:
// the result is a message, the question of the earlier message is closed, the proposal is settled. A decline is said as well.
#include <doctest/doctest.h>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    loom::test::measured_baseline_only(rt->config());  // P17 6: this test is about the x10 hold over three tiny samples, so the floor of the baseline is off
    rt->config().set("model_registry_autorefresh", "off");  // these tests count the transport's requests; fetching the model list for a price is tested in test_estimate_registry
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    add("user", "We should ship on Friday.");
    add("assistant", "Agreed, Friday works if the tests pass.");
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void script(int n = 1) {
    for (int i = 0; i < n; ++i)
      transport->expect("POST", "https://api.test/chat/completions",
                        net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", "The model's answer."}}}}})},
                                                                       {"usage", Json{{"total_tokens", 7}}}}));
  }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions o;
    o.conv_id = conv_id;
    o.stream = false;
    return unwrap(chat_route::handle(*rt, text, o, Json{{"message", text}}, nullptr));
  }
  Message message(const std::string& id) { return *unwrap(rt->db().get_msg(id)); }
  std::size_t count() { return unwrap(rt->db().get_msgs(conv_id)).size(); }
  // Three small summaries set the baseline of the usage guard; the next one on a conversation 20000 times as big is x10 growth.
  void baseline() {
    script(3);
    for (int i = 0; i < 3; ++i)
      REQUIRE(unwrap(rt->actions().run(Json{{"action", "summarise"}, {"element", conv_id}})).at("status") == "completed");
    REQUIRE(transport->requests().size() == 3);
    add("user", std::string(60000, 'x'));
  }
  // "Streść tę rozmowę" + "tak": the run is held by the guard, the conversation shows the question.
  struct Held {
    chat_route::Outcome asked, yes;
    Json run;  // the action_run block of the message that shows the question
  };
  Held hold() {
    Held h;
    h.asked = input("Streść tę rozmowę");
    REQUIRE(h.asked.handled);
    h.yes = input("tak");
    REQUIRE(h.yes.handled);
    h.run = message(h.yes.assistant_message_id).metadata.at("action_run");
    return h;
  }
};

}  // namespace

TEST_SUITE("confirm_usage_hold") {
  TEST_CASE("confirming the usage hold through actions.confirm posts the result, closes the question and settles the proposal") {
    Fixture f;
    f.baseline();
    const auto h = f.hold();
    INFO(h.run.dump());
    REQUIRE(h.run.at("status") == "awaiting_usage");
    REQUIRE(h.run.at("pending").at("kind") == "usage");
    CHECK(f.message(h.asked.assistant_message_id).metadata.at("action_proposal").at("status") == "waiting");
    CHECK(f.transport->requests().size() == 3);  // nothing left the machine
    const std::size_t before = f.count();
    // a wrong receipt answers nothing and says nothing
    CHECK_FALSE(chat_route::confirm(*f.rt, Json{{"run_id", h.run.at("run_id")}, {"approved", true}, {"receipt_id", "usage_made_up"}, {"ref", "web"}}));
    CHECK(f.count() == before);
    f.script();
    const Json done = unwrap(chat_route::confirm(
        *f.rt, Json{{"run_id", h.run.at("run_id")}, {"approved", true}, {"receipt_id", h.run.at("pending").at("receipt_id")}, {"ref", "web"}}));
    INFO(done.dump());
    CHECK(done.at("status") == "completed");
    CHECK(f.transport->requests().size() == 4);
    // the result is a message of the conversation
    REQUIRE(done.contains("message_id"));
    CHECK(f.count() == before + 1);
    const Message result = f.message(done.at("message_id"));
    CHECK(result.role == "assistant");
    CHECK(result.text.find("The model's answer.") != std::string::npos);
    CHECK(result.metadata.at("action_run").at("status") == "completed");
    CHECK(result.metadata.at("action_run").at("run_id") == h.run.at("run_id"));
    // the question is closed where it was asked, the proposal says how it ended
    const Message asked = f.message(h.yes.assistant_message_id);
    CHECK(asked.metadata.at("action_run").at("pending").is_null());
    CHECK(asked.metadata.at("action_run").at("status") == "completed");
    CHECK(f.message(h.asked.assistant_message_id).metadata.at("action_proposal").at("status") == "executed");
    // the run cannot be answered twice
    CHECK_FALSE(chat_route::confirm(*f.rt, Json{{"run_id", h.run.at("run_id")}, {"approved", true}, {"receipt_id", h.run.at("pending").at("receipt_id")}}));
    CHECK(f.count() == before + 1);
  }

  TEST_CASE("declining the usage hold through actions.confirm says so in the conversation; nothing is sent") {
    Fixture f;
    f.baseline();
    const auto h = f.hold();
    REQUIRE(h.run.at("status") == "awaiting_usage");
    const std::size_t before = f.count();
    const Json no = unwrap(chat_route::confirm(
        *f.rt, Json{{"run_id", h.run.at("run_id")}, {"approved", false}, {"receipt_id", h.run.at("pending").at("receipt_id")}, {"ref", "web"}}));
    CHECK(no.at("status") == "rejected");
    CHECK(f.transport->requests().size() == 3);
    REQUIRE(no.contains("message_id"));
    CHECK(f.count() == before + 1);
    CHECK(f.message(no.at("message_id")).metadata.at("action_run").at("status") == "rejected");
    CHECK(f.message(h.yes.assistant_message_id).metadata.at("action_run").at("pending").is_null());
    CHECK(f.message(h.asked.assistant_message_id).metadata.at("action_proposal").at("status") == "rejected");
  }

  TEST_CASE("a run outside any conversation is the plain action: no message anywhere") {
    Fixture f;
    f.baseline();
    const Json held = unwrap(f.rt->actions().run(Json{{"action", "summarise"}, {"element", f.conv_id}}));
    REQUIRE(held.at("status") == "awaiting_usage");
    const std::size_t before = f.count();
    f.script();
    const Json done = unwrap(chat_route::confirm(
        *f.rt, Json{{"run_id", held.at("id")}, {"approved", true}, {"receipt_id", held.at("pending").at("receipt_id")}, {"ref", "api"}}));
    CHECK(done.at("status") == "completed");
    CHECK_FALSE(done.contains("message_id"));
    CHECK(f.count() == before);
  }
}
