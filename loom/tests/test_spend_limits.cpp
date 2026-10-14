// P17 2: spending limits per period (the owner's rule: the user decides, a preset is not a rule). The setting `usage_spend_limits` is a list of
// {period, amount, currency, on_exceed}; the preset is none. Before an action run that calls a model, the runner adds the spend of the period to the
// estimate of the run and applies `on_exceed`; the x10 guard is separate. The limits are set and read back from the conversation.
#include <doctest/doctest.h>

#include <ctime>
#include <filesystem>
#include <fstream>

#include "loom/action_runner.h"
#include "loom/action_spend.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/providers.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace spend = loom::actions::spend;

namespace {

Json reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 7}}}};
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id, message_id;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("model_registry_autorefresh", "off");  // these tests count the transport's requests
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    NewMessage m;
    m.conv_id = conv_id;
    m.role = "user";
    m.text = "We should ship on Friday. The tests must pass first, and the release notes need a second pair of eyes.";
    message_id = unwrap(rt->db().create_msg(m));
  }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  Json meta(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->metadata; }
  std::string text_of(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->text; }
  chat_route::Outcome say_and_confirm(const std::string& sentence, const std::string& yes) {
    const auto proposal = input(sentence);
    REQUIRE_MESSAGE(proposal.handled, sentence);
    const Json p = meta(proposal).at("action_proposal");
    CHECK_MESSAGE(p.at("action") == "usage.set_limit", sentence << " -> " << p.value("action", ""));
    CHECK_MESSAGE(p.at("status") == "pending", sentence);
    return input(yes);
  }
  // A completed operation of the ledger that reported `usd` (now).
  void spend_usd(double usd, const std::string& op = "op1") {
    auto policy = unwrap(UsagePolicy::open(td.path() / "usage-policy.sqlite", Json::object()));
    Json estimate{{"operation_id", op}, {"baseline_key", "chat.send|test|m"}, {"model", "test/model"}, {"resources", Json{{"output_tokens", nullptr}, {"cost_usd", nullptr}}}};
    unwrap(policy->request(estimate));
    unwrap(policy->complete(op, Json{{"resources", Json{{"requests", 1}, {"output_tokens", 120}, {"cost_usd", usd}}}, {"provenance", "provider_reported"}}));
  }
  void limits(const Json& list) {
    rt->config().set("usage_spend_limits", list);
    unwrap(rt->config().save());
  }
  Json summarise_preview() { return unwrap(rt->actions().preview(Json{{"action", "summarise"}, {"element", message_id}})); }
  Json summarise_run(const Json& extra = Json::object()) {
    Json request{{"action", "summarise"}, {"element", message_id}, {"ref", "test"}};
    for (auto it = extra.begin(); it != extra.end(); ++it) request[it.key()] = it.value();
    return unwrap(rt->actions().run(request));
  }
  void script(const std::string& content) { transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content))); }
  std::size_t model_calls() {
    std::size_t n = 0;
    for (const auto& r : transport->requests())
      if (r.method == "POST" && r.url.find("/chat/completions") != std::string::npos) ++n;
    return n;
  }
};

Json limit(const char* period, double amount, const char* on_exceed, const char* currency = "USD") {
  return Json{{"period", period}, {"amount", amount}, {"currency", currency}, {"on_exceed", on_exceed}};
}

const Json* gate_of(const Json& doc, const std::string& id) {
  for (const auto& g : doc.at("gates"))
    if (g.at("id") == id) return &g;
  return nullptr;
}

}  // namespace

TEST_SUITE("spend_limits") {
  TEST_CASE("the words of a limit: periods, currencies and what happens are data; a currency without a rate is not guessed") {
    Fixture f;
    const Json data = spend::data_of(f.td.path());
    REQUIRE_FALSE(data.empty());
    CHECK(spend::period_in(data, "na miesiac") == "month");
    CHECK(spend::period_in(data, "per week") == "week");
    CHECK(spend::period_in(data, "dziennie") == "day");
    CHECK(spend::period_in(data, "na zawsze").empty());
    auto pln = unwrap(spend::entry_from(data, Json{{"amount", "20,5"}, {"currency", "zl"}, {"period", "miesiac"}}));
    CHECK(pln.currency == "PLN");
    CHECK(pln.amount == doctest::Approx(20.5));
    CHECK(pln.period == "month");
    CHECK(pln.on_exceed == "confirm");  // the preset
    CHECK(pln.rate_source == "preset");
    CHECK(pln.usd() == doctest::Approx(20.5 * pln.rate));
    auto usd = unwrap(spend::entry_from(data, Json{{"amount", 5}, {"currency", "$"}, {"period", "weekly"}, {"on_exceed", "deny"}}));
    CHECK(usd.currency == "USD");
    CHECK(usd.rate == 1.0);
    CHECK(usd.rate_source == "identity");
    CHECK(usd.on_exceed == "deny");
    CHECK(unwrap(spend::entry_from(data, Json{{"amount", 3}, {"period", "day"}, {"on_exceed", "tylko ostrzegaj"}})).on_exceed == "notify");
    // nothing is guessed: no amount, no period, an unknown currency without a rate
    CHECK_FALSE(spend::entry_from(data, Json{{"period", "day"}}));
    CHECK_FALSE(spend::entry_from(data, Json{{"amount", 0}, {"period", "day"}}));
    CHECK_FALSE(spend::entry_from(data, Json{{"amount", 5}, {"period", "fortnight"}}));
    CHECK_FALSE(spend::entry_from(data, Json{{"amount", 5}, {"period", "day"}, {"currency", "bitcoin"}}));
    // ... but the user's own rate makes it a limit
    auto own = unwrap(spend::entry_from(data, Json{{"amount", 5}, {"period", "day"}, {"currency", "bitcoin"}, {"rate", 60000.0}}));
    CHECK(own.rate_source == "user");
    CHECK(own.usd() == doctest::Approx(300000.0));
  }

  TEST_CASE("the window of a period is the one that \"how much did I spend\" reads: today, this week from Monday, this month from the 1st") {
    Fixture f;
    const Json data = spend::data_of(f.td.path());
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;  // 2026-10-14 is a Wednesday
    tm.tm_mday = 14;
    tm.tm_hour = 15;
    tm.tm_min = 30;
    const std::time_t now = timegm(&tm);
    CHECK(spend::window(data, "day", now).from == "2026-10-14 00:00:00");
    CHECK(spend::window(data, "week", now).from == "2026-10-12 00:00:00");
    CHECK(spend::window(data, "month", now).from == "2026-10-01 00:00:00");
    CHECK(spend::window(data, "month", now).to == "2026-10-14 15:30:01");
  }

  TEST_CASE("no limit is the preset: a run is not gated, nothing is estimated for it") {
    Fixture f;
    f.spend_usd(1000.0);
    const Json preview = f.summarise_preview();
    CHECK(gate_of(preview, "spend:day") == nullptr);
    CHECK(preview.at("needs_confirmation").empty());
    CHECK(preview.at("denied").empty());
    CHECK(f.model_calls() == 0);
  }

  TEST_CASE("a limit that is passed becomes a gate of the plan: confirm waits for the yes, deny does not start, notify goes on and says so") {
    Fixture f;
    f.spend_usd(0.02);
    // confirm: the day's 0.01 USD is already spent
    f.limits(Json::array({limit("day", 0.01, "confirm")}));
    const Json preview = f.summarise_preview();
    const Json* g = gate_of(preview, "spend:day");
    REQUIRE(g != nullptr);
    CHECK(g->at("decision") == "confirm");
    CHECK(g->at("status") == "needs_confirmation");
    CHECK(g->at("initiator") == "user");
    CHECK(g->at("rule") == "spend_limit:day");
    CHECK(g->at("reason").at("en").get<std::string>().find("already used up") != std::string::npos);
    CHECK(g->at("reason").at("pl").get<std::string>().find("wykorzystany") != std::string::npos);
    CHECK(preview.at("needs_confirmation").size() == 1);
    CHECK(preview.at("runnable") == true);
    const Json held = f.summarise_run();
    CHECK(held.at("status") == "awaiting_confirmation");
    CHECK(f.model_calls() == 0);  // nothing was sent while the limit waits for the user
    // the yes (with the receipt) goes on
    f.script("A summary.");
    const Json done = f.summarise_run(Json{{"confirmed", true}, {"receipt_id", preview.at("plan_receipt")}});
    CHECK(done.at("status") == "completed");
    CHECK(f.model_calls() == 1);

    // deny: the run does not start, with the reason and the initiator
    f.limits(Json::array({limit("day", 0.01, "deny")}));
    const Json denied = f.summarise_run();
    CHECK(denied.at("status") == "denied");
    CHECK(denied.at("error").at("gates").at(0).at("initiator") == "user");
    CHECK(denied.at("error").at("message").get<std::string>().find("spending limit") != std::string::npos);
    CHECK(f.model_calls() == 1);

    // notify: the run goes on, the gate only carries the notice
    f.limits(Json::array({limit("day", 0.01, "notify")}));
    f.script("Another summary.");
    const Json noted = f.summarise_run();
    CHECK(noted.at("status") == "completed");
    const Json* n = gate_of(noted, "spend:day");
    REQUIRE(n != nullptr);
    CHECK(n->at("decision") == "allow");
    CHECK(n->at("notice").at("pl").get<std::string>().find("Uwaga") != std::string::npos);
    CHECK(f.model_calls() == 2);
  }

  TEST_CASE("a limit that is not passed leaves the plan alone; reading, editing a setting and raising the limit are never stopped by it") {
    Fixture f;
    f.spend_usd(0.02);
    f.limits(Json::array({limit("month", 50.0, "deny")}));  // 0.02 + the estimate of a short summary is far below 50 USD
    const Json preview = f.summarise_preview();
    CHECK(gate_of(preview, "spend:month") == nullptr);
    CHECK(preview.at("denied").empty());
    // already passed, but the plan calls no model: a question about the spend, a setting, the limit itself
    f.limits(Json::array({limit("month", 0.001, "deny")}));
    const Json question = unwrap(f.rt->actions().preview(Json{{"action", "usage.spend"}, {"element", "application"}}));
    CHECK(gate_of(question, "spend:month") == nullptr);
    CHECK(question.at("denied").empty());
    const Json raise = unwrap(f.rt->actions().preview(Json{{"action", "usage.set_limit"}, {"element", "usage_spend_limits"}, {"args", Json{{"amount", 100}, {"period", "month"}}}}));
    CHECK(raise.at("denied").empty());
  }

  TEST_CASE("the estimate of the run counts too: a run that would take the period past the limit is gated before it spends") {
    Fixture f;
    f.spend_usd(0.001);
    f.limits(Json::array({limit("day", 0.002, "confirm")}));
    // no price in the registry: the estimate is unknown, only what was spent counts, and the reason says so when it is passed
    CHECK(gate_of(f.summarise_preview(), "spend:day") == nullptr);
    f.limits(Json::array({limit("day", 0.0005, "confirm")}));
    const Json unknown_preview = f.summarise_preview();
    const Json* unknown = gate_of(unknown_preview, "spend:day");
    REQUIRE(unknown != nullptr);
    CHECK(unknown->at("reason").at("en").get<std::string>().find("not known") != std::string::npos);
    // a listed price: the money of the run is a range, and its upper end is added to the spent part
    f.transport->expect("GET", "https://api.test/models",
                        net::ScriptedTransport::Reply::json(200, Json{{"data", Json::array({Json{{"id", "test/model"}, {"name", "T"}, {"context_length", 8000},
                                                                                                {"pricing", Json{{"prompt", "0.000003"}, {"completion", "0.000015"}}}}})}}));
    LOOM_REQUIRE_OK(f.rt->models().update_from_api());
    f.limits(Json::array({limit("day", 100.0, "confirm")}));
    const Json estimate = f.summarise_preview().at("estimate");
    REQUIRE(estimate.at("money").at("known") == true);
    const double high = estimate.at("money").at("high").get<double>();
    const double low = estimate.at("money").at("low").get<double>();
    REQUIRE(high > 0);
    f.limits(Json::array({limit("day", 0.001 + high * 0.9, "confirm")}));  // spent 0.001 + the upper end passes it, the spent part alone does not
    const Json priced_preview = f.summarise_preview();
    const Json* g = gate_of(priced_preview, "spend:day");
    REQUIRE(g != nullptr);
    CHECK(g->at("reason").at("en").get<std::string>().find("takes the daily total past the limit") != std::string::npos);
    CHECK(g->at("spend").at("already_over") == false);
    CHECK(g->at("spend").at("estimate_usd").get<double>() == doctest::Approx(high));
    // the bound is data: with a limit between the two ends, `high` (the preset) gates the run and `low` (an overlay) does not
    if (low < high) {
      f.limits(Json::array({limit("day", 0.001 + low + (high - low) / 2, "confirm")}));
      CHECK(gate_of(f.summarise_preview(), "spend:day") != nullptr);
      std::filesystem::create_directories(f.td.path() / "profiles");
      std::ofstream(f.td.path() / "profiles" / "action_run.pack")
          << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "action_run"}, {"overrides", Json{{"spend_limits", Json{{"estimate_bound", "low"}}}}}}.dump();
      CHECK(gate_of(f.summarise_preview(), "spend:day") == nullptr);
    }
  }

  TEST_CASE("the receipt binds the decision, not the numbers of the moment: more spend between preview and run does not void the yes") {
    Fixture f;
    f.spend_usd(0.02);
    f.limits(Json::array({limit("day", 0.01, "confirm")}));
    const Json preview = f.summarise_preview();
    f.spend_usd(0.01, "op2");  // the ledger moves on
    f.script("A summary.");
    const Json done = f.summarise_run(Json{{"confirmed", true}, {"receipt_id", preview.at("plan_receipt")}});
    CHECK(done.at("status") == "completed");
  }

  TEST_CASE("from the conversation: set, replace, read back with the spend, clear; the growth limit keeps its sentences") {
    Fixture f;
    // the growth factor is still the growth factor
    const Json growth = unwrap(chat_route::route(*f.rt, Json{{"text", "ustaw limit wzrostu zużycia na 20"}, {"conv_id", f.conv_id}}));
    CHECK(growth.at("command").at("args").contains("growth_factor"));
    CHECK_FALSE(growth.at("command").at("args").contains("amount"));
    const Json bare = unwrap(chat_route::route(*f.rt, Json{{"text", "ustaw limit 20"}, {"conv_id", f.conv_id}}));
    CHECK(bare.at("command").at("args").contains("growth_factor"));

    const auto set = f.say_and_confirm("ustaw limit 20 zł na miesiąc", "tak");
    const Json stored = f.rt->config().get("usage_spend_limits");
    REQUIRE(stored.is_array());
    REQUIRE(stored.size() == 1);
    CHECK(stored[0].at("period") == "month");
    CHECK(stored[0].at("amount") == 20);
    CHECK(stored[0].at("currency") == "PLN");
    CHECK(stored[0].at("on_exceed") == "confirm");
    CHECK(stored[0].at("rate_source") == "preset");
    const std::string said = f.text_of(set);
    CHECK(said.find("20") != std::string::npos);
    CHECK(said.find("miesięczny") != std::string::npos);
    CHECK(said.find("przybliżonym presetem") != std::string::npos);

    f.say_and_confirm("set a weekly limit of $5", "yes");
    CHECK(f.rt->config().get("usage_spend_limits").size() == 2);
    f.say_and_confirm("set the monthly limit to 30 PLN, deny", "yes");  // replaces the monthly one
    const Json now = f.rt->config().get("usage_spend_limits");
    REQUIRE(now.size() == 2);
    CHECK(now[1].at("period") == "month");
    CHECK(now[1].at("amount") == 30);
    CHECK(now[1].at("on_exceed") == "deny");

    // read back in "how much did I spend", with what each period has spent
    f.spend_usd(0.5);
    const auto readback = f.input("ile wydałem w tym tygodniu");
    REQUIRE(readback.handled);
    const auto answered = f.input("tak");
    const std::string text = f.text_of(answered);
    CHECK(text.find("Limity wydatków") != std::string::npos);
    CHECK(text.find("limit tygodniowy 5 USD") != std::string::npos);
    CHECK(text.find("limit miesięczny 30 PLN") != std::string::npos);
    CHECK(text.find("odmawiam") != std::string::npos);
    CHECK(text.find("10%") != std::string::npos);  // 0.5 of 5 USD

    f.say_and_confirm("usuń tygodniowy limit wydatków", "tak");
    const Json left = f.rt->config().get("usage_spend_limits");
    REQUIRE(left.size() == 1);
    CHECK(left[0].at("period") == "month");
  }

  TEST_CASE("a chat sentence to remove a limit that was never set says so, and a limit with an unknown currency is refused with the way out") {
    Fixture f;
    const auto cleared = f.say_and_confirm("remove the weekly spending limit", "yes");
    CHECK(f.text_of(cleared).find("no weekly") != std::string::npos);
    const Json bad = unwrap(f.rt->actions().run(Json{{"action", "usage.set_limit"}, {"element", "usage_spend_limits"}, {"confirmed", true}, {"receipt_id", ""},
                                                       {"args", Json{{"amount", 5}, {"period", "week"}, {"currency", "bitcoin"}}}}));
    // gated like any write: the plan waits; the refusal comes from the executor once confirmed
    CHECK((bad.at("status") == "proposed" || bad.at("status") == "failed"));
  }

  TEST_CASE("in the conversation the proposal says what the limit does: asks, refuses, or only tells") {
    Fixture f;
    f.spend_usd(0.02);
    f.limits(Json::array({limit("day", 0.01, "confirm")}));
    const auto asked = f.input("Summarise this conversation");
    REQUIRE(asked.handled);
    Json p = f.meta(asked).at("action_proposal");
    CHECK(p.at("needs_confirmation").size() == 1);
    CHECK(f.text_of(asked).find("spending limit") != std::string::npos);
    f.input("no");

    f.limits(Json::array({limit("day", 0.01, "deny")}));
    const auto refused = f.input("Summarise this conversation");
    REQUIRE(refused.handled);
    const std::string said = f.text_of(refused);
    CHECK(said.find("spending limit") != std::string::npos);
    CHECK(said.find("usage_spend_limits") != std::string::npos);  // the way out is named
    CHECK(f.model_calls() == 0);

    f.limits(Json::array({limit("day", 0.01, "notify")}));
    const auto told = f.input("Podsumuj tę rozmowę");
    REQUIRE(told.handled);
    const std::string note = f.text_of(told);
    CHECK(note.find("Uwaga:") != std::string::npos);
    CHECK(note.find("Limit wydatków") != std::string::npos);
    f.limits(Json::array());
    const auto plain = f.input("Summarise this conversation");
    REQUIRE(plain.handled);
    CHECK(f.text_of(plain).find("spending limit") == std::string::npos);
  }

  TEST_CASE("by name: an amount is about usage_spend_limits and a growth factor about loom_usage_policy, with no setting key said; a bare call asks for the factor") {
    Fixture f;
    auto routed = [&](const char* text) { return unwrap(chat_route::route(*f.rt, Json{{"text", text}, {"conv_id", f.conv_id}})).at("command"); };
    const Json spend = routed("/usage.set_limit amount=5 period=week");
    CHECK(spend.at("feasible") == true);
    CHECK(spend.at("element").at("id") == "settings:usage_spend_limits");
    const Json growth = routed("/usage.set_limit growth_factor=20");
    CHECK(growth.at("feasible") == true);
    CHECK(growth.at("element").at("id") == "settings:loom_usage_policy");
    const Json positional = routed("/usage.set_limit 20");
    CHECK(positional.at("feasible") == true);
    CHECK(positional.at("args").at("growth_factor") == "20");
    const Json bare = routed("/usage.set_limit");
    CHECK(bare.at("reason").at("code") == "missing_args");
  }

  TEST_CASE("an unusable entry of the setting is left out and reported, never guessed") {
    Fixture f;
    f.limits(Json::array({Json{{"period", "fortnight"}, {"amount", 5}}, Json{{"period", "day"}, {"amount", -1}}, limit("week", 7.0, "deny"),
                          Json{{"period", "day"}, {"amount", 5}, {"currency", "bitcoin"}}}));
    const auto s = spend::stored(f.rt->config(), spend::data_of(f.td.path()));
    REQUIRE(s.entries.size() == 1);
    CHECK(s.entries[0].period == "week");
    CHECK(s.issues.size() == 3);
  }
}
