// P14 7: "confirmed": true from a request body does not skip the proposal. Over HTTP/JNI/CLI a confirmation is honoured only together with
// the plan_receipt that a preview or a proposal gave for exactly this plan; a bare confirmed:true returns the proposal with its receipt.
// The rules (denials) and the x10 usage guard still apply after the receipt.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_runner.h"
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
    rt->config().set("model_registry_autorefresh", "off");
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    add("user", "We should ship on Friday.");
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void script(int n) {
    for (int i = 0; i < n; ++i)
      transport->expect("POST", "https://api.test/chat/completions",
                        net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", "A short summary."}}}}})},
                                                                       {"usage", Json{{"total_tokens", 7}}}}));
  }
  std::filesystem::path overlay_file() { return td.path() / "profiles" / "action_rules.pack"; }
  Json overlay_rules() {
    if (!std::filesystem::exists(overlay_file())) return Json::object();
    std::ifstream in(overlay_file());
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return unwrap(json::parse(bytes)).at("overrides").value("rules", Json::object());
  }
  // The write that the preset asks to confirm: a rule for this conversation.
  Json rule_request(const std::string& action = "depict") {
    return Json{{"scope", "conversation"}, {"scope_id", conv_id}, {"effect", "confirm"}, {"action", action}, {"ref", "api"}};
  }
};

}  // namespace

TEST_SUITE("confirm_receipt") {
  TEST_CASE("a bare confirmed:true is not the yes: the plan comes back as a proposal with its receipt and nothing is written") {
    Fixture f;
    Json request = f.rule_request();
    request["confirmed"] = true;
    const Json bare = unwrap(f.rt->actions().invoke("rules.set", request));
    INFO(bare.dump());
    CHECK(bare.at("status") == "proposed");
    CHECK(bare.at("confirmation_refused").at("reason") == "receipt_required");
    CHECK(bare.at("pending").at("kind") == "proposal");
    CHECK(bare.at("pending").at("receipt_id").get<std::string>().rfind("arr_", 0) == 0);
    CHECK(bare.at("plan_receipt").get<std::string>().rfind("arp_", 0) == 0);
    CHECK(bare.at("pending").at("plan_receipt") == bare.at("plan_receipt"));
    CHECK(bare.at("confirmations").empty());
    CHECK_FALSE(std::filesystem::exists(f.overlay_file()));
    // the proposal is answered as any proposal is: with its own receipt
    const Json answered = unwrap(f.rt->actions().confirm(Json{{"run_id", bare.at("id")}, {"approved", true}, {"receipt_id", bare.at("pending").at("receipt_id")}, {"ref", "api"}}));
    CHECK(answered.at("status") == "completed");
    CHECK(f.overlay_rules().size() == 1);
  }

  TEST_CASE("the receipt of the plan that was shown lets the request run; the receipt of another plan does not") {
    Fixture f;
    // the proposal of "confirm depict" gives the receipt of that plan
    Json ask = f.rule_request();
    ask["propose"] = true;
    const Json shown = unwrap(f.rt->actions().invoke("rules.set", ask));
    REQUIRE(shown.at("status") == "proposed");
    const std::string receipt = shown.at("plan_receipt");
    // a different plan (another action in the rule) with that receipt: refused, proposed again, nothing written
    Json other = f.rule_request("narrate");
    other["confirmed"] = true;
    other["receipt_id"] = receipt;
    const Json mismatch = unwrap(f.rt->actions().invoke("rules.set", other));
    CHECK(mismatch.at("status") == "proposed");
    CHECK(mismatch.at("confirmation_refused").at("reason") == "receipt_mismatch");
    CHECK(mismatch.at("plan_receipt") != receipt);
    CHECK_FALSE(std::filesystem::exists(f.overlay_file()));
    // a receipt that is not one: the same
    other["receipt_id"] = "arr_not_a_plan_receipt";
    CHECK(unwrap(f.rt->actions().invoke("rules.set", other)).at("confirmation_refused").at("reason") == "receipt_mismatch");
    // the same request as was shown, with its receipt: runs, and the run says whose receipt it was
    Json yes = f.rule_request();
    yes["confirmed"] = true;
    yes["receipt_id"] = receipt;
    const Json done = unwrap(f.rt->actions().invoke("rules.set", yes));
    INFO(done.dump());
    CHECK(done.at("status") == "completed");
    CHECK_FALSE(done.contains("confirmation_refused"));
    CHECK(done.at("confirmations").at(0).at("kind") == "request");
    CHECK(done.at("confirmations").at(0).at("receipt_id") == receipt);
    CHECK(done.at("plan_receipt") == receipt);
    CHECK(f.overlay_rules().size() == 1);
  }

  TEST_CASE("actions.preview gives the receipt of a plan that is run directly: the same request, with confirmed and the receipt") {
    Fixture f;
    // a rule of the user: every summary asks first
    unwrap(loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "person"}, {"effect", "confirm"}, {"action", "summarise"}, {"ref", "api"}}));
    Json request{{"action", "summarise"}, {"element", f.conv_id}, {"ref", "api"}};
    const Json preview = unwrap(f.rt->actions().preview(request));
    CHECK(preview.at("needs_confirmation").size() == 1);
    const std::string receipt = preview.at("plan_receipt");
    // bare: proposed, no model call
    request["confirmed"] = true;
    const Json bare = unwrap(f.rt->actions().run(request));
    CHECK(bare.at("status") == "proposed");
    CHECK(bare.at("plan_receipt") == receipt);
    CHECK(f.transport->requests().empty());
    // the receipt of the preview: the run goes
    f.script(1);
    request["receipt_id"] = receipt;
    const Json done = unwrap(f.rt->actions().run(request));
    CHECK(done.at("status") == "completed");
    CHECK(f.transport->requests().size() == 1);
    // what was shown is what is run: another argument is another plan, the earlier receipt does not cover it
    f.script(1);
    request["args"] = Json{{"lang", "en"}};
    const Json other = unwrap(f.rt->actions().run(request));
    CHECK(other.at("status") == "proposed");
    CHECK(other.at("confirmation_refused").at("reason") == "receipt_mismatch");
    CHECK(other.at("plan_receipt") != receipt);
    CHECK(f.transport->requests().size() == 1);
  }

  TEST_CASE("after the receipt the rules and the x10 guard still apply") {
    Fixture f;
    unwrap(loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "person"}, {"effect", "confirm"}, {"action", "summarise"}, {"ref", "api"}}));
    // a denial is not lifted by a receipt
    unwrap(loom::test::invoke_confirmed(*f.rt, "rules.set", Json{{"scope", "person"}, {"effect", "deny"}, {"action", "narrate"}, {"ref", "api"}}));
    Json narrate{{"action", "narrate"}, {"element", f.conv_id}, {"ref", "api"}};
    CHECK(unwrap(f.rt->actions().run(narrate)).at("status") == "denied");
    narrate["confirmed"] = true;
    narrate["receipt_id"] = unwrap(f.rt->actions().preview(Json{{"action", "narrate"}, {"element", f.conv_id}})).at("plan_receipt");
    CHECK(unwrap(f.rt->actions().run(narrate)).at("status") == "denied");
    CHECK(f.transport->requests().empty());
    // the x10 guard: three small summaries set the baseline, the receipt covers the rule's question, not the guard's
    f.script(3);
    for (int i = 0; i < 3; ++i) REQUIRE(unwrap(loom::test::run_confirmed(*f.rt, Json{{"action", "summarise"}, {"element", f.conv_id}, {"ref", "api"}})).at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 3);
    const std::string big = f.add("user", std::string(60000, 'x'));
    f.script(1);
    const Json held = unwrap(loom::test::run_confirmed(*f.rt, Json{{"action", "summarise"}, {"element", big}, {"ref", "api"}}));
    CHECK(held.at("status") == "awaiting_usage");
    CHECK(held.at("pending").at("kind") == "usage");
    CHECK(f.transport->requests().size() == 3);  // nothing left the machine
  }
}
