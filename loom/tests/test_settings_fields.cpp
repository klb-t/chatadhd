#include <doctest/doctest.h>

#include <fstream>
#include <map>
#include <set>

#include "loom/config.h"
#include "loom/providers.h"
#include "loom/runtime_profile.h"
#include "loom/settings_fields.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
std::string joined(const std::vector<std::string>& problems) {
  std::string out;
  for (const auto& p : problems) out += p + "\n";
  return out;
}
}  // namespace

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

  // ── M1-D0: settings as humans read them (groups + human per field, runtime-profile values as fields) ──

  TEST_CASE("every field has a Polish label and a group; the groups partition the fields (count from the pack, not a literal)") {
    Json doc = unwrap(settings_fields());
    const RuntimeProfile pack_profile = unwrap(RuntimeProfile::builtin("settings_fields"));
    const Json& pack = pack_profile.values();
    CHECK(doc["fields"].size() == pack["fields"].size());
    CHECK(doc["profile_values"].size() == pack["profile_values"].size());
    REQUIRE(doc["groups"].is_object());
    REQUIRE_FALSE(doc["groups"].empty());
    std::map<std::string, std::size_t> per_group;
    for (auto it = doc["groups"].begin(); it != doc["groups"].end(); ++it) {
      INFO("group ", it.key());
      CHECK_FALSE(it.value()["label"]["pl"].get<std::string>().empty());
      CHECK_FALSE(it.value()["label"]["en"].get<std::string>().empty());
      CHECK_FALSE(it.value()["why"]["pl"].get<std::string>().empty());
      CHECK(it.value()["order"].is_number());
      per_group[it.key()] = 0;
    }
    auto count = [&](const Json& entry, const std::string& who) {
      INFO(who);
      REQUIRE(entry.contains("human"));
      const Json& h = entry["human"];
      CHECK_FALSE(h["label"]["pl"].get<std::string>().empty());
      CHECK_FALSE(h["label"]["en"].get<std::string>().empty());
      CHECK_FALSE(h["why"]["pl"].get<std::string>().empty());
      CHECK_FALSE(h["effect"]["pl"].get<std::string>().empty());
      REQUIRE(h["levels"].is_array());
      // settable nowhere is only what a read_only field says (its rule carries the reason)
      if (!entry.contains("rule") || entry["rule"]["effect"] != "deny") CHECK_FALSE(h["levels"].empty());
      const std::string g = h["group"].get<std::string>();
      REQUIRE(per_group.count(g) == 1);
      ++per_group[g];
    };
    for (const auto& f : doc["fields"]) count(f, f["scope"].get<std::string>() + ":" + f["key"].get<std::string>());
    for (const auto& p : doc["profile_values"]) count(p, "profile value " + p["profile"].get<std::string>() + p["pointer"].get<std::string>());
    std::size_t sum = 0;
    for (const auto& [id, n] : per_group) {
      INFO("group ", id);
      CHECK(n > 0);  // today no group is empty; an empty one must carry empty_reason (checked by problems)
      sum += n;
    }
    CHECK(sum == doc["fields"].size() + doc["profile_values"].size());
    // the levels are a preset subset of the scope chain of graph/scopes.pack (checked by problems against the engine's chain)
    const auto problems = settings_fields_problems(doc);
    INFO(joined(problems));
    CHECK(problems.empty());
  }

  TEST_CASE("problems reports a field without human, an unknown group or subgroup, a level outside the chain, an unresolved profile value") {
    Json doc = unwrap(settings_fields());
    auto has = [](const std::vector<std::string>& v, std::string_view part) {
      for (const auto& s : v) if (s.find(part) != std::string::npos) return true;
      return false;
    };
    Json nohuman = doc;
    nohuman["fields"][0].erase("human");
    CHECK(has(settings_fields_problems(nohuman), "no human: config:base_url"));
    Json badgroup = doc;
    badgroup["fields"][0]["human"]["group"] = "nonsense";
    CHECK(has(settings_fields_problems(badgroup), "unknown group 'nonsense'"));
    Json badsub = doc;
    badsub["fields"][0]["human"]["subgroup"] = "nonsense";
    CHECK(has(settings_fields_problems(badsub), "unknown subgroup 'nonsense'"));
    Json badlevel = doc;
    badlevel["fields"][0]["human"]["levels"] = Json::array({"galaxy"});
    CHECK(has(settings_fields_problems(badlevel), "level outside the scope chain 'galaxy'"));
    Json nolevels = doc;
    nolevels["fields"][0]["human"]["levels"] = Json::array();
    CHECK(has(settings_fields_problems(nolevels), "settable nowhere): config:base_url"));
    Json nolevels_ro = doc;  // a read_only field (rule deny) may be settable nowhere
    for (auto& f : nolevels_ro["fields"]) if (f["key"] == "_config_version") REQUIRE(f["human"]["levels"].empty());
    CHECK_FALSE(has(settings_fields_problems(nolevels_ro), "config:_config_version"));
    Json unresolved = doc;
    REQUIRE_FALSE(unresolved["profile_values"].empty());
    unresolved["profile_values"][0].erase("value");
    unresolved["profile_values"][0]["problem"] = "no value in profile 'worker': pointer /nonsense";
    CHECK(has(settings_fields_problems(unresolved), "profile value worker/batch_threshold: "));
    // a group left without fields must say why
    Json empty = doc;
    empty["groups"]["pusta"] = Json{{"label", Json{{"pl", "Pusta"}, {"en", "Empty"}}}, {"why", Json{{"pl", "x"}, {"en", "x"}}},
                                    {"order", 99}, {"subgroups", Json::array()}};
    CHECK(has(settings_fields_problems(empty), "empty group without empty_reason: pusta"));
    empty["groups"]["pusta"]["empty_reason"] = Json{{"pl", "Pola tej grupy przyjdą z M1-D1."}, {"en", "Its fields come with M1-D1."}};
    CHECK_FALSE(has(settings_fields_problems(empty), "empty group without empty_reason: pusta"));
  }

  TEST_CASE("a profile value is read from its runtime profile, with the user's overlay of that profile, never from config") {
    Json doc = unwrap(settings_fields());
    const Json* threshold = nullptr;
    for (const auto& p : doc["profile_values"])
      if (p["profile"] == "worker" && p["pointer"] == "/batch_threshold") threshold = &p;
    REQUIRE(threshold);
    CHECK((*threshold)["value"] == unwrap(RuntimeProfile::builtin("worker")).values()["batch_threshold"]);
    CHECK((*threshold)["human"]["group"] == "kontekst");
    CHECK_FALSE((*threshold).contains("problem"));
    // the value follows the profile overlay in the data dir (profiles/worker.pack), the chip names the profile
    fsutil::TempDir td;
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(td.path() / "profiles" / "worker.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "worker"}, {"overrides", Json{{"batch_threshold", 7}}}}.dump();
    Json over = unwrap(settings_fields(td.path()));
    for (const auto& p : over["profile_values"])
      if (p["profile"] == "worker" && p["pointer"] == "/batch_threshold") CHECK(p["value"] == 7);
    const auto over_problems = settings_fields_problems(over, td.path());
    INFO(joined(over_problems));
    CHECK(over_problems.empty());
    // a pointer that resolves to nothing is a problem on the entry, reported, never a crash or a silent drop
    std::ofstream(td.path() / "profiles" / "settings_fields.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "settings_fields"},
                {"overrides", Json{{"profile_values", Json::array({Json{{"profile", "worker"}, {"pointer", "/no_such_key"},
                                                                         {"human", doc["profile_values"][0]["human"]}}})}}}}.dump();
    Json broken = unwrap(settings_fields(td.path()));
    REQUIRE(broken["profile_values"].size() == 1);
    CHECK_FALSE(broken["profile_values"][0].contains("value"));
    CHECK(broken["profile_values"][0]["problem"].get<std::string>().find("/no_such_key") != std::string::npos);
    CHECK_FALSE(settings_fields_problems(broken, td.path()).empty());
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
