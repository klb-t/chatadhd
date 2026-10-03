// Hand-authored synthetic adapter tests. Real Runtime -> ScriptedTransport,
// no answer-quality claim, paid provider or evaluation corpus.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

model::Entity entity(const std::string& label) {
  model::Entity e;
  e.kind = "component";
  e.canonical_key = label;
  e.label = label;
  e.id = model::Entity::make_id(e.kind, label);
  e.origin = model::Origin::Archive;
  e.confidence = 1;
  return e;
}

model::Claim claim(const model::Entity& a, const model::Entity& b, const std::string& quote) {
  model::Claim c;
  c.subject = a.id;
  c.object = b.id;
  c.predicate = "depends_on";
  c.qualifiers.valid_from = "2026-09-30";
  c.assessment.evidence = model::EvidenceClass::Observed;
  c.assessment.origin = model::Origin::Archive;
  c.assessment.confidence = 0.9;
  model::Support support;
  support.observation = "ob.chat_retrieval.synthetic";
  support.extractor = "chat_retrieval_fixture@1";
  support.quote = quote;
  c.assessment.support = {support};
  c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  return c;
}

struct Fixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string run, conv;
  model::Entity a = entity("anchor alpha"), b = entity("anchor beta"), c = entity("anchor gamma");
  model::Entity x = entity("remote counter"), y = entity("remote endpoint"), z = entity("remote premise");
  model::Entity crypto = entity("remote encryption"), vault = entity("remote vault");
  model::Claim ab = claim(a, b, "RAW-ALPHA " + std::string(300, 'a') + " END-ALPHA");
  model::Claim bc = claim(b, c, "SECOND-HOP-ONLY");
  model::Claim counter = claim(x, y, "RECORDED-COUNTER-BYTES");
  model::Claim premise = claim(y, z, "REQUIRED-PREMISE-BYTES");
  model::Claim semantic = claim(crypto, vault, "ENCRYPTION-REMOTE-BYTES encryption");

  Fixture() {
    RuntimeOptions opts;
    opts.data_dir = dir.path().string();
    opts.start_workers = false;
    opts.http = transport;
    rt = unwrap(Runtime::open(opts));
    rt->config().set("base_url", "https://retrieval.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("system_prompt", "System instruction.");
    rt->config().set("semantic_analysis", false);
    rt->secrets().set("api_key", "synthetic-retrieval-test-key");
    conv = unwrap(rt->db().create_conv("Retrieval adapter fixture")).id;
    const auto pack = unwrap(rt->knowledge().pack());
    auto& store = rt->knowledge().store();
    run = unwrap(store.begin_run(pack->hash(), Json{{"fixture", "chat_retrieval_plan"}})).id;
    ab.assessment.counter.claims = {counter.id};
    ab.assessment.premises.claims = {premise.id};
    LOOM_REQUIRE_OK(store.put_entities(run, {a, b, c, x, y, z, crypto, vault}));
    LOOM_REQUIRE_OK(store.put_claims(run, {ab, bc, counter, premise, semantic}));
    LOOM_REQUIRE_OK(store.finish_run(run, "done", Json::object()));
  }

  Json request() const {
    return Json{{"run", run}, {"lang", "en"}, {"goal_type", "answer_question"},
                {"targets", Json::array({a.id})}, {"budget_tokens", 100000}};
  }

  Json option_json(const Json& request) const {
    return Json{{"conv_id", conv}, {"stream", false}, {"include_memory", false},
                {"include_graph_memory", false}, {"include_history", false}, {"knowledge_context", request}};
  }

  void reply() {
    transport->expect("POST", "https://retrieval.test/chat/completions", net::ScriptedTransport::Reply::json(
        200, Json{{"choices", Json::array({Json{{"message", {{"content", "Synthetic reply."}}}}})}}));
  }

  ChatResult send(const Json& request) {
    const auto options = unwrap(ChatOptions::from_json(option_json(request)));
    reply();
    return unwrap(rt->chat().send("CURRENT-TURN", options));
  }

  Json capture(const ChatResult& result, std::size_t index = 0) const {
    const auto requests = transport->requests();
    REQUIRE(requests.size() > index);
    const auto body = unwrap(json::parse(requests[index].body));
    CHECK(result.context_trace["messages"] == body["messages"]);
    CHECK(result.context_trace["messages_sha256"] == Sha256::hex(json::dump(body["messages"])));
    const auto saved = unwrap(rt->db().get_msg(result.user_message_id));
    REQUIRE(saved);
    CHECK(saved->metadata["context_trace"] == result.context_trace);
    CHECK(saved->text == "CURRENT-TURN");
    int current = 0, knowledge = 0;
    for (const auto& message : body["messages"]) {
      if (message["role"] == "user" && message["content"] == "CURRENT-TURN") ++current;
      if (message["role"] == "system" && message["content"] == result.context_trace["knowledge_context"]["prompt"]) ++knowledge;
    }
    CHECK(current == 1);
    CHECK(knowledge == 1);
    // Capture the actual serialized body, never authentication headers.
    return Json{{"schema", "loom.chat_retrieval_adapter_evidence/1"}, {"provenance", "synthetic_scripted_transport"},
                {"request_body_raw", requests[index].body}, {"request_body", body},
                {"context_trace", result.context_trace}, {"stored_context_trace", saved->metadata["context_trace"]}};
  }
};

Json plan(const Json& theses) {
  return Json{{"id", "explicit-retrieval-plan"},
      {"source_ref", {{"kind", "product"}, {"id", "unverified-caller-reference"},
                      {"version", 999}, {"note", "Żółć: declaration only"}}}, {"theses", theses}};
}

const Json& plan_trace(const ChatResult& result) {
  return result.context_trace.at("knowledge_context").at("context_set").at("goal").at("params").at("plan_trace");
}

bool selected(const Json& thesis, const std::string& ref, const std::string& resolution = "") {
  return std::any_of(thesis.at("selected_refs").begin(), thesis.at("selected_refs").end(), [&](const auto& row) {
    return row.at("id") == ref && (resolution.empty() || row.at("resolution") == resolution);
  });
}

bool prompt_contains(const ChatResult& result, const std::string& text) {
  return result.context_trace["knowledge_context"]["prompt"].get<std::string>().find(text) != std::string::npos;
}

void save_evidence(const std::string& name, const Json& evidence) {
  const char* directory = std::getenv("LOOM_CHAT_RETRIEVAL_EVIDENCE_DIR");
  if (!directory || !*directory) return;
  std::filesystem::create_directories(directory);
  const auto path = std::filesystem::path(directory) / (name + ".json");
  REQUIRE_FALSE(std::filesystem::exists(path));
  std::ofstream output(path);
  REQUIRE(output.good());
  output << evidence.dump(2) << '\n';
  output.close();
  REQUIRE(output.good());
}

}  // namespace

TEST_SUITE("chat_retrieval_plan") {
  TEST_CASE("all six explicit extensions reuse the canonical parser and preserve opaque provenance") {
    Json input{{"plan", plan(Json::array({Json{{"id", "one"}, {"text", "A caller-authored thesis."}}}))},
               {"claim_targets", Json::array({"cl.explicit"})}, {"include_counter_evidence", true},
               {"candidate_channels", Json::array({Json{{"id", "tfidf"}, {"limit", 7}, {"min_score", 0.1}}})},
               {"candidate_scan_limit", 23}, {"lexical_shadow", true}, {"relation_hops", 0}, {"detail_resolution", "raw"}};
    const auto parsed = unwrap(ChatOptions::from_json(Json{{"knowledge_context", input}}));
    REQUIRE(parsed.knowledge_context);
    CHECK(parsed.knowledge_context->to_json() == unwrap(context::ContextRequest::from_json(input)).to_json());
    CHECK(parsed.knowledge_context->plan == input["plan"]);
    CHECK_FALSE(parsed.active_task_spec);
    for (const auto& source : {Json(nullptr), Json("caller-note"), Json::array({"opaque", 3})}) {
      input["plan"]["source_ref"] = source;
      const auto next = unwrap(ChatOptions::from_json(Json{{"knowledge_context", input}}));
      CHECK(next.knowledge_context->plan["source_ref"] == source);
      CHECK_FALSE(next.active_task_spec);
    }
  }

  TEST_CASE("invalid extension types nested typos and duplicate identities fail before effects") {
    Fixture f;
    const auto valid_plan = plan(Json::array({Json{{"id", "one"}, {"text", "Explicit thesis"}}}));
    std::vector<Json> invalid;
    for (const auto& value : {Json(true), Json("plan"), Json::array(), Json::object()}) invalid.push_back(Json{{"plan", value}});
    for (const auto& value : {Json(nullptr), Json("cl.x"), Json::array({""}), Json::array({"cl.x", "cl.x"}), Json::array({true})}) {
      invalid.push_back(Json{{"claim_targets", value}});
    }
    for (const auto* field : {"include_counter_evidence", "lexical_shadow"}) {
      for (const auto& value : {Json(nullptr), Json(1), Json(0.0), Json("true")}) invalid.push_back(Json{{field, value}});
    }
    for (const auto& value : {Json(nullptr), Json(true), Json(0), Json(-1), Json(2.5), Json("8"), Json(std::numeric_limits<std::uint64_t>::max())}) {
      invalid.push_back(Json{{"candidate_scan_limit", value}});
    }
    for (const auto& value : {Json(nullptr), Json("tfidf"), Json::object(), Json::array({Json{{"id", ""}}}),
                             Json::array({Json{{"id", "tfidf"}}, Json{{"id", "tfidf"}}})}) {
      invalid.push_back(Json{{"candidate_channels", value}});
    }
    for (const auto& channel : {Json{{"id", "tfidf"}, {"limt", 1}}, Json{{"id", "tfidf"}, {"limit", true}},
                               Json{{"id", "tfidf"}, {"limit", 0}}, Json{{"id", "tfidf"}, {"min_score", true}},
                               Json{{"id", "tfidf"}, {"min_score", std::numeric_limits<double>::infinity()}}}) {
      invalid.push_back(Json{{"candidate_channels", Json::array({channel})}});
    }
    const std::vector<std::pair<std::string, Json>> wrong_thesis{
        {"id", " "}, {"text", 1}, {"claims", Json::array({"same", "same"})}, {"targets", Json::array({false})},
        {"relation_hops", true}, {"relation_hops", -1}, {"relation_hops", 1.5},
        {"detail_resolution", "automatic"}, {"require_counter_evidence", 1},
        {"budget_weight", 0}, {"budget_weight", true}, {"budget_weight", std::numeric_limits<double>::quiet_NaN()},
        {"claim_targets", Json::array({"wrong-level"})}};
    for (const auto& [field, value] : wrong_thesis) {
      auto bad = valid_plan;
      bad["theses"][0][field] = value;
      invalid.push_back(Json{{"plan", bad}});
    }
    auto bad = valid_plan;
    bad["theses"].push_back(bad["theses"][0]);
    invalid.push_back(Json{{"plan", bad}});
    bad = valid_plan;
    bad["active_task_spec"] = Json::object();
    invalid.push_back(Json{{"plan", bad}});
    invalid.push_back(Json{{"candidate_channnels", Json::array()}});
    const auto before = unwrap(f.rt->db().get_msgs(f.conv)).size();
    for (const auto& input : invalid) {
      INFO(input.dump());
      const auto parsed = ChatOptions::from_json(f.option_json(input));
      REQUIRE_FALSE(parsed);
      CHECK(parsed.error().code == Errc::InvalidArgument);
    }
    CHECK(f.transport->requests().empty());
    CHECK(unwrap(f.rt->db().get_msgs(f.conv)).size() == before);
  }

  TEST_CASE("actual plan sends explicit claims premises recorded counters and offline channel candidates") {
    Fixture f;
    auto request = f.request();
    request["claim_targets"] = Json::array({f.ab.id});
    request["include_counter_evidence"] = true;
    request["relation_hops"] = 2;
    request["detail_resolution"] = "label";
    request["candidate_channels"] = Json::array({Json{{"id", "tfidf"}, {"limit", 10}, {"min_score", 0}}});
    request["candidate_scan_limit"] = 20;
    request["lexical_shadow"] = true;
    request["plan"] = plan(Json::array({Json{{"id", "explicit"}, {"text", "szyfrowanie"},
        {"relation_hops", 0}, {"detail_resolution", "raw"}, {"require_counter_evidence", true}}}));
    const auto result = f.send(request);
    REQUIRE(f.transport->requests().size() == 1);
    const auto& trace = plan_trace(result);
    CHECK(trace["plan"] == request["plan"]);
    CHECK(result.context_trace["knowledge_context_request"]["plan"] == request["plan"]);
    CHECK_FALSE(result.context_trace.contains("active_task"));
    CHECK_FALSE(unwrap(f.rt->db().get_msg(result.user_message_id))->metadata.contains("active_task"));
    const auto& thesis = trace["theses"][0];
    CHECK(thesis["relation_hops"] == 0);
    CHECK(thesis["detail_resolution"] == "raw");
    CHECK(thesis["claims"] == Json::array({f.ab.id}));
    for (const auto& ref : {f.ab.id, f.premise.id, f.counter.id, f.semantic.id}) CHECK(selected(thesis, ref, "raw"));
    CHECK_FALSE(selected(thesis, f.bc.id));
    CHECK(thesis.contains("counter_evidence"));
    const auto& retrieval = thesis["selection_params"]["candidate_retrieval"];
    CHECK(retrieval["scan_limit"] == 20);
    CHECK(retrieval["channels"][0]["status"] == "ok");
    CHECK(retrieval["channels"][0]["method"] == "tfidf");
    REQUIRE(retrieval["channels"][0]["accepted_hits"].size() == 1);
    CHECK(retrieval["channels"][0]["accepted_hits"][0]["ref"] == f.semantic.id);
    CHECK(retrieval["lexical_shadow"]["status"] == "ok");
    CHECK(retrieval["lexical_shadow"]["hits"].empty());
    CHECK(retrieval["lexical_shadow"]["selection_effect"] == "none");
    for (const auto* text : {"END-ALPHA", "RECORDED-COUNTER-BYTES", "REQUIRED-PREMISE-BYTES", "ENCRYPTION-REMOTE-BYTES", "Retrieval plan coverage"}) {
      CHECK(prompt_contains(result, text));
    }
    CHECK_FALSE(prompt_contains(result, "SECOND-HOP-ONLY"));
    save_evidence("plan_counter_channels", f.capture(result));
  }

  TEST_CASE("per thesis scope and detail remain independent in the transmitted context") {
    Fixture f;
    auto request = f.request();
    request["plan"] = plan(Json::array({
        Json{{"id", "near-label"}, {"text", "Inspect the first relation"}, {"relation_hops", 1}, {"detail_resolution", "label"}, {"require_counter_evidence", false}},
        Json{{"id", "wide-raw"}, {"text", "Inspect both relations"}, {"relation_hops", 2}, {"detail_resolution", "raw"}, {"require_counter_evidence", false}}}));
    const auto result = f.send(request);
    const auto& theses = plan_trace(result)["theses"];
    CHECK(selected(theses[0], f.ab.id, "label"));
    CHECK_FALSE(selected(theses[0], f.bc.id));
    CHECK(selected(theses[1], f.ab.id, "raw"));
    CHECK(selected(theses[1], f.bc.id, "raw"));
    CHECK(selected(theses[0], f.premise.id));
    CHECK(selected(theses[1], f.premise.id));
    CHECK_FALSE(selected(theses[0], f.counter.id));
    CHECK_FALSE(selected(theses[1], f.counter.id));
    CHECK(prompt_contains(result, "END-ALPHA"));
    CHECK(prompt_contains(result, "SECOND-HOP-ONLY"));
    save_evidence("plan_scope_detail", f.capture(result));
  }

  TEST_CASE("counter policy also works without a plan and does not invent a plan") {
    Fixture f;
    auto request = f.request();
    request["claim_targets"] = Json::array({f.ab.id});
    request["relation_hops"] = 0;
    request["detail_resolution"] = "raw";
    const auto without = f.send(request);
    request["include_counter_evidence"] = true;
    const auto with = f.send(request);
    CHECK_FALSE(prompt_contains(without, "RECORDED-COUNTER-BYTES"));
    CHECK(prompt_contains(with, "RECORDED-COUNTER-BYTES"));
    CHECK(prompt_contains(without, "REQUIRED-PREMISE-BYTES"));
    CHECK(prompt_contains(with, "REQUIRED-PREMISE-BYTES"));
    CHECK_FALSE(with.context_trace["knowledge_context"]["context_set"]["goal"]["params"].contains("plan_trace"));
    REQUIRE(f.transport->requests().size() == 2);
    save_evidence("counter_without_plan", Json{{"disabled", f.capture(without, 0)}, {"enabled", f.capture(with, 1)}});
  }

  TEST_CASE("uninstalled channel remains unavailable and visible without another provider call") {
    Fixture f;
    auto request = f.request();
    request["candidate_channels"] = Json::array({Json{{"id", "embedding:not-installed"}}});
    request["plan"] = plan(Json::array({Json{{"id", "missing-capability"}, {"text", "Explicit remote instrument request"}}}));
    const auto result = f.send(request);
    REQUIRE(f.transport->requests().size() == 1);
    const auto& retrieval = plan_trace(result)["theses"][0]["selection_params"]["candidate_retrieval"];
    CHECK(retrieval["channels"][0]["status"] == "unavailable");
    CHECK(retrieval["channels"][0]["scores"].empty());
    CHECK(prompt_contains(result, "candidate_channel_unavailable"));
    save_evidence("unavailable_channel", f.capture(result));
  }

  TEST_CASE("caller plan provenance coexists with independently bound active task without certifying the plan") {
    Fixture f;
    NewMessage source;
    source.conv_id = f.conv;
    source.role = "user";
    source.text = "W1-GOAL: retain original audit identifiers.";
    const auto source_id = unwrap(f.rt->db().create_msg(source));
    const auto source_before = unwrap(f.rt->db().get_msg(source_id))->to_json();
    Json spec{{"schema", "loom.active_task_spec/1"}, {"product_ref", {{"kind", "product"}, {"id", "bound-task"}}},
        {"goal_id", "audit"}, {"knowledge_run", nullptr},
        {"scope", {{"conversation_id", f.conv}, {"branch_id", "native:active"}, {"task_id", "audit"}}},
        {"version", 1}, {"previous_product_ref", nullptr}, {"known_at", "2026-09-30T20:00:00Z"},
        {"representation", "derived_product"}, {"materializer", {{"id", "manual-fixture"}, {"version", "1"}}},
        {"history_event_ids", Json::array({"event.source"})},
        {"source_refs", Json::array({Json{{"event_id", "event.source"}, {"locator", {{"source", "synthetic-chat"}}},
                                         {"known_at", "2026-09-30T19:00:00Z"}, {"quote", source.text}}})},
        {"statements", Json::array({Json{{"id", "goal"}, {"kind", "goal"}, {"status", "active"}, {"text", source.text},
            {"source_event_ids", Json::array({"event.source"})}, {"claim_ids", Json::array()}, {"conditions", Json::array()}, {"supersedes", Json::array()}}})},
        {"compiled_instruction", {{"text", "placeholder"}, {"source_map", Json::array({Json{{"span", {{"byte_start", 0}, {"byte_len", 11}}}, {"statement_ids", Json::array({"goal"})}}})}}}};
    auto request = f.request();
    request["plan"] = plan(Json::array({Json{{"id", "caller-thesis"}, {"text", "Separate retrieval topic"}}}));
    // Even matching the product id does not verify the caller's version claim.
    request["plan"]["source_ref"]["id"] = "bound-task";
    auto options = f.option_json(request);
    options["active_task_spec"] = spec;
    options["active_task_bindings"] = Json{{"event.source", {{"message_id", source_id}, {"text_sha256", Sha256::hex(source.text)}}}};
    f.reply();
    const auto result = unwrap(f.rt->chat().send("CURRENT-TURN", unwrap(ChatOptions::from_json(options))));
    CHECK(result.context_trace["active_task"]["binding_verification"] == "native_message_text_sha256_and_optional_quote");
    CHECK(result.context_trace["active_task"]["supplied_spec"]["version"] == 1);
    CHECK(plan_trace(result)["plan"] == request["plan"]);
    CHECK(plan_trace(result)["plan"]["source_ref"]["version"] == 999);
    REQUIRE(plan_trace(result)["theses"].size() == 1);
    CHECK(plan_trace(result)["theses"][0]["text"] == "Separate retrieval topic");
    CHECK_FALSE(plan_trace(result).contains("binding_verification"));
    CHECK(unwrap(f.rt->db().get_msg(source_id))->to_json() == source_before);
    REQUIRE(f.transport->requests().size() == 1);
    save_evidence("independent_w1_w2", f.capture(result));
  }

  TEST_CASE("omitted and explicit default extensions leave the legacy recipe unchanged") {
    const Json defaults{{"plan", nullptr}, {"claim_targets", Json::array()}, {"include_counter_evidence", false},
                        {"candidate_channels", Json::array()}, {"candidate_scan_limit", 10000}, {"lexical_shadow", false}};
    const auto empty = unwrap(ChatOptions::from_json(Json{{"knowledge_context", Json::object()}}));
    const auto explicit_defaults = unwrap(ChatOptions::from_json(Json{{"knowledge_context", defaults}}));
    CHECK(empty.knowledge_context->to_json() == explicit_defaults.knowledge_context->to_json());
    const auto omitted = unwrap(ChatOptions::from_json(Json::object()));
    CHECK_FALSE(omitted.knowledge_context);
    Fixture f;
    int builds = 0;
    f.rt->chat().set_knowledge_context_builder([&](const context::ContextRequest&) -> Result<Json> {
      ++builds;
      return Error(Errc::Internal, "must not build");
    });
    f.reply();
    const auto result = unwrap(f.rt->chat().send("CURRENT-TURN", omitted));
    CHECK(builds == 0);
    CHECK(result.context_trace.is_null());
    REQUIRE(f.transport->requests().size() == 1);
    const auto body = unwrap(json::parse(f.transport->requests()[0].body));
    CHECK(body["messages"] == Json::array({Json{{"role", "system"}, {"content", "System instruction."}},
                                           Json{{"role", "user"}, {"content", "CURRENT-TURN"}}}));
  }
}
