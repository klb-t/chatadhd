// Offline W3 regression cases. Linked by the source-local selector runner.
#include <doctest/doctest.h>
#include <stdexcept>

#include "context_execution.h"
#include "context_goal_usage.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "test_helpers.h"

namespace {
using namespace loom;
using namespace loom::context;
using loom::test::unwrap;

struct GoalUsageFixture {
  fsutil::TempDir directory;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  std::shared_ptr<const kb::Pack> pack;
  std::unique_ptr<ContextEngine> engine;
  ContextRequest request;

  explicit GoalUsageFixture(std::shared_ptr<net::HttpTransport> custom_transport = {}) {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = custom_transport ? std::move(custom_transport) : transport;
    runtime = unwrap(Runtime::open(options));
    runtime->config().set("base_url", "https://goal.test");
    runtime->config().set("semantic_model", "fixture/goal");
    runtime->config().set("semantic_analysis", false);
#if LOOM_CONTEXT_HAS_USAGE_POLICY
    runtime->config().set("loom_usage_policy", usage_policy_defaults());
#endif
    runtime->secrets().set("api_key", "synthetic-goal-token");
    pack = unwrap(runtime->knowledge().pack());
    auto& store = runtime->knowledge().store();
    request.run = unwrap(store.begin_run(pack->hash(), Json::object())).id;
    LOOM_REQUIRE_OK(store.finish_run(request.run, "done", Json::object()));
    request.text = "asdkjhasd";
    engine = std::make_unique<ContextEngine>(*runtime, store, pack);
  }

  void response() {
    transport->expect("POST", "https://goal.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        Json{{"choices", Json::array({Json{{"message", Json{{"content", "{\"goal_type\":\"verify_claim\",\"confidence\":0.91}"}}}}})},
            {"usage", Json{{"completion_tokens", 2}, {"cost", 1.0}}}}));
  }
};

Json unrestricted_options(bool authorized = true) {
  return Json{{"goal_typing", Json{{"enabled", true}, {"calls_authorized", authorized},
      {"max_requests", 3}, {"max_input_bytes", 500000}, {"max_output_tokens", 9000},
      {"timeout_ms", 120000}, {"max_response_bytes", 500000}}}};
}

class ThrowingPartialGoalTransport final : public net::HttpTransport {
 public:
  std::string partial = "{SIMULATED_PRIVATE_PARTIAL_GOAL_RESPONSE";
  std::size_t calls = 0;
  Result<net::HttpResponse> send(const net::HttpRequest&, const net::StreamSink* sink,
                                const CancelToken*) override {
    ++calls;
    if (sink) {
      if (sink->on_headers) sink->on_headers(200, {});
      if (sink->on_data) sink->on_data(partial);
    }
    throw std::runtime_error("SIMULATED_PRIVATE_TRANSPORT_EXCEPTION");
  }
  std::string name() const override { return "synthetic_throwing_partial_goal"; }
};
}

TEST_CASE("goal execution: nested scopes own settings and restore prior capability") {
  CHECK(current_context_execution_scope() == nullptr);
  Json options = unrestricted_options();
  {
    ContextExecutionScope outer(options);
    options["goal_typing"]["max_requests"] = 0;
    CHECK(outer.options()["goal_typing"]["max_requests"] == 3);
    outer.note_goal_typing_request();
    outer.record_usage_decision(Json{{"operation_id", "synthetic"}, {"status", "allowed"}});
    {
      ContextExecutionScope inner(Json::object());
      CHECK(current_context_execution_scope() == &inner);
      CHECK(inner.goal_typing_requests() == 0);
      CHECK(inner.usage_decisions().empty());
    }
    CHECK(current_context_execution_scope() == &outer);
    CHECK(outer.goal_typing_requests() == 1);
    CHECK(outer.usage_decisions().size() == 1);
  }
  CHECK(current_context_execution_scope() == nullptr);
}

TEST_CASE("goal execution: validation accepts configurable presets and rejects malformed authorization") {
  CHECK(validate_context_execution_options(unrestricted_options()));
  for (const auto* section : {"goal_typing", "embedding", "unified"}) {
    CHECK_FALSE(validate_context_execution_options(Json{{section, Json{{"enabled", "true"}}}}));
    CHECK_FALSE(validate_context_execution_options(Json{{section, Json{{"calls_authorized", 1}}}}));
    CHECK_FALSE(validate_context_execution_options(Json{{section, nullptr}}));
  }
  auto options = unrestricted_options();
  options["goal_typing"]["max_requests"] = -1;
  CHECK_FALSE(validate_context_execution_options(options));
  options = unrestricted_options();
  options["goal_typing"]["timeout_ms"] = static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1;
  CHECK_FALSE(validate_context_execution_options(options));
  options = unrestricted_options();
  options["goal_typing"]["confidence_threshold"] = nullptr;
  CHECK_FALSE(validate_context_execution_options(options));
  for (const auto* estimate : {"estimated_cost_usd", "estimated_output_tokens", "estimated_response_bytes"}) {
    options = unrestricted_options();
    options["goal_typing"][estimate] = nullptr;
    CHECK(validate_context_execution_options(options));
    options["goal_typing"][estimate] = 0.25;
    CHECK(validate_context_execution_options(options));
    options["goal_typing"][estimate] = -1;
    CHECK_FALSE(validate_context_execution_options(options));
    options["goal_typing"][estimate] = "unknown";
    CHECK_FALSE(validate_context_execution_options(options));
  }
}

TEST_CASE("goal execution: configured credentials and enabled flag do not authorize preview or select") {
  GoalUsageFixture fixture;
  fixture.runtime->config().set("context_execution", unrestricted_options());
  auto preview = unwrap(fixture.engine->build(fixture.request));
  CHECK(preview["goal"]["params"]["classifier"] == "cue");
  CHECK(preview["goal_typing_capability"]["available"] == true);
  CHECK(preview["goal_typing_capability"]["execution_scope_active"] == false);
  CHECK(fixture.transport->requests().empty());
  {
    ContextExecutionScope scope(unrestricted_options(false));
    const auto selected = unwrap(fixture.engine->build(fixture.request));
    CHECK(selected["goal"]["params"]["classifier"] == "cue");
    CHECK(selected["goal_typing_capability"]["calls_authorized"] == false);
    CHECK(selected["execution_usage"].empty());
    CHECK(scope.goal_typing_requests() == 0);
  }
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("goal execution: legacy ceilings stay scoped to the historical native compatibility adapter") {
  GoalUsageFixture fixture;
  const GoalTypingBudget large{3, 500000, 9000, 120000, 500000};
  const auto old_adapter = fixture.engine->type_goal_with_model(fixture.request, large);
  CHECK_FALSE(old_adapter);
  REQUIRE(old_adapter.error().code == Errc::InvalidArgument);
  fixture.response();
  ContextExecutionScope scope(unrestricted_options());
  const auto goal = unwrap(fixture.engine->type_goal_with_model(fixture.request, large));
#if LOOM_CONTEXT_HAS_USAGE_POLICY
  CHECK(goal.params["classifier"] == "llm");
  REQUIRE(fixture.transport->requests().size() == 1);
  CHECK(fixture.transport->requests()[0].timeout_ms == 120000);
  CHECK(unwrap(json::parse(fixture.transport->requests()[0].body))["max_tokens"] == 9000);
  CHECK(scope.goal_typing_requests() == 1);
  CHECK_FALSE(scope.usage_decisions().empty());
#else
  CHECK(goal.params["classifier"] == "cue");
  CHECK(goal.params["external_goal_typing"]["status"] == "usage_policy_unavailable");
  CHECK(fixture.transport->requests().empty());
  CHECK(scope.goal_typing_requests() == 0);
#endif
}

TEST_CASE("goal execution: unknown requested instrument remains in the availability report") {
  GoalUsageFixture fixture;
  fixture.request.candidate_channels.push_back(CandidateChannelRequest{"synthetic_missing_channel", 50, 0});
  const auto built = unwrap(fixture.engine->build(fixture.request));
  const auto& channels = built["selector_channels"];
  const auto found = std::find_if(channels.begin(), channels.end(), [](const auto& channel) {
    return json::get_string(channel, "id") == "synthetic_missing_channel";
  });
  REQUIRE(found != channels.end());
  CHECK((*found)["available"] == false);
  CHECK((*found)["last_retrieval_status"] == "unavailable");
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("goal execution: thrown transport retains partial raw response without retry or fabricated charge") {
  auto transport = std::make_shared<ThrowingPartialGoalTransport>();
  GoalUsageFixture fixture(transport);
#if LOOM_CONTEXT_HAS_USAGE_POLICY && LOOM_CONTEXT_CLAIM_HAS_DURABLE_DIRECTORIES
  ContextExecutionScope scope(unrestricted_options());
#endif
  const GoalTypingBudget budget{1, 256000, 4096, 60000, 256000};
  const auto goal = unwrap(fixture.engine->type_goal_with_model(fixture.request, budget));
  const auto& attempt = goal.params["external_goal_typing"];
  CHECK(goal.params["classifier"] == "cue");
  CHECK(attempt["requests"] == 1);
  CHECK(attempt["transport_error"] == "network");
  CHECK(attempt["response"]["source_status"] == "recorded");
  CHECK(attempt["response"]["complete"] == false);
  const auto raw = fixture.runtime->blobs().read(attempt["response"]["blob_hash"].get<std::string>());
  REQUIRE(raw);
  CHECK(*raw == transport->partial);
  const auto sources = fixture.runtime->provenance().find_sources_by_hash(Sha256::hex(transport->partial));
  REQUIRE(sources);
  REQUIRE(sources->size() == 1);
  CHECK(sources->front().metadata["complete"] == false);
  CHECK(transport->calls == 1);
  CHECK(goal.params.dump().find("SIMULATED_PRIVATE") == std::string::npos);
#if LOOM_CONTEXT_HAS_USAGE_POLICY && LOOM_CONTEXT_CLAIM_HAS_DURABLE_DIRECTORIES
  REQUIRE_FALSE(scope.usage_decisions().empty());
  const auto& actual = scope.usage_decisions().back()["actual"];
  CHECK(actual["requests"]["value"] == 1);
  CHECK(actual["response_bytes"]["value"] == transport->partial.size());
  CHECK(actual["output_tokens"]["value"].is_null());
  CHECK(actual["cost_usd"]["value"].is_null());
#endif
}

TEST_CASE("goal execution: exclusive claims retain unknown spend across independent scopes") {
  fsutil::TempDir directory;
  const Json receipt{{"operation_id", "synthetic/crash-operation"}, {"receipt_id", "synthetic_receipt"}, {"authorized", true}};
#if LOOM_CONTEXT_CLAIM_HAS_DURABLE_DIRECTORIES
  {
    ContextExecutionScope scope(Json::object());
    const auto first = unwrap(claim_context_usage_operation(directory.path(), receipt));
    CHECK(first["claimed"] == true);
    CHECK(first["amount_spent"].is_null());
  }
  {
    ContextExecutionScope scope(Json::object());
    const auto repeated = unwrap(claim_context_usage_operation(directory.path(), receipt));
    CHECK(repeated["claimed"] == false);
    CHECK(repeated["status"] == "already_started");
    CHECK(repeated["amount_spent"].is_null());
  }
  CHECK_FALSE(claim_context_usage_operation(directory.path(), Json{{"operation_id", "unauthorized"},
      {"receipt_id", "synthetic_receipt"}, {"authorized", false}}));
#else
  const auto unavailable = claim_context_usage_operation(directory.path(), receipt);
  REQUIRE_FALSE(unavailable);
  CHECK(unavailable.error().code == Errc::Unavailable);
#endif
}

#if LOOM_CONTEXT_HAS_USAGE_POLICY && LOOM_CONTEXT_CLAIM_HAS_DURABLE_DIRECTORIES
TEST_CASE("goal execution: unchanged output ceilings do not become expected x10 consumption") {
  GoalUsageFixture fixture;
  auto options = unrestricted_options();
  for (int iteration = 0; iteration < 2; ++iteration) {
    fixture.response();
    ContextExecutionScope scope(options);
    const auto built = unwrap(fixture.engine->build(fixture.request));
    CHECK(built["goal"]["params"]["classifier"] == "llm");
    REQUIRE_FALSE(scope.usage_decisions().empty());
    const auto& admission = scope.usage_decisions().front();
    CHECK(admission["status"] == "allowed");
    CHECK(admission["resources"]["output_tokens"]["estimate"].is_null());
    CHECK(admission["resources"]["response_bytes"]["estimate"].is_null());
    CHECK(admission["resources"]["output_tokens"]["requires_confirmation"] == false);
    CHECK(admission["resources"]["response_bytes"]["requires_confirmation"] == false);
    CHECK(admission["estimate"]["execution_budget"]["max_output_tokens"] == 9000);
    CHECK(admission["estimate"]["execution_budget"]["max_response_bytes"] == 500000);
    if (iteration == 1) {
      CHECK(admission["resources"]["output_tokens"]["baseline"] == 2);
      CHECK(admission["resources"]["response_bytes"]["baseline"].get<double>() > 0);
    }
  }
  REQUIRE(fixture.transport->requests().size() == 2);
  options["goal_typing"]["estimated_output_tokens"] = 20;
  {
    ContextExecutionScope scope(options);
    const auto paused = unwrap(fixture.engine->build(fixture.request));
    CHECK(paused["goal"]["params"]["external_goal_typing"]["status"] == "requires_confirmation");
    REQUIRE(scope.usage_decisions().size() == 1);
    CHECK(scope.usage_decisions().front()["resources"]["output_tokens"]["ratio"] == 10.0);
    CHECK(scope.goal_typing_requests() == 0);
  }
  CHECK(fixture.transport->requests().size() == 2);
}

TEST_CASE("goal execution: resumed admission cannot re-send a claimed unmeasured operation") {
  GoalUsageFixture fixture;
  Json options = unrestricted_options();
  const GoalTypingBudget budget{3, 500000, 9000, 120000, 500000};
  net::HttpRequest request;
  request.method = "POST";
  request.url = "https://goal.test/chat/completions";
  request.body = "{\"synthetic\":true}";
  Json receipt;
  {
    ContextExecutionScope scope(options);
    Json attempt{{"model", "fixture/goal"}};
    detail::GoalTypingUsage usage;
    REQUIRE(usage.admit(*fixture.runtime, options["goal_typing"], request, 18, budget, attempt));
    receipt = attempt["usage_policy"];
    fixture.response();
    REQUIRE(fixture.runtime->http().send(request));
    // Simulate loss after sending and before measurement/response persistence.
    // The admitted ledger record still has no actual measurements.
  }
  REQUIRE(fixture.transport->requests().size() == 1);
  options["goal_typing"]["usage"] = Json{{"resume_operation_id", receipt["operation_id"]}};
  {
    ContextExecutionScope scope(options);
    Json attempt{{"model", "fixture/goal"}};
    detail::GoalTypingUsage usage;
    CHECK_FALSE(usage.admit(*fixture.runtime, options["goal_typing"], request, 18, budget, attempt));
    CHECK(attempt["status"] == "operation_already_started");
    CHECK(attempt["execution_claim"]["claimed"] == false);
    CHECK(attempt["execution_claim"]["amount_spent"].is_null());
    REQUIRE_FALSE(scope.usage_decisions().empty());
    CHECK(scope.usage_decisions().back()["schema"] == "loom.context_execution_claim/1");
  }
  CHECK(fixture.transport->requests().size() == 1);
}

TEST_CASE("goal execution: exact x10 receipt pauses then resumes the default operation once") {
  GoalUsageFixture fixture;
  Json policy = usage_policy_defaults();
  policy["initial_baselines"] = Json{{"synthetic.goal", Json{{"requests", 1}, {"input_bytes", 100000},
      {"response_bytes", 100000}, {"output_tokens", 10000}, {"cost_usd", 0.1}}}};
  fixture.runtime->config().set("loom_usage_policy", policy);
  Json options = unrestricted_options();
  options["goal_typing"]["estimated_cost_usd"] = 1.0;
  options["goal_typing"]["usage"] = Json{{"baseline_key", "synthetic.goal"}};
  Json receipt;
  {
    ContextExecutionScope scope(options);
    const auto paused = unwrap(fixture.engine->build(fixture.request));
    CHECK(paused["goal"]["params"]["classifier"] == "cue");
    CHECK(paused["goal"]["params"]["external_goal_typing"]["status"] == "requires_confirmation");
    CHECK(scope.goal_typing_requests() == 0);
    REQUIRE(paused["execution_usage"].size() == 1);
    receipt = paused["execution_usage"][0];
    CHECK(receipt["resources"]["cost_usd"]["ratio"] == 10.0);
    CHECK(receipt["authorized"] == false);
  }
  CHECK(fixture.transport->requests().empty());
  options["goal_typing"]["usage"]["resume_operation_id"] = receipt["operation_id"];
  options["goal_typing"]["usage"]["confirmation"] = Json{{"receipt_id", receipt["receipt_id"]},
      {"approved", true}, {"ref", "synthetic-owner-confirmation"}};
  fixture.response();
  {
    ContextExecutionScope scope(options);
    const auto resumed = unwrap(fixture.engine->build(fixture.request));
    CHECK(resumed["goal"]["type"] == "verify_claim");
    CHECK(resumed["goal"]["params"]["classifier"] == "llm");
    CHECK(resumed["goal"]["confidence"] == 0.91);
    CHECK(scope.goal_typing_requests() == 1);
    REQUIRE_FALSE(resumed["execution_usage"].empty());
    const auto& final = resumed["execution_usage"].back();
    CHECK(final["operation_id"] == receipt["operation_id"]);
    CHECK(final["status"] == "completed");
    CHECK(final["actual"]["cost_usd"]["value"] == 1.0);
    CHECK(final["confirmation"]["ref"] == "synthetic-owner-confirmation");
    const auto& response = resumed["goal"]["params"]["external_goal_typing"]["response"];
    CHECK(response["source_status"] == "recorded");
    CHECK_FALSE(response["blob_hash"].get<std::string>().empty());
    CHECK(resumed.dump().find("synthetic-goal-token") == std::string::npos);
  }
  REQUIRE(fixture.transport->requests().size() == 1);
  {
    ContextExecutionScope scope(options);
    const auto repeated = unwrap(fixture.engine->build(fixture.request));
    CHECK(repeated["goal"]["params"]["external_goal_typing"]["status"] == "operation_already_attempted");
    CHECK(scope.goal_typing_requests() == 0);
  }
  CHECK(fixture.transport->requests().size() == 1);
}
#endif
