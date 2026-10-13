// P10 D: the proposal shows an estimated money range where the model registry knows the price, and stays "unknown"
// (never zero) where it does not.
#include <doctest/doctest.h>

#include <fstream>

#include "loom/action_estimate.h"
#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/providers.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

void write_file(const std::filesystem::path& p, std::string_view content) {
  std::ofstream f(p, std::ios::binary);
  f << content;
}

struct Registry {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  net::ScriptedTransport transport;
  std::unique_ptr<ModelRegistry> reg;
  Registry() {
    write_file(td.path() / "models.json",
               R"([{"id":"priced/model","pricing":{"prompt":"0.000002","completion":"0.000004"}},
                   {"id":"free/model","pricing":{"prompt":"0","completion":"0"}},
                   {"id":"half/model","pricing":{"prompt":"0.000002"}},
                   {"id":"odd/model","pricing":{"prompt":"cheap","completion":"cheaper"}},
                   {"id":"unlisted/model"}])");
    reg = std::make_unique<ModelRegistry>(td.path() / "models.json", cfg, secrets, transport);
  }
  actions::EstimateServices services() {
    actions::EstimateServices s;
    s.models = reg.get();
    s.config = &cfg;
    s.settings = unwrap(RuntimeProfile::builtin("action_run")).values();
    s.catalogue = Json{{"summarise", Json{{"content_op", "condense"}}}, {"translate", Json{{"content_op", "preserve"}}},
                       {"depict", Json{{"content_op", "preserve"}}}};
    return s;
  }
};

// One model step of `action` with a system and a prompt of 40 characters each.
Json plan_with(const std::string& model, const std::string& action = "summarise") {
  return Json{{"items", Json::array({Json{{"id", "a1"}, {"action", action}}})},
              {"steps", Json::array({Json{{"id", "a1.s1"}, {"item", "a1"}, {"action", "use_model"},
                                          {"with", Json{{"model", model}, {"system", std::string(40, 's')}, {"prompt", std::string(40, 'p')}}},
                                          {"inputs", Json::array({Json{{"from", "input"}}})}}})}};
}

}  // namespace

TEST_SUITE("action_estimate") {
  TEST_CASE("a listed price gives a money range from the size of the input; the arithmetic is the data's") {
    Registry r;
    // 4000 characters of input: 1000..1333 tokens, plus 80 characters of prompt: 20..27 tokens
    const Json e = actions::estimate_plan(r.services(), plan_with("priced/model"), Json::object(), 4000);
    CHECK(e.at("schema") == "loom.action_estimate/1");
    CHECK(e.at("model_calls") == 1);
    CHECK(e.at("tokens").at("input").at("low") == 1020);
    CHECK(e.at("tokens").at("input").at("high") == 1360);
    // condense: the output is 5%..40% of the content (floor 32 tokens)
    CHECK(e.at("tokens").at("output").at("low") == 50);
    CHECK(e.at("tokens").at("output").at("high") == 534);
    REQUIRE(e.at("money").at("known") == true);
    CHECK(e.at("money").at("currency") == "USD");
    CHECK(e.at("money").at("low").get<double>() == doctest::Approx(1020 * 2e-6 + 50 * 4e-6).epsilon(1e-9));
    CHECK(e.at("money").at("high").get<double>() == doctest::Approx(1360 * 2e-6 + 533.3333333 * 4e-6).epsilon(1e-4));
    CHECK(e.at("money").at("low").get<double>() < e.at("money").at("high").get<double>());
    CHECK(e.at("calls").at(0).at("price").at("known") == true);
    CHECK(e.at("assumptions").at("note").get<std::string>().find("not a limit") != std::string::npos);
    // The completion limit of the user caps the output estimate (it is the user's own setting, not ours).
    r.cfg.set("max_tokens", 100);
    const Json capped = actions::estimate_plan(r.services(), plan_with("priced/model"), Json::object(), 4000);
    CHECK(capped.at("tokens").at("output").at("high") == 100);
    // Calibration is data: a different band of characters per token changes the answer.
    actions::EstimateServices s = r.services();
    s.settings["estimate"]["chars_per_token"] = Json{{"low", 2.0}, {"high", 2.0}};
    CHECK(actions::estimate_plan(s, plan_with("priced/model"), Json::object(), 4000).at("tokens").at("input").at("low") == 2040);
  }

  TEST_CASE("a model the registry does not price makes the money unknown, with the reason and the models - never zero") {
    Registry r;
    for (const char* model : {"unlisted/model", "not/in/the/registry", "half/model", "odd/model"}) {
      INFO(model);
      const Json e = actions::estimate_plan(r.services(), plan_with(model), Json::object(), 4000);
      REQUIRE(e.at("money").at("known") == false);
      CHECK_FALSE(e.at("money").contains("low"));   // not 0, not a number at all
      CHECK_FALSE(e.at("money").contains("high"));
      CHECK(e.at("money").at("reason").get<std::string>().find("price") != std::string::npos);
      CHECK(e.at("money").at("models") == Json::array({model}));
      CHECK(e.at("calls").at(0).at("usd").is_null());
      CHECK(e.at("tokens").at("input").at("low") == 1020);  // the tokens are still estimated
    }
    // No model named and no default: unknown as well.
    Json none = plan_with("");
    none["steps"][0]["with"].erase("model");
    CHECK(actions::estimate_plan(r.services(), none, Json::object(), 100).at("money").at("known") == false);
    // A model that is listed at a price of 0 is a known zero: free is a fact, unlisted is not.
    const Json free = actions::estimate_plan(r.services(), plan_with("free/model"), Json::object(), 4000);
    REQUIRE(free.at("money").at("known") == true);
    CHECK(free.at("money").at("low") == 0.0);
    CHECK(free.at("money").at("high") == 0.0);
  }

  TEST_CASE("the model comes from the step, from an argument or from the default; an unpriced step makes the whole plan unknown") {
    Registry r;
    r.cfg.set("default_model", "priced/model");
    Json no_model = plan_with("x");
    no_model["steps"][0]["with"].erase("model");
    CHECK(actions::estimate_plan(r.services(), no_model, Json::object(), 400).at("money").at("known") == true);
    Json by_arg = plan_with("x");
    by_arg["steps"][0]["with"]["model"] = Json{{"$arg", "model"}};
    CHECK(actions::estimate_plan(r.services(), by_arg, Json{{"model", "unlisted/model"}}, 400).at("money").at("known") == false);
    CHECK(actions::estimate_plan(r.services(), by_arg, Json{{"model", "priced/model"}}, 400).at("money").at("known") == true);
    // Two model steps: the second takes the output of the first; one unpriced model spoils the sum, which is not a partial sum.
    Json two = plan_with("priced/model", "translate");
    Json second = two["steps"][0];
    second["id"] = "a1.s2";
    second["with"]["model"] = "unlisted/model";
    second["inputs"] = Json::array({Json{{"from", "a1.s1"}}});
    two["steps"].push_back(second);
    const Json e = actions::estimate_plan(r.services(), two, Json::object(), 4000);
    CHECK(e.at("model_calls") == 2);
    CHECK(e.at("money").at("known") == false);
    CHECK(e.at("money").at("models") == Json::array({"unlisted/model"}));
    CHECK(e.at("calls").at(1).at("tokens").at("input").at("low").get<int>() > 800);  // it reads what the first one wrote
    // A step that its own condition keeps out (an argument that is not given) is not counted.
    Json conditional = plan_with("priced/model");
    Json speak = conditional["steps"][0];
    speak["id"] = "a1.s2";
    speak["when"] = Json{{"arg", "speech_model"}};
    speak["with"]["model"] = "unlisted/model";
    conditional["steps"].push_back(speak);
    const Json skipped = actions::estimate_plan(r.services(), conditional, Json::object(), 400);
    CHECK(skipped.at("model_calls") == 1);
    CHECK(skipped.at("money").at("known") == true);
    CHECK(skipped.at("calls").at(1).at("skipped") == true);
    CHECK(actions::estimate_plan(r.services(), conditional, Json{{"speech_model", "x"}}, 400).at("money").at("known") == false);
  }

  TEST_CASE("an image is not priced per token: the money is unknown with that reason") {
    Registry r;
    Json plan = plan_with("priced/model", "depict");
    plan["steps"][0]["with"]["wants"] = Json::array({"image"});
    const Json e = actions::estimate_plan(r.services(), plan, Json::object(), 400);
    CHECK(e.at("money").at("known") == false);
    CHECK(e.at("money").at("reason").get<std::string>().find("not text") != std::string::npos);
    CHECK(e.at("calls").at(0).at("kind") == "media");
  }

  TEST_CASE("a plan that calls no model costs nothing, and says so") {
    Registry r;
    const Json e = actions::estimate_plan(r.services(), Json{{"items", Json::array()}, {"steps", Json::array()}}, Json::object(), 0);
    CHECK(e.at("model_calls") == 0);
    CHECK(e.at("money").at("known") == true);
    CHECK(e.at("money").at("high") == 0.0);
  }

  TEST_CASE("the preview and the chat proposal carry it: a range where the price is listed, 'unknown' where it is not") {
    fsutil::TempDir td;
    auto transport = std::make_shared<net::ScriptedTransport>();
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    auto rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "priced/model");
    const std::string conv = unwrap(rt->db().create_conv("Planning")).id;
    NewMessage m;
    m.conv_id = conv;
    m.role = "user";
    m.text = std::string(2000, 'w') + " ship on Friday.";
    const std::string message = unwrap(rt->db().create_msg(m));

    // models.json as the registry reads it (OpenRouter prices: USD per token, as strings)
    transport->expect("GET", "https://api.test/models",
                      net::ScriptedTransport::Reply::json(200, Json{{"data", Json::array({Json{{"id", "priced/model"}, {"name", "Priced"}, {"context_length", 8000},
                                                                                              {"pricing", Json{{"prompt", "0.000003"}, {"completion", "0.000015"}}}}})}}));
    LOOM_REQUIRE_OK(rt->models().update_from_api());
    const Json preview = unwrap(rt->actions().preview(Json{{"action", "summarise"}, {"element", message}}));
    const Json& est = preview.at("estimate");
    REQUIRE(est.at("money").at("known") == true);
    CHECK(est.at("money").at("low").get<double>() > 0);
    CHECK(est.at("money").at("high").get<double>() > est.at("money").at("low").get<double>());
    CHECK(est.at("input_chars").get<int>() == 2016);

    ChatOptions opts;
    opts.conv_id = conv;
    opts.stream = false;
    auto input = [&](const std::string& text) {
      Json req{{"message", text}};
      return unwrap(chat_route::handle(*rt, text, opts, req, nullptr));
    };
    const auto priced = input("Summarise this conversation");
    const Message answer = *unwrap(rt->db().get_msg(priced.assistant_message_id));
    const Json& cost = answer.metadata.at("action_proposal").at("cost");
    CHECK(cost.at("money").at("known") == true);
    CHECK(cost.at("money").at("currency") == "USD");
    CHECK(cost.at("tokens").at("input").at("low").get<int>() > 400);
    CHECK(answer.text.find("Money: about ") != std::string::npos);
    CHECK(answer.text.find("USD") != std::string::npos);

    // The same command with a model the registry does not list: unknown, said in words, no number.
    rt->config().set("default_model", "mystery/model");
    const auto unpriced = input("Summarise this conversation");
    const Message unknown = *unwrap(rt->db().get_msg(unpriced.assistant_message_id));
    const Json& cost2 = unknown.metadata.at("action_proposal").at("cost");
    CHECK(cost2.at("money").at("known") == false);
    CHECK_FALSE(cost2.at("money").contains("low"));
    CHECK(unknown.text.find("Money: unknown") != std::string::npos);
    CHECK(unknown.text.find("do not read it as free") != std::string::npos);
  }
}
