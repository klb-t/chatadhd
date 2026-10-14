// P14 3a/3b (R19): the reason money is unknown is a code with texts in the language of the proposal (data), and an empty model registry (a fresh
// data directory) is filled once from the provider's public model list when an estimate needs a price (setting model_registry_autorefresh).
#include <doctest/doctest.h>

#include <fstream>

#include "loom/action_estimate.h"
#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/net/http.h"
#include "loom/providers.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

const char* kModels = "https://api.test/models";

Json model_list() {
  return Json{{"data", Json::array({Json{{"id", "priced/model"}, {"name", "Priced"}, {"context_length", 8000},
                                         {"pricing", Json{{"prompt", "0.000003"}, {"completion", "0.000015"}}}}})}};
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv, message;

  Fixture(const std::string& model = "priced/model") {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", model);
    conv = unwrap(rt->db().create_conv("Planning")).id;
    NewMessage m;
    m.conv_id = conv;
    m.role = "user";
    m.text = std::string(2000, 'w') + " ship on Friday.";
    message = unwrap(rt->db().create_msg(m));
  }
  Json preview() { return unwrap(rt->actions().preview(Json{{"action", "summarise"}, {"element", message}})); }
  std::size_t gets() {
    std::size_t n = 0;
    for (const auto& r : transport->requests())
      if (r.method == "GET" && r.url == kModels) ++n;
    return n;
  }
  Message ask(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv;
    opts.stream = false;
    const auto out = unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
    return *unwrap(rt->db().get_msg(out.assistant_message_id));
  }
  void overlay(const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(td.path() / "profiles" / "action_run.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "action_run"}, {"overrides", overrides}}.dump();
  }
};

Json reasons() { return unwrap(RuntimeProfile::builtin("action_run")).values().at("estimate").at("reasons"); }

}  // namespace

TEST_SUITE("estimate_registry") {
  TEST_CASE("3a: money unknown carries a code; the texts are data; the proposal says it in its own language") {
    Fixture f("mystery/model");
    f.rt->config().set("model_registry_autorefresh", "off");  // (the registry stays empty here; 3b is below)
    const Json r = reasons();
    // the estimate: a code, and the English text of the data
    const Json e = f.preview().at("estimate");
    CHECK(e.at("money").at("known") == false);
    CHECK(e.at("money").at("reason_code") == "registry_empty");
    CHECK(e.at("money").at("reason") == r.at("registry_empty").at("en"));
    CHECK(e.at("calls").at(0).at("price").at("why_code") == "registry_empty");
    // Polish proposal: the Polish text, nothing English from the estimate
    const Message pl = f.ask("Streść tę rozmowę");
    const Json& money_pl = pl.metadata.at("action_proposal").at("cost").at("money");
    CHECK(money_pl.at("reason_code") == "registry_empty");
    CHECK(money_pl.at("reason") == r.at("registry_empty").at("pl"));
    CHECK(pl.text.find(r.at("registry_empty").at("pl").get<std::string>()) != std::string::npos);
    CHECK(pl.text.find("Pieniądze: nieznane") != std::string::npos);
    CHECK(pl.text.find("the model registry") == std::string::npos);
    // English proposal: the English text
    const Message en = f.ask("Summarise this conversation");
    CHECK(en.metadata.at("action_proposal").at("cost").at("money").at("reason") == r.at("registry_empty").at("en"));
    CHECK(en.text.find(r.at("registry_empty").at("en").get<std::string>()) != std::string::npos);
  }

  TEST_CASE("3a: every unknown has its own code - a model the registry lists without a price is not an empty registry") {
    Fixture f;
    f.transport->expect("GET", kModels, net::ScriptedTransport::Reply::json(200, model_list()));
    REQUIRE(f.rt->models().update_from_api());
    f.rt->config().set("default_model", "unlisted/model");
    const Json e = f.preview().at("estimate");
    CHECK(e.at("money").at("reason_code") == "no_price");
    CHECK(e.at("money").at("reason") == reasons().at("no_price").at("en"));
    const Message pl = f.ask("Streść tę rozmowę");
    CHECK(pl.metadata.at("action_proposal").at("cost").at("money").at("reason") == reasons().at("no_price").at("pl"));
    // and the registry was listed, so nothing was fetched again
    CHECK(f.gets() == 1);
  }

  TEST_CASE("3b: an estimate that needs a price on an empty registry fetches the public model list once, then prices") {
    Fixture f;
    CHECK(f.rt->models().size() == 0);
    f.transport->expect("GET", kModels, net::ScriptedTransport::Reply::json(200, model_list()));
    const Json e = f.preview().at("estimate");
    INFO(e.dump());
    REQUIRE(e.at("money").at("known") == true);
    CHECK(e.at("money").at("high").get<double>() > e.at("money").at("low").get<double>());
    CHECK(f.gets() == 1);
    CHECK(f.rt->models().size() == 1);
    // the list is the provider's public one: a GET with the key as for any provider call, no body, nothing of the user's
    const auto sent = f.transport->requests().at(0);
    CHECK(sent.method == "GET");
    CHECK(sent.body.empty());
    // the registry is filled now: later estimates do not fetch
    CHECK(f.preview().at("estimate").at("money").at("known") == true);
    CHECK(f.gets() == 1);
    // the proposal in the chat is priced as well
    const Message answer = f.ask("Summarise this conversation");
    CHECK(answer.metadata.at("action_proposal").at("cost").at("money").at("known") == true);
    CHECK(f.gets() == 1);
  }

  TEST_CASE("3b: a failed fetch keeps the money unknown with its reason; the next try waits the retry window, which is data") {
    Fixture f;
    f.transport->expect("GET", kModels, net::ScriptedTransport::Reply::json(500, Json{{"error", "down"}}));
    const Json first = f.preview().at("estimate");
    CHECK(first.at("money").at("known") == false);
    CHECK(first.at("money").at("reason_code") == "registry_fetch_failed");
    CHECK(first.at("money").at("reason").get<std::string>().find("API error 500") != std::string::npos);
    CHECK(first.at("money").at("reason_params").at("why").get<std::string>().find("500") != std::string::npos);
    CHECK(f.gets() == 1);
    const Message pl = f.ask("Streść tę rozmowę");
    CHECK(pl.metadata.at("action_proposal").at("cost").at("money").at("reason").get<std::string>().rfind("rejestr modeli jest pusty", 0) == 0);
    CHECK(pl.text.find("API error 500") != std::string::npos);
    CHECK(f.gets() == 1);  // inside the window: not again, the reason is remembered
    // a window of 0 seconds in the data: the next estimate tries again
    f.overlay(Json{{"estimate", Json{{"registry_refresh", Json{{"retry_after_seconds", 0}}}}}});
    f.transport->expect("GET", kModels, net::ScriptedTransport::Reply::json(200, model_list()));
    const Json second = f.preview().at("estimate");
    CHECK(second.at("money").at("known") == true);
    CHECK(f.gets() == 2);
  }

  TEST_CASE("3b: the setting off never fetches; a name that is no preset means on_demand") {
    Fixture off;
    off.rt->config().set("model_registry_autorefresh", "off");
    const Json e = off.preview().at("estimate");
    CHECK(e.at("money").at("reason_code") == "registry_empty");
    CHECK(off.gets() == 0);
    CHECK(off.transport->requests().empty());

    Fixture odd;
    odd.rt->config().set("model_registry_autorefresh", "whenever");
    odd.transport->expect("GET", kModels, net::ScriptedTransport::Reply::json(200, model_list()));
    CHECK(odd.preview().at("estimate").at("money").at("known") == true);
    CHECK(odd.gets() == 1);
  }

  TEST_CASE("3b: an estimate that needs no price fetches nothing (no model call, or a price that is not per token)") {
    Fixture f;
    // a plan that calls no model: the settings action
    const Json plan = unwrap(f.rt->actions().preview(Json{{"action", "settings.get"}, {"args", Json{{"key", "max_tokens"}}}}));
    CHECK(plan.at("estimate").at("model_calls") == 0);
    CHECK(f.gets() == 0);
    // recognition is priced by the provider by the second or the page: the registry cannot say, so it is not asked
    const std::string audio = unwrap(f.rt->blobs().put("AUDIO", "audio/mp4")).hash;
    const Json media = unwrap(f.rt->actions().preview(Json{{"action", "transcribe"}, {"element", audio}}));
    CHECK(media.at("estimate").at("money").at("reason_code") == "recognition_priced_by_provider");
    CHECK(f.gets() == 0);
    CHECK(f.transport->requests().empty());
  }
}
