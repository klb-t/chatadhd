#include <doctest/doctest.h>

#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "loom/config.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
Json historical_config() {
  return Json{{"base_url", "https://openrouter.ai/api/v1"},
              {"default_model", "anthropic/claude-sonnet-4-20250514"},
              {"semantic_model", ""}, {"temperature", 0.7}, {"max_tokens", 4096},
              {"theme", "dark"},
              {"system_prompt", "You are a helpful assistant with access to the user's hierarchical memory."},
              {"auto_title", true}, {"stream", true}, {"semantic_analysis", true},
              {"graph_memory_depth", 2}, {"graph_memory_max_nodes", 20}};
}

Json historical_keys() {
  return Json::array({"base_url", "default_model", "semantic_model", "temperature", "max_tokens",
                      "theme", "system_prompt", "auto_title", "stream", "semantic_analysis",
                      "graph_memory_depth", "graph_memory_max_nodes"});
}

// Exercise the proposed universal recipe: match injected facts, render each
// inert segment, then join paths. This does not assert installed bootstrap wiring.
std::vector<fs::path> candidates(const RuntimeProfile& profile, const PathEnv& env) {
  const auto& values = profile.values();
  const Json facts{{"is_android", env.is_android}, {"has_xdg_data_home", env.xdg_data_home.has_value()}};
  const Json variables{{"home", env.home.value_or(values.at("missing_home").get<std::string>())},
                       {"xdg_data_home", env.xdg_data_home.value_or("")}};
  for (const auto& rule : values.at("candidate_rules")) {
    bool matches = true;
    for (auto it = rule.at("when").begin(); it != rule.at("when").end(); ++it) {
      if (!facts.contains(it.key()) || facts.at(it.key()) != it.value()) matches = false;
    }
    if (!matches) continue;
    std::vector<fs::path> paths;
    for (const auto& segments : rule.at("path_segments")) {
      fs::path path;
      for (const auto& segment : segments)
        path /= unwrap(render_profile_template(segment.get<std::string>(), variables));
      paths.push_back(std::move(path));
    }
    return paths;
  }
  return {};
}
}  // namespace

TEST_SUITE("runtime_bootstrap_profiles") {
  TEST_CASE("usage preset equals the historical six fields and retains floating identity") {
    const auto profile = unwrap(RuntimeProfile::builtin("usage_policy"));
    const Json expected{{"schema", "loom.usage_policy/1"}, {"growth_factor", 10.0},
                        {"baseline_window", 32}, {"ledger_busy_timeout_ms", 30000},
                        {"include_reservations", true}, {"initial_baselines", Json::object()}};
    CHECK(profile.values() == expected);
    CHECK(profile.values().dump() == expected.dump());
    CHECK(profile.values().at("growth_factor").is_number_float());
    CHECK(profile.values().at("growth_factor").dump() == "10.0");
  }

  TEST_CASE("usage descriptor preserves nullable quantities and open extension fields") {
    const auto profile = unwrap(RuntimeProfile::builtin("usage_policy"));
    const auto changed = unwrap(profile.with_overrides(
        Json{{"baseline_window", nullptr},
             {"initial_baselines", Json{{"offline-research", Json{{"tokens", nullptr}, {"bytes", 0.0}}}}},
             {"owner_extension", Json{{"note", "synthetic"}}}}));
    CHECK(changed.values().at("baseline_window").is_null());
    CHECK(changed.values().at("initial_baselines").at("offline-research").at("tokens").is_null());
    CHECK(changed.values().at("owner_extension").at("note") == "synthetic");
    CHECK(profile.with_overrides(Json{{"baseline_window", 1000000}}));
    CHECK(profile.with_overrides(Json{{"baseline_window", std::numeric_limits<std::int64_t>::max()}}));
    CHECK_FALSE(profile.with_overrides(Json{{"baseline_window", std::numeric_limits<std::uint64_t>::max()}}));
    CHECK_FALSE(profile.with_overrides(Json{{"baseline_window", 0}}));
    CHECK_FALSE(profile.with_overrides(Json{{"initial_baselines", Json{{"offline-research", Json{{"bytes", -1}}}}}}));
    // Native W2 validation remains necessary for factor>1, names and complete
    // policy/lifecycle semantics; this descriptor does not grant execution.
  }

  TEST_CASE("config recipe preserves the exact twelve-key order and separate Loom fallbacks") {
    const auto profile = unwrap(RuntimeProfile::builtin("config"));
    const auto& values = profile.values();
    CHECK(values.at("legacy_defaults") == historical_config());
    CHECK(values.at("legacy_defaults").dump() == historical_config().dump());
    CHECK(values.at("legacy_key_order") == historical_keys());
    Json actual_keys = Json::array();
    for (auto it = values.at("legacy_defaults").begin(); it != values.at("legacy_defaults").end(); ++it)
      actual_keys.push_back(it.key());
    CHECK(actual_keys == historical_keys());
    const Json expected{{"loom_event_log_types", Json::array({"conv:created", "import:done", "graph:changed"})},
                        {"loom_task_workers", 1}};
    CHECK(values.at("loom_defaults") == expected);
    CHECK_FALSE(values.at("legacy_defaults").contains("loom_task_workers"));
    CHECK_FALSE(values.at("loom_defaults").contains("loom_usage_policy"));
  }

  TEST_CASE("historical fresh Config does not write and existing bytes remain untouched") {
    fsutil::TempDir tmp;
    REQUIRE(tmp.valid());
    const auto path = tmp.path() / "config.json";
    Config fresh(path);
    CHECK(fresh.all().dump() == historical_config().dump());
    CHECK_FALSE(fs::exists(path));
    CHECK_FALSE(fresh.upgraded_on_load());
    const std::string existing = "{\n  \"default_model\": \"owner/synthetic-model\",\n  \"owner_extra\": {\"preserved\": true}\n}\n";
    unwrap(fsutil::write_file(path, existing));
    Config stored(path);
    CHECK(stored.get("default_model") == "owner/synthetic-model");
    CHECK(stored.get("owner_extra") == Json{{"preserved", true}});
    CHECK_FALSE(stored.upgraded_on_load());
    CHECK(unwrap(fsutil::read_file(path)) == existing);
    CHECK(stored.get("loom_task_workers") == 1);
    CHECK_FALSE(stored.all().contains("loom_task_workers"));
  }

  TEST_CASE("candidate recipes preserve platform order and filesystem joining") {
    const auto profile = unwrap(RuntimeProfile::builtin("runtime_paths"));
    PathEnv env;
    env.is_android = true;
    const std::vector<fs::path> android{"/storage/emulated/0/Documents/ChatADHD", "/storage/emulated/0/Download/chatadhd_data"};
    CHECK(candidates(profile, env) == android);
    CHECK(candidates(profile, env) == data_dir_candidates(env));
    env.is_android = false;
    const std::vector<fs::path> absent_home{"/.chatadhd"};
    CHECK(candidates(profile, env) == absent_home);
    CHECK(candidates(profile, env) == data_dir_candidates(env));
    env.home = "/synthetic/home/";
    const std::vector<fs::path> home{"/synthetic/home/.chatadhd"};
    CHECK(candidates(profile, env) == home);
    CHECK(candidates(profile, env) == data_dir_candidates(env));
    env.xdg_data_home = "/synthetic/xdg/";
    const std::vector<fs::path> xdg{"/synthetic/xdg/chatadhd", "/synthetic/home/.chatadhd"};
    CHECK(candidates(profile, env) == xdg);
    CHECK(candidates(profile, env) == data_dir_candidates(env));
    CHECK(profile.values().at("resolution_precedence") ==
          Json::array({"explicit_override", "nonempty_data_environment", "first_sentinel_candidate", "first_candidate"}));
    CHECK(profile.values().at("process_inputs") ==
          Json{{"chatadhd_data", "CHATADHD_DATA"}, {"xdg_data_home", "XDG_DATA_HOME"}, {"home", "HOME"}});
  }

  TEST_CASE("sentinel bytes directory order and every DataPaths suffix equal the startup baseline") {
    const auto profile = unwrap(RuntimeProfile::builtin("runtime_paths"));
    const auto& values = profile.values();
    CHECK(values.at("sentinel").at("filename") == ".chatadhd_data");
    CHECK(values.at("sentinel").at("content") == "ChatADHD data directory\n");
    CHECK(values.at("initialize_subdirs") == Json::array({"attachments", "exports", "logs"}));
    const Json suffixes{{"db", "chatadhd.db"}, {"fts_index", "chatadhd.fts.db"}, {"config", "config.json"},
                        {"secrets", "secrets.json"}, {"memory", "memory.json"}, {"models", "models.json"},
                        {"github_sync", "github_sync.json"}, {"attachments", "attachments"},
                        {"exports", "exports"}, {"logs", "logs"}, {"blobs", "blobs"}};
    CHECK(values.at("data_path_suffixes") == suffixes);
    const fs::path root("/synthetic/root");
    const auto paths = DataPaths::for_root(root);
    CHECK(paths.db == root / "chatadhd.db");
    CHECK(paths.fts_index == root / "chatadhd.fts.db");
    CHECK(paths.config == root / "config.json");
    CHECK(paths.secrets == root / "secrets.json");
    CHECK(paths.memory == root / "memory.json");
    CHECK(paths.models == root / "models.json");
    CHECK(paths.github_sync == root / "github_sync.json");
    CHECK(paths.attachments == root / "attachments");
    CHECK(paths.exports == root / "exports");
    CHECK(paths.logs == root / "logs");
    CHECK(paths.blobs == root / "blobs");
    fsutil::TempDir tmp;
    REQUIRE(tmp.valid());
    const auto initialized = tmp.path() / "data";
    unwrap(ensure_data_dir(initialized));
    CHECK(unwrap(fsutil::read_file(initialized / ".chatadhd_data")) == "ChatADHD data directory\n");
    for (const auto& name : {"attachments", "exports", "logs"}) CHECK(fs::is_directory(initialized / name));
    CHECK_FALSE(fs::exists(initialized / "blobs"));
  }
}
