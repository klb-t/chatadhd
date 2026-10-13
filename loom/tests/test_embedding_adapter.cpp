// The text embedding call: the exact request it puts on the wire (URL, header
// order, body bytes, timeout) and how every kind of answer is read.
#include <doctest/doctest.h>

#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/providers.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "providers/embedding.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::providers;
using loom::test::unwrap;
using Reply = net::ScriptedTransport::Reply;

namespace {

struct Env {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  net::ScriptedTransport transport;
  std::unique_ptr<ProviderRegistry> registry;

  Env() {
    cfg.set("base_url", "https://openrouter.test/api/v1/");
    secrets.set("api_key", "sk-embed");
    secrets.set("local_key", "lk-embed");
    registry = std::make_unique<ProviderRegistry>(cfg, secrets);
    LOOM_REQUIRE_OK(registry->load_builtin());
    const Json embed = Json::array({Json{{"resource", "embedding"}, {"name", "embed"}, {"constraints", Json::object()}}});
    LOOM_REQUIRE_OK(registry->load(Json::array({
        Json{{"id", "local_embed"}, {"display_name", "Local"}, {"base_url", "http://localhost:9000/v1"},
             {"auth_scheme", "token"}, {"auth_secret", "local_key"}, {"default_headers", Json{{"X-Tenant", "t1"}}},
             {"capabilities", embed}, {"metadata", Json{{"embedding_path", "vectors"}}}},
        Json{{"id", "keyed_embed"}, {"display_name", "Keyed"}, {"base_url", "https://keyed.test"},
             {"auth_scheme", "x-api-key"}, {"auth_secret", "local_key"}, {"capabilities", embed}},
        Json{{"id", "query_embed"}, {"display_name", "Query"}, {"base_url", "https://query.test"},
             {"auth_scheme", "query:key"}, {"auth_secret", "local_key"}, {"capabilities", embed}}})));
  }

  EmbeddingRequest request(std::string provider = "openrouter", std::string model = "text-embedding-3-small") {
    EmbeddingRequest r;
    r.provider_id = std::move(provider);
    r.model = std::move(model);
    r.timeout_ms = 4321;
    r.calls_authorized = true;
    return r;
  }
  Result<EmbeddingReply> embed(const EmbeddingRequest& r, const std::vector<std::string>& texts, bool* attempted = nullptr) {
    return provider_embed(*registry, secrets, transport, r, texts, attempted);
  }
};

Json answer(std::initializer_list<std::vector<double>> vectors, std::string model = "text-embedding-3-small") {
  Json data = Json::array();
  int index = 0;
  for (const auto& v : vectors) data.push_back(Json{{"object", "embedding"}, {"index", index++}, {"embedding", v}});
  return Json{{"object", "list"}, {"data", data}, {"model", model}, {"usage", Json{{"prompt_tokens", 5}, {"total_tokens", 5}}}};
}

}  // namespace

TEST_SUITE("embedding_adapter") {
  TEST_CASE("golden: request bytes are pinned for every auth scheme") {
    Env env;
    EmbeddingRequest r = env.request();
    r.request_options = Json{{"dimensions", 2}};
    env.transport.expect("POST", "https://openrouter.test/api/v1/embeddings", Reply::json(200, answer({{0.5, -1.0}, {1.5, 2.0}})));
    bool attempted = false;
    auto reply = unwrap(env.embed(r, {"alpha \xC4\x85", "beta"}, &attempted));
    CHECK(attempted);
    REQUIRE(reply.vectors.size() == 2);
    CHECK(reply.vectors[0] == std::vector<float>{0.5f, -1.0f});
    CHECK(reply.vectors[1] == std::vector<float>{1.5f, 2.0f});
    CHECK(reply.usage["prompt_tokens"] == 5);
    CHECK(reply.requested_model == "text-embedding-3-small");
    REQUIRE(reply.reported_model);
    CHECK(*reply.reported_model == "text-embedding-3-small");

    env.transport.expect("POST", "http://localhost:9000/v1/vectors", Reply::json(200, answer({{1.0}}, "m")));
    unwrap(env.embed(env.request("local_embed", "m"), {"one"}));
    env.transport.expect("POST", "https://keyed.test/embeddings", Reply::json(200, answer({{1.0}}, "m")));
    unwrap(env.embed(env.request("keyed_embed", "m"), {"one"}));
    env.transport.expect("POST", "https://query.test/embeddings", Reply::json(200, answer({{1.0}}, "m")));
    unwrap(env.embed(env.request("query_embed", "m"), {"one"}));

    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 4);
    CHECK(loom::test::request_json(reqs[0]) ==
          Json{{"method", "POST"}, {"url", "https://openrouter.test/api/v1/embeddings"},
               {"headers", Json::array({Json::array({"Content-Type", "application/json"}),
                                        Json::array({"Authorization", "Bearer sk-embed"})})},
               {"body", "{\"dimensions\":2,\"model\":\"text-embedding-3-small\",\"input\":[\"alpha \xC4\x85\",\"beta\"],\"encoding_format\":\"float\"}"},
               {"timeout_ms", 4321}, {"stream", false}});
    CHECK(loom::test::request_json(reqs[1]) ==
          Json{{"method", "POST"}, {"url", "http://localhost:9000/v1/vectors"},
               {"headers", Json::array({Json::array({"X-Tenant", "t1"}), Json::array({"Content-Type", "application/json"}),
                                        Json::array({"Authorization", "token lk-embed"})})},
               {"body", "{\"model\":\"m\",\"input\":[\"one\"],\"encoding_format\":\"float\"}"},
               {"timeout_ms", 4321}, {"stream", false}});
    CHECK(loom::test::request_json(reqs[2]) ==
          Json{{"method", "POST"}, {"url", "https://keyed.test/embeddings"},
               {"headers", Json::array({Json::array({"Content-Type", "application/json"}), Json::array({"x-api-key", "lk-embed"})})},
               {"body", "{\"model\":\"m\",\"input\":[\"one\"],\"encoding_format\":\"float\"}"},
               {"timeout_ms", 4321}, {"stream", false}});
    CHECK(loom::test::request_json(reqs[3]) ==
          Json{{"method", "POST"}, {"url", "https://query.test/embeddings?key=lk-embed"},
               {"headers", Json::array({Json::array({"Content-Type", "application/json"})})},
               {"body", "{\"model\":\"m\",\"input\":[\"one\"],\"encoding_format\":\"float\"}"},
               {"timeout_ms", 4321}, {"stream", false}});
  }

  TEST_CASE("answers are read strictly: order by index, every malformed shape is an error, nothing is retried") {
    Env env;
    // Rows come back out of order; vectors follow their index.
    Json shuffled = answer({{1.0, 1.0}, {2.0, 2.0}});
    std::swap(shuffled["data"][0]["index"], shuffled["data"][1]["index"]);
    env.transport.expect("POST", "https://openrouter.test", Reply::json(200, shuffled));
    auto ok = unwrap(env.embed(env.request(), {"a", "b"}));
    CHECK(ok.vectors[0] == std::vector<float>{2.0f, 2.0f});
    CHECK(ok.vectors[1] == std::vector<float>{1.0f, 1.0f});

    struct Case { Reply reply; Errc code; std::string message; std::size_t texts = 2; };
    Json duplicate = answer({{1.0}, {2.0}});
    duplicate["data"][1]["index"] = 0;
    Json ragged = answer({{1.0}, {2.0, 3.0}});
    Json nonnumber = answer({{1.0}});
    nonnumber["data"][0]["embedding"] = Json::array({"x"});
    Json other_model = answer({{1.0}}, "someone/else");
    Json bad_model = answer({{1.0}});
    bad_model["model"] = 7;
    const std::vector<Case> cases{
        {Reply::json(200, answer({{1.0}, {2.0}, {3.0}})), Errc::Parse, "embedding response count mismatch"},
        {Reply::json(200, duplicate), Errc::Parse, "embedding response duplicate index"},
        {Reply::json(200, ragged), Errc::Parse, "embedding vector dimension mismatch"},
        {Reply::json(200, nonnumber), Errc::Parse, "embedding vector contains nonnumber", 1},
        {Reply::json(200, other_model), Errc::Conflict, "embedding reported model does not match requested binding", 1},
        {Reply::json(200, bad_model), Errc::Parse, "embedding reported model invalid", 1},
        {Reply::text(200, "[1]"), Errc::Parse, "embedding response is not a JSON object"},
        {Reply::text(200, "not json"), Errc::Parse, "embedding response is not a JSON object"},
        {Reply::text(503, "overloaded"), Errc::Http, "embedding HTTP request failed with status 503"},
        {Reply::fail(Errc::Timeout, "slow"), Errc::Timeout, "embedding transport failed"}};
    for (const auto& c : cases) {
      INFO(c.message);
      env.transport.expect("POST", "https://openrouter.test", c.reply);
      const std::size_t before = env.transport.requests().size();
      auto got = env.embed(env.request(), std::vector<std::string>(c.texts, "t"));
      REQUIRE_FALSE(got);
      CHECK(got.error().code == c.code);
      CHECK(got.error().message == c.message);
      CHECK(env.transport.requests().size() == before + 1);   // one attempt, no retry
    }
    // An accepted alias is the caller's data.
    env.transport.expect("POST", "https://openrouter.test", Reply::json(200, other_model));
    EmbeddingRequest aliased = env.request();
    aliased.accepted_reported_models = {"someone/else"};
    CHECK(unwrap(env.embed(aliased, {"a"})).reported_model == std::optional<std::string>("someone/else"));
  }

  TEST_CASE("refusals before any byte leaves: authorization, identity, options, missing credential") {
    Env env;
    bool attempted = true;
    EmbeddingRequest unauthorized = env.request();
    unauthorized.calls_authorized = false;
    auto refused = env.embed(unauthorized, {"a"}, &attempted);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == Errc::Unavailable);
    CHECK_FALSE(attempted);

    EmbeddingRequest stale = env.request();
    stale.expected_identity = "0000";
    CHECK(env.embed(stale, {"a"}).error().code == Errc::Conflict);

    EmbeddingRequest overriding = env.request();
    overriding.request_options = Json{{"input", "x"}};
    CHECK(env.embed(overriding, {"a"}).error().code == Errc::InvalidArgument);
    EmbeddingRequest encoded = env.request();
    encoded.request_options = Json{{"encoding_format", "base64"}};
    CHECK(env.embed(encoded, {"a"}).error().code == Errc::Unsupported);

    // No texts: identity only, nothing sent.
    auto empty = unwrap(env.embed(env.request(), {}));
    CHECK(empty.vectors.empty());
    CHECK_FALSE(empty.identity.empty());

    env.secrets.set("api_key", "");
    CHECK(env.embed(env.request(), {"a"}).error().code == Errc::Unavailable);
    CHECK(env.transport.requests().empty());
  }

  TEST_CASE("the first response is recorded before it is judged, also when it is an error") {
    Env env;
    std::vector<std::pair<std::string, Json>> recorded;
    EmbeddingRequest r = env.request();
    r.record_response = [&](std::string_view bytes, const Json& meta) -> Result<Json> {
      recorded.emplace_back(std::string(bytes), meta);
      return Json{{"source_id", "src_1"}};
    };
    env.transport.expect("POST", "https://openrouter.test", Reply::text(500, "{\"error\":\"boom\"}"));
    CHECK_FALSE(env.embed(r, {"a"}));
    REQUIRE(recorded.size() == 1);
    CHECK(recorded[0].first == "{\"error\":\"boom\"}");
    CHECK(recorded[0].second["status"] == 500);
    CHECK(recorded[0].second["complete"] == true);
    CHECK(recorded[0].second["provider_id"] == "openrouter");
    CHECK(recorded[0].second["input_hashes"][0] == Sha256::hex("a"));
    CHECK(recorded[0].second["transport_error"].is_null());

    env.transport.expect("POST", "https://openrouter.test", Reply::json(200, answer({{1.0}})));
    auto good = unwrap(env.embed(r, {"a"}));
    REQUIRE(recorded.size() == 2);
    CHECK(recorded[1].second["reported_model"] == "text-embedding-3-small");
    CHECK(good.response_source == Json{{"source_id", "src_1"}});

    // A recorder that fails stops the call: bytes that cannot be kept are not used.
    r.record_response = [](std::string_view, const Json&) -> Result<Json> { return Error(Errc::Io, "disk"); };
    env.transport.expect("POST", "https://openrouter.test", Reply::json(200, answer({{1.0}})));
    auto lost = env.embed(r, {"a"});
    REQUIRE_FALSE(lost);
    CHECK(lost.error().message == "embedding first-response storage failed");
  }

  TEST_CASE("identity and capability reflect the manifest, the endpoint and the account, never the key") {
    Env env;
    const auto id1 = unwrap(embedding_identity(*env.registry, env.secrets, env.request()));
    CHECK(id1.size() == 64);
    CHECK(id1.find("sk-embed") == std::string::npos);
    env.secrets.set("api_key", "sk-other");
    CHECK(unwrap(embedding_identity(*env.registry, env.secrets, env.request())) != id1);
    const Json cap = embedding_capability(*env.registry, env.secrets, env.request());
    CHECK(cap["available"] == true);
    CHECK(cap["modalities"] == Json::array({"text"}));
    EmbeddingRequest unknown = env.request("no_such_provider");
    CHECK(embedding_capability(*env.registry, env.secrets, unknown)["available"] == false);
  }
}
