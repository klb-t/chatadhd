// New synthetic M2 suite. Native compilation/execution is reserved for integrator.
#include <doctest/doctest.h>
#include "loom/url_share_import.h"
#include "loom/loom.h"
#include <atomic>
#include <cstdlib>
#include "loom/provenance.h"
#include "loom/util/base64.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"
using namespace loom;
using loom::test::unwrap;
namespace {
Json request(const std::string& path = "/share", const std::string& mode = "share") {
  return {{"schema", "loom.url_import_request/1"}, {"url", "http://fixture.example.invalid" + path}, {"mode", mode}};
}
Json fetch_fixture(const Json& r) {
  const auto worker = std::filesystem::path(__FILE__).parent_path() / "url_share/fixture_worker.py";
  return unwrap(url_share::fetch(r, unwrap(url_share::policy()), worker));
}
struct Store {
  fsutil::TempDir dir;
  std::unique_ptr<Database> db = loom::test::open_db(dir.path() / "data.db");
  BlobStore blobs{dir.path() / "blobs", *db};
  ProvenanceStore prov{*db};
  Json save(const Json& s) { return unwrap(url_share::store(*db, blobs, prov, s)); }
};
Json static_snapshot() {
  const std::string raw = R"({"conversation":{"title":"Synthetic"},"messages":[{"role":"user","text":"Żółw 🐢"}]})";
  return {{"schema", "loom.url_snapshot/1"}, {"mode", "share"}, {"llm_calls", 0},
    {"resource", {{"source_url", "https://example.invalid/share"}, {"normalized_url", "https://example.invalid/share"},
                  {"final_url", "https://example.invalid/share"}, {"status", "complete"}, {"reason", "fetched"},
                  {"mime", "application/json"}, {"sha256", Sha256::hex(raw)}, {"body_base64", base64::encode(raw)}}},
    {"parsed", {{"status", "complete"}, {"reason", "provided_messages"}, {"title", "Synthetic"}, {"scope", "provided_messages_only"},
                {"messages", Json::array({Json{{"role", "user"}, {"text", "Żółw 🐢"}, {"pointer", "/messages/0"}, {"attachments", Json::array()}}})}}}};
}
}
TEST_SUITE("url_share_import") {
  TEST_CASE("provider share tree references provenance idempotency and reopen") {
    const Json input{{"schema", "loom.url_import_request/1"}, {"mode", "share"},
                     {"url", "https://chatgpt.com/share/synthetic-share"}};
    const auto worker = std::filesystem::path(__FILE__).parent_path() / "url_share/provider_fixture_worker.py";
    const auto snapshot = unwrap(url_share::fetch(input, unwrap(url_share::policy()), worker));
    REQUIRE(snapshot["parsed"]["status"] == "complete");
    Store s;
    const auto result = s.save(snapshot);
    CHECK(result["adapter"] == "chatgpt.share_html/1");
    CHECK(result["branch"]["selected_node"] == "tool");
    CHECK(result["messages"] == 4); CHECK(result["llm_calls"] == 0);
    REQUIRE(result["attachments"].size() == 2);
    for (const auto& asset : result["attachments"]) {
      CHECK(asset["status"] == "reference"); CHECK_FALSE(asset.contains("blob_hash"));
    }
    const auto cid = result["conversations"][0]["id"].get<std::string>();
    const auto original = unwrap(s.prov.get_source(result["source_id"].get<std::string>()));
    REQUIRE(original);
    CHECK(unwrap(s.blobs.read(original->blob_hash)) == unwrap(base64::decode(snapshot["resource"]["body_base64"].get<std::string>())));
    CHECK(s.save(snapshot)["already_imported"] == true);
    s.db->close();
    auto reopened = loom::test::open_db(s.dir.path() / "data.db");
    BlobStore blobs(s.dir.path() / "blobs", *reopened); ProvenanceStore prov(*reopened);
    const auto all = unwrap(reopened->get_msgs(cid, true));
    REQUIRE(all.size() == 4);
    const auto& rows = snapshot["parsed"]["messages"];
    for (std::size_t i = 0; i < all.size(); ++i) {
      CHECK(all[i].role == rows[i]["role"].get<std::string>());
      CHECK(all[i].text == rows[i]["text"].get<std::string>());
      CHECK(all[i].semantic_status == "done"); CHECK(all[i].attachments.empty());
      CHECK(all[i].status == (rows[i]["active"].get<bool>() ? "active" : "version"));
      const auto origins = unwrap(prov.for_subject(all[i].id));
      REQUIRE(origins.size() == 1);
      CHECK(origins[0].locator["embedded_data"]["format"] == "turbo_stream_v2");
      CHECK(origins[0].locator["embedded_data"]["json_pointer"] == rows[i]["pointer"]);
    }
    CHECK_FALSE(all[0].parent_id);
    CHECK(all[1].parent_id == all[0].id); CHECK(all[2].parent_id == all[0].id);
    CHECK(all[3].parent_id == all[2].id);
    const auto active = unwrap(reopened->get_msgs(cid));
    REQUIRE(active.size() == 3);
    CHECK(active[1].text == "Wybrana odpowiedź\n"); CHECK(active[2].text.empty());
    CHECK(unwrap(reopened->count_pending_semantic()) == 0);
    const auto repeated = unwrap(url_share::store(*reopened, blobs, prov, snapshot));
    CHECK(repeated["already_imported"] == true); CHECK(repeated["conversations"][0]["id"] == cid);
  }

  TEST_CASE("provider unresolved or duplicate parent pointers roll back") {
    for (const bool duplicate : {false, true}) {
      Store s; auto snapshot = static_snapshot();
      auto counts = [](Database& db) {
        auto statement = unwrap(db.conn().prepare(
            "SELECT (SELECT count(*) FROM messages), (SELECT count(*) FROM loom_sources), "
            "(SELECT count(*) FROM loom_provenance)"));
        REQUIRE(unwrap(statement.step()));
        return Json::array({statement.get_int(0), statement.get_int(1), statement.get_int(2)});
      };
      const auto before = counts(*s.db);
      auto& row = snapshot["parsed"]["messages"][0];
      row["parent_pointer"] = duplicate ? Json(nullptr) : Json("/missing");
      if (duplicate) snapshot["parsed"]["messages"].push_back(row);
      CHECK_FALSE(url_share::store(*s.db, s.blobs, s.prov, snapshot));
      CHECK(unwrap(s.db->list_convs()).empty()); CHECK(unwrap(s.prov.list_sources()).empty());
      CHECK(unwrap(s.db->count_pending_semantic()) == 0);
      CHECK(counts(*s.db) == before); // all DB rows, not only the visible conversation list
      s.db->close();
      auto reopened = loom::test::open_db(s.dir.path() / "data.db");
      CHECK(counts(*reopened) == before);
    }
  }

#if defined(__unix__) && !defined(__ANDROID__)
  // Integrator: run ONLY this case from copied build artifacts, in an environment
  // without source checkout mounts. It uses no fixture/helper source path.
  TEST_CASE("deployed artifact default fetch and cleanup without checkout") {
    fsutil::TempDir isolated;
    const auto previous_cwd = std::filesystem::current_path();
    const char* old_tmp = std::getenv("TMPDIR");
    const std::optional<std::string> previous_tmp = old_tmp ? std::optional<std::string>(old_tmp) : std::nullopt;
    struct Restore {
      std::filesystem::path cwd;
      std::optional<std::string> tmp;
      ~Restore() {
        std::error_code ec; std::filesystem::current_path(cwd, ec);
        if (tmp) ::setenv("TMPDIR", tmp->c_str(), 1); else ::unsetenv("TMPDIR");
      }
    } restore{previous_cwd, previous_tmp};
    REQUIRE(::setenv("TMPDIR", isolated.path().c_str(), 1) == 0);
    std::filesystem::current_path(isolated.path());
    const Json input{{"schema", "loom.url_import_request/1"}, {"mode", "page"}, {"url", "http://127.0.0.1/"}};
    const auto p = unwrap(url_share::policy());
    const auto result = unwrap(url_share::fetch(input, p)); // default, no isolated_worker
    CHECK(result["schema"] == "loom.url_snapshot/1");
    CHECK(result["parsed"]["reason"] == "address_not_public"); // helper actually executed
    CHECK(result["parsed"]["status"] == "denied"); CHECK(result["llm_calls"] == 0);
    CHECK(std::filesystem::is_empty(isolated.path()));
    auto capped = p; capped["limits"]["worker_output_bytes"] = 1;
    const auto overflow = unwrap(url_share::fetch(input, capped));
    CHECK(overflow["parsed"]["reason"] == "worker_output_oversize");
    CHECK(std::filesystem::is_empty(isolated.path()));
    // Deterministic watchdog/child-error probes use ONLY the trusted C++ seam.
    const auto script = isolated.path() / "slow.py";
    LOOM_REQUIRE_OK(fsutil::write_file(script, "import time\ntime.sleep(10)\n"));
    auto timed = p; timed["limits"]["worker_timeout_ms"] = 50;
    CHECK(unwrap(url_share::fetch(input, timed, script))["parsed"]["reason"] == "timeout");
    std::filesystem::remove(script);
    CHECK(std::filesystem::is_empty(isolated.path()));
    const auto probe = isolated.path() / "permissions.py";
    LOOM_REQUIRE_OK(fsutil::write_file(probe, R"PY(import json, os, pathlib, stat
request_path = pathlib.Path(os.readlink('/proc/self/fd/0'))
print(json.dumps({'schema':'loom.url_snapshot/1', 'request_mode':stat.S_IMODE(request_path.stat().st_mode), 'directory_mode':stat.S_IMODE(request_path.parent.stat().st_mode)}))
)PY"));
    const auto modes = unwrap(url_share::fetch(input, p, probe));
    CHECK(modes["request_mode"] == 0600);
    CHECK(modes["directory_mode"] == 0700);
    std::filesystem::remove(probe);
    CHECK(std::filesystem::is_empty(isolated.path()));
    const auto broken = isolated.path() / "broken.py";
    LOOM_REQUIRE_OK(fsutil::write_file(broken, "raise RuntimeError('synthetic')\n"));
    CHECK(unwrap(url_share::fetch(input, p, broken))["parsed"]["reason"] == "url_worker_failed");
    std::filesystem::remove(broken);
    CHECK(std::filesystem::is_empty(isolated.path()));
  }
#endif

  TEST_CASE("existing C API import action reaches URL hook and never calls model transport") {
    fsutil::TempDir dir;
    const auto options = Json{{"data_dir", dir.path().string()}, {"start_workers", false}}.dump();
    const char* error = nullptr;
    std::unique_ptr<LoomContext, decltype(&loom_shutdown)> ctx(loom_init_ex(options.c_str(), &error), loom_shutdown);
    if (error) { INFO(error); loom_free_string(error); }
    REQUIRE(ctx);
    std::atomic<int> calls{0};
    auto deny = [](const char*, LoomHttpResponse* response, void* ud) {
      static_cast<std::atomic<int>*>(ud)->fetch_add(1);
      return loom_http_response_fail(response, LOOM_E_NETWORK, "unexpected model transport");
    };
    REQUIRE(loom_set_http_transport(ctx.get(), deny, &calls) == LOOM_OK);
    const auto descriptor = dir.path() / "synthetic.loomurl";
    Json r = request(); r["url"] = "http://127.0.0.1/";
    LOOM_REQUIRE_OK(fsutil::write_file(descriptor, r.dump()));
    const auto* raw = loom_import_file_ex(ctx.get(), descriptor.string().c_str(), "{}", nullptr, nullptr);
    REQUIRE(raw);
    auto parsed = json::parse(raw); loom_free_string(raw);
    REQUIRE(parsed);
    CHECK_FALSE(parsed->contains("error"));
    CHECK((*parsed)["format"] == "url"); CHECK((*parsed)["status"] != "complete");
    CHECK((*parsed)["conversations"].empty()); CHECK(calls.load() == 0);
  }

  TEST_CASE("transaction idempotence changed source revision and explicit analysis opt out") {
    Store s; const auto input = static_snapshot();
    const auto first = s.save(input); const auto same = s.save(input);
    CHECK(same["already_imported"] == true);
    CHECK(same["source_id"] == first["source_id"]);
    CHECK(same["conversations"] == first["conversations"]);
    CHECK(unwrap(s.db->count_pending_semantic()) == 0);
    Json changed = input;
    const std::string raw = R"({"conversation":{"title":"Synthetic"},"messages":[{"role":"user","text":"Changed"}]})";
    changed["resource"]["body_base64"] = base64::encode(raw);
    changed["resource"]["sha256"] = Sha256::hex(raw);
    changed["parsed"]["messages"][0]["text"] = "Changed";
    const auto next = s.save(changed);
    CHECK(next["source_id"] != first["source_id"]);
    CHECK(next["previous_source_id"] == first["source_id"]);
    CHECK(next["conversations"][0]["id"] != first["conversations"][0]["id"]);
    const auto original = unwrap(s.prov.get_source(first["source_id"].get<std::string>()));
    REQUIRE(original);
    CHECK(unwrap(s.blobs.read(original->blob_hash)) == unwrap(base64::decode(input["resource"]["body_base64"].get<std::string>())));
    CHECK(unwrap(s.db->list_convs()).size() == 2);
  }
  TEST_CASE("bad hash rolls back native rows and does not enqueue semantic work") {
    Store s; Json input = static_snapshot(); input["resource"]["sha256"] = "wrong";
    CHECK_FALSE(url_share::store(*s.db, s.blobs, s.prov, input));
    CHECK(unwrap(s.db->list_convs()).empty()); CHECK(unwrap(s.prov.list_sources()).empty());
    CHECK(unwrap(s.db->count_pending_semantic()) == 0);
  }
  TEST_CASE("reopen preserves exact roles order Unicode empty text attachments and provenance") {
    Store s;
    const auto snapshot = fetch_fixture(request());
    const auto saved = s.save(snapshot);
    CHECK(saved["status"] == "partial"); CHECK(saved["llm_calls"] == 0);
    CHECK(saved["messages"] == 5);
    CHECK(saved["attachments"][0]["status"] == "complete");
    CHECK(saved["attachments"][1]["status"] == "missing");
    CHECK(saved["attachments"][2]["status"] == "unsupported");
    const auto cid = saved["conversations"][0]["id"].get<std::string>();
    s.db->close();
    auto reopened = loom::test::open_db(s.dir.path() / "data.db");
    BlobStore blobs(s.dir.path() / "blobs", *reopened); ProvenanceStore prov(*reopened);
    const auto messages = unwrap(reopened->get_msgs(cid));
    REQUIRE(messages.size() == snapshot["parsed"]["messages"].size());
    for (std::size_t i = 0; i < messages.size(); ++i) {
      CHECK(messages[i].role == snapshot["parsed"]["messages"][i]["role"].get<std::string>());
      CHECK(messages[i].text == snapshot["parsed"]["messages"][i]["text"].get<std::string>());
      CHECK(messages[i].metadata["url_import"]["sequence"] == i);
      CHECK(messages[i].semantic_status == "done");
      const auto origins = unwrap(prov.for_subject(messages[i].id));
      REQUIRE(origins.size() == 1);
      CHECK(origins[0].locator["json_pointer"] == "/messages/" + std::to_string(i));
      if (i) CHECK(messages[i].parent_id == messages[i - 1].id);
    }
    CHECK(unwrap(reopened->count_pending_semantic()) == 0);
    const auto asset = unwrap(prov.get_source(saved["attachments"][0]["source_id"].get<std::string>()));
    REQUIRE(asset);
    CHECK(unwrap(blobs.read(asset->blob_hash)) == std::string("synthetic attachment\0\xff\n", 23));
    const auto repeated = unwrap(url_share::store(*reopened, blobs, prov, snapshot));
    CHECK(repeated["already_imported"] == true); CHECK(repeated["conversations"][0]["id"] == cid);
  }
  TEST_CASE("isolated HTTP to native page source and redirect statuses") {
    Store s;
    const auto page = s.save(fetch_fixture(request("/redirect", "page")));
    CHECK(page["status"] == "complete"); CHECK(page["final_url"] == "http://fixture.example.invalid/page");
    const auto cid = page["conversations"][0]["id"].get<std::string>();
    CHECK(unwrap(s.db->get_msgs(cid))[0].text == "Rzeczywista treść strony — żółw 🐢");
    for (const auto* path : {"/forbidden", "/missing", "/private", "/title"}) {
      const auto rejected = s.save(fetch_fixture(request(path)));
      CHECK(rejected["status"] != "complete"); CHECK(rejected["conversations"].empty());
    }
  }
  TEST_CASE("production worker rejects private URL with no test transport toggle") {
    auto r = request(); r["url"] = "http://127.0.0.1/";
    const auto result = unwrap(url_share::fetch(r, unwrap(url_share::policy())));
    CHECK(result["parsed"]["status"] != "complete"); CHECK(result["parsed"]["messages"].empty());
    r["allow_private"] = true;
    CHECK_FALSE(url_share::fetch(r, unwrap(url_share::policy())));
  }
  TEST_CASE("policy overrides cannot replace the executable or the transport") {
    CHECK_FALSE(url_share::policy({{"worker", {{"python", "/tmp/untrusted"}}}}));
    CHECK_FALSE(url_share::policy({{"allow_private", true}}));
    CHECK_FALSE(url_share::policy({{"limits", {{"max_bytes", -1}}}}));
    const auto p = unwrap(url_share::policy({{"limits", {{"max_bytes", 4096}, {"max_redirects", 0}}}}));
    CHECK(p["limits"]["max_bytes"] == 4096); CHECK(p["limits"]["max_redirects"] == 0);
  }
}
