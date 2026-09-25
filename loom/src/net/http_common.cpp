// Foundation part of loom/net/http.h: request/response JSON, URL + form
// helpers, multipart builder and ScriptedTransport (test double).
// The real transport lives in http_default.cpp (wave 2 net).
#include <algorithm>
#include <cctype>

#include "loom/net/http.h"
#include "loom/util/base64.h"
#include "loom/util/ids.h"
#include "loom/util/utf8.h"

namespace loom::net {

namespace {
bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}
}  // namespace

std::string header_value(const Headers& h, std::string_view name) {
  for (const auto& [k, v] : h) {
    if (iequals(k, name)) return v;
  }
  return {};
}

Json HttpRequest::to_json() const {
  Json hs = Json::object();
  for (const auto& [k, v] : headers) hs[k] = v;
  Json j{{"method", method}, {"url", url}, {"headers", hs}, {"timeout_ms", timeout_ms}, {"stream", stream}};
  if (utf8::is_valid(body)) {
    j["body"] = body;
  } else {
    j["body"] = "";
    j["body_base64"] = base64::encode(body);
  }
  return j;
}

Result<HttpRequest> HttpRequest::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "http request must be an object");
  HttpRequest r;
  r.method = json::get_string(j, "method", "GET");
  r.url = json::get_string(j, "url");
  if (r.url.empty()) return Error(Errc::InvalidArgument, "http request needs a url");
  if (const Json* h = json::find(j, "headers"); h && h->is_object()) {
    for (auto it = h->begin(); it != h->end(); ++it) {
      if (it.value().is_string()) r.headers.emplace_back(it.key(), it.value().get<std::string>());
    }
  }
  if (const Json* b64 = json::find(j, "body_base64"); b64 && b64->is_string() && !b64->get_ref<const std::string&>().empty()) {
    LOOM_TRY_ASSIGN(r.body, base64::decode(b64->get<std::string>(), true));
  } else {
    r.body = json::get_string(j, "body");
  }
  r.timeout_ms = static_cast<int>(json::get_int(j, "timeout_ms", 30000));
  r.stream = json::get_bool(j, "stream", false);
  return r;
}

Result<Json> HttpResponse::json() const { return json::parse(body); }

// ── URL helpers ────────────────────────────────────────────────────
Result<Url> parse_url(std::string_view url) {
  Url u;
  auto sep = url.find("://");
  if (sep == std::string_view::npos) return Error(Errc::InvalidArgument, "URL without scheme: " + std::string(url));
  u.scheme = std::string(url.substr(0, sep));
  std::transform(u.scheme.begin(), u.scheme.end(), u.scheme.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::string_view rest = url.substr(sep + 3);
  auto slash = rest.find_first_of("/?#");
  std::string_view authority = rest.substr(0, slash);
  u.target = slash == std::string_view::npos ? "/" : std::string(rest.substr(slash));
  if (!u.target.empty() && u.target[0] != '/') u.target = "/" + u.target;
  if (auto hash = u.target.find('#'); hash != std::string::npos) u.target.resize(hash);
  if (auto at = authority.rfind('@'); at != std::string_view::npos) authority = authority.substr(at + 1);
  std::string_view host = authority;
  std::string_view port;
  if (!authority.empty() && authority.front() == '[') {
    auto close = authority.find(']');
    if (close == std::string_view::npos) return Error(Errc::InvalidArgument, "bad IPv6 host");
    host = authority.substr(1, close - 1);
    if (close + 1 < authority.size() && authority[close + 1] == ':') port = authority.substr(close + 2);
  } else if (auto colon = authority.rfind(':'); colon != std::string_view::npos) {
    host = authority.substr(0, colon);
    port = authority.substr(colon + 1);
  }
  if (host.empty()) return Error(Errc::InvalidArgument, "URL without host: " + std::string(url));
  u.host = std::string(host);
  if (!port.empty()) {
    int p = 0;
    for (char c : port) {
      if (c < '0' || c > '9') return Error(Errc::InvalidArgument, "bad port in URL");
      p = p * 10 + (c - '0');
      if (p > 65535) return Error(Errc::InvalidArgument, "bad port in URL");
    }
    u.port = p;
  } else {
    u.port = u.scheme == "https" ? 443 : 80;
  }
  return u;
}

std::string url_encode(std::string_view s) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 15]);
    }
  }
  return out;
}

std::string form_urlencode(const std::vector<std::pair<std::string, std::string>>& fields) {
  // Python urllib.parse.urlencode (quote_plus): spaces become '+'.
  auto enc = [](std::string_view v) {
    std::string e = url_encode(v);
    std::string out;
    out.reserve(e.size());
    for (std::size_t i = 0; i < e.size(); ++i) {
      if (e.compare(i, 3, "%20") == 0) {
        out.push_back('+');
        i += 2;
      } else {
        out.push_back(e[i]);
      }
    }
    return out;
  };
  std::string out;
  for (const auto& [k, v] : fields) {
    if (!out.empty()) out.push_back('&');
    out += enc(k);
    out.push_back('=');
    out += enc(v);
  }
  return out;
}

std::string with_query(std::string_view url, const std::vector<std::pair<std::string, std::string>>& params) {
  if (params.empty()) return std::string(url);
  std::string out(url);
  out.push_back(out.find('?') == std::string::npos ? '?' : '&');
  out += form_urlencode(params);
  return out;
}

MultipartBody build_multipart(const std::vector<MultipartPart>& parts) {
  std::string boundary = "loom" + random_hex(24);
  MultipartBody mb;
  mb.content_type = "multipart/form-data; boundary=" + boundary;
  for (const auto& p : parts) {
    mb.body += "--" + boundary + "\r\n";
    mb.body += "Content-Disposition: form-data; name=\"" + p.name + "\"";
    if (!p.filename.empty()) mb.body += "; filename=\"" + p.filename + "\"";
    mb.body += "\r\n";
    if (!p.content_type.empty()) mb.body += "Content-Type: " + p.content_type + "\r\n";
    mb.body += "\r\n";
    mb.body += p.data;
    mb.body += "\r\n";
  }
  mb.body += "--" + boundary + "--\r\n";
  return mb;
}

// ── ScriptedTransport ──────────────────────────────────────────────
ScriptedTransport::Reply ScriptedTransport::Reply::json(int status, const Json& body) {
  Reply r;
  r.status = status;
  r.headers = {{"Content-Type", "application/json"}};
  r.chunks = {json::dump(body)};
  return r;
}

ScriptedTransport::Reply ScriptedTransport::Reply::text(int status, std::string body) {
  Reply r;
  r.status = status;
  r.headers = {{"Content-Type", "text/plain"}};
  r.chunks = {std::move(body)};
  return r;
}

ScriptedTransport::Reply ScriptedTransport::Reply::sse(const std::vector<std::string>& data_lines) {
  Reply r;
  r.status = 200;
  r.headers = {{"Content-Type", "text/event-stream"}};
  for (const auto& d : data_lines) r.chunks.push_back("data: " + d + "\n\n");
  return r;
}

ScriptedTransport::Reply ScriptedTransport::Reply::fail(Errc code, std::string message) {
  Reply r;
  r.error = Error(code, std::move(message));
  return r;
}

void ScriptedTransport::expect(std::string method, std::string url_prefix, Reply reply) {
  std::lock_guard lk(mu_);
  expectations_.push_back(Expectation{std::move(method), std::move(url_prefix), std::move(reply)});
}

void ScriptedTransport::set_fallback(Reply reply) {
  std::lock_guard lk(mu_);
  fallback_ = std::move(reply);
}

std::vector<HttpRequest> ScriptedTransport::requests() const {
  std::lock_guard lk(mu_);
  return seen_;
}

std::size_t ScriptedTransport::pending_expectations() const {
  std::lock_guard lk(mu_);
  return expectations_.size();
}

Result<HttpResponse> ScriptedTransport::send(const HttpRequest& req, const StreamSink* sink, const CancelToken* cancel) {
  std::optional<Reply> reply;
  {
    std::lock_guard lk(mu_);
    seen_.push_back(req);
    for (auto it = expectations_.begin(); it != expectations_.end(); ++it) {
      bool method_ok = it->method.empty() || iequals(it->method, req.method);
      if (method_ok && req.url.rfind(it->url_prefix, 0) == 0) {
        reply = std::move(it->reply);
        expectations_.erase(it);
        break;
      }
    }
    if (!reply && fallback_) reply = fallback_;
  }
  if (!reply) return Error(Errc::Network, "no scripted reply for " + req.method + " " + req.url);
  if (reply->error) return *reply->error;
  if (cancel && cancel->cancelled()) return Error(Errc::Cancelled, "request cancelled");
  HttpResponse resp;
  resp.status = reply->status;
  resp.headers = reply->headers;
  if (sink) {
    if (sink->on_headers && !sink->on_headers(resp.status, resp.headers)) {
      return Error(Errc::Cancelled, "aborted by sink");
    }
    for (const auto& c : reply->chunks) {
      if (cancel && cancel->cancelled()) return Error(Errc::Cancelled, "request cancelled");
      if (sink->on_data && !sink->on_data(c)) return Error(Errc::Cancelled, "aborted by sink");
    }
  } else {
    for (const auto& c : reply->chunks) resp.body += c;
  }
  return resp;
}

}  // namespace loom::net
