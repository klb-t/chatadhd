// Google Drive storage backend (P4). Objects are Drive files in one folder, named by their full key
// (Drive allows "/" in names), so the flat key/value contract of the other backends holds without a
// folder tree to keep consistent. The sha256 of the content is kept in the file's appProperties.
//
// OAuth2: the refresh token, client id and client secret are SECRETS (<secret_ref>_refresh_token,
// _client_id, _client_secret). The access token lives in memory only, is refreshed before it expires and
// once more when the API answers 401. Secrets never appear in errors, logs or JSON; token endpoint
// failures report only the HTTP status and the OAuth "error" code.
//
// Config (all optional, presets): root_folder ("root"), api_endpoint, upload_endpoint, token_endpoint
// (defaults = Google's), token_skew_s (60), resumable_threshold (5 MiB; larger bodies use a resumable
// upload session, -1 = always multipart).
#include <algorithm>
#include <chrono>
#include <mutex>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/util/sha256.h"
#include "storage_internal.h"

namespace loom::storage::detail {
namespace {

constexpr std::string_view kLog = "loom.storage.gdrive";

class GDriveBackend final : public StorageBackend {
 public:
  GDriveBackend(std::string id, const Json& cfg, const BackendEnv& env) : id_(std::move(id)), env_(env) {
    secret_ref_ = json::get_string(cfg, "secret_ref", "storage_" + id_);
    folder_ = json::get_string(cfg, "root_folder", "root");
    api_ = trim(json::get_string(cfg, "api_endpoint", "https://www.googleapis.com/drive/v3"));
    upload_ = trim(json::get_string(cfg, "upload_endpoint", "https://www.googleapis.com/upload/drive/v3"));
    token_url_ = json::get_string(cfg, "token_endpoint", "https://oauth2.googleapis.com/token");
    skew_s_ = json::get_int(cfg, "token_skew_s", 60);
    resumable_ = json::get_int(cfg, "resumable_threshold", 5LL * 1024 * 1024);
  }
  const std::string& id() const noexcept override { return id_; }
  std::string kind() const override { return "gdrive"; }

  Status put(std::string_view key, std::string_view bytes, std::string_view mime) override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY_ASSIGN(auto existing, find_ids(key));
    Json meta{{"appProperties", Json{{"sha256", Sha256::hex(bytes)}}}};
    if (!mime.empty()) meta["mimeType"] = std::string(mime);
    const std::string content_type = mime.empty() ? "application/octet-stream" : std::string(mime);
    std::string url = upload_;
    std::string method;
    if (existing.empty()) {
      meta["name"] = std::string(key);
      meta["parents"] = Json::array({folder_});
      url += "/files";
      method = "POST";
    } else {
      url += "/files/" + uri_encode(existing.front(), false);
      method = "PATCH";
    }
    const bool resumable = resumable_ >= 0 && static_cast<std::int64_t>(bytes.size()) > resumable_;
    if (resumable) {
      url += "?uploadType=resumable&fields=id";
      std::vector<std::pair<std::string, std::string>> h{{"Content-Type", "application/json; charset=UTF-8"},
                                                         {"X-Upload-Content-Type", content_type}};
      LOOM_TRY_ASSIGN(auto init, call(method, url, json::dump(meta), h, 0));
      const std::string session = init.header("location");
      if (session.empty()) return Error(Errc::Http, "gdrive: resumable upload gave no session location");
      LOOM_TRY(call("PUT", session, std::string(bytes), {{"Content-Type", content_type}}, 0));
    } else {
      std::string boundary = "loom_boundary_" + Sha256::hex(bytes).substr(0, 16);
      while (bytes.find(boundary) != std::string_view::npos) boundary += "x";
      std::string body = "--" + boundary + "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n" + json::dump(meta) +
                         "\r\n--" + boundary + "\r\nContent-Type: " + content_type + "\r\n\r\n" + std::string(bytes) +
                         "\r\n--" + boundary + "--";
      url += "?uploadType=multipart&fields=id";
      LOOM_TRY(call(method, url, body, {{"Content-Type", "multipart/related; boundary=" + boundary}}, 0));
    }
    // Drive allows duplicate names: drop extras left by an earlier racing writer.
    for (std::size_t i = 1; i < existing.size(); ++i) (void)remove_id(existing[i]);
    return {};
  }

  Result<std::string> get(std::string_view key) const override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY_ASSIGN(auto ids, find_ids(key));
    if (ids.empty()) return Error(Errc::NotFound, "no such object: " + std::string(key));
    LOOM_TRY_ASSIGN(auto r, call("GET", api_ + "/files/" + uri_encode(ids.front(), false) + "?alt=media", "", {}, 0));
    return std::move(r.body);
  }

  Result<ObjectStat> stat(std::string_view key) const override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY_ASSIGN(auto files, query(name_query(key), "files(id,name,size,mimeType,appProperties)"));
    if (files.empty()) return Error(Errc::NotFound, "no such object: " + std::string(key));
    return to_stat(files.front(), std::string(key));
  }

  Result<std::vector<ObjectStat>> list(std::string_view prefix) const override {
    LOOM_TRY_ASSIGN(auto files, query("'" + esc(folder_) + "' in parents and trashed=false",
                                      "nextPageToken,files(id,name,size,mimeType,appProperties)"));
    std::vector<ObjectStat> out;
    for (const auto& f : files) {
      const std::string name = json::get_string(f, "name");
      if (name.rfind(prefix, 0) != 0 || !valid_key(name)) continue;
      out.push_back(to_stat(f, name));
    }
    std::sort(out.begin(), out.end(), [](const ObjectStat& a, const ObjectStat& b) { return a.key < b.key; });
    out.erase(std::unique(out.begin(), out.end(), [](const ObjectStat& a, const ObjectStat& b) { return a.key == b.key; }), out.end());
    return out;
  }

  Status remove(std::string_view key) override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY_ASSIGN(auto ids, find_ids(key));
    for (const auto& i : ids) LOOM_TRY(remove_id(i));
    return {};
  }

  Status probe() const override {
    LOOM_TRY(call("GET", api_ + "/files/" + uri_encode(folder_, false) + "?fields=id", "", {}, 0));
    return {};
  }

 private:
  static std::string trim(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
  }
  // Drive query string literal escaping.
  static std::string esc(std::string_view s) {
    std::string o;
    for (char c : s) {
      if (c == '\\' || c == '\'') o += '\\';
      o += c;
    }
    return o;
  }
  std::string name_query(std::string_view key) const {
    return "name='" + esc(key) + "' and '" + esc(folder_) + "' in parents and trashed=false";
  }
  static ObjectStat to_stat(const Json& f, std::string key) {
    ObjectStat s;
    s.key = std::move(key);
    const std::string sz = json::get_string(f, "size");  // Drive sends int64 as a string
    try { s.size = sz.empty() ? 0 : std::stoll(sz); } catch (...) { s.size = 0; }
    s.mime = json::get_string(f, "mimeType");
    if (f.contains("appProperties") && f["appProperties"].is_object()) s.sha256 = json::get_string(f["appProperties"], "sha256");
    return s;
  }

  Result<std::vector<Json>> query(const std::string& q, const std::string& fields) const {
    std::vector<Json> out;
    std::string token;
    for (int guard = 0; guard < 100000; ++guard) {
      std::string url = api_ + "/files?pageSize=1000&spaces=drive&q=" + uri_encode(q, false) + "&fields=" + uri_encode(fields, false);
      if (!token.empty()) url += "&pageToken=" + uri_encode(token, false);
      LOOM_TRY_ASSIGN(auto r, call("GET", url, "", {}, 0));
      auto j = r.json();
      if (!j) return Error(Errc::Http, "gdrive: unreadable list reply");
      if (j->contains("files") && (*j)["files"].is_array()) for (const auto& f : (*j)["files"]) out.push_back(f);
      token = json::get_string(*j, "nextPageToken");
      if (token.empty()) break;
    }
    return out;
  }
  Result<std::vector<std::string>> find_ids(std::string_view key) const {
    LOOM_TRY_ASSIGN(auto files, query(name_query(key), "files(id)"));
    std::vector<std::string> ids;
    for (const auto& f : files) if (auto i = json::get_string(f, "id"); !i.empty()) ids.push_back(i);
    return ids;
  }
  Status remove_id(const std::string& fid) {
    LOOM_TRY(call("DELETE", api_ + "/files/" + uri_encode(fid, false), "", {}, 404));
    return {};
  }

  // ── OAuth2 ──
  Status refresh_locked() const {
    if (!env_.secrets) return Error(Errc::Auth, "no secrets store for gdrive backend " + id_);
    const std::string cid = env_.secrets->get_string(secret_ref_ + "_client_id");
    const std::string csec = env_.secrets->get_string(secret_ref_ + "_client_secret");
    const std::string rt = env_.secrets->get_string(secret_ref_ + "_refresh_token");
    if (cid.empty() || rt.empty())
      return Error(Errc::Auth, "gdrive credentials missing (secrets " + secret_ref_ + "_client_id/_client_secret/_refresh_token)");
    if (!env_.http) return Error(Errc::Unavailable, "no HTTP transport for network storage");
    std::vector<std::pair<std::string, std::string>> form{{"grant_type", "refresh_token"}, {"client_id", cid}, {"refresh_token", rt}};
    if (!csec.empty()) form.emplace_back("client_secret", csec);
    net::HttpRequest req;
    req.method = "POST";
    req.url = token_url_;
    req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    req.headers.emplace_back("Accept", "application/json");
    req.body = net::form_urlencode(form);
    auto r = env_.http->send(req);
    token_.clear();
    if (!r) return Error(r.error().code, "gdrive token refresh failed: " + r.error().message);
    auto j = r->json();
    if (!r->ok() || !j) {
      std::string why = j ? json::get_string(*j, "error") : std::string();
      return Error(r->status == 400 || r->status == 401 ? Errc::Auth : Errc::Http,
                   "gdrive token refresh returned HTTP " + std::to_string(r->status) + (why.empty() ? "" : ": " + why));
    }
    token_ = json::get_string(*j, "access_token");
    if (token_.empty()) return Error(Errc::Auth, "gdrive token refresh returned no access token");
    const std::int64_t ttl = json::get_int(*j, "expires_in", 3600);
    expires_ = std::chrono::steady_clock::now() + std::chrono::seconds(std::max<std::int64_t>(0, ttl - skew_s_));
    log::debug(kLog, "access token refreshed for {}", id_);
    return {};
  }
  Result<std::string> bearer(bool force_refresh) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (force_refresh || token_.empty() || std::chrono::steady_clock::now() >= expires_) LOOM_TRY(refresh_locked());
    return token_;
  }

  Result<net::HttpResponse> call(const std::string& method, const std::string& url, const std::string& body,
                                 const std::vector<std::pair<std::string, std::string>>& headers,
                                 int extra_ok) const {
    for (int attempt = 0; attempt < 2; ++attempt) {
      LOOM_TRY_ASSIGN(std::string tok, bearer(attempt == 1));
      net::HttpRequest req;
      req.method = method;
      req.url = url;
      for (const auto& h : headers) req.headers.push_back(h);
      req.headers.emplace_back("Authorization", "Bearer " + tok);
      req.body = body;
      auto r = http_call(env_, req, {401, extra_ok});  // 401 handled here: refresh once, retry once
      if (!r) return r;
      if (r->status == 401) {
        if (attempt == 0) continue;
        return Error(Errc::Auth, "storage request " + method + " returned HTTP 401 after token refresh");
      }
      return r;
    }
    return Error(Errc::Auth, "gdrive authentication failed");
  }

  std::string id_;
  BackendEnv env_;
  std::string secret_ref_, folder_, api_, upload_, token_url_;
  std::int64_t skew_s_ = 60, resumable_ = 5LL * 1024 * 1024;
  mutable std::mutex mu_;
  mutable std::string token_;  // memory only, never persisted or logged
  mutable std::chrono::steady_clock::time_point expires_{};
};
}  // namespace

Result<std::unique_ptr<StorageBackend>> make_gdrive(const std::string& id, const Json& cfg, const BackendEnv& env) {
  return std::unique_ptr<StorageBackend>(new GDriveBackend(id, cfg, env));
}

}  // namespace loom::storage::detail
