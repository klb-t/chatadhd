// P12 3: the incremental index behind the detection of repeated sequences and of the classes an action is used on. It must give the
// counts and tails that rereading the whole log would, read each checkpoint once, and survive a restart (a snapshot in the run log).
#include <doctest/doctest.h>

#include <map>
#include <random>

#include "loom/action_index.h"
#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::actions;
using loom::test::unwrap;

namespace {

std::string stamp(int second) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "2026-10-14T10:%02d:%02d.000000Z", second / 60, second % 60);
  return buf;
}

Json run_doc(const std::string& id, const std::string& action, const std::string& cls, const std::string& element, const std::string& conv,
             const std::vector<std::string>& made, int second, const std::string& status = "completed") {
  Json items = Json::array();
  for (const auto& m : made) items.push_back(Json{{"form", m}});
  return Json{{"id", id}, {"status", status}, {"created", stamp(second)}, {"updated", stamp(second)},
              {"request", Json{{"action", action}, {"context", Json{{"conv_id", conv}, {"message_id", "m_" + id}}}}},
              {"element", Json{{"id", element}, {"class", cls}}}, {"result", Json{{"produced", Json{{"items", items}}}}}};
}

const auto kContent = [](const std::string& action) { return action != "settings.set"; };

// What a full reread would count: every pipeline of every conversation.
std::map<std::string, std::size_t> recount(const std::vector<Json>& docs, const IndexSettings& s) {
  std::map<std::string, std::vector<SequenceItem>> by_conv;
  for (const auto& d : docs) {
    if (d.value("status", "") != "completed" || !kContent(d.at("request").at("action"))) continue;
    SequenceItem i;
    i.run_id = d.at("id");
    i.action = d.at("request").at("action");
    i.cls = d.at("element").at("class");
    i.element_id = d.at("element").at("id");
    i.conv = d.at("request").at("context").at("conv_id");
    i.created = d.at("created");
    i.updated = d.at("updated");
    for (const auto& row : d.at("result").at("produced").at("items")) i.produced.insert(row.at("form"));
    by_conv[i.conv].push_back(i);
  }
  std::map<std::string, std::size_t> count;
  for (const auto& [conv, list] : by_conv)
    for (std::size_t len = 2; len <= s.max_len; ++len)
      for (std::size_t from = 0; from + len <= list.size(); ++from)
        if (is_pipeline(list, from, len, s.gap_seconds)) ++count[chain_signature(list, from, len)];
  return count;
}

// A stream of runs in three conversations: mostly chains (each run works on what the one before made), sometimes not.
std::vector<Json> stream(std::size_t n, unsigned seed) {
  std::mt19937 rng(seed);
  const std::vector<std::string> actions{"summarise", "translate", "rephrase", "critique"};
  const std::vector<std::string> classes{"message", "conversation"};
  std::map<std::string, std::string> last_made;
  std::vector<Json> docs;
  for (std::size_t i = 0; i < n; ++i) {
    const std::string conv = "c_" + std::to_string(rng() % 3);
    const std::string id = "ar_" + std::to_string(i);
    const bool chained = rng() % 4 != 0 && last_made.count(conv);
    const std::string element = chained ? last_made[conv] : "e_" + std::to_string(rng() % 6);
    const std::string cls = classes[rng() % 2];
    const std::string made = "f_" + std::to_string(i);
    last_made[conv] = made;
    docs.push_back(run_doc(id, actions[rng() % actions.size()], cls, element, conv, {made}, static_cast<int>(i)));
  }
  return docs;
}

struct Rt {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  void open() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
  }
};

}  // namespace

TEST_SUITE("sequence_index") {
  TEST_CASE("the index counts what a full reread would: every chain of every conversation, and the tail of each") {
    IndexSettings s;
    s.chains_per_class = 100000;  // no eviction in this case
    s.convs = 100;
    for (unsigned seed : {1u, 7u, 42u}) {
      INFO("seed " << seed);
      const auto docs = stream(300, seed);
      auto store = make_memory_run_store();
      SequenceIndex index(*store);
      // fed in pieces, as the commands come: the result must not depend on how often it is refreshed
      for (std::size_t i = 0; i < docs.size(); ++i) {
        REQUIRE(store->save(docs[i]));
        if (i % 17 == 0) REQUIRE(index.refresh(kContent, s));
      }
      REQUIRE(index.refresh(kContent, s));
      const auto expected = recount(docs, s);
      REQUIRE_FALSE(expected.empty());
      std::size_t total = 0;
      for (const auto& [sig, n] : expected) {
        CHECK_MESSAGE(index.count(sig) == n, sig);
        total += n;
      }
      std::size_t held = 0;
      const Json state = index.state();
      for (auto it = state.at("classes").begin(); it != state.at("classes").end(); ++it) held += it.value().size();
      CHECK(held == total);  // nothing counted that a reread would not count
      // the tail of a conversation is its last max_len runs
      for (const char* conv : {"c_0", "c_1", "c_2"}) {
        std::vector<std::string> ids;
        for (const auto& d : docs)
          if (d.at("request").at("context").at("conv_id") == conv) ids.push_back(d.at("id"));
        const auto tail = index.tail(conv);
        REQUIRE(tail.size() == s.max_len);
        for (std::size_t k = 0; k < tail.size(); ++k) CHECK(tail[k].run_id == ids[ids.size() - tail.size() + k]);
      }
      // a refresh with nothing new reads nothing
      const auto before = index.stats().checkpoints_read;
      REQUIRE(index.refresh(kContent, s));
      CHECK(index.stats().checkpoints_read == before);
      CHECK(index.stats().checkpoints_read == docs.size());  // each checkpoint was read once
    }
  }

  TEST_CASE("only completed runs of content actions count; a run checkpointed again is folded once") {
    IndexSettings s;
    auto store = make_memory_run_store();
    SequenceIndex index(*store);
    REQUIRE(store->save(run_doc("ar_1", "summarise", "message", "m_1", "c", {"f_1"}, 1, "awaiting_confirmation")));
    REQUIRE(store->save(run_doc("ar_1", "summarise", "message", "m_1", "c", {"f_1"}, 2)));
    REQUIRE(store->save(run_doc("ar_1", "summarise", "message", "m_1", "c", {"f_1"}, 3)));  // a later checkpoint of the same run
    REQUIRE(store->save(run_doc("ar_2", "settings.set", "settings_field", "max_tokens", "c", {}, 4)));
    REQUIRE(store->save(run_doc("ar_3", "translate", "message", "f_1", "c", {"f_3"}, 5, "failed")));
    REQUIRE(index.refresh(kContent, s));
    CHECK(index.stats().runs_folded == 1);
    CHECK(index.tail("c").size() == 1);
    CHECK(index.classes_of("summarise") == std::vector<std::string>{"message"});
    CHECK(index.messages_of("summarise") == std::vector<std::string>{"m_ar_1"});
    CHECK(index.classes_of("settings.set").empty());
  }

  TEST_CASE("the rings are bounded: the oldest chain of a class falls out, and the count with it; so does the tail of the least recent conversation") {
    IndexSettings s;
    s.chains_per_class = 4;
    s.convs = 2;
    auto store = make_memory_run_store();
    SequenceIndex index(*store);
    int second = 0;
    // ten times the same two-step chain on one conversation: 10 occurrences happened, 4 are kept
    std::string prev = "e_0";
    for (int i = 0; i < 10; ++i) {
      const std::string a = "fa_" + std::to_string(i), b = "fb_" + std::to_string(i);
      REQUIRE(store->save(run_doc("a" + std::to_string(i), "summarise", "message", prev, "c_1", {a}, second++)));
      REQUIRE(store->save(run_doc("b" + std::to_string(i), "translate", "message", a, "c_1", {b}, second++)));
      prev = "e_" + std::to_string(i + 1);
    }
    REQUIRE(index.refresh(kContent, s));
    CHECK(index.count("message|summarise+translate") == 4);
    // three conversations, two tails kept: the first one that went quiet is the one forgotten
    REQUIRE(store->save(run_doc("x1", "summarise", "message", "e_x", "c_2", {"fx"}, second++)));
    REQUIRE(store->save(run_doc("x2", "summarise", "message", "e_y", "c_3", {"fy"}, second++)));
    REQUIRE(index.refresh(kContent, s));
    CHECK(index.tail("c_1").empty());
    CHECK(index.tail("c_2").size() == 1);
    CHECK(index.tail("c_3").size() == 1);
  }

  TEST_CASE("a restart loads the snapshot and folds only what came after it; the state is the one that was left") {
    IndexSettings s;
    s.snapshot_every = 10;
    const auto docs = stream(57, 3);
    auto store = make_memory_run_store();
    Json left;
    {
      SequenceIndex first(*store);
      for (std::size_t i = 0; i < docs.size(); ++i) {
        REQUIRE(store->save(docs[i]));
        if (i % 5 == 4) REQUIRE(first.refresh(kContent, s));
      }
      REQUIRE(first.refresh(kContent, s));
      CHECK(first.stats().snapshots >= 4);
      left = first.state();
    }
    SequenceIndex second(*store);  // a new process over the same log
    REQUIRE(second.refresh(kContent, s));
    CHECK(second.stats().loaded_from_snapshot == 1);
    CHECK(second.stats().checkpoints_read < 2 * s.snapshot_every);  // not the whole log
    CHECK(second.state().at("last_seq") == left.at("last_seq"));
    CHECK(second.state().at("classes") == left.at("classes"));
    CHECK(second.state().at("convs") == left.at("convs"));
    CHECK(second.state().at("actions") == left.at("actions"));
    // and it goes on from there: one more run is one more checkpoint read
    const auto read = second.stats().checkpoints_read;
    REQUIRE(store->save(run_doc("ar_new", "summarise", "message", "e_9", "c_0", {"f_new"}, 99)));
    REQUIRE(second.refresh(kContent, s));
    CHECK(second.stats().checkpoints_read == read + 1);
  }

  TEST_CASE("in the event log: the snapshot is found again after thousands of other events, and runs are read once") {
    Rt r;
    r.open();
    RunStore& store = r.rt->actions().store();
    IndexSettings s;
    s.snapshot_every = 3;
    const auto docs = stream(12, 5);
    for (const auto& d : docs) REQUIRE(store.save(d));
    REQUIRE(r.rt->actions().sequence_index().refresh(kContent, s));
    CHECK(r.rt->actions().sequence_index().stats().snapshots >= 1);
    const Json left = r.rt->actions().sequence_index().state();
    for (int i = 0; i < 4300; ++i) unwrap(r.rt->event_log().append("filler", "x", Json::object()));  // further back than the first look
    SequenceIndex fresh(store);
    REQUIRE(fresh.refresh(kContent, s));
    CHECK(fresh.stats().loaded_from_snapshot == 1);
    CHECK(fresh.state().at("classes") == left.at("classes"));
    CHECK(fresh.stats().checkpoints_read < docs.size());
  }

  TEST_CASE("through the conversation: a chain run twice before a restart and once after it is offered; each checkpoint is read once") {
    Rt r;
    r.open();
    const std::string conv = unwrap(r.rt->db().create_conv("Planning")).id;
    NewMessage seed;
    seed.conv_id = conv;
    seed.role = "user";
    seed.text = "We should ship on Friday.";
    unwrap(r.rt->db().create_msg(seed));
    r.rt->config().set("intent_routing_run", "always");
    r.rt->config().set("action_result_target", "conversation_message");
    auto script = [&](const std::string& text) {
      r.transport->expect("POST", "https://api.test/chat/completions",
                          net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", text}}}}})},
                                                                         {"usage", Json{{"total_tokens", 7}}}}));
    };
    auto input = [&](const std::string& text) {
      ChatOptions opts;
      opts.conv_id = conv;
      opts.stream = false;
      return unwrap(chat_route::handle(*r.rt, text, opts, Json{{"message", text}}, nullptr));
    };
    auto round = [&](int n) {
      script("Summary " + std::to_string(n));
      REQUIRE(input("Summarise the last message").handled);
      script("Zusammenfassung " + std::to_string(n));
      return input("Translate the last message into German");
    };
    CHECK_FALSE(round(0).result.at("routed").contains("follow_up"));
    CHECK_FALSE(round(1).result.at("routed").contains("follow_up"));
    const auto read_before_restart = r.rt->actions().sequence_index().stats().checkpoints_read;
    EventQuery q;
    q.type = "action.run";
    q.limit = 1'000'000;
    const std::size_t run_events = unwrap(r.rt->event_log().query(q)).size();
    CHECK(read_before_restart <= run_events);  // the log was never read twice over, however many commands followed each other
    // restart: a new Runtime over the same data directory
    r.rt.reset();
    r.open();
    r.rt->config().set("intent_routing_run", "always");
    r.rt->config().set("action_result_target", "conversation_message");
    const auto third = round(2);
    REQUIRE(third.result.at("routed").contains("follow_up"));  // the two rounds before the restart still count
    CHECK(r.rt->actions().sequence_index().stats().refreshes >= 1);
  }
}
