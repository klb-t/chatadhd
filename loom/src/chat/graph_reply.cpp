#include "graph_reply.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

#include "loom/config.h"
#include "loom/providers.h"
#include "loom/provenance.h"
#include "loom/net/sse.h"
#include "loom/util/base64.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "context/context_execution.h"
#include "context/context_usage_claim.h"

#if __has_include("packet/packet.h")
#include "packet/packet.h"
#define LOOM_CHAT_GRAPH_PACKET_AVAILABLE 1
#else
#define LOOM_CHAT_GRAPH_PACKET_AVAILABLE 0
#endif
#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#define LOOM_CHAT_MODEL_USAGE_AVAILABLE 1
#else
#define LOOM_CHAT_MODEL_USAGE_AVAILABLE 0
#endif

namespace loom::chat {
namespace {

Json failure(std::string_view status, std::string_view text, const Json& source,
             std::string_view code = "unavailable") {
  return Json{{"schema", "loom.chat_graph_reply/1"}, {"status", status}, {"text", text},
      {"retained_text", text}, {"first_response_ref", source}, {"model_content_origin", "model"},
      {"acceptance_establishes_content_truth", false}, {"canonical_store_written", false},
      {"error", Json{{"code", code}}}};
}

Result<Json> invoke(const GraphReplyCallbacks& callbacks, const Json& command) {
  auto result = callbacks.operation ? callbacks.operation(command) : graph_reply_packet_operation(command);
  if (!result) return result.error();
  if (const auto* error = json::find(*result, "error"))
    return Error(json::get_string(*error, "code") == "unavailable" ? Errc::Unavailable : Errc::InvalidArgument,
                 json::get_string(*error, "message", "graph_packet_operation_failed"));
  return result;
}

const Json& definition(const Json& recipe) {
  const auto* value = json::find(recipe, "definition");
  return value && value->is_object() ? *value : recipe;
}

Result<Json> strict_object(std::string_view raw) {
  if (!utf8::is_valid(raw) || raw.starts_with("\xef\xbb\xbf"))
    return Error(Errc::Parse, "reply_encoding_invalid");
  bool duplicate = false;
  std::vector<std::set<std::string>> keys;
  try {
    auto value = Json::parse(raw.begin(), raw.end(), [&](int, Json::parse_event_t event, Json& parsed) {
      if (event == Json::parse_event_t::object_start) keys.emplace_back();
      else if (event == Json::parse_event_t::object_end) keys.pop_back();
      else if (event == Json::parse_event_t::key && !keys.back().insert(parsed.get<std::string>()).second)
        duplicate = true;
      return true;
    });
    if (duplicate || !value.is_object()) return Error(Errc::Parse, "reply_object_or_unique_keys_required");
    return value;
  } catch (...) { return Error(Errc::Parse, "reply_json_invalid"); }
}

[[maybe_unused]] Json quantity(const Json& usage, std::initializer_list<const char*> keys) {
  for (auto key : keys) {
    const auto* value = json::find(usage, key);
    if (value && value->is_number() && std::isfinite(value->get<double>()) && value->get<double>() >= 0)
      return *value;
  }
  return nullptr;
}

[[maybe_unused]] void record_decision(Json& decisions, const Json& decision) {
  decisions.push_back(decision);
  if (auto* scope = context::current_context_execution_scope()) scope->record_usage_decision(decision);
}

Result<Json> parse_postprocess_response(std::string_view bytes, bool stream) {
  if (!stream) return strict_object(bytes);
  Json response{{"choices", Json::array({Json{{"message", {{"content", ""}}}}})}};
  std::string content, model;
  bool invalid = false;
  net::SseParser parser;
  auto event = [&](const net::SseEvent& value) {
    if (value.is_done()) return;
    auto parsed = strict_object(value.data);
    if (!parsed) { invalid = true; return; }
    if (const auto* reported = json::find(*parsed, "model")) {
      if (!reported->is_string()) { invalid = true; return; }
      const auto name = reported->get<std::string>();
      if (!model.empty() && model != name) { invalid = true; return; }
      model = name;
    }
    if (const auto* usage = json::find(*parsed, "usage"); usage && usage->is_object()) response["usage"] = *usage;
    const auto* choices = json::find(*parsed, "choices");
    if (!choices || !choices->is_array()) { invalid = true; return; }
    if (choices->empty()) return;  // A usage-only final event is supported.
    const auto& first = (*choices)[0];
    if (!first.is_object() || !first.contains("delta") || !first["delta"].is_object()) { invalid = true; return; }
    if (const auto* chunk = json::find(first["delta"], "content"); chunk && !chunk->is_null()) {
      if (!chunk->is_string()) { invalid = true; return; }
      content += chunk->get<std::string>();
    }
  };
  parser.feed(bytes, event);
  parser.finish(event);
  if (invalid) return Error(Errc::Parse, "postprocess_sse_invalid");
  response["choices"][0]["message"]["content"] = content;
  if (!model.empty()) response["model"] = model;
  return response;
}

}  // namespace

Result<Json> graph_reply_packet_operation(const Json& command) {
#if LOOM_CHAT_GRAPH_PACKET_AVAILABLE
  auto result = packet::execute(command);
  if (!result) return result.error();
  if (const auto* error = json::find(*result, "error"))
    return Error(errc_from_name(json::get_string(*error, "code")).value_or(Errc::InvalidArgument),
        json::get_string(*error, "message", "graph_packet_operation_failed"));
  return result;
#else
  (void)command;
  return Error(Errc::Unavailable, "native_graph_packet_dependency_unavailable");
#endif
}

Result<GraphReplyPrepared> prepare_graph_reply(const Json& options, const Json& base_packet,
    const Json& host, const Json& effective_recipe, const Json& manifest, GraphReplyCallbacks callbacks) {
  if (!options.is_object()) return Error(Errc::InvalidArgument, "graph reply options must be an object");
  GraphReplyPrepared result;
  result.options = options;
  result.mode = json::get_string(options, "mode", "off");
  if (options.contains("mode") && !options["mode"].is_string())
    return Error(Errc::InvalidArgument, "graph reply mode must be text");
  if (result.mode == "graph") result.mode = "answer_as_graph";
  if (result.mode == "text+JSONgraph") result.mode = "text_plus_JSONgraph";
  if (result.mode == "separate-model-postprocess") result.mode = "separate_model_afterwards";
  if (result.mode != "off" && result.mode != "answer_as_graph" && result.mode != "text_plus_JSONgraph" &&
      result.mode != "separate_model_afterwards")
    return Error(Errc::Unsupported, "graph reply mode unavailable");
  result.base_packet = base_packet;
  result.host = host;
  result.effective_recipe = effective_recipe;
  result.manifest = manifest;
  result.callbacks = std::move(callbacks);
  result.request_patch = Json::object();
  result.append_messages = Json::array();
  result.capability = Json{{"available", false}, {"mode", result.mode}, {"status", "off"}};
  if (result.mode == "off") return result;
  result.capability["status"] = "unavailable";
  if (!effective_recipe.is_object() || !json::find(effective_recipe, "request") ||
      !effective_recipe["request"].is_object()) {
    result.capability["reason"] = "effective_recipe_request_unavailable";
    return result;
  }
  if (!manifest.is_object() || json::get_string(manifest, "schema") != "loom.method_graph/1") {
    result.capability["reason"] = "method_manifest_unavailable";
    return result;
  }
  auto capabilities = invoke(result.callbacks, Json{{"operation", "capabilities"}});
  if (!capabilities) { result.capability["reason"] = "native_graph_packet_unavailable"; return result; }
  auto checked = invoke(result.callbacks, Json{{"operation", "validate"}, {"packet", base_packet},
      {"resource_limits", options.value("resource_limits", Json::object())}});
  if (!checked) return checked.error();
  if (!host.is_object() || json::get_string(host, "request_id").empty() ||
      json::get_string(host, "turn_id").empty() || json::get_string(host, "model").empty())
    return Error(Errc::InvalidArgument, "graph reply host identity required");
  const auto recipe_hash = json::get_string(effective_recipe, "definition_sha256");
  if (recipe_hash.empty() || recipe_hash != Sha256::hex(json::canonical(definition(effective_recipe))) ||
      json::get_string(host, "recipe_sha256") != recipe_hash)
    return Error(Errc::InvalidArgument, "effective graph recipe hash mismatch");
  result.request_patch = effective_recipe["request"];
  if (const auto* messages = json::find(effective_recipe, "append_messages")) {
    if (!messages->is_array()) return Error(Errc::InvalidArgument, "recipe append_messages must be an array");
    result.append_messages = *messages;
  }
  result.capability["available"] = true;
  result.capability["status"] = "available";
  result.capability["packet"] = *capabilities;
  return result;
}

Result<Json> retain_graph_reply_bytes(GraphReplyServices& services, std::string_view bytes,
                                     const Json& metadata) {
  LOOM_TRY_ASSIGN(auto blob, services.blobs.put(bytes, "application/octet-stream"));
  SourceRecord source;
  source.kind = "api";
  source.blob_hash = blob.hash;
  source.size = blob.size;
  source.mime = "application/octet-stream";
  source.title = "Graph reply captured bytes";
  source.parser = "loom.chat.graph_reply";
  source.parser_version = "1";
  source.metadata = metadata;
  LOOM_TRY_ASSIGN(auto id, services.provenance.add_source(std::move(source)));
  return Json{{"schema", "loom.chat_response_source/1"}, {"source_status", "recorded"},
      {"source_id", id}, {"blob_hash", blob.hash}, {"bytes", blob.size}};
}

Json finish_graph_reply(GraphReplyServices& services, const GraphReplyPrepared& prepared,
    std::string_view model_content, std::string_view retained_display_text,
    const Json& first_response_ref, const Json& instrumentation) {
  auto result = failure("unavailable", retained_display_text, first_response_ref);
  result["mode"] = prepared.mode;
  result["capability"] = prepared.capability;
  result["manifest"] = prepared.manifest;
  if (prepared.mode == "off") { result["status"] = "off"; result.erase("error"); return result; }
  try {
    auto source = retain_graph_reply_bytes(services, model_content, Json{{"channel", "decoded_model_content"},
        {"model_origin", Json{{"kind", "model"}, {"model", json::get_string(prepared.host, "model")}}},
        {"model_identity_basis", json::get_string(instrumentation, "model_identity_basis", "host_declared_unverified")},
        {"derived_from", first_response_ref}, {"instrumentation", instrumentation}});
    if (!source) { result["status"] = "storage_error"; result["error"]["code"] = std::string(errc_name(source.error().code)); return result; }
    result["model_content_source_ref"] = *source;
    if (!prepared.capability.value("available", false)) return result;
    std::string graph(model_content), display(retained_display_text);
    Json transform{{"operation", "identity_model_content"}};
    const auto* binding = json::find(definition(prepared.effective_recipe), "output_binding");
    if (prepared.mode == "text_plus_JSONgraph" && (!binding || !binding->is_object())) {
      result["error"]["code"] = "graph_output_binding_unavailable";
      return result;
    }
    if (binding) {
      if (!binding->is_object() || !(*binding).contains("graph_pointer") || !(*binding)["graph_pointer"].is_string()) {
        result["status"] = "schema_error"; result["error"]["code"] = "graph_output_binding_invalid"; return result;
      }
      auto parsed = strict_object(model_content);
      if (!parsed) { result["status"] = "schema_error"; result["error"]["code"] = parsed.error().message; return result; }
      if (const auto* text_pointer = json::find(*binding, "text_pointer")) {
        if (!text_pointer->is_string()) throw std::invalid_argument("text pointer invalid");
        const auto& text = parsed->at(Json::json_pointer(text_pointer->get<std::string>()));
        if (!text.is_string()) throw std::invalid_argument("text binding must be text");
        display = text.get<std::string>();
        // The configured ordinary-text field is a projection of this same
        // first response, usable even when its graph fails schema validation.
        result["text"] = display;
        result["display_text_source"] = "first_response_output_binding";
        result["display_text_binding"] = Json{{"json_pointer", *text_pointer},
            {"operation", "json_pointer_string_decode"}, {"source_ref", *source}};
      }
      const auto pointer = (*binding)["graph_pointer"].get<std::string>();
      const auto& value = parsed->at(Json::json_pointer(pointer));
      graph = value.is_string() ? value.get<std::string>() : json::canonical(value);
      transform = Json{{"operation", value.is_string() ? "json_pointer_string_decode" : "json_pointer_canonical_projection"},
          {"json_pointer", pointer}, {"source_sha256", Sha256::hex(model_content)}};
    }
    auto projected_source = retain_graph_reply_bytes(services, graph, Json{{"channel", "graph_compiler_input"},
        {"model_content_origin", "model"}, {"derived_from", *source}, {"transform", transform}});
    if (!projected_source) { result["status"] = "storage_error"; result["error"]["code"] = std::string(errc_name(projected_source.error().code)); return result; }
    result["graph_projection_source_ref"] = *projected_source;
    const auto limits = prepared.options.value("resource_limits", Json::object());
    auto compilation = invoke(prepared.callbacks, Json{{"operation", "compile_reply"}, {"packet", prepared.base_packet},
        {"raw_base64", base64::encode(graph)}, {"host", prepared.host}, {"resource_limits", limits}});
    if (!compilation) {
      result["status"] = "schema_error"; result["error"]["code"] = compilation.error().message;
      result["graph_projection_transform"] = transform;
      return result;
    }
    result["compilation"] = *compilation;
    result["base_packet"] = prepared.base_packet;
    result["graph_projection_transform"] = transform;
    // Applying a projection requires explicit policy data. Missing data is
    // unavailable; no preview/auto dictionary is invented in this adapter.
    const auto* policy = json::find(prepared.options, "apply_policy");
    if (!policy || !policy->is_object()) { result["error"]["code"] = "graph_apply_policy_unavailable"; return result; }
    auto applied = invoke(prepared.callbacks, Json{{"operation", "apply_compiled_reply"}, {"packet", prepared.base_packet},
        {"compilation", *compilation}, {"policy", *policy},
        {"explicitly_accepted", prepared.options.value("explicitly_accepted", Json(false))}, {"resource_limits", limits}});
    if (!applied) { result["status"] = "schema_error"; result["error"]["code"] = applied.error().message; return result; }
    Json candidate = (*applied)["packet"];
    result["projection_receipt"] = (*applied)["receipt"];
    if (!(*applied)["receipt"].value("accepted", false)) {
      auto preview = invoke(prepared.callbacks, Json{{"operation", "preview"}, {"packet", prepared.base_packet},
          {"diff", (*compilation)["diff"]}, {"resource_limits", limits}});
      if (!preview) { result["status"] = "schema_error"; result["error"]["code"] = preview.error().message; return result; }
      candidate = preview->at("candidate_packet");
    }
    Json model_origin = nullptr;
    for (const auto& entity : (*compilation)["diff"]["entities"]["add"])
      if (entity.contains("attrs") && entity["attrs"].contains("model_origin")) {
        model_origin = entity["attrs"]["model_origin"]; break;
      }
    if (!model_origin.is_object() || model_origin.value("kind", "") != "model") {
      result["status"] = "schema_error"; result["error"]["code"] = "compiler_model_origin_unavailable"; return result;
    }
    Json bindings{{"node_ids", (*compilation)["node_ids"]}, {"compilation_sha256", (*compilation)["compilation_sha256"]},
        {"model_origin", model_origin}, {"raw_response_source_ref", first_response_ref},
        {"raw_response_sha256", first_response_ref.value("blob_hash", Json(nullptr))},
        {"model_content_sha256", Sha256::hex(model_content)}, {"compiled_content_sha256", Sha256::hex(graph)},
        {"compiler_input_sha256", (*compilation)["raw_capture"]["sha256"]},
        {"response_text_sha256", Sha256::hex((*compilation)["response_text"].get<std::string>())},
        {"instrumentation", instrumentation}, {"transform", transform}};
    if (!prepared.binding_origin.is_null()) bindings["origin"] = prepared.binding_origin;
    if (!prepared.request_bytes.empty()) bindings["request_bytes"] = prepared.request_bytes;
    if (prepared.manifest.contains("trace") && prepared.manifest["trace"].value("request_hash_scope", "") == "actual_sent_payload")
      bindings["expected_request_sha256"] = prepared.manifest["trace"].value("request_sha256", Json(nullptr));
    result["candidate_packet"] = candidate;
    auto unbound_source = retain_graph_reply_bytes(services, json::canonical(candidate), Json{{"channel", "compiled_graph_candidate"},
        {"model_content_origin", "model"}, {"content_verification", "unverified"}, {"binding_status", "unbound"},
        {"compilation_sha256", (*compilation)["compilation_sha256"]}, {"method_manifest", prepared.manifest}});
    if (!unbound_source) { result["status"] = "storage_error"; result["error"]["code"] = std::string(errc_name(unbound_source.error().code)); return result; }
    result["candidate_source_ref"] = *unbound_source;
    if (!prepared.callbacks.bind_results) {
      result["error"]["code"] = "method_result_binding_unavailable";
      return result;
    }
    if (prepared.binding_origin.is_null()) { result["error"]["code"] = "method_result_origin_unavailable"; return result; }
    auto bound = prepared.callbacks.bind_results(candidate, prepared.manifest, bindings);
    if (!bound) { result["status"] = "binding_error"; result["error"]["code"] = std::string(errc_name(bound.error().code)); return result; }
    const auto& bound_packet = bound->at("packet");
    // Method trace binding adds provenance; it must not rewrite model content
    // or promote the deterministic compiler's output to recorded/user facts.
    for (const auto& id : (*compilation)["node_ids"]) {
      const auto before = std::find_if(candidate["entities"].begin(), candidate["entities"].end(),
          [&](const Json& entity) { return entity["id"] == id; });
      const auto after = std::find_if(bound_packet["entities"].begin(), bound_packet["entities"].end(),
          [&](const Json& entity) { return entity["id"] == id; });
      if (before == candidate["entities"].end() || after == bound_packet["entities"].end() || *before != *after) {
        result["status"] = "binding_error"; result["error"]["code"] = "method_binding_changed_model_content"; return result;
      }
    }
    candidate = bound_packet;
    result["manifest"] = bound->at("manifest");
    auto validated = invoke(prepared.callbacks, Json{{"operation", "validate"}, {"packet", candidate}, {"resource_limits", limits}});
    if (!validated) { result["status"] = "binding_error"; result["error"]["code"] = validated.error().message; return result; }
    result["candidate_packet"] = candidate;
    auto candidate_source = retain_graph_reply_bytes(services, json::canonical(candidate), Json{{"channel", "compiled_graph_candidate"},
        {"model_content_origin", "model"}, {"content_verification", "unverified"},
        {"compilation_sha256", (*compilation)["compilation_sha256"]}, {"method_manifest", result["manifest"]}});
    if (!candidate_source) { result["status"] = "storage_error"; result["error"]["code"] = std::string(errc_name(candidate_source.error().code)); return result; }
    result["candidate_source_ref"] = *candidate_source;
    result["model_origin"] = model_origin;
    result["status"] = "candidate";
    result["text"] = prepared.mode == "answer_as_graph" ? (*compilation)["response_text"] : Json(display);
    result.erase("error");
    if (const auto* admission = json::find(prepared.options, "admission")) {
      if (!admission->is_object() || !(*admission).contains("mode") || !(*admission)["mode"].is_string())
        throw std::invalid_argument("graph admission invalid");
      if ((*admission)["mode"] == "automatic") {
        const auto* request = json::find(*admission, "store_request");
        if (!request || !request->is_object() || !prepared.callbacks.accept) {
          result["status"] = "admission_unavailable";
          result["error"] = Json{{"code", "canonical_graph_store_request_unavailable"}};
          return result;
        }
        Json store_request = *request;
        store_request["packet"] = candidate;
        auto accepted = prepared.callbacks.accept(store_request);
        if (!accepted) { result["status"] = "admission_error"; result["error"] = Json{{"code", std::string(errc_name(accepted.error().code))}}; return result; }
        const auto* receipt = json::find(*accepted, "receipt");
        if (!receipt || !receipt->is_object() || json::get_string(*receipt, "schema") != "loom.graph_packet_store_receipt/1" ||
            receipt->value("explicitly_accepted", Json(false)) != Json(true) ||
            receipt->value("acceptance_establishes_content_truth", Json(true)) != Json(false) ||
            receipt->value("packet", Json(nullptr)) != candidate) {
          result["status"] = "admission_error"; result["error"] = Json{{"code", "canonical_graph_store_receipt_invalid"}}; return result;
        }
        result["store_receipt"] = *accepted;
        result["canonical_store_written"] = true;
        result["status"] = "accepted";
      } else if ((*admission)["mode"] != "candidate") {
        result["status"] = "admission_unavailable"; result["error"] = Json{{"code", "graph_admission_mode_unavailable"}};
      }
    }
    return result;
  } catch (...) { result["status"] = "schema_error"; result["error"] = Json{{"code", "graph_reply_adapter_input_invalid"}}; return result; }
}

Result<Json> graph_reply_fragment(const Json& result, const Json& address, const GraphReplyCallbacks& callbacks) {
  if (!result.is_object() || !result.contains("base_packet") || !result.contains("compilation"))
    return Error(Errc::Unavailable, "graph reply compilation unavailable");
  return invoke(callbacks, Json{{"operation", "reply_fragment"}, {"packet", result["base_packet"]},
      {"compilation", result["compilation"]}, {"address", address}});
}

struct ModelUsageGuard::Impl {
  explicit Impl(GraphReplyServices& borrowed) : services(borrowed) {}
  GraphReplyServices& services;
  Json decisions = Json::array();
  std::string operation;
  std::size_t input_bytes = 0;
  bool admitted = false;
#if LOOM_CHAT_MODEL_USAGE_AVAILABLE
  std::unique_ptr<UsagePolicy> policy;
#endif
};
ModelUsageGuard::ModelUsageGuard(GraphReplyServices& services) : impl_(std::make_unique<Impl>(services)) {}
ModelUsageGuard::~ModelUsageGuard() = default;
const Json& ModelUsageGuard::decisions() const { return impl_->decisions; }

Result<Json> ModelUsageGuard::admit(const net::HttpRequest& request, const Json& estimate, const Json& confirmation) {
  if (!context::current_context_execution_scope()) return Error(Errc::Unavailable, "model execution scope required");
#if LOOM_CHAT_MODEL_USAGE_AVAILABLE
  if (impl_->admitted) return Error(Errc::Conflict, "model usage admission already consumed");
  if (!estimate.is_object() || json::get_string(estimate, "baseline_key").empty() ||
      !estimate.contains("resources") || !estimate["resources"].is_object())
    return Error(Errc::InvalidArgument, "model usage estimate requires explicit cohort and resource data");
  Json bound = estimate;
  impl_->input_bytes = request.body.size();
  bound["resources"]["requests"] = 1;
  bound["resources"]["input_bytes"] = impl_->input_bytes;
  Json headers = Json::array();
  for (const auto& [name, value] : request.headers) headers.push_back(Json::array({name, value}));
  bound["request_identity_sha256"] = Sha256::hex(json::canonical(Json{{"method", request.method}, {"url", request.url},
      {"headers", headers}, {"body", request.body}, {"timeout_ms", request.timeout_ms}, {"stream", request.stream}}));
  bound["request_body_sha256"] = Sha256::hex(request.body);
  if (!bound.contains("operation_id")) bound["operation_id"] = "model_usage_" + random_hex(32);
  impl_->operation = json::get_string(bound, "operation_id");
  LOOM_TRY_ASSIGN(auto settings, effective_usage_policy_options(impl_->services.config));
  LOOM_TRY_ASSIGN(auto opened, UsagePolicy::open(impl_->services.root / "usage-policy.sqlite", settings));
  impl_->policy = std::move(opened);
  LOOM_TRY_ASSIGN(auto decision, impl_->policy->request(bound));
  record_decision(impl_->decisions, decision);
  if (!confirmation.empty() && json::get_string(decision, "status") == "requires_confirmation") {
    if (!confirmation.is_object() || !confirmation.contains("approved") || !confirmation["approved"].is_boolean())
      return Error(Errc::InvalidArgument, "model usage confirmation invalid");
    LOOM_TRY_ASSIGN(auto confirmed, impl_->policy->confirm(impl_->operation, json::get_string(confirmation, "receipt_id"),
        confirmation["approved"].get<bool>(), json::get_string(confirmation, "ref")));
    decision = std::move(confirmed);
    record_decision(impl_->decisions, decision);
  }
  if (!decision.value("authorized", false)) return decision;
  if (const auto* actual = json::find(decision, "actual"); actual && actual->contains("requests")) {
    decision["dispatch_status"] = "operation_already_attempted";
    return decision;
  }
  LOOM_TRY_ASSIGN(auto claim, context::claim_context_usage_operation(impl_->services.root, decision));
  record_decision(impl_->decisions, claim);
  decision["execution_claim"] = claim;
  decision["dispatch_authorized"] = claim.value("claimed", false);
  impl_->admitted = decision["dispatch_authorized"].get<bool>();
  return decision;
#else
  (void)request; (void)estimate; (void)confirmation;
  return Error(Errc::Unavailable, "shared model usage policy unavailable");
#endif
}

Result<Json> ModelUsageGuard::settle(bool attempted, std::size_t received_bytes, const Json& usage) {
#if LOOM_CHAT_MODEL_USAGE_AVAILABLE
  if (!impl_->policy || !impl_->admitted) return Error(Errc::Conflict, "model usage was not dispatched");
  if (!attempted) {
    LOOM_TRY_ASSIGN(auto cancelled, impl_->policy->cancel(impl_->operation, "model_transport_not_attempted"));
    record_decision(impl_->decisions, cancelled);
    impl_->admitted = false;
    return cancelled;
  }
  LOOM_TRY_ASSIGN(auto measured, impl_->policy->complete(impl_->operation, Json{{"resources", Json{{"requests", 1},
      {"input_bytes", impl_->input_bytes}, {"response_bytes", received_bytes}}}, {"provenance", "instrument_measured"}}));
  record_decision(impl_->decisions, measured);
  LOOM_TRY_ASSIGN(auto reported, impl_->policy->complete(impl_->operation, Json{{"resources", Json{
      {"output_tokens", quantity(usage, {"completion_tokens", "output_tokens"})},
      {"cost_usd", quantity(usage, {"cost", "cost_usd"})}}}, {"provenance", "provider_reported"}}));
  record_decision(impl_->decisions, reported);
  impl_->admitted = false;
  return reported;
#else
  (void)attempted; (void)received_bytes; (void)usage;
  return Error(Errc::Unavailable, "shared model usage policy unavailable");
#endif
}

Json run_graph_reply_postprocess(GraphReplyServices& services, const GraphReplyPrepared& prepared,
    const Json& payload, const Json& options, std::string_view text, const Json& primary_source,
    const CancelToken* cancel) {
  auto result = failure("unavailable", text, primary_source);
  result["mode"] = "separate_model_afterwards";
  result["primary_response_ref"] = primary_source;
  try {
    if (prepared.mode != "separate_model_afterwards" || !prepared.capability.value("available", false)) return result;
    if (!options.is_object() || !options.contains("calls_authorized") || options["calls_authorized"] != Json(true) ||
        !context::current_context_execution_scope()) {
      result["error"]["code"] = "postprocess_calls_not_authorized"; return result;
    }
    if (!services.providers) { result["error"]["code"] = "provider_registry_unavailable"; return result; }
    auto manifest = services.providers->get(json::get_string(options, "provider_id"));
    if (!manifest || !services.providers->available(*manifest)) { result["error"]["code"] = "postprocess_provider_unavailable"; return result; }
    bool capable = false;
    const bool stream = payload.is_object() && payload.value("stream", Json(false)) == Json(true);
    const auto constraints = options.value("capability_constraints", Json::object());
    if (!constraints.is_object()) { result["error"]["code"] = "postprocess_capability_constraints_invalid"; return result; }
    for (const auto& offered : manifest->capabilities)
      if (offered.resource == "llm" && offered.name == (stream ? "chat.stream" : "chat.completions") &&
          constraints_satisfied(offered.constraints, constraints)) capable = true;
    if (!capable) { result["error"]["code"] = "postprocess_capability_unavailable"; return result; }
    if (!payload.is_object() || json::get_string(payload, "model").empty() || !payload.contains("messages") ||
        !payload["messages"].is_array() || (payload.contains("stream") && !payload["stream"].is_boolean())) {
      result["error"]["code"] = "postprocess_payload_invalid"; return result;
    }
    const auto* timeout = json::find(options, "timeout_ms");
    if (!timeout || !timeout->is_number_integer() || (timeout->is_number_unsigned() &&
        timeout->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) ||
        timeout->get<std::int64_t>() <= 0 || timeout->get<std::int64_t>() > std::numeric_limits<int>::max()) {
      result["error"]["code"] = "postprocess_timeout_data_required"; return result;
    }
    const std::set<std::string> auth{"none", "bearer", "token", "x-api-key", "query:key"};
    if (!auth.contains(manifest->auth_scheme)) { result["error"]["code"] = "postprocess_auth_adapter_unavailable"; return result; }
    const auto key = services.secrets.get_string(manifest->auth_secret);
    if (!manifest->auth_secret.empty() && key.empty()) { result["error"]["code"] = "postprocess_credential_unavailable"; return result; }
    net::HttpRequest request;
    request.method = "POST";
    request.url = manifest->base_url;
    while (!request.url.empty() && request.url.back() == '/') request.url.pop_back();
    const auto* endpoint = json::find(options, "endpoint_path");
    if (!endpoint) endpoint = json::find(manifest->metadata, "chat_completions_path");
    if (!endpoint || !endpoint->is_string() || endpoint->get<std::string>().empty()) {
      result["error"]["code"] = "postprocess_endpoint_data_required"; return result;
    }
    const auto path = endpoint->get<std::string>();
    request.url += path.front() == '/' ? path : "/" + path;
    request.body = json::dump(payload);
    request.timeout_ms = timeout->get<int>();
    request.stream = stream;
    for (auto header = manifest->default_headers.begin(); header != manifest->default_headers.end(); ++header) {
      if (!header->is_string()) { result["error"]["code"] = "postprocess_manifest_header_invalid"; return result; }
      request.headers.emplace_back(header.key(), header->get<std::string>());
    }
    request.headers.emplace_back("Content-Type", "application/json");
    if (manifest->auth_scheme == "bearer") request.headers.emplace_back("Authorization", "Bearer " + key);
    else if (manifest->auth_scheme == "token") request.headers.emplace_back("Authorization", "token " + key);
    else if (manifest->auth_scheme == "x-api-key") request.headers.emplace_back("x-api-key", key);
    else if (manifest->auth_scheme == "query:key") request.url = net::with_query(request.url, {{"key", key}});
    const auto* estimate = json::find(options, "usage_estimate");
    if (!estimate) { result["error"]["code"] = "postprocess_usage_estimate_data_required"; return result; }
    ModelUsageGuard guard(services);
    auto admission = guard.admit(request, *estimate, options.value("confirmation", Json::object()));
    result["execution_usage"] = guard.decisions();
    if (!admission) { result["error"]["code"] = std::string(errc_name(admission.error().code)); return result; }
    result["usage_admission"] = *admission;
    if (!admission->value("dispatch_authorized", false)) { result["status"] = "usage_held"; return result; }
    if (cancel && cancel->cancelled()) {
      auto settlement = guard.settle(false, 0);
      result["status"] = "cancelled";
      result["execution_usage"] = guard.decisions();
      if (!settlement) result["error"]["code"] = "preflight_cancellation_accounting_error";
      return result;
    }
    std::string bytes;
    int status = 0;
    bool received_headers = false;
    net::StreamSink sink;
    sink.on_headers = [&](int code, const net::Headers&) { status = code; received_headers = true; return true; };
    sink.on_data = [&](std::string_view chunk) { bytes.append(chunk); return !(cancel && cancel->cancelled()); };
    Result<net::HttpResponse> response = Error(Errc::Network, "postprocess_transport_failed");
    try { response = services.http.send(request, &sink, cancel); }
    catch (...) { response = Error(Errc::Network, "postprocess_transport_failed"); }
    if (response) { status = response->status; if (bytes.empty()) bytes = response->body; }
    Json source_ref = nullptr;
    if (received_headers || response || !bytes.empty()) {
      auto source = retain_graph_reply_bytes(services, bytes, Json{{"channel", "http_wire"}, {"status", status},
          {"complete", static_cast<bool>(response)}, {"provider_id", manifest->id},
          {"requested_model", payload["model"]}, {"request_sha256", Sha256::hex(request.body)},
          {"model_origin", "model"}, {"transport_error", response ? Json(nullptr) : Json(std::string(errc_name(response.error().code)))}});
      if (!source) {
        auto unknown = guard.settle(true, bytes.size());
        result["execution_usage"] = guard.decisions();
        result["accounting_status"] = unknown ? "measured_with_unknown_provider_usage" : "error";
        const Json missing{{"source_status", "unavailable"}, {"blob_hash", Sha256::hex(bytes)}, {"bytes", bytes.size()}};
        result["postprocess_first_response_ref"] = missing;
        result["first_response_ref"] = missing;
        result["status"] = "storage_error"; result["error"]["code"] = std::string(errc_name(source.error().code)); return result;
      }
      source_ref = *source;
      result["postprocess_first_response_ref"] = source_ref;
      result["first_response_ref"] = source_ref;
    }
    // Parse only after the exact first bytes have a durable source reference.
    Json parsed_usage = Json::object();
    auto parsed = parse_postprocess_response(bytes, stream);
    if (parsed) {
      if (const auto* usage = json::find(*parsed, "usage"); usage && usage->is_object()) parsed_usage = *usage;
    }
    auto settlement = guard.settle(true, bytes.size(), parsed_usage);
    result["execution_usage"] = guard.decisions();
    if (!settlement) { result["status"] = "accounting_error"; result["error"]["code"] = std::string(errc_name(settlement.error().code)); return result; }
    if (!response || !response->ok()) {
      result["status"] = "postprocess_error";
      result["error"]["code"] = response ? "http" : std::string(errc_name(response.error().code));
      return result;
    }
    if (!parsed) { result["status"] = "schema_error"; result["error"]["code"] = "postprocess_response_json_invalid"; return result; }
    const auto* choices = json::find(*parsed, "choices");
    if (!choices || !choices->is_array() || choices->empty() || !(*choices)[0].is_object() ||
        !(*choices)[0].contains("message") || !(*choices)[0]["message"].is_object() ||
        !(*choices)[0]["message"].contains("content") || !(*choices)[0]["message"]["content"].is_string()) {
      result["status"] = "schema_error"; result["error"]["code"] = "postprocess_content_invalid"; return result;
    }
    auto actual = prepared;
    actual.request_bytes = request.body;
    const auto reported_model = json::get_string(*parsed, "model");
    if (!reported_model.empty()) actual.host["model"] = reported_model;
    Json instrumentation{{"requested_model", payload["model"]},
        {"reported_model", reported_model.empty() ? Json(nullptr) : Json(reported_model)},
        {"model_identity_basis", reported_model.empty() ? "requested_unverified" : "provider_reported"},
        {"http_status", status}, {"usage", parsed_usage}, {"execution_usage", guard.decisions()}};
    auto finished = finish_graph_reply(services, actual, (*choices)[0]["message"]["content"].get<std::string>(), text,
        source_ref, instrumentation);
    finished["primary_response_ref"] = primary_source;
    finished["postprocess_first_response_ref"] = source_ref;
    finished["execution_usage"] = guard.decisions();
    return finished;
  } catch (...) { result["status"] = "postprocess_error"; result["error"]["code"] = "postprocess_adapter_input_invalid"; return result; }
}

}  // namespace loom::chat
