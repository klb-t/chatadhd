// Real ChatEngine sends with synthetic transports only. Linked by W3's runner.
#include <doctest/doctest.h>

#include "loom/chat_engine.h"
#include "loom/db.h"
#include "loom/graph_packet_store.h"
#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"
#include "graph_reply.h"

#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#endif

namespace {
using namespace loom;
using loom::test::unwrap;

struct ChatGraphFixture {
  fsutil::TempDir directory;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  explicit ChatGraphFixture(std::shared_ptr<net::HttpTransport> custom = {}) {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = custom ? std::move(custom) : transport;
    runtime = unwrap(Runtime::open(options));
    runtime->config().set("base_url", "https://chatgraph.test");
    runtime->config().set("default_model", "synthetic/main");
    runtime->config().set("system_prompt", "Synthetic stable prefix.");
    runtime->config().set("semantic_analysis", false);
    runtime->secrets().set("api_key", "synthetic-private-token");
#if __has_include("loom/usage_policy.h")
    runtime->config().set("loom_usage_policy", usage_policy_defaults());
#endif
  }
  void options(const Json& graph) {
    runtime->config().set("context_execution", Json{{"graph_reply", graph}});
  }
  ChatResult send(std::string_view text = "Synthetic user request") {
    ChatOptions options;
    options.stream = false;
    return unwrap(runtime->chat().send(text, options));
  }
  void response(std::string_view text) {
    transport->expect("POST", "https://chatgraph.test/chat/completions", net::ScriptedTransport::Reply::json(200,
        Json{{"choices", Json::array({Json{{"message", Json{{"content", text}}}}})},
            {"usage", Json{{"prompt_tokens", 10}, {"completion_tokens", 2}}}}));
  }
};

Json graph_origin() {
  return Json{{"kind", "system"}, {"actor", "synthetic-chat-adapter"}, {"model", nullptr},
      {"recipe_sha256", nullptr}, {"response_sha256", nullptr}};
}
Json graph_profile(bool stream = false) {
  const Json vocabulary{{"kinds", Json{{"method", "fixture.method"}, {"method_version", "fixture.method_version"},
      {"recipe_version", "fixture.recipe_version"}, {"run", "fixture.run"},
      {"parameter_set_version", "fixture.parameter_set_version"},
      {"compiler_transform", "fixture.compiler_transform"}}},
    {"predicates", Json{{"version_of", "fixture.version_of"}, {"uses_recipe", "fixture.uses_recipe"},
      {"requests_method_version", "fixture.requests_method_version"}, {"produced_in_run", "fixture.produced_in_run"},
      {"uses_parameter_set", "fixture.uses_parameter_set"}, {"uses_combination", "fixture.uses_combination"},
      {"produced_by_method_version", "fixture.produced_by_method_version"}, {"projected_by_compiler", "fixture.projected_by_compiler"}}}};
  const Json method_definition{{"execution_capability", "llm.chat.completions"}, {"parameters", Json::object()}};
  const Json recipe_definition{{"model", "synthetic/graph"}, {"parameters", Json::object()},
      {"request", Json{{"model", "synthetic/graph"}, {"messages", Json::array({Json{{"role", "user"}, {"content", ""}}})},
          {"temperature", 0.0}, {"max_tokens", 100}, {"stream", stream}}},
      {"request_bindings", Json::array({Json{{"target", Json::array({"messages", 0, "content"})},
          {"source", Json::array({"base_packet_sha256"})}}})}};
  auto entity = [&](std::string id, std::string kind, Json attrs) {
    model::Entity value;
    value.id = std::move(id);
    value.kind = std::move(kind);
    value.canonical_key = value.id;
    value.label = value.id;
    value.evidence = model::EvidenceClass::User;
    value.origin = model::Origin::User;
    value.attrs = std::move(attrs);
    return value.to_json();
  };
  auto version_attrs = [](const Json& definition) {
    return Json{{"definition", definition}, {"definition_sha256", Sha256::hex(json::canonical(definition))}};
  };
  Json entities = Json::array({entity("fx_chat_method", "fixture.method", Json::object()),
      entity("fx_chat_version", "fixture.method_version", version_attrs(method_definition)),
      entity("fx_chat_recipe", "fixture.recipe_version", version_attrs(recipe_definition)),
      entity("fx_chat_compiler", "fixture.compiler_transform", Json{{"implementation", "native_graph_reply"}})});
  auto edge = [](std::string id, std::string subject, std::string predicate, std::string object) {
    model::Claim claim;
    claim.id = std::move(id);
    claim.subject = std::move(subject);
    claim.predicate = std::move(predicate);
    claim.object = std::move(object);
    claim.value = nullptr;
    claim.assessment.evidence = model::EvidenceClass::User;
    claim.assessment.origin = model::Origin::User;
    claim.assessment.confidence = 1;
    return claim.to_json();
  };
  return Json{{"vocabulary", vocabulary}, {"entities", entities},
      {"claims", Json::array({edge("fx_version_edge", "fx_chat_version", "fixture.version_of", "fx_chat_method"),
          edge("fx_recipe_edge", "fx_chat_version", "fixture.uses_recipe", "fx_chat_recipe")})},
      {"sources", Json::array()}, {"selection", Json{{"members", Json::array({Json{{"method_version_id", "fx_chat_version"}}})},
          {"parameter_layers", Json::array({"recipe", "method", "user"})}}}};
}
Json prepared_graph_options(bool authorized) {
  return Json{{"mode", "answer_as_graph"}, {"profile", graph_profile()},
      {"run_context", Json{{"origin", graph_origin()}, {"compiler_transform_id", "fx_chat_compiler"}}},
      {"apply_policy", Json{{"schema", "loom.graph_packet_apply_policy/1"}, {"acceptance", "auto"},
          {"allow_source_tombstones", false}}},
      {"admission", Json{{"mode", "candidate"}}},
      {"transport", Json{{"calls_authorized", authorized},
          {"usage_estimate", Json{{"baseline_key", "synthetic.chat.graph"},
              {"resources", Json{{"output_tokens", 2}, {"cost_usd", nullptr}}}}}}}};
}

// Bind the fake model response to the packet actually sent by ChatEngine,
// rather than compiling a fixture packet outside the send path.
class BoundChatGraphTransport final : public net::HttpTransport {
 public:
  bool malformed = false;
  bool wrapped = false;
  bool separate = false;
  std::string reported_model;
  std::vector<net::HttpRequest> requests;
  std::vector<std::string> contents;
  std::vector<std::string> wire;
  Result<net::HttpResponse> send(const net::HttpRequest& request, const net::StreamSink* sink, const CancelToken*) override {
    requests.push_back(request);
    auto payload = json::parse(request.body);
    if (!payload) return payload.error();
    std::string content;
    if (separate && requests.size() == 1) content = "Primary ordinary answer";
    else if (malformed) content = wrapped ? json::dump(Json{{"text", "Human readable answer"},
        {"graph", Json{{"schema", "invalid-graph"}, {"content", "Invalid graph content Ω🙂"}}}}) : "Invalid graph content Ω🙂";
    else {
      const std::string packet_id = (*payload)["messages"][0]["content"].get<std::string>();
      Json graph{{"schema", "loom.graph_reply/2"}, {"base_packet_sha256", packet_id}, {"response_id", "root"},
          {"nodes", Json::array({Json{{"id", "root"}, {"role", "response"}, {"text", nullptr}, {"children", Json::array({"leaf"})}},
              Json{{"id", "leaf"}, {"role", "part"}, {"text", "Aą🙂"}, {"children", Json::array()}}})},
          {"links", Json::array()}};
      content = wrapped ? json::dump(Json{{"text", "Human readable answer"}, {"graph", graph}}) : json::dump(graph);
    }
    contents.push_back(content);
    Json response{{"choices", Json::array({Json{{"message", Json{{"content", content}}}}})},
        {"usage", Json{{"prompt_tokens", 10}, {"completion_tokens", 2}, {"cost", 0.001}}}};
    if (!reported_model.empty()) response["model"] = reported_model;
    std::string bytes = json::dump(response);
    if (request.stream) {
      response["choices"][0].erase("message");
      response["choices"][0]["delta"] = Json{{"content", content}};
      bytes = "data: " + json::dump(response) + "\n\ndata: [DONE]\n\n";
    }
    if (sink) {
      if (sink->on_headers && !sink->on_headers(200, {})) return Error(Errc::Cancelled, "fixture headers aborted");
      if (sink->on_data && !sink->on_data(bytes)) return Error(Errc::Cancelled, "fixture data aborted");
    }
    wire.push_back(bytes);
    return net::HttpResponse{200, {{"Content-Type", "application/json"}}, sink ? std::string() : bytes};
  }
  std::string name() const override { return "bound_chat_graph_fixture"; }
};

[[maybe_unused]] void add_output_binding(Json& options) {
  auto& recipe = options["profile"]["entities"][2]["attrs"];
  recipe["definition"]["output_binding"] = Json{{"graph_pointer", "/graph"}, {"text_pointer", "/text"}};
  recipe["definition_sha256"] = Sha256::hex(json::canonical(recipe["definition"]));
}

[[maybe_unused]] void add_postprocess_provider(Json& options) {
  options["provider_manifests"] = Json::array({Json{{"id", "synthetic_graph_provider"}, {"display_name", "Synthetic graph provider"},
      {"base_url", "https://chatgraph.test"}, {"auth_scheme", "none"}, {"auth_secret", ""},
      {"default_headers", Json::object()}, {"capabilities", Json::array({Json{{"resource", "llm"},
          {"name", "chat.completions"}, {"constraints", Json::object()}}})},
      {"metadata", Json{{"chat_completions_path", "/graph-afterwards"}}}}});
  options["postprocess_transport"] = options["transport"];
  options["postprocess_transport"]["provider_id"] = "synthetic_graph_provider";
  options["postprocess_transport"]["timeout_ms"] = 100000;
}

[[maybe_unused]] std::string check_parameter_graph(const Json& graph) {
  REQUIRE(graph.contains("manifest"));
  const auto& manifest = graph["manifest"];
  REQUIRE(manifest.contains("bindings"));
  const auto& bindings = manifest["bindings"];
  REQUIRE(bindings.contains("parameter_set_version_id"));
  REQUIRE(bindings["parameter_set_version_id"].is_string());
  const auto id = bindings["parameter_set_version_id"].get<std::string>();
  const auto& trace = manifest["trace"];
  REQUIRE(trace.contains("parameter_set_version_id"));
  CHECK(trace["parameter_set_version_id"] == id);
  const Json expected_definition{{"effective_parameters", trace["effective_parameters"]},
      {"user_overrides", trace["user_overrides"]}};
  const auto expected_hash = Sha256::hex(json::canonical(expected_definition));
  CHECK(trace["parameter_set_sha256"] == expected_hash);
  CHECK(manifest["definition_hashes"]["parameter_set"] == expected_hash);
  bool actual_entity = false;
  for (const auto& entity : graph["candidate_packet"]["entities"]) if (entity["id"] == id) {
    actual_entity = true;
    CHECK(entity["kind"] == "fixture.parameter_set_version");
    CHECK(entity["attrs"]["definition"] == expected_definition);
    CHECK(entity["attrs"]["definition_sha256"] == expected_hash);
  }
  CHECK(actual_entity);
  for (const char* source : {"method_version_id", "run_id"}) {
    bool actual_edge = false;
    for (const auto& claim : graph["candidate_packet"]["claims"])
      if (claim["subject"] == bindings[source] && claim["predicate"] == "fixture.uses_parameter_set" && claim["object"] == id)
        actual_edge = true;
    CHECK(actual_edge);
  }
  if (bindings.contains("combination_version_id") && !bindings["combination_version_id"].is_null()) {
    bool actual_edge = false;
    for (const auto& claim : graph["candidate_packet"]["claims"])
      if (claim["subject"] == bindings["run_id"] && claim["predicate"] == "fixture.uses_combination" &&
          claim["object"] == bindings["combination_version_id"]) actual_edge = true;
    CHECK(actual_edge);
  }
  return id;
}
}

TEST_CASE("chat graph wiring: explicit off has identical request bytes and no source side effects") {
  ChatGraphFixture plain, off;
  off.options(Json{{"mode", "off"}, {"profile", "invalid-unused-data"}});
  plain.response("Same answer");
  off.response("Same answer");
  const auto a = plain.send(), b = off.send();
  CHECK(a.text == b.text);
  REQUIRE(plain.transport->requests().size() == 1);
  REQUIRE(off.transport->requests().size() == 1);
  CHECK(plain.transport->requests()[0].body == off.transport->requests()[0].body);
  const auto assistant = unwrap(off.runtime->db().get_msg(b.assistant_message_id));
  REQUIRE(assistant);
  CHECK_FALSE(assistant->metadata.contains("graph_reply"));
  CHECK_FALSE(std::filesystem::exists(off.directory.path() / "context_execution_claims"));
}

TEST_CASE("chat graph wiring: unavailable recipe retains malformed first wire bytes and assistant text") {
  ChatGraphFixture fixture;
  fixture.options(Json{{"mode", "answer_as_graph"}});
  const std::string raw = "{ incomplete provider response α🙂";
  fixture.transport->expect("POST", "https://chatgraph.test/chat/completions", net::ScriptedTransport::Reply::text(200, raw));
  const auto result = fixture.send();
  CHECK(result.text == raw);
  const auto assistant = unwrap(fixture.runtime->db().get_msg(result.assistant_message_id));
  REQUIRE(assistant);
  CHECK(assistant->text == raw);
  const auto& graph = assistant->metadata["graph_reply"];
  CHECK(graph["status"] == "transport_error");
  CHECK(graph["error"]["code"] == "parse");
  CHECK(graph["model_content_origin"] == "model");
  CHECK(graph["canonical_store_written"] == false);
  CHECK(graph["first_response_ref"]["blob_hash"] == Sha256::hex(raw));
  CHECK(unwrap(fixture.runtime->blobs().read(graph["first_response_ref"]["blob_hash"].get<std::string>())) == raw);
  CHECK(result.context_trace["graph_reply"] == graph);
  CHECK(graph.dump().find("synthetic-private-token") == std::string::npos);
}

TEST_CASE("chat graph wiring: SSE chunks are stored before cancellation and partial text survives") {
  ChatGraphFixture fixture;
  fixture.options(Json{{"mode", "separate_model_afterwards"}});
  const auto reply = net::ScriptedTransport::Reply::sse({
      "{\"choices\":[{\"delta\":{\"content\":\"Aą🙂\"}}]}",
      "{\"choices\":[{\"delta\":{\"content\":\"discarded tail\"}}]}", "[DONE]"});
  const std::string first_chunk = reply.chunks.front();
  fixture.transport->expect("POST", "https://chatgraph.test/chat/completions", reply);
  CancelToken cancel;
  ChatOptions options;
  options.stream = true;
  ChatCallbacks callbacks;
  callbacks.on_chunk = [&](std::string_view) {
    CHECK(fixture.runtime->blobs().has(Sha256::hex(first_chunk)));
    cancel.cancel();
  };
  const auto result = unwrap(fixture.runtime->chat().send("Synthetic streaming request", options, callbacks, &cancel));
  CHECK(result.cancelled);
  CHECK(result.text == "Aą🙂");
  REQUIRE(fixture.transport->requests().size() == 1);
  const auto assistant = unwrap(fixture.runtime->db().get_msg(result.assistant_message_id));
  REQUIRE(assistant);
  const auto& graph = assistant->metadata["graph_reply"];
  CHECK(graph["status"] == "cancelled");
  CHECK(graph["transport_chunk_sources"].size() == 1);
  CHECK(unwrap(fixture.runtime->blobs().read(graph["primary_response_ref"]["blob_hash"].get<std::string>())) == first_chunk);
}

TEST_CASE("chat graph wiring: actual prepared graph dispatch fails closed without owner authorization") {
  ChatGraphFixture fixture;
  fixture.options(prepared_graph_options(false));
  ChatOptions options;
  options.stream = false;
  const auto result = fixture.runtime->chat().send("Synthetic graph request", options);
#if __has_include("packet/packet.h")
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Errc::Paused);
  CHECK(fixture.transport->requests().empty());
  auto conversation = fixture.runtime->chat().current_conv();
  REQUIRE(conversation);
  const auto rows = unwrap(fixture.runtime->db().get_msgs(conversation->id));
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].metadata["context_trace"]["graph_reply"]["status"] == "authorization_required");
#else
  // Without W4, an ordinary primary chat remains possible; this is not a
  // graph-generation request. No prepared recipe is dispatched or accepted.
  REQUIRE(result);
  CHECK(result->context_trace["graph_reply"]["status"] == "transport_error");
#endif
}

TEST_CASE("chat graph wiring: actual W4 and W2 dispatch binds candidate results and verified UTF-8 fragments") {
#if __has_include("packet/packet.h") && __has_include("loom/usage_policy.h")
  auto transport = std::make_shared<BoundChatGraphTransport>();
  ChatGraphFixture fixture(transport);
  fixture.options(prepared_graph_options(true));
  const auto result = fixture.send();
  REQUIRE(transport->requests.size() == 1);
  CHECK(result.text == "Aą🙂");
  const auto assistant = unwrap(fixture.runtime->db().get_msg(result.assistant_message_id));
  REQUIRE(assistant);
  const auto& graph = assistant->metadata["graph_reply"];
  REQUIRE(graph["status"] == "candidate");
  CHECK(graph["retained_text"] == transport->contents[0]);
  CHECK(graph["first_response_ref"]["blob_hash"] == Sha256::hex(transport->wire[0]));
  CHECK(graph["model_content_origin"] == "model");
  CHECK(graph["canonical_store_written"] == false);
  CHECK(graph["manifest"]["trace"]["request_bytes_sha256"] == Sha256::hex(transport->requests[0].body));
  REQUIRE(graph.contains("usage_settlement"));
  check_parameter_graph(graph);
  const auto& candidate = graph["candidate_packet"];
  for (const auto& id : graph["compilation"]["node_ids"]) {
    bool method_edge = false, compiler_edge = false;
    for (const auto& claim : candidate["claims"]) if (claim["subject"] == id) {
      method_edge = method_edge || claim["predicate"] == "fixture.produced_by_method_version";
      compiler_edge = compiler_edge || claim["predicate"] == "fixture.projected_by_compiler";
    }
    CHECK(method_edge);
    CHECK(compiler_edge);
    for (const auto& entity : candidate["entities"]) if (entity["id"] == id) {
      CHECK(entity["origin"] == "model_knowledge");
      CHECK(entity["attrs"]["model_origin"]["kind"] == "model");
    }
  }
  const auto fragment = unwrap(chat::graph_reply_fragment(graph, Json{{"local_id", "leaf"}}));
  CHECK(fragment["text"] == "Aą🙂");
  CHECK(fragment["span"]["byte_len"] == 7);
  CHECK(fragment["span"]["char_len"] == 3);
  CHECK_FALSE(fixture.runtime->db().conn().has_table("loom_kb_graph_receipts"));
#else
  CHECK(true); // Combined dependency runner executes the actual graph path.
#endif
}

TEST_CASE("chat graph wiring: all generating modes retain original text on actual compiler schema errors") {
#if __has_include("packet/packet.h") && __has_include("loom/usage_policy.h")
  for (const char* mode : {"answer_as_graph", "text_plus_JSONgraph", "separate_model_afterwards"}) {
    CAPTURE(std::string(mode));
    auto transport = std::make_shared<BoundChatGraphTransport>();
    transport->malformed = true;
    transport->wrapped = std::string_view(mode) == "text_plus_JSONgraph";
    transport->separate = std::string_view(mode) == "separate_model_afterwards";
    ChatGraphFixture fixture(transport);
    auto options = prepared_graph_options(true);
    options["mode"] = mode;
    if (std::string_view(mode) == "text_plus_JSONgraph") add_output_binding(options);
    if (transport->separate) add_postprocess_provider(options);
    fixture.options(options);
    const auto result = fixture.send();
    REQUIRE(transport->requests.size() == (transport->separate ? 2 : 1));
    const std::string retained = transport->separate ? "Primary ordinary answer" :
        transport->wrapped ? "Human readable answer" : transport->contents[0];
    CHECK(result.text == retained);
    const auto assistant = unwrap(fixture.runtime->db().get_msg(result.assistant_message_id));
    REQUIRE(assistant);
    const auto& graph = assistant->metadata["graph_reply"];
    REQUIRE(graph["status"] == "schema_error");
    CHECK(graph["text"] == retained);
    CHECK(graph["canonical_store_written"] == false);
    CHECK(graph["first_response_ref"]["blob_hash"] == Sha256::hex(transport->wire.back()));
    REQUIRE(graph.contains("model_content_source_ref"));
    REQUIRE(graph["model_content_source_ref"].contains("blob_hash"));
    CHECK(unwrap(fixture.runtime->blobs().read(graph["model_content_source_ref"]["blob_hash"].get<std::string>())) == transport->contents.back());
  }
#else
  CHECK(true);
#endif
}

TEST_CASE("chat graph wiring: wrapper binding and separate graph request execute actual registry recipes") {
#if __has_include("packet/packet.h") && __has_include("loom/usage_policy.h")
  for (const char* mode : {"text_plus_JSONgraph", "separate_model_afterwards"}) {
    CAPTURE(std::string(mode));
    auto transport = std::make_shared<BoundChatGraphTransport>();
    transport->wrapped = std::string_view(mode) == "text_plus_JSONgraph";
    transport->separate = std::string_view(mode) == "separate_model_afterwards";
    ChatGraphFixture fixture(transport);
    auto options = prepared_graph_options(true);
    options["mode"] = mode;
    if (transport->wrapped) add_output_binding(options);
    else add_postprocess_provider(options);
    fixture.options(options);
    const auto result = fixture.send();
    REQUIRE(transport->requests.size() == (transport->separate ? 2 : 1));
    CHECK(result.text == (transport->wrapped ? "Human readable answer" : "Primary ordinary answer"));
    const auto assistant = unwrap(fixture.runtime->db().get_msg(result.assistant_message_id));
    REQUIRE(assistant);
    const auto& graph = assistant->metadata["graph_reply"];
    REQUIRE(graph["status"] == "candidate");
    CHECK(graph["manifest"]["trace"]["request_bytes_sha256"] == Sha256::hex(transport->requests.back().body));
    if (transport->separate) {
      CHECK(transport->requests.back().url == "https://chatgraph.test/graph-afterwards");
      CHECK(graph["primary_response_ref"]["blob_hash"] == Sha256::hex(transport->wire[0]));
      CHECK(graph["first_response_ref"]["blob_hash"] == Sha256::hex(transport->wire[1]));
    }
  }
#else
  CHECK(true);
#endif
}

TEST_CASE("chat graph wiring: explicit all-native automatic admission writes actual canonical receipts") {
#if __has_include("packet/packet.h") && __has_include("loom/usage_policy.h")
  auto transport = std::make_shared<BoundChatGraphTransport>();
  ChatGraphFixture fixture(transport);
  auto options = prepared_graph_options(true);
  options["admission"] = Json{{"mode", "automatic"}, {"selection_scope", "all_native_rows"},
      {"store_request", Json{{"operation", "accept"}, {"target", "synthetic-chat-target"}, {"explicitly_accepted", true}}}};
  fixture.options(options);
  for (int round = 0; round < 2; ++round) {
    const auto result = fixture.send();
    const auto assistant = unwrap(fixture.runtime->db().get_msg(result.assistant_message_id));
    REQUIRE(assistant);
    const auto& graph = assistant->metadata["graph_reply"];
    REQUIRE(graph["status"] == "accepted");
    CHECK(graph["canonical_store_written"] == true);
    const auto& receipt = graph["store_receipt"]["receipt"];
    kb::GraphPacketStore store(fixture.runtime->db());
    const auto stored = unwrap(store.execute(Json{{"operation", "read"}, {"receipt_id", receipt["id"]}}));
    CHECK(stored["receipt"] == receipt);
    const std::string run = receipt["run_id"].get<std::string>();
    kb::KnowledgeStore knowledge(fixture.runtime->db());
    for (const auto& id : graph["compilation"]["node_ids"]) {
      const auto entity = unwrap(knowledge.get_entity(run, id.get<std::string>()));
      REQUIRE(entity);
      CHECK(entity->origin == model::Origin::ModelKnowledge);
    }
  }
  CHECK(transport->requests.size() == 2);
#else
  CHECK(true);
#endif
}

TEST_CASE("chat graph wiring: buffered and SSE provider aliases remain distinct from requested model") {
#if __has_include("packet/packet.h") && __has_include("loom/usage_policy.h")
  for (bool stream : {false, true}) {
    CAPTURE(stream);
    auto transport = std::make_shared<BoundChatGraphTransport>();
    transport->reported_model = "synthetic/resolved-graph-version";
    ChatGraphFixture fixture(transport);
    auto options = prepared_graph_options(true);
    options["profile"] = graph_profile(stream);
    fixture.options(options);
    ChatOptions chat_options;
    chat_options.stream = stream;
    ChatCallbacks callbacks;
    if (stream) callbacks.on_chunk = [](std::string_view) {};
    const auto result = unwrap(fixture.runtime->chat().send("Synthetic alias request", chat_options, callbacks));
    REQUIRE(transport->requests.size() == 1);
    CHECK(result.text == "Aą🙂");
    const auto& graph = result.context_trace["graph_reply"];
    REQUIRE(graph["status"] == "candidate");
    CHECK(graph["model_origin"]["model"] == "synthetic/resolved-graph-version");
    CHECK(graph["manifest"]["trace"]["requested_model"] == "synthetic/graph");
    CHECK(graph["manifest"]["trace"]["actual_model"] == "synthetic/resolved-graph-version");
    CHECK(graph["manifest"]["trace"]["instrumentation"]["model_identity_basis"] == "provider_reported");
    CHECK(graph["manifest"]["trace"]["instrumentation"]["requested_model"] == "synthetic/graph");
  }
#else
  CHECK(true);
#endif
}

TEST_CASE("chat graph wiring: caller model binding changes actual request and effective method version") {
#if __has_include("packet/packet.h") && __has_include("loom/usage_policy.h")
  auto transport = std::make_shared<BoundChatGraphTransport>();
  ChatGraphFixture fixture(transport);
  auto options = prepared_graph_options(true);
  auto& recipe = options["profile"]["entities"][2]["attrs"];
  recipe["definition"].erase("model");
  recipe["definition"]["parameter_bindings"] = Json::array({Json{{"target", Json::array({"chosen_model"})},
      {"source", Json::array({"model"})}}});
  recipe["definition"]["request_bindings"].push_back(Json{{"target", Json::array({"model"})},
      {"source", Json::array({"effective_parameters", "chosen_model"})}});
  recipe["definition_sha256"] = Sha256::hex(json::canonical(recipe["definition"]));
  fixture.options(options);
  Json versions = Json::array();
  Json parameter_versions = Json::array();
  for (const char* model_name : {"synthetic/first-bound", "synthetic/second-bound"}) {
    ChatOptions chat_options;
    chat_options.stream = false;
    chat_options.model = model_name;
    const auto result = unwrap(fixture.runtime->chat().send("Same synthetic request", chat_options));
    const auto& graph = result.context_trace["graph_reply"];
    REQUIRE(graph["status"] == "candidate");
    CHECK(graph["manifest"]["trace"]["requested_model"] == model_name);
    CHECK(graph["manifest"]["trace"]["effective_parameters"]["chosen_model"] == model_name);
    CHECK(unwrap(json::parse(transport->requests.back().body))["model"] == model_name);
    versions.push_back(graph["manifest"]["bindings"]["method_version_id"]);
    parameter_versions.push_back(check_parameter_graph(graph));
  }
  CHECK(versions[0] != versions[1]);
  CHECK(parameter_versions[0] != parameter_versions[1]);
#else
  CHECK(true);
#endif
}
