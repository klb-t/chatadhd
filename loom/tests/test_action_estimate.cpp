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
#include "loom/usage_policy.h"
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

  TEST_CASE("P31: a generative action is estimated at the size of its kind, not of a short input; the table is data") {
    Registry r;
    actions::EstimateServices s = r.services();
    s.catalogue["explain_simply"] = Json{{"content_op", "restyle"}};
    s.catalogue["elaborate"] = Json{{"content_op", "expand"}};
    // 40 characters of input: 10..13 tokens. R19: explain_simply wrote 250 tokens against a high of 84.
    const Json e = actions::estimate_plan(s, plan_with("priced/model", "explain_simply"), Json::object(), 40);
    CHECK(e.at("tokens").at("output").at("low") == 48);
    CHECK(e.at("tokens").at("output").at("high") == 320);
    CHECK(e.at("tokens").at("output").at("high").get<double>() >= 250);
    CHECK(e.at("money").at("high").get<double>() >= 250 * 4e-6);
    const Json x = actions::estimate_plan(s, plan_with("priced/model", "elaborate"), Json::object(), 40);
    CHECK(x.at("tokens").at("output").at("high") == 600);
    // an op the table does not list keeps the single floor; a large input still wins over the floor
    const Json t = actions::estimate_plan(s, plan_with("priced/model", "translate"), Json::object(), 40);
    CHECK(t.at("tokens").at("output").at("low") == 32);
    CHECK(t.at("tokens").at("output").at("high") == 32);
    const Json big = actions::estimate_plan(s, plan_with("priced/model", "explain_simply"), Json::object(), 12000);
    CHECK(big.at("tokens").at("output").at("high").get<double>() > 320);
    // the table is data: emptied, the old floor is back
    s.settings["estimate"]["output_floor_by_op"] = Json::object();
    const Json old = actions::estimate_plan(s, plan_with("priced/model", "explain_simply"), Json::object(), 40);
    CHECK(old.at("tokens").at("output").at("high") == 32);
    // and the completion limit of the user still caps it
    s.settings = unwrap(RuntimeProfile::builtin("action_run")).values();
    r.cfg.set("max_tokens", 100);
    CHECK(actions::estimate_plan(s, plan_with("priced/model", "explain_simply"), Json::object(), 40).at("tokens").at("output").at("high") == 100);
  }

  TEST_CASE("P31: what the ledger measured calibrates the high bound once there are enough samples (min samples, headroom and mode are data)") {
    Registry r;
    actions::EstimateServices s = r.services();
    s.catalogue["explain_simply"] = Json{{"content_op", "restyle"}};
    std::vector<std::string> asked;
    actions::ObservedUsage seen{5, 240.0, 200.0, 280.0};
    s.observed = [&](const std::string& action, const std::string& model, const std::string& resource) -> std::optional<actions::ObservedUsage> {
      asked.push_back(action + "|" + model + "|" + resource);
      return seen;
    };
    const Json e = actions::estimate_plan(s, plan_with("priced/model", "explain_simply"), Json::object(), 40);
    REQUIRE(asked.size() == 1);
    CHECK(asked[0] == "explain_simply|priced/model|output_tokens");
    CHECK(e.at("tokens").at("output").at("low") == 200);
    CHECK(e.at("tokens").at("output").at("high") == 360);  // max(280, 240 x 1.5)
    CHECK(e.at("calls").at(0).at("calibration").at("samples") == 5);
    CHECK(e.at("calls").at(0).at("calibration").at("source") == "usage_ledger");
    // fewer samples than the preset's minimum: the assumption stays
    seen.samples = 2;
    const Json few = actions::estimate_plan(s, plan_with("priced/model", "explain_simply"), Json::object(), 40);
    CHECK(few.at("tokens").at("output").at("high") == 320);
    CHECK_FALSE(few.at("calls").at(0).contains("calibration"));
    // the minimum is data
    seen.samples = 2;
    s.settings["estimate"]["calibration"]["min_samples"] = 2;
    CHECK(actions::estimate_plan(s, plan_with("priced/model", "explain_simply"), Json::object(), 40).at("tokens").at("output").at("high") == 360);
    // mode off: the ledger is not even asked
    asked.clear();
    s.settings["estimate"]["calibration"]["mode"] = "off";
    CHECK(actions::estimate_plan(s, plan_with("priced/model", "explain_simply"), Json::object(), 40).at("tokens").at("output").at("high") == 320);
    CHECK(asked.empty());
    // the completion limit caps a calibrated band too
    s.settings["estimate"]["calibration"]["mode"] = "on";
    r.cfg.set("max_tokens", 300);
    CHECK(actions::estimate_plan(s, plan_with("priced/model", "explain_simply"), Json::object(), 40).at("tokens").at("output").at("high") == 300);
  }

  TEST_CASE("P31: the preview reads the ledger by itself: samples of the action and the model change the output band; no ledger, no call") {
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
    rt->config().set("model_registry_autorefresh", "off");
    const std::string conv = unwrap(rt->db().create_conv("Planning")).id;
    NewMessage m;
    m.conv_id = conv;
    m.role = "user";
    m.text = "Explain the migration plan.";
    const std::string message = unwrap(rt->db().create_msg(m));
    transport->expect("GET", "https://api.test/models",
                      net::ScriptedTransport::Reply::json(200, Json{{"data", Json::array({Json{{"id", "priced/model"}, {"name", "Priced"}, {"context_length", 8000},
                                                                                              {"pricing", Json{{"prompt", "0.000003"}, {"completion", "0.000015"}}}}})}}));
    LOOM_REQUIRE_OK(rt->models().update_from_api());
    const Json before = unwrap(rt->actions().preview(Json{{"action", "explain_simply"}, {"element", message}}));
    CHECK_FALSE(std::filesystem::exists(td.path() / "usage-policy.sqlite"));  // a question never creates the ledger
    CHECK(before.at("estimate").at("tokens").at("output").at("high") == 320);
    {
      auto policy = unwrap(UsagePolicy::open(td.path() / "usage-policy.sqlite", Json::object()));
      const char* keys[] = {"explain_simply|https://api.test/chat/completions|priced/model", "explain_simply|https://api.test/chat/completions|priced/model",
                            "explain_simply|https://api.test/chat/completions|priced/model", "explain_simply|https://api.test/chat/completions|other/model"};
      const double outputs[] = {250, 270, 230, 5000};
      for (int i = 0; i < 4; ++i) {
        const std::string op = "op" + std::to_string(i);
        unwrap(policy->request(Json{{"operation_id", op}, {"baseline_key", keys[i]}, {"resources", Json{{"output_tokens", nullptr}}}}));
        unwrap(policy->complete(op, Json{{"resources", Json{{"output_tokens", outputs[i]}}}, {"provenance", "provider_reported"}}));
      }
      const Json seen = unwrap(policy->observed("explain_simply", "priced/model", "output_tokens"));
      CHECK(seen.at("samples") == 3);
      CHECK(seen.at("min") == 230);
      CHECK(seen.at("max") == 270);
      CHECK(seen.at("mean").get<double>() == doctest::Approx(250.0));
      CHECK(unwrap(policy->observed("summarise", "priced/model", "output_tokens")).at("samples") == 0);
      CHECK(unwrap(policy->observed("explain_simply", "no/model", "output_tokens")).at("samples") == 0);
    }
    const Json after = unwrap(rt->actions().preview(Json{{"action", "explain_simply"}, {"element", message}}));
    CHECK(after.at("estimate").at("tokens").at("output").at("low") == 230);
    CHECK(after.at("estimate").at("tokens").at("output").at("high") == 375);  // 250 x 1.5 headroom beats the largest sample
    CHECK(after.at("estimate").at("calls").at(0).at("calibration").at("samples") == 3);
  }

  TEST_CASE("P31: a tiny estimate is written with its digits in the proposal, never as 0.0000") {
    fsutil::TempDir td;
    auto transport = std::make_shared<net::ScriptedTransport>();
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    auto rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "cheap/model");
    rt->config().set("model_registry_autorefresh", "off");
    const std::string conv = unwrap(rt->db().create_conv("Planning")).id;
    NewMessage m;
    m.conv_id = conv;
    m.role = "user";
    m.text = "Ship on Friday.";
    unwrap(rt->db().create_msg(m));
    transport->expect("GET", "https://api.test/models",
                      net::ScriptedTransport::Reply::json(200, Json{{"data", Json::array({Json{{"id", "cheap/model"}, {"name", "Cheap"}, {"context_length", 8000},
                                                                                              {"pricing", Json{{"prompt", "0.0000001"}, {"completion", "0.0000004"}}}}})}}));
    LOOM_REQUIRE_OK(rt->models().update_from_api());
    ChatOptions opts;
    opts.conv_id = conv;
    opts.stream = false;
    for (const char* sentence : {"Summarise this conversation", "Streść tę rozmowę"}) {
      const auto out = unwrap(chat_route::handle(*rt, sentence, opts, Json{{"message", sentence}}, nullptr));
      const Message answer = *unwrap(rt->db().get_msg(out.assistant_message_id));
      CAPTURE(answer.text);
      CHECK(answer.text.find("0.0000 ") == std::string::npos);
      CHECK(answer.text.find("0.0000 do") == std::string::npos);
      const Json& money = answer.metadata.at("action_proposal").at("cost").at("money");
      REQUIRE(money.at("known") == true);
      CHECK(money.at("high").get<double>() > 0);
      CHECK(answer.metadata.at("action_proposal").at("cost").at("money_format").at("significant") == 2);
    }
  }
}
