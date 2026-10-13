// Concurrent use_model calls through the shared usage guard: the ledger is
// created and written by several threads at once.  Every call must succeed.
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
#include "loom/use_model.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using Reply = net::ScriptedTransport::Reply;

namespace {

int iterations() {
  if (const char* env = std::getenv("LOOM_STRESS_ITERATIONS")) {
    const int n = std::atoi(env);
    if (n > 0) return n;
  }
  return 12;
}

Json chat_reply() {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", "ok"}}}}})},
              {"usage", Json{{"completion_tokens", 1}}}};
}

}  // namespace

TEST_SUITE("use_model_concurrency") {
  TEST_CASE("first use of a fresh ledger by several threads at once: every guarded call succeeds") {
    constexpr int kThreads = 8;
    const int rounds = iterations();
    std::vector<std::string> failures;
    for (int round = 0; round < rounds; ++round) {
      fsutil::TempDir td;
      Config cfg{td.path() / "config.json"};
      Secrets secrets{td.path() / "secrets.json"};
      cfg.set("base_url", "https://api.test");
      secrets.set("api_key", "sk-test");
      net::ScriptedTransport transport;
      transport.set_fallback(Reply::json(200, chat_reply()));
      const llm::Services services(cfg, secrets, transport);

      std::barrier start(kThreads);
      std::mutex mu;
      std::vector<std::thread> threads;
      for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
          llm::ModelRequest request;
          request.kind = "stress.concurrent";
          request.models = {llm::ModelSpec{"m", "", Json::object()}};
          request.prompt = "message " + std::to_string(t);
          request.parameters = Json{{"max_tokens", 5}};
          request.timeout_ms = 5000;
          start.arrive_and_wait();
          auto outcome = llm::use_model(services, std::move(request));
          std::string why;
          if (!outcome) why = "request error: " + outcome.error().to_string();
          else if (!outcome->ok()) {
            const auto& r = outcome->last();
            why = std::string(llm::to_string(r.state)) + ": " + (r.error ? r.error->to_string() : "no error");
          }
          if (!why.empty()) {
            std::lock_guard lk(mu);
            failures.push_back("round " + std::to_string(round) + " thread " + std::to_string(t) + " -> " + why);
          }
        });
      for (auto& th : threads) th.join();
      CHECK(transport.requests().size() == static_cast<std::size_t>(kThreads) - [&] {
        std::lock_guard lk(mu);
        std::size_t lost = 0;
        for (const auto& f : failures) if (f.rfind("round " + std::to_string(round) + " ", 0) == 0) ++lost;
        return lost;
      }());
    }
    for (const auto& f : failures) MESSAGE(f);
    CHECK(failures.empty());
  }
}
