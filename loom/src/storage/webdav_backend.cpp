#include <algorithm>
#include <set>

#include "loom/config.h"
#include "loom/util/base64.h"
#include "loom/util/sha256.h"
#include "storage_internal.h"

namespace loom::storage::detail {
namespace {

class WebDavBackend final : public StorageBackend {
 public:
  WebDavBackend(std::string id, const Json& cfg, const BackendEnv& env) : id_(std::move(id)), env_(env) {
    base_ = json::get_string(cfg, "url");
    while (!base_.empty() && base_.back() == '/') base_.pop_back();
    prefix_ = json::get_string(cfg, "prefix");
    secret_ref_ = json::get_string(cfg, "secret_ref", "storage_" + id_);
  }
  const std::string& id() const noexcept override { return id_; }
  std::string kind() const override { return "webdav"; }

  Status put(std::string_view key, std::string_view bytes, std::string_view mime) override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY(make_parents(join_key(prefix_, key)));
    net::HttpRequest r = base_req("PUT", join_key(prefix_, key));
    if (!mime.empty()) r.headers.emplace_back("Content-Type", std::string(mime));
    r.body = std::string(bytes);
    LOOM_TRY(send(r));
    return {};
  }
  Result<std::string> get(std::string_view key) const override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY_ASSIGN(auto r, send(base_req("GET", join_key(prefix_, key))));
    return std::move(r.body);
  }
  Result<ObjectStat> stat(std::string_view key) const override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY_ASSIGN(auto r, send(base_req("HEAD", join_key(prefix_, key))));
    ObjectStat s;
    s.key = std::string(key);
    try { s.size = std::stoll(r.header("content-length")); } catch (...) { s.size = 0; }
    s.mime = r.header("content-type");
    return s;  // WebDAV has no standard content hash; verification re-reads the object
  }
  Result<std::vector<ObjectStat>> list(std::string_view prefix) const override {
    std::vector<ObjectStat> out;
    const std::string root = join_key(prefix_, "");
    std::vector<std::string> todo{root};  // collection paths relative to base, "" = base
    std::set<std::string> seen;
    while (!todo.empty()) {
      std::string coll = todo.back();
      todo.pop_back();
      if (!seen.insert(coll).second) continue;
      net::HttpRequest r = base_req("PROPFIND", coll);
      r.headers.emplace_back("Depth", "1");
      r.headers.emplace_back("Content-Type", "application/xml");
      r.body = "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:getcontentlength/><d:resourcetype/></d:prop></d:propfind>";
      auto resp = send(r, {207, 404});
      if (!resp) return Error(resp.error());
      if (resp->status == 404) continue;
      for (std::string_view e : xml_elements(resp->body, "response")) {
        std::string href = decode(xml_text(e, "href"));
        std::string rel = relative_to_base(href);
        while (!rel.empty() && rel.back() == '/') rel.pop_back();
        if (rel.empty() || rel == coll) continue;
        const bool is_coll = !xml_elements(e, "collection").empty();
        if (is_coll) { todo.push_back(rel); continue; }
        std::string key = rel;
        if (!root.empty()) {
          if (key.rfind(root + "/", 0) != 0) continue;
          key = key.substr(root.size() + 1);
        }
        if (key.size() < prefix.size() || key.compare(0, prefix.size(), prefix) != 0) continue;
        ObjectStat s;
        s.key = key;
        try { s.size = std::stoll(xml_text(e, "getcontentlength")); } catch (...) { s.size = 0; }
        out.push_back(std::move(s));
      }
    }
    std::sort(out.begin(), out.end(), [](const ObjectStat& a, const ObjectStat& b) { return a.key < b.key; });
    return out;
  }
  Status remove(std::string_view key) override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY(send(base_req("DELETE", join_key(prefix_, key)), {404}));
    return {};
  }
  Status probe() const override {
    net::HttpRequest r = base_req("PROPFIND", "");
    r.headers.emplace_back("Depth", "0");
    LOOM_TRY(send(r, {207}));
    return {};
  }

 private:
  static std::string decode(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
          std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
        o += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
        i += 2;
      } else {
        o += s[i];
      }
    }
    return o;
  }
  // The server returns absolute paths ("/dav/files/u/x"); strip the base URL's path.
  std::string relative_to_base(const std::string& href) const {
    std::string path = href;
    if (auto p = path.find("://"); p != std::string::npos) { auto s = path.find('/', p + 3); path = s == std::string::npos ? "/" : path.substr(s); }
    std::string bp;
    if (auto u = net::parse_url(base_)) bp = u->target;
    while (bp.size() > 1 && bp.back() == '/') bp.pop_back();
    if (bp != "/" && path.rfind(bp, 0) == 0) path = path.substr(bp.size());
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    return path;
  }
  net::HttpRequest base_req(const std::string& method, const std::string& rel) const {
    net::HttpRequest r;
    r.method = method;
    r.url = base_ + (rel.empty() ? "" : "/" + uri_encode(rel, true));
    return r;
  }
  Result<net::HttpResponse> send(net::HttpRequest r, std::initializer_list<int> accept = {}) const {
    if (base_.empty()) return Error(Errc::InvalidArgument, "webdav backend " + id_ + " needs url");
    if (!env_.secrets) return Error(Errc::Auth, "no secrets store for webdav backend " + id_);
    const std::string token = env_.secrets->get_string(secret_ref_ + "_token");
    const std::string user = env_.secrets->get_string(secret_ref_ + "_user");
    const std::string pass = env_.secrets->get_string(secret_ref_ + "_password");
    if (!token.empty()) r.headers.emplace_back("Authorization", "Bearer " + token);
    else if (!user.empty()) r.headers.emplace_back("Authorization", "Basic " + base64::encode(user + ":" + pass));
    else return Error(Errc::Auth, "webdav credentials missing (secrets " + secret_ref_ + "_user/_password or _token)");
    return http_call(env_, r, accept);
  }
  Status make_parents(const std::string& rel_file) {
    std::string cur;
    std::size_t start = 0;
    while (true) {
      std::size_t slash = rel_file.find('/', start);
      if (slash == std::string::npos) break;
      cur = rel_file.substr(0, slash);
      start = slash + 1;
      if (known_.count(cur)) continue;
      // 201 created; 405 already exists; 409 parent missing (cannot happen, parents go first)
      LOOM_TRY(send(base_req("MKCOL", cur), {405}));
      known_.insert(cur);
    }
    return {};
  }

  std::string id_;
  BackendEnv env_;
  std::string base_, prefix_, secret_ref_;
  std::set<std::string> known_;
};
}  // namespace

Result<std::unique_ptr<StorageBackend>> make_webdav(const std::string& id, const Json& cfg, const BackendEnv& env) {
  return std::unique_ptr<StorageBackend>(new WebDavBackend(id, cfg, env));
}

}  // namespace loom::storage::detail
