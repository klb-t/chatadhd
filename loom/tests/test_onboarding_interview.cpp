// M1-K2a: the interview of the person ("Ty") in two languages, the first question about HOW MUCH to ask (detail levels),
// and remarks that close topics. Synthetic data only; the scenario is the builtin loom/data/onboarding/scenario.pack.
#include <doctest/doctest.h>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "loom/onboarding.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::unwrap;

namespace {
Json scenario_pack() {
  auto path = std::filesystem::weakly_canonical(std::filesystem::path(LOOM_TEST_FIXTURES) / ".." / "..") / "data/onboarding/scenario.pack";
  std::ifstream input(path);
  std::stringstream text; text << input.rdbuf();
  return unwrap(json::parse(text.str()));
}
Json defaults_with(Json rule) { return Json{{"privacy", {{"rules", Json::array({std::move(rule)})}}}, {"settings", {{"preference_mode", "ask"}}}}; }
Json wildcard_rule() {
  return Json{{"id", "fixture/*"}, {"category", "*"}, {"store", true}, {"infer", false}, {"explicit_only", true}, {"providers", Json::array({"offline/provider-a"})},
    {"max_detail", nullptr}, {"max_sensitivity", nullptr}, {"retention", {{"history", "full"}, {"max_events", nullptr}, {"metadata_keys", Json::array()}}}};
}
ProfileSession fresh() { return unwrap(ProfileSession::create(scenario_pack(), defaults_with(wildcard_rule()))); }
Json act(std::string op, std::string id, std::string field = "") {
  Json a{{"op", op}, {"id", id}, {"time", "2000-01-01T00:00:00Z"}, {"source_refs", Json::array({"synthetic/" + id})}};
  if (!field.empty()) a["field"] = field;
  return a;
}
void know(ProfileSession& s, const std::string& field, Json value) {
  auto a = act("answer", "ans/" + field, field); a["value"] = std::move(value); a["provenance"] = "user_stated";
  LOOM_REQUIRE_OK(s.dispatch(a));
  auto r = act("review", "rev/" + field); r["candidate"] = "ans/" + field; r["decision"] = "confirmed";
  LOOM_REQUIRE_OK(s.dispatch(r));
}
void set_status(ProfileSession& s, const std::string& field, const std::string& status) {
  auto a = act("status", "st/" + field + "/" + status, field); a["status"] = status;
  LOOM_REQUIRE_OK(s.dispatch(a));
}
// the first question ("how much to ask") answered: the plans below are the full ones
ProfileSession chosen() { auto s = fresh(); know(s, "interview.detail", "normal"); return s; }
Json plan_of(const ProfileSession& s, const std::string& level, Json by_category = Json::object()) {
  return unwrap(s.plan(Json{{"level", level}, {"by_category", std::move(by_category)}}));
}
std::set<std::string> ask_ids(const Json& plan) {
  std::set<std::string> ids;
  for (const auto& row : plan["asks"]) ids.insert(row["field"].get<std::string>());
  return ids;
}
std::string skip_reason(const Json& plan, const std::string& field) {
  for (const auto& row : plan["skipped"]) if (row["field"] == field) return row["reason"].get<std::string>();
  return "";
}
std::vector<long> numbers_in(const std::string& text) {
  std::vector<long> out;
  for (std::size_t i = 0; i < text.size();) {
    if (std::isdigit(static_cast<unsigned char>(text[i]))) {
      std::size_t j = i; while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
      out.push_back(std::stol(text.substr(i, j - i))); i = j;
    } else ++i;
  }
  return out;
}
Json remark(ProfileSession& s, const std::string& text, const std::string& locale, Json options = Json::object()) {
  return unwrap(s.apply_remark(text, locale, options));
}
bool matched(const Json& result, const std::string& rule) {
  for (const auto& id : result["matched"]) if (id == rule) return true;
  return false;
}
Json apply_options(Json extra = Json::object()) {
  Json o{{"apply", true}, {"id", "remark/1"}, {"time", "2000-01-02T00:00:00Z"}};
  for (const auto& [k, v] : extra.items()) o[k] = v;
  return o;
}
}  // namespace

TEST_SUITE("onboarding") {
  TEST_CASE("interview: every text for the person exists in Polish and English, and the Polish one is a translation") {
    const auto scenario = scenario_pack();
    auto expect_both = [](const Json& text, const std::string& what) {
      INFO(what);
      REQUIRE(text.is_object());
      CHECK(text.contains("en")); CHECK(text.contains("pl"));
      CHECK_FALSE(text["pl"].get<std::string>().empty());
      CHECK(text["pl"] != text["en"]);
    };
    for (const auto& section : scenario["sections"]) {
      expect_both(section["title"], "section " + section["id"].get<std::string>());
      for (const auto& q : section["questions"]) expect_both(q["text"], "question " + q["field"].get<std::string>());
    }
    for (const auto& f : scenario["fields"]) expect_both(f["topic"], "topic " + f["id"].get<std::string>());
    for (const auto& c : scenario["categories"]) expect_both(c["label"], "category " + c["id"].get<std::string>());
    for (const auto& level : scenario["detail_levels"]) {
      expect_both(level["label"], "level " + level["id"].get<std::string>());
      for (const auto& [part, text] : level["preview"].items()) expect_both(text, "preview " + part);
    }
    for (const auto& [code, text] : scenario["skip_reasons"].items()) expect_both(text, "skip reason " + code);
    for (const auto& rule : scenario["remark_rules"]) expect_both(rule["understood"], "understood " + rule["id"].get<std::string>());
    // the recipe the model method is hashed from carries the same sections, and the displayed hash is the real one
    CHECK(scenario["graph_method"]["recipe"]["sections"] == scenario["sections"]);
    CHECK(scenario["method_ref"]["recipe_hash"] == Sha256::hex(json::canonical(scenario["graph_method"])));
  }

  // M1-K2a-fix: the rows of "Ty" took their names from graph_sources.pack, which had labels for 9 of the 13 fields, so four rows
  // showed the raw field id (interview.detail, health.needs, ...). Data driven: any field the scenario adds needs its name there.
  TEST_CASE("interview: every field of the scenario has a name in both languages in the slot_labels of graph_sources.pack, and no name is the raw id") {
    const auto scenario = scenario_pack();
    auto path = std::filesystem::weakly_canonical(std::filesystem::path(LOOM_TEST_FIXTURES) / ".." / "..") / "data/runtime/graph_sources.pack";
    std::ifstream input(path);
    std::stringstream text; text << input.rdbuf();
    const Json pack = unwrap(json::parse(text.str()));
    const Json* labels = nullptr;
    for (const auto& source : pack.at("defaults").at("sources"))
      if (source.value("id", "") == "party") labels = &source.at("slot_labels");
    REQUIRE(labels != nullptr);
    std::vector<std::string> missing;
    for (const auto& f : scenario.at("fields")) {
      const std::string id = f.at("id").get<std::string>();
      for (const char* lang : {"en", "pl"}) {
        const Json* one = labels->contains(id) ? &labels->at(id) : nullptr;
        const bool ok = one && one->is_object() && one->contains(lang) && (*one)[lang].is_string() &&
                        !(*one)[lang].get<std::string>().empty() && (*one)[lang].get<std::string>() != id;
        if (!ok) missing.push_back(id + " [" + lang + "]");
      }
    }
    std::string list;
    for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
    INFO("slot_labels of graph_sources.pack (source party) lack a name, or name the raw id, for: " << list);
    CHECK(missing.empty());
  }

  TEST_CASE("interview: questions(locale) returns the requested language, English as the fallback that says so") {
    const auto scenario = scenario_pack();
    auto s = fresh();
    const auto pl = s.questions("pl");
    const auto en = s.questions("en");
    REQUIRE(pl.size() == scenario["sections"].size());
    CHECK(pl[0]["id"] == "detail");  // the first thing the person is asked is how much to ask
    for (std::size_t i = 0; i < pl.size(); ++i) {
      CHECK_FALSE(pl[i].contains("fallback"));
      CHECK(pl[i]["title"] == scenario["sections"][i]["title"]["pl"]);
      CHECK(en[i]["title"] == scenario["sections"][i]["title"]["en"]);
      for (std::size_t j = 0; j < pl[i]["questions"].size(); ++j) {
        const auto& q = scenario["sections"][i]["questions"][j];
        CHECK_FALSE(pl[i]["questions"][j].contains("fallback"));
        CHECK(pl[i]["questions"][j]["text"] == q["text"]["pl"]);
        CHECK(en[i]["questions"][j]["text"] == q["text"]["en"]);
        // the owner's register: second person, no "application"/"user" calls in the Polish questions
        const auto text = pl[i]["questions"][j]["text"].get<std::string>();
        CHECK(text.find("aplikacj") == std::string::npos); CHECK(text.find("użytkownik") == std::string::npos);
      }
    }
    const auto other = s.questions("de");  // no German: English, and each row says so
    CHECK(other[0]["questions"][0]["text"] == scenario["sections"][0]["questions"][0]["text"]["en"]);
    CHECK(other[0]["questions"][0]["fallback"] == true);
    // the model gets one string in one language
    const auto request = unwrap(s.model_request("offline/provider-a", "pl"));
    CHECK(request["questions"][0]["text"] == scenario["sections"][0]["questions"][0]["text"]["pl"]);
    CHECK(unwrap(s.model_request("offline/provider-a"))["questions"][0]["text"] == scenario["sections"][0]["questions"][0]["text"]["en"]);
  }

  TEST_CASE("interview: a legacy scenario with plain-string texts still loads; a malformed text does not") {
    auto scenario = scenario_pack();
    for (auto& section : scenario["sections"]) {
      section["title"] = section["title"]["en"];
      for (auto& q : section["questions"]) q["text"] = q["text"]["en"];
    }
    auto s = unwrap(ProfileSession::create(scenario, defaults_with(wildcard_rule())));
    CHECK(s.questions("pl")[0]["questions"][0]["text"] == scenario["sections"][0]["questions"][0]["text"]);
    auto bad = scenario_pack(); bad["sections"][0]["questions"][0]["text"] = Json{{"pl", "tylko po polsku"}};
    CHECK_FALSE(ProfileSession::create(bad, defaults_with(wildcard_rule())).has_value());
    bad = scenario_pack(); bad["detail_levels"][0]["preview"].erase("asks");
    CHECK_FALSE(ProfileSession::create(bad, defaults_with(wildcard_rule())).has_value());
  }

  TEST_CASE("interview: the first question is how much to ask, and nothing else is asked before it is answered") {
    auto s = fresh();
    const auto first = unwrap(s.plan(Json{{"level", "normal"}}));
    CHECK(first["level_choice_pending"] == true);
    CHECK(ask_ids(first) == std::set<std::string>{"interview.detail"});
    CHECK(skip_reason(first, "communication.style") == "level_not_chosen");
    // the question names the setting its answer belongs to (the profile field only records the answer)
    const Json described = s.scenario();
    for (const auto& f : described["fields"]) if (f["id"] == "interview.detail") CHECK(f["setting_key"] == "profile_detail_level");
    know(s, "interview.detail", "minimal");
    const auto second = plan_of(s, "minimal");
    CHECK(second["level_choice_pending"] == false);
    CHECK(skip_reason(second, "interview.detail") == "known");
    // a person who declines to answer it is not asked again, and the preset level of the setting applies
    auto d = fresh(); set_status(d, "interview.detail", "declined");
    CHECK(plan_of(d, "normal")["level_choice_pending"] == false);
    CHECK(skip_reason(plan_of(d, "normal"), "interview.detail") == "declined");
    CHECK_FALSE(plan_of(d, "normal")["asks"].empty());
  }

  TEST_CASE("interview: minimal asks fewer questions than normal, normal fewer than detailed, each level inside the next") {
    auto s = chosen();
    const auto minimal = plan_of(s, "minimal"), normal = plan_of(s, "normal"), detailed = plan_of(s, "detailed");
    CHECK(minimal["asks"].size() < normal["asks"].size());
    CHECK(normal["asks"].size() < detailed["asks"].size());
    const auto a = ask_ids(minimal), b = ask_ids(normal), c = ask_ids(detailed);
    CHECK(std::includes(b.begin(), b.end(), a.begin(), a.end()));
    CHECK(std::includes(c.begin(), c.end(), b.begin(), b.end()));
    CHECK(a == std::set<std::string>{"communication.style", "communication.language"});  // what the profile prefix of "minimal" carries
    CHECK(minimal["counts"]["asks"] == minimal["asks"].size());
    const Json described = s.scenario();
    CHECK(minimal["counts"]["skipped"].get<std::size_t>() + minimal["counts"]["asks"].get<std::size_t>() == described["fields"].size());
    CHECK(skip_reason(minimal, "communication.attention") == "detail_above_level");
    CHECK(skip_reason(minimal, "knowledge.remember") == "detail_above_level");
    CHECK(skip_reason(normal, "health.needs") == "detail_above_level");
    CHECK(ask_ids(detailed).contains("health.needs"));
  }

  TEST_CASE("interview: unknown level and a missing level are errors, never a silent default") {
    auto s = chosen();
    CHECK_FALSE(s.plan(Json{{"level", "enormous"}}).has_value());
    CHECK_FALSE(s.plan(Json::object()).has_value());
    CHECK_FALSE(s.plan(Json{{"level", "normal"}, {"by_category", {{"health", "enormous"}}}}).has_value());
  }

  TEST_CASE("interview: declined and never are respected on the next plan, known answers are not asked again") {
    auto s = chosen();
    CHECK(ask_ids(plan_of(s, "detailed")).contains("communication.style"));
    set_status(s, "communication.style", "declined");
    set_status(s, "communication.language", "never");
    know(s, "work.projects", "synthetic project");
    for (const char* level : {"minimal", "normal", "detailed"}) {
      const auto p = plan_of(s, level);
      CHECK_FALSE(ask_ids(p).contains("communication.style"));
      CHECK_FALSE(ask_ids(p).contains("communication.language"));
      CHECK_FALSE(ask_ids(p).contains("work.projects"));
      CHECK(skip_reason(p, "communication.style") == "declined");
      CHECK(skip_reason(p, "communication.language") == "never");
      CHECK(skip_reason(p, "work.projects") == "known");
    }
    // a never field stays never even where it is also above the level: the strongest reason is reported
    set_status(s, "knowledge.avoid", "never");
    CHECK(skip_reason(plan_of(s, "minimal"), "knowledge.avoid") == "never");
    // the privacy rules are respected too: a cap below the field's sensitivity means it is not asked
    auto cap = wildcard_rule(); cap["max_sensitivity"] = 1;
    auto capped = unwrap(ProfileSession::create(scenario_pack(), defaults_with(cap)));
    know(capped, "interview.detail", "detailed");
    const auto p = plan_of(capped, "detailed");
    CHECK(skip_reason(p, "health.needs") == "privacy_denied");
    CHECK_FALSE(ask_ids(p).contains("communication.attention"));
  }

  TEST_CASE("interview: a field inside the detail cap but above the sensitivity cap is skipped for that reason") {
    auto scenario = scenario_pack();
    scenario["detail_levels"][0]["max_detail"] = 3;  // minimal: any detail, but only ordinary sensitivity
    auto s = unwrap(ProfileSession::create(scenario, defaults_with(wildcard_rule())));
    know(s, "interview.detail", "minimal");
    const auto p = plan_of(s, "minimal");
    CHECK(skip_reason(p, "communication.attention") == "sensitivity_above_level");
    CHECK(skip_reason(p, "health.needs") == "sensitivity_above_level");
    CHECK(ask_ids(p).contains("knowledge.remember"));
  }

  TEST_CASE("interview: a level per category overrides the global level for that category only") {
    auto s = chosen();
    const auto p = plan_of(s, "detailed", Json{{"health", "minimal"}, {"communication", "minimal"}});
    CHECK(skip_reason(p, "health.needs") == "detail_above_level");
    CHECK(skip_reason(p, "communication.attention") == "detail_above_level");
    CHECK(ask_ids(p).contains("knowledge.remember"));
    const auto q = plan_of(s, "minimal", Json{{"health", "detailed"}});
    CHECK(ask_ids(q).contains("health.needs"));
    CHECK_FALSE(ask_ids(q).contains("relations.people"));
  }

  TEST_CASE("interview: the numbers in the level previews are the real counts, in both languages, also after answers") {
    auto s = fresh();
    for (const char* locale : {"pl", "en"}) {
      const auto levels = unwrap(s.levels(locale));
      REQUIRE(levels.size() == 3);
      CHECK(levels[0]["id"] == "minimal"); CHECK(levels[1]["id"] == "normal"); CHECK(levels[2]["id"] == "detailed");
    }
    know(s, "interview.detail", "normal");
    know(s, "communication.style", "short and warm");
    set_status(s, "relations.people", "never");
    set_status(s, "communication.length", "declined");
    for (const char* locale : {"pl", "en"}) {
      const auto levels = unwrap(s.levels(locale));
      for (const auto& level : levels) {
        const auto id = level["id"].get<std::string>();
        const auto p = plan_of(s, id);  // the plan of the same profile at that level (the preview assumes the level is chosen)
        const auto expect_asks = static_cast<long>(ask_ids(p).size());
        CHECK(level["counts"]["asks"] == expect_asks);
        CHECK(level["counts"]["stores"] == p["counts"]["stores"]);
        CHECK(level["counts"]["model_sees"] == p["counts"]["model_sees"]);
        const auto& preview = level["preview"];
        CHECK(numbers_in(preview["asks"].get<std::string>()) == std::vector<long>{expect_asks});
        CHECK(numbers_in(preview["stores"].get<std::string>()) == std::vector<long>{p["counts"]["stores"].get<long>()});
        CHECK(numbers_in(preview["model_sees"].get<std::string>()) == std::vector<long>{p["counts"]["model_sees"].get<long>()});
        for (const char* part : {"asks", "stores", "model_sees"}) CHECK(preview[part].get<std::string>().find('{') == std::string::npos);
        CHECK_FALSE(level.contains("fallback"));
      }
    }
    // the known answer and the never field change the counts (so they are not constants)
    CHECK(unwrap(s.levels("en"))[1]["counts"]["model_sees"] == 1);
    CHECK(unwrap(s.levels("en"))[2]["counts"]["stores"].get<long>() == plan_of(s, "detailed")["counts"]["stores"].get<long>());
    // Polish and English previews differ and the topics are the real questions of that level
    CHECK(unwrap(s.levels("pl"))[0]["preview"]["asks"] != unwrap(s.levels("en"))[0]["preview"]["asks"]);
    CHECK(unwrap(s.levels("pl"))[0]["label"] == "Minimalnie");
    CHECK(unwrap(s.levels("pl"))[0]["preview"]["asks"].get<std::string>().find("język") != std::string::npos);
    CHECK(unwrap(s.levels("de"))[0]["fallback"] == true);
  }

  TEST_CASE("interview remarks: 'nie pytaj o zdrowie' and 'do not ask about health' propose never for the category, and write nothing") {
    for (const auto& [text, locale] : {std::pair<std::string, std::string>{"nie pytaj o zdrowie", "pl"}, {"Proszę, NIE PYTAJ o zdrowie!", "pl"},
                                         {"do not ask about health", "en"}, {"Don't ask about my health.", "en"}}) {
      INFO(text);
      auto s = chosen();
      const auto before = s.snapshot();
      const auto result = remark(s, text, locale);
      REQUIRE(matched(result, "never.health"));
      const auto& proposal = result["proposals"][0];
      CHECK(proposal["kind"] == "status"); CHECK(proposal["status"] == "never"); CHECK(proposal["category"] == "health");
      CHECK(proposal["fields"] == Json::array({"communication.attention", "health.needs"}));  // ADHD needs are health too
      CHECK_FALSE(result["understood"][0].get<std::string>().empty());
      CHECK(s.snapshot() == before);  // a proposal writes nothing
    }
  }

  TEST_CASE("interview remarks: applying a never reuses the existing never semantics and keeps it forever") {
    auto s = chosen();
    const auto result = remark(s, "nie pytaj o zdrowie", "pl", apply_options());
    CHECK(result["applied"].size() == 2);
    auto via_op = chosen(); set_status(via_op, "health.needs", "never");
    CHECK(s.snapshot()["fields"]["health.needs"] == via_op.snapshot()["fields"]["health.needs"]);  // same field state as the person's own never
    const auto state = s.snapshot();
    CHECK(state["fields"]["health.needs"]["status"] == "never");
    CHECK(state["fields"]["communication.attention"]["status"] == "never");
    CHECK(state["fields"]["communication.style"]["status"] == "unknown");
    // not asked, not sent, not inferred, not answered; planned out at every level
    for (const char* op : {"ask", "send", "infer"}) {
      Json request{{"op", op}, {"category", "health"}, {"field", "health.needs"}, {"provider", "offline/provider-a"}};
      CHECK(unwrap(privacy_decision(state, request))["allowed"] == false);
    }
    auto answer = act("answer", "late", "health.needs"); answer["value"] = "x";
    CHECK_FALSE(s.dispatch(answer).has_value());
    CHECK(skip_reason(plan_of(s, "detailed"), "health.needs") == "never");
    // the model request of the section holding it no longer carries the question
    for (const char* section : {"detail", "identity", "communication", "knowledge", "privacy"}) LOOM_REQUIRE_OK(s.dispatch([&] { auto a = act("skip", std::string("skip/") + section); a["section"] = section; return a; }()));
    const auto request = unwrap(s.model_request("offline/provider-a"));
    CHECK(request["section"] == "life");
    std::set<std::string> asked;
    for (const auto& q : request["questions"]) asked.insert(q["field"].get<std::string>());
    CHECK(asked == std::set<std::string>{"relations.people", "routine.rhythm"});
    CHECK(request["policy"]["health.needs"]["may_ask"] == false);
    // a second remark does not change it; the same remark applied twice is idempotent
    const auto again = remark(s, "nie pytaj o zdrowie", "pl", apply_options({{"id", "remark/2"}}));
    CHECK(again["applied"].empty()); CHECK(again["proposals"][0]["already"] == true);
    // only the person's explicit reopen lifts it
    const auto reopen = remark(s, "możesz pytać o zdrowie", "pl", apply_options({{"id", "remark/3"}}));
    CHECK(matched(reopen, "reopen.health")); CHECK_FALSE(matched(reopen, "never.health"));
    CHECK(s.snapshot()["fields"]["health.needs"]["status"] == "unknown");
    CHECK(ask_ids(plan_of(s, "detailed")).contains("health.needs"));
  }

  TEST_CASE("interview remarks: a never over stored answers needs the person to see them erased first") {
    auto s = chosen();
    know(s, "health.needs", "synthetic note");
    const auto proposal = remark(s, "nie pytaj o zdrowie", "pl");
    CHECK(proposal["proposals"][0]["erases"] == Json::array({"health.needs"}));
    const auto before = s.snapshot();
    auto refused = s.apply_remark("nie pytaj o zdrowie", "pl", apply_options());
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == Errc::Conflict);
    CHECK(s.snapshot() == before);  // nothing half-applied
    LOOM_REQUIRE_OK(s.apply_remark("nie pytaj o zdrowie", "pl", apply_options({{"allow_erase", true}})));
    CHECK(s.snapshot()["fields"]["health.needs"]["status"] == "never");
    CHECK(s.snapshot()["fields"]["health.needs"]["value"].is_null());
  }

  TEST_CASE("interview remarks: every category of the scenario has a never rule and the patterns match in both languages") {
    auto s = chosen();
    const Json described = s.scenario();
    std::set<std::string> categories;
    for (const auto& f : described["fields"]) if (f["category"] != "interview") categories.insert(f["category"].get<std::string>());
    CHECK(categories.size() >= 8);
    for (const auto& category : categories) {
      INFO(category);
      bool found = false;
      for (const auto& rule : described["remark_rules"]) {
        if (rule["effect"].value("category", "") != category || rule["effect"].value("status", "") != "never") continue;
        found = true;
        for (const char* locale : {"pl", "en"}) {
          REQUIRE_FALSE(rule["pattern"][locale].empty());
          for (const auto& pattern : rule["pattern"][locale]) {  // every pattern, said in the person's own words, finds its rule
            INFO(pattern.get<std::string>());
            const auto result = remark(s, "Dobrze. " + pattern.get<std::string>() + ".", locale);
            CHECK(matched(result, rule["id"].get<std::string>()));
          }
        }
      }
      CHECK(found);
    }
    // the rules that reopen mirror them
    for (const auto& category : categories) {
      bool found = false;
      for (const auto& rule : described["remark_rules"]) if (rule["effect"].value("category", "") == category && rule["effect"].value("status", "") == "unknown") found = true;
      CHECK(found);
    }
  }

  TEST_CASE("interview remarks: 'wystarczy' and 'pytaj więcej' propose one level lower and higher; no match, no effect") {
    auto s = chosen();
    auto less = remark(s, "wystarczy", "pl", Json{{"level", "normal"}});
    REQUIRE(matched(less, "level.less"));
    CHECK(less["proposals"][0]["from"] == "normal"); CHECK(less["proposals"][0]["to"] == "minimal"); CHECK(less["proposals"][0]["write"] == "profile_detail_level");
    CHECK(remark(s, "That's enough for now", "en", Json{{"level", "detailed"}})["proposals"][0]["to"] == "normal");
    CHECK(remark(s, "Pytaj WIĘCEJ", "pl", Json{{"level", "normal"}})["proposals"][0]["to"] == "detailed");
    CHECK(remark(s, "pytaj mnie wiecej", "pl", Json{{"level", "minimal"}})["proposals"][0]["to"] == "normal");  // diacritics folded
    CHECK(remark(s, "ask more", "en", Json{{"level", "normal"}})["proposals"][0]["to"] == "detailed");
    const auto limit = remark(s, "ask more", "en", Json{{"level", "detailed"}});
    CHECK(limit["proposals"][0]["at_limit"] == true); CHECK(limit["proposals"][0]["to"] == "detailed");
    CHECK(remark(s, "wystarczy", "pl")["proposals"][0]["needs_level"] == true);  // without the current level the target is not guessed
    // the level is a setting: the flag does not write it, and nothing in the profile changes
    const auto before = s.snapshot();
    const auto applied = remark(s, "wystarczy", "pl", apply_options({{"level", "normal"}}));
    CHECK(applied["applied"].empty());
    CHECK(s.snapshot()["fields"] == before["fields"]);
    // no match, no effect: unrelated text, a different word, a negated phrase, the other language
    for (const auto& text : {"dzisiaj jest ładna pogoda", "wystarczyło mi tego", "nie wystarczy", "to nie jest enough", "pytaj o zdrowie"}) {
      INFO(text);
      const auto none = remark(s, text, "pl");
      CHECK(none["matched"].empty()); CHECK(none["proposals"].empty());
    }
    CHECK(remark(s, "do not ask more", "en")["matched"].empty());
    CHECK(remark(s, "nie pytaj o zdrowie", "en")["matched"].empty());       // Polish words do not count as English
    CHECK(remark(s, "nie pytaj o zdrowie", "")["matched"].size() == 1);    // no language named: both are tried
    // contradicting remarks are reported and refused, not resolved by guessing
    const auto both = remark(s, "wystarczy, ale pytaj więcej", "pl", Json{{"level", "normal"}});
    CHECK(both["conflicts"].size() == 1);
    auto refused = s.apply_remark("nie pytaj o zdrowie, możesz pytać o zdrowie", "pl", apply_options());
    CHECK(refused.error().code == Errc::Conflict);
  }

  TEST_CASE("interview remarks: the 'adhd' remark closes only the attention question") {
    auto s = chosen();
    const auto r = remark(s, "nie pytaj o ADHD", "pl", apply_options());
    CHECK(matched(r, "never.attention")); CHECK_FALSE(matched(r, "never.health"));
    CHECK(s.snapshot()["fields"]["communication.attention"]["status"] == "never");
    CHECK(s.snapshot()["fields"]["health.needs"]["status"] == "unknown");
  }
}
