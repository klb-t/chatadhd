// Source-private offline regressions. Real W4/W2 overlays are optional runner
// dependencies; absence is tested as an explicit unavailable capability.
#include <doctest/doctest.h>
#include <fstream>
#include <stdexcept>

#include "graph_reply.h"
#include "context/context_execution.h"
#include "context/context_usage_claim.h"
#include "loom/config.h"
#include "loom/graph_packet_store.h"
#include "loom/provenance.h"
#include "loom/providers.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

#if __has_include("packet/packet.h")
#define W3_VERIFY_GRAPH_PACKET 1
#else
#define W3_VERIFY_GRAPH_PACKET 0
#endif
#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#define W3_VERIFY_GRAPH_USAGE 1
#else
#define W3_VERIFY_GRAPH_USAGE 0
#endif

namespace {
using namespace loom;
using namespace loom::chat;
using loom::test::unwrap;

Json instrument_origin() {
  return Json{{"kind", "system"}, {"actor", "offline_graph_reply_fixture"}, {"model", nullptr},
              {"recipe_sha256", nullptr}, {"response_sha256", nullptr}};
}
struct GraphReplyFixture {
  fsutil::TempDir directory;
  std::shared_ptr<net::ScriptedTransport> http = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  explicit GraphReplyFixture(std::shared_ptr<net::HttpTransport> custom = {}) {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = custom ? std::move(custom) : http;
    runtime = unwrap(Runtime::open(options));
#if W3_VERIFY_GRAPH_USAGE
    runtime->config().set("loom_usage_policy", usage_policy_defaults());
#endif
    LOOM_REQUIRE_OK(runtime->providers().load(Json::array({Json{{"id", "graph_fixture"}, {"display_name", "Offline fixture"},
      {"base_url", "https://graph.fixture.test"}, {"auth_scheme", "none"}, {"auth_secret", ""},
      {"default_headers", {{"Fixture-Protocol", "1"}}}, {"capabilities", Json::array({
        Json{{"resource", "llm"}, {"name", "chat.completions"}, {"constraints", Json::object()}},
        Json{{"resource", "llm"}, {"name", "chat.stream"}, {"constraints", Json::object()}}})},
      {"metadata", {{"chat_completions_path", "/fixture-completions"}}}}})));
  }
  GraphReplyServices services() {
    return {runtime->paths().root, runtime->db(), runtime->config(), runtime->secrets(), runtime->http(),
            &runtime->providers(), runtime->blobs(), runtime->provenance()};
  }
  Json source(std::string_view bytes) {
    auto borrowed = services();
    return unwrap(retain_graph_reply_bytes(borrowed, bytes, Json{{"channel", "offline_wire"}}));
  }
  std::string bytes(const Json& reference) { return unwrap(runtime->blobs().read(reference["blob_hash"].get<std::string>())); }
};
Json recipe(Json output = nullptr) {
  Json definition{{"request", Json{{"model", "fixture/model"}, {"messages", Json::array({
    Json{{"role", "user"}, {"content", "Fixture prompt bytes supplied as data."}}})}}}};
  if (!output.is_null()) definition["output_binding"] = output;
  Json result = definition;
  result["definition"] = definition;
  result["definition_sha256"] = Sha256::hex(json::canonical(definition));
  return result;
}
GraphReplyCallbacks offline_capabilities() {
  GraphReplyCallbacks callbacks;
  callbacks.operation = [](const Json& command) -> Result<Json> {
    if (command["operation"] == "capabilities") return Json{{"schema", "fixture_capabilities"}};
    if (command["operation"] == "validate") return command["packet"];
    return Error(Errc::Unavailable, "offline_fixture_has_no_compiler");
  };
  return callbacks;
}
GraphReplyPrepared prepared(std::string mode, bool native = false, Json output = nullptr) {
  const auto data = recipe(std::move(output));
  Json base = Json::object();
  GraphReplyCallbacks callbacks = offline_capabilities();
#if W3_VERIFY_GRAPH_PACKET
  if (native) {
    callbacks.operation = {};
    base = unwrap(graph_reply_packet_operation(Json{{"operation", "make"}, {"origin", instrument_origin()}}));
  }
#else
  (void)native;
#endif
  Json options{{"mode", mode}, {"apply_policy", {{"schema", "loom.graph_packet_apply_policy/1"},
    {"acceptance", "preview"}, {"allow_source_tombstones", false}}}, {"admission", {{"mode", "candidate"}}}};
  auto value = unwrap(prepare_graph_reply(options, base, Json{{"request_id", "fixture_request"}, {"turn_id", "fixture_turn"},
      {"model", "fixture/model"}, {"recipe_sha256", data["definition_sha256"]}}, data,
      Json{{"schema", "loom.method_graph/1"}}, callbacks));
  value.binding_origin = instrument_origin();
  value.request_bytes = json::dump(data["request"]);
  value.callbacks.bind_results = [](const Json& packet, const Json& manifest, const Json& bindings) -> Result<Json> {
    Json trace = manifest;
    trace["fixture_actual_bindings"] = bindings;
    return Json{{"packet", packet}, {"manifest", trace}};
  };
  return value;
}
[[maybe_unused]] Json graph(const Json& packet) {
  return Json{{"schema", "loom.graph_reply/2"}, {"base_packet_sha256", packet["packet_id"]},
    {"response_id", "root"}, {"nodes", Json::array({
      Json{{"id", "root"}, {"text", nullptr}, {"role", "response"}, {"children", Json::array({"a", "b"})}},
      Json{{"id", "a"}, {"text", "Aą🙂"}, {"role", "premise"}, {"children", Json::array()}},
      Json{{"id", "b"}, {"text", "B"}, {"role", "conclusion"}, {"children", Json::array()}}})},
    {"links", Json::array({Json{{"from", "a"}, {"predicate", "supports"}, {"to", "b"}}})}};
}
[[maybe_unused]] Json transport_options(std::string operation) {
  return Json{{"provider_id", "graph_fixture"}, {"timeout_ms", 123456}, {"calls_authorized", true},
      {"usage_estimate", Json{{"operation_id", std::move(operation)}, {"baseline_key", "graph_reply_fixture/cohort"},
          {"resources", Json{{"output_tokens", nullptr}, {"response_bytes", nullptr}, {"cost_usd", nullptr}}}}}};
}
class ThrowingGraphTransport : public net::HttpTransport {
 public:
  std::string partial = "{PRIVATE_FIXTURE_PARTIAL";
  int calls = 0;
  Result<net::HttpResponse> send(const net::HttpRequest&, const net::StreamSink* sink, const CancelToken*) override {
    ++calls;
    if (sink && sink->on_headers) sink->on_headers(200, {});
    if (sink && sink->on_data) sink->on_data(partial);
    throw std::runtime_error("PRIVATE_FIXTURE_TRANSPORT_THROW");
  }
  std::string name() const override { return "throwing_graph_fixture"; }
};
}

TEST_CASE("graph reply: off leaves ordinary text byte-identical without dependencies") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  const auto existing_sources = unwrap(fixture.runtime->provenance().list_sources()).size();
  auto value = unwrap(prepare_graph_reply(Json{{"mode", "off"}}, nullptr, nullptr, nullptr, nullptr));
  const std::string text = "  Aą🙂\n\nNo graph recipe.  ";
  const auto result = finish_graph_reply(services, value, text, text, nullptr);
  CHECK(result["status"] == "off");
  CHECK(result["text"] == text);
  CHECK_FALSE(result.contains("error"));
  CHECK_FALSE(result["canonical_store_written"].get<bool>());
  CHECK(fixture.http->requests().empty());
  CHECK(unwrap(fixture.runtime->provenance().list_sources()).size() == existing_sources);
}

TEST_CASE("graph reply: canonical modes preserve explicit versioned recipe request") {
  for (const auto& [alias, canonical] : std::vector<std::pair<std::string, std::string>>{
      {"graph", "answer_as_graph"}, {"text+JSONgraph", "text_plus_JSONgraph"},
      {"separate-model-postprocess", "separate_model_afterwards"}}) {
    auto value = prepared(alias);
    CHECK(value.mode == canonical);
    CHECK(value.request_patch == value.effective_recipe["request"]);
    CHECK(value.append_messages.empty());
    CHECK(value.capability["available"] == true);
  }
  CHECK_FALSE(prepare_graph_reply(Json{{"mode", "invented_mode"}}, nullptr, nullptr, nullptr, nullptr));
}

TEST_CASE("graph reply: missing data is unavailable and captured bytes survive dependency failure") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  auto value = unwrap(prepare_graph_reply(Json{{"mode", "answer_as_graph"}}, nullptr, nullptr, nullptr, nullptr));
  CHECK(value.capability["reason"] == "effective_recipe_request_unavailable");
  auto source = fixture.source("RAW first bytes");
  const auto result = finish_graph_reply(services, value, "unstructured model answer", "ordinary text", source);
  CHECK(result["status"] == "unavailable");
  CHECK(result["text"] == "ordinary text");
  CHECK(result["first_response_ref"] == source);
  CHECK(fixture.bytes(result["model_content_source_ref"]) == "unstructured model answer");
  CHECK(fixture.http->requests().empty());
#if !W3_VERIFY_GRAPH_PACKET
  CHECK_FALSE(graph_reply_packet_operation(Json{{"operation", "capabilities"}}));
#endif
}

TEST_CASE("graph reply: strict recipe identity rejects invented matching host hash") {
  auto data = recipe();
  data["definition"]["request"]["messages"][0]["content"] = "changed bytes";
  auto result = prepare_graph_reply(Json{{"mode", "answer_as_graph"}}, Json::object(),
      Json{{"request_id", "r"}, {"turn_id", "t"}, {"model", "fixture/model"}, {"recipe_sha256", data["definition_sha256"]}},
      data, Json{{"schema", "loom.method_graph/1"}}, offline_capabilities());
  CHECK_FALSE(result);
  CHECK(result.error().code == Errc::InvalidArgument);
}

TEST_CASE("graph reply: binary first response persists exactly with safe public references") {
  GraphReplyFixture fixture;
  const std::string bytes("\xff\0A\r\n", 5);
  auto services = fixture.services();
  auto reference = unwrap(retain_graph_reply_bytes(services, bytes, Json{{"private_fixture_marker", "PRIVATE_FIXTURE"}}));
  CHECK(fixture.bytes(reference) == bytes);
  CHECK(reference["blob_hash"] == Sha256::hex(bytes));
  CHECK(reference["bytes"] == bytes.size());
  CHECK(reference.dump().find("PRIVATE_FIXTURE") == std::string::npos);
}

TEST_CASE("graph reply: separate model requires explicit calls and execution scope") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  auto value = prepared("separate_model_afterwards");
  auto options = transport_options("not_authorized");
  auto result = run_graph_reply_postprocess(services, value, value.request_patch, options, "primary answer", nullptr);
  CHECK(result["error"]["code"] == "postprocess_calls_not_authorized");
  {
    context::ContextExecutionScope scope(Json::object());
    options["calls_authorized"] = false;
    result = run_graph_reply_postprocess(services, value, value.request_patch, options, "primary answer", nullptr);
    CHECK(result["error"]["code"] == "postprocess_calls_not_authorized");
  }
  CHECK(result["retained_text"] == "primary answer");
  CHECK(fixture.http->requests().empty());
}

#if W3_VERIFY_GRAPH_PACKET
TEST_CASE("graph reply: real W4 compiler exposes UTF8 fragment and model origin unchanged") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  auto value = prepared("answer_as_graph", true);
  auto content = json::dump(graph(value.base_packet));
  auto source = fixture.source(content);
  auto result = finish_graph_reply(services, value, content, "fallback answer", source);
  REQUIRE(result["status"] == "candidate");
  CHECK(result["text"] == "Aą🙂B");
  CHECK(result["retained_text"] == "fallback answer");
  CHECK(result["model_origin"]["kind"] == "model");
  CHECK(result["projection_receipt"]["accepted"] == false);
  CHECK(result["projection_receipt"]["before_packet"] == value.base_packet);
  CHECK(result["projection_receipt"]["after_packet_sha256"] == value.base_packet["packet_id"]);
  CHECK_FALSE(result["acceptance_establishes_content_truth"].get<bool>());
  CHECK(fixture.bytes(result["candidate_source_ref"]) == json::canonical(result["candidate_packet"]));
  auto fragment = unwrap(graph_reply_fragment(result, Json{{"local_id", "a"}}));
  CHECK(fragment["text"] == "Aą🙂");
  CHECK(fragment["span"]["char_len"] == 3);
  CHECK(fragment["span"]["byte_len"] == 7);
  auto modified = result;
  modified["compilation"]["response_text"] = "edited";
  CHECK_FALSE(graph_reply_fragment(modified, Json{{"local_id", "a"}}));
  for (const auto& entity : result["candidate_packet"]["entities"])
    if (entity["kind"] == "graph_reply_node") {
      CHECK(entity["origin"] == "model_knowledge");
      CHECK(entity["attrs"]["model_origin"]["kind"] == "model");
      CHECK(entity["attrs"]["content_verification"] == "unverified");
    }
  CHECK(fixture.http->requests().empty());
}

TEST_CASE("graph reply: malformed graph and duplicate wrapper keys retain first content without repair") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  auto value = prepared("text_plus_JSONgraph", true, Json{{"graph_pointer", "/graph"}, {"text_pointer", "/text"}});
  const auto reply = graph(value.base_packet);
  const std::string content = "{\"text\":\"first\",\"text\":\"later\",\"graph\":" + json::dump(reply) + "}";
  auto source = fixture.source(content);
  auto result = finish_graph_reply(services, value, content, "ordinary retained", source);
  CHECK(result["status"] == "schema_error");
  CHECK(result["text"] == "ordinary retained");
  CHECK_FALSE(result.contains("compilation"));
  CHECK(fixture.bytes(result["model_content_source_ref"]) == content);
  value = prepared("answer_as_graph", true);
  auto invalid = graph(value.base_packet);
  invalid["nodes"][1]["text"] = nullptr;
  const auto invalid_bytes = json::dump(invalid);
  result = finish_graph_reply(services, value, invalid_bytes, "ordinary retained", fixture.source(invalid_bytes));
  CHECK(result["status"] == "schema_error");
  CHECK(result["text"] == "ordinary retained");
  CHECK(fixture.bytes(result["graph_projection_source_ref"]) == invalid_bytes);
  CHECK(fixture.http->requests().empty());
}

TEST_CASE("graph reply: wrapper projection is explicit while original bytes and display remain distinct") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  auto value = prepared("text_plus_JSONgraph", true, Json{{"graph_pointer", "/graph"}, {"text_pointer", "/text"}});
  const auto content = json::dump(Json{{"text", "ordinary answer"}, {"graph", graph(value.base_packet)}});
  const auto result = finish_graph_reply(services, value, content, content, fixture.source(content));
  REQUIRE(result["status"] == "candidate");
  CHECK(result["text"] == "ordinary answer");
  CHECK(result["retained_text"] == content);
  CHECK(result["graph_projection_transform"]["operation"] == "json_pointer_canonical_projection");
  CHECK(fixture.bytes(result["model_content_source_ref"]) == content);
  CHECK(result["manifest"]["fixture_actual_bindings"]["request_bytes"] == value.request_bytes);
  CHECK(result["manifest"]["fixture_actual_bindings"]["origin"] == instrument_origin());
}

TEST_CASE("graph reply: valid first text field survives malformed graph without repairing the response") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  auto value = prepared("text_plus_JSONgraph", true, Json{{"graph_pointer", "/graph"}, {"text_pointer", "/text"}});
  auto invalid = graph(value.base_packet);
  invalid["nodes"][1]["text"] = nullptr;
  const auto content = json::dump(Json{{"text", "First ordinary answer Aą🙂"}, {"graph", invalid}});
  const auto result = finish_graph_reply(services, value, content, content, fixture.source(content));
  CHECK(result["status"] == "schema_error");
  CHECK(result["text"] == "First ordinary answer Aą🙂");
  CHECK(result["display_text_source"] == "first_response_output_binding");
  CHECK(result["retained_text"] == content);
  CHECK(fixture.bytes(result["model_content_source_ref"]) == content);
  CHECK(fixture.bytes(result["graph_projection_source_ref"]) == json::canonical(invalid));
  CHECK_FALSE(result["canonical_store_written"].get<bool>());
  CHECK(fixture.http->requests().empty());
  const auto missing_graph = json::dump(Json{{"text", "First text survives absent graph field"}});
  const auto missing = finish_graph_reply(services, value, missing_graph, missing_graph, fixture.source(missing_graph));
  CHECK(missing["status"] == "schema_error");
  CHECK(missing["text"] == "First text survives absent graph field");
  CHECK(missing["retained_text"] == missing_graph);
}

TEST_CASE("graph reply: failed or dishonest result binding preserves full candidate without promotion") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  auto value = prepared("answer_as_graph", true);
  const auto content = json::dump(graph(value.base_packet));
  value.callbacks.bind_results = {};
  auto result = finish_graph_reply(services, value, content, "retained", fixture.source(content));
  CHECK(result["error"]["code"] == "method_result_binding_unavailable");
  CHECK(fixture.bytes(result["candidate_source_ref"]) == json::canonical(result["candidate_packet"]));
  value.callbacks.bind_results = [](const Json& packet, const Json& manifest, const Json&) -> Result<Json> {
    auto promoted = packet;
    for (auto& entity : promoted["entities"]) if (entity["kind"] == "graph_reply_node") entity["origin"] = "recorded";
    return Json{{"packet", promoted}, {"manifest", manifest}};
  };
  result = finish_graph_reply(services, value, content, "retained", fixture.source(content));
  CHECK(result["error"]["code"] == "method_binding_changed_model_content");
  CHECK_FALSE(result["canonical_store_written"].get<bool>());
  CHECK(result["text"] == "retained");
}

TEST_CASE("graph reply: invalid canonical-store callback receipt never claims acceptance") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  auto value = prepared("answer_as_graph", true);
  value.options["admission"] = Json{{"mode", "automatic"}, {"store_request", Json::object()}};
  int writes = 0;
  value.callbacks.accept = [&](const Json&) -> Result<Json> { ++writes; return Json{{"error", {{"code", "conflict"}}}}; };
  const auto content = json::dump(graph(value.base_packet));
  auto result = finish_graph_reply(services, value, content, "retained", fixture.source(content));
  CHECK(result["status"] == "admission_error");
  CHECK(result["error"]["code"] == "canonical_graph_store_receipt_invalid");
  CHECK_FALSE(result["canonical_store_written"].get<bool>());
  CHECK_FALSE(result["acceptance_establishes_content_truth"].get<bool>());
  CHECK(writes == 1);
}

TEST_CASE("graph reply: explicit automatic admission uses native store without changing model content truth") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  auto value = prepared("answer_as_graph", true);
  const auto content = json::dump(graph(value.base_packet));
  const auto source = fixture.source(content);
  const auto preview = finish_graph_reply(services, value, content, "retained", source);
  REQUIRE(preview["status"] == "candidate");
  Json selection = Json::object(), expected = Json::object();
  for (const char* collection : {"entities", "claims", "sources"}) {
    selection[collection] = Json::array(); expected[collection] = Json::object();
    for (const auto& row : preview["candidate_packet"][collection]) {
      const auto id = std::string_view(collection) == "sources" ? row["observation"]["id"] : row["id"];
      selection[collection].push_back(id);
      expected[collection][id.get<std::string>()] = nullptr;
    }
  }
  value.options["admission"] = Json{{"mode", "automatic"}, {"store_request", Json{
      {"operation", "accept"}, {"target", "offline_graph_target"}, {"selection", selection},
      {"expected_rows", expected}, {"explicitly_accepted", true}}}};
  kb::GraphPacketStore store(fixture.runtime->db());
  value.callbacks.accept = [&](const Json& request) { return store.execute(request); };
  const auto accepted = finish_graph_reply(services, value, content, "retained", source);
  REQUIRE(accepted["status"] == "accepted");
  CHECK(accepted["canonical_store_written"] == true);
  CHECK(accepted["acceptance_establishes_content_truth"] == false);
  CHECK(accepted["candidate_packet"] == preview["candidate_packet"]);
  const auto replay = unwrap(store.execute(Json{{"operation", "replay"}, {"receipt_id", accepted["store_receipt"]["receipt"]["id"]}}));
  CHECK(replay["row_drift"]["matches"] == true);
  for (const auto& entity : replay["receipt"]["packet"]["entities"])
    if (entity["kind"] == "graph_reply_node") {
      CHECK(entity["origin"] == "model_knowledge");
      CHECK(entity["attrs"]["content_verification"] == "unverified");
    }
}
#endif

#if W3_VERIFY_GRAPH_USAGE && LOOM_CONTEXT_CLAIM_HAS_DURABLE_DIRECTORIES
TEST_CASE("graph reply usage: exact immutable HTTP identity cannot dispatch an admitted operation twice") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  context::ContextExecutionScope scope(Json::object());
  net::HttpRequest request;
  request.method = "POST"; request.url = "https://graph.fixture.test/fixture-completions";
  request.body = "{\"messages\":[]}";
  request.headers = {{"Duplicate", "first"}, {"Duplicate", "second"}};
  Json estimate{{"operation_id", "exact_guard"}, {"baseline_key", "exact_guard_cohort"}, {"resources", Json::object()}};
  ModelUsageGuard first(services);
  CHECK(unwrap(first.admit(request, estimate))["dispatch_authorized"] == true);
  CHECK(unwrap(first.settle(true, 7))["actual"]["cost_usd"]["value"].is_null());
  ModelUsageGuard repeated(services);
  const auto retry = unwrap(repeated.admit(request, estimate));
  CHECK(retry["dispatch_status"] == "operation_already_attempted");
  CHECK_FALSE(retry.value("dispatch_authorized", false));
  std::swap(request.headers[0], request.headers[1]);
  ModelUsageGuard different(services);
  auto changed = different.admit(request, estimate);
  CHECK_FALSE(changed);
  CHECK(changed.error().code == Errc::Conflict);
}

TEST_CASE("graph reply usage: explicit expected tenfold growth pauses before transport and exact confirmation resumes") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  context::ContextExecutionScope scope(Json::object());
  net::HttpRequest request; request.body = "same payload";
  auto options = transport_options("baseline_graph");
  options["usage_estimate"]["resources"]["cost_usd"] = 1;
  ModelUsageGuard baseline(services);
  CHECK(unwrap(baseline.admit(request, options["usage_estimate"]))["dispatch_authorized"] == true);
  unwrap(baseline.settle(true, 1, Json{{"cost", 1}}));
  auto estimate = options["usage_estimate"];
  estimate["operation_id"] = "larger_graph";
  estimate["resources"]["cost_usd"] = 10;
  ModelUsageGuard growth(services);
  const auto paused = unwrap(growth.admit(request, estimate));
  CHECK(paused["status"] == "requires_confirmation");
  CHECK_FALSE(paused.value("dispatch_authorized", false));
  CHECK(fixture.http->requests().empty());
  ModelUsageGuard confirmed(services);
  const auto resumed = unwrap(confirmed.admit(request, estimate,
      Json{{"approved", true}, {"receipt_id", paused["receipt_id"]}, {"ref", "offline_owner_confirmation"}}));
  CHECK(resumed["dispatch_authorized"] == true);
  unwrap(confirmed.settle(false, 0));
}

TEST_CASE("graph reply postprocess: malformed and HTTP first responses persist exactly without retry") {
  for (const int status : {200, 503}) {
    GraphReplyFixture fixture;
    auto services = fixture.services();
    auto value = prepared("separate_model_afterwards");
    const std::string first = "{PRIVATE_FIXTURE_BAD_JSON";
    fixture.http->expect("POST", "https://graph.fixture.test/fixture-completions", net::ScriptedTransport::Reply::text(status, first));
    context::ContextExecutionScope scope(Json::object());
    const auto result = run_graph_reply_postprocess(services, value, value.request_patch,
        transport_options("malformed_" + std::to_string(status)), "primary survives", nullptr);
    CHECK(result["text"] == "primary survives");
    CHECK(result["retained_text"] == "primary survives");
    CHECK(result["status"] == (status == 200 ? "schema_error" : "postprocess_error"));
    CHECK(fixture.bytes(result["postprocess_first_response_ref"]) == first);
    CHECK(result["first_response_ref"] == result["postprocess_first_response_ref"]);
    CHECK(result["primary_response_ref"].is_null());
    REQUIRE(fixture.http->requests().size() == 1);
    CHECK(fixture.http->requests()[0].timeout_ms == 123456);
    CHECK(net::header_value(fixture.http->requests()[0].headers, "Fixture-Protocol") == "1");
    CHECK(result.dump().find("PRIVATE_FIXTURE") == std::string::npos);
    CHECK(result["execution_usage"].back()["actual"]["requests"]["value"] == 1);
    CHECK(result["execution_usage"].back()["actual"]["cost_usd"]["value"].is_null());
  }
}

TEST_CASE("graph reply postprocess: thrown partial response is retained with unknown spend and no second send") {
  auto http = std::make_shared<ThrowingGraphTransport>();
  GraphReplyFixture fixture(http);
  auto services = fixture.services();
  auto value = prepared("separate_model_afterwards");
  context::ContextExecutionScope scope(Json::object());
  const auto options = transport_options("partial_graph");
  const auto first = run_graph_reply_postprocess(services, value, value.request_patch, options, "primary", nullptr);
  CHECK(first["status"] == "postprocess_error");
  CHECK(fixture.bytes(first["postprocess_first_response_ref"]) == http->partial);
  CHECK(first["execution_usage"].back()["actual"]["response_bytes"]["value"] == http->partial.size());
  CHECK(first["execution_usage"].back()["actual"]["cost_usd"]["value"].is_null());
  const auto repeated = run_graph_reply_postprocess(services, value, value.request_patch, options, "primary", nullptr);
  CHECK(repeated["status"] == "usage_held");
  CHECK(http->calls == 1);
  CHECK(first.dump().find("PRIVATE_FIXTURE") == std::string::npos);
}

TEST_CASE("graph reply postprocess: real storage failure blocks parsing graph admission and preserves unknown cost") {
  GraphReplyFixture fixture;
  auto services = fixture.services();
  const auto blocked = fixture.directory.path() / "not_a_blob_directory";
  { std::ofstream file(blocked); file << "blocking file"; }
  BlobStore failing_blobs(blocked, fixture.runtime->db());
  GraphReplyServices failing_services{services.root, services.db, services.config, services.secrets, services.http,
      services.providers, failing_blobs, services.provenance};
  auto value = prepared("separate_model_afterwards");
  fixture.http->expect("POST", "https://graph.fixture.test/fixture-completions", net::ScriptedTransport::Reply::json(200,
      Json{{"choices", Json::array({Json{{"message", {{"content", "not parsed"}}}}})}, {"usage", {{"cost", 5}}}}));
  context::ContextExecutionScope scope(Json::object());
  const auto result = run_graph_reply_postprocess(failing_services, value, value.request_patch, transport_options("storage_failure"), "primary", nullptr);
  CHECK(result["status"] == "storage_error");
  CHECK(result["text"] == "primary");
  CHECK(result["accounting_status"] == "measured_with_unknown_provider_usage");
  CHECK(result["execution_usage"].back()["actual"]["cost_usd"]["value"].is_null());
  CHECK_FALSE(result.contains("compilation"));
  CHECK(fixture.http->requests().size() == 1);
}

#if W3_VERIFY_GRAPH_PACKET
TEST_CASE("graph reply postprocess: buffered and SSE success retain wire bytes and actual reported model separately") {
  for (const bool stream : {false, true}) {
    GraphReplyFixture fixture;
    auto services = fixture.services();
    auto value = prepared("separate_model_afterwards", true);
    value.request_patch["stream"] = stream;
    const auto content = json::dump(graph(value.base_packet));
    Json reply{{"model", "fixture/resolved_model"}, {"choices", Json::array({Json{{"message", {{"content", content}}}}})},
        {"usage", {{"completion_tokens", 21}, {"cost", 0.1}}}};
    std::string wire = json::dump(reply);
    if (stream) {
      const Json a{{"model", "fixture/resolved_model"}, {"choices", Json::array({Json{{"delta", {{"content", content.substr(0, 13)}}}}})}};
      const Json b{{"model", "fixture/resolved_model"}, {"choices", Json::array({Json{{"delta", {{"content", content.substr(13)}}}}})}};
      const Json usage{{"choices", Json::array()}, {"usage", reply["usage"]}};
      wire = "data: " + json::dump(a) + "\r\n\r\ndata: " + json::dump(b) + "\n\ndata: " + json::dump(usage) + "\n\ndata: [DONE]\n\n";
    }
    fixture.http->expect("POST", "https://graph.fixture.test/fixture-completions", net::ScriptedTransport::Reply::text(200, wire));
    context::ContextExecutionScope scope(Json::object());
    const auto primary = fixture.source("primary original wire");
    const auto result = run_graph_reply_postprocess(services, value, value.request_patch,
        transport_options(stream ? "stream_success" : "buffered_success"), "primary answer remains", primary);
    REQUIRE(result["status"] == "candidate");
    CHECK(result["text"] == "primary answer remains");
    CHECK(result["primary_response_ref"] == primary);
    CHECK(fixture.bytes(result["postprocess_first_response_ref"]) == wire);
    CHECK(result["model_origin"]["kind"] == "model");
    CHECK(result["model_origin"]["model"] == "fixture/resolved_model");
    CHECK(result["manifest"]["fixture_actual_bindings"]["instrumentation"]["requested_model"] == "fixture/model");
    CHECK(result["manifest"]["fixture_actual_bindings"]["instrumentation"]["reported_model"] == "fixture/resolved_model");
    CHECK(result["manifest"]["fixture_actual_bindings"]["request_bytes"] == fixture.http->requests()[0].body);
    CHECK(result["execution_usage"].back()["actual"]["output_tokens"]["value"] == 21);
    CHECK(result["execution_usage"].back()["actual"]["cost_usd"]["value"] == 0.1);
    REQUIRE(fixture.http->requests().size() == 1);
    CHECK(fixture.http->requests()[0].stream == stream);
  }
}
#endif
#endif
