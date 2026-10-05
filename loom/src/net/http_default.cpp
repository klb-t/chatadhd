// OWNER: wave 2 net. Default transport: cpp-httplib + OpenSSL (when
// available). Honours HTTPS_PROXY and SSL_CERT_FILE/REQUESTS_CA_BUNDLE.
#include "loom/net/http.h"

#include <chrono>
#include <stdexcept>
#include <string>

#include "httplib.h"
#include "loom/log.h"
#include "loom/runtime_profile.h"

namespace loom::net {
namespace {

Errc map_httplib_error(httplib::Error e) {
  switch (e) {
    case httplib::Error::ConnectionTimeout:
      return Errc::Timeout;
    case httplib::Error::Canceled:
      return Errc::Cancelled;
    case httplib::Error::UnsupportedMultipartBoundaryChars:
      return Errc::InvalidArgument;
    case httplib::Error::Connection:
    case httplib::Error::BindIPAddress:
    case httplib::Error::Read:
    case httplib::Error::Write:
    case httplib::Error::ExceedRedirectCount:
    case httplib::Error::SSLConnection:
    case httplib::Error::SSLLoadingCerts:
    case httplib::Error::SSLServerVerification:
    case httplib::Error::SSLServerHostnameVerification:
    case httplib::Error::Compression:
    case httplib::Error::ProxyConnection:
    default:
      return Errc::Network;
  }
}

class DefaultTransport final : public HttpTransport {
 public:
  explicit DefaultTransport(RuntimeProfile profile) : profile_(std::move(profile)) {}

  Result<HttpResponse> send(const HttpRequest& req, const StreamSink* sink, const CancelToken* cancel) override {
    if (cancel && cancel->cancelled()) return Error(Errc::Cancelled, "request cancelled");

    auto urlr = parse_url(req.url);
    if (!urlr) return urlr.error();
    const Url& u = *urlr;
    LOOM_TRY_ASSIGN(auto policy, HttpTransportPolicy::for_request(req, profile_));

    std::string scheme_host_port = u.scheme + "://" + u.host + ":" + std::to_string(u.port);
    httplib::Client cli(scheme_host_port);
    if (!cli.is_valid()) {
      return Error(Errc::Unavailable, u.scheme == "https" ? "HTTPS is not available (built without TLS support)"
                                                           : "could not build an HTTP client for " + req.url);
    }

#ifdef LOOM_HAVE_OPENSSL
    cli.enable_server_certificate_verification(true);
    if (policy.ca_cert_path) {
      cli.set_ca_cert_path(*policy.ca_cert_path);
    }
#endif

    if (policy.proxy_url) {
      if (auto pu = parse_url(*policy.proxy_url)) {
        cli.set_proxy(pu->host, pu->port);
      } else {
        log::debug("loom.net.http", "ignoring unparsable configured proxy value");
      }
    }

    int timeout_ms = policy.timeout_ms;
    cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms));
    cli.set_read_timeout(std::chrono::milliseconds(timeout_ms));
    cli.set_write_timeout(std::chrono::milliseconds(timeout_ms));
    cli.set_follow_location(policy.follow_redirects);
    cli.set_keep_alive(policy.keep_alive);

    httplib::Request hreq;
    hreq.method = req.method;
    hreq.path = u.target;
    for (const auto& [k, v] : req.headers) hreq.headers.emplace(k, v);
    hreq.body = req.body;

    int status = 0;
    Headers resp_headers;
    bool got_headers = false;
    bool aborted = false;
    std::string buffered_body;

    hreq.response_handler = [&](const httplib::Response& hres) -> bool {
      status = hres.status;
      resp_headers.clear();
      for (const auto& kv : hres.headers) resp_headers.emplace_back(kv.first, kv.second);
      got_headers = true;
      if (cancel && cancel->cancelled()) {
        aborted = true;
        return false;
      }
      if (sink && sink->on_headers && !sink->on_headers(status, resp_headers)) {
        aborted = true;
        return false;
      }
      return true;
    };
    hreq.content_receiver = [&](const char* data, size_t len, uint64_t, uint64_t) -> bool {
      if (cancel && cancel->cancelled()) {
        aborted = true;
        return false;
      }
      if (sink) {
        if (sink->on_data && !sink->on_data(std::string_view(data, len))) {
          aborted = true;
          return false;
        }
      } else {
        buffered_body.append(data, len);
      }
      return true;
    };

    httplib::Response hres;
    httplib::Error herr = httplib::Error::Success;
    bool ok = cli.send(hreq, hres, herr);

    if (cancel && cancel->cancelled()) return Error(Errc::Cancelled, "request cancelled");
    if (aborted) return Error(Errc::Cancelled, "aborted by sink");
    if (!ok) {
      return Error(map_httplib_error(herr), "http transport error (" + httplib::to_string(herr) + "): " + req.method +
                                                " " + req.url);
    }

    HttpResponse out;
    if (got_headers) {
      out.status = status;
      out.headers = resp_headers;
    } else {
      out.status = hres.status;
      for (const auto& kv : hres.headers) out.headers.emplace_back(kv.first, kv.second);
    }
    if (!sink) out.body = buffered_body;
    return out;
  }

  std::string name() const override {
#ifdef LOOM_HAVE_OPENSSL
    return "httplib+openssl";
#else
    return "httplib";
#endif
  }

 private:
  RuntimeProfile profile_;
};

}  // namespace

Result<std::shared_ptr<HttpTransport>> make_default_transport_with_profile(const RuntimeProfile& profile) {
  if (profile.domain() != "net") return Error(Errc::InvalidArgument, "expected net profile");
  LOOM_TRY_ASSIGN(auto builtin, RuntimeProfile::builtin("net"));
  LOOM_TRY_ASSIGN(auto checked, builtin.with_values(profile.values()));
  return std::shared_ptr<HttpTransport>(std::make_shared<DefaultTransport>(std::move(checked)));
}

Result<std::shared_ptr<HttpTransport>> make_default_transport_checked(const std::filesystem::path& data_dir,
                                                                   const Json& overrides) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("net", data_dir, overrides));
  return make_default_transport_with_profile(profile);
}

std::shared_ptr<HttpTransport> make_default_transport() {
  auto transport = make_default_transport_checked();
  if (!transport) throw std::logic_error(transport.error().to_string());
  return std::move(*transport);
}

}  // namespace loom::net
