// M1-K2b-1: the interview of the person ("Ty") reachable through the actions (profile.interview, profile.show, profile.set, profile.confirm), over the
// durable store, in FORM MODE: no model is called and no network is touched (the scripted transport must see no request). Synthetic data only.
#include <doctest/doctest.h>

#include <filesystem>
#include <set>

#include "loom/action_runner.h"
#include "loom/loom.h"
#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Fx {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  Fx() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
  }
  // One action through the runner (run_gated): the run record.
  Json call(const std::string& action, Json request = Json::object()) {
    if (!request.contains("locale")) request["locale"] = "pl";
    return unwrap(rt->actions().invoke(action, request));
  }
  // An action that waits for the person: asks first (awaiting_confirmation), nothing is written until the yes.
  Json ask(const std::string& action, Json request) {
    const Json waiting = call(action, request);
    INFO(waiting.dump());
    REQUIRE(waiting.at("status") == "awaiting_confirmation");
    return waiting;
  }
  Json yes(const Json& waiting) {
    return unwrap(rt->actions().confirm(Json{{"run_id", waiting.at("id")}, {"approved", true}, {"receipt_id", waiting.at("pending").at("receipt_id")}, {"ref", "test"}}));
  }
  Json confirmed(const std::string& action, Json request) { return yes(ask(action, std::move(request))); }
  static Json data_of(const Json& run) {
    INFO(run.dump());
    REQUIRE(run.at("status") == "completed");
    return run.at("result").value("data", Json::object());
  }
  static std::string text_of(const Json& run) {
    std::string out;
    const Json parts = run.at("result").value("parts", Json::array());
    for (const auto& p : parts) {
      if (p.is_string()) out += p.get<std::string>() + "\n";
      else if (p.is_object() && p.contains("text") && p.at("text").is_string()) out += p.at("text").get<std::string>() + "\n";
    }
    return out;
  }
  static std::string failure_of(const Json& run) {
    if (run.contains("error") && !run.at("error").is_null()) return run.at("error").dump();
    return run.dump();
  }
  Json go(const std::string& op, Json more = Json::object()) {
    more["op"] = op;
    return call("profile.interview", std::move(more));
  }
  Json started() { return data_of(go("start")); }
  Json shown() { return data_of(call("profile.show")); }
  static const Json& slot(const Json& show, const std::string& id) {
    for (const auto& s : show.at("slots"))
      if (s.at("slot") == id) return s;
    FAIL("no slot " << id);
    static const Json none;
    return none;
  }
  void level(const std::string& word) {
    started();
    const Json a = data_of(go("answer", Json{{"slot", "interview.detail"}, {"text", word}}));
    REQUIRE(a.at("written") == true);
  }
  void no_network() { CHECK(transport->requests().empty()); }
};

}  // namespace

TEST_SUITE("profile_ops") {
  TEST_CASE("start opens the profile and asks the level question with the three level previews, in the requested language") {
    Fx f;
    const Json run = f.go("start");
    const Json d = Fx::data_of(run);
    CHECK(d.at("mode") == "form");
    CHECK(d.at("level_choice_pending") == true);
    REQUIRE(d.at("question").at("is_level_question") == true);
    std::set<std::string> ids;
    for (const auto& l : d.at("question").at("levels")) ids.insert(l.at("id").get<std::string>());
    CHECK(ids == std::set<std::string>{"minimal", "normal", "detailed"});
    CHECK(Fx::text_of(run).find("formularz") != std::string::npos);  // it says it is a form, without a model
    // English on request
    const Json en = Fx::data_of(f.call("profile.interview", Json{{"op", "start"}, {"locale", "en"}}));
    CHECK(en.at("question").at("text") != d.at("question").at("text"));
    f.no_network();
  }

  TEST_CASE("the level answer is stored through the setting and the next question is one of the two minimal fields") {
    Fx f;
    f.started();
    const Json run = f.go("answer", Json{{"slot", "interview.detail"}, {"text", "minimalnie"}});
    const Json d = Fx::data_of(run);
    CHECK(d.at("level_chosen") == "minimal");
    CHECK(f.rt->config().get("profile_detail_level") == "minimal");  // the setting, not a constant of the code
    CHECK(d.at("remaining") == 2);
    const std::string next = d.at("question").at("field");
    CHECK((next == "communication.style" || next == "communication.language"));
    // a level that is not one of them is not guessed
    Fx g;
    g.started();
    const Json bad = g.go("answer", Json{{"slot", "interview.detail"}, {"text", "jakoś"}});
    CHECK(bad.at("status") != "completed");
    CHECK(Fx::failure_of(bad).find("Nie rozpoznałem") != std::string::npos);
    CHECK(g.rt->config().get("profile_detail_level") == "normal");
    f.no_network();
  }

  TEST_CASE("a remark is only proposed; nothing is written until the person confirms; then the category is never and the plan no longer lists it") {
    Fx f;
    f.level("szczegółowo");
    const Json before = f.shown();
    CHECK(Fx::slot(before, "health.needs").at("asked_at_level") == true);
    const Json asked = Fx::data_of(f.go("answer", Json{{"slot", "communication.style"}, {"text", "nie pytaj o zdrowie"}}));
    CHECK(asked.at("written") == false);
    REQUIRE(asked.at("proposal").is_object());
    CHECK(asked.at("proposal").at("action") == "profile.confirm");
    CHECK(asked.at("proposal").at("gate") == "confirm:profile.confirm");
    CHECK(f.shown().at("slots") == before.at("slots"));  // nothing changed
    // the confirmation gate asks first, and until the yes nothing is written
    const Json waiting = f.ask("profile.confirm", asked.at("proposal").at("with"));
    CHECK(f.shown().at("slots") == before.at("slots"));
    const Json done = Fx::data_of(f.yes(waiting));
    CHECK(done.at("written") == true);
    const Json after = f.shown();
    CHECK(Fx::slot(after, "health.needs").at("state") == "never");
    CHECK(Fx::slot(after, "communication.attention").at("state") == "never");  // attention is health in the person's sense (K2a decision 6)
    CHECK(Fx::slot(after, "health.needs").at("asked_at_level") == false);
    CHECK(Fx::slot(after, "health.needs").at("reason") == "never");
    CHECK(Fx::slot(after, "health.needs").contains("value") == false);
    f.no_network();
  }

  TEST_CASE("declined and never survive a second interview, and a never is only reopened by the person") {
    Fx f;
    f.level("detailed");
    f.go("decline", Json{{"slot", "knowledge.avoid"}});
    f.go("never", Json{{"category", "routine"}});
    const Json again = Fx::data_of(f.go("start"));
    CHECK(again.at("level_choice_pending") == false);
    const Json show = f.shown();
    CHECK(Fx::slot(show, "knowledge.avoid").at("state") == "declined");
    CHECK(Fx::slot(show, "routine.rhythm").at("state") == "never");
    CHECK(Fx::slot(show, "knowledge.avoid").at("asked_at_level") == false);
    // answering a never slot is refused with the way to reopen it
    const Json refused = f.go("answer", Json{{"slot", "routine.rhythm"}, {"text", "wstaję o szóstej"}});
    CHECK(refused.at("status") != "completed");
    CHECK(Fx::failure_of(refused).find("reopen") != std::string::npos);
    // the person reopens it himself
    const Json reopened = Fx::data_of(f.go("never", Json{{"category", "routine"}, {"reopen", true}}));
    CHECK(reopened.at("written") == true);
    CHECK(Fx::slot(f.shown(), "routine.rhythm").at("state") == "unknown");
    f.no_network();
  }

  TEST_CASE("show never leaks a value of a never or unknown slot, and a slot above the chosen level says above_level") {
    Fx f;
    f.level("normalnie");
    const std::string secret = "sekret-xyz-123";
    // an answer waits for the yes of the section card, then the section is confirmed through the gate
    f.go("answer", Json{{"slot", "work.projects"}, {"text", secret}});
    const Json review = Fx::data_of(f.go("review"));
    REQUIRE(review.at("proposal").is_object());
    CHECK(review.at("proposal").at("with").at("kind") == "section");
    const Json before_yes = f.call("profile.show");
    CHECK(Fx::slot(Fx::data_of(before_yes), "work.projects").at("state") == "unknown");  // an answer not yet confirmed is not "known"
    CHECK(Fx::data_of(before_yes).dump().find(secret) == std::string::npos);
    CHECK(Fx::text_of(before_yes).find(secret) == std::string::npos);
    f.confirmed("profile.confirm", review.at("proposal").at("with"));
    const Json known = f.shown();
    CHECK(Fx::slot(known, "work.projects").at("state") == "known");
    CHECK(Fx::slot(known, "work.projects").at("value") == secret);
    CHECK(Fx::slot(known, "work.projects").at("provenance") == "user_stated");
    // above the level: the honest reason, and the old field names stay
    const Json up = Fx::slot(known, "health.needs");
    CHECK(up.at("state") == "unknown");
    CHECK(up.at("asked_at_level") == false);
    CHECK(up.at("reason") == "above_level");
    CHECK(up.contains("value") == false);
    // never: erasing a stored answer is not done behind the person's back
    const Json guard = Fx::data_of(f.go("never", Json{{"slot", "work.projects"}}));
    CHECK(guard.at("written") == false);
    CHECK(guard.at("would_erase").size() == 1);
    CHECK(Fx::slot(f.shown(), "work.projects").at("state") == "known");
    const Json erased = Fx::data_of(f.go("never", Json{{"slot", "work.projects"}, {"erase", true}}));
    CHECK(erased.at("written") == true);
    const Json run = f.call("profile.show");
    CHECK(Fx::slot(Fx::data_of(run), "work.projects").at("state") == "never");
    CHECK(Fx::data_of(run).dump().find(secret) == std::string::npos);
    CHECK(Fx::text_of(run).find(secret) == std::string::npos);
    f.no_network();
  }

  TEST_CASE("profile.set asks first, writes as user_stated, and refuses a never slot") {
    Fx f;
    f.level("normal");
    const Json req{{"slot", "work.projects"}, {"value", "klub szachowy"}};
    const Json waiting = f.ask("profile.set", req);
    CHECK(Fx::slot(f.shown(), "work.projects").at("state") == "unknown");  // asked, not yet written
    const Json done = Fx::data_of(f.yes(waiting));
    CHECK(done.at("provenance") == "user_stated");
    const Json show = f.shown();
    CHECK(Fx::slot(show, "work.projects").at("value") == "klub szachowy");
    CHECK(Fx::slot(show, "work.projects").at("provenance") == "user_stated");
    // never: refused, with the hint
    f.go("never", Json{{"slot", "knowledge.remember"}});
    const Json refused = f.yes(f.ask("profile.set", Json{{"slot", "knowledge.remember"}, {"value", "cokolwiek"}}));
    CHECK(refused.at("status") != "completed");
    CHECK(Fx::failure_of(refused).find("reopen") != std::string::npos);
    CHECK(Fx::slot(f.shown(), "knowledge.remember").at("state") == "never");
    f.no_network();
  }

  TEST_CASE("a revision conflict is reported, not swallowed") {
    Fx f;
    const Json start = f.started();
    const std::string old_revision = start.at("revision");
    f.go("answer", Json{{"slot", "interview.detail"}, {"text", "normal"}});  // the revision moves on
    const Json stale = f.go("answer", Json{{"slot", "work.projects"}, {"text", "x"}, {"expected_revision", old_revision}});
    CHECK(stale.at("status") != "completed");
    CHECK(Fx::failure_of(stale).find("Profil zmienił się") != std::string::npos);
    CHECK(Fx::slot(f.shown(), "work.projects").at("state") == "unknown");
    // a number that is not a revision is refused too
    CHECK(f.go("status", Json{{"expected_revision", "abc"}}).at("status") != "completed");
    f.no_network();
  }

  TEST_CASE("skip holds for the session only, status counts, and the model path is refused with the reason") {
    Fx f;
    f.level("normal");
    const Json skipped = Fx::data_of(f.go("skip"));
    CHECK(skipped.at("written") == true);
    const Json status = Fx::data_of(f.go("status"));
    CHECK(status.at("skipped_sections").size() == 1);
    const Json back = Fx::data_of(f.go("start"));  // a new start brings it back
    CHECK(back.at("sections_back").size() == 1);
    CHECK(Fx::data_of(f.go("status")).at("skipped_sections").empty());
    const Json model = f.go("start", Json{{"mode", "model"}});
    CHECK(model.at("status") != "completed");
    CHECK(Fx::failure_of(model).find("bez modelu") != std::string::npos);
    f.no_network();
  }

  TEST_CASE("through loom_invoke (the C ABI): the same JSON shape, and an unknown operation is an error, not a crash") {
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
    const Json start = call("profile.interview", Json{{"op", "start"}, {"locale", "pl"}});
    REQUIRE(start.at("status") == "completed");
    CHECK(start.at("result").at("data").at("question").at("is_level_question") == true);
    const Json show = call("profile.show", Json{{"locale", "pl"}});
    CHECK(show.at("status") == "completed");
    CHECK(show.at("result").at("data").at("slots").size() >= 13);
    const Json unknown = call("profile.interview", Json{{"op", "dance"}});
    CHECK(unknown.at("status") != "completed");
    loom_shutdown(ctx);
  }
}
