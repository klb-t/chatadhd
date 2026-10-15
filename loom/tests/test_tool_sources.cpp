// S2/R34 (PRZEGLAD_STRAZNIK_SRODOWISKA 3.3, step 8): tool sources as an adapter. A local fake MCP server (tests/fixtures/fake_mcp_server.py,
// stdio) and a scripted HTTP transport stand in for external servers; nothing leaves the machine.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "loom/action_runner.h"
#include "loom/action_table.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/environments.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/tool_sources.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

constexpr const char* kCanary = "CANARY-mcp-token-9d8c7b6a5f4e3d2c1b0a";
constexpr const char* kCanaryEnv = "CANARY-mcp-env-1a2b3c4d5e6f7a8b9c0d";

std::string fixture_server() { return (fs::path(LOOM_TEST_FIXTURES) / "fake_mcp_server.py").string(); }

struct Fixture {
  fsutil::TempDir td;
  std::unique_ptr<Runtime> rt;
  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    rt = unwrap(Runtime::open(o));
  }
  Json call(const char* action, const Json& request = Json::object()) { return unwrap(tool_sources::invoke(*rt, action, request)); }
  Json add_stdio(const std::string& id = "fake", Json extra = Json::object()) {
    Json r{{"id", id}, {"kind", "mcp"}, {"transport", "stdio"}, {"command", "python3"}, {"args", Json::array({fixture_server()})}, {"label", "Fake"}};
    for (auto it = extra.begin(); it != extra.end(); ++it) r[it.key()] = it.value();
    return call("add", r);
  }
  Json catalogue_entry(const std::string& id) {
    for (const auto& a : unwrap(action_catalogue(rt->paths().root)).at("actions"))
      if (a.at("id") == id) return a;
    return Json();
  }
  Json run(const std::string& action, const Json& args) {
    return unwrap(rt->actions().run(Json{{"action", action}, {"element", Json{{"class", "application"}}}, {"args", args}, {"ref", "test"}}));
  }
  std::string everything() {  // every table, the config, the overlays: where a secret must never be
    std::string all;
    for (const auto& n : unwrap(rt->db().list_nodes(std::nullopt, 1000000))) all += n.to_json().dump();
    for (const auto& l : unwrap(rt->db().get_links())) all += l.to_json().dump();
    for (const char* f : {"config.json", "profiles/actions.pack", "profiles/action_rules.pack"}) {
      std::ifstream in(td.path() / f, std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      all += ss.str();
    }
    return all;
  }
};

}  // namespace

TEST_SUITE("tool_sources") {
  TEST_CASE("nothing is contacted or listed until the user adds a source") {
    Fixture f;
    const Json list = f.call("list");
    CHECK(list.at("sources").empty());
    CHECK(list.at("kinds").at(0) == "mcp");
    CHECK(f.catalogue_entry("tool.fake.echo").is_null());
    CHECK(!fs::exists(f.td.path() / "profiles/actions.pack"));
  }

  TEST_CASE("listing a source writes one overlay action per tool, with the input schema as parameters") {
    Fixture f;
    const Json added = f.add_stdio();
    CHECK(added.at("sync").at("ok") == true);
    CHECK(added.at("sync").at("tools") == 5);
    const Json echo = f.catalogue_entry("tool.fake.echo");
    REQUIRE(echo.is_object());
    CHECK(echo.at("port") == "tool_source");
    CHECK(echo.at("action") == "call:fake:echo");
    CHECK(echo.at("status") == "implemented");
    CHECK(echo.at("origin") == "external");
    REQUIRE(echo.at("params").size() == 1);
    CHECK(echo.at("params").at(0).at("name") == "text");
    CHECK(echo.at("params").at(0).at("type") == "string");
    CHECK(echo.at("params").at(0).at("required") == true);
    const Json add = f.catalogue_entry("tool.fake.add");
    CHECK(add.at("params").at(0).at("type") == "number");
    CHECK(add.at("params").at(1).at("has_default") == true);
    CHECK(add.at("title").get<std::string>().find("Add numbers") == 0);
    // the shipped catalogue is untouched; the overlay holds the actions
    CHECK(action_catalogue().dump().find("tool.fake") == std::string::npos);
    CHECK(fs::exists(f.td.path() / "profiles/actions.pack"));
    // listing again is idempotent
    CHECK(f.call("sync").at("sources").at(0).at("removed") == 0);
  }

  TEST_CASE("effects are unknown whatever the server claims; hints are only recorded") {
    Fixture f;
    f.add_stdio();
    const Json echo = f.catalogue_entry("tool.fake.echo");
    CHECK(echo.at("hints").at("readOnlyHint") == true);
    const Json& fx = echo.at("effects");
    CHECK(fx.at("effects_unknown") == true);
    CHECK(fx.at("reads_only") == false);
    CHECK(fx.at("writes") == true);
    CHECK(fx.at("sends_off_device") == true);
    CHECK(f.catalogue_entry("tool.fake.wipe").at("hints").at("destructiveHint") == true);
  }

  TEST_CASE("a run needs confirmation by default; after the user relaxes the rule for one tool it runs; the other tools still ask") {
    Fixture f;
    f.add_stdio();
    const Json ask = f.run("tool.fake.echo", Json{{"text", "hello"}});
    INFO(ask.dump());
    CHECK(ask.at("status") != "completed");
    CHECK(ask.dump().find("echo: hello") == std::string::npos);  // nothing ran
    const Json plan = unwrap(f.rt->actions().preview(Json{{"action", "tool.fake.echo"}, {"element", Json{{"class", "application"}}}, {"args", Json{{"text", "hi"}}}}));
    INFO(plan.dump());
    CHECK(plan.dump().find("needs_confirmation") != std::string::npos);

    const Json trusted = f.call("trust", Json{{"source", "fake"}, {"tool", "echo"}, {"mode", "allow"}});
    CHECK(trusted.at("actions").at(0) == "tool.fake.echo");
    const Json done = f.run("tool.fake.echo", Json{{"text", "hello"}});
    INFO(done.dump());
    // The interpreter reaches the adapter through ActionRunner::invoke_port("tool_source", ...). Until S1 routes that port the step
    // reports it cannot be run as a step; the port itself is exercised below either way.
    const bool routed = done.at("status") == "completed";
    if (routed) CHECK(done.dump().find("echo: hello") != std::string::npos);
    else CHECK(done.dump().find("cannot be run as a step") != std::string::npos);
    CHECK(f.run("tool.fake.wipe", Json{{"what", "x"}}).at("status") != "completed");  // another tool of the source: still asks
    // reset takes the user's rule back out: the preset asks again
    f.call("trust", Json{{"source", "fake"}, {"tool", "echo"}, {"mode", "reset"}});
    CHECK(f.run("tool.fake.echo", Json{{"text", "again"}}).at("status") == ask.at("status"));
    // per source
    f.call("trust", Json{{"source", "fake"}, {"mode", "allow"}});
    CHECK(f.call("list").at("sources").at(0).at("trusted").size() == 5);
    CHECK(f.run("tool.fake.wipe", Json{{"what", "x"}}).at("status") != ask.at("status"));
  }

  TEST_CASE("the port call returns the tool's result and sends only declared arguments") {
    Fixture f;
    f.add_stdio();
    const Json r = f.call("call:fake:echo", Json{{"text", "hello"}, {"conv_id", "c_private"}, {"junk", 1}});
    CHECK(r.at("text") == "echo: hello");
    CHECK(r.at("is_error") == false);
    const Json sum = f.call("call:fake:add", Json{{"a", 2}, {"b", 3}});
    CHECK(sum.at("text") == "5");
    CHECK(sum.at("structured").at("sum") == 5);
    auto failed = tool_sources::invoke(*f.rt, "call:fake:fail", Json::object());
    REQUIRE(!failed);
    CHECK(failed.error().message.find("it broke") != std::string::npos);
    CHECK(!tool_sources::invoke(*f.rt, "call:nosuch:echo", Json::object()));
  }

  TEST_CASE("an unreachable source reports why and keeps the actions it had") {
    Fixture f;
    f.add_stdio();
    Json sources = f.rt->config().get("tool_sources");
    sources["fake"]["command"] = "/nonexistent/program";
    f.rt->config().set("tool_sources", sources);
    const Json s = f.call("sync", Json{{"source", "fake"}});
    CHECK(s.at("sources").at(0).at("ok") == false);
    CHECK(!f.catalogue_entry("tool.fake.echo").is_null());
  }

  TEST_CASE("removing the source removes its actions, rules and graph nodes") {
    Fixture f;
    f.add_stdio();
    f.add_stdio("second");
    f.call("trust", Json{{"source", "fake"}, {"mode", "allow"}});
    const Json gone = f.call("remove", Json{{"id", "fake"}});
    CHECK(gone.at("removed_actions").size() == 5);
    CHECK(gone.at("removed_rules").size() == 5);
    CHECK(f.catalogue_entry("tool.fake.echo").is_null());
    CHECK(!f.catalogue_entry("tool.second.echo").is_null());  // the other source is untouched
    CHECK(f.everything().find("tool_source:fake") == std::string::npos);
    CHECK(f.everything().find("environment:service:tool_source:fake") == std::string::npos);
    CHECK(f.everything().find("environment:service:tool_source:second") != std::string::npos);
    CHECK(f.call("list").at("sources").size() == 1);
    f.call("remove", Json{{"id", "second"}});
    CHECK(f.catalogue_entry("tool.second.echo").is_null());
    CHECK(f.call("list").at("sources").empty());
    CHECK(!tool_sources::invoke(*f.rt, "remove", Json{{"id", "second"}}));
  }

  TEST_CASE("in the graph a source is a service environment with a capability provided by the port and an access that references the vault") {
    Fixture f;
    f.rt->secrets().set("fake_token", kCanary);
    REQUIRE(bool(f.rt->secrets().save()));
    f.add_stdio("fake", Json{{"access", Json{{"secret", "fake_token"}}}});
    auto env = unwrap(f.rt->db().find_node("environment:service:tool_source:fake", "environment"));
    REQUIRE(env);
    CHECK(env->metadata.at("class") == "service_environment");
    auto cap = unwrap(f.rt->db().find_node("capability:service:tool_source:fake:tools", "capability"));
    auto acc = unwrap(f.rt->db().find_node("access:service:tool_source:fake:vault", "access"));
    auto port = unwrap(f.rt->db().find_node("port:tool_source", "port"));
    REQUIRE(cap);
    REQUIRE(acc);
    REQUIRE(port);
    CHECK(acc->metadata.at("refs").at(0).at("secret_ref") == "fake_token");
    CHECK(acc->metadata.at("refs").at(0).at("state") == "set");
    bool provided = false, stored = false, accessed = false;
    for (const auto& l : unwrap(f.rt->db().get_links())) {
      provided = provided || (l.src == cap->id && l.dst == port->id && l.link_type == "provided_by");
      accessed = accessed || (l.src == env->id && l.dst == acc->id && l.link_type == "accessed_via");
      stored = stored || (l.src == acc->id && l.link_type == "stored_in");
    }
    CHECK(provided);
    CHECK(accessed);
    CHECK(stored);
    // the environments of R32 see it
    const Json envs = unwrap(environments::list(*f.rt, Json{{"sync", true}}));
    bool seen = false;
    for (const auto& e : envs.at("environments")) seen = seen || e.dump().find("tool_source:fake") != std::string::npos;
    CHECK(seen);
  }

  TEST_CASE("secrets are references: the source accepts a vault key name, never the key; no secret bytes anywhere") {
    Fixture f;
    f.rt->secrets().set("fake_token", kCanary);
    f.rt->secrets().set("fake_env_key", kCanaryEnv);
    REQUIRE(bool(f.rt->secrets().save()));
    // a key typed into the access is refused
    auto bad = tool_sources::invoke(*f.rt, "add", Json{{"id", "bad"}, {"transport", "http"}, {"url", "http://x/mcp"}, {"access", Json{{"token", kCanary}}}});
    REQUIRE(!bad);
    CHECK(bad.error().message.find(kCanary) == std::string::npos);
    const Json added = f.add_stdio("fake", Json{{"access", Json{{"secret", "fake_token"}}}, {"env", Json{{"FAKE_SECRET", Json{{"secret", "fake_env_key"}}}}}});
    const Json present = f.call("call:fake:env_present", Json{{"name", "FAKE_SECRET"}});
    CHECK(present.at("text") == "present");  // the process got it
    std::string answers = added.dump() + f.call("list").dump() + f.call("sync").dump() + present.dump() +
                          unwrap(environments::list(*f.rt, Json{{"sync", true}})).dump();
    CHECK(answers.find(kCanary) == std::string::npos);
    CHECK(answers.find(kCanaryEnv) == std::string::npos);
    const std::string all = f.everything();
    CHECK(all.find(kCanary) == std::string::npos);
    CHECK(all.find(kCanaryEnv) == std::string::npos);
    CHECK(all.find("fake_token") != std::string::npos);  // the reference is there
    // a missing key is said, by name
    f.rt->secrets().erase("fake_env_key");
    const Json s = f.call("sync", Json{{"source", "fake"}});
    CHECK(s.at("sources").at(0).at("ok") == false);
    CHECK(s.at("sources").at(0).at("error").get<std::string>().find("fake_env_key") != std::string::npos);
  }

  TEST_CASE("the streamable HTTP transport: JSON and SSE answers, session header, bearer from the vault") {
    Fixture f;
    f.rt->secrets().set("http_token", kCanary);
    REQUIRE(bool(f.rt->secrets().save()));
    auto t = std::make_shared<net::ScriptedTransport>();
    using R = net::ScriptedTransport::Reply;
    R init = R::json(200, Json{{"jsonrpc", "2.0"}, {"id", 1}, {"result", Json{{"protocolVersion", "2025-06-18"}, {"capabilities", Json::object()}}}});
    init.headers.emplace_back("Mcp-Session-Id", "sess-1");
    t->expect("POST", "https://tools.example/mcp", init);
    t->expect("POST", "https://tools.example/mcp", R::text(202, ""));
    t->expect("POST", "https://tools.example/mcp",
              R::json(200, Json{{"jsonrpc", "2.0"}, {"id", 2}, {"result", Json{{"tools", Json::array({Json{{"name", "ping"}, {"description", "Ping"}, {"inputSchema", Json{{"type", "object"}, {"properties", Json{{"host", Json{{"type", "string"}}}}}}}}})}}}}));
    t->expect("DELETE", "https://tools.example/mcp", R::text(405, ""));
    f.rt->set_http_transport(t);
    f.call("add", Json{{"id", "web"}, {"transport", "http"}, {"url", "https://tools.example/mcp?key=urlsecret"}, {"access", Json{{"secret", "http_token"}}}});
    CHECK(!f.catalogue_entry("tool.web.ping").is_null());
    const auto first = t->requests();
    REQUIRE(first.size() >= 3);
    CHECK(net::header_value(first[0].headers, "Authorization") == std::string("Bearer ") + kCanary);  // the bytes reach the wire only
    CHECK(net::header_value(first[1].headers, "Mcp-Session-Id") == "sess-1");
    CHECK(net::header_value(first[2].headers, "MCP-Protocol-Version") == "2025-06-18");
    // the same answer as an SSE stream
    t = std::make_shared<net::ScriptedTransport>();
    t->expect("POST", "https://tools.example/mcp", init);
    t->expect("POST", "https://tools.example/mcp", R::text(202, ""));
    t->expect("POST", "https://tools.example/mcp",
              R::sse({Json{{"jsonrpc", "2.0"}, {"id", 2}, {"result", Json{{"tools", Json::array({Json{{"name", "pong"}, {"inputSchema", Json::object()}}})}}}}.dump()}));
    t->expect("DELETE", "https://tools.example/mcp", R::text(405, ""));
    f.rt->set_http_transport(t);
    const Json again = f.call("sync", Json{{"source", "web"}});
    CHECK(again.at("sources").at(0).at("ok") == true);
    CHECK(!f.catalogue_entry("tool.web.pong").is_null());
    CHECK(f.catalogue_entry("tool.web.ping").is_null());  // a tool the server no longer lists is gone
    CHECK(f.everything().find(kCanary) == std::string::npos);
    CHECK(f.everything().find("urlsecret") == std::string::npos);
  }

  TEST_CASE("an unknown kind is refused with the known ones; the kind registry takes more kinds as data plus an adapter") {
    Fixture f;
    auto bad = tool_sources::invoke(*f.rt, "add", Json{{"id", "x"}, {"kind", "carrier_pigeon"}, {"transport", "stdio"}, {"command", "x"}});
    REQUIRE(!bad);
    CHECK(bad.error().message.find("known: mcp") != std::string::npos);
    CHECK(tool_sources::registry().contains("mcp"));
  }
}
TEST_CASE("zz debug") {
  Fixture f;
  f.add_stdio();
  std::ifstream in(f.td.path() / "profiles/actions.pack");
  std::stringstream ss; ss << in.rdbuf();
  MESSAGE(ss.str().substr(0, 1500));
  auto cat = action_catalogue(f.rt->paths().root);
  MESSAGE(cat ? std::to_string(cat->at("actions").size()) : cat.error().message);
}
