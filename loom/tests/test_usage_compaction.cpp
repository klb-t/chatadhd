// Usage ledger compaction: old completed operations are folded into per-cohort
// aggregates; the growth guard's baseline decisions stay identical, the file
// shrinks, and pending / confirmed / denied operations are never touched.
#include <doctest/doctest.h>

#include <fstream>

#include "loom/config.h"
#include "loom/loom.h"
#include "loom/sqlite.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

constexpr const char* kFar = "2999-01-01 00:00:00";   // "now" far enough that every event is old

Json resources(double bytes, double money) {
  return Json{{"requests", 1}, {"input_bytes", bytes}, {"money_usd", money}};
}
Json estimate(const std::string& op, const std::string& key, double bytes, double money = 0.001) {
  return Json{{"operation_id", op}, {"baseline_key", key}, {"resources", resources(bytes, money)}};
}
Json measured(double bytes, double money) {
  return Json{{"resources", resources(bytes, money)}, {"provenance", "instrument_measured"}};
}
void run_operation(UsagePolicy& p, const std::string& op, const std::string& key, double bytes, double money) {
  auto admitted = unwrap(p.request(estimate(op, key, bytes, money)));
  REQUIRE(admitted["status"] == "allowed");
  unwrap(p.complete(op, measured(bytes, money)));
}

struct Env {
  fsutil::TempDir dir;
  Config config{dir.path() / "config.json"};
  std::filesystem::path ledger = dir.path() / "usage-policy.sqlite";

  std::unique_ptr<UsagePolicy> open(const Json& options = Json::object()) { return unwrap(UsagePolicy::open(ledger, options)); }
  std::int64_t count(const std::string& sql, const std::string& arg = {}) {
    auto db = unwrap(sql::Connection::open(ledger));
    auto st = unwrap(db.prepare(sql));
    if (!arg.empty()) st.bind_all(arg);
    REQUIRE(unwrap(st.step()));
    return st.get_int(0);
  }
  std::uintmax_t bytes_on_disk() const {
    std::uintmax_t n = 0;
    for (const char* suffix : {"", "-wal"}) {
      std::error_code ec;
      const auto size = std::filesystem::file_size(std::filesystem::path(ledger.string() + suffix), ec);
      if (!ec) n += size;
    }
    return n;
  }
};

// Three cohorts: A has more samples than the window, B fewer, C holds every
// operation a compaction must leave alone.
struct Seeded {
  Env env;
  std::unique_ptr<UsagePolicy> policy;
  const std::string A = "chat.send|api|model-a", B = "chat.send|api|model-b", C = "semantic|api|model-c";
  const std::set<std::string> protected_ids{"c_pending", "c_unresolved", "c_held", "c_denied", "c_confirmed", "c_cancelled"};

  explicit Seeded(const Json& options = Json::object()) {
    policy = env.open(options);
    for (int i = 0; i < 100; ++i) run_operation(*policy, "a_" + std::to_string(i), A, 100 + (i % 7), 0.001 * (1 + i % 5));
    for (int i = 0; i < 10; ++i) run_operation(*policy, "b_" + std::to_string(i), B, 200 + i, 0.002);
    for (int i = 0; i < 6; ++i) run_operation(*policy, "c_done_" + std::to_string(i), C, 100 + i, 0.001);
    // Reserved and never completed.
    REQUIRE(unwrap(policy->request(estimate("c_pending", C, 105)))["status"] == "allowed");
    // Completed with an unknown dimension: still holds its reservation.
    REQUIRE(unwrap(policy->request(estimate("c_unresolved", C, 106)))["status"] == "allowed");
    unwrap(policy->complete("c_unresolved", Json{{"resources", Json{{"requests", 1}, {"input_bytes", 106}, {"money_usd", nullptr}}},
                                                  {"provenance", "instrument_measured"}}));
    // Waiting for the owner, refused by the owner, and approved by the owner.
    const auto held = unwrap(policy->request(estimate("c_held", C, 5000)));
    REQUIRE(held["status"] == "requires_confirmation");
    const auto denied = unwrap(policy->request(estimate("c_denied", C, 6000)));
    REQUIRE(denied["status"] == "requires_confirmation");
    unwrap(policy->confirm("c_denied", denied["receipt_id"].get<std::string>(), false, "owner"));
    const auto approved = unwrap(policy->request(estimate("c_confirmed", C, 7000)));
    REQUIRE(approved["status"] == "requires_confirmation");
    unwrap(policy->confirm("c_confirmed", approved["receipt_id"].get<std::string>(), true, "owner"));
    unwrap(policy->complete("c_confirmed", measured(7000, 0.001)));
    REQUIRE(unwrap(policy->request(estimate("c_cancelled", C, 107)))["status"] == "allowed");
    unwrap(policy->cancel("c_cancelled", "test"));
  }

  // What the growth guard would decide for a spread of next calls (receipts included).
  Json decisions() {
    Json out = Json::array();
    for (const auto& key : {A, B, C})
      for (const double bytes : {50.0, 105.0, 900.0, 1100.0, 1500.0, 99999.0}) {
        Json probe = estimate("probe", key, bytes, bytes / 100000.0);
        probe["resources"]["gpu_seconds"] = 3;   // a resource no cohort has measured
        out.push_back(unwrap(policy->preview(probe)));
      }
    return out;
  }
  Json protected_state() {
    Json out = Json::object();
    for (const auto& key : {C}) {
      const auto inspected = unwrap(policy->inspect(key));
      Json operations = Json::array(), events = Json::array();
      for (const auto& op : inspected["operations"])
        if (protected_ids.count(op["operation_id"].get<std::string>())) operations.push_back(op);
      for (const auto& event : inspected["events"])
        if (protected_ids.count(event["operation_id"].get<std::string>())) events.push_back(event);
      out["operations"] = operations;
      out["events"] = events;
    }
    return out;
  }
  Json settings(Json overrides = Json::object()) { return unwrap(usage_compaction_settings(env.config, env.dir.path(), &overrides)); }
};

}  // namespace

TEST_SUITE("usage_compaction") {
  TEST_CASE("the preset is data; layers replace top-level fields in order: profile overlay, config key, override") {
    Env env;
    const Json preset = unwrap(usage_compaction_settings(env.config, env.dir.path()));
    CHECK(preset["mode"] == "auto");
    CHECK(preset["older_than_days"] == 30);
    CHECK(preset["statuses"] == Json::array({"completed"}));
    CHECK(preset["reclaim_space"] == true);

    std::filesystem::create_directories(env.dir.path() / "profiles");
    std::ofstream(env.dir.path() / "profiles" / "usage_compaction.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "usage_compaction"},
                {"overrides", Json{{"older_than_days", 7}, {"mode", "manual"}}}}.dump();
    Json layered = unwrap(usage_compaction_settings(env.config, env.dir.path()));
    CHECK(layered["older_than_days"] == 7);
    CHECK(layered["mode"] == "manual");
    env.config.set("usage_ledger_compaction", Json{{"older_than_days", 5}});
    layered = unwrap(usage_compaction_settings(env.config, env.dir.path()));
    CHECK(layered["older_than_days"] == 5);
    CHECK(layered["mode"] == "manual");
    const Json override_settings{{"older_than_days", 0}, {"mode", "off"}};
    layered = unwrap(usage_compaction_settings(env.config, env.dir.path(), &override_settings));
    CHECK(layered["older_than_days"] == 0);
    CHECK(layered["mode"] == "off");
  }

  TEST_CASE("settings are validated; denied, pending and requires_confirmation can never be named as foldable") {
    Env env;
    for (const Json& bad : {Json{{"mode", "sometimes"}}, Json{{"older_than_days", -1}}, Json{{"older_than_days", "30"}},
                            Json{{"statuses", Json::array({"denied"})}}, Json{{"statuses", Json::array({"requires_confirmation"})}},
                            Json{{"statuses", Json::array({"allowed"})}}, Json{{"statuses", "completed"}},
                            Json{{"reclaim_space", "yes"}}, Json{{"auto_interval_hours", -3}}}) {
      INFO(bad.dump());
      CHECK_FALSE(usage_compaction_settings(env.config, env.dir.path(), &bad));
    }
    CHECK(usage_compaction_settings(env.config, env.dir.path(), nullptr));
    env.config.set("usage_ledger_compaction", "not an object");
    CHECK_FALSE(usage_compaction_settings(env.config, env.dir.path()));
  }

  TEST_CASE("baseline decisions are identical before and after; the file shrinks; protected operations are untouched") {
    Seeded s;
    const Json decisions_before = s.decisions();
    const Json protected_before = s.protected_state();
    const auto operations_before = s.env.count("SELECT COUNT(*) FROM usage_operations");
    REQUIRE(operations_before == 122);
    REQUIRE(s.env.count("SELECT COUNT(*) FROM usage_samples WHERE baseline_key=?", s.A) == 300);
    const auto disk_before = s.env.bytes_on_disk();

    const Json report = unwrap(s.policy->compact(s.settings(), false, kFar));

    CHECK(report["folded_operations"] == 116);      // every completed operation without an owner decision
    CHECK(report["operations_after"] == 6);
    CHECK(report["retained"]["owner_confirmed"] == 1);
    CHECK(report["retained"]["status_not_foldable"] == 3);   // requires_confirmation, denied, cancelled
    CHECK(report["folded_samples"] == 204);          // cohort A: (100 - 32) x 3 resources
    CHECK(report["cohorts"] == 3);
    CHECK(report["space_reclaimed"] == true);
    CHECK(report["bytes_after"].get<std::uint64_t>() < report["bytes_before"].get<std::uint64_t>());
    CHECK(s.env.bytes_on_disk() < disk_before);
    MESSAGE("ledger " << disk_before << " -> " << s.env.bytes_on_disk() << " bytes for " << operations_before << " operations");

    // The same questions get the same answers, receipts included.
    CHECK(s.decisions() == decisions_before);
    CHECK(s.protected_state() == protected_before);
    CHECK(s.env.count("SELECT COUNT(*) FROM usage_operations") == 6);
    CHECK(s.env.count("SELECT COUNT(*) FROM usage_samples WHERE baseline_key=?", s.A) == 96);   // 32 per resource
    CHECK(s.env.count("SELECT samples FROM usage_fold_samples WHERE baseline_key=? AND resource='input_bytes'", s.A) == 68);
    CHECK(s.env.count("SELECT COUNT(*) FROM usage_samples WHERE baseline_key=?", s.B) == 30);   // within the window: kept
    CHECK(s.env.count("SELECT COUNT(*) FROM usage_folded_operations") == 116);

    // Pending and decided operations answer as they did.
    CHECK(unwrap(s.policy->request(estimate("c_denied", s.C, 6000)))["status"] == "denied");
    CHECK(unwrap(s.policy->request(estimate("c_pending", s.C, 105)))["status"] == "allowed");
    CHECK(unwrap(s.policy->request(estimate("c_confirmed", s.C, 7000)))["confirmation"]["ref"] == "owner");

    // The fold is visible in the cohort's inspection, not hidden.
    Json inspected = unwrap(s.policy->inspect(s.A));
    REQUIRE(inspected.contains("folded"));
    CHECK(inspected["folded"]["operations"] == 100);
    CHECK(inspected["folded"]["samples"]["input_bytes"]["samples"] == 68);
    CHECK(inspected["operations"].empty());
    CHECK(inspected["baseline"]["input_bytes"]["samples"] == 32);

    // Compacting again finds nothing more to fold.
    const Json again = unwrap(s.policy->compact(s.settings(), false, kFar));
    CHECK(again["folded_operations"] == 0);
    CHECK(again["folded_samples"] == 0);
  }

  TEST_CASE("an operation waiting for the owner can still be confirmed after a compaction") {
    Env env;
    auto p = env.open();
    for (int i = 0; i < 5; ++i) run_operation(*p, "d_" + std::to_string(i), "D", 100, 0.001);
    const auto held = unwrap(p->request(estimate("d_held", "D", 5000)));
    REQUIRE(held["status"] == "requires_confirmation");
    const Json before = unwrap(p->inspect("D"));
    const Json report = unwrap(p->compact(unwrap(usage_compaction_settings(env.config, env.dir.path())), false, kFar));
    CHECK(report["folded_operations"] == 5);
    // The receipt the owner was shown is still the receipt of the same projection.
    CHECK(unwrap(p->preview(estimate("d_held", "D", 5000)))["receipt_id"] == held["receipt_id"]);
    const auto allowed = unwrap(p->confirm("d_held", held["receipt_id"].get<std::string>(), true, "owner"));
    CHECK(allowed["status"] == "allowed");
    CHECK(allowed["confirmation"]["ref"] == "owner");
    // The reservation it holds is part of the next projection, as before.
    CHECK(unwrap(p->preview(estimate("d_next", "D", 100)))["resources"]["input_bytes"]["reserved"] == 5000);
    CHECK(before["baseline"]["input_bytes"]["value"] == unwrap(p->inspect("D"))["baseline"]["input_bytes"]["value"]);
  }

  TEST_CASE("a folded operation keeps its identity: same estimate answers as completed, a different one conflicts") {
    Seeded s;
    unwrap(s.policy->compact(s.settings(), false, kFar));
    const auto same = unwrap(s.policy->request(estimate("a_5", s.A, 100 + 5, 0.001 * (1 + 5 % 5))));
    CHECK(same["status"] == "completed");
    CHECK(same["authorized"] == false);
    CHECK(same["compacted"] == true);
    const auto different = s.policy->request(estimate("a_5", s.A, 999));
    REQUIRE_FALSE(different);
    CHECK(different.error().code == Errc::Conflict);
    const auto completion = s.policy->complete("a_5", measured(105, 0.001));
    REQUIRE_FALSE(completion);
    CHECK(completion.error().code == Errc::Conflict);
    CHECK(s.policy->complete("never_existed", measured(1, 1)).error().code == Errc::NotFound);
    // A new operation of the cohort is admitted as ever.
    CHECK(unwrap(s.policy->request(estimate("a_new", s.A, 104)))["status"] == "allowed");
  }

  TEST_CASE("only operations older than the age are folded; a dry run and mode off change nothing") {
    Seeded s;
    const auto disk = s.env.bytes_on_disk();
    // Everything was recorded just now: nothing is 30 days old.
    CHECK(unwrap(s.policy->compact(s.settings()))["folded_operations"] == 0);
    // A dry run reports what would be folded and writes nothing.
    const Json dry = unwrap(s.policy->compact(s.settings(), true, kFar));
    CHECK(dry["dry_run"] == true);
    CHECK(dry["folded_operations"] == 116);
    CHECK(dry["folded_samples"] == 204);
    CHECK(s.env.count("SELECT COUNT(*) FROM usage_operations") == 122);
    CHECK(s.env.count("SELECT COUNT(*) FROM usage_folded_operations") == 0);
    CHECK(s.env.bytes_on_disk() >= disk);

    // The command entry point: mode off folds nothing; an override says otherwise.
    const Json off{{"override", Json{{"mode", "off"}}}, {"as_of", kFar}};
    CHECK(unwrap(compact_usage_ledger(s.env.config, s.env.dir.path(), off))["status"] == "off");
    CHECK(s.env.count("SELECT COUNT(*) FROM usage_operations") == 122);
    s.env.config.set("usage_ledger_compaction", Json{{"mode", "off"}});
    CHECK(unwrap(compact_usage_ledger(s.env.config, s.env.dir.path(), Json{{"as_of", kFar}}))["status"] == "off");
    const Json run{{"override", Json{{"mode", "manual"}}}, {"as_of", kFar}};
    const Json done = unwrap(compact_usage_ledger(s.env.config, s.env.dir.path(), run));
    CHECK(done["status"] == "compacted");
    CHECK(done["folded_operations"] == 116);
    // statuses is a setting too: cancelled operations can be folded when the owner says so.
    const Json cancelled{{"override", Json{{"mode", "manual"}, {"statuses", Json::array({"completed", "cancelled"})}}}, {"as_of", kFar}};
    CHECK(unwrap(compact_usage_ledger(s.env.config, s.env.dir.path(), cancelled))["folded_operations"] == 1);
    // No ledger, no compaction: it never creates one.
    Env empty;
    CHECK(unwrap(compact_usage_ledger(empty.config, empty.dir.path(), Json::object()))["status"] == "no_ledger");
    CHECK_FALSE(std::filesystem::exists(empty.ledger));
  }

  TEST_CASE("with the whole history as baseline no sample is summed away; decisions stay identical") {
    Seeded s(Json{{"baseline_window", nullptr}});
    const Json before = s.decisions();
    const Json report = unwrap(s.policy->compact(s.settings(), false, kFar));
    CHECK(report["folded_operations"] == 116);
    CHECK(report["folded_samples"] == 0);
    CHECK(s.env.count("SELECT COUNT(*) FROM usage_samples WHERE baseline_key=?", s.A) == 300);
    CHECK(s.decisions() == before);
  }

  TEST_CASE("a window raised after compaction reads the summed samples for the missing part") {
    Seeded s;
    unwrap(s.policy->compact(s.settings(), false, kFar));
    auto wider = s.env.open(Json{{"baseline_window", 40}});
    Json inspected = unwrap(wider->inspect(s.A));
    CHECK(inspected["baseline"]["input_bytes"]["samples"] == 40);   // 32 kept + 8 from the aggregate
    const double value = inspected["baseline"]["input_bytes"]["value"].get<double>();
    CHECK(value >= 100);
    CHECK(value <= 106);
    // Without the aggregate's help the window simply has what the ledger holds.
    auto all = s.env.open(Json{{"baseline_window", nullptr}});
    CHECK(unwrap(all->inspect(s.A))["baseline"]["input_bytes"]["samples"] == 100);   // 32 kept + 68 summed
  }

  TEST_CASE("the automatic run honours mode and interval and never creates a ledger") {
    Env env;
    CHECK(unwrap(auto_compact_usage_ledger(env.config, env.dir.path()))["reason"] == "no_ledger");
    {
      auto p = env.open();
      run_operation(*p, "x_1", "k", 10, 1);
    }
    const Json first = unwrap(auto_compact_usage_ledger(env.config, env.dir.path()));
    CHECK(first["status"] == "compacted");
    CHECK(first["folded_operations"] == 0);          // nothing is 30 days old yet
    const Json second = unwrap(auto_compact_usage_ledger(env.config, env.dir.path()));
    CHECK(second["status"] == "skipped");
    CHECK(second["reason"] == "not_due");
    env.config.set("usage_ledger_compaction", Json{{"mode", "manual"}});
    CHECK(unwrap(auto_compact_usage_ledger(env.config, env.dir.path()))["reason"] == "mode");
    env.config.set("usage_ledger_compaction", Json{{"auto_interval_hours", 0}});
    CHECK(unwrap(auto_compact_usage_ledger(env.config, env.dir.path()))["status"] == "compacted");   // interval 0: always due
  }

  TEST_CASE("the JSON command entry point: compact, compaction_settings, errors") {
    fsutil::TempDir dir;
    const std::string opts = Json{{"data_dir", dir.path().string()}, {"start_workers", false}}.dump();
    LoomContext* ctx = loom_init_ex(opts.c_str(), nullptr);
    REQUIRE(ctx != nullptr);
    const auto call = [&](const Json& command) {
      const std::string text = command.dump();
      const char* raw = loom_usage_policy_json(ctx, text.c_str());
      REQUIRE(raw != nullptr);
      auto parsed = json::parse(raw);
      loom_free_string(raw);
      REQUIRE(parsed);
      return *parsed;
    };
    CHECK(call(Json{{"action", "compact"}})["status"] == "no_ledger");
    CHECK(call(Json{{"action", "compaction_settings"}})["effective"]["mode"] == "auto");
    CHECK(call(Json{{"action", "compaction_settings"}, {"override", Json{{"mode", "nonsense"}}}}).contains("error"));
    for (int i = 0; i < 3; ++i) {
      const std::string id = "op_" + std::to_string(i);
      REQUIRE(call(Json{{"action", "request"}, {"estimate", estimate(id, "cohort", 100)}})["status"] == "allowed");
      call(Json{{"action", "complete"}, {"operation_id", id}, {"actual", measured(100, 0.001)}});
    }
    const Json dry = call(Json{{"action", "compact"}, {"dry_run", true}, {"as_of", kFar}});
    CHECK(dry["status"] == "dry_run");
    CHECK(dry["folded_operations"] == 3);
    const Json done = call(Json{{"action", "compact"}, {"as_of", kFar}});
    CHECK(done["status"] == "compacted");
    CHECK(done["folded_operations"] == 3);
    CHECK(call(Json{{"action", "inspect"}, {"baseline_key", "cohort"}})["folded"]["operations"] == 3);
    loom_shutdown(ctx);
  }
}
