#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <map>
#include <type_traits>

#include "loom/net/http.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

static_assert(std::is_aggregate_v<net::HttpRequest>);

namespace {
net::EnvironmentLookup environment(const std::map<std::string, std::string>& values) {
  return [values](std::string_view key) -> std::optional<std::string> {
    const auto it = values.find(std::string(key));
    return it == values.end() ? std::nullopt : std::optional<std::string>(it->second);
  };
}
}

TEST_SUITE("net.profile") {
  TEST_CASE("builtin data retains aggregate defaults and serialized request bytes") {
    const auto profile = unwrap(RuntimeProfile::builtin("net"));
    net::HttpRequest aggregate{"POST", "https://example.test/api", {{"X-Synthetic", "yes"}}, "payload", 1234, true};
    CHECK(aggregate.timeout_ms == 1234);
    CHECK(aggregate.stream);
    CHECK(net::HttpRequest{}.timeout_ms == 30000);
    const auto request = unwrap(net::HttpRequest::from_json(Json{{"url", "https://example.test/api"}}));
    const Json expected{{"method", "GET"}, {"url", "https://example.test/api"}, {"headers", Json::object()},
                        {"timeout_ms", 30000}, {"stream", false}, {"body", ""}};
    CHECK(json::dump(request.to_json()) == json::dump(expected));
    const auto policy = unwrap(net::HttpTransportPolicy::for_request(request, profile, environment({})));
    CHECK(policy.timeout_ms == 30000);
    CHECK(policy.follow_redirects);
    CHECK_FALSE(policy.keep_alive);
    CHECK_FALSE(policy.ca_cert_path);
    CHECK_FALSE(policy.proxy_url);
  }

  TEST_CASE("environment precedence is deterministic and builtin HTTP has no proxy mapping") {
    const auto profile = unwrap(RuntimeProfile::builtin("net"));
    net::HttpRequest request;
    request.url = "https://example.test/api";
    const auto both = environment({{"SSL_CERT_FILE", "first-ca.pem"}, {"REQUESTS_CA_BUNDLE", "second-ca.pem"},
                                   {"HTTPS_PROXY", "http://proxy.test:3128"}, {"HTTP_PROXY", "http://other.test:8080"}});
    auto policy = unwrap(net::HttpTransportPolicy::for_request(request, profile, both));
    CHECK(policy.ca_cert_path == std::optional<std::string>("first-ca.pem"));
    CHECK(policy.proxy_url == std::optional<std::string>("http://proxy.test:3128"));
    auto fallback = unwrap(net::HttpTransportPolicy::for_request(request, profile, environment({
      {"SSL_CERT_FILE", ""}, {"REQUESTS_CA_BUNDLE", "second-ca.pem"}})));
    CHECK(fallback.ca_cert_path == std::optional<std::string>("second-ca.pem"));
    CHECK_FALSE(fallback.proxy_url);
    net::HttpRequest http;
    http.url = "http://example.test/api";
    CHECK_FALSE(unwrap(net::HttpTransportPolicy::for_request(http, profile, both)).proxy_url);
  }

  TEST_CASE("custom timeout flags and environment sources apply without provider-name dispatch") {
    const auto base = unwrap(RuntimeProfile::builtin("net"));
    const auto profile = unwrap(base.with_overrides(Json{
      {"default_timeout_ms", 7000}, {"follow_redirects", false}, {"keep_alive", true},
      {"ca_environment", Json::array({"CUSTOM_CA", "SSL_CERT_FILE"})},
      {"proxy_environment", Json{{"https", Json::array({"CUSTOM_PROXY", "HTTPS_PROXY"})},
                                  {"http", Json::array({"HTTP_PROXY"})}}}}));
    const auto vars = environment({{"CUSTOM_CA", "custom-ca.pem"}, {"SSL_CERT_FILE", "fallback-ca.pem"},
                                  {"CUSTOM_PROXY", "http://custom.test:1234"}, {"HTTPS_PROXY", "http://fallback.test:5678"},
                                  {"HTTP_PROXY", "http://http-proxy.test:8080"}});
    auto request = unwrap(net::HttpRequest::from_json_with_profile(Json{{"url", "https://example.test/api"}}, profile));
    CHECK(request.timeout_ms == 7000);
    auto policy = unwrap(net::HttpTransportPolicy::for_request(request, profile, vars));
    CHECK(policy.timeout_ms == 7000);
    CHECK_FALSE(policy.follow_redirects);
    CHECK(policy.keep_alive);
    CHECK(policy.ca_cert_path == std::optional<std::string>("custom-ca.pem"));
    CHECK(policy.proxy_url == std::optional<std::string>("http://custom.test:1234"));
    CHECK(policy.profile_hash == profile.hash());
    request.timeout_ms = 4321;
    CHECK(unwrap(net::HttpTransportPolicy::for_request(request, profile, vars)).timeout_ms == 4321);
    for (int nonpositive : {0, -10}) {
      request.timeout_ms = nonpositive;
      CHECK(unwrap(net::HttpTransportPolicy::for_request(request, profile, vars)).timeout_ms == 7000);
    }
    request.url = "http://example.test/api";
    CHECK(unwrap(net::HttpTransportPolicy::for_request(request, profile, vars)).proxy_url ==
          std::optional<std::string>("http://http-proxy.test:8080"));
    auto clear_ca = unwrap(profile.with_overrides(Json{{"ca_environment", Json::array()}}));
    CHECK_FALSE(unwrap(net::HttpTransportPolicy::for_request(request, clear_ca, vars)).ca_cert_path);
    auto values = profile.values();
    values["proxy_environment"].erase("https");
    const auto removed = unwrap(profile.with_values(values));
    request.url = "https://example.test/api";
    CHECK_FALSE(unwrap(net::HttpTransportPolicy::for_request(request, removed, vars)).proxy_url);
    CHECK(net::make_default_transport_with_profile(removed));
  }

  TEST_CASE("request parser passes profile defaults and explicit values to a scripted transport") {
    const auto base = unwrap(RuntimeProfile::builtin("net"));
    const auto profile = unwrap(base.with_overrides(Json{{"default_timeout_ms", 9876}}));
    net::ScriptedTransport transport;
    transport.set_fallback(net::ScriptedTransport::Reply::text(200, "synthetic reply"));
    const auto missing = unwrap(net::HttpRequest::from_json_with_profile(Json{{"url", "https://example.test/api"}}, profile));
    CHECK(unwrap(transport.send(missing)).body == "synthetic reply");
    const auto explicit_request = unwrap(net::HttpRequest::from_json_with_profile(
        Json{{"url", "https://example.test/api"}, {"timeout_ms", 30000}}, profile));
    CHECK(unwrap(transport.send(explicit_request)).status == 200);
    const auto seen = transport.requests();
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].timeout_ms == 9876);
    CHECK(seen[1].timeout_ms == 30000);
    CHECK(unwrap(net::HttpRequest::from_json_with_profile(explicit_request.to_json(), profile)).to_json() ==
          explicit_request.to_json());
  }

  TEST_CASE("checked factories reject malformed overlays and permissive schemas cannot change TLS verification") {
    const auto builtin = unwrap(RuntimeProfile::builtin("net"));
    CHECK(unwrap(net::make_default_transport_with_profile(builtin))->name() == net::make_default_transport()->name());
    CHECK_FALSE(builtin.with_overrides(Json{{"verify_tls", false}}));
    const auto foreign = unwrap(RuntimeProfile::builtin("memory"));
    CHECK_FALSE(net::make_default_transport_with_profile(foreign));
    Json definition{{"schema", "loom.runtime_profile/1"}, {"domain", "net"}, {"revision", 1},
                    {"defaults", Json{{"default_timeout_ms", "wrong"}}}, {"value_schema", Json{{"type", "object"}}}};
    const auto permissive = unwrap(RuntimeProfile::from_definition(definition));
    CHECK_FALSE(net::make_default_transport_with_profile(permissive));
    CHECK_FALSE(net::HttpRequest::from_json_with_profile(Json{{"url", "https://example.test"}}, permissive));
    CHECK_FALSE(net::HttpTransportPolicy::for_request(net::HttpRequest{"GET", "https://example.test", {}, "", 30000, false}, permissive,
                                                     environment({})));
    fsutil::TempDir data;
    unwrap(fsutil::ensure_dir(data.path() / "profiles"));
    unwrap(fsutil::write_file(data.path() / "profiles/net.pack", "{invalid"));
    CHECK_FALSE(net::make_default_transport_checked(data.path()));
  }

  TEST_CASE("timeout parsing fails before native integer overflow") {
    const auto profile = unwrap(RuntimeProfile::builtin("net"));
    Json request{{"url", "https://example.test"}, {"timeout_ms", std::numeric_limits<int>::max()}};
    CHECK(unwrap(net::HttpRequest::from_json_with_profile(request, profile)).timeout_ms == std::numeric_limits<int>::max());
    request["timeout_ms"] = std::numeric_limits<std::uint64_t>::max();
    CHECK_FALSE(net::HttpRequest::from_json_with_profile(request, profile));
    request["timeout_ms"] = 2147483648.0;
    CHECK_FALSE(net::HttpRequest::from_json_with_profile(request, profile));
    request["timeout_ms"] = std::numeric_limits<double>::infinity();
    CHECK_FALSE(net::HttpRequest::from_json_with_profile(request, profile));
  }
}
