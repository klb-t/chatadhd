// P17 3: the x10 question of the background analysis is asked in the conversation. When the semantic worker is held by the guard, the conversation whose
// message was being analysed shows the guard's question with its receipt; "yes" / "no" as the last thing said answers it through the worker's usage
// confirmation (what POST /api/semantic/usage-confirm does); an answer from another surface closes the question there.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/semantic_worker.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

constexpr const char* kBatches = "https://offline.invalid/batches";

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id;
  std::vector<Json> events;

  explicit Fixture(const std::string& sample_text = "A short sample message") {
    // the worker takes the batch path for any number of messages, against a local endpoint (the overlay is read when the runtime opens)
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(td.path() / "profiles" / "worker.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "worker"},
                {"overrides", Json{{"batch_threshold", 0}, {"batch_endpoint", kBatches}, {"batch_secret_key", "local_batch_key"}}}}.dump();
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("semantic_model", "vendor/model");
    rt->config().set("auto_title", false);
    rt->secrets().set("api_key", "fake-model-key");
    rt->secrets().set("local_batch_key", "fake-batch-key");
    for (int i = 0; i < 6; ++i)
      transport->expect("POST", kBatches, net::ScriptedTransport::Reply::json(201, Json{{"id", "offline-batch"}}));
    // the baseline of the batch cohort: three small batches of one message
    {
      auto policy = unwrap(UsagePolicy::open(td.path() / "usage-policy.sqlite", Json::object()));
      const std::string key = usage_baseline_key(rt->config(), "semantic.batch", kBatches, "model");
      for (int i = 0; i < 3; ++i) {
        const std::string op = "baseline_" + std::to_string(i);
        unwrap(policy->request(Json{{"operation_id", op}, {"baseline_key", key}, {"model", "model"}, {"resources", Json{{"messages", 1}}}}));
        unwrap(policy->complete(op, Json{{"resources", Json{{"messages", 1}}}, {"provenance", "instrument_measured"}}));
      }
    }
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    add("user", sample_text);
    rt->bus().on("semantic:question", [this](std::string_view, const Json& d) { events.push_back(d); });
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  // enough pending messages to be x10 the baseline of one
  void pile(const std::string& text, int n = 60) {
    for (int i = 0; i < n; ++i) add("user", text + " " + std::to_string(i));
  }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  std::vector<Message> messages() { return unwrap(rt->db().get_msgs(conv_id)); }
  // The message that asks the question, if the conversation shows one.
  std::optional<Message> question() {
    std::optional<Message> found;
    for (auto& m : messages())
      if (m.metadata.is_object() && m.metadata.contains("semantic_question")) found = m;
    return found;
  }
  std::size_t batch_posts() {
    std::size_t n = 0;
    for (const auto& r : transport->requests())
      if (r.method == "POST" && r.url == kBatches) ++n;
    return n;
  }
};

}  // namespace

TEST_SUITE("semantic_question") {
  TEST_CASE("a held background analysis asks in the conversation, once, with the guard's receipt and the growth in words") {
    Fixture f;
    f.pile("Another message about the Friday release");
    CHECK_FALSE(f.question().has_value());
    CHECK(unwrap(f.rt->worker().drain_once()) == 0);
    const auto pending = f.rt->worker().status().usage_pending;
    REQUIRE(pending.has_value());
    const auto asked = f.question();
    REQUIRE(asked.has_value());
    CHECK(asked->role == "assistant");
    const Json q = asked->metadata.at("semantic_question");
    CHECK(q.at("status") == "pending");
    CHECK(q.at("receipt_id") == pending->at("receipt_id"));
    CHECK(q.at("operation_id") == pending->at("operation_id"));
    CHECK(q.at("receipt").at("status") == "requires_confirmation");
    CHECK(q.at("lang") == "en");
    // "about 61 times more than usual": the ratio the guard saw, in words
    CHECK(asked->text.find("Background analysis") == std::string::npos);  // the sentence is the user's language, not a title
    CHECK(asked->text.find("The background analysis wants to use about 61 times more than usual (61 messages) - continue?") == 0);
    // asked once: another pass over the same held batch does not ask again
    CHECK(unwrap(f.rt->worker().drain_once()) == 0);
    int questions = 0;
    for (const auto& m : f.messages()) questions += m.metadata.is_object() && m.metadata.contains("semantic_question");
    CHECK(questions == 1);
    // the page is told with ids only
    REQUIRE(f.events.size() == 1);
    CHECK(f.events[0].at("state") == "waiting");
    CHECK(f.events[0].at("conv_id") == f.conv_id);
    CHECK(f.events[0].at("message_id") == asked->id);
    CHECK_FALSE(f.events[0].contains("text"));
    // the question is not content: it is not analysed itself
    CHECK(asked->semantic_status == "done");
  }

  TEST_CASE("\"yes\" answers it through the worker with the receipt: the analysis goes on") {
    Fixture f;
    f.pile("Another message about the Friday release");
    CHECK(unwrap(f.rt->worker().drain_once()) == 0);
    REQUIRE(f.question().has_value());
    const std::string receipt = f.question()->metadata.at("semantic_question").at("receipt_id");
    CHECK(f.batch_posts() == 0);
    const auto answered = f.input("yes");
    REQUIRE(answered.handled);
    CHECK(answered.result.at("text").get<std::string>() == "Right - the background analysis goes on.");
    const Json q = f.question()->metadata.at("semantic_question");
    CHECK(q.at("status") == "approved");
    CHECK(q.at("answered").at("ref").get<std::string>().rfind("chat:", 0) == 0);
    CHECK(f.events.back().at("state") == "closed");
    CHECK(f.events.back().at("status") == "approved");
    // the worker took the confirmation: the next pass submits the batch, once
    CHECK(unwrap(f.rt->worker().drain_once()) == 0);
    CHECK(f.batch_posts() == 1);
    CHECK_FALSE(f.rt->worker().status().usage_pending.has_value());
    // a later "yes" is an ordinary input again: nothing waits
    CHECK_FALSE(f.input("yes").handled);
    CHECK(receipt == q.at("receipt_id").get<std::string>());
  }

  TEST_CASE("\"no\" declines it: nothing is sent, the worker is paused, and the conversation says how to resume") {
    Fixture f;
    f.pile("Wiadomość o piątkowym wydaniu");
    CHECK(unwrap(f.rt->worker().drain_once()) == 0);
    REQUIRE(f.question().has_value());
    const auto answered = f.input("nie");
    REQUIRE(answered.handled);
    CHECK(f.question()->metadata.at("semantic_question").at("status") == "declined");
    CHECK(f.rt->worker().status().paused);
    CHECK(f.batch_posts() == 0);
    CHECK(answered.result.at("text").get<std::string>().find("wstrzymana") != std::string::npos);
  }

  TEST_CASE("the question speaks the language of the conversation, PL as well as EN") {
    Fixture f("Cześć, to jest wiadomość o piątkowym wydaniu");
    f.pile("Wiadomość o piątkowym wydaniu");
    CHECK(unwrap(f.rt->worker().drain_once()) == 0);
    const auto asked = f.question();
    REQUIRE(asked.has_value());
    CHECK(asked->metadata.at("semantic_question").at("lang") == "pl");
    CHECK(asked->text == "Analiza w tle chce zużyć ok. 61 razy więcej niż zwykle (61 wiadomości) - kontynuować?");
    const auto answered = f.input("tak");
    REQUIRE(answered.handled);
    CHECK(answered.result.at("text").get<std::string>() == "Dobrze - analiza w tle jedzie dalej.");
  }

  TEST_CASE("an answer from another surface (the status panel, POST /api/semantic/usage-confirm) closes the question in the conversation") {
    Fixture f;
    f.pile("Another message about the Friday release");
    CHECK(unwrap(f.rt->worker().drain_once()) == 0);
    const std::string receipt = f.question()->metadata.at("semantic_question").at("receipt_id");
    unwrap(f.rt->worker().confirm_usage(true, receipt, "web"));
    const Json q = f.question()->metadata.at("semantic_question");
    CHECK(q.at("status") == "approved");
    CHECK(q.at("answered").at("ref") == "web");
    // the question no longer waits, so a "yes" typed now is not an answer to it
    CHECK_FALSE(f.input("yes").handled);
  }

  TEST_CASE("a newer hold closes the older question as out of date (the worker was restarted: its old receipt cannot be answered any more)") {
    Fixture f;
    auto hold = [&](const char* receipt) {
      f.rt->bus().emit(events::kSemanticUsagePending, Json{{"schema", "loom.semantic_usage_pending/1"}, {"status", "requires_confirmation"}, {"receipt_id", receipt},
                                                           {"operation_id", std::string("op_") + receipt}, {"conv_ids", Json::array({f.conv_id})}, {"messages", 3},
                                                           {"projected", Json{{"messages", Json{{"requires_confirmation", true}, {"ratio", 30.0}}}}}});
    };
    hold("usage_first");
    hold("usage_first");  // the same receipt: asked once
    hold("usage_second");
    int questions = 0, pending = 0, stale = 0;
    for (const auto& m : f.messages())
      if (m.metadata.is_object() && m.metadata.contains("semantic_question")) {
        ++questions;
        const std::string status = m.metadata["semantic_question"].at("status");
        pending += status == "pending";
        stale += status == "stale";
        if (status == "pending") CHECK(m.metadata["semantic_question"].at("receipt_id").get<std::string>() == "usage_second");
      }
    CHECK(questions == 2);
    CHECK(pending == 1);
    CHECK(stale == 1);
    // the page was told: the old question closed, the new one waiting
    REQUIRE(f.events.size() == 3);
    CHECK(f.events[0].at("state") == "waiting");
    CHECK(f.events[1].at("state") == "closed");
    CHECK(f.events[1].at("status") == "stale");
    CHECK(f.events[2].at("state") == "waiting");
  }

  TEST_CASE("\"yes\" to a question the worker does not hold says so and closes it; nothing is confirmed") {
    Fixture f;
    f.rt->bus().emit(events::kSemanticUsagePending, Json{{"schema", "loom.semantic_usage_pending/1"}, {"status", "requires_confirmation"}, {"receipt_id", "usage_made_up"},
                                                         {"operation_id", "op_made_up"}, {"conv_ids", Json::array({f.conv_id})}, {"messages", 3},
                                                         {"projected", Json{{"messages", Json{{"requires_confirmation", true}, {"ratio", 12.4}}}}}});
    REQUIRE(f.question().has_value());
    CHECK(f.question()->text.find("about 12 times more than usual") != std::string::npos);
    const auto answered = f.input("yes");
    REQUIRE(answered.handled);
    CHECK(answered.result.at("text").get<std::string>().find("no longer current") != std::string::npos);
    CHECK(f.question()->metadata.at("semantic_question").at("status") == "stale");
    CHECK_FALSE(f.rt->worker().status().usage_pending.has_value());
  }

  TEST_CASE("without a conversation to show it in, nothing is posted and the worker still has the question") {
    Fixture f;
    chat_route::note_semantic_hold(*f.rt, Json{{"receipt_id", "usage_x"}, {"operation_id", "op"}, {"projected", Json::object()}});
    CHECK_FALSE(f.question().has_value());
    chat_route::note_semantic_hold(*f.rt, Json{{"receipt_id", "usage_y"}, {"conv_ids", Json::array({"c_gone"})}, {"projected", Json::object()}});
    CHECK_FALSE(f.question().has_value());
  }
}
