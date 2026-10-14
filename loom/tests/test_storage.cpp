// Storage backend contract suite + placement/transfer behaviour (P4).
// Network backends run against in-process fake servers on 127.0.0.1 (no real network).
#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <thread>

#include "loom/config.h"
#include "loom/graph_entry.h"
#include "loom/loom.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/storage_command.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/runtime_profile.h"
#include "loom/storage_backend.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "../src/storage/storage_internal.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::storage;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

// ── tiny HTTP/1.1 server accepting ANY method ───────────────────────
struct Req {
  std::string method, path, query, body;
  std::map<std::string, std::string> h;  // lowercase names
};
struct Resp {
  int status = 200;
  std::map<std::string, std::string> h;
  std::string body;
};

std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string pct_decode(const std::string& s) {
  std::string o;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      o += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else o += s[i];
  }
  return o;
}
std::string pct_encode(const std::string& s) { return detail::uri_encode(s, true); }
std::string xml_escape(const std::string& s) {
  std::string o;
  for (char c : s) { if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else o += c; }
  return o;
}

class MiniServer {
 public:
  explicit MiniServer(std::function<Resp(const Req&)> h) : handler_(std::move(h)) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    REQUIRE(::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
    socklen_t len = sizeof a;
    ::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len);
    port_ = ntohs(a.sin_port);
    ::listen(fd_, 16);
    th_ = std::thread([this] { loop(); });
  }
  ~MiniServer() { stop_ = true; th_.join(); ::close(fd_); }
  int port() const { return port_; }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

 private:
  void loop() {
    while (!stop_) {
      pollfd p{fd_, POLLIN, 0};
      if (::poll(&p, 1, 50) <= 0) continue;
      int c = ::accept(fd_, nullptr, nullptr);
      if (c < 0) continue;
      serve(c);
      ::close(c);
    }
  }
  static bool read_more(int c, std::string& buf) {
    pollfd p{c, POLLIN, 0};
    if (::poll(&p, 1, 5000) <= 0) return false;
    char tmp[65536];
    ssize_t n = ::recv(c, tmp, sizeof tmp, 0);
    if (n <= 0) return false;
    buf.append(tmp, static_cast<std::size_t>(n));
    return true;
  }
  void serve(int c) {
    std::string buf;
    std::size_t he;
    while ((he = buf.find("\r\n\r\n")) == std::string::npos) if (!read_more(c, buf)) return;
    Req r;
    std::string head = buf.substr(0, he);
    std::size_t le = head.find("\r\n");
    std::string line = head.substr(0, le);
    auto s1 = line.find(' '), s2 = line.find(' ', s1 + 1);
    r.method = line.substr(0, s1);
    std::string target = line.substr(s1 + 1, s2 - s1 - 1);
    auto q = target.find('?');
    r.path = target.substr(0, q);
    if (q != std::string::npos) r.query = target.substr(q + 1);
    std::size_t pos = le;
    while (pos != std::string::npos && pos < head.size()) {
      std::size_t nx = head.find("\r\n", pos + 2);
      std::string hl = head.substr(pos + 2, nx == std::string::npos ? std::string::npos : nx - pos - 2);
      auto col = hl.find(':');
      if (col != std::string::npos) {
        std::string v = hl.substr(col + 1);
        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        r.h[lower(hl.substr(0, col))] = v;
      }
      pos = nx;
    }
    std::size_t want = r.h.count("content-length") ? std::stoul(r.h["content-length"]) : 0;
    r.body = buf.substr(he + 4);
    while (r.body.size() < want) { std::string more; if (!read_more(c, more)) return; r.body += more; }
    Resp resp = handler_(r);
    std::string out = "HTTP/1.1 " + std::to_string(resp.status) + " X\r\nConnection: close\r\n";
    const bool head_only = r.method == "HEAD";
    if (!resp.h.count("Content-Length")) resp.h["Content-Length"] = std::to_string(resp.body.size());
    for (auto& [k, v] : resp.h) out += k + ": " + v + "\r\n";
    out += "\r\n";
    if (!head_only) out += resp.body;
    std::size_t sent = 0;
    while (sent < out.size()) { ssize_t n = ::send(c, out.data() + sent, out.size() - sent, MSG_NOSIGNAL); if (n <= 0) break; sent += static_cast<std::size_t>(n); }
  }
  std::function<Resp(const Req&)> handler_;
  int fd_ = -1, port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread th_;
};

// ── fake S3 (path style, checks SigV4 with the published algorithm) ──
constexpr const char* kAccess = "AKIATESTACCESS";
constexpr const char* kSecret = "s3cr3t/KEY+never/log/this";

struct FakeS3 {
  std::map<std::string, std::string> objects, meta_sha;
  std::atomic<int> bad_sigs{0};
  // multipart bookkeeping
  struct Upload { std::string key, sha; std::map<int, std::string> parts; };
  std::map<std::string, Upload> uploads;
  int created = 0, aborted = 0, completed = 0, plain_puts = 0, fail_part = 0;  // fail_part: part number answered with 500
  bool complete_error_body = false;
  std::vector<std::size_t> part_sizes;
  std::unique_ptr<MiniServer> srv;
  FakeS3() { srv = std::make_unique<MiniServer>([this](const Req& r) { return handle(r); }); }

  bool signature_ok(const Req& r) {
    auto it = r.h.find("authorization");
    if (it == r.h.end()) return false;
    const std::string& a = it->second;
    auto get = [&](const std::string& k) { auto p = a.find(k + "="); auto e = a.find_first_of(", ", p + k.size() + 1); return a.substr(p + k.size() + 1, e == std::string::npos ? std::string::npos : e - p - k.size() - 1); };
    std::string cred = get("Credential");
    if (cred.rfind(std::string(kAccess) + "/", 0) != 0) return false;
    detail::SigV4In in;
    in.method = r.method;
    in.canonical_path = r.path;
    in.canonical_query = r.query;
    in.payload_hash = r.h.count("x-amz-content-sha256") ? r.h.at("x-amz-content-sha256") : "";
    if (in.payload_hash != Sha256::hex(r.body)) return false;
    in.amzdate = r.h.at("x-amz-date");
    in.region = "us-east-1";
    in.secret_key = kSecret;
    std::string sh = get("SignedHeaders");
    std::size_t s = 0;
    while (s <= sh.size()) { auto e = sh.find(';', s); if (e == std::string::npos) e = sh.size(); std::string n = sh.substr(s, e - s); if (!n.empty()) in.headers[n] = r.h.at(n); s = e + 1; }
    return detail::sigv4_signature(in) == get("Signature");
  }

  Resp handle(const Req& r) {
    Resp o;
    if (!signature_ok(r)) { ++bad_sigs; o.status = 403; o.body = "<Error><Code>SignatureDoesNotMatch</Code></Error>"; return o; }
    std::string path = pct_decode(r.path);
    const std::string base = "/bkt/";
    if (r.method == "GET" && (path == "/bkt" || path == "/bkt/")) {
      std::string prefix;
      std::size_t cont = 0;
      std::set<std::string> params;
      for (std::size_t s = 0; s <= r.query.size();) {
        auto e = r.query.find('&', s); if (e == std::string::npos) e = r.query.size();
        std::string kv = r.query.substr(s, e - s); auto eq = kv.find('=');
        if (eq != std::string::npos) {
          std::string k = kv.substr(0, eq), v = pct_decode(kv.substr(eq + 1));
          if (k == "prefix") prefix = v;
          if (k == "continuation-token") cont = std::stoul(v);
        }
        s = e + 1;
      }
      std::vector<std::pair<std::string, std::string>> m;
      for (auto& [k, v] : objects) if (k.rfind(prefix, 0) == 0) m.emplace_back(k, v);
      const std::size_t page = 2;
      std::string xml = "<?xml version=\"1.0\"?><ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">";
      std::size_t end = std::min(m.size(), cont + page);
      for (std::size_t i = cont; i < end; ++i)
        xml += "<Contents><Key>" + xml_escape(m[i].first) + "</Key><Size>" + std::to_string(m[i].second.size()) + "</Size></Contents>";
      if (end < m.size()) xml += "<IsTruncated>true</IsTruncated><NextContinuationToken>" + std::to_string(end) + "</NextContinuationToken>";
      else xml += "<IsTruncated>false</IsTruncated>";
      o.body = xml + "</ListBucketResult>";
      return o;
    }
    if (path.rfind(base, 0) != 0) { o.status = 404; return o; }
    std::string key = path.substr(base.size());
    if (r.method == "POST" && r.query == "uploads=") {
      std::string id = "UP" + std::to_string(++created) + "/+=";
      uploads[id] = Upload{key, r.h.count("x-amz-meta-sha256") ? r.h.at("x-amz-meta-sha256") : "", {}};
      o.body = "<InitiateMultipartUploadResult><Bucket>bkt</Bucket><Key>" + xml_escape(key) + "</Key><UploadId>" + xml_escape(id) +
               "</UploadId></InitiateMultipartUploadResult>";
      return o;
    }
    if (r.query.find("uploadId=") != std::string::npos) {
      std::string id, pn;
      for (std::size_t s = 0; s <= r.query.size();) {
        auto e = r.query.find('&', s); if (e == std::string::npos) e = r.query.size();
        std::string kv = r.query.substr(s, e - s); auto eq = kv.find('=');
        if (eq != std::string::npos) { if (kv.substr(0, eq) == "uploadId") id = pct_decode(kv.substr(eq + 1)); if (kv.substr(0, eq) == "partNumber") pn = kv.substr(eq + 1); }
        s = e + 1;
      }
      auto u = uploads.find(id);
      if (u == uploads.end() || u->second.key != key) { o.status = 404; o.body = "<Error><Code>NoSuchUpload</Code></Error>"; return o; }
      if (r.method == "PUT") {
        int n = std::stoi(pn);
        if (n == fail_part) { o.status = 500; o.body = "<Error><Code>InternalError</Code></Error>"; return o; }
        u->second.parts[n] = r.body;
        part_sizes.push_back(r.body.size());
        o.h["ETag"] = "\"etag-" + std::to_string(n) + "\"";
        return o;
      }
      if (r.method == "DELETE") { ++aborted; uploads.erase(u); o.status = 204; return o; }
      if (r.method == "POST") {
        if (complete_error_body) { o.body = "<Error><Code>InternalError</Code></Error>"; return o; }
        std::string all;
        std::size_t pos = 0;
        int expect = 1;
        while ((pos = r.body.find("<PartNumber>", pos)) != std::string::npos) {
          int n = std::stoi(r.body.substr(pos + 12));
          if (n != expect++ || !u->second.parts.count(n) || r.body.find("&quot;etag-" + std::to_string(n) + "&quot;", pos) == std::string::npos) { o.status = 400; o.body = "<Error><Code>InvalidPart</Code></Error>"; return o; }
          all += u->second.parts[n];
          pos += 12;
        }
        objects[key] = all; meta_sha[key] = u->second.sha; ++completed; uploads.erase(u);
        o.body = "<CompleteMultipartUploadResult><Key>" + xml_escape(key) + "</Key></CompleteMultipartUploadResult>";
        return o;
      }
    }
    if (r.method == "PUT") { ++plain_puts; objects[key] = r.body; meta_sha[key] = r.h.count("x-amz-meta-sha256") ? r.h.at("x-amz-meta-sha256") : ""; return o; }
    if (r.method == "DELETE") { objects.erase(key); o.status = 204; return o; }
    auto f = objects.find(key);
    if (f == objects.end()) { o.status = 404; return o; }
    if (r.method == "HEAD") { o.h["x-amz-meta-sha256"] = meta_sha[key]; o.h["Content-Length"] = std::to_string(f->second.size()); return o; }
    o.body = f->second;
    return o;
  }
};

// ── fake WebDAV (Basic auth, MKCOL/PUT/PROPFIND) ─────────────────────
struct FakeDav {
  std::map<std::string, std::string> files;  // "dir/file" relative to /dav
  std::set<std::string> colls{""};
  std::unique_ptr<MiniServer> srv;
  FakeDav() { srv = std::make_unique<MiniServer>([this](const Req& r) { return handle(r); }); }
  static std::string parent(const std::string& p) { auto s = p.rfind('/'); return s == std::string::npos ? "" : p.substr(0, s); }
  Resp handle(const Req& r) {
    Resp o;
    if (r.h.count("authorization") == 0 || r.h.at("authorization").rfind("Basic ", 0) != 0) { o.status = 401; return o; }
    std::string path = pct_decode(r.path);
    if (path.rfind("/dav", 0) != 0) { o.status = 404; return o; }
    std::string rel = path.substr(4);
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    while (!rel.empty() && rel.back() == '/') rel.pop_back();
    if (r.method == "MKCOL") {
      if (colls.count(rel)) { o.status = 405; return o; }
      if (!colls.count(parent(rel))) { o.status = 409; return o; }
      colls.insert(rel); o.status = 201; return o;
    }
    if (r.method == "PUT") {
      if (!colls.count(parent(rel))) { o.status = 409; return o; }
      files[rel] = r.body; o.status = 201; return o;
    }
    if (r.method == "DELETE") { if (!files.erase(rel)) o.status = 404; else o.status = 204; return o; }
    if (r.method == "PROPFIND") {
      if (!colls.count(rel) && !files.count(rel)) { o.status = 404; return o; }
      auto resp = [](const std::string& href, bool coll, std::size_t len) {
        return "<d:response><d:href>" + xml_escape(pct_encode(href)) + "</d:href><d:propstat><d:prop>" +
               (coll ? "<d:resourcetype><d:collection/></d:resourcetype>" : "<d:resourcetype/><d:getcontentlength>" + std::to_string(len) + "</d:getcontentlength>") +
               "</d:prop></d:propstat></d:response>";
      };
      std::string xml = "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\">";
      xml += resp("/dav/" + rel + (colls.count(rel) ? "/" : ""), colls.count(rel) > 0, files.count(rel) ? files[rel].size() : 0);
      if (colls.count(rel) && r.h.count("depth") && r.h.at("depth") == "1") {
        for (auto& c : colls) if (!c.empty() && c != rel && parent(c) == rel) xml += resp("/dav/" + c + "/", true, 0);
        for (auto& [f, v] : files) if (parent(f) == rel) xml += resp("/dav/" + f, false, v.size());
      }
      o.status = 207; o.body = xml + "</d:multistatus>"; return o;
    }
    auto f = files.find(rel);
    if (f == files.end()) { o.status = 404; return o; }
    if (r.method == "HEAD") { o.h["Content-Length"] = std::to_string(f->second.size()); return o; }
    o.body = f->second;
    return o;
  }
};

// ── fake Google Drive + OAuth token endpoint, injected as the HTTP transport ──
struct FakeDrive final : net::HttpTransport {
  struct F { std::string name, parent, content, mime, sha; };
  std::map<std::string, F> files;
  std::vector<std::string> sent;            // "METHOD url" of every request (no bodies)
  std::vector<std::string> token_bodies;    // bodies sent to the token endpoint
  std::string current = "at-0";
  int issued = 0, resumable_puts = 0, expires_in = 3600, next_id = 0, page = 2;
  bool refresh_fails = false;
  std::map<std::string, std::pair<std::string, F>> sessions;  // resumable sessions: location -> (existing id or "", meta)
  mutable std::mutex mu;

  std::string name() const override { return "fake-drive"; }
  void revoke() { current = "revoked"; }
  static Result<net::HttpResponse> reply(int st, std::string body, net::Headers h = {}) {
    net::HttpResponse r; r.status = st; r.body = std::move(body); r.headers = std::move(h); return r;
  }
  Json file_json(const std::string& id, const F& f) const {
    return Json{{"id", id}, {"name", f.name}, {"size", std::to_string(f.content.size())}, {"mimeType", f.mime}, {"appProperties", Json{{"sha256", f.sha}}}};
  }
  static std::string unq(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) { if (s[i] == '\\' && i + 1 < s.size()) ++i; o += s[i]; }
    return o;
  }
  static std::string param(const std::string& q, const std::string& name) {
    for (std::size_t s = 0; s <= q.size();) {
      auto e = q.find('&', s); if (e == std::string::npos) e = q.size();
      std::string kv = q.substr(s, e - s); auto eq = kv.find('=');
      if (eq != std::string::npos && kv.substr(0, eq) == name) return pct_decode(kv.substr(eq + 1));
      s = e + 1;
    }
    return "";
  }
  void store(const std::string& id, F meta_in, const std::string& content, const std::string& mime) {
    F& f = files[id];
    if (!meta_in.name.empty()) f.name = meta_in.name;
    if (!meta_in.parent.empty()) f.parent = meta_in.parent;
    if (!meta_in.sha.empty()) f.sha = meta_in.sha;
    f.content = content; f.mime = mime;
  }
  static F meta_of(const std::string& js) {
    Json m = Json::parse(js);
    F f; f.name = json::get_string(m, "name");
    if (m.contains("parents")) f.parent = m["parents"][0].get<std::string>();
    if (m.contains("appProperties")) f.sha = json::get_string(m["appProperties"], "sha256");
    return f;
  }

  Result<net::HttpResponse> send(const net::HttpRequest& req, const net::StreamSink* = nullptr, const CancelToken* = nullptr) override {
    std::lock_guard lk(mu);
    sent.push_back(req.method + " " + req.url);
    if (req.url.rfind("https://oauth2.googleapis.com/token", 0) == 0) {
      token_bodies.push_back(req.body);
      if (refresh_fails || param(req.body, "refresh_token") != "RT-secret/1" || param(req.body, "client_id") != "cid-1" ||
          param(req.body, "client_secret") != "csec-1" || param(req.body, "grant_type") != "refresh_token")
        return reply(400, R"({"error":"invalid_grant","error_description":"Bad"})");
      current = "at-" + std::to_string(++issued);
      return reply(200, json::dump(Json{{"access_token", current}, {"expires_in", expires_in}, {"token_type", "Bearer"}}));
    }
    if (net::header_value(req.headers, "Authorization") != "Bearer " + current) return reply(401, R"({"error":{"code":401}})");
    std::string url = req.url, query;
    if (auto q = url.find('?'); q != std::string::npos) { query = url.substr(q + 1); url = url.substr(0, q); }
    const std::string api = "https://www.googleapis.com/drive/v3/files", up = "https://www.googleapis.com/upload/drive/v3/files";
    if (url.rfind("https://upload.fake/session/", 0) == 0 && req.method == "PUT") {
      auto s = sessions.find(url);
      if (s == sessions.end()) return reply(404, "");
      std::string id = s->second.first.empty() ? "id" + std::to_string(++next_id) : s->second.first;
      store(id, s->second.second, req.body, net::header_value(req.headers, "Content-Type"));
      ++resumable_puts;
      sessions.erase(s);
      return reply(200, "{\"id\":\"" + id + "\"}");
    }
    if (url == up || url.rfind(up + "/", 0) == 0) {
      const std::string existing = url.size() > up.size() ? url.substr(up.size() + 1) : "";
      if (param(query, "uploadType") == "resumable") {
        std::string loc = "https://upload.fake/session/" + std::to_string(++next_id);
        sessions[loc] = {existing, meta_of(req.body)};
        return reply(200, "", {{"Location", loc}});
      }
      std::string ct = net::header_value(req.headers, "Content-Type");
      auto bp = ct.find("boundary=");
      if (bp == std::string::npos) return reply(400, "");
      std::string d = "--" + ct.substr(bp + 9), b = req.body;
      std::size_t p1 = b.find(d), h1 = b.find("\r\n\r\n", p1) + 4, p2 = b.find("\r\n" + d, h1);
      std::size_t h2 = b.find("\r\n\r\n", p2) + 4, p3 = b.rfind("\r\n" + d + "--");
      std::string part_hdr = b.substr(p2, h2 - p2), mime = part_hdr.substr(part_hdr.find("Content-Type: ") + 14);
      mime = mime.substr(0, mime.find("\r\n"));
      std::string id = existing.empty() ? "id" + std::to_string(++next_id) : existing;
      if (!existing.empty() && !files.count(existing)) return reply(404, "");
      store(id, meta_of(b.substr(h1, p2 - h1)), b.substr(h2, p3 - h2), mime);
      return reply(200, "{\"id\":\"" + id + "\"}");
    }
    if (url == api && req.method == "GET") {
      std::string q = param(query, "q"), want_name;
      bool by_name = q.rfind("name='", 0) == 0;
      if (by_name) { auto e = q.find("' and '"); want_name = unq(q.substr(6, e - 6)); }
      std::vector<std::pair<std::string, F>> m;
      for (auto& [id, f] : files) if (!by_name || f.name == want_name) m.emplace_back(id, f);
      std::size_t from = param(query, "pageToken").empty() ? 0 : std::stoul(param(query, "pageToken"));
      std::size_t end = std::min(m.size(), from + static_cast<std::size_t>(page));
      Json out{{"files", Json::array()}};
      for (std::size_t i = from; i < end; ++i) out["files"].push_back(file_json(m[i].first, m[i].second));
      if (end < m.size()) out["nextPageToken"] = std::to_string(end);
      return reply(200, json::dump(out));
    }
    if (url.rfind(api + "/", 0) == 0) {
      std::string id = url.substr(api.size() + 1);
      if (id == "root") return reply(200, R"({"id":"root"})");
      auto f = files.find(id);
      if (f == files.end()) return reply(404, R"({"error":{"code":404}})");
      if (req.method == "DELETE") { files.erase(f); return reply(204, ""); }
      if (req.method == "GET") return reply(200, param(query, "alt") == "media" ? f->second.content : json::dump(file_json(id, f->second)));
    }
    return reply(400, "unexpected request");
  }
};

// ── the contract every backend must satisfy ──────────────────────────
void run_contract(StorageBackend& b) {
  CAPTURE(b.kind());
  LOOM_REQUIRE_OK(b.probe());
  // Embedded NULs: size from the array (the literal is 13 bytes; a hard-coded 15 read past it).
  static constexpr char kBin[] = "\x00\x01\xff binary\r\n\x00";
  const std::string bin(kBin, sizeof(kBin) - 1);
  const std::string h1 = Sha256::hex("alpha");
  LOOM_REQUIRE_OK(b.put(cas_key(h1), "alpha", "text/plain"));
  LOOM_REQUIRE_OK(b.put("named/doc one & two+x.bin", bin, "application/octet-stream"));
  LOOM_REQUIRE_OK(b.put("named/deep/er/z.txt", "zzz"));
  // roundtrip, binary safety
  CHECK(unwrap(b.get(cas_key(h1))) == "alpha");
  CHECK(unwrap(b.get("named/doc one & two+x.bin")) == bin);
  // overwrite replaces
  LOOM_REQUIRE_OK(b.put("named/deep/er/z.txt", "ZZZZ"));
  CHECK(unwrap(b.get("named/deep/er/z.txt")) == "ZZZZ");
  // stat: size; sha256 identity when the backend reports it
  auto st = unwrap(b.stat(cas_key(h1)));
  CHECK(st.size == 5);
  if (!st.sha256.empty()) CHECK(st.sha256 == h1);
  LOOM_REQUIRE_OK(verify_object(b, cas_key(h1), h1));
  CHECK_FALSE(verify_object(b, cas_key(h1), Sha256::hex("other")));
  // missing
  auto miss = b.get("nope/x");
  REQUIRE_FALSE(miss);
  CHECK(miss.error().code == Errc::NotFound);
  CHECK(b.stat("nope/x").error().code == Errc::NotFound);
  // list: prefix filter, nested, sorted
  auto all = unwrap(b.list(""));
  std::vector<std::string> keys;
  for (auto& o : all) keys.push_back(o.key);
  CHECK(std::is_sorted(keys.begin(), keys.end()));
  CHECK(std::count(keys.begin(), keys.end(), "named/deep/er/z.txt") == 1);
  CHECK(std::count(keys.begin(), keys.end(), cas_key(h1)) == 1);
  auto named = unwrap(b.list("named/"));
  CHECK(named.size() == 2);
  CHECK(unwrap(b.list("zzz-none/")).empty());
  // remove is idempotent
  LOOM_REQUIRE_OK(b.remove("named/deep/er/z.txt"));
  LOOM_REQUIRE_OK(b.remove("named/deep/er/z.txt"));
  CHECK(b.get("named/deep/er/z.txt").error().code == Errc::NotFound);
  // keys are validated, never joined into escapes
  for (const char* bad : {"", "/abs", "a//b", "../x", "a/../b", "a/./b", "trail/"}) {
    CAPTURE(bad);
    CHECK(b.put(bad, "x").error().code == Errc::InvalidArgument);
    CHECK(b.get(bad).error().code == Errc::InvalidArgument);
  }
}

BackendEnv env_for(const std::filesystem::path& dir, Secrets* s = nullptr) {
  BackendEnv e;
  e.http = net::make_default_transport();
  e.secrets = s;
  e.data_dir = dir;
  return e;
}

void clear_proxy_env() { for (const char* v : {"HTTP_PROXY", "http_proxy", "HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy"}) ::unsetenv(v); }

}  // namespace

TEST_SUITE("storage") {
  TEST_CASE("key helpers") {
    CHECK(valid_key("a/b/c"));
    CHECK_FALSE(valid_key("a/../c"));
    CHECK_FALSE(valid_key("a\\b"));
    CHECK(cas_key(Sha256::hex("x")) == Sha256::hex("x").substr(0, 2) + "/" + Sha256::hex("x").substr(2, 2) + "/" + Sha256::hex("x"));
  }

  TEST_CASE("sigv4 matches the published AWS example") {
    // AWS S3 docs, "Signature Calculations for the Authorization Header", GET Object with Range.
    detail::SigV4In in;
    in.method = "GET";
    in.canonical_path = "/test.txt";
    in.payload_hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    in.amzdate = "20130524T000000Z";
    in.region = "us-east-1";
    in.secret_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
    in.headers = {{"host", "examplebucket.s3.amazonaws.com"}, {"range", "bytes=0-9"},
                  {"x-amz-content-sha256", in.payload_hash}, {"x-amz-date", in.amzdate}};
    CHECK(detail::sigv4_signature(in) == "f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41");
  }

  TEST_CASE("registry: builtin kinds, planned kind, unknown kind") {
    const auto& reg = BackendRegistry::builtin();
    auto kinds = reg.kinds();
    CHECK(std::count(kinds.begin(), kinds.end(), "local") == 1);
    CHECK(std::count(kinds.begin(), kinds.end(), "s3") == 1);
    CHECK(std::count(kinds.begin(), kinds.end(), "webdav") == 1);
    BackendEnv env;
    CHECK(std::count(kinds.begin(), kinds.end(), "gdrive") == 1);
    BackendRegistry planned;  // a kind declared without an adapter still reports NotImplemented
    planned.add({"tape", "planned", {}});
    CHECK(planned.create("g", Json{{"kind", "tape"}}, env).error().code == Errc::NotImplemented);
    CHECK(reg.create("x", Json{{"kind", "tape"}}, env).error().code == Errc::Unsupported);
    BackendRegistry mine;  // a plugin is just a registered factory
    mine.add({"alias", "implemented", detail::make_local});
    fsutil::TempDir td;
    auto be = unwrap(mine.create("a", Json{{"kind", "alias"}, {"path", td.path().string()}}, env));
    run_contract(*be);
  }

  TEST_CASE("contract: local") {
    fsutil::TempDir td;
    auto be = unwrap(BackendRegistry::builtin().create("loc", Json{{"kind", "local"}, {"path", "{dir}/store"}}, env_for(td.path())));
    run_contract(*be);
    CHECK(std::filesystem::exists(td.path() / "store" / cas_key(Sha256::hex("alpha"))));  // BlobStore layout
  }

  TEST_CASE("contract: s3 over a fake server, SigV4 verified server-side") {
    clear_proxy_env();
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("storage_s3a_access_key", kAccess);
    secrets.set("storage_s3a_secret_key", kSecret);
    FakeS3 fake;
    Json cfg{{"kind", "s3"}, {"endpoint", fake.srv->url()}, {"bucket", "bkt"}, {"prefix", "loom/p"}};
    auto be = unwrap(BackendRegistry::builtin().create("s3a", cfg, env_for(td.path(), &secrets)));
    run_contract(*be);
    CHECK(fake.bad_sigs == 0);
    CHECK(fake.objects.count("loom/p/" + cas_key(Sha256::hex("alpha"))) == 1);  // prefix applied, nothing outside it
    CHECK(unwrap(be->stat(cas_key(Sha256::hex("alpha")))).sha256 == Sha256::hex("alpha"));  // sha256 identity stored as metadata
  }

  TEST_CASE("registry: gdrive is implemented") {
    const auto* d = BackendRegistry::builtin().find("gdrive");
    REQUIRE(d != nullptr);
    CHECK(d->status == "implemented");
    BackendEnv env;  // no transport, no secrets: a clean error, never a crash
    auto be = unwrap(BackendRegistry::builtin().create("g", Json{{"kind", "gdrive"}}, env));
    CHECK(be->probe().error().code == Errc::Auth);
  }

  namespace {
  struct DriveRig {
    fsutil::TempDir td;
    Secrets secrets{td.path() / "secrets.json"};
    std::shared_ptr<FakeDrive> fake = std::make_shared<FakeDrive>();
    DriveRig() {
      secrets.set("storage_gd_client_id", "cid-1");
      secrets.set("storage_gd_client_secret", "csec-1");
      secrets.set("storage_gd_refresh_token", "RT-secret/1");
    }
    std::unique_ptr<StorageBackend> make(Json extra = Json::object()) {
      Json cfg{{"kind", "gdrive"}};
      for (auto it = extra.begin(); it != extra.end(); ++it) cfg[it.key()] = it.value();
      BackendEnv e;
      e.http = fake;
      e.secrets = &secrets;
      e.data_dir = td.path();
      return unwrap(BackendRegistry::builtin().create("gd", cfg, e));
    }
  };
  }  // namespace

  TEST_CASE("contract: gdrive over a fake transport, one token refresh serves the whole run") {
    DriveRig rig;
    auto be = rig.make();
    run_contract(*be);
    CHECK(rig.fake->issued == 1);
    // names are the full keys in the configured folder; sha256 identity is kept
    bool found = false;
    for (auto& [id, f] : rig.fake->files) if (f.name == cas_key(Sha256::hex("alpha"))) { found = true; CHECK(f.parent == "root"); CHECK(f.sha == Sha256::hex("alpha")); }
    CHECK(found);
    CHECK(unwrap(be->stat(cas_key(Sha256::hex("alpha")))).sha256 == Sha256::hex("alpha"));
    // overwrite does not create duplicates
    std::size_t n = rig.fake->files.size();
    LOOM_REQUIRE_OK(be->put("named/again", "1"));
    LOOM_REQUIRE_OK(be->put("named/again", "22"));
    CHECK(rig.fake->files.size() == n + 1);
    CHECK(unwrap(be->get("named/again")) == "22");
    // a quote in a key are escaped in the Drive query
    LOOM_REQUIRE_OK(be->put("q/it's a b", "x"));
    CHECK(unwrap(be->get("q/it's a b")) == "x");
    // the folder and endpoints are data
    auto custom = rig.make(Json{{"root_folder", "folder9"}});
    LOOM_REQUIRE_OK(custom->put("k/v", "x"));
    bool in_folder = false;
    for (auto& [id, f] : rig.fake->files) if (f.name == "k/v") in_folder = f.parent == "folder9";
    CHECK(in_folder);
  }

  TEST_CASE("gdrive: resumable session above the configured threshold") {
    DriveRig rig;
    auto be = rig.make(Json{{"resumable_threshold", 10}});
    std::string big(100, 'z');
    LOOM_REQUIRE_OK(be->put("big/one", big));
    CHECK(rig.fake->resumable_puts == 1);
    CHECK(unwrap(be->get("big/one")) == big);
    LOOM_REQUIRE_OK(be->put("big/one", big + "!"));  // replace through the same path
    CHECK(rig.fake->resumable_puts == 2);
    CHECK(unwrap(be->get("big/one")) == big + "!");
    CHECK(unwrap(be->list("big/")).size() == 1);
    LOOM_REQUIRE_OK(be->put("small", "tiny"));
    CHECK(rig.fake->resumable_puts == 2);
  }

  TEST_CASE("gdrive: access token refreshed on expiry (skew) and on 401, kept in memory only") {
    {
      DriveRig rig;
      rig.fake->expires_in = 30;  // shorter than the 60 s skew preset: always stale
      auto be = rig.make();
      LOOM_REQUIRE_OK(be->put("a/b", "x"));
      int after_put = rig.fake->issued;
      CHECK(after_put >= 2);  // lookup + upload each needed a fresh token
      CHECK(unwrap(be->get("a/b")) == "x");
      CHECK(rig.fake->issued > after_put);
    }
    {
      DriveRig rig;
      rig.fake->expires_in = 30;
      auto be = rig.make(Json{{"token_skew_s", 0}});  // skew is a setting: now the token lasts
      LOOM_REQUIRE_OK(be->put("a/b", "x"));
      CHECK(unwrap(be->get("a/b")) == "x");
      CHECK(rig.fake->issued == 1);
    }
    DriveRig rig;
    auto be = rig.make();
    LOOM_REQUIRE_OK(be->put("a/b", "x"));
    CHECK(rig.fake->issued == 1);
    rig.fake->revoke();  // server drops the token before its announced expiry
    CHECK(unwrap(be->get("a/b")) == "x");
    CHECK(rig.fake->issued == 2);  // exactly one refresh, request retried once
    // the token is never written to the secrets file or the data dir
    std::string on_disk;
    for (auto& e : std::filesystem::recursive_directory_iterator(rig.td.path())) {
      if (!e.is_regular_file()) continue;
      std::ifstream in(e.path(), std::ios::binary);
      on_disk.append(std::istreambuf_iterator<char>(in), {});
    }
    CHECK(on_disk.find("at-1") == std::string::npos);
    CHECK(on_disk.find("at-2") == std::string::npos);
  }

  TEST_CASE("gdrive: failures map to codes and the refresh token never appears in outputs or logs") {
    std::vector<std::string> captured;
    std::mutex mu;
    log::Level old = log::level();
    log::set_level(log::Level::Debug);
    int tok = log::add_sink([&](const log::Record& r) { std::lock_guard lk(mu); captured.push_back(r.formatted()); });
    std::string joined;
    {
      DriveRig rig;
      auto be = rig.make();
      LOOM_REQUIRE_OK(be->put("a/b", "x"));
      (void)be->get("a/b");
      auto missing = be->get("nope/x");
      REQUIRE_FALSE(missing);
      CHECK(missing.error().code == Errc::NotFound);
      joined += missing.error().message + json::dump(unwrap(be->stat("a/b")).to_json());
      // refresh rejected (revoked grant): Auth, with the OAuth error code only
      rig.fake->refresh_fails = true;
      rig.fake->revoke();
      auto bad = be->get("a/b");
      REQUIRE_FALSE(bad);
      CHECK(bad.error().code == Errc::Auth);
      CHECK(bad.error().message.find("invalid_grant") != std::string::npos);
      joined += bad.error().message;
      // missing secrets
      Secrets none(rig.td.path() / "none.json");
      BackendEnv e;
      e.http = rig.fake; e.secrets = &none; e.data_dir = rig.td.path();
      auto nb = unwrap(BackendRegistry::builtin().create("gd", Json{{"kind", "gdrive"}}, e));
      auto nerr = nb->probe();
      REQUIRE_FALSE(nerr);
      CHECK(nerr.error().code == Errc::Auth);
      joined += nerr.error().message;
      // the service description and probe never show credentials
      Json v = unwrap(RuntimeProfile::builtin("storage_security")).values();
      v["storages"]["drive"] = Json{{"class", "user_cloud"}, {"backend", Json{{"kind", "gdrive"}, {"secret_ref", "storage_gd"}}}};
      BackendEnv se; se.http = rig.fake; se.secrets = &rig.secrets; se.data_dir = rig.td.path();
      rig.fake->refresh_fails = false;
      StorageService svc(v, se);
      joined += json::dump(svc.describe()) + json::dump(svc.probe_all());
      // only the token endpoint ever receives the refresh token
      for (const auto& s : rig.fake->sent) joined += s;
      for (const auto& t : rig.fake->token_bodies) CHECK(t.find("RT-secret") != std::string::npos);
      CHECK(rig.fake->token_bodies.size() >= 2);
    }
    {
      std::lock_guard lk(mu);
      for (auto& l : captured) joined += l;
    }
    log::remove_sink(tok);
    log::set_level(old);
    for (const char* secret : {"RT-secret", "RT%2Dsecret", "csec-1", "cid-1", "at-1", "at-2", "Bearer"}) {
      CAPTURE(secret);
      CHECK(joined.find(secret) == std::string::npos);
    }
  }

  TEST_CASE("s3 multipart: part boundaries, completion and the threshold switch") {
    clear_proxy_env();
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("storage_s3a_access_key", kAccess);
    secrets.set("storage_s3a_secret_key", kSecret);
    FakeS3 fake;
    Json cfg{{"kind", "s3"}, {"endpoint", fake.srv->url()}, {"bucket", "bkt"}, {"multipart_part_size", 10}};
    auto be = unwrap(BackendRegistry::builtin().create("s3a", cfg, env_for(td.path(), &secrets)));
    // at or below the threshold: the unchanged single PUT
    LOOM_REQUIRE_OK(be->put("small/exact", std::string(10, 'a'), "text/plain"));
    CHECK(fake.plain_puts == 1);
    CHECK(fake.created == 0);
    // above: 25 bytes = 10 + 10 + 5, stitched in order, hash metadata of the whole object
    std::string data;
    for (int i = 0; i < 25; ++i) data += static_cast<char>('A' + i);
    LOOM_REQUIRE_OK(be->put("big/obj one", data, "application/octet-stream"));
    CHECK(fake.plain_puts == 1);
    CHECK(fake.created == 1);
    CHECK(fake.completed == 1);
    CHECK(fake.aborted == 0);
    CHECK(fake.part_sizes == std::vector<std::size_t>{10, 10, 5});
    CHECK(fake.objects["big/obj one"] == data);
    CHECK(fake.meta_sha["big/obj one"] == Sha256::hex(data));
    CHECK(unwrap(be->get("big/obj one")) == data);
    CHECK(unwrap(be->stat("big/obj one")).sha256 == Sha256::hex(data));
    CHECK(fake.bad_sigs == 0);
    // an exact multiple has no empty trailing part
    fake.part_sizes.clear();
    LOOM_REQUIRE_OK(be->put("big/even", std::string(20, 'e')));
    CHECK(fake.part_sizes == std::vector<std::size_t>{10, 10});
    // threshold is separate from the part size; negative turns multipart off
    auto hi = unwrap(BackendRegistry::builtin().create("s3a", Json{{"kind", "s3"}, {"endpoint", fake.srv->url()}, {"bucket", "bkt"}, {"multipart_part_size", 10}, {"multipart_threshold", 100}}, env_for(td.path(), &secrets)));
    int created = fake.created;
    LOOM_REQUIRE_OK(hi->put("mid/obj", std::string(50, 'm')));
    CHECK(fake.created == created);
    auto off = unwrap(BackendRegistry::builtin().create("s3a", Json{{"kind", "s3"}, {"endpoint", fake.srv->url()}, {"bucket", "bkt"}, {"multipart_part_size", 10}, {"multipart_threshold", -1}}, env_for(td.path(), &secrets)));
    LOOM_REQUIRE_OK(off->put("off/obj", std::string(500, 'o')));
    CHECK(fake.created == created);
  }

  TEST_CASE("s3 multipart: a failing part aborts the upload and leaves no object") {
    clear_proxy_env();
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("storage_s3a_access_key", kAccess);
    secrets.set("storage_s3a_secret_key", kSecret);
    FakeS3 fake;
    Json cfg{{"kind", "s3"}, {"endpoint", fake.srv->url()}, {"bucket", "bkt"}, {"multipart_part_size", 10}};
    auto be = unwrap(BackendRegistry::builtin().create("s3a", cfg, env_for(td.path(), &secrets)));
    fake.fail_part = 2;
    auto st = be->put("big/fail", std::string(35, 'f'));
    REQUIRE_FALSE(st);
    CHECK(st.error().code == Errc::Http);
    CHECK(fake.created == 1);
    CHECK(fake.aborted == 1);
    CHECK(fake.uploads.empty());
    CHECK(fake.objects.count("big/fail") == 0);
    // an <Error> inside a 200 completion is a failure too, also aborted
    fake.fail_part = 0;
    fake.complete_error_body = true;
    auto st2 = be->put("big/fail2", std::string(35, 'f'));
    REQUIRE_FALSE(st2);
    CHECK(fake.aborted == 2);
    CHECK(fake.objects.count("big/fail2") == 0);
    CHECK(json::dump(Json(st2.error().message)).find(kSecret) == std::string::npos);
  }

  TEST_CASE("s3 multipart: part size grows to respect the 10000 part limit") {
    clear_proxy_env();
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("storage_s3a_access_key", kAccess);
    secrets.set("storage_s3a_secret_key", kSecret);
    FakeS3 fake;
    auto be = unwrap(BackendRegistry::builtin().create("s3a", Json{{"kind", "s3"}, {"endpoint", fake.srv->url()}, {"bucket", "bkt"}, {"multipart_part_size", 1}, {"multipart_threshold", 0}}, env_for(td.path(), &secrets)));
    LOOM_REQUIRE_OK(be->put("lim/obj", std::string(10001, 'x')));
    CHECK(fake.part_sizes.size() <= 10000);
    CHECK(fake.objects["lim/obj"].size() == 10001);
  }

  TEST_CASE("contract: webdav over a fake server") {
    clear_proxy_env();
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("storage_dav_user", "alice");
    secrets.set("storage_dav_password", "pw-never-log");
    FakeDav fake;
    Json cfg{{"kind", "webdav"}, {"url", fake.srv->url() + "/dav"}, {"prefix", "chatadhd"}};
    fake.colls.insert("chatadhd");
    auto be = unwrap(BackendRegistry::builtin().create("dav", cfg, env_for(td.path(), &secrets)));
    run_contract(*be);
    CHECK(fake.files.count("chatadhd/" + cas_key(Sha256::hex("alpha"))) == 1);
  }

  TEST_CASE("network errors map to codes and never leak credentials into messages or logs") {
    clear_proxy_env();
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("storage_s3a_access_key", kAccess);
    secrets.set("storage_s3a_secret_key", "WRONG-" + std::string(kSecret));
    secrets.set("storage_dav_user", "alice");
    secrets.set("storage_dav_password", "pw-never-log");
    std::vector<std::string> captured;
    std::mutex mu;
    log::Level old = log::level();
    log::set_level(log::Level::Debug);
    int tok = log::add_sink([&](const log::Record& r) { std::lock_guard lk(mu); captured.push_back(r.formatted()); });
    FakeS3 fake;
    FakeDav dav;
    auto s3 = unwrap(BackendRegistry::builtin().create("s3a", Json{{"kind", "s3"}, {"endpoint", fake.srv->url()}, {"bucket", "bkt"}}, env_for(td.path(), &secrets)));
    auto err = s3->put("k/v", "data");
    REQUIRE_FALSE(err);
    CHECK(err.error().code == Errc::Auth);
    Secrets none(td.path() / "none.json");
    auto s3b = unwrap(BackendRegistry::builtin().create("s3b", Json{{"kind", "s3"}, {"endpoint", fake.srv->url()}, {"bucket", "bkt"}}, env_for(td.path(), &none)));
    CHECK(s3b->probe().error().code == Errc::Auth);
    auto d = unwrap(BackendRegistry::builtin().create("dav", Json{{"kind", "webdav"}, {"url", dav.srv->url() + "/dav"}}, env_for(td.path(), &secrets)));
    LOOM_REQUIRE_OK(d->put("a/b", "x"));
    // unreachable endpoint
    auto dead = unwrap(BackendRegistry::builtin().create("s3a", Json{{"kind", "s3"}, {"endpoint", "http://127.0.0.1:1"}, {"bucket", "bkt"}}, env_for(td.path(), &secrets)));
    CHECK_FALSE(dead->probe());
    // StorageService degrade path logs too
    std::string joined = err.error().message;
    {
      std::lock_guard lk(mu);
      for (auto& l : captured) joined += l;
    }
    log::remove_sink(tok);
    log::set_level(old);
    for (const char* secret : {kSecret, "WRONG-", "pw-never-log", "YWxpY2U6"}) {
      CAPTURE(secret);
      CHECK(joined.find(secret) == std::string::npos);
    }
  }

  // ── placement + service ──────────────────────────────────────────
  Json patched_values(const std::filesystem::path& dir_a, const std::filesystem::path& dir_b, const Json& blobs_placement) {
    auto prof = unwrap(RuntimeProfile::builtin("storage_security"));
    Json v = prof.values();
    v["storages"]["mir_a"] = Json{{"class", "local"}, {"path", dir_a.string()}};
    v["storages"]["mir_b"] = Json{{"class", "local"}, {"path", dir_b.string()}};
    v["placement"]["blobs"] = blobs_placement;
    return v;
  }

  TEST_CASE("default placement is today's layout, byte for byte") {
    fsutil::TempDir td;
    auto svc = unwrap(StorageService::load(env_for(td.path())));
    for (const char* cat : {"blobs", "attachments", "exports", "conversations", "config", "secrets"}) {
      CAPTURE(cat);
      CHECK(svc->is_default_local(cat));
    }
    auto h = unwrap(svc->put_cas("blobs", "hello world", "text/plain"));
    CHECK(h == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
    CHECK(std::filesystem::exists(td.path() / "blobs" / "b9" / "4d" / h));  // exactly BlobStore::path_for
    CHECK(unwrap(fsutil::read_file(td.path() / "blobs" / "b9" / "4d" / h)) == "hello world");
    auto d = svc->describe();
    CHECK(d["categories"].size() >= 10);
    CHECK(d["backends"].size() >= 3);
  }

  TEST_CASE("mirror: write quorum 1 degrades, read falls back in preference order") {
    clear_proxy_env();
    fsutil::TempDir td, a, b;
    Json pl = Json::array({Json{{"storage", "mir_a"}, {"role", "primary"}}, Json{{"storage", "mir_b"}, {"role", "replica"}}});
    StorageService svc(patched_values(a.path(), b.path(), pl), env_for(td.path()));
    auto h = unwrap(svc.put_cas("blobs", "mirrored bytes"));
    CHECK(std::filesystem::exists(a.path() / "blobs" / cas_key(h)));
    CHECK(std::filesystem::exists(b.path() / "blobs" / cas_key(h)));
    std::filesystem::remove(a.path() / "blobs" / cas_key(h));  // primary lost the object
    CHECK(unwrap(svc.get("blobs", cas_key(h))) == "mirrored bytes");  // served by the replica
    // replica unreachable: the write still succeeds on the primary
    auto v = patched_values(a.path(), b.path(), pl);
    v["storages"]["mir_b"] = Json{{"class", "cloud"}, {"backend", Json{{"kind", "s3"}, {"endpoint", "http://127.0.0.1:1"}, {"bucket", "x"}}}};
    StorageService degraded(v, env_for(td.path()));
    LOOM_REQUIRE_OK(degraded.put("blobs", "aa/bb/other", "x"));
    CHECK(std::filesystem::exists(a.path() / "blobs" / "aa" / "bb" / "other"));
    // all targets down -> error
    v["storages"]["mir_a"] = v["storages"]["mir_b"];
    StorageService down(v, env_for(td.path()));
    CHECK_FALSE(down.put("blobs", "aa/bb/other", "x"));
  }

  TEST_CASE("transfer: copy verifies sha256, persists placement, move + delete_source only on flag") {
    fsutil::TempDir td, dst;
    auto db = open_db(td.path() / "p.db");
    auto svc = unwrap(StorageService::load(env_for(td.path()), db.get()));
    std::vector<std::string> hashes;
    for (const char* t : {"one", "two", "three"}) hashes.push_back(unwrap(svc->put_cas("blobs", t)));
    // add a user storage through the profile overlay
    unwrap(fsutil::ensure_dir(td.path() / "profiles"));
    unwrap(fsutil::atomic_write(td.path() / "profiles" / "storage_security.pack",
        json::dump(Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "storage_security"},
                        {"overrides", Json{{"storages", Json{{"usb", Json{{"class", "local"}, {"path", dst.path().string()}}}}}}}})));
    svc = unwrap(StorageService::load(env_for(td.path()), db.get()));
    StorageService::MoveOptions o;
    o.category = "blobs"; o.to = "usb";
    auto dry = unwrap(svc->transfer([&] { auto d = o; d.dry_run = true; return d; }()));
    CHECK(dry["objects"] == 3);
    CHECK_FALSE(std::filesystem::exists(dst.path() / "blobs"));
    auto rep = unwrap(svc->transfer(o));  // copy
    CHECK(rep["copied"] == 3);
    for (auto& h : hashes) {
      CHECK(std::filesystem::exists(dst.path() / "blobs" / cas_key(h)));
      CHECK(std::filesystem::exists(td.path() / "blobs" / cas_key(h)));  // source stays
    }
    auto pl = svc->placement("blobs");
    REQUIRE(pl.size() == 2);
    CHECK(pl[0].storage == "local_dir");
    CHECK(pl[1].storage == "usb");
    CHECK(pl[1].role == "replica");
    // placement persisted as data
    auto again = unwrap(StorageService::load(env_for(td.path()), db.get()));
    CHECK(again->placement("blobs").size() == 2);
    // provenance: the graph knows where a hash lives
    auto loc = svc->locate(hashes[0]);
    std::set<std::string> where;
    for (auto& l : loc) where.insert(l["storage"].get<std::string>());
    CHECK(where.count("local_dir") == 1);
    CHECK(where.count("usb") == 1);
    // delete_source needs move
    auto bad = o; bad.delete_source = true;
    CHECK(svc->transfer(bad).error().code == Errc::InvalidArgument);
    // move without delete keeps the source files
    auto mv = o; mv.move = true;
    auto r2 = unwrap(svc->transfer(mv));
    CHECK(r2["already_present"] == 3);
    CHECK(svc->placement("blobs").size() == 1);
    CHECK(std::filesystem::exists(td.path() / "blobs" / cas_key(hashes[0])));
    // move back with explicit delete
    auto back = unwrap(StorageService::load(env_for(td.path()), db.get()));
    StorageService::MoveOptions mb; mb.category = "blobs"; mb.to = "local_dir"; mb.move = true; mb.delete_source = true;
    auto r3 = unwrap(back->transfer(mb));
    CHECK(r3["removed_from_source"] == 3);
    CHECK_FALSE(std::filesystem::exists(dst.path() / "blobs" / cas_key(hashes[0])));
    CHECK(std::filesystem::exists(td.path() / "blobs" / cas_key(hashes[0])));
    CHECK(back->is_default_local("blobs"));
  }

  // A plugin that misbehaves on demand: fails or silently corrupts a write.
  class FlakyBackend final : public StorageBackend {
   public:
    FlakyBackend(std::string id, std::unique_ptr<StorageBackend> inner, int fail_at, bool corrupt)
        : id_(std::move(id)), inner_(std::move(inner)), fail_at_(fail_at), corrupt_(corrupt) {}
    const std::string& id() const noexcept override { return id_; }
    std::string kind() const override { return "flaky"; }
    Status put(std::string_view k, std::string_view b, std::string_view m) override {
      if (++puts_ == fail_at_) {
        if (!corrupt_) return Error(Errc::Network, "boom");
        return inner_->put(k, std::string(b) + "!", m);
      }
      return inner_->put(k, b, m);
    }
    Result<std::string> get(std::string_view k) const override { return inner_->get(k); }
    Result<ObjectStat> stat(std::string_view k) const override { return inner_->stat(k); }
    Result<std::vector<ObjectStat>> list(std::string_view p) const override { return inner_->list(p); }
    Status remove(std::string_view k) override { return inner_->remove(k); }
    Status probe() const override { return inner_->probe(); }
    std::string id_; std::unique_ptr<StorageBackend> inner_; int fail_at_; bool corrupt_; int puts_ = 0;
  };

  TEST_CASE("transfer: failure or corruption rolls back, placement and source untouched") {
    for (bool corrupt : {false, true}) {
      CAPTURE(corrupt);
      fsutil::TempDir td, dst;
      BackendRegistry reg = BackendRegistry::builtin();
      reg.add({"flaky", "implemented", [corrupt, &dst](const std::string& id, const Json&, const BackendEnv& env) -> Result<std::unique_ptr<StorageBackend>> {
        LOOM_TRY_ASSIGN(auto inner, detail::make_local(id, Json{{"path", dst.path().string()}}, env));
        return std::unique_ptr<StorageBackend>(new FlakyBackend(id, std::move(inner), 3, corrupt));
      }});
      auto prof = unwrap(RuntimeProfile::builtin("storage_security"));
      Json v = prof.values();
      v["storages"]["flaky_store"] = Json{{"class", "cloud"}, {"backend", Json{{"kind", "flaky"}}}};
      StorageService svc(v, env_for(td.path()), &reg);
      std::vector<std::string> hs;
      for (const char* t : {"a1", "a2", "a3", "a4"}) hs.push_back(unwrap(svc.put_cas("blobs", t)));
      StorageService::MoveOptions o; o.category = "blobs"; o.to = "flaky_store"; o.move = true; o.delete_source = true;
      auto r = svc.transfer(o);
      REQUIRE_FALSE(r);
      CHECK(svc.is_default_local("blobs"));  // not switched
      for (auto& h : hs) {
        CHECK(std::filesystem::exists(td.path() / "blobs" / cas_key(h)));  // source intact
        CHECK_FALSE(std::filesystem::exists(dst.path() / "blobs" / cas_key(h)));  // partial copies removed
      }
      CHECK_FALSE(std::filesystem::exists(td.path() / "profiles" / "storage_security.pack"));
    }
  }

  TEST_CASE("transfer respects allowed_storages") {
    fsutil::TempDir td, dst;
    auto prof = unwrap(RuntimeProfile::builtin("storage_security"));
    Json v = prof.values();
    v["storages"]["usb"] = Json{{"class", "local"}, {"path", dst.path().string()}};
    v["allowed_storages"]["blobs"] = Json{{"state", "only"}, {"storages", Json::array({"local_dir"})}};
    StorageService svc(v, env_for(td.path()));
    unwrap(svc.put_cas("blobs", "x"));
    StorageService::MoveOptions o; o.category = "blobs"; o.to = "usb";
    CHECK(svc.transfer(o).error().code == Errc::Conflict);
  }

  TEST_CASE("backup: database snapshot to any backend, verified") {
    fsutil::TempDir td, dst;
    auto db = open_db(td.path() / "p.db");
    auto prof = unwrap(RuntimeProfile::builtin("storage_security"));
    Json v = prof.values();
    v["storages"]["usb"] = Json{{"class", "local"}, {"path", dst.path().string()}};
    StorageService svc(v, env_for(td.path()));
    svc.set_database(db.get());
    auto r = unwrap(svc.backup_database("usb"));
    std::string key = r["key"].get<std::string>();
    CHECK(key.rfind("backups/chatadhd-", 0) == 0);
    auto bytes = unwrap(fsutil::read_file(dst.path() / key));
    CHECK(Sha256::hex(bytes) == r["sha256"].get<std::string>());
    CHECK(bytes.rfind("SQLite format 3", 0) == 0);
    CHECK(svc.backup_database("nope").error().code == Errc::NotFound);
  }

  TEST_CASE("probe_all: reachability known per object store, unknown otherwise; no secrets") {
    clear_proxy_env();
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("storage_cloud_access_key", kAccess);
    secrets.set("storage_cloud_secret_key", kSecret);
    FakeS3 fake;
    auto prof = unwrap(RuntimeProfile::builtin("storage_security"));
    Json v = prof.values();
    v["storages"]["cloud"] = Json{{"class", "user_cloud"}, {"backend", Json{{"kind", "s3"}, {"endpoint", fake.srv->url()}, {"bucket", "bkt"}}}};
    v["storages"]["drive"] = Json{{"class", "user_cloud"}, {"backend", Json{{"kind", "gdrive"}}}};
    StorageService svc(v, env_for(td.path(), &secrets));
    Json p = svc.probe_all();
    std::map<std::string, Json> by;
    for (auto& b : p["backends"]) by[b["id"].get<std::string>()] = b;
    CHECK(by["cloud"]["reachable"]["ok"] == true);
    CHECK(by["cloud"]["credentials_present"] == true);
    CHECK(by["drive"]["adapter"] == "implemented");
    CHECK(by["drive"]["reachable"]["known"] == true);
    CHECK(by["drive"]["reachable"]["ok"] == false);  // no OAuth secrets set: a reported failure, not a crash
    CHECK(by["browser"]["reachable"]["known"] == false);
    CHECK(json::dump(p).find(kSecret) == std::string::npos);
    CHECK(json::dump(p).find(kAccess) == std::string::npos);
  }

  TEST_CASE("BlobStore through the registry: replicate, fall back, hydrate, remote-only") {
    fsutil::TempDir td, usb;
    auto db = open_db(td.path() / "p.db");
    auto prof = unwrap(RuntimeProfile::builtin("storage_security"));
    Json v = prof.values();
    v["storages"]["usb"] = Json{{"class", "local"}, {"path", usb.path().string()}};
    auto both = Json::array({Json{{"storage", "local_dir"}, {"role", "primary"}}, Json{{"storage", "usb"}, {"role", "replica"}}});
    v["placement"]["blobs"] = both;
    StorageService svc(v, env_for(td.path()));
    svc.set_database(db.get());
    BlobStore blobs(td.path() / "blobs", *db);
    blobs.set_placement(&svc);
    auto ref = unwrap(blobs.put("hello world", "text/plain"));
    CHECK(ref.hash == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
    CHECK(std::filesystem::exists(blobs.path_for(ref.hash)));                     // local layout unchanged
    CHECK(std::filesystem::exists(usb.path() / "blobs" / cas_key(ref.hash)));      // replica
    CHECK(svc.locate(ref.hash).size() == 2);
    // local copy lost: still addressable by hash, hydrated on read
    std::filesystem::permissions(blobs.path_for(ref.hash), std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
    std::filesystem::remove(blobs.path_for(ref.hash));
    CHECK(blobs.has(ref.hash));
    CHECK(unwrap(blobs.read(ref.hash)) == "hello world");
    CHECK(std::filesystem::exists(blobs.path_for(ref.hash)));
    LOOM_REQUIRE_OK(blobs.verify(ref.hash));
    // streamed file is replicated too
    unwrap(fsutil::atomic_write(td.path() / "f.bin", "file payload"));
    auto fr = unwrap(blobs.put_file(td.path() / "f.bin"));
    CHECK(std::filesystem::exists(usb.path() / "blobs" / cas_key(fr.hash)));
    // corrupted replica is detected, not served
    std::filesystem::remove(blobs.path_for(fr.hash));
    unwrap(fsutil::atomic_write(usb.path() / "blobs" / cas_key(fr.hash), "tampered"));
    CHECK(blobs.read(fr.hash).error().code == Errc::Conflict);

    // remote-only: local_dir is not a target, nothing is written to the local layout
    v["placement"]["blobs"] = Json::array({Json{{"storage", "usb"}, {"role", "primary"}}});
    StorageService only(v, env_for(td.path()));
    BlobStore rblobs(td.path() / "rblobs", *db);
    rblobs.set_placement(&only);
    auto rr = unwrap(rblobs.put("remote only"));
    CHECK_FALSE(std::filesystem::exists(rblobs.path_for(rr.hash)));
    CHECK(std::filesystem::exists(usb.path() / "blobs" / cas_key(rr.hash)));
    CHECK(rblobs.has(rr.hash));
    CHECK(unwrap(rblobs.read(rr.hash)) == "remote only");
    CHECK(std::filesystem::exists(rblobs.local_path(rr.hash)));
  }

  TEST_CASE("graph entry: storage.probe reports local reachability and leaves network stores unprobed") {
    fsutil::TempDir td;
    graph_entry::Env env;
    env.data_dir = td.path();
    auto entry = unwrap(graph_entry::compute(env));
    const std::string dumped = json::dump(entry);
    CHECK(dumped.find("storage_backends_reachable") != std::string::npos);
    CHECK(dumped.find("storage_backends_unprobed") != std::string::npos);
  }

  TEST_CASE("command surface: Runtime + the existing C ABI JSON entry; per-data-dir; no secrets in results") {
    fsutil::TempDir td, usb;
    unwrap(fsutil::ensure_dir(td.path() / "profiles"));
    unwrap(fsutil::atomic_write(td.path() / "profiles" / "storage_security.pack",
        json::dump(Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "storage_security"},
                        {"overrides", Json{{"storages", Json{{"usb", Json{{"class", "local"}, {"path", usb.path().string()}}}}}}}})));
    std::string opts = Json{{"data_dir", td.path().string()}, {"start_workers", false}}.dump();
    const char* err = nullptr;
    LoomContext* ctx = loom_init_ex(opts.c_str(), &err);
    REQUIRE(ctx != nullptr);
    auto call = [&](const Json& req) {
      const char* out = loom_github_sync(ctx, req.dump().c_str());
      Json j = Json::parse(out ? out : "null", nullptr, false);
      loom_free_string(out);
      return j;
    };
    auto st = call({{"action", "storage.status"}});
    bool saw_usb = false;
    for (auto& b : st["backends"]) saw_usb = saw_usb || b["id"] == "usb";
    CHECK(saw_usb);
    auto probe = call({{"action", "storage.probe"}});
    CHECK(probe["backends"].size() >= 3);
    CHECK(call({{"action", "storage.copy"}, {"category", "blobs"}, {"to", "usb"}, {"dry_run", true}})["dry_run"] == true);
    auto bad = call({{"action", "storage.copy"}, {"category", "blobs"}, {"to", "nowhere"}});
    CHECK(bad.dump().find("nowhere") != std::string::npos);
    CHECK(call({{"action", "storage.placement"}, {"category", "blobs"}})["default_placement"] == true);
    loom_shutdown(ctx);
  }
}
