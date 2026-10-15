// P11 2: scoped settings and rules from the conversation, the CLI's port and the API. "w tej rozmowie ustaw X=Y",
// "dla mnie zawsze pytaj przed depict", "for this conversation never run depict without asking" become a proposal to write a
// rule into the user overlay profiles/action_rules.pack. Writing is an action like any other (the preset rule asks for a
// confirmation), a rule pinned by a higher scope refuses with its reason, and the engine's explanations show the new rule.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_overlay.h"
#include "loom/action_runner.h"
#include "loom/action_table.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"
#include "test_receipts.h"

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
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    add(conv_id, "user", "We should ship on Friday.");
    add(conv_id, "assistant", "Agreed, Friday works if the tests pass.");
  }
  std::string add(const std::string& conv, const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void script(int n, const std::string& content = "A short summary.") {
    for (int i = 0; i < n; ++i)
      transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content)));
  }
  Json route(const std::string& text) { return unwrap(chat_route::route(*rt, Json{{"text", text}, {"conv_id", conv_id}})); }
  chat_route::Outcome input(const std::string& text, const std::string& conv = "") {
    ChatOptions opts;
    opts.conv_id = conv.empty() ? conv_id : conv;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  Json meta(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->metadata; }
  std::string text_of(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->text; }
  std::filesystem::path overlay_file() { return td.path() / "profiles" / "action_rules.pack"; }
  Json overlay_rules() {
    if (!std::filesystem::exists(overlay_file())) return Json::object();
    std::ifstream in(overlay_file());
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return unwrap(json::parse(bytes)).at("overrides").value("rules", Json::object());
  }
  void overlay(const std::string& domain, const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(td.path() / "profiles" / (domain + ".pack"))
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
  }
};

}  // namespace

TEST_SUITE("rules_from_chat") {
  TEST_CASE("a scoped value from the conversation: proposal, the preset's confirmation, the overlay, and the effect on this conversation only") {
    Fixture f;
    const auto proposal = f.input("w tej rozmowie ustaw action_result_target=conversation_message");
    REQUIRE(proposal.handled);
    const Json p = f.meta(proposal).at("action_proposal");
    CHECK(p.at("action") == "rules.set");
    CHECK(p.at("args").at("scope") == "conversation");
    CHECK(p.at("args").at("scope_id") == f.conv_id);
    CHECK(p.at("args").at("key") == "action_result_target");
    CHECK(p.at("args").at("value") == "conversation_message");
    CHECK(p.at("args").at("effect") == "set_value");
    CHECK(p.at("needs_confirmation").size() == 1);  // the preset rule: every write of a rule is confirmed
    CHECK(p.at("needs_confirmation").at(0).at("action") == "rules.set");
    CHECK(p.at("rule").at("target") == Json{{"kind", "setting"}, {"key", "action_results.target"}});
    CHECK(f.text_of(proposal).find("Zasady każą Cię zapytać") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(f.overlay_file()));  // a proposal writes nothing

    const auto done = f.input("tak");
    REQUIRE(done.handled);
    const std::string id = "user:conversation:" + f.conv_id + ":value:action_results.target";
    const Json rules = f.overlay_rules();
    REQUIRE(rules.contains(id));
    CHECK(rules.at(id).at("scope") == "conversation");
    CHECK(rules.at(id).at("scope_id") == f.conv_id);
    CHECK(rules.at(id).at("effect") == "set_value");
    CHECK(rules.at(id).at("value") == "conversation_message");
    CHECK(rules.at(id).at("initiator") == "user");
    CHECK(rules.at(id).at("provenance").at("origin") == "chat");
    CHECK(rules.at(id).at("provenance").at("conv_id") == f.conv_id);
    CHECK(rules.at(id).at("provenance").at("message_id").get<std::string>().rfind("m_", 0) == 0);

    // The rule is in force here: a summary now posts its result as a message of the conversation ...
    f.script(2);
    f.input("Streść tę rozmowę");
    const auto ran = f.input("tak");
    const Json produced = ran.result.at("routed").at("run").at("produced");
    CHECK(produced.at("target") == "conversation_message");
    // ... and nowhere else.
    const std::string other = unwrap(f.rt->db().create_conv("Other")).id;
    f.add(other, "user", "Another thread.");
    f.input("Streść tę rozmowę", other);
    const auto elsewhere = f.input("tak", other);
    CHECK(elsewhere.result.at("routed").at("run").at("produced").at("target") == "graph");

    // The engine's own explanation names the rule as the source of the value.
    const Json listed = unwrap(f.rt->actions().invoke("rules.list", Json{{"user_only", true}}));
    REQUIRE(listed.at("rules").size() == 1);
    CHECK(listed.at("rules").at(0).at("id") == id);
    CHECK(listed.at("rules").at(0).at("source") == "overlay");
  }

  TEST_CASE("a gate from the conversation, in English and Polish; explain and actions.for show the new rule as the source") {
    Fixture f;
    const std::string mid = f.add(f.conv_id, "user", "Plan B is to ship on Monday.");
    const auto proposal = f.input("for this conversation never run summarise without asking");
    REQUIRE(proposal.handled);
    const Json p = f.meta(proposal).at("action_proposal");
    CHECK(p.at("action") == "rules.set");
    CHECK(p.at("args").at("effect") == "confirm");
    CHECK(p.at("args").at("action") == "summarise");
    CHECK(p.at("args").at("scope") == "conversation");
    // before the yes nothing asks for confirmation of a summary ...
    const Json scope = Json{{"conversation", f.conv_id}, {"turn", ""}};
    auto summary_status = [&] {
      const Json rows = unwrap(f.rt->actions().invoke("for", Json{{"element", mid}, {"scope", scope}}));
      for (const auto& r : rows.at("actions"))
        if (r.at("action") == "summarise") return r;
      return Json();
    };
    CHECK(summary_status().at("status") == "applicable");
    f.input("yes");
    // ... after it, in this conversation, one does, and the row says which rule asked and who stands behind it.
    const Json row = summary_status();
    CHECK(row.at("status") == "needs_confirmation");
    const std::string id = "user:conversation:" + f.conv_id + ":gate:summarise";
    CHECK(row.at("explanation").at("decided_by").at("rule") == id);
    const Json explained = unwrap(f.rt->actions().invoke("explain", Json{{"action", "summarise"}, {"element", mid}, {"scope", scope}}));
    bool found = false;
    for (const auto& r : explained.at("rules"))
      if (r.at("id") == id) {
        found = true;
        CHECK(r.at("role") == "decided");
        CHECK(r.at("initiator") == "user");
      }
    CHECK(found);
    // another conversation is not asked
    const std::string other = unwrap(f.rt->db().create_conv("Other")).id;
    const Json elsewhere = unwrap(f.rt->actions().invoke("for", Json{{"element", mid}, {"scope", Json{{"conversation", other}, {"turn", ""}}}}));
    for (const auto& r : elsewhere.at("actions"))
      if (r.at("action") == "summarise") CHECK(r.at("status") == "applicable");
    // the chat shows the question the rule asks
    const Json routed = f.route("Summarise this conversation");
    CHECK(routed.at("command").at("needs_confirmation").at(0).at("initiator") == "user");

    // Polish, for me (person scope): the same action, another level
    const auto pl = f.input("dla mnie zawsze pytaj przed narysuj");
    REQUIRE(pl.handled);
    const Json pp = f.meta(pl).at("action_proposal");
    CHECK(pp.at("args").at("scope") == "person");
    CHECK(pp.at("args").at("effect") == "confirm");
    CHECK(pp.at("args").at("action") == "depict");
    CHECK_FALSE(pp.at("args").contains("scope_id"));
  }

  TEST_CASE("deny and allow, with a reason; a word that names no action is a message, nothing is guessed") {
    Fixture f;
    const Json deny = f.route("dla tej rozmowy nigdy nie uruchamiaj depict");
    CHECK(deny.at("command").at("args").at("effect") == "deny");
    CHECK(deny.at("command").at("rule").at("reason").at("en") == "Set by you.");
    const Json allow = f.route("in this conversation allow summarise");
    CHECK(allow.at("command").at("args").at("effect") == "allow");
    CHECK(f.route("for this conversation never run frobnicate without asking").at("kind") == "message");
    CHECK(f.route("never run depict without asking").at("command").at("rule").at("scope").get<std::string>() == "person");
    CHECK_FALSE(f.route("never run depict without asking").at("command").at("args").contains("scope_id"));
    CHECK(f.route("how do I make a rule for this conversation?").at("kind") == "message");
    // a deny written and confirmed stops the action in that conversation, with the stated reason
    f.input("for this conversation never run summarise");
    f.input("yes");
    const Json routed = f.route("Summarise this conversation");
    CHECK(routed.at("command").at("denied").at(0).at("initiator") == "user");
    CHECK(routed.at("command").at("runnable") == false);
  }

  TEST_CASE("switching a rule off removes its effect; the record stays; an own rule can be restored") {
    Fixture f;
    f.input("for this conversation never run summarise without asking");
    f.input("yes");
    const std::string id = "user:conversation:" + f.conv_id + ":gate:summarise";
    CHECK(f.route("Summarise this conversation").at("command").at("needs_confirmation").size() == 1);
    const auto off = f.input("disable rule " + id);
    REQUIRE(off.handled);
    CHECK(f.meta(off).at("action_proposal").at("action") == "rules.disable");
    f.input("yes");
    CHECK(f.overlay_rules().at(id).at("disabled") == true);
    CHECK(f.route("Summarise this conversation").at("command").at("needs_confirmation").size() == 0);
    const Json listed = unwrap(f.rt->actions().invoke("rules.list", Json{{"scope", "conversation"}}));
    REQUIRE(listed.at("rules").size() == 1);
    CHECK(listed.at("rules").at(0).at("disabled") == true);
    // restore: the switch-off is taken back
    unwrap(loom::test::invoke_confirmed(*f.rt, "rules.disable", Json{{"id", id}, {"restore", true}, {"confirmed", true}}));
    CHECK(f.route("Summarise this conversation").at("command").at("needs_confirmation").size() == 1);
    // a shipped rule is switched off by an override of the overlay, and nothing in the shipped pack changes
    unwrap(loom::test::invoke_confirmed(*f.rt, "rules.disable", Json{{"id", "confirm:rules.set"}, {"confirmed", true}}));
    CHECK(f.overlay_rules().at("confirm:rules.set") == Json{{"disabled", true}});
    const auto no_question = f.input("w tej rozmowie ustaw action_result_target=graph");
    CHECK(f.meta(no_question).at("action_proposal").at("needs_confirmation").size() == 0);
  }

  TEST_CASE("the rule that was written is kept on its proposal, and one call (rulesDisable) takes it back; the proposal then says so") {
    Fixture f;
    const auto proposal = f.input("for this conversation never run summarise without asking");
    const std::string proposal_id = proposal.assistant_message_id;
    const auto done = f.input("yes");
    REQUIRE(done.handled);
    const std::string id = "user:conversation:" + f.conv_id + ":gate:summarise";
    const Json written = unwrap(f.rt->db().get_msg(proposal_id))->metadata.at("action_proposal").at("written");
    CHECK(written.at("kind") == "rule");
    CHECK(written.at("id") == id);
    CHECK(written.at("overlays") == Json::array({"profiles/action_rules.pack"}));
    CHECK(written.at("entry").at("effect") == "confirm");
    CHECK(written.at("entry").at("provenance").at("conv_id") == f.conv_id);
    CHECK(unwrap(f.rt->db().get_msg(proposal_id))->metadata.at("action_proposal").at("status") == "executed");
    // the click: the removal the web sends is a rules.disable that the user's click confirms
    const Json run = unwrap(loom::test::invoke_confirmed(*f.rt, "rules.disable", Json{{"id", id}, {"confirmed", true}, {"ref", "web"}}));
    CHECK(run.at("status") == "completed");
    CHECK(f.overlay_rules().at(id).at("disabled") == true);
    const Json after = unwrap(f.rt->db().get_msg(proposal_id))->metadata.at("action_proposal");
    CHECK(after.at("status") == "undone");
    CHECK(after.at("written").at("undone") == true);
    // taken back from somewhere else (restore): the proposal is the writer again
    unwrap(loom::test::invoke_confirmed(*f.rt, "rules.disable", Json{{"id", id}, {"restore", true}, {"confirmed", true}}));
    const Json restored = unwrap(f.rt->db().get_msg(proposal_id))->metadata.at("action_proposal");
    CHECK(restored.at("status") == "executed");
    CHECK(restored.at("written").at("undone") == false);
    // a rule that was not written from a conversation has no message to tell: nothing breaks
    unwrap(loom::test::invoke_confirmed(*f.rt, "rules.disable", Json{{"id", "confirm:rules.set"}, {"confirmed", true}}));
  }

  TEST_CASE("a rule pinned by a higher scope refuses, with its reason and who pinned it; so does switching it off") {
    Fixture f;
    f.overlay("action_rules", Json{{"rules", Json{{"org_pin", Json{{"id", "org_pin"}, {"scope", "installation"}, {"target", Json{{"kind", "any"}}},
                                                                    {"action", "depict"}, {"effect", "deny"}, {"locked", true}, {"conflict", "locked_by"},
                                                                    {"initiator", "the administrator"},
                                                                    {"reason", Json{{"en", "Images are not licensed on this installation."}, {"pl", "Obrazy nie są licencjonowane w tej instalacji."}}}}}}}});
    const Json refused = f.route("for this conversation allow depict");
    REQUIRE(refused.at("kind") == "command");
    CHECK(refused.at("command").at("feasible") == false);
    CHECK(refused.at("command").at("reason").at("code") == "refused");
    CHECK(refused.at("command").at("reason").at("pin").at("rule") == "org_pin");
    const auto out = f.input("dla tej rozmowy pozwalaj na narysuj");
    REQUIRE(out.handled);
    const std::string said = f.text_of(out);
    CHECK(said.find("Obrazy nie są licencjonowane") != std::string::npos);
    CHECK(said.find("the administrator") != std::string::npos);
    CHECK(f.meta(out).at("action_proposal").at("status") == "unavailable");
    // the same through the API: a refusal, nothing written
    auto direct = loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "conversation"}, {"scope_id", f.conv_id}, {"effect", "allow"}, {"action", "depict"}, {"confirmed", true}});
    REQUIRE(direct);
    CHECK(direct->at("status") == "failed");
    CHECK(direct->at("error").at("message").get<std::string>().find("pinned by the administrator") != std::string::npos);
    CHECK(f.overlay_rules().size() == 1);  // only the pin the test wrote
    // a shipped locked rule cannot be switched off either
    const Json off = f.route("disable rule edit:config:loom_usage_policy");
    CHECK(off.at("command").at("reason").at("code") == "refused");
    CHECK(off.at("command").at("reason").at("pin").at("initiator") == "engine");
    // a rule below the pin that does not contradict it is fine
    CHECK(f.route("for this conversation never run depict without asking").at("command").at("reason").at("code") == "refused");  // a confirmation contradicts the pinned deny
    const Json fine = f.route("for this conversation never run depict");
    CHECK(fine.at("command").at("feasible") == true);  // a deny says the same as the pin
  }

  TEST_CASE("rules.set through the API and loom_invoke is gated like any command; the port and HTTP rows are in the action table") {
    Fixture f;
    // not confirmed: the run waits for the answer the preset rule asks for
    const Json waiting = unwrap(f.rt->actions().invoke(
        "rules.set", Json{{"scope", "installation"}, {"key", "extrapolation.max_cost"}, {"value", 3}, {"reason", "tight budget"}}));
    CHECK(waiting.at("status") == "awaiting_confirmation");
    CHECK_FALSE(std::filesystem::exists(f.overlay_file()));
    const Json done = unwrap(f.rt->actions().confirm(Json{{"run_id", waiting.at("id")}, {"approved", true}, {"receipt_id", waiting.at("pending").at("receipt_id")}}));
    CHECK(done.at("status") == "completed");
    const Json rules = f.overlay_rules();
    const std::string id = "user:installation:value:extrapolation.max_cost";
    REQUIRE(rules.contains(id));
    CHECK(rules.at(id).at("value") == 3);  // a number stays a number
    CHECK(rules.at(id).at("reason") == "tight budget");
    // the chain bounds now come from the installation-level rule, and say so
    const Json chain = unwrap(f.rt->actions().invoke("propose_chain", Json{{"element", "message"}, {"target_action", "transcribe"}}));
    CHECK(chain.at("resolved").at("max_cost").at("rule") == id);
    // the setting name works as well as the scoped key; a setting nobody reads per scope yet is written with a note
    const Json other = unwrap(loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "person"}, {"key", "extrapolation_preset"}, {"value", "generous"}, {"confirmed", true}}));
    CHECK(other.at("status") == "completed");
    CHECK(f.overlay_rules().contains("user:person:value:extrapolation.preset"));
    const Json unread = unwrap(loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "person"}, {"key", "max_tokens"}, {"value", 2048}, {"confirmed", true}}));
    CHECK(unread.at("result").at("data").at("note").at("consumer").get<std::string>().find("nothing reads") != std::string::npos);
    // errors are said: an unknown level, an unknown setting, an unknown action, a deny of nothing
    CHECK_FALSE(unwrap(loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "galaxy"}, {"key", "x"}, {"value", 1}, {"confirmed", true}})).at("status") == "completed");
    CHECK_FALSE(unwrap(loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "person"}, {"key", "no_such_setting"}, {"value", 1}, {"confirmed", true}})).at("status") == "completed");
    CHECK_FALSE(unwrap(loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "person"}, {"effect", "deny"}, {"action", "frobnicate"}, {"confirmed", true}})).at("status") == "completed");
    CHECK_FALSE(unwrap(loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "conversation"}, {"key", "action_result_target"}, {"value", "graph"}, {"confirmed", true}})).at("status") == "completed");
    // the catalogue rows: loom_invoke port actions, HTTP routes
    std::set<std::string> paths;
    for (const auto& row : action_table())
      if (row.value("action", "").rfind("rules.", 0) == 0) paths.insert(row.at("path"));
    CHECK(paths == std::set<std::string>{"/api/rules/disable", "/api/rules/list", "/api/rules/set"});
  }
}
