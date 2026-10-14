#include "media/media_call.h"

#include <filesystem>

#include "loom/config.h"

namespace loom::media_detail {

namespace {

// A call made by hand (no data directory behind it) has no ledger to be admitted
// against and no overlays to read: it runs on built-in settings, unguarded.
struct Scratch {
  Config config;
  Secrets secrets;
  Scratch()
      : config(std::filesystem::temp_directory_path() / "loom-media-unguarded" / "config.json"),
        secrets(std::filesystem::temp_directory_path() / "loom-media-unguarded" / "secrets.json") {}
};

struct HoldState {
  Json confirmation = Json::object();
  Json pending = nullptr;
};
thread_local HoldState* g_hold = nullptr;

Json pending_of(const llm::VariantResult& r, const WireCall& call) {
  const Json& admitted = r.hold["decision"];
  Json resources = Json::object();
  if (const Json* res = json::find(admitted, "resources")) resources = *res;
  return Json{{"schema", "loom.action_usage_pending/1"}, {"stage", "media"},
              {"status", json::get_string(r.hold, "status")},
              {"operation_id", admitted.value("operation_id", Json(nullptr))},
              {"receipt_id", admitted.value("receipt_id", Json(nullptr))},
              {"baseline_key", admitted.value("baseline_key", Json(nullptr))},
              {"projected", resources}, {"decisions", r.hold.value("decisions", Json(nullptr))},
              {"media", call.action}, {"provider", call.manifest}, {"sent", false}};
}

}  // namespace

HoldScope::HoldScope(Json confirmation) : confirming_(confirmation.is_object() && !confirmation.empty()) {
  auto* st = new HoldState();
  if (confirming_) st->confirmation = std::move(confirmation);
  prev_ = g_hold;
  g_hold = st;
}
HoldScope::~HoldScope() {
  delete g_hold;
  g_hold = static_cast<HoldState*>(prev_);
}
Json HoldScope::pending() const { return g_hold ? g_hold->pending : Json(nullptr); }

Result<net::HttpResponse> send(net::HttpTransport& http, const std::shared_ptr<MediaCallContext>& ctx,
                               const WireCall& call, std::string_view label, std::size_t error_body_chars) {
  const bool guarded = ctx && !ctx->config_path.empty();
  std::unique_ptr<Scratch> scratch;
  std::unique_ptr<Config> config;
  std::unique_ptr<Secrets> secrets;
  if (guarded) {  // settings are read per call: the usage policy follows the owner's latest choice
    config = std::make_unique<Config>(ctx->config_path);
    secrets = std::make_unique<Secrets>(ctx->config_path.parent_path() / "secrets.json");
  } else {
    scratch = std::make_unique<Scratch>();
  }
  llm::Services services(guarded ? *config : scratch->config, guarded ? *secrets : scratch->secrets, http);
  if (guarded) services.data_root = ctx->config_path.parent_path();
  if (ctx) {
    std::lock_guard lk(ctx->mu);
    services.on_provenance = ctx->on_provenance;
  }

  llm::ModelRequest request;
  request.kind = call.action;  // the operation kind (guard cohort, provenance)
  request.action = call.action == "media.ocr" ? "ocr" : "transcribe";  // the catalogue action this call serves (P12 4)
  request.models = {llm::ModelSpec{call.model, "", Json::object()}};
  request.parameters = call.parameters;
  request.inputs = {call.input};
  request.wants = {"text"};
  request.referer = false;
  request.timeout_ms = call.timeout_ms;
  // The descriptor's address: origin = route base (usage cohort), the rest = path.
  const auto scheme = call.endpoint.find("://");
  const auto slash = scheme == std::string::npos ? std::string::npos : call.endpoint.find('/', scheme + 3);
  request.route.provider = call.manifest;
  request.route.adapter = call.adapter;
  request.route.base_url = slash == std::string::npos ? call.endpoint : call.endpoint.substr(0, slash);
  request.route.endpoint = slash == std::string::npos ? "" : call.endpoint.substr(slash);
  request.route.credential = call.credential;
  request.route.headers = call.headers;
  if (guarded && g_hold && g_hold->confirmation.is_object() && !g_hold->confirmation.empty()) {
    request.usage.confirmation = std::move(g_hold->confirmation);  // answers this one request, once
    g_hold->confirmation = Json::object();
  }
  if (!guarded) request.usage.gate = llm::make_admitted_gate();

  auto outcome = llm::use_model(services, std::move(request));
  const std::string prefix = std::string(label) + ": ";
  if (!outcome) return Error(outcome.error().code, prefix + outcome.error().message);
  const llm::VariantResult& r = outcome->last();
  switch (r.state) {
    case llm::State::ParseError:  // the family's text could not be read: the provider words its own parse error
    case llm::State::Ok: {
      net::HttpResponse resp;
      resp.status = r.http_status;
      resp.body = r.body;
      return resp;
    }
    case llm::State::Held:
      if (g_hold) g_hold->pending = pending_of(r, call);
      return Error(Errc::Paused, prefix + "held by the usage guard: " + json::get_string(r.hold, "status"));
    case llm::State::HttpError: {
      std::string msg = "HTTP " + std::to_string(r.http_status);
      if (!r.body.empty()) msg += ": " + r.body.substr(0, error_body_chars);
      return Error(Errc::Http, prefix + msg);
    }
    case llm::State::TransportError:
      return Error(Errc::Network, prefix + (r.error ? r.error->message : std::string("transport failed")));
    case llm::State::Cancelled:
      return Error(Errc::Cancelled, prefix + "cancelled");
    default:
      return Error(r.error ? r.error->code : Errc::Unavailable, prefix + (r.error ? r.error->message : std::string("unavailable")));
  }
}

}  // namespace loom::media_detail
