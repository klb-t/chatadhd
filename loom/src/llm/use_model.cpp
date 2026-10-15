// The use-model action: route resolution (settings -> manifest -> adapter),
// request preparation, guarded execution and provenance.  See use_model.h.
#include "loom/use_model.h"

#include <algorithm>
#include <limits>
#include <set>

#include "llm/shared_gate.h"
#include "loom/config.h"
#include "loom/log.h"
#include "loom/providers.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_llm.h"
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

bool contains(const std::vector<std::string>& v, std::string_view s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

std::string list_text(const std::vector<std::string>& v) {
  if (v.empty()) return "none";
  std::string out;
  for (const auto& s : v) out += (out.empty() ? "" : ", ") + s;
  return out;
}

std::vector<std::string> strings_of(const Json& j) {
  std::vector<std::string> out;
  if (j.is_array())
    for (const auto& e : j)
      if (e.is_string()) out.push_back(e.get<std::string>());
  return out;
}

// A data layer replaces exactly the fields it names.
void layer_capabilities(Capabilities& caps, const Json* data) {
  if (!data || !data->is_object()) return;
  if (const Json* v = json::find(*data, "accepts")) caps.accepts = strings_of(*v);
  if (const Json* v = json::find(*data, "produces")) caps.produces = strings_of(*v);
  if (const Json* v = json::find(*data, "modes")) caps.modes = strings_of(*v);
}

}  // namespace

std::string_view to_string(Operation op) noexcept {
  switch (op) {
    case Operation::Complete: return "complete";
    case Operation::BatchSubmit: return "batch_submit";
    case Operation::BatchStatus: return "batch_status";
    case Operation::BatchResults: return "batch_results";
    case Operation::ListModels: return "list_models";
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
  const bool given_credential = !selector.credential.empty();
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
  if (given_credential) {
    r.credential = selector.credential;
  } else if (!r.auth_secret.empty()) {
    r.credential = services.secrets.get_string(r.auth_secret);
    if (r.credential.empty() && selector.require_credential)
      return Error(Errc::Auth, "no credential '" + r.auth_secret + "' stored for model provider '" + r.provider_id + "'");
  }
  return r;
}

bool route_available(const Services& services, const RouteSelector& selector) {
  return static_cast<bool>(resolve_route(services, selector));
}

std::string provider_for_address(const Services& services, std::string_view resource,
                                 std::string_view capability, std::string_view address, bool any_if_unmatched) {
  std::vector<ProviderManifest> manifests;
  if (services.providers) {
    manifests = services.providers->all();
  } else {
    ProviderRegistry local(services.config, services.secrets);
    if (!local.load_builtin()) return {};
    Json extra = services.config.get("provider_manifests");
    if (extra.is_array() && !local.load(extra)) return {};
    manifests = local.all();
  }
  for (const auto& m : manifests) {
    const bool offers = std::any_of(m.capabilities.begin(), m.capabilities.end(), [&](const Capability& c) {
      return c.resource == resource && c.name == capability;
    });
    if (!offers) continue;
    auto url = net::parse_url(m.base_url);
    if (url && !url->host.empty() && address.find(url->host) != std::string_view::npos) return m.id;
  }
  if (any_if_unmatched) {
    for (const auto& m : manifests)
      if (std::any_of(m.capabilities.begin(), m.capabilities.end(),
                      [&](const Capability& c) { return c.resource == resource && c.name == capability; }))
        return m.id;
  }
  return {};
}

Result<net::HttpRequest> describe_request(const Services& services, const RouteSelector& selector, std::string body) {
  LOOM_TRY_ASSIGN(Route route, resolve_route(services, selector));
  auto adapter = make_adapter(route.adapter);
  if (!adapter) return Error(Errc::NotFound, "unknown model adapter '" + route.adapter + "'");
  CallSpec spec;
  LOOM_TRY_ASSIGN(net::HttpRequest built, adapter->build(route, Operation::Complete, spec));
  net::HttpRequest described;
  described.method = "POST";
  described.url = std::move(built.url);
  described.body = std::move(body);
  return described;
}

Status apply_auth(const Route& route, net::HttpRequest& request) {
  const std::string& scheme = route.auth_scheme;
  if (scheme.empty() || scheme == "none") return ok_status();
  // "form:<field>": the key travels in the request body, which the adapter of the
  // family composes (it is the only code that knows the form).
  if (scheme.rfind("form:", 0) == 0) return ok_status();
  // A route resolved without requiring its credential (an analysis preview to
  // a local endpoint) sends no authentication rather than an empty one.
  if (route.credential.empty()) return ok_status();
  if (scheme == "bearer") request.headers.emplace_back("Authorization", "Bearer " + route.credential);
  else if (scheme == "token") request.headers.emplace_back("Authorization", "token " + route.credential);
  else if (scheme == "x-api-key") request.headers.emplace_back("x-api-key", route.credential);
  else if (scheme == "query:key") request.url = net::with_query(request.url, {{"key", route.credential}});
  else return Error(Errc::Unsupported, "auth scheme '" + scheme + "' is not supported for model calls");
  return ok_status();
}

// ── Capabilities and actions as data ──────────────────────────────
Result<Json> action_catalogue(const Services& services) {
  std::filesystem::path root = services.data_root;
  if (root.empty()) root = services.config.path().parent_path();
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("actions", root));
  const Json* actions = json::find(profile.values(), "actions");
  return actions && actions->is_object() ? *actions : Json::object();
}

Result<Capabilities> effective_capabilities(const Services& services, const Route& route, const ModelSpec& model) {
  auto adapter = make_adapter(route.adapter);
  if (!adapter) return Error(Errc::NotFound, "unknown model adapter '" + route.adapter + "'");
  Capabilities caps = adapter->capabilities();
  layer_capabilities(caps, json::find(route.metadata, "capabilities"));
  const Json per_model = services.config.get("model_capabilities");
  if (per_model.is_object()) layer_capabilities(caps, json::find(per_model, model.model));
  layer_capabilities(caps, &model.capabilities);
  return caps;
}

namespace {

Error unsupported(std::string why) { return Error(Errc::Unsupported, std::move(why)); }

// Rejects a request its model or workflow cannot serve, with the reason, before
// any byte is sent.  Nothing is substituted silently.
Status check_request(const Services& services, const ModelRequest& request, const ModelSpec& model, const Route& route,
                     const Adapter& adapter, const CallSpec& spec, const std::set<std::string>& needed) {
  const bool spends = request.operation == Operation::Complete || request.operation == Operation::BatchSubmit;
  if (spends) {
    LOOM_TRY_ASSIGN(Capabilities caps, effective_capabilities(services, route, model));
    const std::string who = "model '" + model.model + "' (adapter " + route.adapter + ")";
    for (const auto& kind : needed)
      if (!contains(caps.accepts, kind))
        return unsupported(who + " does not accept " + kind + " input; it accepts: " + list_text(caps.accepts));
    for (const auto& kind : request.wants)
      if (!contains(caps.produces, kind))
        return unsupported(who + " cannot produce " + kind + "; it produces: " + list_text(caps.produces));
    if (request.stream && !contains(caps.modes, "stream"))
      return unsupported(who + " has no stream mode; modes: " + list_text(caps.modes));
    if (request.operation == Operation::BatchSubmit && !contains(caps.modes, "batch"))
      return unsupported(who + " has no batch mode; modes: " + list_text(caps.modes));
    if (adapter.needs_output_store(spec) && !services.outputs)
      return unsupported(who + " returns media bytes but no output store is configured (Services::outputs)");
  }
  if (!request.action.empty()) {
    const std::string& id = request.action;
    LOOM_TRY_ASSIGN(Json catalogue, action_catalogue(services));
    const Json* a = json::find(catalogue, id);
    if (!a) return unsupported("unknown action '" + id + "'; declare it in the actions profile");
    const std::string status = json::get_string(*a, "status", "planned");
    if (status != "implemented") return unsupported("action '" + id + "' is " + status + ", not implemented");
    // A model call serves an action that is use_model itself or a composite
    // with a use_model step; any other action is not a model call.
    const std::string kind = json::get_string(*a, "kind");
    bool calls_model = id == "use_model";
    if (kind == "composite")
      if (const Json* steps = json::find(*a, "steps"); steps && steps->is_array())
        for (const auto& step : *steps)
          if (json::get_string(step, "action") == "use_model") calls_model = true;
    if (!calls_model) return unsupported("action '" + id + "' (" + kind + ") has no use_model step; it is not a model call");
    if (spends) {
      const auto inputs = strings_of(a->value("inputs", Json::array()));
      const auto outputs = strings_of(a->value("outputs", Json::array()));
      for (const auto& need : needed)
        if (!inputs.empty() && !contains(inputs, need))
          return unsupported("action '" + id + "' does not take " + need + " input; it takes: " + list_text(inputs));
      for (const auto& kind_wanted : request.wants)
        if (!outputs.empty() && !contains(outputs, kind_wanted))
          return unsupported("action '" + id + "' does not return " + kind_wanted + "; it returns: " + list_text(outputs));
    }
  }
  return ok_status();
}

}  // namespace

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
  spec.body_bytes = request.body_bytes;
  spec.wire_headers = request.wire_headers;
  // The body as a description: given, or parsed from the exact bytes.
  Json described_body = request.body;
  if (!described_body.is_object() && !request.body_bytes.empty()) {
    auto parsed = json::parse(request.body_bytes);
    if (parsed && parsed->is_object()) described_body = std::move(*parsed);
  }
  if (!request.context.is_null()) {
    if (!services.resolve_context)
      return Error(Errc::InvalidArgument, "a context reference needs Services::resolve_context");
    LOOM_TRY_ASSIGN(Json resolved, services.resolve_context(request.context));
    if (!resolved.is_array()) return Error(Errc::InvalidArgument, "context resolved to a non-array");
    for (const auto& m : resolved) spec.messages.push_back(m);
  }
  std::set<std::string> needed;
  for (const auto& part : request.inputs) {
    if (!known_part_kind(part.kind)) return Error(Errc::InvalidArgument, "unknown input part kind '" + part.kind + "'");
    needed.insert(part.kind);
    if (part.kind == "text") spec.messages.push_back(Json{{"role", "user"}, {"content", part.text}});
    else if (part.kind == "json") spec.messages.push_back(Json{{"role", "user"}, {"content", json::dump(part.value)}});
    else spec.parts.push_back(part);
  }
  if (!request.prompt.empty()) spec.messages.push_back(Json{{"role", "user"}, {"content", request.prompt}});
  if (!spec.messages.empty() || described_body.is_object() || !request.batch_requests.empty()) needed.insert("text");
  if (described_body.is_object()) {
    // Described for provenance only; the adapter sends the body as given.
    spec.body = described_body;
    spec.parameters = Json::object();
    for (auto it = described_body.begin(); it != described_body.end(); ++it) {
      if (it.key() == "model") { if (spec.model.empty() && it->is_string()) spec.model = it->get<std::string>(); }
      else if (it.key() == "messages") spec.messages = it.value();
      else if (it.key() == "input") {
        // The input of an embeddings body: described as user messages (hashed
        // in the record), never copied into the parameters.
        if (spec.messages.empty()) {
          const auto as_message = [](const Json& v) {
            return Json{{"role", "user"}, {"content", v.is_string() ? v : Json(json::dump(v))}};
          };
          if (it->is_array()) for (const auto& e : *it) spec.messages.push_back(as_message(e));
          else spec.messages.push_back(as_message(*it));
        }
      }
      else spec.parameters[it.key()] = it.value();
    }
    if (request.stream) spec.stream = true;
  }

  LOOM_TRY(check_request(services, request, model, call.route, *adapter, spec, needed));
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
      {"request_sha256", Sha256::hex(call.request.body)},
      {"action", request.action.empty() ? Json(nullptr) : Json(request.action)},
      {"input_kinds", Json(std::vector<std::string>(needed.begin(), needed.end()))},
      {"wants", request.wants}};
  return call;
}

// ── Gate ────────────────────────────────────────────────────────────
std::shared_ptr<UsageGate> make_usage_gate(const Services& services, const UsageSpec& spec) {
  if (spec.gate) return spec.gate;
  std::filesystem::path root = services.data_root;
  if (root.empty()) root = services.config.path().parent_path();
  return std::make_shared<SharedUsageGate>(root, services.config, spec.require_scope,
                                           spec.estimate.is_object() ? spec.estimate : Json::object(),
                                           spec.confirmation);
}

namespace {

class FunctionGate final : public UsageGate {
 public:
  FunctionGate(std::function<Result<GateDecision>(const net::HttpRequest&, const Json&)> admit,
               std::function<void(const GateSettlement&)> settle)
      : admit_(std::move(admit)), settle_(std::move(settle)) {}
  Result<GateDecision> admit(const net::HttpRequest& request, const Json& provenance) override {
    return admit_(request, provenance);
  }
  void settle(const GateSettlement& settlement) override {
    if (settle_) settle_(settlement);
  }

 private:
  std::function<Result<GateDecision>(const net::HttpRequest&, const Json&)> admit_;
  std::function<void(const GateSettlement&)> settle_;
};

}  // namespace

std::shared_ptr<UsageGate> make_function_gate(
    std::function<Result<GateDecision>(const net::HttpRequest&, const Json&)> admit,
    std::function<void(const GateSettlement&)> settle) {
  return std::make_shared<FunctionGate>(std::move(admit), std::move(settle));
}

std::shared_ptr<UsageGate> make_admitted_gate() {
  return make_function_gate([](const net::HttpRequest&, const Json&) -> Result<GateDecision> {
    GateDecision decision;
    decision.dispatch = true;
    decision.status = "admitted_by_caller";
    return decision;
  });
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
                {"endpoint", std::string(cut_query(call.request.url))},
                {"model_use", call.provenance}};
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
  if (!r.outputs.empty()) {
    Json outs = Json::array();
    for (const auto& part : r.outputs) {
      Json o = part.to_json();
      o.erase("text");  // the text itself is in the result; the record keeps references and hashes
      if (part.kind == "embedding" && part.value.is_array())
        o["value"] = Json{{"dimensions", part.value.size()}, {"sha256", Sha256::hex(json::canonical(part.value))}};
      outs.push_back(std::move(o));
    }
    p["outputs"] = std::move(outs);
  }
  if (!r.operation_id.empty()) p["operation_id"] = r.operation_id;
  else if (r.hold.is_object() && r.hold["decision"].is_object())
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
    if (admitted->decision.is_object()) r.operation_id = json::get_string(admitted->decision, "operation_id");
    if (const Json* notices = json::find(admitted->decision, "notices"); notices && notices->is_array() && !notices->empty()) r.notices = *notices;
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
        if (!request.retain_body && adapter->success(op, status)) return true;
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
    if (request.contain_exceptions) {
      try { decoder->finish(); }
      catch (...) {
        r.issues.emplace_back("callback_exception");
        if (request.hooks.on_issue) request.hooks.on_issue("callback_exception");
      }
    } else {
      decoder->finish();
    }
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
      else r.parse_error = parsed.error();
    }
  }

  const Parsed& out = decoder ? decoder->parsed() : buffered;
  r.text = out.text;
  r.reasoning = out.reasoning;
  r.reported_model = out.reported_model;
  r.usage = out.usage;
  r.tool_calls = out.tool_calls;
  r.issues = out.issues;
  if (!out.text.empty()) {
    Part text;
    text.kind = "text";
    text.text = out.text;
    r.outputs.push_back(std::move(text));
  }
  // Structured output: the text is the JSON document (code fences tolerated).
  if (r.state == State::Ok && request.operation == Operation::Complete && !out.text.empty() &&
      std::find(request.wants.begin(), request.wants.end(), "json") != request.wants.end()) {
    if (auto parsed = SemanticLLM::parse_response_json(out.text)) {
      Part structured;
      structured.kind = "json";
      structured.value = std::move(*parsed);
      r.outputs.push_back(std::move(structured));
    } else {
      r.state = State::ParseError;
      r.error = Error(Errc::Parse, "the model output is not valid JSON");
    }
  }
  for (Part part : out.outputs) {
    if (!part.bytes.empty()) {
      if (!services.outputs) {
        r.state = State::ParseError;
        r.error = Error(Errc::Unavailable, "the model returned media bytes but no output store is configured");
        break;
      }
      Json where = call.provenance;
      where["output_kind"] = part.kind;
      auto stored = services.outputs->store(part.bytes, part.mime, where);
      if (!stored) {
        r.state = State::ParseError;
        r.error = Error(Errc::Io, "storing the model output failed: " + stored.error().message);
        break;
      }
      part.blob_hash = json::get_string(*stored, "blob_hash");
      part.node_id = json::get_string(*stored, "node_id");
      part.bytes.clear();
    }
    r.outputs.push_back(std::move(part));
  }
  settlement.data.received_bytes = r.received_bytes;
  settlement.data.usage = r.usage;
  settlement.data.declared = request.usage.declared.is_object() ? request.usage.declared : Json::object();
  if (r.state == State::Ok && request.usage.declared_on_success.is_object())
    for (auto it = request.usage.declared_on_success.begin(); it != request.usage.declared_on_success.end(); ++it)
      settlement.data.declared[it.key()] = it.value();
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
