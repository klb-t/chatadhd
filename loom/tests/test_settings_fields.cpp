#include <doctest/doctest.h>

#include <fstream>
#include <map>
#include <set>

#include "loom/config.h"
#include "loom/effective_view.h"
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
    CHECK(doc["groups"].contains((*threshold)["human"]["group"].get<std::string>()));  // any declared group: grouping is data
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
    // an array index that does not fit (user data) is "no element" on that entry; the other entries and the document stay intact
    std::ofstream(td.path() / "profiles" / "settings_fields.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "settings_fields"},
                {"overrides", Json{{"profile_values", Json::array({
                    Json{{"profile", "worker"}, {"pointer", "/analysis_modes/0"}, {"human", doc["profile_values"][0]["human"]}},
                    Json{{"profile", "worker"}, {"pointer", "/analysis_modes/99999999999999999999999999"}, {"human", doc["profile_values"][0]["human"]}},
                    Json{{"profile", "worker"}, {"pointer", "/analysis_modes/-1"}, {"human", doc["profile_values"][0]["human"]}}})}}}}.dump();
    Json overflow = unwrap(settings_fields(td.path()));
    REQUIRE(overflow["profile_values"].size() == 3);
    CHECK(overflow["profile_values"][0].contains("value"));
    CHECK_FALSE(overflow["profile_values"][0].contains("problem"));
    for (std::size_t i : {std::size_t(1), std::size_t(2)}) {
      CHECK_FALSE(overflow["profile_values"][i].contains("value"));
      CHECK(overflow["profile_values"][i]["problem"].get<std::string>().find("no element") != std::string::npos);
    }
    CHECK(overflow["fields"].size() == doc["fields"].size());
  }

  TEST_CASE("groups are the effective list of the view kind settings_groups: a views overlay renaming or hiding a group is what settings_fields() reports") {
    fsutil::TempDir td;
    // without overlays the two readers agree: the ids of the effective view are the keys of groups
    const auto base = unwrap(views::effective(td.path(), "settings_groups"));
    Json doc = unwrap(settings_fields(td.path()));
    std::set<std::string> ids, keys;
    for (const auto& e : base.entries) ids.insert(e.id);
    for (auto it = doc["groups"].begin(); it != doc["groups"].end(); ++it) keys.insert(it.key());
    REQUIRE_FALSE(ids.empty());
    CHECK(ids == keys);
    for (auto it = doc["groups"].begin(); it != doc["groups"].end(); ++it) CHECK(it.value()["origin"] == "rom");
    // rename (deep modify keeps the other language) and hide through the view overlay of the data dir
    LOOM_REQUIRE_OK(views::put_op(td.path(), "mine", Json{{"view", "settings_groups"}, {"op", "modify"}, {"id", "ty"},
                                                          {"patch", Json{{"label", Json{{"pl", "O mnie"}}}}}}));
    LOOM_REQUIRE_OK(views::put_op(td.path(), "mine", Json{{"view", "settings_groups"}, {"op", "hide"}, {"id", "silnik"}}));
    Json over = unwrap(settings_fields(td.path()));
    CHECK(over["groups"]["ty"]["label"]["pl"] == "O mnie");
    CHECK(over["groups"]["ty"]["label"]["en"] == doc["groups"]["ty"]["label"]["en"]);
    CHECK(over["groups"]["ty"]["origin"] == "rom");  // the entry came from the ROM; the overlay is in its provenance
    REQUIRE(over["groups"]["ty"]["provenance"].is_array());
    CHECK(over["groups"]["ty"]["provenance"].dump().find("overlay:mine") != std::string::npos);
    // a hidden group stays listed with who hid it, so its fields are not orphaned
    CHECK(over["groups"]["silnik"]["hidden"] == true);
    CHECK(over["groups"]["silnik"]["hidden_by"] == "overlay:mine");
    CHECK(over["groups"]["silnik"]["label"] == doc["groups"]["silnik"]["label"]);
    const auto problems = settings_fields_problems(over, td.path());
    INFO(joined(problems));
    CHECK(problems.empty());
    // the view engine says the same
    const auto eff = unwrap(views::effective(td.path(), "settings_groups"));
    CHECK(eff.find("silnik") == nullptr);
    REQUIRE(eff.find("ty"));
    CHECK(eff.find("ty")->value["label"]["pl"] == "O mnie");
    // a bad op is an issue the owner can read, never a dropped group
    std::filesystem::create_directories(td.path() / "views");
    std::ofstream(td.path() / "views" / "zz.json")
        << Json{{"schema", "loom.view_overlay/1"}, {"name", "zz"},
                {"ops", Json::array({Json{{"view", "settings_groups"}, {"op", "modify"}, {"id", "nonsense"}, {"patch", Json{{"order", 1}}}}})}}.dump();
    Json issue = unwrap(settings_fields(td.path()));
    CHECK(issue["groups"].size() == doc["groups"].size());
    REQUIRE(issue.contains("groups_issues"));
    CHECK_FALSE(issue["groups_issues"].empty());
    CHECK_FALSE(settings_fields_problems(issue, td.path()).empty());
  }

  TEST_CASE("an unreadable view overlay (of any view, not only settings_groups) never blanks the settings: the shipped groups are shown and the reason is an issue") {
    fsutil::TempDir td;
    const Json rom = unwrap(settings_fields());
    std::filesystem::create_directories(td.path() / "views");
    for (const char* bytes : {"{not json", R"({"schema":"something/else","ops":[]})"}) {
      INFO(bytes);
      std::ofstream(td.path() / "views" / "broken.json", std::ios::trunc) << bytes;
      auto doc = settings_fields(td.path());
      REQUIRE(doc);
      CHECK((*doc)["fields"].size() == rom["fields"].size());
      REQUIRE((*doc)["groups"].size() == rom["groups"].size());
      for (auto it = rom["groups"].begin(); it != rom["groups"].end(); ++it) {
        CHECK((*doc)["groups"][it.key()]["label"] == it.value()["label"]);
        CHECK((*doc)["groups"][it.key()]["origin"] == "rom");
      }
      REQUIRE(doc->contains("groups_issues"));
      CHECK((*doc)["groups_issues"].dump().find("broken.json") != std::string::npos);
      bool reported = false;
      for (const auto& p : settings_fields_problems(*doc, td.path()))
        reported = reported || (p.find("groups overlay issue") != std::string::npos && p.find("broken.json") != std::string::npos);
      CHECK(reported);
      // the write path that reads the same document keeps working
      LOOM_REQUIRE_OK(check_config_write("theme", td.path()));
      CHECK_FALSE(check_config_write("_config_version", td.path()));
    }
  }

  TEST_CASE("a profile overlay may add a group of its own and move a field into it (no enum closes the groups)") {
    fsutil::TempDir td;
    std::filesystem::create_directories(td.path() / "profiles");
    const RuntimeProfile pack_profile = unwrap(RuntimeProfile::builtin("settings_fields"));
    Json fields = pack_profile.values()["fields"];
    for (auto& f : fields)
      if (f["key"] == "theme") {
        f["human"]["group"] = "moje";
        f["human"].erase("subgroup");  // the subgroup belonged to the old group
      }
    std::ofstream(td.path() / "profiles" / "settings_fields.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "settings_fields"},
                {"overrides", Json{{"groups", Json{{"moje", Json{{"label", Json{{"pl", "Moje"}, {"en", "Mine"}}},
                                                                    {"why", Json{{"pl", "Pola, które wybrałem."}, {"en", "The fields I chose."}}},
                                                                    {"order", 0}, {"subgroups", Json::array()}}}}},
                                   {"fields", fields}}}}.dump();
    Json doc = unwrap(settings_fields(td.path()));
    REQUIRE(doc["groups"].contains("moje"));
    // the view engine reads the same overlaid profile: loom_view_effective("settings_groups") sees the group too
    const auto eff = unwrap(views::effective(td.path(), "settings_groups"));
    REQUIRE(eff.find("moje"));
    CHECK(eff.find("moje")->value["label"]["pl"] == "Moje");
    bool moved = false;
    for (const auto& f : doc["fields"]) if (f["key"] == "theme") moved = f["human"]["group"] == "moje";
    CHECK(moved);
    const auto problems = settings_fields_problems(doc, td.path());
    INFO(joined(problems));
    CHECK(problems.empty());
  }

  TEST_CASE("levels are promises with evidence in data: project/conversation need a chain-resolved reader, turn a per-request option; a role needs ports.pack or a status; a status needs its reason") {
    Json doc = unwrap(settings_fields());
    auto has = [](const std::vector<std::string>& v, std::string_view part) {
      for (const auto& s : v) if (s.find(part) != std::string::npos) return true;
      return false;
    };
    REQUIRE(doc["level_evidence"]["chain"].is_array());
    REQUIRE(doc["level_evidence"]["turn"].is_array());
    // the shipped data: every declared level has its evidence (the problems list is empty), and the evidence names real keys
    std::set<std::string> keys;
    for (const auto& f : doc["fields"]) keys.insert(f["key"].get<std::string>());
    for (const char* list : {"chain", "turn"})
      for (const auto& k : doc["level_evidence"][list]) {
        INFO("level_evidence.", list, " names ", k.get<std::string>());
        CHECK(keys.count(k.get<std::string>()) == 1);
      }
    // a claim without evidence is a problem naming the level and the field
    Json claim = doc;
    for (auto& f : claim["fields"]) if (f["key"] == "theme") f["human"]["levels"] = Json::array({"installation", "person", "project", "conversation", "turn"});
    const auto problems = settings_fields_problems(claim);
    CHECK(has(problems, "level 'project' without a chain-resolved reader (level_evidence.chain): config:theme"));
    CHECK(has(problems, "level 'conversation' without a chain-resolved reader (level_evidence.chain): config:theme"));
    CHECK(has(problems, "level 'turn' without a per-request option (level_evidence.turn): config:theme"));
    // the evidence itself is data: adding the key to it makes the same claim consistent
    claim["level_evidence"]["chain"].push_back("theme");
    claim["level_evidence"]["turn"].push_back("theme");
    CHECK_FALSE(has(settings_fields_problems(claim), "config:theme"));
    // a role chip never dangles: unknown to ports.pack and without role_status is a problem
    Json role = doc;
    role["fields"][0]["human"]["role"] = "no.such.role";
    CHECK(has(settings_fields_problems(role), "role 'no.such.role' unknown to ports.pack and without role_status: config:base_url"));
    role["fields"][0]["human"]["role_status"] = "planned";
    CHECK_FALSE(has(settings_fields_problems(role), "role 'no.such.role'"));
    // the three roles of M1-D1 are declared planned today (ports.pack does not know them)
    for (const auto& f : doc["fields"])
      if (f["human"].contains("role")) {
        const std::string r = f["human"]["role"].get<std::string>();
        if (r == "intent.routing" || r == "image.generate" || r == "speech.synthesize") CHECK(f["human"]["role_status"] == "planned");
      }
    // a field without a reader says so with a reason (never present tense alone); a status without reason is a problem
    std::size_t planned = 0;
    for (const auto& f : doc["fields"])
      if (f.contains("status")) {
        INFO(f["key"].get<std::string>());
        CHECK(f["status"]["state"] == "planned");
        CHECK_FALSE(f["status"]["reason"]["pl"].get<std::string>().empty());
        CHECK_FALSE(f["status"]["by"].get<std::string>().empty());
        ++planned;
      }
    CHECK(planned >= 12);
    Json nostatus = doc;
    nostatus["fields"][0]["status"] = Json{{"state", "planned"}};
    CHECK(has(settings_fields_problems(nostatus), "status without reason pl+en: config:base_url"));
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
