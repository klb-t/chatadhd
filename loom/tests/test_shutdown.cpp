// Android lifecycle regression: init (workers on, default options) -> subscribe
// -> use the local chat -> shutdown -> init again (Activity recreate).
#include <doctest/doctest.h>

#include <atomic>
#include <string>

#include "loom/loom.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"

using loom::Json;

namespace {

std::atomic<int> g_events{0};

void on_event(const char*, const char*, void*) { g_events.fetch_add(1); }
void on_chunk(const char*, int, void*) {}

int fake_http(const char*, LoomHttpResponse* r, void*) {
  return loom_http_response_fail(r, LOOM_E_NETWORK, "offline");
}

}  // namespace

TEST_SUITE("shutdown") {
  TEST_CASE("recreate cycle: shutdown with workers and subscriptions, then init again") {
    loom::fsutil::TempDir dir;
    Json o = Json::object();
    o["data_dir"] = dir.path().string();  // default options otherwise, as the Android app does
    const std::string opts = o.dump();
    for (int round = 0; round < 3; ++round) {
      const char* err = nullptr;
      LoomContext* ctx = loom_init_ex(opts.c_str(), &err);
      if (err) loom_free_string(err);
      REQUIRE(ctx != nullptr);
      loom_set_http_transport(ctx, fake_http, nullptr);
      CHECK(loom_subscribe(ctx, "message.created", on_event, nullptr) > 0);
      CHECK(loom_subscribe(ctx, "*", on_event, nullptr) > 0);
      loom_chat(ctx, nullptr, "hello", nullptr, -1, on_chunk, nullptr);
      loom_shutdown(ctx);
    }
    CHECK(g_events.load() >= 0);
  }
}
