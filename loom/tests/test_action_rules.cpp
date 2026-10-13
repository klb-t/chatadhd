// Wave 2 (GRAF_HUB_KOD_SILNIK §5-§7, §9): which actions apply to which element.
// Traits on classes, requirements and flags on actions, scoped rules, extrapolation proposals.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_rules.h"
#include "loom/action_table.h"
#include "loom/loom.h"
#include "loom/runtime_profile.h"
#include "loom/settings_fields.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::actions::Engine;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

// A user overlay of one runtime-profile domain (<data_dir>/profiles/<domain>.pack).
void overlay(const fs::path& dir, const std::string& domain, const Json& overrides) {
  fs::create_directories(dir / "profiles");
  std::ofstream(dir / "profiles" / (domain + ".pack"))
      << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
}

Json rule(const std::string& id, const std::string& scope, const Json& target, const Json& action, const std::string& effect,
          const Json& extra = Json::object()) {
  Json r{{"id", id}, {"scope", scope}, {"target", target}, {"effect", effect}};
  if (!action.is_null()) r["action"] = action;
  for (auto it = extra.begin(); it != extra.end(); ++it) r[it.key()] = it.value();
  return r;
}

Json rules_overlay(std::initializer_list<Json> rules) {
  Json o = Json::object();
  for (const auto& r : rules) o[r.at("id").get<std::string>()] = r;
  return Json{{"rules", o}};
}

Json cls(const std::string& id) { return Json{{"kind", "class"}, {"id", id}}; }
Json of_kind(const std::string& kind, const std::string& id) { return Json{{"kind", kind}, {"id", id}}; }

// The row of one action in an actions_for result (null when absent).
Json row(const Json& result, const std::string& action) {
  for (const auto& r : result.at("actions"))
    if (r.at("action") == action) return r;
  return Json();
}

std::string status_of(const Engine& e, const Json& element, const std::string& action, const Json& scope = Json()) {
  const Json result = unwrap(e.actions_for(element, scope));
  const Json r = row(result, action);
  return r.is_null() ? std::string("absent") : r.at("status").get<std::string>();
}

Json minimal_action(const std::string& id, const Json& requires_, const Json& produces = Json::array(),
                    const Json& effects_over = Json::object()) {
  Json effects{{"reads_only", true}, {"writes", false}, {"sends_off_device", false}, {"costs_money", false},
               {"irreversible", false}, {"touches_private_data", false}};
  for (auto it = effects_over.begin(); it != effects_over.end(); ++it) effects[it.key()] = it.value();
  return Json{{"id", id}, {"kind", "atomic"}, {"status", "planned"}, {"title", id}, {"description", id},
              {"inputs", Json::array({"text"})}, {"outputs", Json::array({"text"})}, {"requires", requires_},
              {"produces", produces}, {"content_op", "none"}, {"form_op", "none"}, {"cost", 1}, {"confidence", 0.9},
              {"effects", effects}, {"consumer", ""}};
}

std::vector<std::string> step_ids(const Json& chain) {
  std::vector<std::string> ids;
  for (const auto& s : chain.at("steps")) ids.push_back(s.at("action").get<std::string>());
  return ids;
}

const Json kAudio = Json{{"class", "audio_form"}};
const Json kMessage = Json{{"class", "message"}};

}  // namespace

TEST_SUITE("action_rules") {
  TEST_CASE("settings_fields output is byte-identical to the pinned golden (field edit rules are a special case of the rules)") {
    const auto path = fs::path(LOOM_TEST_FIXTURES) / "golden" / "settings_fields.json";
    auto golden = fsutil::read_file(path);
    REQUIRE(golden);
    CHECK(unwrap(settings_fields()).dump() == *golden);
  }

  TEST_CASE("the shipped data is consistent: known traits, scopes, effects, groups; every deny states a reason") {
    Engine e = unwrap(Engine::load());
    const Json issues = e.issues();
    INFO(issues.dump(1));
    CHECK(issues.empty());
  }

  // ── 1. traits and the content/form split ──────────────────────────────

  TEST_CASE("traits are inherited along the class chain; a class id is a trait of its own; a node adds its own") {
    Engine e = unwrap(Engine::load());
    auto traits_of = [&](const Json& element) {
      const Json result = unwrap(e.actions_for(element));
      std::set<std::string> out;
      for (const auto& t : result["element"]["traits"]) out.insert(t.get<std::string>());
      return out;
    };
    auto message = traits_of(kMessage);
    for (const char* t : {"message", "text_form", "form", "has_text", "expresses_content", "in_conversation", "editable", "versioned"})
      CHECK_MESSAGE(message.count(t) == 1, "message lacks ", t);
    auto music = traits_of(Json{{"class", "music_form"}});
    for (const char* t : {"musical", "has_audio", "audio_form", "form"}) CHECK_MESSAGE(music.count(t) == 1, "music lacks ", t);
    CHECK(music.count("has_text") == 0);
    // where a trait came from is part of the explanation
    const Json summarise = row(unwrap(e.actions_for(kMessage)), "summarise");
    REQUIRE_FALSE(summarise.is_null());
    CHECK(summarise["explanation"]["matched"][0] == Json{{"trait", "has_text"}, {"from", "class:text_form"}});
    // a node adds a trait of its own to a class that lacks it
    CHECK(status_of(e, Json{{"class", "person"}}, "summarise") == "absent");
    const Json with_text = row(unwrap(e.actions_for(Json{{"class", "person"}, {"traits", Json::array({"has_text"})}})), "summarise");
    REQUIRE_FALSE(with_text.is_null());
    CHECK(with_text["explanation"]["matched"][0]["from"] == "node");
    // an unknown class is answered from its own traits and reported, not refused
    const Json odd = unwrap(e.actions_for(Json{{"class", "nonsense"}}));
    CHECK_FALSE(row(odd, "use_model").is_null());
    CHECK(odd["issues"][0]["code"] == "class_unknown");
    // a bare string is a class id
    CHECK(status_of(e, Json("message"), "summarise") == "applicable");
  }

  TEST_CASE("content and form are separate classes related by expresses, whose weight is fidelity") {
    auto profile = unwrap(RuntimeProfile::builtin("graph_entry"));
    const Json& classes = profile.values()["ontology"]["classes"];
    REQUIRE(classes.contains("content"));
    REQUIRE(classes.contains("form"));
    CHECK_FALSE(classes["content"].contains("parent"));
    CHECK_FALSE(classes["form"].contains("parent"));
    auto reaches_form = [&](std::string c) {
      for (int guard = 0; guard < 10 && !c.empty(); ++guard) {
        if (c == "form") return true;
        c = classes[c].value("parent", "");
      }
      return false;
    };
    for (const char* modality : {"text_form", "image_form", "audio_form", "video_form", "music_form", "motion_form", "code_form", "table_form"}) {
      INFO(modality);
      REQUIRE(classes.contains(modality));
      CHECK(reaches_form(modality));
    }
    const Json& expresses = profile.values()["ontology"]["relations"]["expresses"];
    CHECK(expresses["from"] == "form");
    CHECK(expresses["to"] == "content");
    CHECK(expresses["weight"]["name"] == "fidelity");
    CHECK(expresses["weight"]["range"] == Json::array({0, 1}));
    // a content carries a canonical text, so text actions fit it; a form of another modality needs a reading first
    Engine e = unwrap(Engine::load());
    CHECK(status_of(e, Json{{"class", "content"}}, "summarise") == "applicable");
    CHECK(status_of(e, kAudio, "summarise") == "absent");
  }

  // ── 2. requirements: a new action needs no change to a class ───────────

  TEST_CASE("a new action fits every existing class that meets its requirements, without editing a class") {
    fsutil::TempDir td;
    overlay(td.path(), "actions", Json{{"actions", Json{{"shout", minimal_action("shout", Json::array({"has_text"}))},
                                                       {"listen", minimal_action("listen", Json::array({"has_audio"}))}}}});
    Engine before = unwrap(Engine::load());
    Engine after = unwrap(Engine::load(td.path()));
    for (const char* c : {"message", "document", "conversation", "code_form", "observation", "content", "text_form"}) {
      INFO(c);
      CHECK(status_of(before, Json{{"class", c}}, "shout") == "absent");
      CHECK(status_of(after, Json{{"class", c}}, "shout") == "applicable");
    }
    for (const char* c : {"audio_form", "person", "storage", "image_form"}) {
      INFO(c);
      CHECK(status_of(after, Json{{"class", c}}, "shout") == "absent");
    }
    // a subclass inherits the fit: music is audio
    CHECK(status_of(after, kAudio, "listen") == "applicable");
    CHECK(status_of(after, Json{{"class", "music_form"}}, "listen") == "applicable");
    CHECK(status_of(after, kMessage, "listen") == "absent");
    // the classes themselves are untouched: same traits for the same element
    CHECK(unwrap(before.actions_for(kMessage))["element"] == unwrap(after.actions_for(kMessage))["element"]);
    CHECK_FALSE(fs::exists(td.path() / "profiles" / "graph_entry.pack"));
    CHECK(after.issues().empty());
  }

  TEST_CASE("the two axes and the declared numbers are in every action; options list the unmatched and filter planned ones") {
    Engine e = unwrap(Engine::load());
    for (const auto& [id, a] : e.catalogue().items()) {
      INFO(id);
      CHECK(a.contains("requires"));
      CHECK(a.contains("produces"));
      CHECK(a.contains("content_op"));
      CHECK(a.contains("form_op"));
      CHECK(a.contains("cost"));
      CHECK(a.contains("confidence"));
    }
    const Json read = row(unwrap(e.actions_for(kMessage)), "summarise");
    CHECK(read["content_op"] == "condense");
    CHECK(read["groups"].dump().find("condensing") != std::string::npos);
    const Json unmatched = unwrap(e.actions_for(kAudio, Json(), Json{{"include_unmatched", true}}));
    const Json missing = row(unmatched, "summarise");
    CHECK(missing["status"] == "not_applicable");
    CHECK(missing["missing"] == Json::array({"has_text"}));
    const Json only_implemented = unwrap(e.actions_for(kMessage, Json(), Json{{"implemented_only", true}}));
    CHECK_FALSE(row(only_implemented, "semantic.analyse").is_null());
    CHECK_FALSE(row(only_implemented, "summarise").is_null());  // implemented: its use_model step is data the interpreter runs
    CHECK_FALSE(row(only_implemented, "elaborate").is_null());  // implemented (P11): its use_model step is data too
    for (const auto& [id, a] : e.catalogue().items())            // whatever is still only declared is not listed
      if (a.value("status", "") == "planned") CHECK(row(only_implemented, id).is_null());
    CHECK_FALSE(row(only_implemented, "depict").is_null());     // implemented: two use_model steps, data
  }

  // ── 3. rules ───────────────────────────────────────────────────────

  TEST_CASE("rules: the most specific scope wins; rules of other instances are inactive and the explanation says so") {
    fsutil::TempDir td;
    const Json msg = cls("message");
    overlay(td.path(), "action_rules",
            rules_overlay({rule("inst", "installation", msg, "summarise", "confirm"),
                           rule("mine", "person", msg, "summarise", "allow", Json{{"scope_id", "me"}}),
                           rule("c1", "conversation", msg, "summarise", "deny",
                                Json{{"scope_id", "c_1"}, {"initiator", "owner"}, {"reason", Json{{"en", "not in this conversation"}}}})}));
    Engine e = unwrap(Engine::load(td.path()));
    // installation level: only the installation rule is in reach
    CHECK(status_of(e, kMessage, "summarise", "installation") == "needs_confirmation");
    // the person's own rule is more specific
    CHECK(status_of(e, kMessage, "summarise", Json{{"person", "me"}}) == "applicable");
    // the conversation rule is the most specific, but only for conversation c_1
    CHECK(status_of(e, kMessage, "summarise", Json{{"person", "me"}, {"conversation", "c_1"}}) == "denied");
    CHECK(status_of(e, kMessage, "summarise", Json{{"person", "me"}, {"conversation", "c_2"}}) == "applicable");
    // another person: the person rule is inactive, the installation rule remains
    CHECK(status_of(e, kMessage, "summarise", Json{{"person", "you"}}) == "needs_confirmation");
    // a conversation without a named person: c1 applies, mine is inactive
    CHECK(status_of(e, kMessage, "summarise", Json{{"conversation", "c_1"}}) == "denied");
    // a level name alone names no instance, so a rule bound to an instance stays inactive
    CHECK(status_of(e, kMessage, "summarise", "conversation") == "needs_confirmation");

    const Json why = unwrap(e.explain(kMessage, "summarise", Json{{"person", "me"}, {"conversation", "c_2"}}));
    CHECK(why["status"] == "applicable");
    CHECK(why["decided_by"]["rule"] == "mine");
    CHECK(why["decided_by"]["conflict_rule"] == "most_specific_wins");
    std::map<std::string, std::string> roles;
    for (const auto& r : why["rules"]) roles[r["id"].get<std::string>()] = r["role"].get<std::string>();
    CHECK(roles["mine"] == "decided");
    CHECK(roles["inst"] == "overridden");
    CHECK(roles["c1"] == "inactive");
  }

  TEST_CASE("a denial carries the stated reason and its initiator; a deny without a reason is reported") {
    fsutil::TempDir td;
    overlay(td.path(), "action_rules",
            rules_overlay({rule("never", "person", cls("message"), "share", "deny",
                                Json{{"scope_id", "me"}, {"initiator", "me"}, {"reason", Json{{"en", "private"}, {"pl", "prywatne"}}}})}));
    Engine e = unwrap(Engine::load(td.path()));
    const Json mine = Json{{"person", "me"}};
    const Json r = row(unwrap(e.actions_for(kMessage, mine)), "share");
    CHECK(r["status"] == "denied");
    CHECK(r["reason"] == Json{{"en", "private"}, {"pl", "prywatne"}});
    CHECK(r["initiator"] == "me");
    CHECK(r["explanation"]["decided_by"]["rule"] == "never");
    const Json why = unwrap(e.explain(kMessage, "share", mine));
    CHECK(why["reason"]["pl"] == "prywatne");
    // outside the person's scope nothing denies it (no initiator, no block)
    CHECK(status_of(e, kMessage, "share", Json{{"person", "you"}}) == "applicable");
    CHECK(e.issues().empty());

    fsutil::TempDir bare;
    overlay(bare.path(), "action_rules", rules_overlay({rule("silent", "installation", cls("message"), "share", "deny")}));
    Engine b = unwrap(Engine::load(bare.path()));
    CHECK(status_of(b, kMessage, "share") == "denied");  // enforced, but the data is flagged
    const Json problems = b.issues();
    REQUIRE(problems.size() == 1);
    CHECK(problems[0]["code"] == "deny_without_reason");
  }

  TEST_CASE("a default action per class: shipped presets, a class beats its ancestor, a closer scope beats a farther one") {
    Engine e = unwrap(Engine::load());
    const Json conv = unwrap(e.actions_for(Json{{"class", "conversation"}}));
    CHECK(conv["default_action"] == "chat.send");
    CHECK(conv["actions"][0]["action"] == "chat.send");
    CHECK(conv["actions"][0]["status"] == "default");
    CHECK(conv["actions"][0]["default"] == true);
    CHECK(unwrap(e.actions_for(kAudio))["default_action"] == "transcribe");
    CHECK(unwrap(e.actions_for(Json{{"class", "image_form"}}))["default_action"] == "describe_image");
    CHECK(unwrap(e.actions_for(kMessage))["default_action"].is_null());
    CHECK(status_of(e, kAudio, "transcribe") == "default");

    fsutil::TempDir td;
    overlay(td.path(), "action_rules",
            rules_overlay({rule("d_text", "installation", cls("text_form"), "summarise", "default_action"),
                           rule("d_msg", "installation", cls("message"), "translate", "default_action")}));
    Engine f = unwrap(Engine::load(td.path()));
    CHECK(unwrap(f.actions_for(kMessage))["default_action"] == "translate");                      // the class beats its ancestor
    CHECK(unwrap(f.actions_for(Json{{"class", "document"}}))["default_action"] == "summarise");   // a document is a text form
    CHECK(unwrap(f.actions_for(Json{{"class", "conversation"}}))["default_action"] == "summarise");  // installation beats app_preset
    CHECK(unwrap(f.actions_for(Json{{"class", "conversation"}}, "app_preset"))["default_action"] == "chat.send");

    // a default that is denied is still named, with the denial; a default that does not fit is reported
    fsutil::TempDir g;
    overlay(g.path(), "action_rules",
            rules_overlay({rule("d", "installation", cls("message"), "summarise", "default_action"),
                           rule("x", "installation", cls("message"), "summarise", "deny", Json{{"reason", "no"}}),
                           rule("d_audio", "installation", cls("message"), "transcribe", "default_action", Json{{"scope", "turn"}})}));
    Engine h = unwrap(Engine::load(g.path()));
    const Json denied = row(unwrap(h.actions_for(kMessage, "installation")), "summarise");
    CHECK(denied["status"] == "denied");
    CHECK(denied["default"] == true);
    const Json misfit = unwrap(h.actions_for(kMessage));  // turn level: d_audio wins, transcribe needs audio
    CHECK(misfit["default_action"].is_null());
    CHECK(misfit["issues"][0]["code"] == "default_action_not_fitting");
  }

  TEST_CASE("rules over classes, traits, bundles, nodes, effect flags and action groups (the examples of section 5)") {
    fsutil::TempDir td;
    overlay(td.path(), "graph_entry",
            Json{{"ontology", Json{{"classes", Json{{"diary", Json{{"label", Json{{"en", "Diary"}, {"pl", "Dziennik"}}}, {"parent", "document"}}},
                                                    {"evidence", Json{{"label", Json{{"en", "Evidence"}, {"pl", "Dowód"}}}, {"parent", "document"}}}}}}}});
    overlay(td.path(), "actions",
            Json{{"actions", Json{{"delete", minimal_action("delete", Json::array({"editable"}), Json::array(),
                                                            Json{{"writes", true}, {"irreversible", true}, {"reads_only", false}})}}}});
    overlay(td.path(), "action_rules",
            rules_overlay({
                // "actions that send off the device, for the class diary: ask"
                rule("diary_off_device", "installation", cls("diary"), Json{{"flag", "sends_off_device"}}, "confirm"),
                // "never delete, for the class evidence"
                rule("evidence_keep", "installation", cls("evidence"), "delete", "deny", Json{{"reason", Json{{"en", "evidence is never deleted"}}}, {"initiator", "owner"}}),
                // a trait bundle and an action group
                rule("rich_spend", "installation", of_kind("bundle", "rich_content"), Json{{"group", "spends_money"}}, "confirm"),
                // a trait
                rule("audio_private", "installation", of_kind("trait", "has_audio"), Json{{"group", "private_data"}}, "deny", Json{{"reason", "voice is private"}}),
                // one node, every action
                rule("secret_node", "installation", of_kind("node", "m_secret"), "*", "deny", Json{{"reason", "sealed"}}),
                // any element, an action group given as the target
                rule("irreversible_ask", "installation", of_kind("action_group", "irreversible"), Json(), "confirm"),
            }));
    Engine e = unwrap(Engine::load(td.path()));
    INFO(e.issues().dump());
    CHECK(e.issues().empty());
    const Json diary = Json{{"class", "diary"}};
    CHECK(status_of(e, diary, "use_model") == "needs_confirmation");  // sends off device
    CHECK(status_of(e, Json{{"class", "document"}}, "use_model") == "needs_confirmation");  // rich bundle + spends money
    CHECK(status_of(e, diary, "save") == "applicable");
    CHECK(status_of(e, diary, "delete") == "needs_confirmation");  // irreversible, any element
    CHECK(status_of(e, Json{{"class", "evidence"}}, "delete") == "denied");
    const Json why = unwrap(e.explain(Json{{"class", "evidence"}}, "delete"));
    CHECK(why["reason"]["en"] == "evidence is never deleted");
    CHECK(why["initiator"] == "owner");
    CHECK(status_of(e, Json{{"class", "evidence"}}, "summarise") == "needs_confirmation");  // spends money, rich content
    // a class outside the bundle is not touched by the bundle rule
    CHECK(status_of(e, Json{{"class", "storage"}}, "use_model") == "applicable");
    // a trait: audio, private-data actions
    CHECK(status_of(e, kAudio, "share") == "denied");
    CHECK(status_of(e, kAudio, "save") == "applicable");
    // one node
    CHECK(status_of(e, Json{{"class", "message"}, {"id", "m_secret"}}, "summarise") == "denied");
    CHECK(status_of(e, Json{{"class", "message"}, {"id", "m_other"}}, "save") == "applicable");
    CHECK(status_of(e, Json{{"class", "message"}, {"id", "m_secret"}}, "save") == "denied");
  }

  TEST_CASE("settings field edit rules are rules of the same store; a locked engine rule holds against a lower scope") {
    Engine e = unwrap(Engine::load());
    actions::Element field;
    field.cls = "settings_field";
    field.attrs = Json{{"field_scope", "config"}, {"key", "_config_version"}};
    actions::Scope installation = unwrap(e.parse_scope("installation"));
    auto d = unwrap(e.decide(field, "edit", installation));
    CHECK(d.effect == "deny");
    CHECK(d.decision == "deny");
    CHECK(d.rule == "edit:config:_config_version");
    CHECK(d.initiator == "engine");
    CHECK(d.reason["en"].is_string());
    field.attrs["key"] = "theme";
    CHECK(unwrap(e.decide(field, "edit", installation)).effect == "allow");  // no rule = allow (R43)

    fsutil::TempDir td;
    const Json usage = Json{{"kind", "settings_field"}, {"field_scope", "config"}, {"key", "loom_usage_policy"}};
    const Json version = Json{{"kind", "settings_field"}, {"field_scope", "config"}, {"key", "_config_version"}};
    overlay(td.path(), "action_rules",
            rules_overlay({rule("lift_usage", "person", usage, "edit", "allow", Json{{"scope_id", "me"}}),
                           rule("lift_version", "installation", version, "edit", "allow"),
                           rule("project_confirm", "project", of_kind("trait", "editable"), "edit", "confirm", Json{{"scope_id", "p1"}})}));
    Engine f = unwrap(Engine::load(td.path()));
    field.attrs = usage;
    field.attrs.erase("kind");
    const auto me = unwrap(f.parse_scope(Json{{"person", "me"}}));
    auto pinned = unwrap(f.decide(field, "edit", me));
    CHECK(pinned.effect == "require_confirmation");  // pinned by the engine: locked_by
    CHECK(pinned.conflict_rule == "locked_by");
    field.attrs = version;
    field.attrs.erase("kind");
    CHECK(unwrap(f.decide(field, "edit", installation)).effect == "allow");  // not locked: a more specific scope lifts it
    // the API describes the installation: a project's rule does not show there, but a question asked inside the project sees it
    field.attrs = Json{{"field_scope", "config"}, {"key", "theme"}};
    CHECK(unwrap(f.decide(field, "edit", installation)).effect == "allow");
    CHECK(unwrap(f.decide(field, "edit", unwrap(f.parse_scope(Json{{"project", "p1"}})))).effect == "confirm");  // settings_field is editable
    CHECK(unwrap(f.decide(field, "edit", unwrap(f.parse_scope(Json{{"project", "p2"}})))).effect == "allow");
    const Json fields = unwrap(settings_fields(td.path()));
    for (const auto& item : fields["fields"]) {
      if (item["key"] == "_config_version") CHECK(item["edit"] == "optional");
      if (item["key"] == "loom_usage_policy") CHECK(item["edit"] == "requires_confirmation");
    }
  }

  // ── 3b. conflict resolution per the rules of scopes.pack ───────────────

  TEST_CASE("every conflict rule of scopes.pack resolves competing rules") {
    const Json msg = cls("message");
    // Two or three rules for (message, summarise) at the installation (general) and person (specific) level.
    auto outcome = [&](std::initializer_list<Json> rules, const Json& scope = Json{{"person", "me"}}) {
      fsutil::TempDir td;
      overlay(td.path(), "action_rules", rules_overlay(rules));
      Engine e = unwrap(Engine::load(td.path()));
      return std::make_pair(unwrap(e.explain(kMessage, "summarise", scope)), e.issues());
    };
    const Json me = Json{{"scope_id", "me"}};
    auto with = [&](const Json& base, const Json& extra) {
      Json r = base;
      for (auto it = extra.begin(); it != extra.end(); ++it) r[it.key()] = it.value();
      return r;
    };
    {  // most_specific_wins
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "deny", Json{{"conflict", "most_specific_wins"}, {"reason", "g"}}),
                                  rule("s", "person", msg, "summarise", "allow", me)});
      CHECK(r["status"] == "applicable");
      CHECK(r["decided_by"]["rule"] == "s");
    }
    {  // the chain default is most_specific_wins too (nobody names a rule)
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "deny", Json{{"reason", "g"}}),
                                  rule("s", "person", msg, "summarise", "allow", me)});
      CHECK(r["decided_by"]["conflict_rule"] == "most_specific_wins");
    }
    {  // locked_by: the higher scope pinned it
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "deny", Json{{"conflict", "locked_by"}, {"locked", true}, {"reason", "g"}}),
                                  rule("s", "person", msg, "summarise", "allow", me)});
      CHECK(r["status"] == "denied");
      CHECK(r["decided_by"]["rule"] == "g");
      // not pinned: falls back to the most specific
      auto [r2, issues2] = outcome({rule("g", "installation", msg, "summarise", "deny", Json{{"conflict", "locked_by"}, {"reason", "g"}}),
                                    rule("s", "person", msg, "summarise", "allow", me)});
      CHECK(r2["status"] == "applicable");
    }
    {  // first_defined: the most general definition
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "confirm", Json{{"conflict", "first_defined"}}),
                                  rule("s", "person", msg, "summarise", "allow", me)});
      CHECK(r["status"] == "needs_confirmation");
      CHECK(r["decided_by"]["rule"] == "g");
    }
    for (const char* strict : {"intersection", "min"}) {  // the strictest outcome
      INFO(strict);
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "allow", Json{{"conflict", strict}}),
                                  rule("s", "person", msg, "summarise", "deny", with(me, Json{{"reason", "s"}})),
                                  rule("t", "team", msg, "summarise", "confirm")});
      CHECK(r["status"] == "denied");
    }
    {  // max: the most permissive outcome
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "deny", Json{{"conflict", "max"}, {"reason", "g"}}),
                                  rule("s", "person", msg, "summarise", "confirm", me)});
      CHECK(r["status"] == "needs_confirmation");
    }
    {  // merge: the most specific decision, the reasons of all scopes combined
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "deny", Json{{"conflict", "merge"}, {"reason", Json{{"en", "a"}}}}),
                                  rule("s", "person", msg, "summarise", "confirm", with(me, Json{{"reason", Json{{"pl", "b"}}}}))});
      CHECK(r["status"] == "needs_confirmation");
      CHECK(r["reason"] == Json{{"en", "a"}, {"pl", "b"}});
    }
    {  // weighted_vote
      auto [r, issues] = outcome({rule("g1", "installation", msg, "summarise", "allow", Json{{"conflict", "weighted_vote"}}),
                                  rule("g2", "organization", msg, "summarise", "allow"),
                                  rule("s", "person", msg, "summarise", "deny", with(me, Json{{"weight", 3}, {"reason", "s"}}))});
      CHECK(r["status"] == "denied");
      auto [r2, issues2] = outcome({rule("g1", "installation", msg, "summarise", "allow", Json{{"conflict", "weighted_vote"}}),
                                    rule("g2", "organization", msg, "summarise", "allow"),
                                    rule("s", "person", msg, "summarise", "deny", with(me, Json{{"reason", "s"}}))});
      CHECK(r2["status"] == "applicable");
    }
    {  // ask: the scopes disagree, so the user decides
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "deny", Json{{"conflict", "ask"}, {"reason", "g"}}),
                                  rule("s", "person", msg, "summarise", "allow", me)});
      CHECK(r["status"] == "needs_confirmation");
      CHECK(r["decided_by"]["basis"] == "conflict");
      CHECK(r["decided_by"]["rules"].size() == 2);
      auto [r2, issues2] = outcome({rule("g", "installation", msg, "summarise", "allow", Json{{"conflict", "ask"}}),
                                    rule("s", "person", msg, "summarise", "allow", me)});
      CHECK(r2["status"] == "applicable");  // they agree: nothing to ask
    }
    {  // by_context: a rule bound to the context outranks one that is not
      const Json project = Json{{"person", "me"}, {"project", "p1"}};
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "deny",
                                       Json{{"conflict", "by_context"}, {"when", Json{{"context", Json{{"project", "p1"}}}}}, {"reason", "g"}}),
                                  rule("s", "person", msg, "summarise", "allow", me)},
                                 project);
      CHECK(r["status"] == "denied");
      auto [r2, issues2] = outcome({rule("g", "installation", msg, "summarise", "deny",
                                         Json{{"conflict", "by_context"}, {"when", Json{{"context", Json{{"project", "p1"}}}}}, {"reason", "g"}}),
                                    rule("s", "person", msg, "summarise", "allow", me)},
                                   Json{{"person", "me"}, {"project", "p2"}});
      CHECK(r2["status"] == "applicable");  // the context does not hold: the bound rule is inactive
    }
    {  // a conflict rule that scopes.pack does not know is reported and the default applies
      auto [r, issues] = outcome({rule("g", "installation", msg, "summarise", "deny", Json{{"conflict", "coin_toss"}, {"reason", "g"}}),
                                  rule("s", "person", msg, "summarise", "allow", me)});
      CHECK(r["status"] == "applicable");
      CHECK(issues[0]["code"] == "conflict_rule_unknown");
    }
  }

  TEST_CASE("scopes: levels come from the chain; global is an alias of installation; a rule outside the chain is reported and inert") {
    Engine e = unwrap(Engine::load());
    CHECK_FALSE(e.parse_scope("nowhere"));
    CHECK_FALSE(e.parse_scope(Json{{"nowhere", "x"}}));
    CHECK_FALSE(e.parse_scope(Json{{"person", 3}}));
    CHECK(unwrap(e.parse_scope("global")).level == "installation");
    CHECK(unwrap(e.parse_scope(Json())).level == "turn");  // the most specific level
    CHECK(unwrap(e.parse_scope(Json{{"conversation", "c"}, {"person", "p"}})).level == "conversation");

    fsutil::TempDir td;
    overlay(td.path(), "action_rules",
            rules_overlay({rule("alias", "global", cls("message"), "share", "deny", Json{{"reason", "r"}}),
                           rule("lost", "galaxy", cls("message"), "save", "deny", Json{{"reason", "r"}})}));
    Engine f = unwrap(Engine::load(td.path()));
    CHECK(status_of(f, kMessage, "share", "installation") == "denied");
    CHECK(status_of(f, kMessage, "share", "app_preset") == "applicable");  // above the installation level
    CHECK(status_of(f, kMessage, "save") == "applicable");
    const Json problems = f.issues();
    REQUIRE(problems.size() == 1);
    CHECK(problems[0]["code"] == "rule_scope_unknown");
  }

  // ── 4. extrapolation: chains of actions that supply a missing trait ────

  TEST_CASE("summarise an audio recording: read the recording into text, then condense it") {
    Engine e = unwrap(Engine::load());
    CHECK(status_of(e, kAudio, "summarise") == "absent");
    const Json why = unwrap(e.explain(kAudio, "summarise"));
    CHECK(why["fits"] == false);
    CHECK(why["status"] == "not_applicable");
    CHECK(why["missing"] == Json::array({"has_text"}));
    REQUIRE(why.contains("extrapolation"));  // explain offers the way there

    const Json proposal = unwrap(e.propose_chain(kAudio, Json{{"action", "summarise"}}));
    CHECK(proposal["proposal_only"] == true);
    REQUIRE(proposal["chains"].size() == 1);
    const Json chain = proposal["chains"][0];
    CHECK(step_ids(chain) == std::vector<std::string>{"transcribe", "summarise"});
    CHECK(chain["steps"][0]["form_op"] == "read");      // audio -> text content
    CHECK(chain["steps"][0]["content_op"] == "preserve");
    CHECK(chain["steps"][1]["content_op"] == "condense");
    CHECK(chain["length"] == 2);
    CHECK(chain["cost"] == 2.0);
    CHECK(chain["confidence"] == doctest::Approx(0.765));
    CHECK(chain["effects"]["sends_off_device"] == true);
    CHECK(chain["executable"] == false);  // both steps are still planned: a proposal, never executed
    CHECK(chain["needs_confirmation"].empty());

    // a trait as the target
    const Json to_image = unwrap(e.propose_chain(kAudio, Json{{"trait", "has_image"}}));
    REQUIRE(to_image["chains"].size() == 1);
    CHECK(step_ids(to_image["chains"][0]) == std::vector<std::string>{"transcribe", "depict"});
    CHECK(to_image["chains"][0]["steps"][1]["form_op"] == "express");

    // an image can be read two ways; the more confident chain comes first
    const Json from_image = unwrap(e.propose_chain(Json{{"class", "image_form"}}, Json{{"action", "summarise"}}));
    REQUIRE(from_image["chains"].size() == 2);
    CHECK(step_ids(from_image["chains"][0]) == std::vector<std::string>{"ocr", "summarise"});
    CHECK(step_ids(from_image["chains"][1]) == std::vector<std::string>{"describe_image", "summarise"});

    // nothing to do when the element already fits, or already has the trait
    const Json direct = unwrap(e.propose_chain(kMessage, Json{{"action", "summarise"}}));
    CHECK(direct["direct"] == true);
    CHECK(step_ids(direct["chains"][0]) == std::vector<std::string>{"summarise"});
    CHECK(unwrap(e.propose_chain(kMessage, Json{{"trait", "has_text"}}))["already_present"] == true);
    // an unreachable goal gives no chain, not an error
    CHECK(unwrap(e.propose_chain(Json{{"class", "person"}}, Json{{"action", "summarise"}}))["chains"].empty());
    // bad targets are errors
    CHECK_FALSE(e.propose_chain(kAudio, Json{{"action", "no_such_action"}}));
    CHECK_FALSE(e.propose_chain(kAudio, Json::object()));
    CHECK_FALSE(e.propose_chain(kAudio, Json{{"action", "summarise"}, {"trait", "has_text"}}));
  }

  TEST_CASE("chain thresholds from the preset are respected; a call may override them; null removes a bound; off disables") {
    Engine e = unwrap(Engine::load());
    const Json target = Json{{"action", "summarise"}};
    // the shipped preset: length 3, cost 10, confidence 0.25
    const Json standard = unwrap(e.propose_chain(kAudio, target));
    CHECK(standard["preset"] == "standard");
    CHECK(standard["bounds"]["max_length"] == 3);
    CHECK(standard["chains"].size() == 1);
    // max length 1: not enough for two steps; the answer says what it would take
    const Json too_long = unwrap(e.propose_chain(kAudio, target, Json(), Json{{"max_length", 1}}));
    CHECK(too_long["chains"].empty());
    CHECK(too_long["beyond_bounds"]["exceeds"] == Json::array({"max_length"}));
    CHECK(too_long["beyond_bounds"]["length"] == 2);
    // max cost 1.5: the chain costs 2
    const Json too_dear = unwrap(e.propose_chain(kAudio, target, Json(), Json{{"max_cost", 1.5}}));
    CHECK(too_dear["chains"].empty());
    CHECK(too_dear["beyond_bounds"]["exceeds"] == Json::array({"max_cost"}));
    // min confidence 0.8: the chain has 0.765
    const Json too_unsure = unwrap(e.propose_chain(kAudio, target, Json(), Json{{"min_confidence", 0.8}}));
    CHECK(too_unsure["chains"].empty());
    CHECK(too_unsure["beyond_bounds"]["exceeds"] == Json::array({"min_confidence"}));
    // exactly on a bound is inside it
    CHECK(unwrap(e.propose_chain(kAudio, target, Json(), Json{{"max_length", 2}, {"max_cost", 2.0}}))["chains"].size() == 1);
    // null removes the bound: no limit unless the user sets one
    CHECK(unwrap(e.propose_chain(kAudio, target, Json(), Json{{"max_length", nullptr}, {"max_cost", nullptr}, {"min_confidence", nullptr}}))["chains"].size() == 1);
    CHECK(unwrap(e.propose_chain(kAudio, target, Json(), Json{{"preset", "generous"}}))["bounds"]["max_length"].is_null());
    // max results
    const Json image = unwrap(e.propose_chain(Json{{"class", "image_form"}}, target, Json(), Json{{"max_results", 1}}));
    REQUIRE(image["chains"].size() == 1);
    CHECK(step_ids(image["chains"][0])[0] == "ocr");
    // off disables proposals, and says so
    const Json off = unwrap(e.propose_chain(kAudio, target, Json(), Json{{"preset", "off"}}));
    CHECK(off["enabled"] == false);
    CHECK(off["chains"].empty());
    CHECK_FALSE(off.contains("beyond_bounds"));
    // bad values and presets are errors
    CHECK_FALSE(e.propose_chain(kAudio, target, Json(), Json{{"preset", "reckless"}}));
    CHECK_FALSE(e.propose_chain(kAudio, target, Json(), Json{{"max_length", "two"}}));
    CHECK_FALSE(e.propose_chain(kAudio, target, Json(), Json{{"max_length", 1.5}}));

    // the user's overlay changes the active preset and the presets themselves
    fsutil::TempDir td;
    overlay(td.path(), "action_rules", Json{{"extrapolation", Json{{"active", "off"}}}});
    Engine switched_off = unwrap(Engine::load(td.path()));
    CHECK(unwrap(switched_off.propose_chain(kAudio, target))["enabled"] == false);
    CHECK(unwrap(switched_off.propose_chain(kAudio, target, Json(), Json{{"preset", "standard"}}))["chains"].size() == 1);
    CHECK(unwrap(switched_off.explain(kAudio, "summarise"))["extrapolation"]["enabled"] == false);
    fsutil::TempDir tight;
    overlay(tight.path(), "action_rules", Json{{"extrapolation", Json{{"presets", Json{{"standard", Json{{"max_length", 1}}}}}}}});
    Engine strict = unwrap(Engine::load(tight.path()));
    CHECK(unwrap(strict.propose_chain(kAudio, target))["chains"].empty());
  }

  TEST_CASE("the rules apply to every step of a chain: a denied step is not proposed, a confirmed one is marked") {
    fsutil::TempDir td;
    overlay(td.path(), "action_rules",
            rules_overlay({rule("no_asr", "person", cls("audio_form"), "transcribe", "deny",
                                Json{{"scope_id", "me"}, {"initiator", "me"}, {"reason", Json{{"en", "recordings stay on the device"}}}}),
                           rule("ask_summary", "installation", of_kind("trait", "has_text"), "summarise", "confirm")}));
    Engine e = unwrap(Engine::load(td.path()));
    const Json target = Json{{"action", "summarise"}};
    const Json blocked = unwrap(e.propose_chain(kAudio, target, Json{{"person", "me"}}));
    CHECK(blocked["chains"].empty());
    REQUIRE(blocked["rejected"].size() == 1);
    CHECK(blocked["rejected"][0]["action"] == "transcribe");
    CHECK(blocked["rejected"][0]["reason"]["en"] == "recordings stay on the device");
    CHECK(blocked["rejected"][0]["initiator"] == "me");
    // someone else is not bound by it; the summary step needs a confirmation
    const Json other = unwrap(e.propose_chain(kAudio, target, Json{{"person", "you"}}));
    REQUIRE(other["chains"].size() == 1);
    CHECK(other["chains"][0]["needs_confirmation"] == Json::array({"summarise"}));
    CHECK(other["chains"][0]["steps"][1]["status"] == "needs_confirmation");
    CHECK(other["rejected"].empty());
  }

  // ── 5. exposure through loom_invoke and the action table ───────────────

  TEST_CASE("the queries are catalogued, routed over HTTP like the storage actions, and answered through loom_invoke") {
    // catalogue entries with their routes
    bool seen_for = false, seen_explain = false, seen_chain = false;
    for (const auto& row_ : action_table()) {
      if (row_["port"] != "actions") continue;
      CHECK(row_["verb"] == "POST");
      if (row_["action"] == "for") { seen_for = true; CHECK(row_["path"] == "/api/actions/for"); }
      if (row_["action"] == "explain") { seen_explain = true; CHECK(row_["path"] == "/api/actions/explain"); }
      if (row_["action"] == "propose_chain") { seen_chain = true; CHECK(row_["path"] == "/api/actions/propose_chain"); }
    }
    CHECK(seen_for);
    CHECK(seen_explain);
    CHECK(seen_chain);

    fsutil::TempDir dir;
    const std::string opts = Json{{"data_dir", dir.path().string()}, {"start_workers", false}}.dump();
    const char* err = nullptr;
    LoomContext* ctx = loom_init_ex(opts.c_str(), &err);
    REQUIRE(ctx != nullptr);
    auto call = [&](const char* action, const Json& request) {
      const char* s = loom_invoke(ctx, "actions", action, request.dump().c_str());
      REQUIRE(s != nullptr);
      auto parsed = json::parse(s);
      loom_free_string(s);
      REQUIRE(parsed);
      return *parsed;
    };
    Engine e = unwrap(Engine::load(dir.path()));
    const Json for_audio = call("for", Json{{"element", kAudio}, {"scope", "installation"}});
    CHECK(for_audio == unwrap(e.actions_for(kAudio, "installation")));
    CHECK(for_audio["default_action"] == "transcribe");
    const Json unmatched = call("for", Json{{"element", kAudio}, {"include_unmatched", true}});
    CHECK(row(unmatched, "summarise")["status"] == "not_applicable");
    const Json explained = call("explain", Json{{"element", kAudio}, {"action", "summarise"}});
    CHECK(explained == unwrap(e.explain(kAudio, "summarise")));
    const Json chain = call("propose_chain", Json{{"element", kAudio}, {"target_action", "summarise"}});
    CHECK(step_ids(chain["chains"][0]) == std::vector<std::string>{"transcribe", "summarise"});
    CHECK(call("propose_chain", Json{{"element", kAudio}, {"target_trait", "has_text"}, {"preset", "off"}})["enabled"] == false);
    CHECK(call("propose_chain", Json{{"element", kAudio}, {"target_action", "summarise"}, {"max_length", 1}})["chains"].empty());
    // errors are the usual C ABI errors
    CHECK(call("for", Json::object()).contains("error"));
    CHECK(call("explain", Json{{"element", kAudio}}).contains("error"));
    CHECK(call("explain", Json{{"element", kAudio}, {"action", "no_such"}})["error"]["code"] == "not_found");
    CHECK(call("propose_chain", Json{{"element", kAudio}}).contains("error"));
    CHECK(call("propose_chain", Json{{"element", kAudio}, {"target_trait", "a"}, {"target_action", "b"}}).contains("error"));
    CHECK(call("for", Json{{"element", kAudio}, {"scope", "nowhere"}}).contains("error"));
    CHECK(call("nope", Json{{"element", kAudio}}).contains("error"));
    loom_shutdown(ctx);
  }
}
