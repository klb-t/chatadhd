#include <doctest/doctest.h>

#include <sys/stat.h>

#include "loom/config.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
int mode_of(const std::filesystem::path& p) {
  struct stat st{};
  if (::stat(p.c_str(), &st) != 0) return -1;
  return static_cast<int>(st.st_mode & 0777);
}
}  // namespace

TEST_SUITE("config") {
  TEST_CASE("defaults are exactly Python DEFAULTS; no file written on fresh start") {
    fsutil::TempDir td;
    Config cfg(td.path() / "config.json");
    const Json& d = config_defaults();
    CHECK(d.size() == 12);
    CHECK(cfg.get("base_url") == "https://openrouter.ai/api/v1");
    CHECK(cfg.get("default_model") == "anthropic/claude-sonnet-4-20250514");
    CHECK(cfg.get("semantic_model") == "");
    CHECK(cfg.get("temperature") == 0.7);
    CHECK(cfg.get("max_tokens") == 4096);
    CHECK(cfg.get("theme") == "dark");
    CHECK(cfg.get("stream") == true);
    CHECK(cfg.get("semantic_analysis") == true);
    CHECK(cfg.get("graph_memory_depth") == 2);
    CHECK(cfg.get("graph_memory_max_nodes") == 20);
    CHECK(cfg.get("auto_title") == true);
    CHECK(cfg.get("missing", "fb") == "fb");
    CHECK(cfg.get("loom_task_workers") == 1);  // Loom policy default, not persisted
    CHECK(!cfg.upgraded_on_load());
    CHECK(!std::filesystem::exists(td.path() / "config.json"));
    cfg.set("theme", "amoled");
    LOOM_REQUIRE_OK(cfg.save());
    auto text = unwrap(fsutil::read_file(td.path() / "config.json"));
    CHECK(text.rfind("{\n  \"base_url\": \"https://openrouter.ai/api/v1\",\n", 0) == 0);
    CHECK(text.find("\"theme\": \"amoled\"") != std::string::npos);
    CHECK(text.find("loom_task_workers") == std::string::npos);
    CHECK(!std::filesystem::exists(td.path() / "config.tmp"));
    Config again(td.path() / "config.json");
    CHECK(again.get("theme") == "amoled");
  }

  TEST_CASE("loading preserves configured semantic model IDs and file bytes") {
    fsutil::TempDir td;
    auto p = td.path() / "config.json";
    for (const char* model : {"anthropic/claude-haiku-4-5", "x/claude-haiku-4", "local/future-model", ""}) {
      for (bool versioned : {false, true}) {
        Json content{{"semantic_model", model}, {"theme", "amoled"}};
        if (versioned) content["_config_version"] = 3;
        std::string original = json::dump(content);
        LOOM_REQUIRE_OK(fsutil::write_file(p, original));
        Config cfg(p);
        CHECK(cfg.get("semantic_model") == model);
        CHECK(cfg.get("theme") == "amoled");
        CHECK(!cfg.upgraded_on_load());
        CHECK(unwrap(fsutil::read_file(p)) == original);
        Config again(p);
        CHECK(again.get("semantic_model") == model);
        CHECK(unwrap(fsutil::read_file(p)) == original);
      }
    }
  }

  TEST_CASE("corrupt or non-object config falls back to defaults") {
    fsutil::TempDir td;
    auto p = td.path() / "config.json";
    LOOM_REQUIRE_OK(fsutil::write_file(p, "{not json"));
    Config c1(p);
    CHECK(c1.get("theme") == "dark");
    CHECK(!c1.load_error().empty());
    LOOM_REQUIRE_OK(fsutil::write_file(p, "[1, 2]"));
    Config c2(p);
    CHECK(c2.get("max_tokens") == 4096);
    CHECK(c2.load_error() == "root must be a JSON object");
  }

  TEST_CASE("secrets: chmod 600, has/get/erase, unicode kept raw") {
    fsutil::TempDir td;
    auto p = td.path() / "secrets.json";
    Secrets s(p);
    CHECK(!s.has("api_key"));
    s.set("api_key", "sk-zażółć");
    LOOM_REQUIRE_OK(s.save());
    CHECK(mode_of(p) == 0600);
    CHECK(unwrap(fsutil::read_file(p)).find("zażółć") != std::string::npos);  // ensure_ascii=False
    Secrets s2(p);
    CHECK(s2.has("api_key"));
    CHECK(s2.get_string("api_key") == "sk-zażółć");
    s2.set("empty", "");
    CHECK(!s2.has("empty"));
    s2.erase("api_key");
    LOOM_REQUIRE_OK(s2.save());
    CHECK(mode_of(p) == 0600);
    CHECK(!Secrets(p).has("api_key"));
    CHECK(s2.keys() == std::vector<std::string>{"empty"});
  }

  TEST_CASE("paths: resolution order, sentinel, subdirs") {
    fsutil::TempDir td;
    PathEnv env;
    env.home = td.path().string();
    // 3) nothing exists: first candidate created
    auto d = unwrap(resolve_data_dir(std::nullopt, env));
    CHECK(d == td.path() / ".chatadhd");
    for (const char* sub : {".chatadhd_data", "attachments", "exports", "logs"}) CHECK(std::filesystem::exists(d / sub));
    CHECK(unwrap(fsutil::read_file(d / ".chatadhd_data")) == "ChatADHD data directory\n");

    // XDG: first candidate is $XDG/chatadhd, but the sentinel in ~/.chatadhd wins.
    env.xdg_data_home = (td.path() / "xdg").string();
    CHECK(data_dir_candidates(env).front() == td.path() / "xdg" / "chatadhd");
    CHECK(unwrap(resolve_data_dir(std::nullopt, env)) == td.path() / ".chatadhd");

    // 2) env CHATADHD_DATA beats candidates; relative + ~ expansion.
    env.chatadhd_data = "~/custom/../data";
    CHECK(unwrap(resolve_data_dir(std::nullopt, env)) == td.path() / "data");
    // 1) explicit override beats env.
    auto o = unwrap(resolve_data_dir((td.path() / "over").string(), env));
    CHECK(o == td.path() / "over");
    CHECK(std::filesystem::exists(o / ".chatadhd_data"));

    PathEnv android;
    android.is_android = true;
    auto c = data_dir_candidates(android);
    REQUIRE(c.size() == 2);
    CHECK(c[0] == "/storage/emulated/0/Documents/ChatADHD");
    CHECK(c[1] == "/storage/emulated/0/Download/chatadhd_data");

    auto dp = DataPaths::for_root("/x");
    CHECK(dp.db == "/x/chatadhd.db");
    CHECK(dp.secrets == "/x/secrets.json");
    CHECK(dp.memory == "/x/memory.json");
    CHECK(dp.models == "/x/models.json");
  }
}
