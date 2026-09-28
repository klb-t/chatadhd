// C ABI tests. Every fixture is offline unless it installs its own test transport.
#include <doctest/doctest.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "loom/loom.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "test_helpers.h"

using loom::Json;

namespace {

// Takes ownership of a returned string and parses it.
Json take(const char* s) {
  REQUIRE(s != nullptr);
  auto j = loom::json::parse(s);
  loom_free_string(s);
  if (!j) FAIL("invalid JSON from C API");
  return std::move(j).value();
}

bool is_error(const Json& j, const char* code = nullptr) {
  if (!j.is_object() || !j.contains("error")) return false;
  return code == nullptr || j["error"]["code"] == code;
}

struct Ctx {
  loom::fsutil::TempDir dir;
  LoomContext* ctx = nullptr;
  std::atomic<int> denied_http_requests{0};

  static int deny_http(const char*, LoomHttpResponse* response, void* ud) {
    static_cast<Ctx*>(ud)->denied_http_requests.fetch_add(1);
    return loom_http_response_fail(response, LOOM_E_NETWORK, "unexpected HTTP request in offline C ABI test");
  }

  void restore_offline_transport() {
    REQUIRE(loom_set_http_transport(ctx, deny_http, this) == LOOM_OK);
  }

  Ctx() {
    std::string opts = Json{{"data_dir", dir.path().string()}, {"start_workers", false}}.dump();
    const char* err = nullptr;
    ctx = loom_init_ex(opts.c_str(), &err);
    if (err) {
      INFO(err);
      loom_free_string(err);
    }
    REQUIRE(ctx != nullptr);
    restore_offline_transport();
  }
  ~Ctx() {
    loom_shutdown(ctx);
    // Some APIs intentionally absorb transport failures; they must still fail
    // this test if they attempted an unscripted request.
    CHECK(denied_http_requests.load() == 0);
  }
};

}  // namespace

TEST_SUITE("capi") {
  TEST_CASE("version, init errors, NULL safety") {
    Json v = take(loom_version());
    CHECK(v["abi"] == LOOM_ABI_VERSION);
    CHECK(v["version"].is_string());
    CHECK(v.contains("fts5"));

    const char* err = nullptr;
    CHECK(loom_init_ex("{\"bogus\": 1}", &err) == nullptr);
    REQUIRE(err != nullptr);
    CHECK(is_error(take(err), "invalid_argument"));
    err = nullptr;
    CHECK(loom_init_ex("{not json", &err) == nullptr);
    CHECK(is_error(take(err), "parse"));

    CHECK(is_error(take(loom_list_conversations(nullptr, 10)), "invalid_argument"));
    CHECK(loom_delete_conversation(nullptr, "c_x") == LOOM_E_INVALID_ARGUMENT);
    loom_shutdown(nullptr);
    loom_free_string(nullptr);
    loom_semantic_pause(nullptr);
    loom_set_config(nullptr, "k", "1");
  }

  TEST_CASE("conversations and messages round-trip") {
    Ctx c;
    Json info = take(loom_info(c.ctx));
    CHECK(info["data_dir"] == c.dir.path().string());
    CHECK(std::filesystem::exists(c.dir.path() / "chatadhd.db"));
    CHECK(std::filesystem::exists(c.dir.path() / ".chatadhd_data"));

    Json conv = take(loom_create_conversation(c.ctx, "Hello"));
    std::string cid = conv["id"];
    CHECK(conv["title"] == "Hello");
    CHECK(take(loom_create_conversation(c.ctx, nullptr))["title"] == "New Chat");
    Json list = take(loom_list_conversations(c.ctx, 0));
    CHECK(list.size() == 2);
    CHECK(take(loom_get_conversation(c.ctx, cid.c_str()))["id"] == cid);
    CHECK(is_error(take(loom_get_conversation(c.ctx, "c_nope")), "not_found"));

    Json upd = take(loom_update_conversation(c.ctx, cid.c_str(), R"({"title":"Renamed","metadata":{"pin":true}})"));
    CHECK(upd["title"] == "Renamed");
    CHECK(upd["metadata"]["pin"] == true);
    CHECK(is_error(take(loom_update_conversation(c.ctx, cid.c_str(), R"({"nope":1})")), "invalid_argument"));
    CHECK(is_error(take(loom_update_conversation(c.ctx, "c_nope", R"({"title":"x"})")), "not_found"));

    // Messages are created through the (foundation) DB here; chat is wave 2.
    CHECK(take(loom_get_messages(c.ctx, cid.c_str())).empty());
    CHECK(is_error(take(loom_get_message(c.ctx, "m_nope")), "not_found"));
    CHECK(loom_delete_conversation(c.ctx, "c_nope") == LOOM_E_NOT_FOUND);
    CHECK(loom_delete_conversation(c.ctx, cid.c_str()) == LOOM_OK);
    CHECK(take(loom_list_conversations(c.ctx, 10)).size() == 1);
  }

  TEST_CASE("edit / versions / status / update / search via C API") {
    Ctx c;
    Json conv = take(loom_create_conversation(c.ctx, "t"));
    std::string cid = conv["id"];
    // Seed a message through a second, independent runtime-less DB handle.
    {
      auto db = loom::test::open_db(c.dir.path() / "chatadhd.db");
      loom::NewMessage m;
      m.conv_id = cid;
      m.text = "the quick brown fox";
      m.role = "user";
      REQUIRE(db->create_msg(m));
    }
    Json msgs = take(loom_get_messages(c.ctx, cid.c_str()));
    REQUIRE(msgs.size() == 1);
    std::string mid = msgs[0]["id"];
    Json edited = take(loom_edit_message(c.ctx, mid.c_str(), "the quick brown cat"));
    CHECK(edited["version_num"] == 2);
    Json versions = take(loom_get_versions(c.ctx, mid.c_str()));
    CHECK(versions.size() == 2);
    CHECK(take(loom_get_versions(c.ctx, versions[0]["version_group_id"].get<std::string>().c_str())).size() == 2);
    CHECK(loom_restore_version(c.ctx, mid.c_str()) == LOOM_OK);
    CHECK(take(loom_get_messages(c.ctx, cid.c_str()))[0]["id"] == mid);
    CHECK(loom_restore_version(c.ctx, "m_nope") == LOOM_E_NOT_FOUND);
    CHECK(loom_set_message_status(c.ctx, mid.c_str(), "excluded") == LOOM_OK);
    CHECK(loom_set_message_status(c.ctx, mid.c_str(), "bogus") == LOOM_E_INVALID_ARGUMENT);
    CHECK(take(loom_get_messages(c.ctx, cid.c_str())).empty());
    CHECK(take(loom_get_messages_ex(c.ctx, cid.c_str(), 1)).size() == 2);
    CHECK(loom_update_message(c.ctx, mid.c_str(), R"({"weight": 2.0, "status": "active"})") == LOOM_OK);
    CHECK(take(loom_get_message(c.ctx, mid.c_str()))["weight"] == 2.0);
    CHECK(loom_update_message(c.ctx, mid.c_str(), R"({"bad": 1})") == LOOM_E_INVALID_ARGUMENT);

    Json s = take(loom_search(c.ctx, "fox", nullptr));
    CHECK(s["results"].size() == 1);
    Json s2 = take(loom_search(c.ctx, "cat", R"({"include_inactive": true, "mode": "like"})"));
    CHECK(s2["mode"] == "like");
    CHECK(s2["results"].size() == 1);
    CHECK(is_error(take(loom_search(c.ctx, "x", R"({"mode":"nope"})")), "invalid_argument"));
  }

  TEST_CASE("config and secrets (values never exposed)") {
    Ctx c;
    Json cfg = take(loom_get_config(c.ctx));
    CHECK(cfg["default_model"] == "anthropic/claude-sonnet-4-20250514");
    CHECK(cfg["loom_task_workers"] == 1);
    loom_set_config(c.ctx, "temperature", "0.2");
    loom_set_config(c.ctx, "theme", "amoled");  // not JSON -> stored as string
    CHECK(take(loom_get_config(c.ctx))["temperature"] == 0.2);
    CHECK(take(loom_get_config(c.ctx))["theme"] == "amoled");
    CHECK(loom_set_config_json(c.ctx, R"({"max_tokens": 1000, "stream": false})") == LOOM_OK);
    CHECK(loom_set_config_json(c.ctx, "[1]") == LOOM_E_INVALID_ARGUMENT);
    auto on_disk = loom::json::parse(*loom::fsutil::read_file(c.dir.path() / "config.json"));
    CHECK((*on_disk)["max_tokens"] == 1000);

    CHECK(loom_has_secret(c.ctx, "api_key") == 0);
    CHECK(loom_set_secret(c.ctx, "api_key", "sk-secret") == LOOM_OK);
    CHECK(loom_has_secret(c.ctx, "api_key") == 1);
    Json keys = take(loom_list_secret_keys(c.ctx));
    CHECK(keys == Json::array({"api_key"}));
    std::string cfg_dump = take(loom_get_config(c.ctx)).dump();
    CHECK(cfg_dump.find("sk-secret") == std::string::npos);
    CHECK(loom_delete_secret(c.ctx, "api_key") == LOOM_OK);
    CHECK(loom_delete_secret(c.ctx, "api_key") == LOOM_E_NOT_FOUND);
    CHECK(loom_has_secret(c.ctx, "api_key") == 0);
  }

  TEST_CASE("events: subscribe, emit, unsubscribe; logs") {
    Ctx c;
    struct Seen {
      std::vector<std::string> names;
      std::vector<std::string> payloads;
    } seen;
    auto cb = [](const char* ev, const char* payload, void* ud) {
      auto* s = static_cast<Seen*>(ud);
      s->names.emplace_back(ev);
      s->payloads.emplace_back(payload);
    };
    int64_t tok = loom_subscribe(c.ctx, "*", cb, &seen);
    CHECK(tok > 0);
    take(loom_create_conversation(c.ctx, "evt"));
    CHECK(loom_emit(c.ctx, "custom:thing", R"({"x": 1})") == LOOM_OK);
    REQUIRE(seen.names.size() == 2);
    CHECK(seen.names[0] == "conv:created");
    CHECK(seen.names[1] == "custom:thing");
    CHECK(Json::parse(seen.payloads[1])["x"] == 1);
    CHECK(loom_unsubscribe(c.ctx, tok) == LOOM_OK);
    CHECK(loom_unsubscribe(c.ctx, tok) == LOOM_E_NOT_FOUND);
    take(loom_create_conversation(c.ctx, "evt2"));
    CHECK(seen.names.size() == 2);
    CHECK(loom_subscribe(c.ctx, "", cb, &seen) == LOOM_E_INVALID_ARGUMENT);

    // conv:created is persisted to the event log by default policy.
    Json evs = take(loom_query_events(c.ctx, R"({"type": "conv:created"})"));
    CHECK(evs.size() == 2);

    struct LogSeen {
      std::atomic<int> n{0};
    } ls;
    CHECK(loom_set_log_sink([](int, const char*, const char*, void* ud) { static_cast<LogSeen*>(ud)->n++; },
                            LOOM_LOG_INFO, &ls) == LOOM_OK);
    loom_set_log_stderr(0);
    CHECK(loom_delete_conversation(c.ctx, take(loom_create_conversation(c.ctx, "x"))["id"].get<std::string>().c_str()) ==
          LOOM_OK);  // logs "Deleted conversation"
    CHECK(ls.n.load() >= 1);
    CHECK(loom_set_log_sink(nullptr, 0, nullptr) == LOOM_OK);
    Json logs = take(loom_get_logs(5));
    CHECK(logs.is_array());
    CHECK(!logs.empty());
  }

  TEST_CASE("tasks, provenance and graph queries over the foundation") {
    Ctx c;
    Json tasks = take(loom_list_tasks(c.ctx, nullptr));
    CHECK(tasks.empty());
    CHECK(take(loom_resume_tasks(c.ctx))["recovered"] == 0);
    CHECK(is_error(take(loom_get_task(c.ctx, "t_nope")), "not_found"));
    CHECK(loom_cancel_task(c.ctx, "t_nope") == LOOM_E_NOT_FOUND);
    CHECK(take(loom_list_sources(c.ctx, 0)).empty());
    Json prov = take(loom_get_provenance(c.ctx, "c_x"));
    CHECK(prov["records"].empty());
    CHECK(take(loom_get_nodes(c.ctx, nullptr)).empty());
    CHECK(take(loom_get_edges(c.ctx, R"({"link_type": "mentions"})")).empty());
    Json g = take(loom_get_graph_data(c.ctx, nullptr));
    CHECK(g["nodes"].empty());
    CHECK(is_error(take(loom_expand_graph(c.ctx, "{}", 2)), "invalid_argument"));
  }

  TEST_CASE("unconfigured areas answer with well-formed results or errors offline") {
    Ctx c;
    struct Chunks {
      std::vector<std::string> chunks;
      int done = 0;
    } ch;
    auto cb = [](const char* chunk, int done, void* ud) {
      auto* s = static_cast<Chunks*>(ud);
      s->chunks.emplace_back(chunk);
      s->done += done;
    };
    loom_chat(c.ctx, nullptr, "hello", nullptr, -1, cb, &ch);
    // ChatEngine is implemented now: no API key configured still fails, but
    // a "start" chunk is emitted (once the user message is persisted) before
    // the final "error" chunk, so 1 or 2 chunks are both legitimate.
    REQUIRE((ch.chunks.size() == 1 || ch.chunks.size() == 2));
    CHECK(ch.done == 1);
    Json last = Json::parse(ch.chunks.back());
    CHECK(last["type"] == "error");
    CHECK(last["code"].is_string());

    Json r = take(loom_chat_ex(c.ctx, R"({"message": "hi", "bogus": 1})", nullptr, nullptr));
    CHECK(is_error(r, "invalid_argument"));
    r = take(loom_chat_ex(c.ctx, R"({"model": "x"})", nullptr, nullptr));
    CHECK(is_error(r, "invalid_argument"));
    CHECK(loom_chat_cancel(c.ctx, "rq_none") == LOOM_E_NOT_FOUND);

    CHECK(take(loom_get_models(c.ctx)).is_array());
    CHECK(take(loom_semantic_status(c.ctx)).contains("pending"));
    CHECK(take(loom_crypto_status(c.ctx))["iterations"] == 600000);
    CHECK(take(loom_media_status(c.ctx)).contains("asr"));
    CHECK(take(loom_detect_format(c.ctx, "/tmp/x.json")).contains("format"));
    for (const char* s : {loom_list_memory(c.ctx), loom_import_file(c.ctx, "/nonexistent.json", nullptr, nullptr, nullptr),
                          loom_export_conversation(c.ctx, "c_x", "json"), loom_select_context(c.ctx, "x", 2, 100),
                          loom_transcribe(c.ctx, "/x.wav", nullptr), loom_ocr(c.ctx, "/x.png", nullptr),
                          loom_crypto_encrypt(c.ctx, "x"), loom_refresh_models(c.ctx)}) {
      Json j = take(s);
      // Either a real result (once implemented) or a well-formed error.
      if (j.is_object() && j.contains("error")) {
        CHECK(j["error"]["code"].is_string());
        CHECK(j["error"]["message"].is_string());
      }
    }
  }

  TEST_CASE("platform HTTP transport injection round-trip") {
    struct Platform {
      std::vector<std::string> requests;
      bool fail = false;
    } plat;
    Ctx c;  // The callback's state outlives the context, including early failure.
    auto send = [](const char* request_json, LoomHttpResponse* resp, void* ud) -> int {
      auto* platform = static_cast<Platform*>(ud);
      platform->requests.emplace_back(request_json);
      if (platform->fail) return loom_http_response_fail(resp, LOOM_E_NETWORK, "scripted offline failure");
      if (loom_http_response_begin(resp, 200, R"({"Content-Type":"application/json"})") != LOOM_OK) return LOOM_OK;
      const char* part1 = "[";
      const char* part2 = "]";
      loom_http_response_write(resp, part1, std::strlen(part1));
      loom_http_response_write(resp, part2, std::strlen(part2));
      return LOOM_OK;
    };
    REQUIRE(loom_set_http_transport(c.ctx, send, &plat) == LOOM_OK);
    Json info = take(loom_info(c.ctx));
    CHECK(info["http_transport"] == "platform-callback");

    // This was an obsolete stub smoke call using the real default transport.
    // Exercise the same C ABI -> GitHubSync -> HTTP path with a local response.
    auto sync_dir = c.dir.path() / "empty_sync";
    std::filesystem::create_directories(sync_dir);
    std::string sync_request = Json{{"action", "status"}, {"repo", "a/b"},
                                    {"local_path", sync_dir.string()}}.dump();
    Json status = take(loom_github_sync(c.ctx, sync_request.c_str()));
    REQUIRE_FALSE(status.contains("error"));
    CHECK(status["files"] == Json::array());
    REQUIRE(plat.requests.size() == 1);
    Json request = Json::parse(plat.requests[0]);
    CHECK(request["method"] == "GET");
    CHECK(request["url"] == "https://api.github.com/repos/a/b/contents/?ref=main");
    CHECK(request["body"] == "");
    CHECK_FALSE(request["headers"].contains("Authorization"));

    // A callback failure must be returned, never retried through a live fallback.
    REQUIRE(loom_set_secret(c.ctx, "api_key", "sk-offline-test") == LOOM_OK);
    REQUIRE(loom_set_config_json(c.ctx, R"({"base_url":"https://api.test"})") == LOOM_OK);
    plat.fail = true;
    Json failure = take(loom_refresh_models(c.ctx));
    CHECK(is_error(failure, "network"));
    CHECK(failure["error"]["message"] == "scripted offline failure");
    REQUIRE(plat.requests.size() == 2);
    CHECK(Json::parse(plat.requests[1])["url"] == "https://api.test/models");

    REQUIRE(loom_set_http_transport(c.ctx, nullptr, nullptr) == LOOM_OK);
    CHECK(take(loom_info(c.ctx))["http_transport"] != "platform-callback");
    // Verify the reset API without sending anything through its live transport.
    c.restore_offline_transport();
    CHECK(take(loom_info(c.ctx))["http_transport"] == "platform-callback");
  }

  TEST_CASE("C ABI boundary hardening: NULL args, invalid UTF-8, huge inputs") {
    Ctx c;

    // NULL args on a broad sample of functions across every capi_*.cpp file
    // must return a well-formed error / negative code, never crash. (Every
    // guard_json/guard_int prologue already asserts ctx aliveness; this
    // exercises the "other required arg is NULL" branch specifically.)
    CHECK(is_error(take(loom_get_conversation(c.ctx, nullptr)), "invalid_argument"));
    CHECK(is_error(take(loom_update_conversation(c.ctx, nullptr, "{}")), "invalid_argument"));
    CHECK(loom_delete_conversation(c.ctx, nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(is_error(take(loom_get_message(c.ctx, nullptr)), "invalid_argument"));
    CHECK(is_error(take(loom_edit_message(c.ctx, nullptr, "x")), "invalid_argument"));
    CHECK(is_error(take(loom_edit_message(c.ctx, "m_x", nullptr)), "invalid_argument"));
    CHECK(loom_restore_version(c.ctx, nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(loom_set_message_status(c.ctx, nullptr, "active") == LOOM_E_INVALID_ARGUMENT);
    CHECK(loom_set_message_status(c.ctx, "m_x", nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(is_error(take(loom_search(c.ctx, nullptr, nullptr)), "invalid_argument"));
    CHECK(loom_set_secret(c.ctx, nullptr, "v") == LOOM_E_INVALID_ARGUMENT);
    CHECK(loom_set_secret(c.ctx, "k", nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(loom_has_secret(c.ctx, nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(is_error(take(loom_get_provenance(c.ctx, nullptr)), "invalid_argument"));
    CHECK(is_error(take(loom_get_task(c.ctx, nullptr)), "invalid_argument"));
    CHECK(loom_cancel_task(c.ctx, nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(loom_delete_memory(c.ctx, nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(is_error(take(loom_update_memory(c.ctx, nullptr, "{}")), "invalid_argument"));
    CHECK(is_error(take(loom_select_context(c.ctx, nullptr, 1, 100)), "invalid_argument"));
    // Every subscribe/emit/unsubscribe NULL combination.
    CHECK(loom_subscribe(nullptr, "x", [](const char*, const char*, void*) {}, nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(loom_subscribe(c.ctx, nullptr, [](const char*, const char*, void*) {}, nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(loom_subscribe(c.ctx, "x", nullptr, nullptr) == LOOM_E_INVALID_ARGUMENT);
    CHECK(loom_unsubscribe(c.ctx, -1) == LOOM_E_INVALID_ARGUMENT);
    CHECK(loom_emit(c.ctx, nullptr, "{}") == LOOM_E_INVALID_ARGUMENT);

    // Invalid UTF-8 bytes embedded in otherwise-plain C strings (never
    // parsed as JSON on the way in, e.g. a conversation title or message
    // text) must round-trip through the JSON encoder without throwing or
    // crashing: loom::json::dump() replaces bad sequences rather than
    // raising (see include/loom/util/json.h), and no capi_*.cpp caller is
    // allowed to use the throwing nlohmann dump() directly on such data.
    const char bad_utf8[] = {'h', 'i', ' ', '\xff', '\xfe', ' ', 'o', 'k', '\0'};
    Json conv = take(loom_create_conversation(c.ctx, bad_utf8));
    REQUIRE(!conv.contains("error"));
    std::string cid = conv["id"];
    CHECK(take(loom_get_conversation(c.ctx, cid.c_str()))["id"] == cid);

    Json msg = take(loom_edit_message(c.ctx, "m_nope", bad_utf8));
    CHECK(is_error(msg, "not_found"));  // reaches the DB layer without crashing

    // A JSON *document* containing invalid UTF-8 is rejected at parse time
    // (JSON strings must be valid UTF-8; nlohmann validates this) rather
    // than accepted and mishandled later.
    std::string bad_json_body = std::string("{\"title\": \"") + bad_utf8 + "\"}";
    Json upd = take(loom_update_conversation(c.ctx, cid.c_str(), bad_json_body.c_str()));
    CHECK(is_error(upd, "parse"));

    // Huge inputs: a multi-megabyte title/message text must be handled (or
    // cleanly rejected) without crashing, hanging or truncating silently in
    // a way that corrupts the JSON envelope.
    std::string huge(8 * 1024 * 1024, 'x');  // 8 MiB, well past any sane title
    Json hugeConv = take(loom_create_conversation(c.ctx, huge.c_str()));
    REQUIRE(!hugeConv.contains("error"));
    CHECK(hugeConv["title"].get<std::string>().size() == huge.size());
    CHECK(loom_delete_conversation(c.ctx, hugeConv["id"].get<std::string>().c_str()) == LOOM_OK);

    std::string huge_json = std::string(R"({"content":")") + std::string(4 * 1024 * 1024, 'y') + "\"}";
    Json hugeMem = take(loom_create_memory(c.ctx, huge_json.c_str()));
    REQUIRE(!hugeMem.contains("error"));
    CHECK(loom_delete_memory(c.ctx, hugeMem["id"].get<std::string>().c_str()) == LOOM_OK);

    // Empty-but-non-NULL strings are the other common boundary and must be
    // treated as "missing", same as NULL, not as a valid empty value.
    CHECK(is_error(take(loom_get_conversation(c.ctx, "")), "invalid_argument"));
    CHECK(loom_delete_conversation(c.ctx, cid.c_str()) == LOOM_OK);
  }

  TEST_CASE("concurrent C API use from several threads") {
    Ctx c;
    std::atomic<int> errors{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
      ts.emplace_back([&, t] {
        for (int i = 0; i < 25; ++i) {
          std::string title = "t" + std::to_string(t) + "_" + std::to_string(i);
          Json conv = take(loom_create_conversation(c.ctx, title.c_str()));
          if (conv.contains("error")) errors++;
          Json list = take(loom_list_conversations(c.ctx, 5));
          if (!list.is_array()) errors++;
          Json s = take(loom_search(c.ctx, "t", nullptr));
          if (s.contains("error")) errors++;
        }
      });
    }
    for (auto& t : ts) t.join();
    CHECK(errors.load() == 0);
    CHECK(take(loom_list_conversations(c.ctx, 1000)).size() == 100);
  }
}
