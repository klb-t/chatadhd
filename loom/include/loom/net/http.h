// loom/net/http.h — HTTP transport abstraction.            [OWNER: wave 2 net]
//
// Every network call in Loom (chat, semantic LLM, batches, models, ASR, OCR,
// GitHub) goes through an HttpTransport, so platforms can inject their own
// stack (Android: OkHttp through JNI via loom_set_http_transport) and tests
// can script responses. Python used `requests` directly.
//
// Contract for implementations:
//   * send() blocks the calling thread until the response is complete, the
//     sink aborts, the CancelToken fires, or the timeout elapses.
//   * Non-2xx statuses are NOT errors: they come back in HttpResponse.status
//     (callers decide, like Python code checking resp.status_code).
//   * Transport failures map to Errc::Network, Errc::Timeout, Errc::Cancelled
//     (token fired or a sink callback returned false), Errc::Unavailable
//     (e.g. https without TLS support).
//   * With a StreamSink, body bytes are delivered incrementally through
//     on_data and HttpResponse.body stays empty; without one the body is
//     buffered. on_headers is called once before any on_data.
//   * Implementations must be safe to call from several threads at once.
#pragma once

#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom { class RuntimeProfile; }

namespace loom::net {

using Headers = std::vector<std::pair<std::string, std::string>>;

// Case-insensitive header lookup ("" when absent).
std::string header_value(const Headers& h, std::string_view name);

// The legacy aggregate default comes from the immutable built-in net recipe.
// Profile-aware JSON parsing applies the selected recipe's omitted default.
int builtin_http_timeout_ms();

struct HttpRequest {
  std::string method = "GET";
  std::string url;
  Headers headers;
  std::string body;
  int timeout_ms = builtin_http_timeout_ms();
  // Hint that the caller will consume the body incrementally (SSE, JSONL).
  bool stream = false;

  // {"method","url","headers":{name:value,...},"body","timeout_ms","stream"}
  // (the shape handed to platform transports through the C API).
  Json to_json() const;
  static Result<HttpRequest> from_json(const Json& j);
  static Result<HttpRequest> from_json_with_profile(const Json& j, const RuntimeProfile& profile);
};

struct HttpResponse {
  int status = 0;
  Headers headers;
  std::string body;
  bool ok() const noexcept { return status >= 200 && status < 300; }
  std::string header(std::string_view name) const { return header_value(headers, name); }
  Result<Json> json() const;  // parse body
};

struct StreamSink {
  // Return false to abort the transfer (send() then returns Errc::Cancelled).
  std::function<bool(int status, const Headers& headers)> on_headers;
  std::function<bool(std::string_view chunk)> on_data;
};

class HttpTransport {
 public:
  virtual ~HttpTransport() = default;
  virtual Result<HttpResponse> send(const HttpRequest& req, const StreamSink* sink = nullptr,
                                    const CancelToken* cancel = nullptr) = 0;
  virtual std::string name() const = 0;
};

// Desktop default: cpp-httplib (HTTPS when built with OpenSSL). The built-in
// recipe retains HTTPS_PROXY and SSL_CERT_FILE/REQUESTS_CA_BUNDLE precedence.
// On builds without a usable
// stack every send() fails with Errc::Unavailable.
std::shared_ptr<HttpTransport> make_default_transport();
// Objects are immutable snapshots. Existing malformed overlays fail closed;
// custom definitions are revalidated against the supported net descriptor.
Result<std::shared_ptr<HttpTransport>> make_default_transport_checked(
    const std::filesystem::path& data_dir = {}, const Json& overrides = Json::object());
Result<std::shared_ptr<HttpTransport>> make_default_transport_with_profile(const RuntimeProfile& profile);

using EnvironmentLookup = std::function<std::optional<std::string>(std::string_view)>;
// Deterministic preparation shared by the real adapter and offline inspection.
// An empty lookup reads the process environment. No request is sent here.
// TLS certificate verification remains an engine invariant, never a setting.
struct HttpTransportPolicy {
  int timeout_ms = 0;
  bool follow_redirects = false;
  bool keep_alive = false;
  std::optional<std::string> ca_cert_path;
  std::optional<std::string> proxy_url;
  std::string profile_hash;
  static Result<HttpTransportPolicy> for_request(const HttpRequest& request, const RuntimeProfile& profile,
                                                const EnvironmentLookup& lookup = {});
};

// ── Test double (implemented in the foundation; used by all wave-2 tests) ──
// Expectations are consumed FIFO: the first unused expectation whose method
// matches (empty = any) and whose url starts with url_prefix answers the
// request. Unmatched requests get the fallback reply (default: error
// Errc::Network "no scripted reply").
class ScriptedTransport final : public HttpTransport {
 public:
  struct Reply {
    int status = 200;
    Headers headers;
    std::vector<std::string> chunks;  // concatenated for buffered requests
    std::optional<Error> error;       // transport-level failure instead of a response
    static Reply json(int status, const Json& body);
    static Reply text(int status, std::string body);
    static Reply sse(const std::vector<std::string>& data_lines);  // "data: <line>\n\n" per entry
    static Reply fail(Errc code, std::string message);
  };

  void expect(std::string method, std::string url_prefix, Reply reply);
  void set_fallback(Reply reply);
  std::vector<HttpRequest> requests() const;  // every request seen, in order
  std::size_t pending_expectations() const;

  Result<HttpResponse> send(const HttpRequest& req, const StreamSink* sink = nullptr,
                            const CancelToken* cancel = nullptr) override;
  std::string name() const override { return "scripted"; }

 private:
  struct Expectation {
    std::string method;
    std::string url_prefix;
    Reply reply;
  };
  mutable std::mutex mu_;
  std::deque<Expectation> expectations_;
  std::optional<Reply> fallback_;
  std::vector<HttpRequest> seen_;
};

// ── Helpers (implemented in the foundation) ─────────────────────────
struct Url {
  std::string scheme;  // "https"
  std::string host;
  int port = 0;        // explicit or scheme default (80/443)
  std::string target;  // path + query, at least "/"
};
Result<Url> parse_url(std::string_view url);

// RFC 3986 percent-encoding of everything except unreserved characters.
std::string url_encode(std::string_view s);
// application/x-www-form-urlencoded (Python requests `data=` dict).
std::string form_urlencode(const std::vector<std::pair<std::string, std::string>>& fields);
// Appends ?k=v&... (or &k=v when a query exists).
std::string with_query(std::string_view url, const std::vector<std::pair<std::string, std::string>>& params);

struct MultipartPart {
  std::string name;
  std::string filename;      // empty for plain fields
  std::string content_type;  // empty for plain fields
  std::string data;
};
struct MultipartBody {
  std::string content_type;  // "multipart/form-data; boundary=..."
  std::string body;
};
// Python requests `files=` + `data=`.
MultipartBody build_multipart(const std::vector<MultipartPart>& parts);

}  // namespace loom::net
