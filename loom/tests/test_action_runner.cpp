// The interpreter bound to a runtime (R54): real executors behind run/confirm/cancel, element ids,
// the usage guard's x10 hold, settings actions under the edit rules, invoke ports, accepted chains.
#include <doctest/doctest.h>

#include "loom/action_runner.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/settings_fields.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})},
              {"usage", Json{{"total_tokens", 7}}}};
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    loom::test::measured_baseline_only(rt->config());  // P17 6: this test is about the x10 hold over three tiny samples, so the floor of the baseline is off
    rt->config().set("model_registry_autorefresh", "off");  // these tests count the transport's requests; fetching the model list for a price is tested in test_estimate_registry
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    add("user", "We should ship on Friday.");
    add("assistant", "Agreed, Friday works if the tests pass.");
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void script(int n, const std::string& content = "A short summary.") {
    for (int i = 0; i < n; ++i)
      transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content)));
  }
  actions::ActionRunner& runner() { return rt->actions(); }
};

std::string text_result(const Json& run) {
  std::string out;
  for (const auto& p : run.at("result").at("parts"))
    if (p.at("kind") == "text") out += p.at("text").get<std::string>();
  return out;
}

}  // namespace

TEST_SUITE("action_runner") {
  TEST_CASE("summarise on a conversation id: the transcript goes to the model with the prompt from data, the answer is the result, the step carries the model provenance") {
    Fixture f;
    f.script(1);
    const Json run = unwrap(f.runner().run(Json{{"action", "summarise"}, {"element", f.conv_id}, {"ref", "test"}}));
    REQUIRE(run.at("status") == "completed");
    CHECK(text_result(run) == "A short summary.");
    const auto requests = f.transport->requests();
    REQUIRE(requests.size() == 1);
    const Json body = unwrap(json::parse(requests[0].body));
    CHECK(body.at("model") == "test/model");
    CHECK(body.at("messages").at(0).at("role") == "system");
    const std::string all = body.at("messages").dump();
    CHECK(all.find("User: We should ship on Friday.") != std::string::npos);
    CHECK(all.find("Assistant: Agreed") != std::string::npos);
    CHECK(all.find("Summarise the text above.") != std::string::npos);
    const Json step = run.at("steps").at(0);
    CHECK(step.at("variants").at(0).at("provenance").at(0).at("schema") == "loom.model_use/1");
    CHECK(step.at("variants").at(0).at("provenance").at(0).at("requested_model") == "test/model");
    CHECK(run.at("request").at("element").at("id") == f.conv_id);  // the resolved element is what the run stores
    CHECK(run.at("element").at("class") == "conversation");
    // The run is durable: another runner call reads it back from the event log, step events are in the log.
    const Json again = unwrap(f.runner().status(Json{{"run_id", run.at("id")}}));
    CHECK(again.at("status") == "completed");
    CHECK(again.at("steps").at(0).at("outputs_sha256") == step.at("outputs_sha256"));
    const auto events = unwrap(f.rt->event_log().query(EventQuery{0, std::string("action.step"), run.at("id").get<std::string>(), 100}));
    CHECK(events.size() == 1);
    const Json listing = unwrap(f.runner().status(Json{{"list", true}}));
    CHECK(listing.at("runs").at(0).at("id") == run.at("id"));
  }

  TEST_CASE("a model named in the call and parameters in the step are data; nothing is sent when the plan is only previewed or proposed") {
    Fixture f;
    f.rt->config().set("default_model", "other/default");
    const Json preview = unwrap(f.runner().preview(Json{{"action", "summarise"}, {"element", f.conv_id}}));
    CHECK(preview.at("runnable") == true);
    CHECK(preview.at("plan").at("cost").at("model_calls") == 1);
    CHECK(preview.at("plan").at("effects").at("sends_off_device") == true);
    const Json proposed = unwrap(f.runner().run(Json{{"action", "summarise"}, {"element", f.conv_id}, {"propose", true}}));
    CHECK(proposed.at("status") == "proposed");
    CHECK(f.transport->requests().empty());
    f.script(1);
    const Json done = unwrap(f.runner().confirm(Json{{"run_id", proposed.at("id")}, {"approved", true},
        {"receipt_id", proposed.at("pending").at("receipt_id")}, {"ref", "test"}}));
    CHECK(done.at("status") == "completed");
    const Json body = unwrap(json::parse(f.transport->requests().at(0).body));
    CHECK(body.at("model") == "other/default");
    CHECK(body.at("temperature") == 0.3);
  }

  TEST_CASE("no key, no model call: the step fails with the reason and the run says where") {
    Fixture f;
    f.rt->secrets().set("api_key", "");
    const Json run = unwrap(f.runner().run(Json{{"action", "summarise"}, {"element", f.conv_id}}));
    CHECK(run.at("status") == "failed");
    CHECK(run.at("error").at("step") == "a1.s1");
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("x10 growth pauses the model step with the guard's receipt; confirming sends once; declining sends nothing") {
    Fixture f;
    f.script(4);
    for (int i = 0; i < 3; ++i) REQUIRE(unwrap(f.runner().run(Json{{"action", "summarise"}, {"element", f.conv_id}})).at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 3);
    const std::string big_id = f.add("user", std::string(60000, 'x'));
    // a message element: the big one alone is far above the baseline of the three small runs
    const Json held = unwrap(f.runner().run(Json{{"action", "summarise"}, {"element", big_id}}));
    REQUIRE(held.at("status") == "awaiting_usage");
    CHECK(held.at("pending").at("kind") == "usage");
    CHECK(held.at("pending").at("usage_pending").at("sent") == false);
    CHECK(f.transport->requests().size() == 3);  // nothing left the machine
    const std::string receipt = held.at("pending").at("receipt_id").get<std::string>();
    // a run-level confirmed:true does not cover the usage guard; only its own receipt does
    auto wrong = f.runner().confirm(Json{{"run_id", held.at("id")}, {"approved", true}, {"receipt_id", "arr_made_up"}});
    CHECK_FALSE(wrong);
    CHECK(f.transport->requests().size() == 3);
    const Json done = unwrap(f.runner().confirm(Json{{"run_id", held.at("id")}, {"approved", true}, {"receipt_id", receipt}, {"ref", "web"}}));
    CHECK(done.at("status") == "completed");
    CHECK(f.transport->requests().size() == 4);
    CHECK(text_result(done) == "A short summary.");
    // declining: the ledger records the denial, nothing is sent
    const Json held2 = unwrap(f.runner().run(Json{{"action", "summarise"}, {"element", f.add("user", std::string(61000, 'y'))}}));
    if (held2.at("status") == "awaiting_usage") {
      const Json no = unwrap(f.runner().confirm(Json{{"run_id", held2.at("id")}, {"approved", false},
          {"receipt_id", held2.at("pending").at("receipt_id")}}));
      CHECK(no.at("status") == "rejected");
      CHECK(f.transport->requests().size() == 4);
    }
  }

  TEST_CASE("settings.set honours the values a field declares: a listed value is stored; a planned or unlisted one is refused or stored with a warning, as the data says") {
    Fixture f;
    // a listed value
    const Json ok = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "navigator_layout"}, {"value", "list"}}}}));
    CHECK(ok.at("status") == "completed");
    CHECK(f.rt->config().get("navigator_layout") == "list");
    // a value no reader understands never lands in config.json; the message names the values
    const Json unknown = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "navigator_layout"}, {"value", "nonsense"}}}}));
    CHECK(unknown.at("status") == "failed");
    CHECK(unknown.at("error").at("message").get<std::string>().find("\"both\", \"list\", \"map\"") != std::string::npos);
    CHECK(f.rt->config().get("navigator_layout") == "list");
    // the order `stability` is in the context data since M1-D2 (it was planned with on_set: refuse before): it is stored like any listed value
    // (the refuse rule itself is exercised on a synthetic value in test_settings_fields)
    const Json stability = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "chat_context_order"}, {"value", "stability"}}}}));
    CHECK(stability.at("status") == "completed");
    CHECK(f.rt->config().get("chat_context_order") == "stability");
    // the empty value is a value of its own (the profile's order), listed in the data
    CHECK(unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "chat_context_order"}, {"value", ""}}}})).at("status") == "completed");
    CHECK(f.rt->config().get("chat_context_order") == "");
    CHECK(unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "chat_context_order"}, {"value", "stable_first"}}}})).at("status") == "completed");
    // `windows` was planned (on_set: warn) until the window system drew the grid from its dock tree (M1-OKNA-W3): it is a listed
    // value now, stored without a warning (the planned-and-warn path is exercised on a value of its own in test_settings_fields)
    const Json windows = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "layout_mode"}, {"value", "windows"}}}}));
    REQUIRE(windows.at("status") == "completed");
    CHECK(f.rt->config().get("layout_mode") == "windows");
    CHECK_FALSE(windows.at("result").at("data").contains("warning"));
    // outside a closed list whose reader falls back (on_unlisted: warn): stored, the warning names the list
    const Json other = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "layout_mode"}, {"value", "tiles"}}}}));
    REQUIRE(other.at("status") == "completed");
    CHECK(other.at("result").at("data").at("warning").at("status") == "unlisted");
    CHECK(other.at("result").at("data").at("warning").at("values").size() == 3);
    // a listed, available value has nothing to say
    const Json grid = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "layout_mode"}, {"value", "views_grid"}}}}));
    CHECK_FALSE(grid.at("result").at("data").contains("warning"));
    // a field without a values list stays open (type only)
    CHECK(unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "theme"}, {"value", "anything"}}}})).at("status") == "completed");
  }

  TEST_CASE("settings.set: chat_context_order lists its reader's orders (values_from), so an order a profile overlay adds is stored and only an unknown name is refused") {
    Fixture f;
    const auto root = f.rt->paths().root;
    // the user's own order, added the way the reader (chat_engine) reads orders: profiles/chat_context.pack
    unwrap(fsutil::write_file(root / "profiles" / "chat_context.pack",
        Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "chat_context"},
             {"overrides", Json{{"orders", Json{{"mine", Json::array({"memory", "system", "project", "history", "graph"})}}}}}}.dump()));
    const Json mine = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "chat_context_order"}, {"value", "mine"}}}}));
    CHECK(mine.at("status") == "completed");
    CHECK_FALSE(mine.at("result").at("data").contains("warning"));
    CHECK(f.rt->config().get("chat_context_order") == "mine");
    // a shipped order is stored as well
    CHECK(unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "chat_context_order"}, {"value", "stability"}}}})).at("status") == "completed");
    CHECK(f.rt->config().get("chat_context_order") == "stability");
    // a name the effective profile does not know is still refused (chat.send would reject it), and nothing is stored
    const Json unknown = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "chat_context_order"}, {"value", "no_such_order"}}}}));
    CHECK(unknown.at("status") == "failed");
    CHECK(unknown.at("error").at("message").get<std::string>().find("\"mine\"") != std::string::npos);
    CHECK(f.rt->config().get("chat_context_order") == "stability");
    // the raw write paths read the same list
    CHECK(unwrap(check_config_patch(Json{{"chat_context_order", "mine"}}, root)).empty());
    CHECK_FALSE(check_config_patch(Json{{"chat_context_order", "no_such_order"}}, root));
    // the document says where the values come from
    const Json doc = unwrap(settings_fields(root));
    for (const auto& field : doc.at("fields")) {
      if (field.at("key") != "chat_context_order") continue;
      bool listed = false;
      for (const auto& v : field.at("values"))
        if (v.at("value") == "mine") { listed = true; CHECK(v.at("from_profile") == "chat_context"); }
        else if (v.at("value") == "stability") { CHECK_FALSE(v.contains("status")); CHECK_FALSE(v.contains("status_superseded")); }
      CHECK(listed);
    }
  }

  TEST_CASE("settings.set: the field is the element; the value is typed; edit rules apply; credentials stay out of the conversation") {
    Fixture f;
    const Json set = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "semantic_retry_mode"}, {"value", "automatic"}}}}));
    CHECK(set.at("status") == "completed");
    CHECK(set.at("element").at("class") == "settings_field");
    CHECK(set.at("result").at("data").at("value") == "automatic");
    CHECK(f.rt->config().get("semantic_retry_mode") == "automatic");
    // a value the setting refuses is a failure with the setting's own message
    const Json bad = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "semantic_retry_mode"}, {"value", "sometimes"}}}}));
    CHECK(bad.at("status") == "failed");
    // (the field's declared values, settings_fields.pack: the same two the worker accepts)
    CHECK(bad.at("error").at("message").get<std::string>().find("\"explicit_requeue\", \"automatic\"") != std::string::npos);
    CHECK(f.rt->config().get("semantic_retry_mode") == "automatic");
    // types: an integer field takes a number written as text
    CHECK(unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "max_tokens"}, {"value", "2048"}}}})).at("status") == "completed");
    CHECK(f.rt->config().get("max_tokens") == 2048);
    // read back
    const Json got = unwrap(f.runner().run(Json{{"action", "settings.get"}, {"args", Json{{"key", "semantic_retry_mode"}}}}));
    CHECK(got.at("result").at("data").at("value") == "automatic");
    // a credential is denied by the rule, with its reason; its value never appears
    const Json secret = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "api_key"}, {"value", "sk-evil"}}}}));
    CHECK(secret.at("status") == "denied");
    CHECK(secret.at("error").at("gates").at(0).at("rule") == "deny:settings.set:secret");
    CHECK(secret.at("error").at("gates").at(0).at("initiator") == "engine");
    // The value of a credential is stored nowhere: not in the answer, not in the stored run, not in the event log.
    CHECK(secret.dump().find("sk-evil") == std::string::npos);
    CHECK(unwrap(f.runner().status(Json{{"run_id", secret.at("id")}})).dump().find("sk-evil") == std::string::npos);
    for (const auto& e : unwrap(f.rt->event_log().query(EventQuery{0, std::nullopt, std::nullopt, 100000})))
      CHECK(e.payload.dump().find("sk-evil") == std::string::npos);
    // a run on a credential cannot wait for an answer (its value would have to be stored)
    {
      f.rt->config().set("unused", 1);
      auto waits = f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "api_key"}, {"value", "sk-evil"}}}, {"propose", true}});
      CHECK((!waits || waits->at("status") == "denied"));
    }
    CHECK(f.rt->secrets().get_string("api_key") == "sk-test-key");
    // the edit rule of a read-only field applies through gate_as
    const Json ro = unwrap(f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "_config_version"}, {"value", "9"}}}}));
    CHECK(ro.at("status") == "denied");
    // an unknown key is not a settings field: the action does not fit it
    auto none = f.runner().run(Json{{"action", "settings.set"}, {"args", Json{{"key", "no_such_key"}, {"value", "1"}}}});
    CHECK_FALSE(none);
    // the field that raises consumption by an order of magnitude asks first (require_confirmation)
    const Json asks = unwrap(f.runner().run(Json{{"action", "settings.set"},
        {"args", Json{{"key", "loom_usage_policy"}, {"value", "{\"growth_factor\": 10}"}}}}));
    CHECK(asks.at("status") == "awaiting_confirmation");
    CHECK(asks.at("pending").at("gates").at(0).at("rule") == "edit:config:loom_usage_policy");
  }

  TEST_CASE("an atomic action of the catalogue runs through its invoke port") {
    Fixture f;
    const Json run = unwrap(f.runner().run(Json{{"action", "storage.status"}, {"element", "storage"}}));
    CHECK(run.at("status") == "completed");
    CHECK(run.at("result").at("data").is_object());
    // the query actions are steps too
    const Json fits = unwrap(f.runner().run(Json{{"action", "actions.for"}, {"element", "application"}, {"args", Json{{"element", f.conv_id}}}}));
    CHECK(fits.at("status") == "completed");
    CHECK(fits.at("result").at("data").at("element").at("class") == "conversation");
  }

  TEST_CASE("the port tool_source runs as a step: tool_source.list completes instead of Unsupported") {
    Fixture f;
    const Json listed = unwrap(f.runner().run(Json{{"action", "tool_source.list"}, {"element", Json{{"class", "application"}}}}));
    CHECK(listed.at("status") == "completed");
    CHECK(listed.at("result").at("data").is_object());
  }

  TEST_CASE("the ports environments and agent run as steps (P26): the same answers as through loom_invoke") {
    Fixture f;
    const Json listed = unwrap(f.runner().run(Json{{"action", "environments.list"}, {"element", Json{{"class", "application"}}}}));
    CHECK(listed.at("status") == "completed");
    CHECK(listed.at("result").at("data").at("schema") == "loom.environments_list/1");
    const Json synced = unwrap(f.runner().run(Json{{"action", "environments.sync"}, {"element", Json{{"class", "application"}}}}));
    CHECK(synced.at("status") == "completed");
    CHECK(synced.at("result").at("data").at("schema") == "loom.environments_sync/1");
    // agent.run is in the catalogue and runnable; it is a model action, so it asks like one: nothing is sent before the answer
    const Json plan = unwrap(f.runner().run(Json{{"action", "agent.run"}, {"element", Json{{"class", "application"}}}, {"args", Json{{"prompt", "say hi"}}}, {"propose", true}}));
    CHECK(plan.at("status") == "proposed");
    CHECK(f.transport->requests().empty());
    const Json agent_run = unwrap(f.runner().run(Json{{"action", "agent.run"}, {"element", Json{{"class", "application"}}}, {"args", Json{{"prompt", "say hi"}}}}));
    CHECK((agent_run.at("status") == "awaiting_confirmation" || agent_run.at("status") == "awaiting_usage" || agent_run.at("status") == "completed"));
    if (agent_run.at("status") == "awaiting_confirmation") CHECK(f.transport->requests().empty());
    // after the answer the loop runs: one model turn, the answer is the result
    if (agent_run.at("status") == "awaiting_confirmation") {
      f.script(1, "hi there");
      const Json done = unwrap(f.runner().confirm(Json{{"run_id", agent_run.at("id")}, {"approved", true},
                                                       {"receipt_id", agent_run.at("pending").at("receipt_id")}, {"ref", "test"}}));
      CHECK(done.at("status") == "completed");
      CHECK(done.at("result").at("data").at("text") == "hi there");
    }
  }

  TEST_CASE("a chain from propose_chain is accepted and run through the same interpreter") {
    Fixture f;
    const std::string mid = f.add("user", "The launch plan has three phases and two risks.");
    const Json proposal = unwrap(f.runner().invoke("propose_chain", Json{{"element", mid}, {"target_trait", "summary"}}));
    REQUIRE(proposal.at("chains").size() >= 1);
    const Json chain = proposal.at("chains").at(0);
    CHECK(chain.at("executable") == true);
    f.script(1, "Three phases, two risks.");
    const Json run = unwrap(f.runner().run(Json{{"chain", chain}, {"element", mid}}));
    CHECK(run.at("status") == "completed");
    CHECK(text_result(run) == "Three phases, two risks.");
    CHECK(run.at("plan").at("items").at(0).at("action") == "summarise");
    // a chain that includes a planned action never starts and nothing is sent
    // (a microphone is a device: recording is the client's, this runtime has no driver for it, so the chain from it is declared only)
    const std::string audio = "device:microphone";
    const Json audio_chain = unwrap(f.runner().invoke("propose_chain", Json{{"element", audio}, {"target_trait", "summary"}}));
    REQUIRE(audio_chain.at("chains").size() >= 1);
    CHECK(audio_chain.at("chains").at(0).at("executable") == false);
    auto refused = f.runner().run(Json{{"chain", audio_chain.at("chains").at(0)}, {"element", audio}});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message.find("planned, not implemented") != std::string::npos);
    CHECK(f.transport->requests().size() == 1);
  }

  TEST_CASE("cancel: a stored proposal is cancelled; an unknown run is not found") {
    Fixture f;
    const Json proposed = unwrap(f.runner().run(Json{{"action", "summarise"}, {"element", f.conv_id}, {"propose", true}}));
    const Json cancelled = unwrap(f.runner().cancel(Json{{"run_id", proposed.at("id")}, {"ref", "test"}}));
    CHECK(cancelled.at("status") == "cancelled");
    CHECK(f.transport->requests().empty());
    auto none = f.runner().cancel(Json{{"run_id", "ar_000000000000"}});
    REQUIRE_FALSE(none);
    CHECK(none.error().code == Errc::NotFound);
  }

  TEST_CASE("an external cancel token set before the run stops it before the first step") {
    Fixture f;
    CancelToken token;
    token.cancel();
    const Json run = unwrap(f.runner().run(Json{{"action", "summarise"}, {"element", f.conv_id}}, &token));
    CHECK(run.at("status") == "cancelled");
    CHECK(f.transport->requests().empty());
  }
}
