#include <doctest/doctest.h>

#include <fstream>
#include <set>

#include "loom/config.h"
#include "loom/providers.h"
#include "loom/settings_fields.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("settings_fields") {
  TEST_CASE("metadata and real defaults agree in both directions") {
    Json doc = unwrap(settings_fields());
    CHECK(settings_fields_problems(doc).empty());
    std::set<std::string> described;
    for (const auto& f : doc["fields"]) {
      if (f["scope"] == "config") described.insert(f["key"].get<std::string>());
    }
    for (const Json* d : {&config_defaults(), &loom_config_defaults()})
      for (auto it = d->begin(); it != d->end(); ++it) {
        INFO("default key without description: ", it.key());
        CHECK(described.count(it.key()) == 1);
      }
    // The other direction: a field declared "present" must have a real default with that value.
    for (const auto& f : doc["fields"]) {
      if (f["scope"] != "config") continue;
      const std::string k = f["key"].get<std::string>();
      const bool real = config_defaults().contains(k) || loom_config_defaults().contains(k);
      INFO("field ", k);
      CHECK((f["default"] == "present") == real);
      if (real) CHECK(f["default_value"] == (config_defaults().contains(k) ? config_defaults()[k] : loom_config_defaults()[k]));
    }
  }

  TEST_CASE("every secret key used by a provider is described, without values") {
    Json doc = unwrap(settings_fields());
    std::set<std::string> secrets;
    for (const auto& f : doc["fields"])
      if (f["scope"] == "secret") {
        secrets.insert(f["key"].get<std::string>());
        CHECK(f["sensitive"] == true);
        CHECK(!f.contains("default_value"));
        CHECK(f["default"] == "absent");
      }
    for (const char* k : {"api_key", "anthropic_batch_key", "github_token", "groq_api_key",
                          "google_speech_api_key", "ocr_space_api_key"})
      CHECK(secrets.count(k) == 1);
  }

  TEST_CASE("problems detects missing description, wrong default and undescribed defaults") {
    Json doc = unwrap(settings_fields());
    Json broken = doc;
    broken["fields"].erase(broken["fields"].begin());  // drop base_url
    CHECK(!settings_fields_problems(broken).empty());
    Json wrong = doc;
    for (auto& f : wrong["fields"]) if (f["key"] == "theme") f["default"] = "absent";
    CHECK(!settings_fields_problems(wrong).empty());
    Json nodesc = doc;
    nodesc["fields"][0]["description"] = "";
    CHECK(!settings_fields_problems(nodesc).empty());
  }

  TEST_CASE("only read_only is enforced; undescribed keys stay writable") {
    CHECK(check_config_write("theme"));
    CHECK(check_config_write("loom_usage_policy"));
    CHECK(check_config_write("some_future_key"));
    CHECK(!check_config_write("_config_version"));
    CHECK(settings_edit_rule("config", "_config_version") == "read_only");
    CHECK(settings_edit_rule("config", "some_future_key").empty());
  }

  TEST_CASE("user overlay rules change the edit label (open string) and can lift read_only") {
    // The edit rules live in the one rule store (profile action_rules); the overlay merges by rule id.
    fsutil::TempDir td;
    std::filesystem::create_directories(td.path() / "profiles");
    auto write = [&](const Json& rules) {
      std::ofstream(td.path() / "profiles" / "action_rules.pack")
          << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "action_rules"},
                  {"overrides", Json{{"rules", rules}}}}.dump();
    };
    write(Json{{"x", Json{{"id", "x"}, {"scope", "global"}, {"action", "edit"}, {"effect", "needs_review"},
                          {"target", Json{{"kind", "settings_field"}, {"field_scope", "config"}, {"key", "theme"}}}}},
               {"edit:config:_config_version", Json{{"disabled", true}}}});
    auto doc = settings_fields(td.path());
    REQUIRE(doc);
    for (const auto& f : (*doc)["fields"]) {
      if (f["key"] == "theme") CHECK(f["edit"] == "needs_review");
      if (f["key"] == "_config_version") CHECK(f["edit"] == "optional");  // rule switched off: no rule = allow
    }
    CHECK(check_config_write("_config_version", td.path()));
    CHECK(check_config_write("theme", td.path()));  // unknown effect is not enforced
  }

  TEST_CASE("edit rules are general rules over (target, action)") {
    Json doc = unwrap(settings_fields());
    for (const auto& f : doc["fields"]) if (f["key"] == "_config_version") {
      CHECK(f["rule"]["action"] == "edit");
      CHECK(f["rule"]["effect"] == "deny");
    }
  }
}
