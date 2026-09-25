#include <doctest/doctest.h>

#include <atomic>
#include <stdexcept>
#include <thread>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/provenance.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

TEST_SUITE("event_bus") {
  TEST_CASE("on / emit / off, constants match Python") {
    CHECK(events::kMsgCreated == "message:created");
    CHECK(events::kMsgUpdated == "message:updated");
    CHECK(events::kNodeCreated == "node:created");
    CHECK(events::kEdgeCreated == "edge:created");
    CHECK(events::kConvCreated == "conv:created");
    CHECK(events::kConvSwitched == "conv:switched");
    CHECK(events::kGraphChanged == "graph:changed");
    CHECK(events::kImportDone == "import:done");
    CHECK(events::kSemanticProgress == "semantic:progress");

    EventBus bus;
    std::vector<std::string> got;
    auto id = bus.on("message:created", [&](std::string_view, const Json& d) { got.push_back(d["id"]); });
    CHECK(id != 0);
    bus.emit("message:created", Json{{"id", "m_1"}});
    bus.emit("other", Json{{"id", "x"}});
    CHECK(got == std::vector<std::string>{"m_1"});
    CHECK(bus.handler_count("message:created") == 1);
    CHECK(bus.off(id));
    CHECK(!bus.off(id));
    bus.emit("message:created", Json{{"id", "m_2"}});
    CHECK(got.size() == 1);
    bus.emit("no:subscribers");  // data defaults to null
  }

  TEST_CASE("handler exceptions are contained; later handlers still run") {
    EventBus bus;
    int calls = 0;
    bus.on("e", [](std::string_view, const Json&) { throw std::runtime_error("boom"); });
    bus.on("e", [&](std::string_view, const Json&) { calls++; });
    bus.emit("e");
    CHECK(calls == 1);
  }

  TEST_CASE("wildcard, re-entrancy and scoped subscriptions") {
    EventBus bus;
    std::vector<std::string> names;
    bus.on("*", [&](std::string_view ev, const Json&) { names.emplace_back(ev); });
    EventBus::SubscriptionId self = 0;
    int self_calls = 0;
    self = bus.on("a", [&](std::string_view, const Json&) {
      self_calls++;
      bus.off(self);         // unsubscribe itself during emit
      bus.emit("b");         // nested emit
    });
    bus.emit("a");
    bus.emit("a");
    CHECK(self_calls == 1);
    CHECK(names == std::vector<std::string>{"a", "b", "a"});
    {
      int n = 0;
      ScopedSubscription sub(bus, bus.on("c", [&](std::string_view, const Json&) { n++; }));
      bus.emit("c");
      CHECK(n == 1);
    }
    CHECK(bus.handler_count("c") == 0);
  }

  TEST_CASE("thread safety: concurrent emit + subscribe/unsubscribe") {
    EventBus bus;
    std::atomic<long> received{0};
    std::atomic<bool> stop{false};
    bus.on("tick", [&](std::string_view, const Json&) { received++; });
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
      ts.emplace_back([&] {
        for (int i = 0; i < 2000; ++i) bus.emit("tick", Json{{"i", i}});
      });
    }
    ts.emplace_back([&] {
      while (!stop.load()) {
        auto id = bus.on("tick", [](std::string_view, const Json&) {});
        bus.off(id);
      }
    });
    for (int t = 0; t < 4; ++t) ts[static_cast<std::size_t>(t)].join();
    stop = true;
    ts.back().join();
    CHECK(received.load() == 8000);
  }

  TEST_CASE("selected events are persisted to the EventLog") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "e.db");
    EventLog log(*db);
    EventBus bus;
    bus.set_persistence(&log, {"import:done", "task:*"});
    bus.emit("import:done", Json{{"id", "c_1"}, {"count", 3}});
    bus.emit("message:created", Json{{"id", "m_1"}});
    bus.emit("task:changed", Json{{"id", "t_1"}});
    bus.emit("import:done", Json("scalar"));
    auto evs = unwrap(log.query());
    REQUIRE(evs.size() == 3);
    CHECK(evs[0].type == "import:done");
    CHECK(evs[0].subject_id == "c_1");
    CHECK(evs[0].payload["count"] == 3);
    CHECK(evs[1].type == "task:changed");
    CHECK(evs[2].payload["data"] == "scalar");
    bus.set_persistence(nullptr, {});
    bus.emit("import:done", Json::object());
    CHECK(unwrap(log.query()).size() == 3);
  }
}
