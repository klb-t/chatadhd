#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "native-ui-common.h"
#include "method-ui-routes.h"
#include "../../src/chat/graph_reply.h"
#include "../../src/context/context_execution.h"
#include "../../src/extract/prompt_contract.h"
#include "loom/config.h"
#include "loom/net/sse.h"
#include "loom/semantic_llm.h"
#include "loom/model.h"
#include "loom/util/base64.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom_server {
namespace analysis_ui_detail {
using native_ui::Json;
namespace prompts = loom::extract::prompts;

inline loom::Error invalid(std::string message) { return {loom::Errc::InvalidArgument, std::move(message)}; }
inline std::string lower(std::string value) {
  for (auto& byte : value) byte = static_cast<char>(std::tolower(static_cast<unsigned char>(byte)));
  return value;
}
inline bool credential_name(const std::string& name) {
  const auto key = lower(name);
  return key == "authorization" || key == "proxy-authorization" || key == "cookie" || key == "set-cookie" ||
      key == "api_key" || key == "apikey" || key == "x-api-key" || key == "access_token" ||
      key == "password" || key == "client_secret" || key == "token" || key == "secret";
}
inline std::string hide_text(std::string text, const std::vector<std::string>& secrets) {
  for (const auto& secret : secrets) {
    if (secret.empty()) continue;
    std::size_t offset = 0;
    while ((offset = text.find(secret, offset)) != std::string::npos) {
      text.replace(offset, secret.size(), "[redacted]"); offset += 10;
    }
  }
  return text;
}
inline Json redact(const Json& value, const std::vector<std::string>& secrets) {
  if (value.is_string()) return hide_text(value.get<std::string>(), secrets);
  if (value.is_array()) { Json result = Json::array(); for (const auto& child : value) result.push_back(redact(child, secrets)); return result; }
  if (!value.is_object()) return value;
  Json result = Json::object();
  for (const auto& child : value.items())
    result[child.key()] = credential_name(child.key()) && child.value().is_string() ? Json("[redacted]") : redact(child.value(), secrets);
  return result;
}
inline void header_secrets(const Json& headers, std::vector<std::string>& secrets) {
  if (!headers.is_object()) return;
  for (const auto& header : headers.items()) if (credential_name(header.key()) && header.value().is_string())
    secrets.push_back(header.value().get<std::string>());
}
inline void url_secrets(const std::string& url, std::vector<std::string>& secrets) {
  const auto authority_start = url.find("://");
  if (authority_start != std::string::npos) {
    const auto end = url.find_first_of("/?#", authority_start + 3);
    const auto account_end = url.find('@', authority_start + 3);
    if (account_end != std::string::npos && (end == std::string::npos || account_end < end))
      secrets.push_back(url.substr(authority_start + 3, account_end - authority_start - 3));
  }
  auto query = url.find('?');
  if (query == std::string::npos) return;
  ++query;
  while (query < url.size()) {
    const auto end = url.find_first_of("&#", query);
    const auto equals = url.find('=', query);
    if (equals != std::string::npos && (end == std::string::npos || equals < end)) {
      const auto key = lower(url.substr(query, equals - query));
      if (credential_name(key) || key == "key" || key == "sig" || key == "signature" || key == "access_key")
        secrets.push_back(url.substr(equals + 1, (end == std::string::npos ? url.size() : end) - equals - 1));
    }
    if (end == std::string::npos || url[end] == '#') break;
    query = end + 1;
  }
}
inline loom::Result<std::string> required_string(const Json& value, const char* key) {
  if (!value.contains(key) || !value[key].is_string() || value[key].get_ref<const std::string&>().empty())
    return invalid(std::string(key) + " must be a nonempty string");
  return value[key].get<std::string>();
}
inline loom::Status object_field(const Json& value, const char* key) {
  if (value.contains(key) && !value[key].is_object()) return invalid(std::string(key) + " must be an object");
  return {};
}
inline loom::Result<int> timeout(const Json& execution, int preset) {
  if (!execution.contains("timeout_ms")) return preset;
  const auto& value = execution["timeout_ms"];
  if (!value.is_number_integer() || value.get<double>() < 0 || value.get<double>() > std::numeric_limits<int>::max())
    return invalid("timeout_ms must fit a nonnegative native int");
  return value.get<int>();
}
inline loom::Result<std::size_t> response_limit(const Json& execution, const Json& definition) {
  const auto limits = definition["analysis_parameters"].value("limits", Json::object());
  const Json value = execution.value("max_response_bytes", limits.value("max_response_bytes", Json(nullptr)));
  if (value.is_null()) return std::numeric_limits<std::size_t>::max();
  if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
    return invalid("max_response_bytes must be a nonnegative native size or null");
  const auto number = value.get<std::uint64_t>();
  if (number > std::numeric_limits<std::size_t>::max())
    return invalid("max_response_bytes must be a nonnegative native size or null");
  return static_cast<std::size_t>(number);
}

inline loom::Result<prompts::Contract> effective_contract(loom::Runtime& runtime, const Json& body) {
  LOOM_TRY(object_field(body, "prompt_patch"));
  const auto patch = body.value("prompt_patch", Json::object());
  if (body.contains("prompt_snapshot_json")) {
    LOOM_TRY_ASSIGN(auto text, required_string(body, "prompt_snapshot_json"));
    LOOM_TRY_ASSIGN(auto snapshot, loom::json::parse(text));
    return prompts::from_snapshot(prompts::overlay(snapshot, patch));
  }
  if (body.contains("prompt_snapshot")) return prompts::from_snapshot(prompts::overlay(body["prompt_snapshot"], patch));
  const auto id = body.value("contract_id", std::string("semantic.analysis"));
  const auto directory = runtime.config().get("analysis_prompt_dir", (runtime.paths().root / "prompts").string());
  if (!directory.is_string()) return invalid("analysis_prompt_dir must be a string");
  const auto overrides = runtime.config().get("analysis_prompt_overrides", Json::object());
  if (!overrides.is_object()) return invalid("analysis_prompt_overrides must be an object");
  const auto configured = overrides.contains(id) ? overrides[id] : Json::object();
  return prompts::resolve(id, directory.get<std::string>(), prompts::overlay(configured, patch));
}

struct Prepared {
  std::mutex mutex;
  prompts::Contract contract;
  loom::net::HttpRequest request;
  Json preparation;
  Json execution;
  Json estimate;
  Json method_ref;
  Json method_snapshot = nullptr;
  Json method_leaf = nullptr;
  Json graph_plan = nullptr;
  Json graph_origin = nullptr;
  std::string result_kind;
  std::string graph_target;
  Json result = Json::object();
  std::vector<std::string> secrets;
  std::string id;
  std::string identity;
  std::string status = "prepared";
  std::size_t max_response_bytes = 0;
  bool attempted = false;
};
struct State {
  std::mutex mutex;
  std::map<std::string, std::shared_ptr<Prepared>, std::less<>> prepared;
};

inline std::string request_identity(const loom::net::HttpRequest& request) {
  Json headers = Json::array();
  for (const auto& header : request.headers) headers.push_back(Json::array({header.first, header.second}));
  return loom::Sha256::hex(loom::json::canonical(Json{{"method", request.method}, {"url", request.url},
      {"headers", headers}, {"body", request.body}, {"timeout_ms", request.timeout_ms}, {"stream", request.stream}}));
}
inline Json view(const Prepared& prepared) {
  Json request = prepared.request.to_json();
  request["body_bytes"] = prepared.request.body;
  request["body_json"] = prepared.preparation["body_json"];
  Json result{{"schema", "loom.analysis_prepared/1"}, {"prepared_id", prepared.id}, {"status", prepared.status},
      {"request_identity_hash", prepared.identity}, {"request_hash", prepared.preparation["request_hash"]},
      {"contract_hash", prepared.contract.hash}, {"output_schema_hash", prepared.preparation["output_schema_hash"]},
      {"contract", prepared.contract.definition}, {"request", request}, {"execution", prepared.execution},
      {"usage_estimate", prepared.estimate}, {"method_ref", prepared.method_ref}, {"attempted", prepared.attempted},
      {"graph_plan", prepared.graph_plan}, {"graph_binding_available", !prepared.graph_plan.is_null()},
      {"credentials_redacted", true}, {"preparation_persistence", "process_memory"},
      {"dispatch_policy", "one_explicit_attempt_no_automatic_retry"}, {"result", prepared.result}};
  result["contract_json"] = redact(prepared.contract.definition, prepared.secrets).dump();
  return redact(result, prepared.secrets);
}

inline loom::Status prepare_graph(loom::Runtime& runtime, Prepared& prepared, const Json& body) {
  if (!body.contains("method")) return {};
  const auto& method = body["method"];
  if (!method.is_object()) return invalid("method must be native method binding data");
  LOOM_TRY_ASSIGN(auto profile, method_ui::payload(method, "profile"));
  if (!profile.is_object()) return invalid("method.profile_json must encode native method profile data");
  LOOM_TRY_ASSIGN(auto receipts, method_ui::receipt_ids(method.value("receipt_ids", Json::array())));
  loom::context::MethodRegistry registry(runtime.db());
  LOOM_TRY_ASSIGN(auto snapshot, registry.load(profile, receipts));
  const Json capabilities = method_ui::capabilities();
  LOOM_TRY_ASSIGN(auto selection, method_ui::payload(method, "selection", Json::object()));
  LOOM_TRY_ASSIGN(auto resolved, registry.resolve(snapshot, selection, capabilities));
  if (resolved["leaves"].size() != 1) return invalid("analysis requires one explicitly selected method leaf");
  const auto& leaf = resolved["leaves"][0];
  if (!leaf.value("available", false) || leaf.value("execution_capability", std::string()) != "analysis.http")
    return loom::Error(loom::Errc::Unavailable, "selected version does not declare the native analysis.http capability");
  if (!leaf["path"].is_array() || leaf["path"].size() != 1 || leaf.value("weight", 1.0) != 1.0 || !leaf["fusions"].empty())
    return loom::Error(loom::Errc::Unavailable, "single-request analysis does not execute combinations, weights or fusions");
  if (!leaf["recipe"].is_null() || !leaf["prompt"].is_null() || !leaf["preset"].is_null())
    return loom::Error(loom::Errc::Unavailable, "analysis uses its registered W1 contract; separate recipe, prompt or preset links need a matching native execution adapter");
  const auto version_id = leaf["method_version_id"].get<std::string>();
  bool registered = false;
  loom::kb::GraphPacketStore packets(runtime.db());
  for (const auto& receipt_id : receipts) {
    LOOM_TRY_ASSIGN(auto read, packets.execute(Json{{"operation", "read"}, {"receipt_id", receipt_id}}));
    for (const auto& id : read["receipt"]["selection"]["entities"]) if (id == version_id) registered = true;
  }
  if (!registered) return loom::Error(loom::Errc::Unavailable, "analysis version needs an actual accepted native graph receipt");
  const auto& definition = snapshot["entities"][version_id]["attrs"]["definition"];
  if (!definition.contains("analysis_contract") || definition.value("analysis_contract_sha256", Json(nullptr)) != prepared.contract.hash ||
      method_ui::hash(definition["analysis_contract"]) != prepared.contract.hash)
    return loom::Error(loom::Errc::Conflict, "selected graph version differs from the edited analysis contract; save a new version first");
  LOOM_TRY_ASSIGN(prepared.result_kind, required_string(definition, "analysis_result_kind"));
  LOOM_TRY_ASSIGN(prepared.graph_target, required_string(method, "target"));
  prepared.graph_origin = method.at("origin");
  const auto known = method.value("known_at", Json(loom::timeutil::utc_now_iso()));
  Json consumed{{"analysis_contract", prepared.contract.definition}, {"execution", prepared.execution},
      {"request_body", prepared.preparation["body_json"]}, {"request_identity_hash", prepared.identity},
      {"request_bytes_sha256", loom::Sha256::hex(prepared.request.body)}};
  if (redact(consumed, prepared.secrets) != consumed) return invalid("graph definitions and request bodies must not contain transport credentials");
  const Json run_context{{"run_id", prepared.id + "_run"}, {"origin", prepared.graph_origin}, {"known_at", known},
      {"input_sha256", loom::Sha256::hex(prepared.request.body)}, {"effective_parameters", consumed},
      {"measurement_scope", "native_single_analysis_http_attempt"},
      {"measurements", Json{{"provider_calls", nullptr}, {"response_bytes", nullptr}, {"output_tokens", nullptr}, {"cost_usd", nullptr}}}};
  LOOM_TRY_ASSIGN(auto plan, registry.prepare(snapshot, leaf, run_context, loom::chat::graph_reply_packet_operation));
  prepared.method_snapshot = std::move(snapshot); prepared.method_leaf = leaf; prepared.graph_plan = std::move(plan);
  prepared.method_ref = prepared.graph_plan["manifest"]["bindings"];
  return {};
}

inline loom::Status bind_graph(loom::Runtime& runtime, Prepared& prepared, const std::string& bytes, const Json& decoded) {
  const auto known = loom::timeutil::utc_now_iso();
  const auto& manifest = prepared.graph_plan["manifest"];
  const auto wire_model = prepared.preparation["body_json"].value("model", Json(nullptr));
  const bool model_content_available = prepared.result.value("model_content_available", false);
  const auto reported_model = decoded.is_object() ? decoded.value("model", Json(nullptr)) : Json(nullptr);
  const bool reported_model_available = reported_model.is_string() && !reported_model.get_ref<const std::string&>().empty();
  const bool requested_model_available = wire_model.is_string() && !wire_model.get_ref<const std::string&>().empty();
  const auto actual_model = reported_model_available ? reported_model : wire_model;
  Json model_origin{{"kind", "model"}, {"model", actual_model}, {"recipe_sha256", manifest["trace"].value("recipe_sha256", Json(nullptr))},
      {"response_sha256", loom::Sha256::hex(bytes)}, {"request_id", prepared.id}, {"turn_id", prepared.id}, {"parent_turn_id", nullptr}};
  loom::model::Entity entity;
  entity.kind = prepared.result_kind; entity.canonical_key = prepared.id + "/" + loom::Sha256::hex(bytes);
  entity.id = loom::model::Entity::make_id(entity.kind, entity.canonical_key); entity.label = "Analysis response";
  entity.first_seen = known; entity.last_seen = known; entity.origin = model_content_available ? loom::model::Origin::ModelKnowledge : loom::model::Origin::System;
  entity.confidence = model_content_available ? 0 : 1;
  entity.attrs = Json{{"content_verification", "unverified"},
      {"first_response_ref", prepared.result["first_response_ref"]}, {"response_sha256", loom::Sha256::hex(bytes)},
      {"output", prepared.result["output"]}, {"output_validation", prepared.result["output_validation"]},
      {"http_status", prepared.result["http_status"]}, {"capture_truncated", prepared.result["capture_truncated"]}};
  entity.attrs["result_channel"] = model_content_available ? "decoded_model_content" : "transport_or_service_result";
  entity.attrs["requested_model"] = wire_model;
  entity.attrs["reported_model"] = reported_model;
  entity.attrs["model_identity_basis"] = model_content_available ? (reported_model_available ? "provider_reported" : requested_model_available ? "requested_unverified" : "unavailable") : "unavailable";
  if (model_content_available) entity.attrs["model_origin"] = model_origin;
  loom::model::Observation source;
  source.text = loom::base64::encode(bytes);
  source.locator.source = "sha256:" + loom::Sha256::hex(source.text);
  source.locator.member = "analysis-response-base64"; source.locator.byte_start = 0; source.locator.byte_len = static_cast<std::int64_t>(source.text.size());
  source.unit = loom::model::Unit::make_id(source.locator.source, source.locator);
  source.id = loom::model::Observation::make_id(source.unit, source.locator, source.text);
  source.attrs = Json{{"encoding", "base64"}, {"raw_response_sha256", loom::Sha256::hex(bytes)},
      {"derived_from", prepared.result["first_response_ref"]}, {"transform", "base64_encode_exact_http_bytes"}};
  source.date = known; source.artifact_type = "api_response";
  LOOM_TRY_ASSIGN(auto diff, loom::chat::graph_reply_packet_operation(Json{{"operation", "empty_diff"},
      {"packet", prepared.graph_plan["packet"]}, {"proposal_id", entity.id}, {"origin", prepared.graph_origin}, {"known_at", known}}));
  diff["entities"]["add"].push_back(entity.to_json());
  diff["sources"]["add"].push_back(Json{{"observation", source.to_json()}, {"known_at", known}, {"text_sha256", loom::Sha256::hex(source.text)}});
  LOOM_TRY_ASSIGN(auto projected, loom::chat::graph_reply_packet_operation(Json{{"operation", "preview"}, {"packet", prepared.graph_plan["packet"]}, {"diff", diff}}));
  loom::context::MethodRegistry registry(runtime.db());
  Json bindings{{"result_entity_ids", Json::array({entity.id})}, {"origin", prepared.graph_origin}, {"known_at", known},
      {"raw_response_source_ref", prepared.result["first_response_ref"]},
      {"raw_response_sha256", loom::Sha256::hex(bytes)}, {"request_bytes", prepared.request.body},
      {"expected_request_sha256", method_ui::hash(prepared.preparation["body_json"])},
      {"response_provenance", "native_retained_first_http_response"},
      {"instrumentation", Json{{"usage_decisions", prepared.result["usage_decisions"]}, {"request_identity_hash", prepared.identity}}},
      {"measurements", Json{{"provider_calls", 1}, {"response_bytes", bytes.size()}, {"output_tokens", nullptr}, {"cost_usd", nullptr}}}};
  if (model_content_available) bindings["model_origin"] = model_origin;
  LOOM_TRY_ASSIGN(auto bound, registry.bind_results(projected["candidate_packet"], manifest, bindings, loom::chat::graph_reply_packet_operation));
  LOOM_TRY_ASSIGN(auto cas, method_ui::expected_rows(runtime.db(), bound["packet"], prepared.graph_target));
  prepared.result["candidate_packet"] = bound["packet"]; prepared.result["method_manifest"] = bound["manifest"];
  prepared.result["candidate_packet_json"] = bound["packet"].dump();
  prepared.result["result_entity_id"] = entity.id;
  prepared.result["accept_request"] = Json{{"operation", "accept"}, {"target", prepared.graph_target}, {"packet", bound["packet"]},
      {"selection", cas["selection"]}, {"expected_rows", cas["expected_rows"]}, {"explicitly_accepted", true}};
  prepared.result["accept_request_json"] = prepared.result["accept_request"].dump();
  return {};
}

inline loom::Result<Json> prepare(LoomContext* context, State& state, const Json& body) {
  auto& runtime = *context->rt;
  for (const auto key : {"bindings", "request_patch", "execution", "method_ref"}) LOOM_TRY(object_field(body, key));
  LOOM_TRY_ASSIGN(auto contract, effective_contract(runtime, body));
  const auto model_value = body.value("model", runtime.config().get("semantic_model", ""));
  const auto provider_value = body.value("provider", runtime.config().get("base_url", ""));
  if (!model_value.is_string() || !provider_value.is_string()) return invalid("model and provider must be strings");
  LOOM_TRY_ASSIGN(auto wire, prompts::prepare_request(contract, model_value.get<std::string>(), provider_value.get<std::string>(),
      body.value("bindings", Json::object()), body.value("request_patch", Json::object())));
  if (body.contains("body_bytes")) {
    if (!body["body_bytes"].is_string()) return invalid("body_bytes must be exact JSON text");
    const auto bytes = body["body_bytes"].get<std::string>();
    LOOM_TRY_ASSIGN(auto parsed, loom::json::parse(bytes));
    if (!parsed.is_object()) return invalid("body_bytes must contain a JSON object");
    wire["body_bytes"] = bytes; wire["body_json"] = parsed;
    wire["request_hash"] = loom::Sha256::hex(loom::json::canonical(Json{{"method", wire["method"]}, {"url", wire["url"]},
        {"headers", wire["headers"]}, {"body_bytes", bytes}, {"transport", wire["transport"]},
        {"validation_mode", contract.definition["validation_mode"]}, {"output_schema_hash", wire["output_schema_hash"]}}));
  }
  auto prepared = std::make_shared<Prepared>();
  prepared->contract = std::move(contract); prepared->preparation = wire;
  prepared->execution = body.value("execution", Json::object());
  prepared->method_ref = body.value("method_ref", Json::object());
  prepared->request.method = wire["method"].get<std::string>();
  prepared->request.url = wire["url"].get<std::string>();
  LOOM_TRY_ASSIGN(auto url, loom::net::parse_url(prepared->request.url));
  if (url.scheme != "http" && url.scheme != "https") return invalid("analysis transport must use http or https");
  prepared->request.body = wire["body_bytes"].get<std::string>();
  prepared->request.stream = wire["transport"].value("stream", false);
  LOOM_TRY_ASSIGN(prepared->request.timeout_ms, timeout(prepared->execution, wire["transport"].value("timeout_ms", 30000)));
  LOOM_TRY_ASSIGN(prepared->max_response_bytes, response_limit(prepared->execution, prepared->contract.definition));
  header_secrets(wire["headers"], prepared->secrets);
  url_secrets(prepared->request.url, prepared->secrets);
  const auto key = runtime.secrets().get_string("api_key");
  if (!key.empty()) prepared->secrets.push_back(key);
  for (const auto& header : wire["headers"].items()) prepared->request.headers.emplace_back(header.key(), header.value().get<std::string>());
  if (loom::net::header_value(prepared->request.headers, "Authorization").empty() && !key.empty())
    prepared->request.headers.emplace_back("Authorization", "Bearer " + key);
  prepared->id = "analysis_" + loom::random_hex(32);
  prepared->identity = request_identity(prepared->request);
  const auto wire_model = wire["body_json"].value("model", model_value);
  prepared->estimate = prepared->execution.value("usage_estimate", Json::object());
  if (!prepared->estimate.is_object()) return invalid("execution.usage_estimate must be an object");
  prepared->estimate["operation_id"] = prepared->id;
  if (!prepared->estimate.contains("baseline_key")) prepared->estimate["baseline_key"] =
      "analysis/" + prepared->contract.definition["id"].get<std::string>() + "/" + loom::json::canonical(wire_model) + "/" + loom::Sha256::hex(prepared->request.url);
  if (!prepared->estimate.contains("resources")) prepared->estimate["resources"] = Json::object();
  if (!prepared->estimate["resources"].is_object()) return invalid("usage_estimate.resources must be an object");
  for (const auto key_name : {"output_tokens", "response_bytes", "cost_usd"}) {
    const std::string estimate_key = std::string("estimated_") + key_name;
    if (prepared->execution.contains(estimate_key)) prepared->estimate["resources"][key_name] = prepared->execution[estimate_key];
    else if (!prepared->estimate["resources"].contains(key_name)) prepared->estimate["resources"][key_name] = nullptr;
  }
  prepared->estimate["resources"]["requests"] = 1;
  prepared->estimate["resources"]["input_bytes"] = prepared->request.body.size();
  prepared->estimate["contract_hash"] = prepared->contract.hash;
  prepared->estimate["method_ref"] = prepared->method_ref;
  prepared->estimate["estimate_basis"] = "caller_forecasts_or_unknown_and_exact_body_bytes";
  // Caller extensions are ledger input. Credentials may only live in the private transport.
  if (redact(prepared->estimate, prepared->secrets) != prepared->estimate)
    return invalid("usage estimate must not contain credentials");
  LOOM_TRY(prepare_graph(runtime, *prepared, body));
  prepared->estimate["method_ref"] = prepared->method_ref;
  { std::lock_guard lock(state.mutex); state.prepared.emplace(prepared->id, prepared); }
  return view(*prepared);
}

inline loom::Result<Json> decode(const std::string& bytes, bool stream) {
  if (!stream) return loom::json::parse(bytes);
  Json envelope{{"choices", Json::array({Json{{"message", Json::object()}}})}};
  std::string content;
  bool decoded_content = false;
  Json reported_model = nullptr;
  std::optional<loom::Error> error;
  loom::net::SseParser parser;
  const auto event = [&](const loom::net::SseEvent& item) {
    if (item.is_done()) return;
    auto parsed = loom::json::parse(item.data);
    if (!parsed || !parsed->is_object()) { error = invalid("provider SSE contains invalid JSON"); return; }
    if (parsed->contains("usage")) envelope["usage"] = (*parsed)["usage"];
    if (parsed->contains("error")) envelope["error"] = (*parsed)["error"];
    if (parsed->contains("model") && (*parsed)["model"].is_string()) {
      if (!reported_model.is_null() && reported_model != (*parsed)["model"]) { error = invalid("provider SSE reports inconsistent model identities"); return; }
      reported_model = (*parsed)["model"]; envelope["model"] = reported_model;
    }
    const auto choices = parsed->value("choices", Json::array());
    if (!choices.is_array() || choices.empty()) return;
    if (!choices[0].is_object()) { error = invalid("provider SSE choice is not an object"); return; }
    const auto delta = choices[0].value("delta", Json::object());
    if (!delta.is_object()) { error = invalid("provider SSE delta is not an object"); return; }
    if (delta.contains("content") && delta["content"].is_string()) {
      decoded_content = true;
      content += delta["content"].get<std::string>();
    }
    if (choices[0].contains("finish_reason")) envelope["choices"][0]["finish_reason"] = choices[0]["finish_reason"];
  };
  parser.feed(bytes, event); parser.finish(event);
  if (error) return *error;
  if (decoded_content) envelope["choices"][0]["message"]["content"] = content;
  return envelope;
}

inline loom::Result<Json> execute(LoomContext* context, Prepared& prepared, const Json& body) {
  LOOM_TRY_ASSIGN(auto identity, required_string(body, "request_identity_hash"));
  if (identity != prepared.identity) return loom::Error(loom::Errc::Conflict, "prepared request identity changed; prepare and review again");
  if (prepared.attempted) { auto result = view(prepared); result["executed"] = false; result["replayed"] = true; return result; }
  if (prepared.status == "discarded") return loom::Error(loom::Errc::Conflict, "prepared request was discarded");
  if (prepared.graph_plan.is_null()) return loom::Error(loom::Errc::Unavailable, "save and select a native analysis method version with result binding before executing");
  loom::context::MethodRegistry registry(context->rt->db());
  LOOM_TRY(method_ui::refreshed_snapshot(registry, prepared.method_snapshot));
  LOOM_TRY(object_field(body, "confirmation"));
  auto& runtime = *context->rt;
  loom::chat::GraphReplyServices services{runtime.paths().root, runtime.db(), runtime.config(), runtime.secrets(),
      runtime.http(), &runtime.providers(), runtime.blobs(), runtime.provenance()};
  loom::context::ContextExecutionScope scope(Json::object());
  loom::chat::ModelUsageGuard guard(services);
  auto admitted = guard.admit(prepared.request, prepared.estimate, body.value("confirmation", Json::object()));
  if (!admitted) {
    prepared.result["usage_decisions"] = guard.decisions(); prepared.status = "admission_error";
    prepared.result["error"] = Json{{"code", std::string(loom::errc_name(admitted.error().code))}, {"message", "usage admission failed; review the current receipt"}};
    auto result = view(prepared); result["executed"] = false; return result;
  }
  prepared.result = Json{{"usage_decision", *admitted}, {"usage_decisions", guard.decisions()}};
  if (!admitted->value("dispatch_authorized", false)) {
    prepared.status = admitted->value("dispatch_status", admitted->value("status", std::string("blocked")));
    auto result = view(prepared); result["executed"] = false; return result;
  }
  // Consumed before entering transport. A lost response never creates a second attempt.
  prepared.attempted = true; prepared.status = "attempting";
  std::string bytes;
  bool truncated = false;
  int http_status = 0;
  loom::net::StreamSink sink;
  sink.on_headers = [&](int status, const loom::net::Headers&) { http_status = status; return true; };
  sink.on_data = [&](std::string_view part) {
    const auto remaining = prepared.max_response_bytes - bytes.size();
    if (part.size() > remaining) { bytes.append(part.substr(0, remaining)); truncated = true; return false; }
    bytes.append(part); return true;
  };
  loom::Result<loom::net::HttpResponse> response = loom::Error(loom::Errc::Internal, "transport outcome unknown");
  try { response = runtime.http().send(prepared.request, &sink); }
  catch (...) { response = loom::Error(loom::Errc::Internal, "transport threw after the attempt began"); }
  if (response) http_status = response->status;
  prepared.result["http_status"] = http_status;
  prepared.result["capture_truncated"] = truncated;
  prepared.result["raw_response_bytes"] = loom::utf8::is_valid(bytes) ? Json(bytes) : Json(nullptr);
  if (!loom::utf8::is_valid(bytes)) prepared.result["raw_response_base64"] = loom::base64::encode(bytes);
  prepared.result["response_hash"] = loom::Sha256::hex(bytes);
  prepared.result["transport_error"] = response ? Json(nullptr) : Json(std::string(loom::errc_name(response.error().code)));
  // First bytes get an immutable native source before decoding or accounting.
  auto source = loom::chat::retain_graph_reply_bytes(services, bytes, Json{{"channel", "analysis_http_wire"},
      {"prepared_id", prepared.id}, {"request_identity_hash", prepared.identity}, {"request_hash", prepared.preparation["request_hash"]},
      {"contract_hash", prepared.contract.hash}, {"method_ref", prepared.method_ref}, {"http_status", http_status},
      {"complete", static_cast<bool>(response) && !truncated}, {"capture_truncated", truncated},
      {"capture_kind", "http_wire"}, {"origin", "native_transport_capture"}, {"content_classification", "pending_decode"}});
  prepared.result["first_response_ref"] = source ? *source : Json{{"source_status", "unavailable"}, {"blob_hash", loom::Sha256::hex(bytes)}, {"bytes", bytes.size()}};
  auto decoded = decode(bytes, prepared.request.stream);
  const auto usage = decoded && decoded->is_object() ? decoded->value("usage", Json::object()) : Json::object();
  auto settled = guard.settle(true, bytes.size(), usage.is_object() ? usage : Json::object());
  prepared.result["usage_decisions"] = guard.decisions();
  prepared.result["accounting_status"] = settled ? "recorded_with_unknowns_preserved" : "error";
  Json output = nullptr;
  prepared.result["model_content_available"] = false;
  if (decoded) {
    output = *decoded;
    const auto choices = decoded->is_object() ? decoded->value("choices", Json::array()) : Json::array();
    if (choices.is_array() && !choices.empty() && choices[0].is_object()) {
      const auto message = choices[0].value("message", Json::object());
      if (message.is_object() && message.contains("content") && message["content"].is_string()) {
        prepared.result["model_content"] = message["content"];
        prepared.result["model_content_available"] = response && response->ok();
        auto parsed = loom::SemanticLLM::parse_response_json(message["content"].get<std::string>());
        output = parsed ? *parsed : Json(nullptr);
      }
      prepared.result["finish_reason"] = choices[0].value("finish_reason", Json(nullptr));
    }
  }
  prepared.result["output"] = output;
  prepared.result["output_validation"] = prompts::validate_output(prepared.contract, output);
  prepared.result["content_truth"] = prepared.result["model_content_available"] == true ? "unverified_model_output" : "service_or_transport_result";
  auto graph_binding = bind_graph(runtime, prepared, bytes, decoded ? *decoded : Json(nullptr));
  prepared.result["graph_binding_status"] = graph_binding ? "native_result_run_version_edges" : "error";
  if (!graph_binding) prepared.result["graph_binding_error"] = Json{{"code", std::string(loom::errc_name(graph_binding.error().code))}, {"message", graph_binding.error().message}};
  prepared.status = !source ? "storage_error" : !response || !response->ok() ? "transport_error" : truncated ? "response_truncated" :
      !decoded ? "decode_error" : !graph_binding ? "graph_binding_error" : !prepared.result["output_validation"].value("valid", false) ? "schema_rejected" : !settled ? "accounting_error" : "completed";
  auto result = view(prepared); result["executed"] = true; result["replayed"] = false; return result;
}

inline loom::Result<Json> dispatch(LoomContext* context, State& state, const Json& body) {
  if (!context || !context->rt) return invalid("ctx is NULL or shut down");
  if (!body.is_object()) return invalid("analysis request must be an object");
  LOOM_TRY_ASSIGN(auto operation, required_string(body, "operation"));
  if (operation == "catalog") {
    const auto provider = context->rt->config().get("base_url", "");
    std::vector<std::string> secrets{context->rt->secrets().get_string("api_key")};
    if (provider.is_string()) url_secrets(provider.get<std::string>(), secrets);
    return redact(Json{{"schema", "loom.analysis_catalog/1"}, {"contracts", prompts::builtin_catalog()},
      {"runtime", Json{{"model", context->rt->config().get("semantic_model", "")}, {"provider", provider}}}}, secrets);
  }
  if (operation == "resolve") {
    LOOM_TRY_ASSIGN(auto contract, effective_contract(*context->rt, body));
    std::vector<std::string> secrets{context->rt->secrets().get_string("api_key")};
    header_secrets(contract.definition["transport"].value("headers", Json::object()), secrets);
    const auto url = contract.definition["transport"].value("url", Json(nullptr));
    if (url.is_string()) url_secrets(url.get<std::string>(), secrets);
    return redact(Json{{"contract", contract.definition}, {"contract_json", redact(contract.definition, secrets).dump()},
      {"contract_hash", contract.hash}, {"credentials_redacted", true}}, secrets);
  }
  if (operation == "prepare") return prepare(context, state, body);
  LOOM_TRY_ASSIGN(auto id, required_string(body, "prepared_id"));
  std::shared_ptr<Prepared> prepared;
  { std::lock_guard lock(state.mutex); const auto found = state.prepared.find(id);
    if (found == state.prepared.end()) return loom::Error(loom::Errc::NotFound, "prepared handle unavailable; no call was retried");
    prepared = found->second;
  }
  std::lock_guard lock(prepared->mutex);
  if (operation == "inspect") return view(*prepared);
  if (operation == "discard") { prepared->status = "discarded";
    std::lock_guard state_lock(state.mutex); state.prepared.erase(id); return Json{{"prepared_id", id}, {"status", "discarded"}}; }
  if (operation == "execute") return execute(context, *prepared, body);
  return invalid("unknown analysis operation");
}
}  // namespace analysis_ui_detail

inline void register_analysis_ui_routes(httplib::Server& server, LoomContext* context) {
  auto state = std::make_shared<analysis_ui_detail::State>();
  server.Post("/api/analysis", [context, state](const httplib::Request& request, httplib::Response& response) {
    native_ui::protect(response, [&] {
      auto body = native_ui::parse(request);
      if (!body) { native_ui::fail(response, body.error()); return; }
      auto result = analysis_ui_detail::dispatch(context, *state, *body);
      if (!result) { native_ui::fail(response, result.error()); return; }
      native_ui::reply(response, *result);
    });
  });
}
}  // namespace loom_server
