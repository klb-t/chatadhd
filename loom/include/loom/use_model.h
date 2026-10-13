// loom/use_model.h — the one action "use model" (P7 wave 1).
//
// Owner's words: "Use the chosen model (or several models in turn, when we
// vary them) with these parameters, this context and this prompt."  Every
// place that calls a language model builds a ModelRequest and calls
// llm::use_model(); nothing else in Loom knows an endpoint path, an auth
// header, a provider-specific secret name or an HTTP-Referer.
//
//   models      one ModelSpec, or an ordered list of variants.  `mode`:
//                 single        exactly one model
//                 sequential    every variant runs; each result is returned
//                 first_success variants run in order until one succeeds
//               The mode is data on the request; variants are opt-in.
//   parameters  ordered JSON object appended after "model" and "messages"
//               in the wire body (insertion order is part of the bytes).
//   context     already assembled `messages`, or a `context` reference that
//               Services::resolve_context turns into messages.
//   prompt      appended as the final user message when non-empty.
//   route       which provider: the settings choice (config "llm_provider",
//               default "openrouter") resolved through the ProviderRegistry
//               manifest.  The manifest names the adapter, the secret and the
//               auth scheme; call sites never hard-code them.
//   adapter     one per provider API family, selected from data
//               (manifest metadata "adapter"):  openai_chat (OpenAI-compatible
//               chat completions incl. OpenRouter) and anthropic_messages
//               (Messages API incl. Message Batches as batch operations).
//   guard       every spending call is admitted by the shared usage guard
//               (x10 growth asks the owner) before any byte leaves and is
//               settled afterwards.  A held call is returned, never sent.
//   provenance  every variant result carries a "loom.model_use/1" record:
//               which model, through which adapter and provider, with which
//               parameters, produced which output (hashes, never secrets).
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/net/http.h"
#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {
class Config;
class Secrets;
class ProviderRegistry;
}  // namespace loom

namespace loom::llm {

inline constexpr std::string_view kProvenanceSchema = "loom.model_use/1";
// Attribution sent to OpenAI-compatible gateways that ask for it.
inline constexpr std::string_view kAppReferer = "https://github.com/chatadhd";

// ── Borrowed services ───────────────────────────────────────────────
struct Services {
  Services(const Config& c, const Secrets& s, net::HttpTransport& h) : config(c), secrets(s), http(h) {}
  const Config& config;
  const Secrets& secrets;
  net::HttpTransport& http;
  // Optional shared registry; absent: the builtin manifests plus config
  // "provider_manifests" are loaded on demand.
  const ProviderRegistry* providers = nullptr;
  // Where the usage ledger lives; empty: the directory of the config file.
  std::filesystem::path data_root;
  // Receives every variant's provenance record (no secrets) after the call.
  std::function<void(const Json&)> on_provenance;
  // Turns a `ModelRequest::context` reference into OpenAI-shaped messages.
  std::function<Result<Json>(const Json& context)> resolve_context;
};

// ── Routing (settings -> provider manifest -> adapter) ──────────────
enum class Operation { Complete, BatchSubmit, BatchStatus, BatchResults };
std::string_view to_string(Operation op) noexcept;

struct RouteSelector {
  std::string provider;      // manifest id; "" = config "llm_provider", else "openrouter"
  std::string base_url;      // "" = the provider's own (for the settings provider: config key)
  std::string secret;        // "" = the manifest's auth_secret
  std::string endpoint;      // "" = adapter default path; a path or an absolute URL
  std::string adapter;       // "" = manifest metadata "adapter", else openai_chat
  std::optional<net::Headers> headers;  // replaces the manifest's default headers when set
  std::string content_type_header = "Content-Type";  // wire spelling of that header
  bool require_credential = true;   // false: resolve address/adapter only (previews)
};

struct Route {
  std::string provider_id;
  std::string adapter;
  std::string base_url;      // no trailing slash
  std::string endpoint;      // "" = adapter default
  std::string auth_secret;
  std::string auth_scheme;
  std::string credential;    // resolved from secrets; never serialised
  net::Headers default_headers;
  std::string content_type_header = "Content-Type";
  Json metadata = Json::object();
  Json describe() const;     // everything except the credential
};

// Resolves the selector against settings and manifests.  Errc::Auth when the
// manifest needs a secret that is not stored; Errc::NotFound for an unknown
// provider or adapter.
Result<Route> resolve_route(const Services& services, const RouteSelector& selector);
// True when resolve_route would succeed (provider known and credential stored).
bool route_available(const Services& services, const RouteSelector& selector);
// The provider offering (resource, capability) whose host appears in
// `address`, as a manifest id ("" = none).  Where an address picks a protocol
// mode (native batches vs. plain chat completions) the choice is manifest data.
std::string provider_for_address(const Services& services, std::string_view resource,
                                 std::string_view capability, std::string_view address,
                                 bool any_if_unmatched = false);

// ── Request ─────────────────────────────────────────────────────────
struct ModelSpec {
  std::string model;
  std::string provider;                 // "" = the request's route
  Json parameters = Json::object();     // per-variant keys replace/extend the shared ones
};

// What the adapter needs to put one call on the wire.
struct CallSpec {
  std::string model;
  Json messages = Json::array();        // OpenAI-shaped [{"role","content"}]
  Json parameters = Json::object();     // ordered, appended after model/messages
  bool stream = false;
  std::optional<int> timeout_ms;
  std::string title;                    // X-Title attribution ("" = none)
  bool referer = true;                  // HTTP-Referer attribution
  net::Headers extra_headers;           // appended last, in order
  Json batch_requests = Json::array();  // BatchSubmit: [{"custom_id","params":{...}}]
  std::string target;                   // BatchStatus: batch id; BatchResults: results URL
  Json body = nullptr;                  // a complete pre-composed body, used byte for byte
};

struct StreamHooks {
  std::function<void(std::string_view chunk)> on_raw;         // every wire chunk, before parsing
  std::function<void(std::string_view text)> on_text;
  std::function<void(std::string_view text)> on_reasoning;
  // "malformed_chunk" | "model_invalid" | "model_changed"
  std::function<void(std::string_view issue)> on_issue;
  std::function<void()> on_dispatch;                          // admitted; about to send
};

// Gate decision for one outgoing request (see UsageGate).
struct GateDecision {
  bool dispatch = false;
  std::string status;        // the guard's status when not dispatched
  Json decision = nullptr;   // the guard's admission answer
  Json decisions = nullptr;  // every ledger decision recorded for this call
};
struct GateSettlement {
  bool attempted = false;
  std::size_t received_bytes = 0;
  Json usage = Json::object();      // provider-reported usage object
  Json declared = Json::object();   // declared resources (UsageSpec::declared[_on_success])
};
// The shared usage guard seen from the action.  Callers with a bespoke ledger
// flow (graph replies, goal typing) supply their own; everyone else gets the
// shared one (`make_usage_gate`).
class UsageGate {
 public:
  virtual ~UsageGate() = default;
  // `provenance` is the call's deterministic model_use seed (no secrets).
  virtual Result<GateDecision> admit(const net::HttpRequest& request, const Json& provenance) = 0;
  // Called once after a dispatched call, whatever its outcome.
  virtual void settle(const GateSettlement& settlement) = 0;
};

struct UsageSpec {
  bool guard = true;                          // false only for non-spending operations
  Json estimate = nullptr;                    // null: derived from kind/endpoint/model
  Json confirmation = Json::object();
  bool require_scope = false;                 // chat sends require a context scope
  Json declared = Json::object();             // declared resources recorded on settle
  Json declared_on_success = Json::object();  // ... added when the call succeeded
  std::shared_ptr<UsageGate> gate;            // overrides the shared guard
};

struct ModelRequest {
  std::string kind = "model.use";             // operation kind (guard cohort, provenance)
  std::vector<ModelSpec> models;
  std::string mode = "single";                // single | sequential | first_success
  Json parameters = Json::object();
  Json messages = Json::array();
  Json context = nullptr;
  std::string prompt;
  // A complete request body in the adapter's family shape, sent byte for byte
  // (key order included).  For callers whose recipe fixes the whole payload
  // (chat, graph replies); model/messages/parameters then only describe it in
  // the provenance record.
  Json body = nullptr;
  RouteSelector route;
  Operation operation = Operation::Complete;
  Json batch_requests = Json::array();
  std::string target;
  bool stream = false;
  std::optional<int> timeout_ms;
  std::string title;
  bool referer = true;
  net::Headers extra_headers;
  // Read the body through a StreamSink and keep the raw bytes in the result
  // (also implied by `stream`); `max_response_bytes` stops reading past it.
  bool capture = false;
  bool retain_body = true;   // false: success-status bytes reach hooks.on_raw only (large JSONL)
  std::optional<std::size_t> max_response_bytes;
  const CancelToken* cancel = nullptr;
  StreamHooks hooks;
  UsageSpec usage;
  // When set, an exception thrown by the transport or a hook is contained and
  // reported as this TransportError instead of propagating (graph replies and
  // goal typing record it; ordinary chat lets it propagate).
  std::optional<Error> contain_exceptions;
};

// ── Result ──────────────────────────────────────────────────────────
enum class State { Ok, Held, GateError, Unavailable, TransportError, HttpError, ParseError, Cancelled };
std::string_view to_string(State s) noexcept;

struct VariantResult {
  std::size_t index = 0;
  State state = State::Unavailable;
  std::string model, provider, adapter;
  bool sent = false;
  int http_status = 0;
  std::optional<Error> error;                 // Unavailable / GateError / TransportError / ParseError
  std::string text, reasoning, reported_model;
  Json usage = Json::object();
  std::vector<std::string> issues;            // "malformed_chunk" | "model_invalid" | "model_changed"
  std::string body;                           // buffered or captured body; the error body on HttpError
  std::size_t received_bytes = 0;
  bool response_limited = false;
  Json hold = nullptr;                        // Held: {status, decision, decisions}
  Json provenance = Json::object();
  bool ok() const noexcept { return state == State::Ok; }
};

struct ModelOutcome {
  std::string mode;
  std::vector<VariantResult> results;         // one per variant that was attempted
  const VariantResult* first_ok() const noexcept;
  const VariantResult& last() const { return results.back(); }
  bool ok() const noexcept { return first_ok() != nullptr; }
};

// A resolved, wire-ready call (one variant): lets a caller adjust the request
// or inspect its bytes before executing it.
struct PreparedCall {
  std::size_t index = 0;
  Route route;
  CallSpec spec;
  net::HttpRequest request;
  Json provenance = Json::object();           // deterministic seed; no secrets
};

// The action.  Errors are returned only for an invalid request; every
// per-variant failure (no credential, held by the guard, HTTP error, ...) is
// a VariantResult.
Result<ModelOutcome> use_model(const Services& services, ModelRequest request);

// The two halves of use_model for callers that must see the bytes first.
Result<PreparedCall> prepare_call(const Services& services, const ModelRequest& request, std::size_t variant = 0);
VariantResult execute_call(const Services& services, const ModelRequest& request, PreparedCall& call);

// The shared guard as a gate (root = usage ledger directory).  For callers
// that admit an operation which is not itself a model call (e.g. an automatic
// resend of many items) and settle it with declared resources.
std::shared_ptr<UsageGate> make_usage_gate(const Services& services, const UsageSpec& spec);
// What a call to this route would put on the wire, reduced to what the guard
// binds: method, URL, and the caller's body (no credential headers).
Result<net::HttpRequest> describe_request(const Services& services, const RouteSelector& selector,
                                          std::string body);

// ── Adapters: one per provider API family ───────────────────────────
struct Parsed {
  std::string text, reasoning, reported_model;
  Json usage = Json::object();
  std::vector<std::string> issues;
};

class StreamDecoder {
 public:
  virtual ~StreamDecoder() = default;
  virtual void feed(std::string_view chunk) = 0;
  virtual void finish() = 0;
  virtual const Parsed& parsed() const = 0;
};

class Adapter {
 public:
  virtual ~Adapter() = default;
  virtual std::string_view family() const noexcept = 0;
  virtual Result<net::HttpRequest> build(const Route& route, Operation op, const CallSpec& spec) const = 0;
  virtual bool success(Operation op, int http_status) const noexcept = 0;
  // Buffered 2xx body -> text/usage (Errc::Parse with the caller-visible message).
  virtual Result<Parsed> parse(std::string_view body) const = 0;
  virtual std::unique_ptr<StreamDecoder> stream_decoder(const StreamHooks& hooks,
                                                        const CancelToken* cancel) const = 0;
};

// family -> factory.  Built-ins are registered on first use; a new family is
// one register_adapter() call and no edit to any call site.
using AdapterFactory = std::function<std::unique_ptr<Adapter>()>;
void register_adapter(std::string family, AdapterFactory factory);
std::unique_ptr<Adapter> make_adapter(std::string_view family);   // nullptr when unknown
std::vector<std::string> adapter_families();

// Auth for a route's scheme: adds the header (or the query parameter) and
// returns Errc::Unsupported for a scheme this layer cannot express.
Status apply_auth(const Route& route, net::HttpRequest& request);

}  // namespace loom::llm
