// Storage backend contract suite + placement/transfer behaviour (P4).
// Network backends run against in-process fake servers on 127.0.0.1 (no real network).
#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <thread>

#include "loom/config.h"
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
    if (r.method == "PUT") { objects[key] = r.body; meta_sha[key] = r.h.count("x-amz-meta-sha256") ? r.h.at("x-amz-meta-sha256") : ""; return o; }
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

// ── the contract every backend must satisfy ──────────────────────────
void run_contract(StorageBackend& b) {
  CAPTURE(b.kind());
  LOOM_REQUIRE_OK(b.probe());
  const std::string bin("\x00\x01\xff binary\r\n\x00", 15);
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

  TEST_CASE("registry: builtin kinds, planned gdrive, unknown kind") {
    const auto& reg = BackendRegistry::builtin();
    auto kinds = reg.kinds();
    CHECK(std::count(kinds.begin(), kinds.end(), "local") == 1);
    CHECK(std::count(kinds.begin(), kinds.end(), "s3") == 1);
    CHECK(std::count(kinds.begin(), kinds.end(), "webdav") == 1);
    BackendEnv env;
    CHECK(reg.create("g", Json{{"kind", "gdrive"}}, env).error().code == Errc::NotImplemented);
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
    CHECK(by["drive"]["adapter"] == "planned");
    CHECK(by["drive"]["reachable"]["known"] == false);
    CHECK(by["browser"]["reachable"]["known"] == false);
    CHECK(json::dump(p).find(kSecret) == std::string::npos);
    CHECK(json::dump(p).find(kAccess) == std::string::npos);
  }
}
