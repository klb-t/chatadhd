// Offline regressions. Linked with selector.verify.cc's doctest main; no CMake changes.
#include "provider_vector.h"

#include <cmath>
#include <limits>

#include "doctest/doctest.h"
#include "loom/config.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/provenance.h"
#include "loom/selector.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "context_execution.h"

namespace {
using namespace loom;
using namespace loom::context;

struct EmbeddingFixture {
  fsutil::TempDir directory{"loom_provider_vector_"};
  Config config{directory.path() / "config.json"};
  Secrets secrets{directory.path() / "secrets.json"};
  ProviderRegistry registry{config, secrets};
  net::ScriptedTransport transport;
  providers::EmbeddingRequest request;
  std::vector<Json> estimates, actuals;

  EmbeddingFixture() {
    secrets.set("api_key", "synthetic-test-token");
    REQUIRE(registry.load_builtin());
    request.provider_id = "openrouter";
    request.model = "synthetic/embedding";
    request.calls_authorized = true;
  }
  static Json response(const std::vector<std::vector<float>>& values) {
    Json data = Json::array();
    for (std::size_t i = values.size(); i > 0; --i)
      data.push_back(Json{{"index", i - 1}, {"embedding", values[i - 1]}});
    return Json{{"data", data}, {"usage", Json{{"prompt_tokens", 7}, {"cost", 0.001}}}};
  }
  void expect(std::vector<std::vector<float>> values) {
    transport.expect("POST", "https://openrouter.ai/api/v1/embeddings", net::ScriptedTransport::Reply::json(200, response(values)));
  }
  ProviderVectorPolicy policy(std::string status = "allowed") {
    ProviderVectorPolicy policy;
    policy.admit = [this, status](const Json& estimate) -> Result<Json> {
      estimates.push_back(estimate);
      return Json{{"status", status}, {"receipt_id", "synthetic_receipt"}};
    };
    policy.complete = [this](std::string_view, const Json& actual) -> Result<Json> {
      actuals.push_back(actual);
      return Json{{"status", "completed"}};
    };
    return policy;
  }
  std::shared_ptr<resolve::VectorSpace> space(std::shared_ptr<ProviderVectorCache> cache = nullptr,
                                           std::string status = "allowed") {
    return make_provider_vector_space(registry, secrets, transport, request,
                                     std::move(cache), policy(std::move(status)));
  }
};

class FakeEmbedding final : public EmbeddingProvider {
 public:
  std::size_t calls = 0;
  std::vector<std::string> last;
  std::vector<float> value{1, 0};
  bool invalid_count = false;
  std::string model_id() const override { return "synthetic/same-model"; }
  Result<std::vector<std::vector<float>>> embed(const std::vector<std::string>& texts) override {
    ++calls;
    last = texts;
    return std::vector<std::vector<float>>(texts.size() + (invalid_count ? 1 : 0), value);
  }
};

std::unique_ptr<Runtime> recording_runtime(const std::filesystem::path& directory) {
  RuntimeOptions options;
  options.data_dir = directory.string();
  options.start_workers = false;
  options.http = std::make_shared<net::ScriptedTransport>();
  auto opened = Runtime::open(options);
  if (!opened) FAIL("recording fixture failed: " << opened.error().to_string());
  return std::move(*opened);
}

class PartialEmbeddingTransport final : public net::HttpTransport {
 public:
  std::string partial = "{\"data\":[SIMULATED_PRIVATE_PARTIAL";
  std::size_t calls = 0;
  Result<net::HttpResponse> send(const net::HttpRequest&, const net::StreamSink* sink,
                                  const CancelToken*) override {
    ++calls;
    if (sink) {
      if (sink->on_headers) sink->on_headers(200, {});
      if (sink->on_data) sink->on_data(partial);
    }
    return Error(Errc::Timeout, "SIMULATED_PRIVATE_TRANSPORT_ERROR");
  }
  std::string name() const override { return "synthetic_partial_embedding"; }
};

TEST_CASE("provider vector: first responses are immutable raw sources before validation") {
  EmbeddingFixture f;
  auto runtime = recording_runtime(f.directory.path() / "recording");
  f.request.record_response = make_embedding_response_recorder(*runtime);
  auto valid = EmbeddingFixture::response({{1, 0}});
  valid["provider_echo"] = "SIMULATED_PRIVATE_RESPONSE";
  const std::vector<std::pair<int, std::string>> responses{
      {200, " \n" + json::dump(valid) + "\n"},
      {200, "{ malformed SIMULATED_PRIVATE_RESPONSE"},
      {402, "{\"error\":\"SIMULATED_PRIVATE_RESPONSE\"}"}};
  ContextExecutionScope scope(Json::object());
  for (std::size_t index = 0; index < responses.size(); ++index) {
    const auto& [status, bytes] = responses[index];
    f.transport.expect("POST", "https://openrouter.ai/api/v1/embeddings", net::ScriptedTransport::Reply::text(status, bytes));
    auto reply = providers::provider_embed(f.registry, f.secrets, f.transport, f.request, {"exact synthetic input"});
    CHECK(static_cast<bool>(reply) == (index == 0));
    if (reply) CHECK(reply->response_source["source_status"] == "recorded");
    auto sources = runtime->provenance().find_sources_by_hash(Sha256::hex(bytes));
    REQUIRE(sources);
    REQUIRE(sources->size() == 1);
    const auto& source = sources->front();
    auto raw = runtime->blobs().read(source.blob_hash);
    REQUIRE(raw);
    CHECK(*raw == bytes);
    CHECK(source.metadata["complete"] == true);
    CHECK(source.metadata["status"] == status);
    CHECK(source.metadata["input_hashes"] == Json::array({Sha256::hex("exact synthetic input")}));
    CHECK_FALSE(source.metadata["request_sha256"].get<std::string>().empty());
    CHECK_FALSE(source.metadata["identity"].get<std::string>().empty());
  }
  REQUIRE(scope.usage_decisions().size() == 3);
  CHECK(json::dump(scope.usage_decisions()).find("SIMULATED_PRIVATE_RESPONSE") == std::string::npos);
  CHECK(f.transport.requests().size() == 3);
}

TEST_CASE("provider vector: partial first response survives timeout without invented charge") {
  EmbeddingFixture f;
  auto runtime = recording_runtime(f.directory.path() / "recording");
  f.request.record_response = make_embedding_response_recorder(*runtime);
  PartialEmbeddingTransport transport;
  auto cache = std::make_shared<ProviderVectorCache>();
  auto space = make_provider_vector_space(f.registry, f.secrets, transport, f.request, cache, f.policy());
  ContextExecutionScope scope(Json::object());
  auto result = space->vectors({{"q", "text", "partial input", "", ""}});
  REQUIRE_FALSE(result);
  auto sources = runtime->provenance().find_sources_by_hash(Sha256::hex(transport.partial));
  REQUIRE(sources);
  REQUIRE(sources->size() == 1);
  auto raw = runtime->blobs().read(sources->front().blob_hash);
  REQUIRE(raw);
  CHECK(*raw == transport.partial);
  CHECK(sources->front().metadata["complete"] == false);
  CHECK(sources->front().metadata["transport_error"] == "timeout");
  CHECK(transport.calls == 1);
  CHECK(cache->size() == 0);
  REQUIRE(f.actuals.size() == 1);
  CHECK(f.actuals[0]["resources"]["input_tokens"].is_null());
  CHECK(f.actuals[0]["resources"]["cost_usd"].is_null());
  CHECK(json::dump(scope.usage_decisions()).find("SIMULATED_PRIVATE_PARTIAL") == std::string::npos);
}

TEST_CASE("provider vector: raw-source storage failure refuses embeddings and preserves unknown charge") {
  EmbeddingFixture f;
  auto runtime = recording_runtime(f.directory.path() / "recording");
  f.request.record_response = make_embedding_response_recorder(*runtime);
  std::error_code error;
  std::filesystem::remove_all(runtime->blobs().root(), error);
  REQUIRE_FALSE(error);
  REQUIRE(fsutil::write_file(runtime->blobs().root(), "synthetic storage obstruction"));
  f.expect({{1, 0}});
  auto cache = std::make_shared<ProviderVectorCache>();
  ContextExecutionScope scope(Json::object());
  auto result = f.space(cache)->vectors({{"q", "text", "storage-failure input", "", ""}});
  REQUIRE_FALSE(result);
  CHECK(result.error().message == "embedding first-response storage failed");
  CHECK(cache->size() == 0);
  CHECK(f.transport.requests().size() == 1);
  REQUIRE(f.actuals.size() == 1);
  CHECK(f.actuals[0]["resources"]["input_tokens"].is_null());
  CHECK(f.actuals[0]["resources"]["cost_usd"].is_null());
  REQUIRE(scope.usage_decisions().size() == 1);
  CHECK(scope.usage_decisions()[0]["source_status"] == "unavailable");
}

TEST_CASE("provider vector: manifest adapter explicit authorization and ordered reply") {
  EmbeddingFixture f;
  CHECK(f.registry.can("embedding", "embed", Json{{"modalities", Json::array({"text"})}}));
  f.request.calls_authorized = false;
  auto denied = providers::provider_embed(f.registry, f.secrets, f.transport, f.request, {"a"});
  CHECK_FALSE(denied);
  CHECK(f.transport.requests().empty());
  f.request.calls_authorized = true;
  f.request.timeout_ms = 120000;  // configurable, no arbitrary old media timeout ceiling
  f.request.request_options = Json{{"dimensions", 2}, {"custom_option", "preserved"}};
  f.expect({{1, 0}, {0, 1}});
  auto reply = providers::provider_embed(f.registry, f.secrets, f.transport, f.request, {"a", "b"});
  REQUIRE(reply);
  CHECK(reply->vectors == std::vector<std::vector<float>>{{1, 0}, {0, 1}});
  REQUIRE(f.transport.requests().size() == 1);
  CHECK(f.transport.requests().front().timeout_ms == 120000);
  auto body = json::parse(f.transport.requests().front().body);
  REQUIRE(body);
  CHECK((*body)["custom_option"] == "preserved");
  CHECK((*body)["input"] == Json::array({"a", "b"}));
}

TEST_CASE("provider vector: malformed responses never enter the cache") {
  EmbeddingFixture f;
  auto cache = std::make_shared<ProviderVectorCache>();
  const std::vector<Json> invalid{
      Json{{"data", Json::array({Json{{"index", 0}, {"embedding", Json::array({1, 0})}},
                                Json{{"index", 0}, {"embedding", Json::array({0, 1})}}})}},
      Json{{"data", Json::array({Json{{"index", 0}, {"embedding", Json::array({1})}}})}},
      Json{{"data", Json::array({Json{{"index", 0}, {"embedding", Json::array({1, 0})}},
                                Json{{"index", 1}, {"embedding", Json::array({1})}}})}},
      Json{{"data", Json::array({Json{{"index", -1}, {"embedding", Json::array({1})}},
                                Json{{"index", 1}, {"embedding", Json::array({1})}}})}},
      Json{{"data", Json::array({Json{{"index", 0}, {"embedding", Json::array({"bad"})}},
                                Json{{"index", 1}, {"embedding", Json::array({1})}}})}},
      Json{{"data", Json::array({Json{{"index", 0}, {"embedding", Json::array({1e-200})}},
                                Json{{"index", 1}, {"embedding", Json::array({1})}}})}}};
  for (const auto& response : invalid) {
    f.transport.expect("POST", "https://openrouter.ai/api/v1/embeddings", net::ScriptedTransport::Reply::json(200, response));
    auto vectors = f.space(cache)->vectors({{"a", "text", "a", "", ""}, {"b", "text", "b", "", ""}});
    CHECK_FALSE(vectors);
    CHECK(cache->size() == 0);
    REQUIRE_FALSE(f.actuals.empty());
    CHECK(f.actuals.back()["resources"]["cost_usd"].is_null());
  }
}

TEST_CASE("provider vector: exact bytes cache and dedup precede admission") {
  EmbeddingFixture f;
  auto cache = std::make_shared<ProviderVectorCache>();
  auto space = f.space(cache);
  f.expect({{1, 0}, {0, 1}});
  auto first = space->vectors({{"a", "text", "alpha", "", "caller-wrong-hash"},
                              {"b", "text", "beta", "", "caller-wrong-hash"},
                              {"dup", "text", "alpha", "", ""}});
  REQUIRE(first);
  CHECK(first->size() == 3);
  CHECK((*first)[0] == (*first)[2]);
  CHECK((*first)[0] != (*first)[1]);
  CHECK(cache->size() == 2);
  REQUIRE(f.estimates.size() == 1);
  CHECK(f.estimates[0]["resources"]["input_items"] == 2);
  CHECK(f.estimates[0]["resources"]["input_bytes"] == 9);
  CHECK(f.estimates[0]["resources"]["cost_usd"].is_null());
  f.request.calls_authorized = false;
  auto offline = f.space(cache)->vectors({{"moved-id", "text", "alpha", "", "different-wrong-hash"}});
  REQUIRE(offline);
  CHECK(f.transport.requests().size() == 1);
  CHECK(f.estimates.size() == 1);
  auto miss = f.space(cache)->vectors({{"a", "text", "changed", "", "caller-wrong-hash"}});
  CHECK_FALSE(miss);
  CHECK(f.transport.requests().size() == 1);
}

TEST_CASE("provider vector: identity separates model options account and provider") {
  EmbeddingFixture f;
  auto cache = std::make_shared<ProviderVectorCache>();
  auto query = std::vector<resolve::EmbedInput>{{"q", "text", "same bytes", "", ""}};
  f.expect({{1, 0}});
  REQUIRE(f.space(cache)->vectors(query));
  f.request.model = "synthetic/other-model";
  f.expect({{0, 1}});
  REQUIRE(f.space(cache)->vectors(query));
  f.request.request_options = Json{{"dimensions", 2}};
  f.expect({{1, 1}});
  REQUIRE(f.space(cache)->vectors(query));
  f.secrets.set("api_key", "synthetic-other-account");
  f.expect({{1, -1}});
  REQUIRE(f.space(cache)->vectors(query));
  auto manifest = f.registry.get("openrouter");
  REQUIRE(manifest);
  manifest->id = "same-endpoint-other-provider";
  REQUIRE(f.registry.load(Json::array({manifest->to_json()})));
  f.request.provider_id = manifest->id;
  f.expect({{-1, 0}});
  REQUIRE(f.space(cache)->vectors(query));
  CHECK(f.transport.requests().size() == 5);
  CHECK(cache->size() == 5);
}

TEST_CASE("provider vector: durable cache can serve offline after recreation") {
  EmbeddingFixture f;
  auto cache = std::make_shared<ProviderVectorCache>(f.directory.path() / "vectors");
  f.expect({{1, 0}});
  REQUIRE(f.space(cache)->vectors({{"q", "text", "durable", "", ""}}));
  auto restarted = std::make_shared<ProviderVectorCache>(f.directory.path() / "vectors");
  f.request.calls_authorized = false;
  REQUIRE(f.space(restarted)->vectors({{"new-q", "text", "durable", "", ""}}));
  CHECK(f.transport.requests().size() == 1);
  CHECK(restarted->size() == 1);
}

TEST_CASE("provider vector: confirmation blocks transport and unknown charges survive failures") {
  EmbeddingFixture f;
  auto pending = f.space(nullptr, "requires_confirmation")->vectors({{"q", "text", "growth", "", ""}});
  REQUIRE_FALSE(pending);
  CHECK(pending.error().code == Errc::Paused);
  CHECK(pending.error().message.find("synthetic_receipt") != std::string::npos);
  CHECK(f.transport.requests().empty());
  CHECK(f.actuals.empty());
  f.transport.expect("POST", "https://openrouter.ai/api/v1/embeddings", net::ScriptedTransport::Reply::fail(Errc::Timeout, "private-provider-echo"));
  auto failed = f.space()->vectors({{"q", "text", "attempted", "", ""}});
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("private-provider-echo") == std::string::npos);
  REQUIRE(f.actuals.size() == 1);
  CHECK(f.actuals[0]["resources"]["requests"] == 1);
  CHECK(f.actuals[0]["resources"]["input_tokens"].is_null());
  CHECK(f.actuals[0]["resources"]["cost_usd"].is_null());
}

TEST_CASE("provider vector: confirmation retries retain the immutable operation") {
  EmbeddingFixture f;
  auto policy = f.policy();
  bool confirmed = false;
  policy.admit = [&](const Json& estimate) -> Result<Json> {
    f.estimates.push_back(estimate);
    return Json{{"status", confirmed ? "allowed" : "requires_confirmation"}, {"receipt_id", "same-receipt"}};
  };
  auto space = make_provider_vector_space(f.registry, f.secrets, f.transport, f.request, nullptr, policy);
  const std::vector<resolve::EmbedInput> query{{"q", "text", "same immutable bytes", "", ""}};
  CHECK_FALSE(space->vectors(query));
  confirmed = true;
  f.expect({{1, 0}});
  REQUIRE(space->vectors(query));
  REQUIRE(f.estimates.size() == 2);
  CHECK(f.estimates[0] == f.estimates[1]);
  CHECK(f.estimates[0]["input_hashes"].size() == 1);
  CHECK(f.transport.requests().size() == 1);
}

TEST_CASE("provider vector: resumed receipt is consumed before distinct later batches") {
  EmbeddingFixture f;
  auto policy = f.policy();
  policy.operation_id = "synthetic_previously_confirmed";
  auto space = make_provider_vector_space(f.registry, f.secrets, f.transport, f.request, nullptr, policy);
  f.expect({{1, 0}});
  REQUIRE(space->vectors({{"q", "text", "confirmed first batch", "", ""}}));
  f.expect({{0, 1}});
  REQUIRE(space->vectors({{"q2", "text", "independent next batch", "", ""}}));
  REQUIRE(f.estimates.size() == 2);
  CHECK(f.estimates[0]["operation_id"] == "synthetic_previously_confirmed");
  CHECK(f.estimates[1]["operation_id"] != f.estimates[0]["operation_id"]);
}

TEST_CASE("provider vector: pre-send account change cancels without invented consumption") {
  EmbeddingFixture f;
  auto policy = f.policy();
  std::size_t cancellations = 0;
  policy.admit = [&](const Json&) -> Result<Json> {
    f.secrets.set("api_key", "synthetic-account-changed-at-admission");
    return Json{{"status", "allowed"}};
  };
  policy.cancel = [&](std::string_view, std::string_view reason) -> Result<Json> {
    ++cancellations;
    CHECK(reason == "embedding_request_not_sent");
    return Json{{"status", "cancelled"}};
  };
  auto space = make_provider_vector_space(f.registry, f.secrets, f.transport, f.request, nullptr, policy);
  auto vectors = space->vectors({{"q", "text", "race", "", ""}});
  CHECK_FALSE(vectors);
  CHECK(cancellations == 1);
  CHECK(f.actuals.empty());
  CHECK(f.transport.requests().empty());
}

TEST_CASE("provider vector: injected owner cache survives fresh spaces and isolates replacement") {
  auto provider = std::make_shared<FakeEmbedding>();
  const std::vector<resolve::EmbedInput> query{{"q", "text", "native", "", ""}};
  REQUIRE(make_injected_vector_space(provider)->vectors(query));
  REQUIRE(make_injected_vector_space(provider)->vectors(query));
  CHECK(provider->calls == 1);
  auto replacement = std::make_shared<FakeEmbedding>();
  replacement->value = {0, 1};
  auto replaced = make_injected_vector_space(replacement)->vectors(query);
  REQUIRE(replaced);
  CHECK(replacement->calls == 1);
  CHECK(replaced->front().count("#0") == 0);
  CHECK(replaced->front().count("#1") == 1);
}

TEST_CASE("provider vector: offline injected preview reads cache without invoking provider") {
  auto provider = std::make_shared<FakeEmbedding>();
  bool authorized_scope = false;
  auto space = make_injected_vector_space(provider, nullptr, [&] { return authorized_scope; });
  const std::vector<resolve::EmbedInput> query{{"q", "text", "native-scope", "", ""}};
  CHECK_FALSE(space->vectors(query));
  CHECK(provider->calls == 0);
  authorized_scope = true;
  REQUIRE(space->vectors(query));
  CHECK(provider->calls == 1);
  authorized_scope = false;
  REQUIRE(space->vectors(query));
  CHECK(provider->calls == 1);
  CHECK_FALSE(space->vectors({{"q2", "text", "uncached native-scope", "", ""}}));
  CHECK(provider->calls == 1);
}

TEST_CASE("provider vector: runtime scope requires explicit injected call authorization") {
  fsutil::TempDir directory;
  RuntimeOptions runtime_options;
  runtime_options.data_dir = directory.path().string();
  runtime_options.start_workers = false;
  runtime_options.http = std::make_shared<net::ScriptedTransport>();
  auto opened = Runtime::open(runtime_options);
  REQUIRE(opened);
  auto runtime = std::move(*opened);
  auto loaded_pack = runtime->knowledge().pack();
  REQUIRE(loaded_pack);
  auto pack = *loaded_pack;
  auto& store = runtime->knowledge().store();
  auto begun = store.begin_run(pack->hash(), Json{{"fixture", "injected_authorization"}});
  REQUIRE(begun);
  model::Entity entity;
  entity.id = "e_injected_authorization";
  entity.kind = "component";
  entity.canonical_key = "injected authorization";
  entity.label = "Synthetic component";
  REQUIRE(store.put_entities(begun->id, {entity}));
  model::Claim claim;
  claim.id = "cl_injected_authorization";
  claim.subject = entity.id;
  claim.predicate = "records";
  claim.value = "synthetic source retained";
  claim.assessment.evidence = model::EvidenceClass::User;
  claim.assessment.origin = model::Origin::User;
  claim.assessment.confidence = 1;
  REQUIRE(store.put_claims(begun->id, {claim}));
  REQUIRE(store.finish_run(begun->id, "done", Json::object()));
  auto provider = std::make_shared<FakeEmbedding>();
  runtime->set_embedding_provider(provider);
  ContextEngine engine(*runtime, store, pack);
  ContextRequest request;
  request.text = "synthetic component context";
  request.run = begun->id;
  Json options{{"embedding", Json{{"enabled", true}}}};
  auto blocked = build_context_with_execution(engine, request, *runtime, options);
  REQUIRE(blocked);
  CHECK(provider->calls == 0);
  options["embedding"]["calls_authorized"] = true;
  auto authorized = build_context_with_execution(engine, request, *runtime, options);
  REQUIRE(authorized);
  CHECK(provider->calls > 0);  // proves the fixture exercised an actual miss
  const auto calls = provider->calls;
  request.candidate_channels = {{"vector", 50, 0}};
  REQUIRE(engine.build(request));
  CHECK(provider->calls == calls);  // preview may use only the warmed cache
  request.text = "different uncached query";
  REQUIRE(engine.build(request));
  CHECK(provider->calls == calls);
}

TEST_CASE("provider vector: invalid injected values do not poison cache and scaling stays finite") {
  auto provider = std::make_shared<FakeEmbedding>();
  auto cache = std::make_shared<ProviderVectorCache>();
  auto space = make_injected_vector_space(provider, cache);
  const std::vector<resolve::EmbedInput> query{{"q", "text", "native", "", ""}};
  provider->value = {std::numeric_limits<float>::quiet_NaN(), 1};
  CHECK_FALSE(space->vectors(query));
  CHECK(cache->size() == 0);
  provider->value = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  auto huge = space->vectors(query);
  REQUIRE(huge);
  CHECK(std::isfinite(resolve::cosine(huge->front(), huge->front())));
  CHECK(resolve::cosine(huge->front(), huge->front()) == doctest::Approx(1.0));
  provider->invalid_count = true;
  CHECK_FALSE(space->vectors({{"q2", "text", "other", "", ""}}));
  CHECK(cache->size() == 1);
}

TEST_CASE("provider vector: unavailable modality and empty text have no fabricated measurement") {
  auto provider = std::make_shared<FakeEmbedding>();
  auto vectors = make_injected_vector_space(provider)->vectors({{"q", "audio", "caption", "", ""},
                                                                {"empty", "text", "", "", ""}});
  REQUIRE(vectors);
  CHECK(vectors->size() == 2);
  CHECK(vectors->at(0).empty());
  CHECK(vectors->at(1).empty());
  CHECK(provider->calls == 0);
}

}  // namespace
