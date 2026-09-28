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
  bool occurrence_mode = false;
  std::string name() const override { return "offline-grounded-test"; }
  Result<net::HttpResponse> send(const net::HttpRequest& request, const net::StreamSink* sink,
                               const CancelToken*) override {
    if (request.method != "POST" || request.url != "https://semantic.test/v1/chat/completions")
      return Error(Errc::Network, "test forbids unexpected network requests");
    LOOM_TRY_ASSIGN(auto body, json::parse(request.body));
    requests.push_back(body);
    LOOM_TRY_ASSIGN(auto input, json::parse(body.at("messages").at(1).at("content").get<std::string>()));
    if (occurrence_mode) {
      const auto& packet = input.at("source_packet");
      REQUIRE(packet.at("schema") == "loom.source_packet/1");
      REQUIRE(!packet.at("observations").empty());
      const auto& observation = packet.at("observations").at(0);
      REQUIRE(observation.contains("unit"));
      for (const auto& claim : packet.at("claims")) REQUIRE(claim.contains("assessment"));
      const auto quote = observation.at("text").get<std::string>();
      const Json support = Json::array({Json{{"observation", observation.at("id")},
        {"byte_start", 0}, {"byte_len", quote.size()}, {"quote", quote}}});
      Json es = Json::array({
        Json{{"handle", "@s"}, {"kind", "scope"}, {"label", "assertion"},
             {"attrs", Json{{"scope_type", "assertion"}, {"assertion_context", "asserted"}}}, {"support", support}},
        Json{{"handle", "@p"}, {"kind", "expression_occurrence"}, {"label", "source requirement"},
             {"attrs", Json::object()}, {"support", support}},
        Json{{"handle", "@f"}, {"kind", "term_occurrence"}, {"label", "preserve"},
             {"attrs", Json{{"term_type", "predicate"}, {"symbol", "preserve"}}}, {"support", support}}});
      Json cs = Json::array();
      auto add = [&](const char* subject, const char* predicate, const char* object, Json value, Json extra = Json::object()) {
        extra["polarity"] = "positive";
        extra["assertion_context"] = "asserted";
        cs.push_back(Json{{"handle", "@c" + std::to_string(cs.size())}, {"subject", subject}, {"predicate", predicate},
          {"object", object}, {"value", value}, {"qualifiers", Json{{"scope", "@s"}, {"extra", extra}}},
          {"assessment", Json{{"basis", Json{{"support", support}}}, {"premises", Json{{"claims", Json::array()}}}}}});
      };
      add("@p", "operation_type", "", "predicate_application");
      add("@p", "operand", "@f", nullptr, Json{{"port", "predicate"}, {"ordinal", 0}});
      add("@p", "in_scope", "@s", nullptr);
      add("@f", "in_scope", "@s", nullptr);
      const Json bundle{{"schema", "loom.candidate_graph/1"}, {"packet_id", packet.at("snapshot_id")},
        {"entity_drafts", es}, {"claim_drafts", cs}, {"roots", Json::array({"@p"})},
        {"coverage", Json::array({Json{{"support", support}, {"status", "represented"},
          {"reason", "scripted transport interpretation"}, {"drafts", Json::array({"@p"})}}})}, {"unknowns", Json::array()}};
      const Json content{{"schema_version", 2}, {"packet_hash", input.at("packet_hash")}, {"bundles", Json::array({bundle})}};
      const auto bytes = json::dump(Json{{"choices", Json::array({Json{{"finish_reason", "stop"},
        {"message", Json{{"content", json::dump(content)}}}}})}});
      net::HttpResponse response;
      response.status = 200;
      if (sink && sink->on_data) {
        if (!sink->on_data(bytes)) return Error(Errc::Cancelled, "test sink cancelled");
      } else response.body = bytes;
      return response;
    }
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
  TEST_CASE("catalog graph mode retains source packet and caches separately from flat relations") {
    fsutil::TempDir source, data;
    auto http = std::make_shared<GroundedTestTransport>();
    http->occurrence_mode = true;
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
      {"extract", Json{{"semantic", Json{{"representation", "occurrence_graph_v1"}, {"max_requests", 1}}}}}};
    const auto baseline = unwrap(rt->knowledge().run(cfg));
    REQUIRE(baseline.status == "done");
    CHECK(http->requests.empty());
    auto old_claims = unwrap(rt->knowledge().store().query_claims(baseline.run, kb::ClaimQuery{}));
    cfg.llm = "auto";
    const auto graphed = unwrap(rt->knowledge().run(cfg));
    REQUIRE(graphed.status == "done");
    REQUIRE(http->requests.size() == 1);
    CHECK(http->requests.front()["model"] == "cheap/user-choice");
    const auto& stats = graphed.stages.at(1).stats.at("semantic");
    CHECK(stats.at("accepted_bundles") == 1);
    CHECK(stats.at("entity_drafts") == 3);
    CHECK(stats.at("claim_drafts") == 4);
    auto page = unwrap(rt->knowledge().store().query_candidates(graphed.run, "semantic_structure"));
    REQUIRE(page.at("total") == 1);
    const auto& item = page.at("items").at(0);
    CHECK(item.at("payload").at("representation") == "occurrence_graph_v1");
    CHECK(item.at("payload").at("proposal").at("bundle").at("roots") == Json::array({"@p"}));
    CHECK(item.at("eval").at("promoted") == false);
    CHECK(item.at("support").at(0).at("quote") == "ChatADHD must preserve imported source bytes.");
    auto new_claims = unwrap(rt->knowledge().store().query_claims(graphed.run, kb::ClaimQuery{}));
    REQUIRE(old_claims.size() == new_claims.size());
    for (std::size_t i = 0; i < old_claims.size(); ++i) CHECK(old_claims[i].to_json() == new_claims[i].to_json());
    const auto again = unwrap(rt->knowledge().run(cfg));
    CHECK(again.run == graphed.run);
    CHECK(again.stages.at(1).cache_hit);
    CHECK(http->requests.size() == 1);
    cfg.stage_params["extract"]["semantic"]["representation"] = "relation_v1";
    http->occurrence_mode = false;
    const auto flat = unwrap(rt->knowledge().run(cfg));
    CHECK(flat.status == "done");
    CHECK(flat.run != graphed.run);
    CHECK(http->requests.size() == 2);
  }

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
