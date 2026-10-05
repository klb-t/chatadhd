#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "loom/onboarding.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::unwrap;

namespace {
Json scenario_fixture() {
  auto path = std::filesystem::path(__FILE__).parent_path().parent_path() / "data/onboarding/scenario.pack";
  std::ifstream input(path);
  std::stringstream text; text << input.rdbuf();
  return unwrap(json::parse(text.str()));
}
Json rule_fixture(std::string category = "*") {
  return Json{{"id", "fixture/" + category}, {"category", category}, {"store", true}, {"infer", true},
    {"explicit_only", false}, {"providers", Json::array({"offline/provider-a"})},
    {"max_detail", nullptr}, {"max_sensitivity", nullptr}, {"retention", {{"history", "full"}, {"max_events", nullptr},
      {"metadata_keys", Json::array({"source_refs", "review_time", "presentation", "detail", "sensitivity", "candidate", "decision", "status"})}}}};
}
Json defaults_fixture() {
  return Json{{"privacy", {{"rules", Json::array({rule_fixture()})}}}, {"settings", {{"preference_mode", "ask"}}}};
}
Json action(std::string op, std::string id, std::string field_id = "") {
  Json result{{"op", op}, {"id", id}, {"time", "2000-01-01T00:00:00Z"}, {"source_refs", Json::array({"synthetic/" + id})}};
  if (!field_id.empty()) result["field"] = field_id;
  return result;
}
Json answer(std::string id, std::string field_id, Json value, std::string provenance = "user_stated") {
  Json a = action("answer", id, field_id); a["value"] = value; a["provenance"] = provenance; return a;
}
void confirm(ProfileSession& s, std::string id) {
  auto a = action("review", "review/" + id); a["candidate"] = id; a["decision"] = "confirmed";
  LOOM_REQUIRE_OK(s.dispatch(a));
}
Json saved_reply(const ProfileSession* session = nullptr) {
  // Synthetic saved adapter response; no live provider and no private corpus.
  Json reply{{"section", "identity"}, {"summary", "You work on a synthetic test project. Please confirm or correct this."},
    {"questions", Json::array({Json{{"field", "work.projects"}, {"text", "What should this test project achieve?"}}})},
    {"candidates", Json::array({Json{{"id", "model/project"}, {"field", "work.projects"}, {"value", "synthetic project"},
      {"time", "2000-01-01T00:00:00Z"}, {"source_refs", Json::array({"synthetic/archived-message-1"})}}})}};
  if (session) {
    const auto request = unwrap(session->model_request("offline/provider-a"));
    reply["provider"] = request["provider"]; reply["request_token"] = request["request_token"];
  }
  return reply;
}
}  // namespace

TEST_SUITE("onboarding") {
  TEST_CASE("new profile has explicit unknowns and executable scenario without customisation") {
    const auto scenario = scenario_fixture();
    auto session = unwrap(ProfileSession::create(scenario, defaults_fixture()));
    auto state = session.snapshot();
    CHECK(state["schema"] == "loom.onboarding.state/1");
    CHECK(state["fields"].size() == scenario["fields"].size());
    for (const auto& [id, f] : state["fields"].items()) {
      CHECK(f["status"] == "unknown"); CHECK(f["value"].is_null());
      CHECK(state["graph"]["nodes"].contains("profile/" + id));
    }
    const auto request = unwrap(session.model_request("offline/provider-a"));
    CHECK(request["section"] == "identity");
    CHECK(request["questions"].size() == 2);
    CHECK(request["prompt"] == scenario["prompt"]);
    CHECK(request["method_ref"] == scenario["method_ref"]);
    CHECK(request["context"].empty());
  }

  TEST_CASE("declined and never cannot be reasked and never cannot be inferred") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    auto decline = action("status", "decline", "identity.description"); decline["status"] = "declined";
    LOOM_REQUIRE_OK(session.dispatch(decline));
    auto never = action("status", "never", "work.projects"); never["status"] = "never";
    LOOM_REQUIRE_OK(session.dispatch(never));
    CHECK(unwrap(session.model_request("offline/provider-a"))["questions"].empty());
    auto repeat = action("repeat", "repeat"); repeat["section"] = "identity";
    LOOM_REQUIRE_OK(session.dispatch(repeat));
    CHECK(unwrap(session.model_request("offline/provider-a"))["questions"].empty());
    auto proposal = action("propose", "forbidden", "work.projects"); proposal["value"] = "guess";
    auto before = session.snapshot();
    CHECK_FALSE(session.dispatch(proposal));
    CHECK(session.snapshot() == before);
    auto reply = saved_reply(&session);
    CHECK_FALSE(session.ingest_model_reply(reply));
    CHECK(session.snapshot() == before);
    never["id"] = "reopen"; never["status"] = "unknown";
    LOOM_REQUIRE_OK(session.dispatch(never));
    CHECK(unwrap(session.model_request("offline/provider-a"))["questions"].size() == 1);
  }

  TEST_CASE("saved conversation confirms and rejects inferred candidates without claiming observations") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    auto candidate = answer("explicit", "identity.description", "synthetic tester");
    LOOM_REQUIRE_OK(session.dispatch(candidate));
    CHECK(session.snapshot()["fields"]["identity.description"]["status"] == "unknown");
    confirm(session, "explicit");
    auto state = unwrap(session.ingest_model_reply(saved_reply(&session)));
    CHECK(state["session"]["summary"] == saved_reply()["summary"]);
    CHECK(state["session"]["sections"]["identity"]["status"] == "awaiting_confirmation");
    CHECK(state["candidates"]["model/project"]["provenance"] == "model_inferred");
    CHECK(state["fields"]["work.projects"]["status"] == "unknown");
    auto finish = action("confirm_section", "finish"); finish["decision"] = "confirmed";
    CHECK_FALSE(session.dispatch(finish));
    confirm(session, "model/project");
    CHECK(session.snapshot()["fields"]["work.projects"]["provenance"] == "model_inferred");
    CHECK(session.snapshot()["fields"]["work.projects"]["review"] == "confirmed");
    LOOM_REQUIRE_OK(session.dispatch(finish));
    CHECK(session.snapshot()["session"]["section"] == "communication");
    auto reject = action("propose", "reject-me", "communication.style"); reject["value"] = "unsupported preference";
    LOOM_REQUIRE_OK(session.dispatch(reject));
    auto review = action("review", "reject-review"); review["candidate"] = "reject-me"; review["decision"] = "rejected";
    LOOM_REQUIRE_OK(session.dispatch(review));
    CHECK(session.snapshot()["candidates"]["reject-me"]["review"] == "rejected");
    CHECK(session.snapshot()["fields"]["communication.style"]["status"] == "unknown");
  }

  TEST_CASE("form and interview answers share the same candidates review path") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    LOOM_REQUIRE_OK(session.dispatch(answer("form/style", "communication.style", "clear and direct", "form")));
    confirm(session, "form/style");
    auto state = session.snapshot();
    CHECK(state["fields"]["communication.style"]["value"] == "clear and direct");
    CHECK(state["fields"]["communication.style"]["provenance"] == "form");
    auto correction = action("review", "second-review"); correction["candidate"] = "form/style"; correction["decision"] = "confirmed";
    CHECK_FALSE(session.dispatch(correction));
    LOOM_REQUIRE_OK(session.dispatch(answer("edit", "communication.style", "shorter")));
    auto review = action("review", "correct-value"); review["candidate"] = "edit"; review["decision"] = "confirmed"; review["value"] = "short, detailed when asked";
    LOOM_REQUIRE_OK(session.dispatch(review));
    CHECK(session.snapshot()["fields"]["communication.style"]["value"] == "short, detailed when asked");
    CHECK(session.snapshot()["history"].size() >= 4);
  }

  TEST_CASE("privacy filters provider context and enforces storage inference detail and sensitivity") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    LOOM_REQUIRE_OK(session.dispatch(answer("identity", "identity.description", "synthetic tester"))); confirm(session, "identity");
    CHECK(unwrap(session.model_request("offline/provider-a"))["context"].size() == 1);
    CHECK(unwrap(session.model_request("offline/provider-b"))["context"].empty());
    auto rule = rule_fixture("work"); rule["infer"] = false; rule["explicit_only"] = true;
    auto update = action("privacy", "policy"); update["rule"] = rule;
    LOOM_REQUIRE_OK(session.dispatch(update));
    auto proposal = action("propose", "infer-work", "work.projects"); proposal["value"] = "guess";
    CHECK_FALSE(session.dispatch(proposal));
    LOOM_REQUIRE_OK(session.dispatch(answer("direct-work", "work.projects", "explicitly supplied")));
    rule["max_detail"] = 0; update["rule"] = rule; update["id"] = "detail-policy";
    LOOM_REQUIRE_OK(session.dispatch(update));
    CHECK_FALSE(session.dispatch(answer("too-detailed", "work.projects", "detail exceeds policy")));
    rule["max_detail"] = nullptr; rule["max_sensitivity"] = 0; update["rule"] = rule; update["id"] = "sensitivity-policy";
    LOOM_REQUIRE_OK(session.dispatch(update));
    CHECK_FALSE(session.dispatch(answer("too-sensitive", "work.projects", "sensitivity exceeds policy")));
    rule["max_sensitivity"] = nullptr; rule["store"] = false; update["rule"] = rule; update["id"] = "storage-policy";
    LOOM_REQUIRE_OK(session.dispatch(update));
    CHECK_FALSE(session.dispatch(answer("not-storable", "work.projects", "storage denied")));
    auto bad = privacy_decision(session.snapshot(), Json{{"op", "send"}, {"category", "wrong"}, {"field", "work.projects"}});
    CHECK_FALSE(bad);
    CHECK(session.snapshot()["graph"]["nodes"]["privacy/fixture/work"]["rule"]["store"] == false);
  }

  TEST_CASE("pause resume repeated sections and skip survive recreation without changing facts") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    LOOM_REQUIRE_OK(session.dispatch(answer("saved", "identity.description", "synthetic tester"))); confirm(session, "saved");
    LOOM_REQUIRE_OK(session.ingest_model_reply(saved_reply(&session)));
    LOOM_REQUIRE_OK(session.dispatch(action("pause", "pause")));
    CHECK_FALSE(session.model_request("offline/provider-a"));
    auto restored = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture(), session.snapshot()));
    CHECK(restored.snapshot() == session.snapshot());
    LOOM_REQUIRE_OK(restored.dispatch(action("resume", "resume")));
    CHECK(unwrap(restored.model_request("offline/provider-a"))["context"].contains("identity.description"));
    CHECK(restored.snapshot()["session"]["summary"] == saved_reply()["summary"]);
    auto skip = action("skip", "skip"); skip["section"] = "identity";
    LOOM_REQUIRE_OK(restored.dispatch(skip));
    CHECK(restored.snapshot()["session"]["section"] == "communication");
    auto repeat = action("repeat", "again"); repeat["section"] = "identity";
    LOOM_REQUIRE_OK(restored.dispatch(repeat));
    CHECK(restored.snapshot()["fields"]["identity.description"]["value"] == "synthetic tester");
    CHECK_FALSE(restored.snapshot()["session"].contains("summary"));
    CHECK_FALSE(restored.snapshot()["session"].contains("latest_reply"));
    repeat["id"] = "last-part"; repeat["section"] = "privacy";
    LOOM_REQUIRE_OK(restored.dispatch(repeat));
    skip["id"] = "skip-last"; skip["section"] = "privacy";
    LOOM_REQUIRE_OK(restored.dispatch(skip));
    CHECK(restored.snapshot()["session"]["status"] == "active");
    CHECK(restored.snapshot()["session"]["section"] == "identity");
  }

  TEST_CASE("new fields migrate forward while existing user values settings and unrelated graph remain intact") {
    auto scenario = scenario_fixture();
    auto first = unwrap(ProfileSession::create(scenario, defaults_fixture()));
    LOOM_REQUIRE_OK(first.dispatch(answer("old", "identity.description", Json{{"arbitrary", Json::array({1, "two", nullptr})}}))); confirm(first, "old");
    auto existing = first.snapshot();
    existing["fields"]["identity.description"]["extension"] = Json{{"keep", "exactly"}};
    existing["unrelated_legacy_state"] = Json{{"bytes", "  keep\n  "}};
    existing["graph"]["nodes"]["legacy/node"] = Json{{"opaque", true}};
    existing["graph"]["nodes"]["profile/custom"] = Json{{"opaque", "retain unknown profile-like ID"}};
    existing["graph"]["nodes"]["privacy/custom"] = Json{{"opaque", "retain unknown privacy-like ID"}};
    const auto old_field = existing["fields"]["identity.description"];
    const auto old_history = existing["history"];
    scenario["fields"].push_back(Json{{"id", "future.preference"}, {"category", "future"}, {"section", "communication"}, {"detail", 0}, {"sensitivity", 0}});
    auto altered_defaults = defaults_fixture(); altered_defaults["settings"]["preference_mode"] = "automatic";
    auto updated = unwrap(ProfileSession::create(scenario, altered_defaults, existing)).snapshot();
    CHECK(updated["fields"]["identity.description"] == old_field);
    CHECK(updated["history"] == old_history);
    CHECK(updated["unrelated_legacy_state"] == existing["unrelated_legacy_state"]);
    CHECK(updated["graph"]["nodes"]["legacy/node"] == existing["graph"]["nodes"]["legacy/node"]);
    CHECK(updated["graph"]["nodes"]["profile/custom"] == existing["graph"]["nodes"]["profile/custom"]);
    CHECK(updated["graph"]["nodes"]["privacy/custom"] == existing["graph"]["nodes"]["privacy/custom"]);
    CHECK(updated["settings"]["preference_mode"] == "ask");
    CHECK(updated["fields"]["future.preference"]["status"] == "unknown");
    existing["schema"] = "loom.onboarding.state/99";
    CHECK_FALSE(ProfileSession::create(scenario, defaults_fixture(), existing));
    existing["schema"] = "loom.onboarding.state/1";
    existing["graph"]["nodes"]["profile/identity.description"] = Json{{"opaque", "collision must not overwrite me"}};
    const auto collision = existing;
    CHECK_FALSE(ProfileSession::create(scenario, defaults_fixture(), existing));
    CHECK(existing == collision);
  }

  TEST_CASE("history retention removes values and deletion can purge a field") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    LOOM_REQUIRE_OK(session.dispatch(answer("old", "work.projects", "synthetic secret"))); confirm(session, "old");
    auto rule = rule_fixture("work"); rule["retention"] = Json{{"history", "metadata"}, {"max_events", 2}};
    auto update = action("privacy", "policy"); update["rule"] = rule;
    LOOM_REQUIRE_OK(session.dispatch(update));
    std::size_t retained = 0;
    for (const auto& event : session.snapshot()["history"]) if (event["category"] == "work") {
      ++retained; CHECK_FALSE(event.contains("before")); CHECK_FALSE(event.contains("after")); CHECK_FALSE(event.contains("value"));
    }
    CHECK(retained <= 2);
    auto erase = action("delete", "delete", "work.projects"); erase["purge_history"] = true;
    LOOM_REQUIRE_OK(session.dispatch(erase));
    CHECK(session.snapshot()["fields"]["work.projects"]["status"] == "unknown");
    CHECK(session.snapshot()["fields"]["work.projects"]["value"].is_null());
    CHECK_FALSE(session.snapshot()["candidates"].contains("old"));
    CHECK(json::dump(session.snapshot()).find("synthetic secret") == std::string::npos);
    rule["retention"]["history"] = "none"; update["rule"] = rule; update["id"] = "none-policy";
    LOOM_REQUIRE_OK(session.dispatch(update));
    for (const auto& event : session.snapshot()["history"]) CHECK(event["category"] != "work");
  }

  TEST_CASE("ordinary proposals honor ask candidate automatic settings without claiming user confirmation") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    for (const auto mode : {"ask", "candidate", "automatic"}) {
      auto settings = action("settings", std::string("setting/") + mode); settings["settings"] = Json{{"preference_mode", mode}};
      LOOM_REQUIRE_OK(session.dispatch(settings));
      auto proposal = action("propose", std::string("proposal/") + mode, "communication.length"); proposal["value"] = mode;
      auto state = unwrap(session.dispatch(proposal));
      CHECK(state["candidates"][proposal["id"].get<std::string>()]["presentation"] == mode);
      if (std::string(mode) == "automatic") {
        CHECK(state["fields"]["communication.length"]["status"] == "known");
        CHECK(state["fields"]["communication.length"]["provenance"] == "model_inferred");
        CHECK(state["fields"]["communication.length"]["review"] == "accepted_automatically");
      } else CHECK(state["fields"]["communication.length"]["status"] == "unknown");
    }
  }

  TEST_CASE("malformed actions and replies are atomic") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    auto before = session.snapshot();
    CHECK_FALSE(session.dispatch(Json{{"op", "pause"}})); CHECK(session.snapshot() == before);
    auto malformed = action("privacy", "bad"); malformed["rule"] = Json{{"id", 2}};
    CHECK_FALSE(session.dispatch(malformed)); CHECK(session.snapshot() == before);
    auto reply = saved_reply(&session); reply["candidates"].push_back(Json{{"id", "malformed"}});
    CHECK_FALSE(session.ingest_model_reply(reply)); CHECK(session.snapshot() == before);
    CHECK_FALSE(session.ingest_model_reply(Json{{"summary", true}})); CHECK(session.snapshot() == before);
    auto malformed_settings = action("settings", "bad-setting"); malformed_settings["settings"] = Json{{"preference_mode", "invented"}};
    CHECK_FALSE(session.dispatch(malformed_settings)); CHECK(session.snapshot() == before);
  }

  TEST_CASE("late responses cannot restore a deleted or never field even with no structured candidates") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    auto reply = saved_reply(&session); reply["questions"] = Json::array(); reply["candidates"] = Json::array();
    reply["summary"] = "synthetic forgotten cache value";
    auto erase = action("delete", "forget", "work.projects"); erase["purge_history"] = true;
    LOOM_REQUIRE_OK(session.dispatch(erase));
    const auto after = session.snapshot();
    CHECK_FALSE(session.ingest_model_reply(reply)); CHECK(session.snapshot() == after);
    auto uncorrelated = saved_reply(); CHECK_FALSE(session.ingest_model_reply(uncorrelated));
    auto current_reply = saved_reply(&session); current_reply["questions"] = Json::array(); current_reply["candidates"] = Json::array();
    current_reply["summary"] = "synthetic forgotten cache value";
    LOOM_REQUIRE_OK(session.ingest_model_reply(current_reply));
    CHECK(json::dump(session.snapshot()).find("synthetic forgotten cache value") != std::string::npos);
    auto never = action("status", "never-cache", "work.projects"); never["status"] = "never"; never["purge_history"] = true;
    LOOM_REQUIRE_OK(session.dispatch(never));
    CHECK(json::dump(session.snapshot()).find("synthetic forgotten cache value") == std::string::npos);
  }

  TEST_CASE("tightened history retention removes existing summary caches and does not persist new prose") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    auto reply = saved_reply(&session); reply["questions"] = Json::array(); reply["candidates"] = Json::array(); reply["summary"] = "synthetic confidential summary";
    LOOM_REQUIRE_OK(session.ingest_model_reply(reply));
    CHECK(json::dump(session.snapshot()).find("synthetic confidential summary") != std::string::npos);
    auto update = action("privacy", "cache-retention"); auto rule = rule_fixture("work"); rule["retention"]["history"] = "metadata"; update["rule"] = rule;
    LOOM_REQUIRE_OK(session.dispatch(update));
    CHECK(json::dump(session.snapshot()).find("synthetic confidential summary") == std::string::npos);
    reply = saved_reply(&session); reply["questions"] = Json::array(); reply["candidates"] = Json::array(); reply["summary"] = "new synthetic confidential summary";
    LOOM_REQUIRE_OK(session.ingest_model_reply(reply));
    CHECK(json::dump(session.snapshot()).find("new synthetic confidential summary") == std::string::npos);
    CHECK(session.snapshot()["session"]["sections"]["identity"]["summary_redacted"] == true);
  }

  TEST_CASE("reviewed candidate values obey retention and full-history purge catches review events") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    LOOM_REQUIRE_OK(session.dispatch(answer("earlier", "work.projects", "synthetic obsolete preference"))); confirm(session, "earlier");
    LOOM_REQUIRE_OK(session.dispatch(answer("latest", "work.projects", "current preference"))); confirm(session, "latest");
    auto update = action("privacy", "metadata"); auto rule = rule_fixture("work"); rule["retention"]["history"] = "metadata"; update["rule"] = rule;
    LOOM_REQUIRE_OK(session.dispatch(update));
    CHECK_FALSE(session.snapshot()["candidates"]["earlier"].contains("value"));
    CHECK(session.snapshot()["fields"]["work.projects"]["value"] == "current preference");
    CHECK(json::dump(session.snapshot()).find("synthetic obsolete preference") == std::string::npos);
    rule["retention"]["history"] = "full"; rule["retention"]["max_events"] = 0; update["rule"] = rule; update["id"] = "zero-retained";
    LOOM_REQUIRE_OK(session.dispatch(update));
    CHECK_FALSE(session.snapshot()["candidates"]["latest"].contains("value"));
    auto fresh = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    LOOM_REQUIRE_OK(fresh.dispatch(answer("full", "work.projects", "synthetic full-history purge target"))); confirm(fresh, "full");
    auto erase = action("delete", "erase-full", "work.projects"); erase["purge_history"] = true;
    LOOM_REQUIRE_OK(fresh.dispatch(erase));
    CHECK(json::dump(fresh.snapshot()).find("synthetic full-history purge target") == std::string::npos);
  }

  TEST_CASE("summary retention follows actual contributors and zero history keeps no prose copies") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    auto privacy = rule_fixture("privacy"); privacy["store"] = false;
    auto update = action("privacy", "unrelated-policy"); update["rule"] = privacy;
    LOOM_REQUIRE_OK(session.dispatch(update));
    auto reply = saved_reply(&session); reply["questions"] = Json::array(); reply["candidates"] = Json::array(); reply["summary"] = "allowed identity section summary";
    LOOM_REQUIRE_OK(session.ingest_model_reply(reply));
    CHECK(session.snapshot()["session"]["summary"] == "allowed identity section summary");
    update["id"] = "unrelated-tightening"; privacy["retention"]["history"] = "none"; update["rule"] = privacy;
    LOOM_REQUIRE_OK(session.dispatch(update));
    CHECK(session.snapshot()["session"]["summary"] == "allowed identity section summary");
    auto work = rule_fixture("work"); work["retention"]["max_events"] = 0; update["id"] = "zero-work-history"; update["rule"] = work;
    LOOM_REQUIRE_OK(session.dispatch(update));
    CHECK_FALSE(session.snapshot()["session"].contains("summary"));
    CHECK_FALSE(session.snapshot()["session"].contains("latest_reply"));
    reply = saved_reply(&session); reply["questions"] = Json::array(); reply["candidates"] = Json::array(); reply["summary"] = "zero-history prose must stay transient";
    LOOM_REQUIRE_OK(session.ingest_model_reply(reply));
    CHECK(json::dump(session.snapshot()).find("zero-history prose must stay transient") == std::string::npos);
  }

  TEST_CASE("expert scenarios are checked before installing unusable references or negative classifications") {
    auto scenario = scenario_fixture(); scenario["sections"][0]["questions"][0]["field"] = "typo";
    CHECK_FALSE(ProfileSession::create(scenario, defaults_fixture()));
    scenario = scenario_fixture(); scenario["sections"].push_back(scenario["sections"][0]);
    CHECK_FALSE(ProfileSession::create(scenario, defaults_fixture()));
    scenario = scenario_fixture(); scenario["fields"].push_back(scenario["fields"][0]);
    CHECK_FALSE(ProfileSession::create(scenario, defaults_fixture()));
    scenario = scenario_fixture(); scenario["fields"][0]["section"] = "missing";
    CHECK_FALSE(ProfileSession::create(scenario, defaults_fixture()));
    scenario = scenario_fixture(); scenario["fields"][0]["sensitivity"] = -1;
    CHECK_FALSE(ProfileSession::create(scenario, defaults_fixture()));
    scenario = scenario_fixture(); scenario["sections"][0]["questions"][0]["text"] = false;
    CHECK_FALSE(ProfileSession::create(scenario, defaults_fixture()));
  }

  TEST_CASE("inference may fill a declined field without reopening its questions") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    auto decline = action("status", "decline-work", "work.projects"); decline["status"] = "declined";
    LOOM_REQUIRE_OK(session.dispatch(decline));
    auto settings = action("settings", "auto"); settings["settings"] = Json{{"preference_mode", "automatic"}};
    LOOM_REQUIRE_OK(session.dispatch(settings));
    auto proposal = action("propose", "derived-work", "work.projects"); proposal["value"] = "synthetic inferred work";
    LOOM_REQUIRE_OK(session.dispatch(proposal));
    CHECK(session.snapshot()["fields"]["work.projects"]["status"] == "known");
    CHECK(session.snapshot()["fields"]["work.projects"]["question_disposition"] == "declined");
    const auto request = unwrap(session.model_request("offline/provider-a"));
    for (const auto& question : request["questions"]) CHECK(question["field"] != "work.projects");
    decline["id"] = "explicit-reopen"; decline["status"] = "unknown";
    LOOM_REQUIRE_OK(session.dispatch(decline));
    CHECK(unwrap(session.model_request("offline/provider-a"))["questions"].size() == 2);
  }

  TEST_CASE("pending candidate own sensitivity is checked when privacy tightens before sending") {
    auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults_fixture()));
    auto proposal = action("propose", "sensitive", "work.projects"); proposal["value"] = "synthetic sensitive candidate"; proposal["sensitivity"] = 5;
    LOOM_REQUIRE_OK(session.dispatch(proposal));
    CHECK(unwrap(session.model_request("offline/provider-a"))["candidates"].size() == 1);
    auto update = action("privacy", "tighten"); auto rule = rule_fixture("work"); rule["max_sensitivity"] = 2; update["rule"] = rule;
    LOOM_REQUIRE_OK(session.dispatch(update));
    CHECK(unwrap(session.model_request("offline/provider-a"))["candidates"].empty());
    auto review = action("review", "cannot-review"); review["candidate"] = "sensitive"; review["decision"] = "confirmed";
    CHECK_FALSE(session.dispatch(review));
  }

  TEST_CASE("opaque proposal payload does not bypass metadata none or zero-event retention") {
    for (const auto mode : {"metadata", "none", "full"}) {
      auto defaults = defaults_fixture();
      defaults["privacy"]["rules"][0]["retention"]["history"] = mode;
      if (std::string(mode) == "full") defaults["privacy"]["rules"][0]["retention"]["max_events"] = 0;
      auto session = unwrap(ProfileSession::create(scenario_fixture(), defaults));
      auto proposal = action("propose", "extra-payload", "work.projects"); proposal["value"] = "current allowed value";
      proposal["explanation"] = Json{{"nested", "synthetic extra payload must be forgotten"}};
      proposal["arbitrary_future_payload"] = Json::array({"synthetic extra payload must be forgotten"});
      LOOM_REQUIRE_OK(session.dispatch(proposal));
      CHECK(json::dump(session.snapshot()).find("synthetic extra payload must be forgotten") != std::string::npos);
      confirm(session, "extra-payload");
      CHECK(session.snapshot()["fields"]["work.projects"]["value"] == "current allowed value");
      CHECK(json::dump(session.snapshot()).find("synthetic extra payload must be forgotten") == std::string::npos);
      CHECK(session.snapshot()["candidates"]["extra-payload"]["review"] == "confirmed");
      CHECK(session.snapshot()["candidates"]["extra-payload"]["provenance"] == "model_inferred");
    }
  }
}
