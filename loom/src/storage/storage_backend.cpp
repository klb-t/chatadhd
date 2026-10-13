#include "loom/storage_backend.h"

#include <algorithm>

#include "loom/net/http.h"
#include "loom/util/sha256.h"
#include "storage_internal.h"

namespace loom::storage {

Json ObjectStat::to_json() const {
  return Json{{"key", key}, {"size", size}, {"sha256", sha256.empty() ? Json(nullptr) : Json(sha256)}, {"mime", mime}};
}

bool valid_key(std::string_view key) noexcept {
  if (key.empty() || key.front() == '/' || key.back() == '/') return false;
  std::size_t start = 0;
  while (start <= key.size()) {
    std::size_t end = key.find('/', start);
    if (end == std::string_view::npos) end = key.size();
    std::string_view seg = key.substr(start, end - start);
    if (seg.empty() || seg == "." || seg == "..") return false;
    start = end + 1;
  }
  return key.find('\0') == std::string_view::npos && key.find('\\') == std::string_view::npos;
}

std::string cas_key(std::string_view hash) {
  std::string h(hash);
  if (h.size() < 4) return h;
  return h.substr(0, 2) + "/" + h.substr(2, 2) + "/" + h;
}

Status verify_object(const StorageBackend& b, std::string_view key, std::string_view expected) {
  LOOM_TRY_ASSIGN(std::string bytes, b.get(key));
  if (Sha256::hex(bytes) != expected)
    return Error(Errc::Conflict, "object does not match its hash on " + b.id() + ": " + std::string(key));
  return {};
}

void BackendRegistry::add(KindDescriptor d) {
  std::string k = d.kind;
  kinds_[k] = std::move(d);
}
const KindDescriptor* BackendRegistry::find(std::string_view kind) const {
  auto it = kinds_.find(kind);
  return it == kinds_.end() ? nullptr : &it->second;
}
std::vector<std::string> BackendRegistry::kinds() const {
  std::vector<std::string> out;
  for (const auto& e : kinds_) out.push_back(e.first);
  return out;
}
Result<std::unique_ptr<StorageBackend>> BackendRegistry::create(const std::string& id, const Json& cfg,
                                                                const BackendEnv& env) const {
  const std::string kind = json::get_string(cfg, "kind");
  const KindDescriptor* d = find(kind);
  if (!d) return Error(Errc::Unsupported, "unknown storage backend kind '" + kind + "' for " + id);
  if (!d->factory)
    return Error(Errc::NotImplemented, "storage backend kind '" + kind + "' is " + d->status + " (no adapter yet)");
  return d->factory(id, cfg, env);
}

const BackendRegistry& BackendRegistry::builtin() {
  static const BackendRegistry r = [] {
    BackendRegistry x;
    x.add({"local", "implemented", detail::make_local});
    x.add({"s3", "implemented", detail::make_s3});
    x.add({"webdav", "implemented", detail::make_webdav});
    // Open item: Google Drive needs an OAuth refresh flow (token endpoint, scopes, resumable upload) that does
    // not fit the key/value contract of the other adapters without its own design; the kind is declared so that
    // data can already name it and reports "planned".
    x.add({"gdrive", "planned", {}});
    return x;
  }();
  return r;
}

}  // namespace loom::storage

namespace loom::storage::detail {

std::string expand_dir(std::string_view path, const std::filesystem::path& data_dir) {
  std::string s(path);
  const std::string tok = "{dir}";
  const std::string dir = data_dir.string();
  for (std::size_t p = s.find(tok); p != std::string::npos; p = s.find(tok, p + dir.size())) s.replace(p, tok.size(), dir);
  return s;
}

std::string uri_encode(std::string_view s, bool keep_slash) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~' || (keep_slash && c == '/')) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string join_key(std::string_view prefix, std::string_view key) {
  std::string p(prefix);
  while (!p.empty() && p.back() == '/') p.pop_back();
  while (!p.empty() && p.front() == '/') p.erase(p.begin());
  return p.empty() ? std::string(key) : p + "/" + std::string(key);
}

Result<net::HttpResponse> http_call(const BackendEnv& env, const net::HttpRequest& req, std::initializer_list<int> accept) {
  if (!env.http) return Error(Errc::Unavailable, "no HTTP transport for network storage");
  auto r = env.http->send(req);
  if (!r) return Error(r.error().code, "storage request " + req.method + " failed: " + r.error().message);
  const int st = r->status;
  if (r->ok() || std::find(accept.begin(), accept.end(), st) != accept.end()) return std::move(*r);
  std::string body = r->body.substr(0, 200);
  Errc code = st == 404 ? Errc::NotFound : (st == 401 || st == 403) ? Errc::Auth : st == 429 ? Errc::RateLimited : Errc::Http;
  return Error(code, "storage request " + req.method + " returned HTTP " + std::to_string(st) +
                         (body.empty() ? "" : ": " + body));
}

namespace {
std::string_view local_name(std::string_view tag) {
  std::size_t end = tag.find_first_of(" \t\r\n/");
  tag = tag.substr(0, end);
  auto c = tag.find(':');
  return c == std::string_view::npos ? tag : tag.substr(c + 1);
}
}  // namespace

std::vector<std::string_view> xml_elements(std::string_view xml, std::string_view local) {
  std::vector<std::string_view> out;
  std::size_t pos = 0;
  while ((pos = xml.find('<', pos)) != std::string_view::npos) {
    std::size_t gt = xml.find('>', pos);
    if (gt == std::string_view::npos) break;
    std::string_view tag = xml.substr(pos + 1, gt - pos - 1);
    pos = gt + 1;
    if (tag.empty() || tag[0] == '/' || tag[0] == '?' || tag[0] == '!' || local_name(tag) != local) continue;
    if (tag.back() == '/') { out.push_back(std::string_view()); continue; }
    // find the next closing tag with the same local name
    std::size_t scan = gt + 1;
    while (true) {
      std::size_t lt = xml.find('<', scan);
      if (lt == std::string_view::npos) return out;
      std::size_t g2 = xml.find('>', lt);
      if (g2 == std::string_view::npos) return out;
      std::string_view t2 = xml.substr(lt + 1, g2 - lt - 1);
      if (!t2.empty() && t2[0] == '/' && local_name(t2.substr(1)) == local) {
        out.push_back(xml.substr(gt + 1, lt - gt - 1));
        pos = g2 + 1;
        break;
      }
      scan = g2 + 1;
    }
  }
  return out;
}

std::string xml_text(std::string_view xml, std::string_view local) {
  auto v = xml_elements(xml, local);
  return v.empty() ? std::string() : xml_unescape(v.front());
}

std::string xml_unescape(std::string_view s) {
  std::string o(s);
  const std::pair<const char*, const char*> reps[] = {{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
  for (const auto& [from, to] : reps) {
    std::string f(from);
    for (std::size_t p = o.find(f); p != std::string::npos; p = o.find(f, p + 1)) o.replace(p, f.size(), to);
  }
  return o;
}

}  // namespace loom::storage::detail
