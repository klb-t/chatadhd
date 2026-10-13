// Scoped values (R47): the thresholds of chain proposals resolve through the same scope chain as the rules
// (installation -> user -> conversation), and propose_chain / explain say which scope set each value.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_rules.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::actions::Engine;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

void overlay(const fs::path& dir, const Json& overrides) {
  fs::create_directories(dir / "profiles");
  std::ofstream(dir / "profiles" / "action_rules.pack")
      << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "action_rules"}, {"overrides", overrides}}.dump();
}

void user_settings(const fs::path& dir, const Json& values) { std::ofstream(dir / "config.json") << values.dump(); }

Json value_rule(const std::string& id, const std::string& scope, const std::string& scope_id, const std::string& key, const Json& value,
                const Json& extra = Json::object()) {
  Json r{{"id", id}, {"scope", scope}, {"target", Json{{"kind", "setting"}, {"key", key}}}, {"effect", "set_value"}, {"value", value}};
  if (!scope_id.empty()) r["scope_id"] = scope_id;
  for (auto it = extra.begin(); it != extra.end(); ++it) r[it.key()] = it.value();
  return r;
}

Json overrides_of(std::initializer_list<Json> rules) {
  Json o = Json::object();
  for (const auto& r : rules) o[r.at("id").get<std::string>()] = r;
  return Json{{"rules", o}};
}

const Json kAudio = Json{{"class", "audio_form"}};
const Json kTarget = Json{{"action", "summarise"}};  // audio -> transcribe -> summarise: length 2, cost 2.0
Json in_conversation(const std::string& id) { return Json{{"conversation", id}, {"turn", ""}}; }

}  // namespace

TEST_SUITE("action_scope") {
  TEST_CASE("without any rule the preset is the installation's and the answer says so") {
    fsutil::TempDir td;
    Engine e = unwrap(Engine::load(td.path()));
    const Json answer = unwrap(e.propose_chain(kAudio, kTarget, in_conversation("c_a")));
    CHECK(answer.at("preset") == "standard");
    const Json& preset = answer.at("resolved").at("preset");
    CHECK(preset.at("scope") == "installation");
    CHECK(preset.at("source").get<std::string>().rfind("profile:", 0) == 0);
    CHECK(answer.at("resolved").at("max_cost").at("scope") == "installation");
    CHECK(answer.at("resolved").at("max_cost").at("preset") == "standard");
    CHECK(answer.at("scope").at("level") == "turn");
    CHECK(answer.at("bounds").at("max_cost") == 10);
    CHECK(answer.at("chains").size() == 1);
  }

  TEST_CASE("the user's setting is the person level (alias user) and is itself the preset's scope for the bounds") {
    fsutil::TempDir td;
    user_settings(td.path(), Json{{"extrapolation_preset", "generous"}});
    Engine e = unwrap(Engine::load(td.path()));
    const Json answer = unwrap(e.propose_chain(kAudio, kTarget));
    CHECK(answer.at("preset") == "generous");
    CHECK(answer.at("resolved").at("preset").at("scope") == "person");
    CHECK(answer.at("resolved").at("preset").at("source") == "setting:extrapolation_preset");
    CHECK(answer.at("bounds").at("max_cost").is_null());
    CHECK(answer.at("resolved").at("max_cost").at("scope") == "person");
    CHECK(answer.at("resolved").at("max_cost").at("preset") == "generous");
    // A question asked at the installation level does not see the user's setting.
    const Json installation = unwrap(e.propose_chain(kAudio, kTarget, Json("installation")));
    CHECK(installation.at("preset") == "standard");
    CHECK(installation.at("resolved").at("preset").at("scope") == "installation");
    // The user's own thresholds override the preset's at the user's level.
    user_settings(td.path(), Json{{"extrapolation_bounds", Json{{"max_cost", 1.5}, {"max_length", nullptr}}}});
    Engine bounded = unwrap(Engine::load(td.path()));
    const Json tight = unwrap(bounded.propose_chain(kAudio, kTarget));
    CHECK(tight.at("preset") == "standard");
    CHECK(tight.at("bounds").at("max_cost") == 1.5);
    CHECK(tight.at("bounds").at("max_length").is_null());
    CHECK(tight.at("resolved").at("max_cost").at("source") == "setting:extrapolation_bounds");
    CHECK(tight.at("resolved").at("max_cost").at("scope") == "person");
    CHECK(tight.at("resolved").at("min_confidence").at("scope") == "installation");  // untouched: the preset's
    CHECK(tight.at("chains").empty());
    CHECK(tight.contains("beyond_bounds"));
    // A live Config replaces what the file said.
    bounded.set_user_values(Json{{"extrapolation_preset", "off"}});
    CHECK(unwrap(bounded.propose_chain(kAudio, kTarget)).at("enabled") == false);
  }

  TEST_CASE("a rule on one conversation changes the thresholds of that conversation only; the more specific scope wins") {
    fsutil::TempDir td;
    user_settings(td.path(), Json{{"extrapolation_preset", "off"}});
    overlay(td.path(), overrides_of({value_rule("conv_generous", "conversation", "c_a", "extrapolation.preset", "generous", Json{{"initiator", "the user, in this chat"}})}));
    Engine e = unwrap(Engine::load(td.path()));
    CHECK(e.issues().empty());
    const Json mine = unwrap(e.propose_chain(kAudio, kTarget, in_conversation("c_a")));
    CHECK(mine.at("preset") == "generous");
    CHECK(mine.at("enabled") == true);
    const Json& by = mine.at("resolved").at("preset");
    CHECK(by.at("scope") == "conversation");
    CHECK(by.at("scope_id") == "c_a");
    CHECK(by.at("rule") == "conv_generous");
    CHECK(by.at("initiator") == "the user, in this chat");
    CHECK(by.at("source") == "rule");
    CHECK(mine.at("resolved").at("max_cost").at("scope") == "conversation");  // the preset's bound, at the scope that chose it
    CHECK(mine.at("resolved").at("max_cost").at("scope_id") == "c_a");
    // Another conversation keeps the user's level; a question without a conversation too.
    const Json other = unwrap(e.propose_chain(kAudio, kTarget, in_conversation("c_b")));
    CHECK(other.at("preset") == "off");
    CHECK(other.at("resolved").at("preset").at("scope") == "person");
    CHECK(unwrap(e.propose_chain(kAudio, kTarget)).at("preset") == "off");
    // The considered rules say what was overridden and what was inactive.
    const auto sv = unwrap(e.resolve_value("extrapolation.preset", unwrap(e.parse_scope(in_conversation("c_a"))),
                                           {actions::Baseline{"installation", "standard", "profile:test", 0}, actions::Baseline{"person", "off", "setting:x", 0}}));
    CHECK(sv.value == "generous");
    CHECK(sv.considered.size() == 3);
    CHECK(sv.considered.dump().find("overridden") != std::string::npos);
    const auto elsewhere = unwrap(e.resolve_value("extrapolation.preset", unwrap(e.parse_scope(in_conversation("c_b"))),
                                                  {actions::Baseline{"installation", "standard", "profile:test", 0}}));
    CHECK(elsewhere.value == "standard");
    CHECK(elsewhere.considered.dump().find("inactive") != std::string::npos);  // the rule of c_a is there, not active here
  }

  TEST_CASE("a bound of one conversation overrides the preset below it, and not one that a more specific scope chose") {
    fsutil::TempDir td;
    overlay(td.path(), overrides_of({value_rule("c_cost", "conversation", "c_a", "extrapolation.max_cost", 1.5),
                                     value_rule("p_gen", "person", "", "extrapolation.preset", "generous"),
                                     value_rule("c_len", "conversation", "c_a", "extrapolation.max_length", 1)}));
    Engine e = unwrap(Engine::load(td.path()));
    const Json a = unwrap(e.propose_chain(kAudio, kTarget, in_conversation("c_a")));
    CHECK(a.at("preset") == "generous");  // the person-level rule (alias user works for rules too)
    CHECK(a.at("bounds").at("max_cost") == 1.5);
    CHECK(a.at("bounds").at("max_length") == 1);
    CHECK(a.at("resolved").at("max_cost").at("scope") == "conversation");
    CHECK(a.at("chains").empty());
    CHECK(a.at("beyond_bounds").at("exceeds").size() == 2);
    const Json b = unwrap(e.propose_chain(kAudio, kTarget, in_conversation("c_b")));
    CHECK(b.at("bounds").at("max_cost").is_null());
    CHECK(b.at("chains").size() == 1);
    // A preset chosen at a more specific scope than a bound rule brings its own bounds.
    fsutil::TempDir td2;
    overlay(td2.path(), overrides_of({value_rule("p_cost", "person", "", "extrapolation.max_cost", 1.5),
                                      value_rule("c_gen", "conversation", "c_a", "extrapolation.preset", "generous")}));
    Engine f = unwrap(Engine::load(td2.path()));
    const Json c = unwrap(f.propose_chain(kAudio, kTarget, in_conversation("c_a")));
    CHECK(c.at("bounds").at("max_cost").is_null());
    CHECK(c.at("resolved").at("max_cost").at("scope") == "conversation");
    CHECK(c.at("resolved").at("max_cost").at("source") == "preset:generous");
    const Json d = unwrap(f.propose_chain(kAudio, kTarget, in_conversation("c_z")));
    CHECK(d.at("bounds").at("max_cost") == 1.5);
    CHECK(d.at("resolved").at("max_cost").at("scope") == "person");
  }

  TEST_CASE("a pin from a higher scope holds against the user and the conversation (conflict rule locked_by)") {
    fsutil::TempDir td;
    user_settings(td.path(), Json{{"extrapolation_preset", "generous"}});
    overlay(td.path(), overrides_of({value_rule("org_pin", "installation", "", "extrapolation.preset", "standard",
                                                Json{{"locked", true}, {"conflict", "locked_by"}, {"initiator", "the administrator"}})}));
    Engine e = unwrap(Engine::load(td.path()));
    const Json answer = unwrap(e.propose_chain(kAudio, kTarget, in_conversation("c_a")));
    CHECK(answer.at("preset") == "standard");
    CHECK(answer.at("resolved").at("preset").at("rule") == "org_pin");
    CHECK(answer.at("resolved").at("preset").at("conflict_rule") == "locked_by");
    CHECK(answer.at("resolved").at("preset").at("initiator") == "the administrator");
  }

  TEST_CASE("explain says which scope set the thresholds of the chain it offers; a call overrides every scope") {
    fsutil::TempDir td;
    overlay(td.path(), overrides_of({value_rule("c_gen", "conversation", "c_a", "extrapolation.preset", "generous")}));
    Engine e = unwrap(Engine::load(td.path()));
    const Json why = unwrap(e.explain(kAudio, "summarise", in_conversation("c_a")));
    REQUIRE(why.contains("extrapolation"));
    CHECK(why.at("extrapolation").at("resolved").at("preset").at("scope") == "conversation");
    CHECK(why.at("extrapolation").at("resolved").at("preset").at("scope_id") == "c_a");
    CHECK(why.at("extrapolation").at("preset") == "generous");
    CHECK(unwrap(e.explain(kAudio, "summarise", in_conversation("c_q"))).at("extrapolation").at("resolved").at("preset").at("scope") == "installation");
    const Json called = unwrap(e.propose_chain(kAudio, kTarget, in_conversation("c_a"), Json{{"preset", "off"}, {"max_cost", 7}}));
    CHECK(called.at("enabled") == false);
    CHECK(called.at("resolved").at("preset").at("source") == "call");
    const Json bound_called = unwrap(e.propose_chain(kAudio, kTarget, in_conversation("c_a"), Json{{"max_length", 3}}));
    CHECK(bound_called.at("bounds").at("max_length") == 3);
    CHECK(bound_called.at("resolved").at("max_length").at("source") == "call");
  }

  TEST_CASE("a value that no preset knows is refused with the scope that set it; malformed value rules are reported") {
    fsutil::TempDir td;
    overlay(td.path(), overrides_of({value_rule("typo", "conversation", "c_a", "extrapolation.preset", "reckless"),
                                     Json{{"id", "no_key"}, {"scope", "installation"}, {"target", Json{{"kind", "setting"}}}, {"effect", "set_value"}, {"value", 1}},
                                     Json{{"id", "no_value"}, {"scope", "installation"}, {"target", Json{{"kind", "setting"}, {"key", "k"}}}, {"effect", "set_value"}}}));
    Engine e = unwrap(Engine::load(td.path()));
    auto refused = e.propose_chain(kAudio, kTarget, in_conversation("c_a"));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message.find("reckless") != std::string::npos);
    CHECK(refused.error().message.find("conversation") != std::string::npos);
    CHECK(unwrap(e.propose_chain(kAudio, kTarget, in_conversation("c_b"))).at("preset") == "standard");
    const std::string issues = e.issues().dump();
    CHECK(issues.find("value_rule_without_key") != std::string::npos);
    CHECK(issues.find("value_rule_without_value") != std::string::npos);
    // A bound that is not a number is refused, not guessed.
    fsutil::TempDir td2;
    overlay(td2.path(), overrides_of({value_rule("bad", "person", "", "extrapolation.max_cost", "plenty")}));
    Engine f = unwrap(Engine::load(td2.path()));
    CHECK_FALSE(f.propose_chain(kAudio, kTarget));
  }

  TEST_CASE("a question asked above the installation and a preset named in the call keep the preset's own bounds") {
    fsutil::TempDir td;
    user_settings(td.path(), Json{{"extrapolation_preset", "generous"}, {"extrapolation_bounds", Json{{"max_cost", 1.5}}}});
    Engine e = unwrap(Engine::load(td.path()));
    // above the installation no baseline is active: the profile's active preset answers, as before values were scoped
    const Json high = unwrap(e.propose_chain(kAudio, kTarget, Json("app_preset")));
    CHECK(high.at("preset") == "standard");
    CHECK(high.at("resolved").at("preset").at("fallback") == true);
    CHECK(high.at("bounds").at("max_cost") == 10);
    CHECK(high.at("bounds").at("max_length") == 3);
    // a preset named in the call is taken as it is: not the user's thresholds, not a rule, at any level
    const Json called = unwrap(e.propose_chain(kAudio, kTarget, Json(), Json{{"preset", "standard"}}));
    CHECK(called.at("bounds").at("max_cost") == 10);
    CHECK(called.at("resolved").at("max_cost").at("source") == "preset:standard");
    const Json at_installation = unwrap(e.propose_chain(kAudio, kTarget, Json("installation"), Json{{"preset", "standard"}}));
    CHECK(at_installation.at("bounds").at("max_length") == 3);
    CHECK(at_installation.at("resolved").at("max_length").at("scope") == "installation");
  }

  TEST_CASE("value rules never take part in the gate or the default of an action") {
    fsutil::TempDir td;
    overlay(td.path(), overrides_of({value_rule("v", "conversation", "c_a", "extrapolation.preset", "generous")}));
    Engine with_rule = unwrap(Engine::load(td.path()));
    fsutil::TempDir empty;
    Engine without = unwrap(Engine::load(empty.path()));
    const Json message = Json{{"class", "message"}};
    CHECK(with_rule.actions_for(message, in_conversation("c_a")).value().dump() == without.actions_for(message, in_conversation("c_a")).value().dump());
  }
}
