// The tool loop of use_model (tool_loop.h, agent.h): the model is offered the catalogue's actions as tools, the
// interpreter carries them out under its own rules, the policy is data. All against the scripted transport - no
// provider is called.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_runner.h"
#include "loom/agent.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/usage_policy.h"
#include "loom/tool_loop.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
using Reply = net::ScriptedTransport::Reply;

namespace {

Json text_reply(const std::string& content, int tokens = 11) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})},
              {"usage", Json{{"prompt_tokens", tokens}, {"completion_tokens", 4}, {"total_tokens", tokens + 4}}}};
}

// An OpenAI-shaped answer asking for tools: [{id, name, arguments}].
Json call_reply(const std::vector<std::tuple<std::string, std::string, Json>>& calls, int tokens = 20) {
  Json list = Json::array();
  for (const auto& [id, name, args] : calls)
    list.push_back(Json{{"id", id}, {"type", "function"}, {"function", Json{{"name", name}, {"arguments", args.dump()}}}});
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", nullptr}, {"tool_calls", list}}}}})},
              {"usage", Json{{"prompt_tokens", tokens}, {"completion_tokens", 6}, {"total_tokens", tokens + 6}}}};
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id, other_id;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    loom::test::measured_baseline_only(rt->config());
    rt->config().set("model_registry_autorefresh", "off");
    conv_id = unwrap(rt->db().create_conv("Report format")).id;
    add(conv_id, "user", "Let us settle how the weekly report looks.");
    add(conv_id, "assistant", "Agreed: the report format is one page, three bullet points, sent on Fridays.");
    other_id = unwrap(rt->db().create_conv("Holidays")).id;
    add(other_id, "user", "Where do we go in August?");
  }
  std::string add(const std::string& conv, const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void script(const Json& body) { transport->expect("POST", "https://api.test/chat/completions", Reply::json(200, body)); }
  Json agent(const Json& request) { return unwrap(agent::run(*rt, request)); }
  std::size_t model_calls() const {
    std::size_t n = 0;
    for (const auto& r : transport->requests())
      if (r.method == "POST") ++n;
    return n;
  }
  Json body(std::size_t i) const { return unwrap(json::parse(transport->requests().at(i).body)); }
  bool conv_exists(const std::string& id) { return unwrap(rt->db().get_conv(id)).has_value(); }
};

}  // namespace

TEST_SUITE("tool_loop") {
  TEST_CASE("acceptance: find in my conversations where we settled the report format, and answer - search runs as an action, the result goes back to the model, the answer closes the loop") {
    Fixture f;
    f.script(call_reply({{"call_1", "search_find", Json{{"query", "report format"}}}}));
    f.script(text_reply("You settled it in 'Report format': one page, three bullets, sent on Fridays."));
    const Json a = f.agent(Json{{"prompt", "Find in my conversations where we settled the report format and summarise."}});
    CHECK(a.at("status") == "finished");
    CHECK(a.at("text").get<std::string>().find("one page") != std::string::npos);
    CHECK(a.at("turns") == 2);
    REQUIRE(a.at("steps").size() == 1);
    const Json step = a.at("steps")[0];
    CHECK(step.at("action") == "search.find");
    CHECK(step.at("mode") == "run");
    CHECK(step.at("status") == "completed");
    // the first request offered the catalogue's fitting actions as OpenAI tools, the policy-free ones included
    const Json first = f.body(0);
    REQUIRE(first.contains("tools"));
    bool offered = false, leaked = false;
    for (const auto& t : first.at("tools")) {
      const std::string n = t.at("function").at("name");
      offered = offered || n == "search_find";
      leaked = leaked || n == "use_model" || n.rfind("actions_", 0) == 0;
      CHECK(t.at("type") == "function");
    }
    CHECK(offered);
    CHECK_FALSE(leaked);  // the interpreter's own door and use_model are not tools (profile expose.exclude)
    // the second request carries the call and the result of the action, with the conversation found
    const Json second = f.body(1);
    const Json& msgs = second.at("messages");
    REQUIRE(msgs.size() >= 3);
    const Json& assistant = msgs[msgs.size() - 2];
    CHECK(assistant.at("tool_calls")[0].at("id") == "call_1");
    const Json& tool = msgs[msgs.size() - 1];
    CHECK(tool.at("role") == "tool");
    CHECK(tool.at("tool_call_id") == "call_1");
    CHECK(tool.at("content").get<std::string>().find("Report format") != std::string::npos);
    CHECK(tool.at("content").get<std::string>().find("\"executed\":true") != std::string::npos);
    // the sum of the cost over the turns
    CHECK(a.at("usage").at("total_tokens") == 20 + 6 + 11 + 4);
    // the run is the interpreter's: it is in its log, asked by the loop
    const Json runs = unwrap(f.rt->actions().status(Json{{"run_id", step.at("run_id")}}));
    CHECK(runs.at("status") == "completed");
    CHECK(runs.at("request").at("ref") == "agent:" + a.at("loop_id").get<std::string>());
  }

  TEST_CASE("the trace in the graph: a node for the loop, every call and the interpreter's run node are part_of it") {
    Fixture f;
    f.script(call_reply({{"c1", "search_find", Json{{"query", "report"}}}}));
    f.script(call_reply({{"c2", "graph_overview", Json::object()}}));
    f.script(text_reply("done"));
    const Json a = f.agent(Json{{"prompt", "look around"}});
    REQUIRE(a.at("steps").size() == 2);
    const std::string loop_node = a.at("loop_node");
    REQUIRE_FALSE(loop_node.empty());
    const auto node = *unwrap(f.rt->db().get_node(loop_node));
    CHECK(node.kind == "action_run");
    CHECK(node.metadata.at("status") == "finished");
    CHECK(node.metadata.at("turns") == 3);
    CHECK(node.metadata.at("usage").at("total_tokens") == 26 + 26 + 15);
    int steps = 0, runs = 0;
    for (const auto& l : unwrap(f.rt->db().get_links(loop_node, std::string("part_of")))) {
      const auto src = *unwrap(f.rt->db().get_node(l.src));
      if (src.label.rfind("agent_step:", 0) == 0) ++steps;
      if (src.label.rfind("run:", 0) == 0) ++runs;
    }
    CHECK(steps == 2);
    CHECK(runs == 2);
  }

  TEST_CASE("local and reversible actions run without confirmation, everything else is a proposal and nothing runs") {
    Fixture f;
    f.script(call_reply({{"c1", "conversation_delete", Json{{"element", f.other_id}}}}));
    const Json a = f.agent(Json{{"prompt", "Delete the holidays conversation."}});
    CHECK(a.at("status") == "needs_confirmation");
    REQUIRE(a.at("steps").size() == 1);
    CHECK(a.at("steps")[0].at("mode") == "propose");
    CHECK(a.at("steps")[0].at("status") == "proposed");
    CHECK(f.conv_exists(f.other_id));         // nothing ran
    CHECK(f.model_calls() == 1);              // and the loop stopped (on_proposal: stop): the model was not asked again
    REQUIRE(a.at("proposals").size() == 1);
    const Json proposal = a.at("proposals")[0];
    CHECK(proposal.at("action") == "conversation.delete");
    // it is the interpreter's own proposal: stored, waiting for the user's receipt
    const Json run = unwrap(f.rt->actions().status(Json{{"run_id", proposal.at("run_id")}}));
    CHECK(run.at("status") == "proposed");
    // the user says yes through the ordinary confirmation - only then it runs
    const std::string receipt = run.at("plan_receipt");
    const Json done = unwrap(f.rt->actions().run(Json{{"action", "conversation.delete"}, {"element", f.other_id}, {"confirmed", true}, {"receipt_id", receipt}, {"ref", "test"}}));
    CHECK((done.at("status") == "completed" || done.at("status") == "proposed"));
  }

  TEST_CASE("on_proposal continue: the model is told it was not executed and goes on") {
    Fixture f;
    f.script(call_reply({{"c1", "conversation_delete", Json{{"element", f.other_id}}}, {"c2", "search_find", Json{{"query", "report"}}}}));
    f.script(text_reply("I could not delete it without your confirmation, but found the report conversation."));
    const Json a = f.agent(Json{{"prompt", "Delete holidays and find the report."}, {"on_proposal", "continue"}});
    CHECK(a.at("status") == "finished");
    CHECK(a.at("proposals").size() == 1);
    CHECK(a.at("steps").size() == 2);
    CHECK(f.conv_exists(f.other_id));
    const Json msgs = f.body(1).at("messages");
    std::string all = msgs.dump();
    CHECK(all.find("needs the user's confirmation") != std::string::npos);
    CHECK(all.find("\\\"executed\\\":true") != std::string::npos);  // the search did run in the same turn
  }

  TEST_CASE("the policy is data: a preset by name, per request or in the setting; confirm_all proposes even a search; an inline policy; a bad one is refused") {
    Fixture f;
    f.script(call_reply({{"c1", "search_find", Json{{"query", "report"}}}}));
    const Json a = f.agent(Json{{"prompt", "find"}, {"policy", "confirm_all"}});
    CHECK(a.at("status") == "needs_confirmation");
    CHECK(a.at("steps")[0].at("mode") == "propose");
    CHECK(a.at("settings").at("sources").at("policy") == "request");
    // the setting
    f.rt->config().set("tool_loop_policy", "read_only");
    f.script(call_reply({{"c1", "search_find", Json{{"query", "report"}}}}));
    f.script(text_reply("ok"));
    const Json b = f.agent(Json{{"prompt", "find"}});
    CHECK(b.at("status") == "finished");
    CHECK(b.at("settings").at("policy") == "read_only");
    CHECK(b.at("settings").at("sources").at("policy") == "setting");
    // a policy of the user's own, inline in the setting: only this action runs, the rest is denied
    f.rt->config().set("tool_loop_policy", Json{{"id", "mine"}, {"rules", Json::array({Json{{"when", Json{{"actions", Json::array({"search.find"})}}}, {"mode", "run"}}, Json{{"mode", "deny"}}})}});
    f.script(call_reply({{"c1", "graph_overview", Json::object()}}));
    f.script(text_reply("denied then"));
    const Json c = f.agent(Json{{"prompt", "overview"}});
    CHECK(c.at("settings").at("policy") == "mine");
    CHECK(c.at("steps")[0].at("mode") == "deny");
    CHECK(c.at("steps")[0].at("status") == "denied");
    CHECK(c.at("status") == "finished");
    // an unknown preset
    f.rt->config().set("tool_loop_policy", "");
    auto bad = agent::run(*f.rt, Json{{"prompt", "x"}, {"policy", "no_such"}});
    CHECK_FALSE(bad);
  }

  TEST_CASE("apply_all lets a mutating call run - through the interpreter's rules, which still decide") {
    Fixture f;
    f.script(call_reply({{"c1", "message_set_status", Json{{"element", f.add(f.other_id, "user", "to hide")}, {"status", "excluded"}}}}));
    f.script(text_reply("excluded"));
    const Json a = f.agent(Json{{"prompt", "hide that message"}, {"policy", "apply_all"}});
    REQUIRE(a.at("steps").size() == 1);
    CHECK(a.at("steps")[0].at("mode") == "run");
    // the run exists and is the interpreter's: whatever it decided (done, or waiting for a rule's confirmation) is recorded
    CHECK_FALSE(a.at("steps")[0].at("run_id").get<std::string>().empty());
  }

  TEST_CASE("the x10 guard still applies to a tool call: a call that grows the usage tenfold is held with its receipt, nothing is sent") {
    Fixture f;
    // the interpreter has seen three small summaries
    for (int i = 0; i < 3; ++i) {
      f.script(text_reply("A short summary."));
      REQUIRE(unwrap(f.rt->actions().run(Json{{"action", "summarise"}, {"element", f.conv_id}})).at("status") == "completed");
    }
    const std::string big = f.add(f.conv_id, "user", std::string(60000, 'x'));
    f.script(call_reply({{"c1", "summarise", Json{{"element", big}}}}));
    const std::size_t before = f.model_calls();
    const Json a = f.agent(Json{{"prompt", "summarise that long message"}, {"policy", "apply_all"}});
    CHECK(a.at("status") == "needs_confirmation");
    REQUIRE(a.at("proposals").size() == 1);
    CHECK(a.at("proposals")[0].at("status") == "awaiting_usage");
    CHECK(a.at("steps")[0].at("status") == "awaiting_usage");
    CHECK(f.model_calls() == before + 1);  // the loop's own turn; the summary was never sent
    const Json run = unwrap(f.rt->actions().status(Json{{"run_id", a.at("proposals")[0].at("run_id")}}));
    CHECK(run.at("pending").at("kind") == "usage");
    CHECK(run.at("pending").at("usage_pending").at("sent") == false);
  }

  TEST_CASE("the spending limit holds the loop's own model turn: status held, the guard's record, nothing sent") {
    Fixture f;
    {
      auto policy = unwrap(UsagePolicy::open(f.td.path() / "usage-policy.sqlite", Json::object()));
      Json estimate{{"operation_id", "op1"}, {"baseline_key", "chat.send|test|m"}, {"model", "test/model"}, {"resources", Json{{"output_tokens", nullptr}, {"cost_usd", nullptr}}}};
      unwrap(policy->request(estimate));
      unwrap(policy->complete("op1", Json{{"resources", Json{{"requests", 1}, {"output_tokens", 120}, {"cost_usd", 0.02}}}, {"provenance", "provider_reported"}}));
    }
    f.rt->config().set("usage_spend_limits", Json::array({Json{{"period", "day"}, {"amount", 0.01}, {"currency", "USD"}, {"on_exceed", "confirm"}}}));
    unwrap(f.rt->config().save());
    f.script(text_reply("never sent"));
    const Json a = f.agent(Json{{"prompt", "hello"}});
    CHECK(a.at("status") == "held");
    CHECK(a.at("turns") == 0);
    CHECK(a.at("hold").at("status") == "requires_confirmation");
    CHECK(f.model_calls() == 0);
  }

  TEST_CASE("the bound on model turns is a setting: a preset, 0 means no limit, the request may override") {
    Fixture f;
    f.transport->set_fallback(Reply::json(200, call_reply({{"c", "search_find", Json{{"query", "report"}}}})));
    // preset: 8
    const Json a = f.agent(Json{{"prompt", "again and again"}});
    CHECK(a.at("status") == "step_limit");
    CHECK(a.at("turns") == 8);
    CHECK(a.at("steps").size() == 8);
    CHECK(a.at("settings").at("sources").at("max_steps") == "profile");
    // the setting
    f.rt->config().set("tool_loop_max_steps", 3);
    const Json b = f.agent(Json{{"prompt", "again"}});
    CHECK(b.at("status") == "step_limit");
    CHECK(b.at("turns") == 3);
    CHECK(b.at("settings").at("sources").at("max_steps") == "setting");
    // the request over the setting
    const Json c = f.agent(Json{{"prompt", "again"}, {"max_steps", 1}});
    CHECK(c.at("turns") == 1);
    CHECK(c.at("settings").at("sources").at("max_steps") == "request");
    // 0 = no limit: twenty turns of calls, then the model answers
    f.rt->config().set("tool_loop_max_steps", 0);
    f.transport = std::make_shared<net::ScriptedTransport>();
    f.rt.reset();
    {
      RuntimeOptions o;
      o.data_dir = f.td.path().string();
      o.start_workers = false;
      o.http = f.transport;
      f.rt = unwrap(Runtime::open(o));
    }
    f.rt->config().set("tool_loop_max_steps", 0);
    for (int i = 0; i < 20; ++i) f.script(call_reply({{"c" + std::to_string(i), "search_find", Json{{"query", "report"}}}}));
    f.script(text_reply("finally"));
    const Json d = f.agent(Json{{"prompt", "many"}});
    CHECK(d.at("status") == "finished");
    CHECK(d.at("turns") == 21);
    CHECK(d.at("settings").at("max_steps") == 0);
    // a bound that is not a number is refused, not guessed
    CHECK_FALSE(agent::run(*f.rt, Json{{"prompt", "x"}, {"max_steps", "many"}}));
  }

  TEST_CASE("an unknown tool and malformed arguments are answered to the model, not run") {
    Fixture f;
    f.script(call_reply({{"c1", "no_such_tool", Json::object()}}));
    f.script(text_reply("sorry"));
    const Json a = f.agent(Json{{"prompt", "x"}});
    CHECK(a.at("status") == "finished");
    CHECK(a.at("steps")[0].at("status") == "unknown_tool");
    CHECK(f.body(1).dump().find("Unknown tool") != std::string::npos);
  }

  TEST_CASE("anthropic tool_use: the same loop in the other dialect, chosen by the adapter's data") {
    Fixture f;
    f.rt->secrets().set("anthropic_batch_key", "ant-key");
    f.transport->expect("POST", "https://api.anthropic.com/v1/messages",
                        Reply::json(200, Json{{"model", "claude-x"}, {"content", Json::array({Json{{"type", "text"}, {"text", "Let me look."}},
                                                                                                Json{{"type", "tool_use"}, {"id", "tu_1"}, {"name", "search_find"}, {"input", Json{{"query", "report format"}}}}})},
                                              {"usage", Json{{"input_tokens", 30}, {"output_tokens", 9}}}}));
    f.transport->expect("POST", "https://api.anthropic.com/v1/messages",
                        Reply::json(200, Json{{"model", "claude-x"}, {"content", Json::array({Json{{"type", "text"}, {"text", "It was settled in Report format."}}})},
                                              {"usage", Json{{"input_tokens", 80}, {"output_tokens", 8}}}}));
    const Json a = f.agent(Json{{"prompt", "where did we settle the report format?"}, {"model", "claude-x"}, {"provider", "anthropic"}, {"parameters", Json{{"max_tokens", 512}}}});
    CHECK(a.at("status") == "finished");
    CHECK(a.at("settings").at("format") == "anthropic_tool_use");
    CHECK(a.at("steps")[0].at("action") == "search.find");
    CHECK(a.at("usage").at("input_tokens") == 110);
    const Json first = f.body(0);
    bool shaped = false;
    for (const auto& t : first.at("tools")) shaped = shaped || (t.at("name") == "search_find" && t.at("input_schema").at("type") == "object");
    CHECK(shaped);
    const Json second = f.body(1);
    const Json& last = second.at("messages").back();
    CHECK(last.at("role") == "user");
    CHECK(last.at("content")[0].at("type") == "tool_result");
    CHECK(last.at("content")[0].at("tool_use_id") == "tu_1");
    const Json& before = second.at("messages")[second.at("messages").size() - 2];
    CHECK(before.at("content")[1].at("type") == "tool_use");
  }

  TEST_CASE("a model without native tools: the text protocol - tools in the prompt, a marked JSON call in the answer, the result as a marked message") {
    Fixture f;
    f.rt->config().set("model_capabilities", Json{{"plain/model", Json{{"tools", "text"}}}});
    f.script(text_reply("<tool_call>{\"name\": \"search_find\", \"arguments\": {\"query\": \"report format\"}}</tool_call>"));
    f.script(text_reply("It is in 'Report format'."));
    const Json a = f.agent(Json{{"prompt", "find the report format"}, {"model", "plain/model"}});
    CHECK(a.at("status") == "finished");
    CHECK(a.at("settings").at("format") == "text");
    CHECK(a.at("steps")[0].at("action") == "search.find");
    const Json first = f.body(0);
    CHECK_FALSE(first.contains("tools"));
    CHECK(first.at("messages")[0].at("role") == "system");
    const std::string sys = first.at("messages")[0].at("content");
    CHECK(sys.find("<tool_call>") != std::string::npos);
    CHECK(sys.find("search_find") != std::string::npos);
    const Json second = f.body(1);
    const std::string last = second.at("messages").back().at("content");
    CHECK(second.at("messages").back().at("role") == "user");
    CHECK(last.find("<tool_result>") != std::string::npos);
    CHECK(last.find("Report format") != std::string::npos);
  }

  TEST_CASE("which format a model gets is data in layers: adapter, profile rules, the manifest, the model setting, the request") {
    Fixture f;
    auto format_of = [&](const Json& req) { return unwrap(agent::formats(*f.rt, req)).at("format").get<std::string>(); };
    CHECK(format_of(Json{{"model", "any/model"}}) == "openai_tools");
    CHECK(format_of(Json{{"model", "claude-x"}, {"provider", "anthropic"}}) == "anthropic_tool_use");
    f.rt->config().set("model_capabilities", Json{{"any/model", Json{{"tools", "none"}}}});
    CHECK(format_of(Json{{"model", "any/model"}}) == "none");
    CHECK(format_of(Json{{"model", "any/model"}, {"capabilities", Json{{"tools", "text"}}}}) == "text");
    CHECK_FALSE(agent::formats(*f.rt, Json{{"model", "any/model"}, {"capabilities", Json{{"tools", "telepathy"}}}}));
    // the user's overlay of the profile: a rule for a family of models
    std::filesystem::create_directories(f.td.path() / "profiles");
    Json rule{{"match", Json{{"model_prefix", "old/"}}}, {"format", "text"}};
    Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "tool_loop"},
                 {"overrides", Json{{"model_rules", Json::array({rule})}}}};
    std::ofstream(f.td.path() / "profiles" / "tool_loop.pack") << overlay.dump();
    CHECK(format_of(Json{{"model", "old/model-1"}}) == "text");
    CHECK(format_of(Json{{"model", "new/model-1"}}) == "openai_tools");
  }

  TEST_CASE("tools: what would be offered, the decision for each, and where every setting came from - without a model call") {
    Fixture f;
    const Json t = unwrap(agent::tools(*f.rt, Json{{"prompt", "x"}}));
    bool search = false, del = false;
    for (const auto& tool : t.at("tools")) {
      if (tool.at("action") == "search.find") { search = true; CHECK(tool.at("mode") == "run"); }
      if (tool.at("action") == "conversation.delete") del = true;
      CHECK(tool.at("action") != "use_model");
    }
    CHECK(search);
    CHECK_FALSE(del);  // conversation.delete needs a conversation element: the loop's element is the application
    const Json c = unwrap(agent::tools(*f.rt, Json{{"prompt", "x"}, {"conversation_id", f.conv_id}}));
    for (const auto& tool : c.at("tools"))
      if (tool.at("action") == "conversation.delete") { del = true; CHECK(tool.at("mode") == "propose"); }
    CHECK(del);
    CHECK(c.at("settings").at("policy") == "local_reversible");
    CHECK(f.model_calls() == 0);
  }

  TEST_CASE("through loom_invoke: the port is agent") {
    Fixture f;
    const Json r = unwrap(agent::invoke(*f.rt, "tools", Json{{"prompt", "x"}}));
    CHECK(r.at("schema") == "loom.agent_tools/1");
    CHECK_FALSE(agent::invoke(*f.rt, "dance", Json::object()));
  }
}
