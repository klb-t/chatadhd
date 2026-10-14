// P17 6: the baseline of the x10 guard before it is "normal usage". A cohort with fewer than `baseline_min_samples` measured samples has as its
// baseline the larger of the measured mean and a floor (the size of an ordinary request), so one short "cześć" does not make the next ordinary
// message a tenfold growth. The x10 rule itself is unchanged; from `baseline_min_samples` samples on the measured baseline is used as it is.
#include <doctest/doctest.h>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/net/http.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_analyzer.h"
#include "loom/settings_fields.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Env {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  std::unique_ptr<Database> db = loom::test::open_db(td.path() / "test.db");
  EventBus bus;
  std::unique_ptr<SemanticAnalyzer> analyzer = unwrap(SemanticAnalyzer::create());
  net::ScriptedTransport transport;
  Env() {
    secrets.set("api_key", "sk-test-key");
    cfg.set("base_url", "https://api.test");
    cfg.set("default_model", "test/model");
    cfg.set("auto_title", false);
  }
  ChatEngine engine() { return ChatEngine(cfg, secrets, *db, bus, transport, *analyzer); }
  void script(int n) {
    for (int i = 0; i < n; ++i)
      transport.expect("POST", "https://api.test/chat/completions",
                       net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", "ok"}}}}})},
                                                                     {"usage", Json{{"completion_tokens", 3}}}}));
  }
};
ChatOptions plain() {
  ChatOptions o;
  o.stream = false;
  return o;
}

Json estimate(const std::string& id, double bytes, const Json& more = Json::object()) {
  Json e{{"operation_id", id}, {"baseline_key", "cohort"}, {"model", "test/model"}, {"resources", Json{{"input_bytes", bytes}}}};
  for (auto it = more.begin(); it != more.end(); ++it) e[it.key()] = it.value();
  return e;
}

// A completed operation that measured `bytes`.
void sample(UsagePolicy& policy, const std::string& id, double bytes, const Json& resources = Json::object()) {
  unwrap(policy.request(estimate(id, bytes)));
  Json measured = resources;
  measured["input_bytes"] = bytes;
  unwrap(policy.complete(id, Json{{"resources", measured}, {"provenance", "instrument_measured"}}));
}

const Json& dim(const Json& decision, const char* resource) { return decision.at("resources").at(resource); }

}  // namespace

TEST_SUITE("usage_baseline_floor") {
  TEST_CASE("the preset: five samples, an ordinary request of 4000 tokens in and 1000 out, the characters per token of the cost estimates") {
    const Json preset = unwrap(usage_policy_preset());
    CHECK(preset.at("baseline_min_samples") == 5);
    CHECK(preset.at("baseline_floor_tokens") == Json{{"input", 4000}, {"output", 1000}});
    CHECK(preset.at("baseline_floor").empty());
    // the same band the estimates of action runs use (runtime profile action_run, estimate.chars_per_token, its `low` end)
    const Json run = unwrap(RuntimeProfile::builtin("action_run")).values();
    CHECK(preset.at("baseline_floor_chars_per_token").get<double>() == run.at("estimate").at("chars_per_token").at("low").get<double>());
    // an edit of the floor is an edit of loom_usage_policy: the rule of that setting (a locked confirmation) applies to it
    const Json fields = unwrap(settings_fields());
    bool found = false;
    for (const auto& f : fields.at("fields"))
      if (f.at("key") == "loom_usage_policy") {
        found = true;
        CHECK(f.at("edit") == "requires_confirmation");
        CHECK(f.at("description").get<std::string>().find("baseline_min_samples") != std::string::npos);
      }
    CHECK(found);
  }

  TEST_CASE("one tiny first message, then a 3 KB second message: not held; a message ten times the floor: held") {
    Env env;
    env.script(3);
    auto engine = env.engine();
    REQUIRE(engine.send("cześć", plain()));
    const std::string three_kb(3000, 'x');
    auto second = engine.send(three_kb, plain());
    REQUIRE(second);
    CHECK_FALSE(second->usage_pending.has_value());  // before: 3 KB over a 200-byte baseline was x10 and held
    CHECK(env.transport.requests().size() == 2);
    // the floor is 4000 tokens x 4 = 16000 bytes: a 200 KB message is about twelve times that
    const std::string huge(200000, 'x');
    auto held = engine.send(huge, plain());
    REQUIRE(held);
    REQUIRE(held->usage_pending.has_value());
    CHECK((*held->usage_pending)["status"] == "requires_confirmation");
    CHECK(env.transport.requests().size() == 2);  // nothing was sent
    const Json& bytes = held->usage_pending->at("projected").at("input_bytes");
    CHECK(bytes.at("baseline_source") == "floor");
    CHECK(bytes.at("baseline").get<double>() == doctest::Approx(16000.0));
    CHECK(bytes.at("baseline_samples") == 2);
  }

  TEST_CASE("after five samples the measured baseline applies as it always did") {
    Env env;
    env.script(6);
    auto engine = env.engine();
    const std::string sixty_kb(60000, 'x');
    for (int i = 0; i < 4; ++i) REQUIRE(engine.send("hi " + std::to_string(i), plain()));
    // four samples: still the floor (60 KB is under ten times 16000 bytes)
    CHECK(unwrap(engine.preview_usage(sixty_kb, plain())).value("status", "") == "allowed");
    REQUIRE(engine.send("hi 4", plain()));
    // five samples: the measured mean of tiny sends is the baseline, and the same message is x10 growth
    const Json preview = unwrap(engine.preview_usage(sixty_kb, plain()));
    CHECK(preview.value("status", "") == "requires_confirmation");
    CHECK(dim(preview, "input_bytes").at("baseline_source") == "measured");
    CHECK(dim(preview, "input_bytes").at("baseline_samples") == 5);
  }

  TEST_CASE("the floor is a floor: a measured mean above it wins, no sample keeps the old behaviour, a declared baseline is the user's own") {
    fsutil::TempDir td;
    auto policy = unwrap(UsagePolicy::open(td.path() / "u.sqlite", Json::object()));
    // no sample at all: nothing to grow from, as before
    CHECK(dim(unwrap(policy->preview(estimate("p0", 5000000))), "input_bytes").at("baseline_source") == "unavailable");
    CHECK(unwrap(policy->preview(estimate("p0", 5000000))).at("status") == "allowed");
    sample(*policy, "a", 100);
    const Json one = unwrap(policy->preview(estimate("p1", 3000)));
    CHECK(dim(one, "input_bytes").at("baseline_source") == "floor");
    CHECK(dim(one, "input_bytes").at("baseline").get<double>() == doctest::Approx(16000.0));
    CHECK(dim(one, "input_bytes").at("requires_confirmation") == false);
    // a mean above the floor is the baseline, however few the samples
    sample(*policy, "b", 100000);
    const Json big_mean = unwrap(policy->preview(estimate("p2", 3000)));
    CHECK(dim(big_mean, "input_bytes").at("baseline_source") == "measured");
    CHECK(dim(big_mean, "input_bytes").at("baseline").get<double>() == doctest::Approx(50050.0));
    // the x10 rule is the same: ten times the floor-based baseline is held, just under it is not
    fsutil::TempDir td2;
    auto fresh = unwrap(UsagePolicy::open(td2.path() / "u.sqlite", Json::object()));
    sample(*fresh, "a", 100);
    CHECK(dim(unwrap(fresh->preview(estimate("q1", 159999))), "input_bytes").at("requires_confirmation") == false);
    CHECK(dim(unwrap(fresh->preview(estimate("q2", 160000))), "input_bytes").at("requires_confirmation") == true);
  }

  TEST_CASE("the floors of the other resources: tokens as they are, money priced with the model's price when the caller knows it, requests none") {
    fsutil::TempDir td;
    auto policy = unwrap(UsagePolicy::open(td.path() / "u.sqlite", Json::object()));
    unwrap(policy->request(Json{{"operation_id", "a"}, {"baseline_key", "cohort"}, {"model", "m"},
                                {"resources", Json{{"requests", 1}, {"input_tokens", 10}, {"output_tokens", 5}, {"cost_usd", 0.0001}}}}));
    unwrap(policy->complete("a", Json{{"resources", Json{{"requests", 1}, {"input_tokens", 10}, {"output_tokens", 5}, {"cost_usd", 0.0001}}},
                                      {"provenance", "provider_reported"}}));
    const Json e{{"operation_id", "b"}, {"baseline_key", "cohort"}, {"model", "m"},
                 {"resources", Json{{"requests", 1}, {"input_tokens", 3000}, {"output_tokens", 800}, {"cost_usd", 0.02}}},
                 {"price", Json{{"input", 0.000003}, {"output", 0.000015}}}};
    const Json d = unwrap(policy->preview(e));
    CHECK(dim(d, "input_tokens").at("baseline") == 4000);
    CHECK(dim(d, "output_tokens").at("baseline") == 1000);
    CHECK(dim(d, "cost_usd").at("baseline").get<double>() == doctest::Approx(4000 * 0.000003 + 1000 * 0.000015));
    CHECK(dim(d, "requests").at("baseline_source") == "measured");  // a count has no floor
    CHECK(d.at("status") == "allowed");
    // without a price the money has no floor and the measured mean decides, as before
    Json priceless = e;
    priceless.erase("price");
    CHECK(dim(unwrap(policy->preview(priceless)), "cost_usd").at("baseline_source") == "measured");
  }

  TEST_CASE("the floor is data and the user's: off with 0, other numbers, explicit floors per resource, null for none; nonsense is refused") {
    fsutil::TempDir td;
    auto base = [&](const Json& options) {
      auto policy = unwrap(UsagePolicy::open(td.path() / ("u" + std::to_string(options.dump().size()) + ".sqlite"), options));
      sample(*policy, "a", 100);
      return policy;
    };
    auto off = base(Json{{"baseline_min_samples", 0}});
    CHECK(dim(unwrap(off->preview(estimate("p", 3000))), "input_bytes").at("baseline_source") == "measured");
    CHECK(dim(unwrap(off->preview(estimate("p", 3000))), "input_bytes").at("requires_confirmation") == true);  // the old behaviour, on request
    auto smaller = base(Json{{"baseline_floor_tokens", Json{{"input", 100}, {"output", 50}}}, {"baseline_floor_chars_per_token", 3.0}});
    CHECK(dim(unwrap(smaller->preview(estimate("p", 3000))), "input_bytes").at("baseline").get<double>() == doctest::Approx(300.0));
    auto explicit_floor = base(Json{{"baseline_floor", Json{{"input_bytes", 1000000.0}}}});
    CHECK(dim(unwrap(explicit_floor->preview(estimate("p", 3000))), "input_bytes").at("baseline").get<double>() == doctest::Approx(1000000.0));
    auto none = base(Json{{"baseline_floor", Json{{"input_bytes", nullptr}}}});
    CHECK(dim(unwrap(none->preview(estimate("p", 3000))), "input_bytes").at("baseline_source") == "measured");
    // validation: the whole value is checked like the rest of the options
    for (const Json& bad : {Json{{"baseline_min_samples", -1}}, Json{{"baseline_min_samples", 1.5}}, Json{{"baseline_floor_tokens", 5}},
                            Json{{"baseline_floor_tokens", Json{{"input", -1}}}}, Json{{"baseline_floor_tokens", Json{{"throughput", 1}}}},
                            Json{{"baseline_floor_chars_per_token", 0}}, Json{{"baseline_floor", Json{{"input_bytes", "many"}}}},
                            Json{{"baseline_floor", Json::array()}}})
      CHECK_FALSE(validate_usage_policy_options(bad));
    CHECK(validate_usage_policy_options(Json{{"baseline_min_samples", 0}, {"baseline_floor", Json{{"input_bytes", nullptr}}}}));
  }
}
