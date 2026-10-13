// The use-model action: route resolution (settings -> manifest -> adapter),
// request preparation, guarded execution and provenance.  See use_model.h.
#include "loom/use_model.h"

#include <algorithm>
#include <limits>

#include "llm/shared_gate.h"
#include "loom/config.h"
#include "loom/log.h"
#include "loom/providers.h"
#include "loom/usage_policy.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::llm {

namespace {

constexpr std::string_view kDefaultProvider = "openrouter";  // preset of config "llm_provider"

std::string rstrip_slash(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

std::string cfg_text(const Config& cfg, std::string_view key) {
  Json v = cfg.get(key);
  return v.is_string() ? v.get<std::string>() : std::string();
}

// Builtin manifests plus the user's "provider_manifests" setting (an array in
// the ProviderManifest shape), resolved at call time so settings edits apply.
Result<std::optional<ProviderManifest>> find_manifest(const Services& services, const std::string& id) {
  if (services.providers) return services.providers->get(id);
  ProviderRegistry local(services.config, services.secrets);
  LOOM_TRY(local.load_builtin());
  Json extra = services.config.get("provider_manifests");
  if (extra.is_array()) LOOM_TRY(local.load(extra));
  return local.get(id);
}

std::string_view cut_query(std::string_view url) { return url.substr(0, url.find('?')); }

Json merge_parameters(const Json& shared, const Json& specific) {
  Json out = shared.is_object() ? shared : Json::object();
  if (specific.is_object())
    for (auto it = specific.begin(); it != specific.end(); ++it) out[it.key()] = it.value();
  return out;
}

}  // namespace

std::string_view to_string(Operation op) noexcept {
  switch (op) {
    case Operation::Complete: return "complete";
    case Operation::BatchSubmit: return "batch_submit";
    case Operation::BatchStatus: return "batch_status";
    case Operation::BatchResults: return "batch_results";
  }
  return "complete";
}

std::string_view to_string(State s) noexcept {
  switch (s) {
    case State::Ok: return "ok";
    case State::Held: return "held";
    case State::GateError: return "gate_error";
    case State::Unavailable: return "unavailable";
    case State::TransportError: return "transport_error";
    case State::HttpError: return "http_error";
    case State::ParseError: return "parse_error";
    case State::Cancelled: return "cancelled";
  }
  return "unavailable";
}

Json Route::describe() const {
  Json headers = Json::array();
  for (const auto& [name, _] : default_headers) headers.push_back(name);
  return Json{{"provider", provider_id}, {"adapter", adapter}, {"base_url", base_url}, {"endpoint", endpoint},
              {"auth_secret", auth_secret}, {"auth_scheme", auth_scheme}, {"default_header_names", headers}};
}

const VariantResult* ModelOutcome::first_ok() const noexcept {
  for (const auto& r : results)
    if (r.ok()) return &r;
  return nullptr;
}

// ── Routing ─────────────────────────────────────────────────────────
Result<Route> resolve_route(const Services& services, const RouteSelector& selector) {
  std::string id = selector.provider;
  if (id.empty()) id = cfg_text(services.config, "llm_provider");
  if (id.empty()) id = std::string(kDefaultProvider);
  LOOM_TRY_ASSIGN(auto found, find_manifest(services, id));
  if (!found) return Error(Errc::NotFound, "unknown model provider '" + id + "'");
  const ProviderManifest& m = *found;

  Route r;
  r.provider_id = m.id;
  r.adapter = selector.adapter;
  if (r.adapter.empty()) r.adapter = json::get_string(m.metadata, "adapter", "openai_chat");
  r.base_url = selector.base_url;
  if (r.base_url.empty()) {
    // A provider may name the setting that holds its address (the settings
    // provider uses "base_url"); the manifest's own address is the fallback.
    const std::string setting = json::get_string(m.metadata, "base_url_setting");
    if (!setting.empty()) r.base_url = cfg_text(services.config, setting);
    if (r.base_url.empty()) r.base_url = m.base_url;
  }
  r.base_url = rstrip_slash(r.base_url);
  r.endpoint = selector.endpoint;
  r.auth_secret = selector.secret.empty() ? m.auth_secret : selector.secret;
  r.auth_scheme = m.auth_scheme;
  r.content_type_header = selector.content_type_header;
  r.metadata = m.metadata;
  if (selector.headers) {
    r.default_headers = *selector.headers;
  } else {
    for (auto it = m.default_headers.begin(); it != m.default_headers.end(); ++it) {
      if (!it->is_string()) return Error(Errc::InvalidArgument, "provider default header must be text");
      r.default_headers.emplace_back(it.key(), it->get<std::string>());
    }
  }
  if (!make_adapter(r.adapter)) return Error(Errc::NotFound, "unknown model adapter '" + r.adapter + "'");
  if (r.base_url.empty() && r.endpoint.find("://") == std::string::npos)
    return Error(Errc::Unavailable, "no address configured for model provider '" + r.provider_id + "'");
  if (!r.auth_secret.empty()) {
    r.credential = services.secrets.get_string(r.auth_secret);
    if (r.credential.empty())
      return Error(Errc::Auth, "no credential '" + r.auth_secret + "' stored for model provider '" + r.provider_id + "'");
  }
  return r;
}

bool route_available(const Services& services, const RouteSelector& selector) {
  return static_cast<bool>(resolve_route(services, selector));
}

Status apply_auth(const Route& route, net::HttpRequest& request) {
  const std::string& scheme = route.auth_scheme;
  if (scheme.empty() || scheme == "none") return ok_status();
  if (scheme == "bearer") request.headers.emplace_back("Authorization", "Bearer " + route.credential);
  else if (scheme == "token") request.headers.emplace_back("Authorization", "token " + route.credential);
  else if (scheme == "x-api-key") request.headers.emplace_back("x-api-key", route.credential);
  else if (scheme == "query:key") request.url = net::with_query(request.url, {{"key", route.credential}});
  else return Error(Errc::Unsupported, "auth scheme '" + scheme + "' is not supported for model calls");
  return ok_status();
}

// ── Preparation ─────────────────────────────────────────────────────
Result<PreparedCall> prepare_call(const Services& services, const ModelRequest& request, std::size_t variant) {
  if (variant >= request.models.size()) return Error(Errc::InvalidArgument, "model variant out of range");
  const ModelSpec& model = request.models[variant];
  PreparedCall call;
  call.index = variant;

  RouteSelector selector = request.route;
  if (!model.provider.empty()) selector.provider = model.provider;
  LOOM_TRY_ASSIGN(call.route, resolve_route(services, selector));
  auto adapter = make_adapter(call.route.adapter);
  if (!adapter) return Error(Errc::NotFound, "unknown model adapter '" + call.route.adapter + "'");

  CallSpec& spec = call.spec;
  spec.model = model.model;
  spec.parameters = merge_parameters(request.parameters, model.parameters);
  spec.stream = request.stream;
  spec.timeout_ms = request.timeout_ms;
  spec.title = request.title;
  spec.referer = request.referer;
  spec.extra_headers = request.extra_headers;
  spec.batch_requests = request.batch_requests;
  spec.target = request.target;
  spec.messages = request.messages.is_array() ? request.messages : Json::array();
  if (!request.context.is_null()) {
    if (!services.resolve_context)
      return Error(Errc::InvalidArgument, "a context reference needs Services::resolve_context");
    LOOM_TRY_ASSIGN(Json resolved, services.resolve_context(request.context));
    if (!resolved.is_array()) return Error(Errc::InvalidArgument, "context resolved to a non-array");
    for (const auto& m : resolved) spec.messages.push_back(m);
  }
  if (!request.prompt.empty()) spec.messages.push_back(Json{{"role", "user"}, {"content", request.prompt}});

  LOOM_TRY_ASSIGN(call.request, adapter->build(call.route, request.operation, spec));

  call.provenance = Json{
      {"schema", kProvenanceSchema},
      {"kind", request.kind},
      {"variant", variant},
      {"mode", request.mode},
      {"operation", to_string(request.operation)},
      {"requested_model", spec.model},
      {"provider", call.route.provider_id},
      {"adapter", call.route.adapter},
      {"endpoint", std::string(cut_query(call.request.url))},
      {"stream", spec.stream},
      {"parameters", spec.parameters},
      {"parameters_sha256", Sha256::hex(json::canonical(spec.parameters))},
      {"messages_sha256", Sha256::hex(json::dump(spec.messages))},
      {"request_sha256", Sha256::hex(call.request.body)}};
  return call;
}

// ── Gate ────────────────────────────────────────────────────────────
std::shared_ptr<UsageGate> make_usage_gate(const Services& services, const UsageSpec& spec) {
  if (spec.gate) return spec.gate;
  std::filesystem::path root = services.data_root;
  if (root.empty()) root = services.config.path().parent_path();
  return std::make_shared<SharedUsageGate>(root, services.config, spec.require_scope,
                                           spec.estimate.is_object() ? spec.estimate : Json::object(),
                                           spec.confirmation, spec.declared);
}

namespace {

// Settles a dispatched gate exactly once, also when a callback throws.
struct Settlement {
  std::shared_ptr<UsageGate> gate;
  bool dispatched = false;
  GateSettlement data;
  void settle() {
    if (!dispatched || !gate) return;
    dispatched = false;
    gate->settle(data);
  }
  ~Settlement() {
    try { settle(); } catch (...) {}
  }
};

Json derived_estimate(const Services& services, const ModelRequest& request, const PreparedCall& call) {
  const std::string model = call.spec.model;
  Json estimate{{"baseline_key", usage_baseline_key(services.config, request.kind, call.route.base_url, model)},
                {"resources", Json::object()},
                {"instrument", "loom.model." + request.kind},
                {"model", model},
                {"endpoint", std::string(cut_query(call.request.url))}};
  // A confirmed re-send answers the operation that was held.
  const std::string operation = json::get_string(request.usage.confirmation, "operation_id");
  if (!operation.empty()) estimate["operation_id"] = operation;
  return estimate;
}

void finalize_provenance(VariantResult& r, const PreparedCall& call) {
  Json p = call.provenance;
  p["status"] = to_string(r.state);
  p["sent"] = r.sent;
  p["http_status"] = r.http_status;
  p["reported_model"] = r.reported_model.empty() ? Json(nullptr) : Json(r.reported_model);
  p["usage"] = r.usage;
  p["response_bytes"] = r.received_bytes;
  if (!r.body.empty()) p["response_sha256"] = Sha256::hex(r.body);
  if (!r.text.empty()) p["output_sha256"] = Sha256::hex(r.text);
  if (r.hold.is_object() && r.hold["decision"].is_object())
    p["operation_id"] = r.hold["decision"].value("operation_id", Json(nullptr));
  p["recorded_at"] = timeutil::utc_now_iso();
  r.provenance = std::move(p);
}

}  // namespace

// ── Execution ───────────────────────────────────────────────────────
VariantResult execute_call(const Services& services, const ModelRequest& request, PreparedCall& call) {
  VariantResult r;
  r.index = call.index;
  r.model = call.spec.model;
  r.provider = call.route.provider_id;
  r.adapter = call.route.adapter;

  auto adapter = make_adapter(call.route.adapter);
  if (!adapter) {
    r.state = State::Unavailable;
    r.error = Error(Errc::NotFound, "unknown model adapter '" + call.route.adapter + "'");
    finalize_provenance(r, call);
    return r;
  }
  const Operation op = request.operation;
  const net::HttpRequest& req = call.request;

  Settlement settlement;
  if (request.usage.guard || request.usage.gate) {
    UsageSpec usage = request.usage;
    if (!usage.gate && !usage.estimate.is_object()) usage.estimate = derived_estimate(services, request, call);
    settlement.gate = make_usage_gate(services, usage);
    auto admitted = settlement.gate->admit(req, call.provenance);
    if (!admitted) {
      r.state = State::GateError;
      r.error = admitted.error();
      finalize_provenance(r, call);
      return r;
    }
    if (!admitted->dispatch) {
      r.state = State::Held;
      r.hold = Json{{"status", admitted->status}, {"decision", admitted->decision}, {"decisions", admitted->decisions}};
      finalize_provenance(r, call);
      return r;
    }
    settlement.dispatched = true;
  }
  if (request.hooks.on_dispatch) request.hooks.on_dispatch();

  const CancelToken* cancel = request.cancel;
  auto cancelled = [&] { return cancel && cancel->cancelled(); };
  const bool decode_stream = request.stream && op == Operation::Complete;
  const bool use_sink = decode_stream || request.capture;

  Result<net::HttpResponse> resp = Error(Errc::Internal, "transport not attempted");
  r.sent = true;
  settlement.data.attempted = true;
  int status = 0;
  bool aborted = false;
  std::unique_ptr<StreamDecoder> decoder;
  Parsed buffered;

  auto send = [&](const net::StreamSink* sink) {
    if (request.contain_exceptions) {
      try { resp = services.http.send(req, sink, cancel); }
      catch (...) { resp = *request.contain_exceptions; }
    } else {
      resp = services.http.send(req, sink, cancel);
    }
  };

  if (use_sink) {
    if (decode_stream) decoder = adapter->stream_decoder(request.hooks, cancel);
    net::StreamSink sink;
    sink.on_headers = [&](int s, const net::Headers&) {
      status = s;
      return true;
    };
    sink.on_data = [&](std::string_view chunk) -> bool {
      r.received_bytes += std::min(chunk.size(), std::numeric_limits<std::size_t>::max() - r.received_bytes);
      if (request.hooks.on_raw) request.hooks.on_raw(chunk);
      if (cancelled()) {
        aborted = true;
        return false;
      }
      if (!decoder || !adapter->success(op, status)) {
        // Captured or error bytes: keep them, up to the caller's ceiling.
        if (request.max_response_bytes) {
          const std::size_t room = *request.max_response_bytes - std::min(*request.max_response_bytes, r.body.size());
          r.body.append(chunk.substr(0, room));
          if (chunk.size() > room) {
            r.response_limited = true;
            return false;
          }
        } else {
          r.body.append(chunk);
        }
        return true;
      }
      decoder->feed(chunk);
      if (cancelled()) {
        aborted = true;
        return false;
      }
      return true;
    };
    send(&sink);
    if (resp) status = resp->status;
  } else {
    send(nullptr);
    if (resp) {
      status = resp->status;
      r.body = resp->body;
      r.received_bytes = resp->body.size();
    }
  }
  r.http_status = status;

  if (!resp) r.error = resp.error();
  const bool was_cancelled = aborted || cancelled() || (!resp && resp.error().code == Errc::Cancelled);
  if (r.response_limited) {
    r.state = State::HttpError;  // the caller's ceiling stopped the read; error carries the aborted transfer
  } else if (was_cancelled) {
    r.state = State::Cancelled;
  } else if (!resp) {
    r.state = State::TransportError;
  } else if (!adapter->success(op, status)) {
    r.state = State::HttpError;
  } else if (decoder) {
    decoder->finish();
    r.state = cancelled() ? State::Cancelled : State::Ok;
  } else if (op == Operation::Complete && !request.capture) {
    auto parsed = adapter->parse(r.body);
    if (!parsed) {
      r.state = State::ParseError;
      r.error = parsed.error();
    } else {
      buffered = std::move(*parsed);
      r.state = State::Ok;
    }
  } else {
    r.state = State::Ok;
    if (op == Operation::Complete) {
      if (auto parsed = adapter->parse(r.body)) buffered = std::move(*parsed);
    }
  }

  const Parsed& out = decoder ? decoder->parsed() : buffered;
  r.text = out.text;
  r.reasoning = out.reasoning;
  r.reported_model = out.reported_model;
  r.usage = out.usage;
  r.issues = out.issues;
  settlement.data.received_bytes = r.received_bytes;
  settlement.data.usage = r.usage;
  settlement.settle();
  finalize_provenance(r, call);
  return r;
}

Result<ModelOutcome> use_model(const Services& services, ModelRequest request) {
  if (request.models.empty()) return Error(Errc::InvalidArgument, "use_model needs at least one model");
  if (request.mode != "single" && request.mode != "sequential" && request.mode != "first_success")
    return Error(Errc::InvalidArgument, "mode must be single, sequential or first_success");
  if (request.mode == "single" && request.models.size() != 1)
    return Error(Errc::InvalidArgument, "mode single takes exactly one model; use sequential or first_success for variants");
  for (const auto& m : request.models)
    if (m.model.empty() && request.operation == Operation::Complete)
      return Error(Errc::InvalidArgument, "a model spec needs a model id");

  ModelOutcome outcome;
  outcome.mode = request.mode;
  for (std::size_t i = 0; i < request.models.size(); ++i) {
    // A caller-supplied estimate/confirmation answers exactly one operation:
    // the first variant.  Later variants get their own derived estimate.
    ModelRequest per = request;
    if (i > 0) {
      per.usage.estimate = nullptr;
      per.usage.confirmation = Json::object();
      per.usage.gate = nullptr;
    }
    VariantResult result;
    auto prepared = prepare_call(services, per, i);
    if (!prepared) {
      result.index = i;
      result.state = State::Unavailable;
      result.model = request.models[i].model;
      result.error = prepared.error();
      result.provenance = Json{{"schema", kProvenanceSchema}, {"kind", request.kind}, {"variant", i},
                               {"requested_model", request.models[i].model}, {"status", "unavailable"},
                               {"error", std::string(errc_name(prepared.error().code))}};
    } else {
      result = execute_call(services, per, *prepared);
    }
    if (services.on_provenance) {
      try { services.on_provenance(result.provenance); }
      catch (...) { log::warn("loom.use_model", "provenance callback threw"); }
    }
    const State state = result.state;
    const bool success = result.ok();
    outcome.results.push_back(std::move(result));
    // Never step past an unanswered guard or a user cancellation.
    if (state == State::Held || state == State::GateError || state == State::Cancelled) break;
    if (request.mode == "first_success" && success) break;
  }
  return outcome;
}

}  // namespace loom::llm
