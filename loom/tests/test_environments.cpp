// S2/R32 (PRZEGLAD_STRAZNIK_SRODOWISKA §3.1, §4 step 6): environments, capabilities and accesses as graph nodes, and the
// question "what can I do on my phone / in this directory?" answered from the graph. An access holds a reference to the
// secrets vault (a key name), never the bytes: the canary test looks for planted secrets in every table and every answer.
#include <doctest/doctest.h>

#include <filesystem>
#include <set>
#include <string>

#include "loom/db.h"
#include "loom/environments.h"
#include "loom/loom.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

constexpr const char* kCanaryApi = "CANARY-api-0f3a9b7c1d2e4f5a6b7c8d9e";
constexpr const char* kCanaryGithub = "CANARY-github-77aa88bb99cc00dd11ee22";
constexpr const char* kCanaryS3 = "CANARY-s3-5566778899aabbccddeeff00";

struct Fixture {
  fsutil::TempDir td;
  std::unique_ptr<Runtime> rt;
  explicit Fixture(bool with_secrets = false) {
    if (with_secrets) {
      // A configured S3 storage whose credentials live in the vault under its secret_ref prefix.
      unwrap(fsutil::ensure_dir(td.path() / "profiles"));
      unwrap(fsutil::write_file(td.path() / "profiles/storage_security.pack",
          json::dump(Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "storage_security"},
                          {"overrides", {{"storages", {{"s3_example", {{"configured", true}}}}}}}})));
    }
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    rt = unwrap(Runtime::open(o));
    if (with_secrets) {
      rt->secrets().set("api_key", kCanaryApi);
      rt->secrets().set("github_token", kCanaryGithub);
      rt->secrets().set("storage_s3_example_access_key", kCanaryS3);
      unwrap_status(rt->secrets().save());
    }
  }
  static void unwrap_status(const Status& s) { REQUIRE(bool(s)); }
  Json nodes_dump() {
    // every node and link, as the database holds them
    Json out = Json::object();
    out["nodes"] = Json::array();
    for (const auto& n : unwrap(rt->db().list_nodes(std::nullopt, 1000000))) out["nodes"].push_back(n.to_json());
    out["links"] = Json::array();
    for (const auto& l : unwrap(rt->db().get_links())) out["links"].push_back(l.to_json());
    return out;
  }
};

const Json* find_env(const Json& answer, const std::string& id) {
  for (const auto& e : answer.at("environments"))
    if (e.at("id") == id) return &e;
  return nullptr;
}

std::set<std::string> action_ids(const Json& answer) {
  std::set<std::string> ids;
  for (const auto& a : answer.at("actions")) ids.insert(a.at("action").get<std::string>());
  return ids;
}

const Json* action_row(const Json& answer, const std::string& id) {
  for (const auto& a : answer.at("actions"))
    if (a.at("action") == id) return &a;
  return nullptr;
}

}  // namespace

TEST_SUITE("environments") {
  TEST_CASE("sync writes runtimes, the data directory, storages and services as environment nodes with capabilities and accesses") {
    Fixture f;
    const Json s = unwrap(environments::sync(*f.rt));
    for (const char* id : {"runtime:android", "runtime:server", "runtime:cli", "runtime:web", "storage:local_dir", "storage:process_memory", "service:groq"})
      CHECK_MESSAGE(find_env(s, id) != nullptr, id);
    const Json list = unwrap(environments::list(*f.rt, Json{{"sync", false}}));
    const Json* phone = find_env(list, "runtime:android");
    REQUIRE(phone);
    CHECK((*phone)["class"] == "runtime_environment");
    std::set<std::string> caps;
    for (const auto& c : (*phone)["capabilities"]) caps.insert(c["id"].get<std::string>());
    // derived from devices.pack (microphone: permission on android) and from the ports hosted there
    CHECK(caps.count("capability:runtime:android:microphone") == 1);
    CHECK(caps.count("capability:runtime:android:port:asr") == 1);
    CHECK(caps.count("capability:runtime:android:port:storage") == 1);
    for (const auto& c : (*phone)["capabilities"])
      if (c["id"] == "capability:runtime:android:microphone") {
        CHECK(c["availability"] == "permission");
        CHECK(c["element_class"] == "microphone");
      }
    // a browser has a microphone (after permission) but hosts no ports; a server has no microphone
    const Json* web = find_env(list, "runtime:web");
    REQUIRE(web);
    std::set<std::string> web_caps, server_caps;
    for (const auto& c : (*web)["capabilities"]) web_caps.insert(c["id"].get<std::string>());
    for (const auto& c : (*find_env(list, "runtime:server"))["capabilities"]) server_caps.insert(c["id"].get<std::string>());
    CHECK(web_caps.count("capability:runtime:web:microphone") == 1);
    CHECK(web_caps.count("capability:runtime:web:port:asr") == 0);
    CHECK(server_caps.count("capability:runtime:server:microphone") == 0);
    CHECK(server_caps.count("capability:runtime:server:port:asr") == 1);
    // the graph holds the layer: classes of the nodes, link types
    Database& db = f.rt->db();
    CHECK(unwrap(db.list_nodes("environment", 1000)).size() >= 10);
    CHECK(unwrap(db.list_nodes("capability", 1000)).size() >= 20);
    CHECK(unwrap(db.list_nodes("access", 1000)).size() >= 5);
    std::set<std::string> link_types;
    for (const auto& l : unwrap(db.get_links())) link_types.insert(l.link_type);
    for (const char* t : {"has_capability", "accessed_via", "requires_access", "stored_in", "hosted_on", "provided_by"}) CHECK_MESSAGE(link_types.count(t) == 1, t);
  }

  TEST_CASE("sync is idempotent and removes what the sources no longer describe") {
    Fixture f;
    unwrap(environments::sync(*f.rt));
    const auto count = [&] { return f.nodes_dump(); };
    const Json first = count();
    unwrap(environments::sync(*f.rt));
    const Json second = count();
    CHECK(first["nodes"].size() == second["nodes"].size());
    CHECK(first["links"].size() == second["links"].size());
    // a capability node nobody describes any more is removed on the next sync
    Database& db = f.rt->db();
    const std::string env_id = unwrap(db.find_node("environment:runtime:server", "environment"))->id;
    const std::string stale = unwrap(db.get_or_create_node("capability:runtime:server:gone", "capability"));
    unwrap(db.create_link(env_id, stale, "has_capability"));
    const Json s = unwrap(environments::sync(*f.rt));
    CHECK(s["removed"].get<int>() >= 1);
    CHECK_FALSE(unwrap(db.find_node("capability:runtime:server:gone", "capability")).has_value());
  }

  TEST_CASE("an access holds a reference to the vault and never a secret: no canary in any node, link or answer") {
    Fixture f(true);
    const Json sync = unwrap(environments::sync(*f.rt, Json{{"path", f.td.path().string()}}));
    const Json list = unwrap(environments::list(*f.rt));
    const Json phone = unwrap(environments::can_do(*f.rt, Json{{"environment", "phone"}}));
    const Json service = unwrap(environments::can_do(*f.rt, Json{{"environment", "service:chat_provider"}}));
    const Json s3 = unwrap(environments::can_do(*f.rt, Json{{"environment", "storage:s3_example"}}));
    const Json dump = f.nodes_dump();
    const std::string all = sync.dump() + list.dump() + phone.dump() + service.dump() + s3.dump() + dump.dump();
    for (const char* canary : {kCanaryApi, kCanaryGithub, kCanaryS3}) {
      INFO("canary " << canary);
      CHECK(all.find(canary) == std::string::npos);
      // not even a fragment of it
      CHECK(all.find(std::string(canary).substr(7)) == std::string::npos);
    }
    // but the references are there: key names and whether they are set
    const Json* chat = find_env(list, "service:chat_provider");
    REQUIRE(chat);
    REQUIRE((*chat)["accesses"].size() == 1);
    const Json& acc = (*chat)["accesses"][0];
    CHECK(acc["kind"] == "api_key");
    CHECK(acc["state"] == "set");
    CHECK(acc["refs"][0]["secret_ref"] == "api_key");
    const Json* gh = find_env(list, "service:github");
    REQUIRE(gh);
    CHECK((*gh)["accesses"][0]["refs"][0] == Json{{"secret_ref", "github_token"}, {"state", "set"}});
    const Json* groq = find_env(list, "service:groq");
    REQUIRE(groq);
    CHECK((*groq)["accesses"][0]["state"] == "missing");  // declared, not set
    // the s3 storage lists the key names found under its secret_ref prefix
    const Json* st = find_env(list, "storage:s3_example");
    REQUIRE(st);
    REQUIRE((*st)["accesses"].size() == 1);
    CHECK((*st)["accesses"][0]["kind"] == "access_key");
    CHECK((*st)["accesses"][0]["refs"][0]["secret_ref"] == "storage_s3_example_access_key");
    // every access node is linked into the vault, which names its file and holds no value
    Database& db = f.rt->db();
    const auto vault = unwrap(db.find_node("vault:secrets", "secret_vault"));
    REQUIRE(vault.has_value());
    CHECK(vault->metadata["holds_values"] == false);
    int stored = 0;
    for (const auto& l : unwrap(db.get_links(vault->id, "stored_in"))) stored += l.dst == vault->id;
    CHECK(stored >= 4);
    // a value even in a reference-looking place is redacted by the one write path
    CHECK(dump.dump().find("[redacted]") == std::string::npos);
  }

  TEST_CASE("the graph is the source: what can I do on my phone") {
    Fixture f;
    const Json answer = unwrap(environments::can_do(*f.rt, Json{{"environment", "my phone"}}));
    CHECK(answer["schema"] == "loom.environment_can_do/1");
    CHECK(answer["from_graph"] == true);
    CHECK(answer["environment"]["id"] == "runtime:android");
    CHECK(answer["environment"]["resolved"] == "named");
    const auto ids = action_ids(answer);
    // a phone records sound and takes pictures (after the permission), shows notifications, runs the application's own actions
    for (const char* a : {"device.record_audio", "device.capture_image", "device.notify", "storage.status", "ports.show"}) CHECK_MESSAGE(ids.count(a) == 1, a);
    const Json* rec = action_row(answer, "device.record_audio");
    REQUIRE(rec);
    CHECK((*rec)["needs_access"][0] == "access:runtime:android:os_permission");
    CHECK((*rec).contains("effects"));
    CHECK((*rec)["via"][0]["kind"] == "capability");
    const Json* st = action_row(answer, "storage.status");
    REQUIRE(st);
    CHECK_FALSE((*st).contains("needs_access"));
    CHECK((*st)["effects"]["reads_only"].is_boolean());
    CHECK(answer["counts"]["actions"].get<int>() == static_cast<int>(answer["actions"].size()));
    // the same question in Polish, by a sentence
    const Json pl = unwrap(environments::can_do(*f.rt, Json{{"environment", "moim telefonie"}, {"lang", "pl"}}));
    CHECK(pl["environment"]["id"] == "runtime:android");
    // a server has no microphone: no recording action
    const Json server = unwrap(environments::can_do(*f.rt, Json{{"environment", "server"}}));
    CHECK(action_ids(server).count("device.record_audio") == 0);
    CHECK(action_ids(server).count("device.capture_image") == 0);
    // an unknown environment is an error that lists what is known
    auto bad = environments::can_do(*f.rt, Json{{"environment", "my submarine"}});
    REQUIRE_FALSE(bool(bad));
    CHECK(bad.error().message.find("runtime:android") != std::string::npos);
  }

  TEST_CASE("the graph is the source: what can I do in this directory") {
    Fixture f;
    fsutil::TempDir other;
    // the data directory
    const Json data = unwrap(environments::can_do(*f.rt, Json{{"environment", "this directory"}}));
    CHECK(data["environment"]["class"] == "directory_environment");
    std::set<std::string> caps;
    for (const auto& c : data["capabilities"]) caps.insert(c["id"].get<std::string>().substr(c["id"].get<std::string>().rfind(':') + 1));
    CHECK(caps.count("read_files") == 1);
    CHECK(caps.count("write_files") == 1);
    CHECK(caps.count("keep_data") == 1);
    const auto data_ids = action_ids(data);
    for (const char* a : {"device.pick_file", "device.pick_folder", "storage.status", "storage.copy"}) CHECK_MESSAGE(data_ids.count(a) == 1, a);
    // any other directory by path: asked about, added to the graph
    const Json dir = unwrap(environments::can_do(*f.rt, Json{{"path", other.path().string()}}));
    CHECK(dir["environment"]["kind"] == "directory");
    CHECK(dir["environment"]["resolved"] == "path");
    std::set<std::string> dir_caps;
    for (const auto& c : dir["capabilities"]) dir_caps.insert(c["id"].get<std::string>());
    CHECK(dir_caps.size() == 2);  // read and write, but it does not hold the Loom data
    CHECK(action_ids(dir).count("device.pick_folder") == 1);
    CHECK(action_ids(dir).count("storage.status") == 0);
    CHECK(unwrap(f.rt->db().find_node("environment:directory:" + std::filesystem::weakly_canonical(other.path()).generic_string(), "environment")).has_value());
    // a directory that does not exist has no capabilities, and the graph says so
    const Json none = unwrap(environments::can_do(*f.rt, Json{{"path", (other.path() / "nope").string()}}));
    CHECK(none["capabilities"].empty());
    CHECK(none["environment"]["kind"] == "directory");
  }

  TEST_CASE("settings: auto_sync off answers from the nodes as they are; probe_paths off does not touch the file system") {
    Fixture f;
    f.rt->config().set("environments_auto_sync", false);
    auto empty = environments::can_do(*f.rt, Json{{"environment", "phone"}});
    CHECK_FALSE(bool(empty));  // nothing was ever synced and nothing syncs by itself
    unwrap(environments::sync(*f.rt));
    CHECK(bool(environments::can_do(*f.rt, Json{{"environment", "phone"}})));
    f.rt->config().set("environments_probe_paths", false);
    fsutil::TempDir other;
    const Json dir = unwrap(environments::can_do(*f.rt, Json{{"path", other.path().string()}, {"sync", true}}));
    CHECK(dir["capabilities"].empty());
  }

  TEST_CASE("loom_invoke: environments sync, list and can_do; unknown actions are refused") {
    fsutil::TempDir dir;
    std::string opts = Json{{"data_dir", dir.path().string()}, {"start_workers", false}}.dump();
    LoomContext* ctx = loom_init_ex(opts.c_str(), nullptr);
    REQUIRE(ctx != nullptr);
    auto call = [&](const char* action, const Json& req) {
      const char* s = loom_invoke(ctx, "environments", action, req.dump().c_str());
      REQUIRE(s != nullptr);
      Json j = unwrap(json::parse(s));
      loom_free_string(s);
      return j;
    };
    const Json can = call("can_do", Json{{"environment", "phone"}});
    CHECK(can["schema"] == "loom.environment_can_do/1");
    CHECK(action_ids(can).count("device.record_audio") == 1);
    CHECK(call("list", Json::object())["schema"] == "loom.environments_list/1");
    CHECK(call("sync", Json::object())["schema"] == "loom.environments_sync/1");
    CHECK(call("explode", Json::object()).contains("error"));
    // the action of the catalogue reaches the same port
    const char* cat = loom_invoke(ctx, "actions", "list", "{}");
    const Json list = unwrap(json::parse(cat));
    loom_free_string(cat);
    int found = 0;
    for (const auto& a : list["actions"])
      if (a["id"] == "environments.can_do") {
        ++found;
        CHECK(a["port"] == "environments");
        CHECK(a["status"] == "implemented");
        CHECK(a["effects"]["sends_off_device"] == false);
      }
    CHECK(found == 1);
    loom_shutdown(ctx);
  }
}
