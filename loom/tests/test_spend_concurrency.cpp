// P18: the spending limits are read by every admission of the usage ledger, so many admissions at once must not lock each other out: the reads run on
// the connection of the admission, under its busy timeout, and end before the write transaction begins (UsagePolicy::limits_precheck). 80 threads
// admit at once - through the shared guard (use_model) and straight at the ledger - with limits set, on a ledger that is created by them.
#include <doctest/doctest.h>

#include <atomic>
#include <barrier>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/usage_policy.h"
#include "loom/use_model.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using Reply = net::ScriptedTransport::Reply;
using loom::test::unwrap;

namespace {

constexpr int kThreads = 80;

int rounds() {
  if (const char* env = std::getenv("LOOM_STRESS_ITERATIONS")) {
    const int n = std::atoi(env);
    if (n > 0) return n;
  }
  return 1;
}

Json chat_reply() {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", "ok"}}}}})},
              {"usage", Json{{"completion_tokens", 1}}}};
}

Json limits_of(const char* on_exceed) {
  return Json::array({Json{{"period", "month"}, {"amount", 1.0}, {"currency", "USD"}, {"on_exceed", on_exceed}},
                      Json{{"period", "day"}, {"amount", 1000.0}, {"currency", "USD"}, {"on_exceed", "deny"}}});
}

// What the providers charged earlier in the period: the limits have something to add up.
void earlier_spend(const std::filesystem::path& dir, double usd) {
  auto policy = unwrap(UsagePolicy::open(dir / "usage-policy.sqlite", Json::object()));
  Json estimate{{"operation_id", "earlier"}, {"baseline_key", "chat.send|test|m"}, {"model", "m"}, {"resources", Json{{"cost_usd", nullptr}}}};
  unwrap(policy->request(estimate));
  unwrap(policy->complete("earlier", Json{{"resources", Json{{"requests", 1}, {"cost_usd", usd}}}, {"provenance", "provider_reported"}}));
}

// 80 threads call the shared guard at once on a ledger they create. With `limits` the month's limit (on_exceed notify) is passed, so every admission reads the
// spend of the period and tells; without, the same run is the reference: it shows whether a lock error belongs to the limit reads or to the ledger as it was.
void run_shared_guard(bool limits, const char* label) {
  std::vector<std::string> failures;
  for (int round = 0; round < rounds(); ++round) {
    fsutil::TempDir td;
    Config cfg{td.path() / "config.json"};
    Secrets secrets{td.path() / "secrets.json"};
    cfg.set("base_url", "https://api.test");
    if (limits) cfg.set("usage_spend_limits", limits_of("notify"));
    // 80 requests in flight add up as reservations and would trip the x10 guard; this test is about the ledger's locks, so the factor is the user's to raise
    // The 80 admissions queue for the one write lock of the ledger and each decodes the reservations of the others, so the last one waits for all of them: under
    // a sanitizer that is longer than the preset busy timeout (30 s). The timeout is the user's setting (ledger_busy_timeout_ms); what this test pins is that
    // nobody is refused for a reason that waiting does not cure (a read upgraded to a write, a second connection), so the wait is allowed to be long.
    cfg.set("loom_usage_policy", Json{{"baseline_min_samples", 0}, {"growth_factor", 1e12}, {"ledger_busy_timeout_ms", 600000}});
    secrets.set("api_key", "sk-test");
    earlier_spend(td.path(), 2.0);  // the month's limit of 1 USD is passed: every admission computes, and tells
    net::ScriptedTransport transport;
    transport.set_fallback(Reply::json(200, chat_reply()));
    const llm::Services services(cfg, secrets, transport);
    std::barrier start(kThreads);
    std::mutex mu;
    std::atomic<int> notified{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
      threads.emplace_back([&, t] {
        llm::ModelRequest request;
        request.kind = "stress.spend";
        request.models = {llm::ModelSpec{"m", "", Json::object()}};
        request.prompt = "message " + std::to_string(t);
        request.parameters = Json{{"max_tokens", 5}};
        request.timeout_ms = 20000;
        start.arrive_and_wait();
        auto outcome = llm::use_model(services, std::move(request));
        std::string why;
        if (!outcome) why = "request error: " + outcome.error().to_string();
        else if (!outcome->ok()) {
          const auto& r = outcome->last();
          why = std::string(llm::to_string(r.state)) + ": " + (r.error ? r.error->to_string() : "no error");
        } else if (outcome->last().notices.is_array() && !outcome->last().notices.empty()) {
          ++notified;
        }
        if (!why.empty()) {
          std::lock_guard lk(mu);
          failures.push_back("round " + std::to_string(round) + " thread " + std::to_string(t) + " -> " + why);
        }
      });
    for (auto& th : threads) th.join();
    CHECK_MESSAGE(notified.load() == (limits ? kThreads : 0), label);
    CHECK_MESSAGE(transport.requests().size() == static_cast<std::size_t>(kThreads) - failures.size(), label);
  }
  std::string all;
  for (const auto& f : failures) all += f + "\n";
  CHECK_MESSAGE(failures.empty(), label << "\n" << all);
}

}  // namespace

TEST_SUITE("spend_concurrency") {
  TEST_CASE("80 model calls at once through the shared guard with spending limits set: every one is admitted (notify) and none is locked out") {
    run_shared_guard(true, "with limits");
  }

  TEST_CASE("reference: the same 80 model calls with no limit set (what the ledger did before the limits)") {
    run_shared_guard(false, "without limits");
  }

  TEST_CASE("80 admissions at once straight at a ledger they create: confirm holds are decided one by one, a limit that is not passed lets everyone in, nobody is locked out") {
    for (const char* on_exceed : {"confirm", "deny"}) {
      fsutil::TempDir td;
      Config cfg{td.path() / "config.json"};
      cfg.set("usage_spend_limits", Json::array({Json{{"period", "month"}, {"amount", 1000.0}, {"currency", "USD"}, {"on_exceed", on_exceed}}}));
      cfg.set("loom_usage_policy", Json{{"ledger_busy_timeout_ms", 600000}});  // the wait for the one write lock is allowed to be long under a sanitizer
      const Json options = unwrap(effective_usage_policy_options(cfg));
      REQUIRE(options.contains("spend_limits"));
      std::barrier start(kThreads);
      std::mutex mu;
      std::vector<std::string> failures;
      std::atomic<int> allowed{0};
      std::vector<std::thread> threads;
      for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
          start.arrive_and_wait();
          auto policy = UsagePolicy::open(td.path() / "usage-policy.sqlite", options);
          std::string why;
          if (!policy) why = "open: " + policy.error().to_string();
          else {
            const std::string op = "op_" + std::to_string(t);
            Json estimate{{"operation_id", op}, {"baseline_key", "stress|m"}, {"model", "m"}, {"resources", Json{{"requests", 1}, {"cost_usd", 0.01}}}};
            auto decision = (*policy)->request(estimate);
            if (!decision) why = "request: " + decision.error().to_string();
            else if (decision->at("status") != "allowed") why = "status " + decision->at("status").get<std::string>();
            else {
              ++allowed;
              auto done = (*policy)->complete(op, Json{{"resources", Json{{"requests", 1}, {"cost_usd", 0.01}}}, {"provenance", "provider_reported"}});
              if (!done) why = "complete: " + done.error().to_string();
            }
          }
          if (!why.empty()) {
            std::lock_guard lk(mu);
            failures.push_back("thread " + std::to_string(t) + " -> " + why);
          }
        });
      for (auto& th : threads) th.join();
      std::string all;
      for (const auto& f : failures) all += f + "\n";
      INFO(on_exceed);
      CHECK_MESSAGE(failures.empty(), all);
      CHECK(allowed.load() == kThreads);  // 80 x 0.01 USD never reaches the limit of 1000
      auto reader = unwrap(UsagePolicy::open(td.path() / "usage-policy.sqlite", options));
      const Json spent = unwrap(reader->spend_between("2000-01-01 00:00:00", "2999-01-01 00:00:00"));
      CHECK(spent.at("operations") == kThreads);
    }
  }

  TEST_CASE("many threads confirm and ask again over a passed limit: the receipt of a hold is decided once and the others see the same answer") {
    fsutil::TempDir td;
    Config cfg{td.path() / "config.json"};
    cfg.set("usage_spend_limits", limits_of("confirm"));
    cfg.set("loom_usage_policy", Json{{"ledger_busy_timeout_ms", 600000}});
    const Json options = unwrap(effective_usage_policy_options(cfg));
    earlier_spend(td.path(), 2.0);
    const Json estimate{{"operation_id", "held"}, {"baseline_key", "stress|m"}, {"model", "m"}, {"resources", Json{{"requests", 1}}}};
    Json held;
    {
      auto policy = unwrap(UsagePolicy::open(td.path() / "usage-policy.sqlite", options));
      held = unwrap(policy->request(estimate));
      REQUIRE(held.at("status") == "requires_confirmation");
    }
    std::barrier start(kThreads);
    std::mutex mu;
    std::vector<std::string> failures;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
      threads.emplace_back([&, t] {
        start.arrive_and_wait();
        std::string why;
        auto policy = UsagePolicy::open(td.path() / "usage-policy.sqlite", options);
        if (!policy) why = "open: " + policy.error().to_string();
        else if (t % 2 == 0) {
          auto again = (*policy)->request(estimate);  // the same operation asked again: the hold stays the same hold
          if (!again) why = "request: " + again.error().to_string();
          else if (again->at("receipt_id") != held.at("receipt_id")) why = "another receipt";
        } else {
          auto yes = (*policy)->confirm("held", held.at("receipt_id").get<std::string>(), true, "thread");
          if (!yes) why = "confirm: " + yes.error().to_string();
          else if (yes->at("status") != "allowed") why = "confirmed status " + yes->at("status").get<std::string>();
        }
        if (!why.empty()) {
          std::lock_guard lk(mu);
          failures.push_back("thread " + std::to_string(t) + " -> " + why);
        }
      });
    for (auto& th : threads) th.join();
    std::string all;
    for (const auto& f : failures) all += f + "\n";
    // a request that raced a confirmation may see either the hold or the confirmed record; both are answers, neither is a lock error
    for (const auto& f : failures) CHECK_MESSAGE(f.find("locked") == std::string::npos, f);
    auto reader = unwrap(UsagePolicy::open(td.path() / "usage-policy.sqlite", options));
    CHECK(unwrap(reader->request(estimate)).at("status") == "allowed");
  }
}
