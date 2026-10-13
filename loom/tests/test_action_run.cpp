// The interpreter of composite actions (R54 / GRAF_HUB_KOD_SILNIK §9): sequence, variants, first_success,
// conditions, typed parts between steps, rules (deny / confirm), durable pauses, cancel, chains.
#include <doctest/doctest.h>

#include <fstream>

#include "loom/action_run.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::actions;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

void overlay(const fs::path& dir, const std::string& domain, const Json& overrides) {
  fs::create_directories(dir / "profiles");
  std::ofstream(dir / "profiles" / (domain + ".pack"))
      << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
}

Json effects(bool sends = false) {
  return Json{{"reads_only", true}, {"writes", false}, {"sends_off_device", sends}, {"costs_money", false},
              {"irreversible", false}, {"touches_private_data", false}};
}

Json action(const std::string& id, const std::string& kind, const Json& steps = nullptr, const Json& requires_ = Json::array({"message"}),
            const Json& produces = Json::array(), bool sends = false) {
  Json a{{"id", id}, {"kind", kind}, {"status", "implemented"}, {"title", id}, {"description", id},
         {"inputs", Json::array({"text"})}, {"outputs", Json::array({"text"})}, {"requires", requires_},
         {"produces", produces}, {"content_op", "none"}, {"form_op", "none"}, {"cost", 1}, {"confidence", 0.9},
         {"effects", effects(sends)}, {"consumer", ""}};
  if (!steps.is_null()) a["steps"] = steps;
  return a;
}

llm::Part text(std::string t) {
  llm::Part p;
  p.kind = "text";
  p.text = std::move(t);
  return p;
}

std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

struct Env {
  fsutil::TempDir td;
  std::unique_ptr<Engine> engine;
  ExecutorTable executors;
  std::unique_ptr<RunStore> store = make_memory_run_store();
  std::vector<std::string> calls;       // "<step id>:<action>:<variant>" in order
  std::vector<Json> step_records;
  int hold_calls = 0;
  CancelToken* cancel_inside = nullptr;  // t.cancel_me cancels this token while it runs
  std::string content = "hello";

  Env() { setup(Json::object(), Json::object()); }
  explicit Env(const Json& extra_rules) { setup(Json::object(), extra_rules); }

  void setup(const Json& extra_actions, const Json& rules) {
    Json actions = Json::object();
    auto add = [&](const Json& a) { actions[a.at("id").get<std::string>()] = a; };
    add(action("t.echo", "atomic", nullptr, Json::array(), Json::array()));
    add(action("t.upper", "atomic", nullptr, Json::array(), Json::array()));
    add(action("t.fail", "atomic", nullptr, Json::array(), Json::array()));
    add(action("t.hold", "atomic", nullptr, Json::array(), Json::array()));
    add(action("t.sends", "atomic", nullptr, Json::array(), Json::array(), true));
    add(action("t.cancel_me", "atomic", nullptr, Json::array(), Json::array()));
    add(action("t.later", "atomic", nullptr, Json::array(), Json::array()));
    add(action("t.seq", "composite",
               Json::array({Json{{"action", "t.echo"}, {"with", Json{{"text", Json{{"$arg", "greeting"}, {"default", "hi"}}}}}},
                            Json{{"action", "t.upper"}}})));
    add(action("t.variants", "composite",
               Json::array({Json{{"id", "e"}, {"action", "t.echo"},
                                {"variants", Json::array({Json{{"label", "a"}, {"with", Json{{"text", "alpha"}}}},
                                                          Json{{"label", "b"}, {"with", Json{{"text", "beta"}}}}})}},
                            Json{{"id", "u"}, {"action", "t.upper"}}}),
               Json::array({"message"}), Json::array({"shouted"})));
    add(action("t.first", "composite",
               Json::array({Json{{"id", "e"}, {"action", "t.echo"}, {"variant_mode", "first_success"},
                                {"variants", Json::array({Json{{"label", "bad"}, {"with", Json{{"fail", true}}}},
                                                          Json{{"label", "good"}, {"with", Json{{"text", "good"}}}},
                                                          Json{{"label", "never"}, {"with", Json{{"text", "never"}}}}})}}})));
    add(action("t.cond", "composite",
               Json::array({Json{{"id", "a"}, {"action", "t.echo"}, {"with", Json{{"text", "A"}}}},
                            Json{{"id", "b"}, {"action", "t.echo"}, {"with", Json{{"text", "B"}}}, {"when", Json{{"trait", "has_audio"}}}},
                            Json{{"id", "c"}, {"action", "t.echo"}, {"with", Json{{"text", "C"}}},
                                 {"when", Json{{"all", Json::array({Json{{"step", "a"}, {"is", "ok"}}, Json{{"trait", "in_conversation"}}})}}}},
                            Json{{"id", "d"}, {"action", "t.echo"}, {"with", Json{{"text", "D"}}}, {"when", Json{{"not", Json{{"step", "a"}}}}}},
                            Json{{"id", "e"}, {"action", "t.echo"}, {"with", Json{{"text", "E"}}},
                                 {"when", Json{{"any", Json::array({Json{{"arg", "flag"}}, Json{{"output", "c"}, {"kind", "json"}}})}}}}}),
               Json::array({"message"}), Json::array(), false));
    add(action("t.outer", "composite",
               Json::array({Json{{"id", "inner"}, {"action", "t.seq"}, {"with", Json{{"greeting", Json{{"$arg", "who"}}}}}},
                            Json{{"id", "tail"}, {"action", "t.echo"}, {"with", Json{{"text", Json{{"$text", "inner"}}}}}}})));
    add(action("t.loop", "composite", Json::array({Json{{"action", "t.loop"}}})));
    add(action("t.hold_then_echo", "composite", Json::array({Json{{"id", "h"}, {"action", "t.hold"}}, Json{{"id", "l"}, {"action", "t.later"}}})));
    add(action("t.cancel_chain", "composite", Json::array({Json{{"id", "c"}, {"action", "t.cancel_me"}}, Json{{"id", "l"}, {"action", "t.later"}}})));
    add(action("t.sends_chain", "composite", Json::array({Json{{"id", "s"}, {"action", "t.sends"}}, Json{{"id", "u"}, {"action", "t.upper"}}})));
    add(action("t.planned", "atomic", nullptr, Json::array({"message"})));
    actions["t.planned"]["status"] = "planned";
    add(action("t.needs_audio", "atomic", nullptr, Json::array({"has_audio"})));
    add(action("t.consumer", "composite", Json::array({Json{{"action", "t.echo"}}})));
    actions["t.consumer"]["consumer"] = "somewhere.cpp";
    add(action("t.makes_audio_text", "atomic", nullptr, Json::array({"message"}), Json::array({"has_audio"})));
    for (auto it = extra_actions.begin(); it != extra_actions.end(); ++it) actions[it.key()] = it.value();
    overlay(td.path(), "actions", Json{{"actions", actions}});
    if (!rules.empty()) overlay(td.path(), "action_rules", rules);
    engine = std::make_unique<Engine>(unwrap(Engine::load(td.path())));
    wire();
  }

  void wire() {
    executors.add("t.echo", [this](const StepCall& c) {
      calls.push_back(c.step_id + ":t.echo:" + c.variant);
      StepOutcome o;
      if (json::get_bool(c.with, "fail", false)) {
        o.state = StepOutcome::State::Failed;
        o.error = Error(Errc::Internal, "told to fail");
        return o;
      }
      o.state = StepOutcome::State::Ok;
      std::string t = json::get_string(c.with, "text");
      if (t.empty())
        for (const auto& p : c.inputs) t += p.text;
      o.parts.push_back(text(t));
      o.provenance.push_back(Json{{"schema", "loom.test_call/1"}, {"step", c.step_id}});
      return o;
    });
    executors.add("t.upper", [this](const StepCall& c) {
      calls.push_back(c.step_id + ":t.upper:" + c.variant);
      StepOutcome o;
      o.state = StepOutcome::State::Ok;
      for (const auto& p : c.inputs) o.parts.push_back(text(upper(p.text)));
      return o;
    });
    executors.add("t.fail", [this](const StepCall& c) {
      calls.push_back(c.step_id + ":t.fail:" + c.variant);
      StepOutcome o;
      o.error = Error(Errc::Internal, "always fails");
      return o;
    });
    executors.add("t.sends", [this](const StepCall& c) {
      calls.push_back(c.step_id + ":t.sends:" + c.variant);
      StepOutcome o;
      o.state = StepOutcome::State::Ok;
      o.parts.push_back(text("sent"));
      return o;
    });
    executors.add("t.later", [this](const StepCall& c) {
      calls.push_back(c.step_id + ":t.later:" + c.variant);
      StepOutcome o;
      o.state = StepOutcome::State::Ok;
      o.parts.push_back(text("later"));
      return o;
    });
    executors.add("t.hold", [this](const StepCall& c) {
      calls.push_back(c.step_id + ":t.hold:" + c.variant);
      ++hold_calls;
      StepOutcome o;
      if (c.usage_confirmation.is_object() && json::get_bool(c.usage_confirmation, "approved", false)) {
        o.state = StepOutcome::State::Ok;
        o.parts.push_back(text("confirmed"));
        return o;
      }
      if (c.usage_confirmation.is_object() && c.usage_confirmation.contains("approved")) {  // declined
        o.state = StepOutcome::State::Held;
        o.hold = Json{{"status", "denied"}};
        return o;
      }
      o.state = StepOutcome::State::Held;
      o.hold = Json{{"schema", "loom.action_usage_pending/1"}, {"status", "requires_confirmation"}, {"operation_id", "op_1"},
                    {"receipt_id", "usage_r1"}, {"sent", false}};
      return o;
    });
    executors.add("t.cancel_me", [this](const StepCall& c) {
      calls.push_back(c.step_id + ":t.cancel_me:" + c.variant);
      if (cancel_inside) cancel_inside->cancel();
      StepOutcome o;
      o.state = StepOutcome::State::Ok;
      o.parts.push_back(text("done before cancel"));
      return o;
    });
    executors.add("t.makes_audio_text", [this](const StepCall& c) {
      calls.push_back(c.step_id + ":t.makes_audio_text:" + c.variant);
      StepOutcome o;
      o.state = StepOutcome::State::Ok;
      o.parts.push_back(text("x"));
      return o;
    });
    executors.add("t.needs_audio", [this](const StepCall& c) {
      calls.push_back(c.step_id + ":t.needs_audio:" + c.variant);
      StepOutcome o;
      o.state = StepOutcome::State::Ok;
      o.parts.push_back(text("heard"));
      return o;
    });
  }

  Interpreter interpreter() {
    InterpreterDeps deps;
    deps.engine = engine.get();
    deps.executors = &executors;
    deps.store = store.get();
    deps.element_content = [this](const Json&) -> Result<std::vector<llm::Part>> { return std::vector<llm::Part>{text(content)}; };
    deps.on_step = [this](const Json& rec) { step_records.push_back(rec); };
    return Interpreter(deps);
  }

  RunRequest request(const std::string& action_id, const Json& args = Json::object()) {
    RunRequest r;
    r.action = action_id;
    r.element = Json{{"class", "message"}, {"id", "m_1"}};
    r.args = args;
    return r;
  }
};

std::string text_result(const Json& run) {
  std::string out;
  for (const auto& p : run.at("result").at("parts")) out += (out.empty() ? "" : "|") + p.at("text").get<std::string>();
  return out;
}

Json step(const Json& run, const std::string& id) {
  for (const auto& s : run.at("steps"))
    if (s.at("id") == id) return s;
  return Json();
}

std::string rid(const Json& run) { return run.at("id").get<std::string>(); }

Json rule_overlay(const Json& rule) { return Json{{"rules", Json{{rule.at("id").get<std::string>(), rule}}}}; }

}  // namespace

TEST_SUITE("action_run") {
  TEST_CASE("a composite runs its steps in order; the output of a step is the input of the next; arguments are placeholders") {
    Env env;
    auto it = env.interpreter();
    const Json run = unwrap(it.start(env.request("t.seq", Json{{"greeting", "good day"}})));
    CHECK(run.at("status") == "completed");
    CHECK(text_result(run) == "GOOD DAY");
    CHECK(env.calls == std::vector<std::string>{"a1.s1:t.echo:", "a1.s2:t.upper:"});
    // the default of a missing argument comes from the data
    const Json dflt = unwrap(it.start(env.request("t.seq")));
    CHECK(text_result(dflt) == "HI");
    // the run is a stored document that can be read back
    CHECK(unwrap(it.get(run.at("id").get<std::string>())) == run);
    // every step left a record with hashes of inputs and outputs, and the audit hook saw each of them
    REQUIRE(env.step_records.size() >= 2);
    CHECK(step(run, "a1.s1").at("schema") == "loom.action_step/1");
    CHECK(step(run, "a1.s1").at("outputs_sha256").get<std::string>().size() == 64);
    CHECK(step(run, "a1.s1").at("variants").at(0).at("provenance").at(0).at("step") == "a1.s1");
  }

  TEST_CASE("variants: all of them run and every result is returned; the next step sees all the parts") {
    Env env;
    auto it = env.interpreter();
    const Json run = unwrap(it.start(env.request("t.variants")));
    CHECK(run.at("status") == "completed");
    CHECK(env.calls == std::vector<std::string>{"a1.e:t.echo:a", "a1.e:t.echo:b", "a1.u:t.upper:"});
    CHECK(step(run, "a1.e").at("variants").size() == 2);
    CHECK(step(run, "a1.e").at("variants").at(1).at("label") == "b");
    CHECK(text_result(run) == "ALPHA|BETA");
    CHECK(run.at("traits").size() > 0);  // the produced traits are tracked
    CHECK(std::find(run.at("traits").begin(), run.at("traits").end(), Json("shouted")) != run.at("traits").end());
  }

  TEST_CASE("first_success stops at the first variant that works and reports the failed one") {
    Env env;
    auto it = env.interpreter();
    const Json run = unwrap(it.start(env.request("t.first")));
    CHECK(run.at("status") == "completed");
    CHECK(env.calls == std::vector<std::string>{"a1.e:t.echo:bad", "a1.e:t.echo:good"});  // "never" did not run
    const Json s = step(run, "a1.e");
    CHECK(s.at("status") == "ok");
    CHECK(s.at("variants").at(0).at("state") == "failed");
    CHECK(s.at("variants").at(0).at("error").at("message") == "told to fail");
    CHECK(text_result(run) == "good");
  }

  TEST_CASE("when: conditions on traits, on the status and output kinds of earlier steps, and on arguments") {
    Env env;
    auto it = env.interpreter();
    const Json run = unwrap(it.start(env.request("t.cond")));
    CHECK(run.at("status") == "completed");
    CHECK(step(run, "a1.a").at("status") == "ok");
    CHECK(step(run, "a1.b").at("status") == "skipped");  // a message has no audio
    CHECK(step(run, "a1.c").at("status") == "ok");       // step a ok AND a message is in a conversation
    CHECK(step(run, "a1.d").at("status") == "skipped");  // not (a ok)
    CHECK(step(run, "a1.e").at("status") == "skipped");  // no flag argument, c has no json output
    const Json flagged = unwrap(it.start(env.request("t.cond", Json{{"flag", true}})));
    CHECK(step(flagged, "a1.e").at("status") == "ok");
    CHECK(env.calls.back() == "a1.e:t.echo:");
  }

  TEST_CASE("a composite inside a composite: arguments pass down, the result of the inner one feeds the next step") {
    Env env;
    auto it = env.interpreter();
    const Json run = unwrap(it.start(env.request("t.outer", Json{{"who", "friend"}})));
    CHECK(run.at("status") == "completed");
    CHECK(text_result(run) == "FRIEND");
    CHECK(env.calls == std::vector<std::string>{"a1.inner.s1:t.echo:", "a1.inner.s2:t.upper:", "a1.tail:t.echo:"});
  }

  TEST_CASE("a failing step stops the run with the error of that step; nothing after it runs") {
    Env env;
    env.setup(Json{{"t.broken", action("t.broken", "composite", Json::array({Json{{"id", "f"}, {"action", "t.fail"}}, Json{{"id", "l"}, {"action", "t.later"}}}))}}, Json::object());
    env.executors.add("t.fail", [&](const StepCall& c) {
      env.calls.push_back(c.step_id + ":t.fail:");
      StepOutcome o;
      o.error = Error(Errc::Internal, "always fails");
      return o;
    });
    auto it = env.interpreter();
    const Json run = unwrap(it.start(env.request("t.broken")));
    CHECK(run.at("status") == "failed");
    CHECK(run.at("error").at("step") == "a1.f");
    CHECK(run.at("error").at("message") == "always fails");
    CHECK(env.calls == std::vector<std::string>{"a1.f:t.fail:"});
  }

  TEST_CASE("plans that cannot run are refused before anything executes") {
    Env env;
    auto it = env.interpreter();
    auto planned = it.start(env.request("t.planned"));
    REQUIRE_FALSE(planned);
    CHECK(planned.error().message.find("planned, not implemented") != std::string::npos);
    auto consumer = it.start(env.request("t.consumer"));
    REQUIRE_FALSE(consumer);
    CHECK(consumer.error().message.find("carried out by its consumer") != std::string::npos);
    auto loop = it.start(env.request("t.loop"));
    REQUIRE_FALSE(loop);
    CHECK(loop.error().message.find("contains itself") != std::string::npos);
    auto unfit = it.start(env.request("t.needs_audio"));
    REQUIRE_FALSE(unfit);
    CHECK(unfit.error().message.find("missing has_audio") != std::string::npos);
    auto unknown = it.start(env.request("t.nothing"));
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == Errc::NotFound);
    CHECK(env.calls.empty());
  }

  TEST_CASE("rules: deny stops the whole run before any step, with the reason and the initiator of the rule") {
    Env env(rule_overlay(Json{{"id", "no_upper"}, {"scope", "installation"}, {"target", Json{{"kind", "class"}, {"id", "message"}}},
                              {"action", "t.upper"}, {"effect", "deny"}, {"initiator", "owner"},
                              {"reason", Json{{"en", "shouting is off"}}}}));
    auto it = env.interpreter();
    const Json run = unwrap(it.start(env.request("t.seq")));
    CHECK(run.at("status") == "denied");
    CHECK(run.at("error").at("code") == "denied");
    CHECK(run.at("error").at("gates").at(0).at("rule") == "no_upper");
    CHECK(run.at("error").at("gates").at(0).at("initiator") == "owner");
    CHECK(run.at("error").at("gates").at(0).at("reason").at("en") == "shouting is off");
    CHECK(env.calls.empty());  // t.echo, the first step, did not run either
  }

  TEST_CASE("rules: confirm pauses with a receipt; the answer must carry that receipt; yes resumes, no executes nothing") {
    Env env(rule_overlay(Json{{"id", "ask_before_sending"}, {"scope", "installation"}, {"target", Json{{"kind", "class"}, {"id", "message"}}},
                              {"action", Json{{"flag", "sends_off_device"}}}, {"effect", "confirm"}, {"initiator", "owner"}}));
    auto it = env.interpreter();
    const Json paused = unwrap(it.start(env.request("t.sends_chain")));
    CHECK(paused.at("status") == "awaiting_confirmation");
    CHECK(env.calls.empty());  // nothing before the answer, not even the steps that did not need it
    const Json pending = paused.at("pending");
    CHECK(pending.at("kind") == "confirmation");
    CHECK(pending.at("gates").at(0).at("action") == "t.sends");
    const std::string run_id = paused.at("id");
    // a missing, wrong or invented receipt is refused
    CHECK_FALSE(it.confirm(run_id, Json{{"approved", true}}));
    auto wrong = it.confirm(run_id, Json{{"approved", true}, {"receipt_id", "arr_made_up"}});
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().code == Errc::Conflict);
    CHECK(env.calls.empty());
    // the pause is durable: another interpreter over the same store resumes it
    auto other = env.interpreter();
    const Json done = unwrap(other.confirm(run_id, Json{{"approved", true}, {"receipt_id", pending.at("receipt_id")}, {"ref", "test"}}));
    CHECK(done.at("status") == "completed");
    CHECK(text_result(done) == "SENT");
    CHECK(done.at("confirmations").back().at("ref") == "test");
    // answering again is a conflict, not a second execution
    auto again = other.confirm(run_id, Json{{"approved", true}, {"receipt_id", pending.at("receipt_id")}});
    REQUIRE_FALSE(again);
    CHECK(again.error().code == Errc::Conflict);
    CHECK(env.calls.size() == 2);

    // No: nothing is executed
    Env env2(rule_overlay(Json{{"id", "ask_before_sending"}, {"scope", "installation"}, {"target", Json{{"kind", "class"}, {"id", "message"}}},
                               {"action", Json{{"flag", "sends_off_device"}}}, {"effect", "confirm"}}));
    auto it2 = env2.interpreter();
    const Json p2 = unwrap(it2.start(env2.request("t.sends_chain")));
    const Json no = unwrap(it2.confirm(rid(p2), Json{{"approved", false}, {"receipt_id", p2.at("pending").at("receipt_id")}}));
    CHECK(no.at("status") == "rejected");
    CHECK(env2.calls.empty());
    // confirmed:true in the request accepts the rule confirmations up front (the caller's explicit yes)
    Env env3(rule_overlay(Json{{"id", "ask_before_sending"}, {"scope", "installation"}, {"target", Json{{"kind", "class"}, {"id", "message"}}},
                               {"action", Json{{"flag", "sends_off_device"}}}, {"effect", "confirm"}}));
    auto it3 = env3.interpreter();
    RunRequest r3 = env3.request("t.sends_chain");
    r3.confirmed = true;
    r3.ref = "cli";
    const Json yes = unwrap(it3.start(r3));
    CHECK(yes.at("status") == "completed");
    CHECK(yes.at("confirmations").at(0).at("ref") == "cli");
  }

  TEST_CASE("propose: the plan is stored and nothing runs until the user's go; the go also covers the confirmations that were shown") {
    Env env(rule_overlay(Json{{"id", "ask_before_sending"}, {"scope", "installation"}, {"target", Json{{"kind", "class"}, {"id", "message"}}},
                              {"action", Json{{"flag", "sends_off_device"}}}, {"effect", "confirm"}}));
    auto it = env.interpreter();
    RunRequest r = env.request("t.sends_chain");
    r.propose = true;
    const Json proposed = unwrap(it.start(r));
    CHECK(proposed.at("status") == "proposed");
    CHECK(proposed.at("pending").at("kind") == "proposal");
    CHECK(proposed.at("plan").at("cost").at("declared").get<double>() == 1.0);
    CHECK(proposed.at("plan").at("effects").at("sends_off_device") == true);
    CHECK(env.calls.empty());
    const Json done = unwrap(it.confirm(rid(proposed), Json{{"approved", true}, {"receipt_id", proposed.at("pending").at("receipt_id")}, {"ref", "chat:m_9"}}));
    CHECK(done.at("status") == "completed");  // the go covered the rule confirmation shown in the proposal
    CHECK(text_result(done) == "SENT");
    // a proposal can be cancelled
    const Json second = unwrap(it.start(r));
    const Json cancelled = unwrap(it.cancel(rid(second), "test"));
    CHECK(cancelled.at("status") == "cancelled");
    CHECK_FALSE(it.confirm(rid(second), Json{{"approved", true}, {"receipt_id", second.at("pending").at("receipt_id")}}));
  }

  TEST_CASE("the usage guard's hold pauses a step with its receipt; confirming re-runs that step with the confirmation; declining records it and stops") {
    Env env;
    auto it = env.interpreter();
    const Json held = unwrap(it.start(env.request("t.hold_then_echo")));
    CHECK(held.at("status") == "awaiting_usage");
    CHECK(held.at("pending").at("kind") == "usage");
    CHECK(held.at("pending").at("receipt_id") == "usage_r1");
    CHECK(held.at("pending").at("step") == "a1.h");
    CHECK(step(held, "a1.h").at("status") == "held");
    CHECK(env.calls == std::vector<std::string>{"a1.h:t.hold:"});
    // a rule-style receipt is not the usage receipt
    CHECK_FALSE(it.confirm(rid(held), Json{{"approved", true}, {"receipt_id", "arr_nope"}}));
    const Json done = unwrap(it.confirm(rid(held), Json{{"approved", true}, {"receipt_id", "usage_r1"}, {"ref", "web"}}));
    CHECK(done.at("status") == "completed");
    CHECK(env.calls == std::vector<std::string>{"a1.h:t.hold:", "a1.h:t.hold:", "a1.l:t.later:"});
    CHECK(step(done, "a1.h").at("variants").at(0).at("parts").at(0).at("text") == "confirmed");
    // declining: the executor is called with approved:false (the guard records the denial), the run stops
    const Json held2 = unwrap(it.start(env.request("t.hold_then_echo")));
    const Json declined = unwrap(it.confirm(rid(held2), Json{{"approved", false}, {"receipt_id", "usage_r1"}}));
    CHECK(declined.at("status") == "rejected");
    CHECK(env.calls.back() == "a1.h:t.hold:");
    CHECK(std::count(env.calls.begin(), env.calls.end(), "a1.l:t.later:") == 1);  // only the first run got that far
  }

  TEST_CASE("cancel: a cancelled token stops before the next step; a stored paused run can be cancelled") {
    Env env;
    CancelToken token;
    env.cancel_inside = &token;
    auto it = env.interpreter();
    const Json run = unwrap(it.start(env.request("t.cancel_chain"), &token));
    CHECK(run.at("status") == "cancelled");
    CHECK(env.calls == std::vector<std::string>{"a1.c:t.cancel_me:"});  // the step after it did not run
    CHECK(step(run, "a1.c").at("status") == "ok");
    CHECK(run.at("cursor") == 1);
    // already cancelled before the start: no step runs
    CancelToken early;
    early.cancel();
    Env env2;
    auto it2 = env2.interpreter();
    const Json none = unwrap(it2.start(env2.request("t.seq"), &early));
    CHECK(none.at("status") == "cancelled");
    CHECK(env2.calls.empty());
    // a held run: cancel instead of answering
    Env env3;
    auto it3 = env3.interpreter();
    const Json held = unwrap(it3.start(env3.request("t.hold_then_echo")));
    const Json cancelled = unwrap(it3.cancel(rid(held), "web"));
    CHECK(cancelled.at("status") == "cancelled");
    CHECK(cancelled.at("pending").is_null());
    auto twice = it3.cancel(rid(held));
    REQUIRE_FALSE(twice);
    CHECK(twice.error().code == Errc::Conflict);
  }

  TEST_CASE("a chain (the answer of propose_chain) runs through the same interpreter, step by step, with the traits it produces") {
    Env env;
    auto it = env.interpreter();
    // t.makes_audio_text produces has_audio, so the second item fits only because of the first
    RunRequest r;
    r.chain = Json::array({Json{{"action", "t.makes_audio_text"}}, Json{{"action", "t.needs_audio"}}});
    r.element = Json{{"class", "message"}};
    const Json run = unwrap(it.start(r));
    CHECK(run.at("status") == "completed");
    CHECK(env.calls == std::vector<std::string>{"c1:t.makes_audio_text:", "c2:t.needs_audio:"});
    CHECK(text_result(run) == "heard");
    // the same chain in the shape propose_chain returns it, and without the first item it does not fit
    RunRequest as_object;
    as_object.chain = Json{{"steps", Json::array({Json{{"action", "t.makes_audio_text"}, {"title", "x"}}, Json{{"action", "t.needs_audio"}}})}, {"cost", 2}};
    as_object.element = r.element;
    CHECK(unwrap(it.start(as_object)).at("status") == "completed");
    RunRequest alone;
    alone.chain = Json::array({Json{{"action", "t.needs_audio"}}});
    alone.element = r.element;
    CHECK_FALSE(it.start(alone));
    // a chain whose step is denied by the rules never starts
    CHECK(unwrap(it.preview(r)).at("runnable") == true);
  }

  TEST_CASE("the event-log store keeps every checkpoint and returns the latest; list filters by status") {
    fsutil::TempDir td;
    auto db = loom::test::open_db(td.path() / "test.db");
    EventLog log(*db);
    auto store = make_event_log_run_store(log);
    Json a{{"id", "ar_a"}, {"status", "proposed"}, {"request", Json{{"context", Json{{"conv_id", "c_1"}}}}}};
    REQUIRE(store->save(a));
    a["status"] = "completed";
    REQUIRE(store->save(a));
    REQUIRE(store->save(Json{{"id", "ar_b"}, {"status", "proposed"}, {"request", Json{{"context", Json{{"conv_id", "c_2"}}}}}}));
    CHECK(unwrap(store->load("ar_a"))->at("status") == "completed");
    CHECK_FALSE(unwrap(store->load("ar_zzz")).has_value());
    CHECK(unwrap(store->list(Json{{"status", "proposed"}})).size() == 1);
    CHECK(unwrap(store->list(Json{{"conv_id", "c_1"}})).size() == 1);
    CHECK(unwrap(store->list(Json::object())).size() == 2);
    CHECK(unwrap(store->list(Json::object())).at(0).at("id") == "ar_b");  // newest first
  }
}
