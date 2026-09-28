// Full configured pipeline path with a local model transport, not model quality.
#include <doctest/doctest.h>

#include "loom/config.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
class GroundedTestTransport : public net::HttpTransport {
 public:
  std::vector<Json> requests;
  std::string name() const override { return "offline-grounded-test"; }
  Result<net::HttpResponse> send(const net::HttpRequest& request, const net::StreamSink* sink,
                               const CancelToken*) override {
    if (request.method != "POST" || request.url != "https://semantic.test/v1/chat/completions")
      return Error(Errc::Network, "test forbids unexpected network requests");
    LOOM_TRY_ASSIGN(auto body, json::parse(request.body));
    requests.push_back(body);
    LOOM_TRY_ASSIGN(auto input, json::parse(body.at("messages").at(1).at("content").get<std::string>()));
    REQUIRE(!input["entities"].empty());
    REQUIRE(!input["observations"].empty());
    const auto& observation = input["observations"][0];
    const auto quote = observation["text"].get<std::string>();
    Json draft{{"kind", "structure"}, {"unknowns", Json::array()},
      {"claim", Json{{"subject", input["entities"][0]["id"]}, {"predicate", "source_requirement"},
        {"object", ""}, {"value", "retain imported source bytes"},
        {"qualifiers", Json{{"extra", Json{{"polarity", "positive"}, {"assertion_context", "asserted"},
                                           {"topic", "source retention"}}}}},
        {"assessment", Json{{"basis", Json{{"support", Json::array({Json{
          {"observation", observation["id"]}, {"quote", quote}, {"byte_start", 0}, {"byte_len", quote.size()}}})}}},
          {"premises", Json{{"claims", Json::array()}}}}}}}};
    const Json content{{"schema_version", 1}, {"proposals", Json::array({draft})}};
    net::HttpResponse response;
    response.status = 200;
    const auto bytes = json::dump(Json{{"choices", Json::array({Json{{"message", Json{{"content", json::dump(content)}}}}})}});
    if (sink && sink->on_data) {
      if (!sink->on_data(bytes)) return Error(Errc::Cancelled, "test sink cancelled");
    } else {
      response.body = bytes;
    }
    return response;
  }
};
}

TEST_SUITE("knowledge_flow") {
  TEST_CASE("catalog through actual extraction uses configured cheap model and exposes scoped drafts") {
    fsutil::TempDir source, data;
    auto http = std::make_shared<GroundedTestTransport>();
    RuntimeOptions options;
    options.data_dir = data.path().string();
    options.start_workers = false;
    options.http = http;
    auto rt = unwrap(Runtime::open(options));
    rt->config().set("base_url", "https://semantic.test/v1");
    rt->config().set("semantic_model", "cheap/user-choice");
    rt->config().set("default_model", "different/chat-choice");
    rt->secrets().set("api_key", "offline-fixture-key");
    const auto path = source.path() / "requirements.md";
    unwrap(fsutil::write_file(path, "ChatADHD must preserve imported source bytes.\n"));
    knowledge::KnowledgeConfig cfg;
    cfg.sources = {path.string()};
    cfg.stages = {"catalog", "extract"};
    cfg.priors = false;
    cfg.stage_params = Json{{"catalog", Json{{"import", Json{{"mode", "full"}}}}},
                            {"extract", Json{{"semantic", Json{{"max_requests", 1}}}}}};
    auto without_model = unwrap(rt->knowledge().run(cfg));
    REQUIRE(without_model.status == "done");
    CHECK(http->requests.empty());
    auto baseline = unwrap(rt->knowledge().store().query_claims(without_model.run, kb::ClaimQuery{}));
    REQUIRE(!baseline.empty());

    cfg.llm = "auto";
    auto with_model = unwrap(rt->knowledge().run(cfg));
    REQUIRE(with_model.status == "done");
    REQUIRE(with_model.stages.size() == 2);
    CHECK(with_model.stages[1].stats["semantic"]["accepted"] == 1);
    REQUIRE(http->requests.size() == 1);
    CHECK(http->requests[0]["model"] == "cheap/user-choice");
    auto page = unwrap(rt->knowledge().store().query_candidates(with_model.run, "semantic_structure"));
    REQUIRE(page["total"] == 1);
    CHECK(page["items"][0]["eval"]["promoted"] == false);
    CHECK(page["items"][0]["support"][0]["quote"] == "ChatADHD must preserve imported source bytes.");
    CHECK(page["items"][0]["payload"]["provenance"]["model"] == "cheap/user-choice");
    auto canonical = unwrap(rt->knowledge().store().query_claims(with_model.run, kb::ClaimQuery{}));
    REQUIRE(canonical.size() == baseline.size());
    for (std::size_t i = 0; i < canonical.size(); ++i) CHECK(canonical[i].to_json() == baseline[i].to_json());
    CHECK(unwrap(rt->knowledge().store().query_candidates(without_model.run))["total"] == 0);
    auto repeat = unwrap(rt->knowledge().run(cfg));
    CHECK(repeat.run == with_model.run);
    CHECK(repeat.stages[1].cache_hit);
    CHECK(http->requests.size() == 1);
  }
}
