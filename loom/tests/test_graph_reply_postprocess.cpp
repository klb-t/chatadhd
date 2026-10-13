// The separate-model postprocess of a graph reply: the exact request it puts on
// the wire (URL, header order, body bytes, timeout, stream flag) and the
// one-attempt rules around it.
#include <doctest/doctest.h>

#include "chat/graph_reply.h"
#include "context/context_execution.h"
#include "loom/config.h"
#include "loom/providers.h"
#include "loom/runtime.h"
#include "loom/usage_policy.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::chat;
using loom::test::unwrap;
using Reply = net::ScriptedTransport::Reply;

namespace {

struct Fixture {
  fsutil::TempDir directory;
  std::shared_ptr<net::ScriptedTransport> http = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;

  Fixture() {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = http;
    runtime = unwrap(Runtime::open(options));
    runtime->config().set("loom_usage_policy", usage_policy_defaults());
    runtime->secrets().set("graph_key", "gk-test");
    const Json caps = Json::array({Json{{"resource", "llm"}, {"name", "chat.completions"}, {"constraints", Json::object()}},
                                   Json{{"resource", "llm"}, {"name", "chat.stream"}, {"constraints", Json::object()}}});
    LOOM_REQUIRE_OK(runtime->providers().load(Json::array({
        Json{{"id", "graph_fixture"}, {"display_name", "Fixture"}, {"base_url", "https://graph.fixture.test/"},
             {"auth_scheme", "bearer"}, {"auth_secret", "graph_key"}, {"default_headers", Json{{"Fixture-Protocol", "1"}}},
             {"capabilities", caps}, {"metadata", Json{{"chat_completions_path", "/fixture-completions"}}}},
        Json{{"id", "graph_query"}, {"display_name", "Query key"}, {"base_url", "https://query.fixture.test"},
             {"auth_scheme", "query:key"}, {"auth_secret", "graph_key"}, {"capabilities", caps},
             {"metadata", Json{{"chat_completions_path", "/q"}}}}})));
  }
  GraphReplyServices services() {
    return {runtime->paths().root, runtime->db(), runtime->config(), runtime->secrets(), runtime->http(),
            &runtime->providers(), runtime->blobs(), runtime->provenance()};
  }
};

GraphReplyPrepared prepared() {
  Json definition{{"request", Json{{"model", "fixture/model"}, {"messages", Json::array({
      Json{{"role", "user"}, {"content", "Fixture prompt bytes supplied as data."}}})}}}};
  Json recipe = definition;
  recipe["definition"] = definition;
  recipe["definition_sha256"] = Sha256::hex(json::canonical(definition));
  GraphReplyCallbacks callbacks;
  callbacks.operation = [](const Json& command) -> Result<Json> {
    if (command["operation"] == "capabilities") return Json{{"schema", "fixture_capabilities"}};
    if (command["operation"] == "validate") return command["packet"];
    return Error(Errc::Unavailable, "offline_fixture_has_no_compiler");
  };
  auto value = unwrap(prepare_graph_reply(Json{{"mode", "separate_model_afterwards"}}, Json::object(),
      Json{{"request_id", "fixture_request"}, {"turn_id", "fixture_turn"}, {"model", "fixture/model"},
           {"recipe_sha256", recipe["definition_sha256"]}}, recipe, Json{{"schema", "loom.method_graph/1"}}, callbacks));
  value.binding_origin = Json{{"kind", "system"}, {"actor", "fixture"}, {"model", nullptr}, {"recipe_sha256", nullptr}, {"response_sha256", nullptr}};
  return value;
}

Json options(std::string provider, std::string operation) {
  return Json{{"provider_id", std::move(provider)}, {"timeout_ms", 123456}, {"calls_authorized", true},
              {"usage_estimate", Json{{"operation_id", std::move(operation)}, {"baseline_key", "graph_reply_fixture/cohort"},
                                      {"resources", Json{{"output_tokens", nullptr}, {"response_bytes", nullptr}, {"cost_usd", nullptr}}}}}};
}

}  // namespace

TEST_SUITE("graph_reply_postprocess") {
  TEST_CASE("golden: postprocess request bytes are pinned (buffered, streamed, query-key provider)") {
    Fixture f;
    auto services = f.services();
    auto value = prepared();
    context::ContextExecutionScope scope(Json::object());
    f.http->expect("POST", "https://graph.fixture.test/fixture-completions", Reply::text(500, "no"));
    Json payload{{"model", "fixture/model"}, {"messages", Json::array({Json{{"role", "user"}, {"content", "Hallo \xC4\x85"}}})},
                 {"temperature", 0.2}};
    run_graph_reply_postprocess(services, value, payload, options("graph_fixture", "pp_buffered"), "primary", nullptr);
    f.http->expect("POST", "https://graph.fixture.test/fixture-completions", Reply::text(500, "no"));
    Json streamed = payload;
    streamed["stream"] = true;
    run_graph_reply_postprocess(services, value, streamed, options("graph_fixture", "pp_stream"), "primary", nullptr);
    f.http->expect("POST", "https://query.fixture.test/q", Reply::text(500, "no"));
    run_graph_reply_postprocess(services, value, payload, options("graph_query", "pp_query"), "primary", nullptr);

    auto reqs = f.http->requests();
    REQUIRE(reqs.size() == 3);
    const Json headers = Json::array({Json::array({"Fixture-Protocol", "1"}), Json::array({"Content-Type", "application/json"}),
                                      Json::array({"Authorization", "Bearer gk-test"})});
    CHECK(loom::test::request_json(reqs[0]) ==
          Json{{"method", "POST"}, {"url", "https://graph.fixture.test/fixture-completions"}, {"headers", headers},
               {"body", "{\"model\":\"fixture/model\",\"messages\":[{\"role\":\"user\",\"content\":\"Hallo \xC4\x85\"}],\"temperature\":0.2}"},
               {"timeout_ms", 123456}, {"stream", false}});
    CHECK(loom::test::request_json(reqs[1]) ==
          Json{{"method", "POST"}, {"url", "https://graph.fixture.test/fixture-completions"}, {"headers", headers},
               {"body", "{\"model\":\"fixture/model\",\"messages\":[{\"role\":\"user\",\"content\":\"Hallo \xC4\x85\"}],\"temperature\":0.2,\"stream\":true}"},
               {"timeout_ms", 123456}, {"stream", true}});
    CHECK(loom::test::request_json(reqs[2]) ==
          Json{{"method", "POST"}, {"url", "https://query.fixture.test/q?key=gk-test"},
               {"headers", Json::array({Json::array({"Content-Type", "application/json"})})},
               {"body", "{\"model\":\"fixture/model\",\"messages\":[{\"role\":\"user\",\"content\":\"Hallo \xC4\x85\"}],\"temperature\":0.2}"},
               {"timeout_ms", 123456}, {"stream", false}});
  }

  TEST_CASE("one explicit attempt: unauthorized, missing endpoint data and a missing credential send nothing") {
    Fixture f;
    auto services = f.services();
    auto value = prepared();
    const Json payload{{"model", "fixture/model"}, {"messages", Json::array()}};
    auto unauthorized = run_graph_reply_postprocess(services, value, payload, options("graph_fixture", "pp_a"), "primary", nullptr);
    CHECK(unauthorized["error"]["code"] == "postprocess_calls_not_authorized");   // no execution scope
    context::ContextExecutionScope scope(Json::object());
    auto opts = options("graph_fixture", "pp_b");
    opts["endpoint_path"] = "";
    CHECK(run_graph_reply_postprocess(services, value, payload, opts, "primary", nullptr)["error"]["code"] == "postprocess_endpoint_data_required");
    f.runtime->secrets().set("graph_key", "");
    CHECK(run_graph_reply_postprocess(services, value, payload, options("graph_fixture", "pp_c"), "primary", nullptr)["error"]["code"] ==
          "postprocess_provider_unavailable");   // the registry reports no stored credential
    CHECK(f.http->requests().empty());
  }
}
