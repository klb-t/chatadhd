// Port of engine/github_sync.py: bidirectional sync with a GitHub repository
// through the contents API.
#include "loom/github_sync.h"

#include <algorithm>
#include <map>
#include <regex>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"

namespace loom {

namespace fs = std::filesystem;
using OptFiles = std::optional<std::vector<GitHubFile>>;

namespace {
constexpr std::string_view kLog = "loom.github";

// Python fnmatch.translate(), reimplemented over std::regex (ECMAScript).
// '.' in the emitted pattern is written as [\s\S] instead of relying on an
// inline DOTALL flag, so it also matches '\n' the way Python's re.DOTALL does
// (irrelevant for single-line paths, but keeps the translation faithful).
std::string fnmatch_translate(std::string_view pat) {
  static constexpr std::string_view kRegexSpecial = ".^$+*?()[]{}|\\";
  std::string res;
  std::size_t i = 0, n = pat.size();
  while (i < n) {
    char c = pat[i++];
    if (c == '*') {
      if (res.size() < 8 || res.compare(res.size() - 8, 8, "[\\s\\S]*") != 0) res += "[\\s\\S]*";
    } else if (c == '?') {
      res += "[\\s\\S]";
    } else if (c == '[') {
      std::size_t j = i;
      if (j < n && pat[j] == '!') ++j;
      if (j < n && pat[j] == ']') ++j;
      while (j < n && pat[j] != ']') ++j;
      if (j >= n) {
        res += "\\[";
      } else {
        std::string stuff(pat.substr(i, j - i));
        std::string escaped;
        for (char sc : stuff) {
          if (sc == '\\' || sc == '&' || sc == '~' || sc == '|') escaped += '\\';
          escaped += sc;
        }
        i = j + 1;
        if (escaped.empty()) {
          res += "(?!)";
        } else if (escaped.front() == '!') {
          res += "[^" + escaped.substr(1) + "]";
        } else if (escaped.front() == '^' || escaped.front() == '[') {
          res += "[\\" + escaped + "]";
        } else {
          res += "[" + escaped + "]";
        }
      }
    } else if (kRegexSpecial.find(c) != std::string_view::npos) {
      res += '\\';
      res += c;
    } else {
      res += c;
    }
  }
  return res;
}

}  // namespace

bool fnmatch(std::string_view name, std::string_view pattern) {
  try {
    std::regex re(fnmatch_translate(pattern), std::regex::ECMAScript);
    return std::regex_match(name.begin(), name.end(), re);
  } catch (const std::regex_error& e) {
    log::warn(kLog, "fnmatch: bad pattern '{}': {}", pattern, e.what());
    return false;
  }
}

// ── GitHubFile / SyncConfig JSON ─────────────────────────────────────

Json GitHubFile::to_json() const {
  return Json{{"path", path},
              {"sha", sha},
              {"size", size},
              {"content", content ? Json(*content) : Json(nullptr)},
              {"url", url},
              {"local_path", local_path ? Json(*local_path) : Json(nullptr)},
              {"status", status}};
}

Result<GitHubFile> GitHubFile::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::Parse, "GitHubFile must be a JSON object");
  GitHubFile f;
  f.path = json::get_string(j, "path");
  f.sha = json::get_string(j, "sha");
  f.size = json::get_int(j, "size");
  f.content = json::get_opt_string(j, "content");
  f.url = json::get_string(j, "url");
  f.local_path = json::get_opt_string(j, "local_path");
  f.status = json::get_string(j, "status", "unknown");
  return f;
}

Json SyncConfig::to_json() const {
  return Json{{"repo", repo},
              {"branch", branch},
              {"local_path", local_path},
              {"sync_direction", sync_direction},
              {"auto_sync", auto_sync},
              {"include_patterns", include_patterns},
              {"exclude_patterns", exclude_patterns}};
}

Result<SyncConfig> SyncConfig::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::Parse, "SyncConfig must be a JSON object");
  SyncConfig c;
  c.repo = json::get_string(j, "repo");
  c.branch = json::get_string(j, "branch", "main");
  c.local_path = json::get_string(j, "local_path");
  c.sync_direction = json::get_string(j, "sync_direction", "bidirectional");
  c.auto_sync = json::get_bool(j, "auto_sync", false);
  if (const Json* inc = json::find(j, "include_patterns"); inc && inc->is_array()) {
    c.include_patterns.clear();
    for (const auto& v : *inc) {
      if (v.is_string()) c.include_patterns.push_back(v.get<std::string>());
    }
  }
  if (const Json* exc = json::find(j, "exclude_patterns"); exc && exc->is_array()) {
    c.exclude_patterns.clear();
    for (const auto& v : *exc) {
      if (v.is_string()) c.exclude_patterns.push_back(v.get<std::string>());
    }
  }
  return c;
}

// ── GitHubSync ────────────────────────────────────────────────────────

namespace {

net::Headers github_headers(const SyncConfig& cfg) {
  net::Headers h = {{"Accept", "application/vnd.github.v3+json"}, {"User-Agent", "ChatADHD-Sync"}};
  if (!cfg.token.empty()) h.push_back({"Authorization", "token " + cfg.token});
  return h;
}

std::string contents_url(const SyncConfig& cfg, std::string_view path) {
  // Python: f"{BASE_URL}/repos/{repo}/contents/{path}" - the trailing slash is
  // always there, even when path is empty (top-level listing).
  std::string url = std::string(GitHubSync::kBaseUrl) + "/repos/" + cfg.repo + "/contents/";
  url += path;
  return url;
}

}  // namespace

GitHubSync::GitHubSync(SyncConfig cfg, net::HttpTransport& http) : cfg_(std::move(cfg)), http_(http) {}

bool GitHubSync::test_connection() {
  net::HttpRequest req;
  req.method = "GET";
  req.url = std::string(kBaseUrl) + "/repos/" + cfg_.repo;
  req.headers = github_headers(cfg_);
  req.timeout_ms = 10000;
  auto resp = http_.send(req);
  return resp && resp->status == 200;
}

Result<std::vector<GitHubFile>> GitHubSync::list_remote_files(std::string_view path) {
  std::vector<GitHubFile> files;
  net::HttpRequest req;
  req.method = "GET";
  req.url = net::with_query(contents_url(cfg_, path), {{"ref", cfg_.branch}});
  req.headers = github_headers(cfg_);
  req.timeout_ms = 30000;

  auto resp = http_.send(req);
  if (!resp) {
    log::error(kLog, "Failed to list remote files: {}", resp.error().message);
    return files;
  }
  if (!resp->ok()) {
    log::error(kLog, "Failed to list remote files: HTTP {}", resp->status);
    return files;
  }
  auto body = resp->json();
  if (!body) {
    log::error(kLog, "Failed to list remote files: invalid JSON");
    return files;
  }
  Json items = body->is_array() ? *body : Json::array({*body});
  for (const auto& item : items) {
    std::string type = json::get_string(item, "type");
    std::string item_path = json::get_string(item, "path");
    if (type == "file") {
      if (should_include(item_path)) {
        GitHubFile f;
        f.path = item_path;
        f.sha = json::get_string(item, "sha");
        f.size = json::get_int(item, "size");
        f.url = json::get_string(item, "download_url");
        files.push_back(std::move(f));
      }
    } else if (type == "dir") {
      auto sub = list_remote_files(item_path);
      if (sub) {
        for (auto& f : *sub) files.push_back(std::move(f));
      }
    }
  }
  return files;
}

std::vector<GitHubFile> GitHubSync::list_local_files() const {
  std::vector<GitHubFile> files;
  fs::path root(cfg_.local_path);
  std::error_code ec;
  if (!fs::exists(root, ec)) return files;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    const fs::directory_entry& entry = *it;
    std::error_code fec;
    if (!entry.is_regular_file(fec) || fec) continue;
    std::string rel = entry.path().lexically_relative(root).generic_string();
    if (!should_include(rel)) continue;
    GitHubFile f;
    f.path = rel;
    f.sha = "";
    f.size = static_cast<std::int64_t>(entry.file_size(fec));
    f.local_path = entry.path().string();
    files.push_back(std::move(f));
  }
  return files;
}

bool GitHubSync::should_include(std::string_view path) const {
  for (const auto& pattern : cfg_.exclude_patterns) {
    if (fnmatch(path, pattern)) return false;
  }
  for (const auto& pattern : cfg_.include_patterns) {
    if (fnmatch(path, pattern)) return true;
  }
  return cfg_.include_patterns.empty();
}

Result<std::vector<GitHubFile>> GitHubSync::get_sync_status() {
  auto remote_r = list_remote_files();
  if (!remote_r) return remote_r.error();
  std::vector<GitHubFile> local = list_local_files();

  std::map<std::string, GitHubFile, std::less<>> remote_by_path, local_by_path;
  for (auto& f : *remote_r) remote_by_path.emplace(f.path, f);
  for (auto& f : local) local_by_path.emplace(f.path, f);

  std::vector<std::string> all_paths;
  for (const auto& kv : remote_by_path) all_paths.push_back(kv.first);
  for (const auto& kv : local_by_path) {
    if (remote_by_path.find(kv.first) == remote_by_path.end()) all_paths.push_back(kv.first);
  }
  std::sort(all_paths.begin(), all_paths.end());

  std::vector<GitHubFile> result;
  for (const auto& path : all_paths) {
    auto rit = remote_by_path.find(path);
    auto lit = local_by_path.find(path);
    GitHubFile f;
    if (rit != remote_by_path.end() && lit != local_by_path.end()) {
      f.path = path;
      f.sha = rit->second.sha;
      f.size = rit->second.size;
      f.local_path = lit->second.local_path;
      f.status = rit->second.size != lit->second.size ? "modified" : "synced";
    } else if (rit != remote_by_path.end()) {
      f.path = path;
      f.sha = rit->second.sha;
      f.size = rit->second.size;
      f.url = rit->second.url;
      f.status = "new_remote";
    } else {
      f.path = path;
      f.sha = "";
      f.size = lit->second.size;
      f.local_path = lit->second.local_path;
      f.status = "new_local";
    }
    result.push_back(std::move(f));
  }
  return result;
}

Result<std::string> GitHubSync::fetch_file_content(const GitHubFile& file) {
  if (!file.url.empty()) {
    net::HttpRequest req;
    req.method = "GET";
    req.url = file.url;
    req.timeout_ms = 30000;
    auto resp = http_.send(req);
    if (!resp || !resp->ok()) {
      log::error(kLog, "Failed to fetch {}: {}", file.path, resp ? std::to_string(resp->status) : resp.error().message);
      return std::string();
    }
    return resp->body;
  }
  net::HttpRequest req;
  req.method = "GET";
  req.url = net::with_query(contents_url(cfg_, file.path), {{"ref", cfg_.branch}});
  req.headers = github_headers(cfg_);
  req.timeout_ms = 30000;
  auto resp = http_.send(req);
  if (!resp || !resp->ok()) {
    log::error(kLog, "Failed to fetch {}: {}", file.path, resp ? std::to_string(resp->status) : resp.error().message);
    return std::string();
  }
  auto data = resp->json();
  if (!data) {
    log::error(kLog, "Failed to fetch {}: invalid JSON", file.path);
    return std::string();
  }
  if (json::get_string(*data, "encoding") == "base64") {
    auto decoded = base64::decode(json::get_string(*data, "content"), false);
    if (!decoded) {
      log::error(kLog, "Failed to fetch {}: {}", file.path, decoded.error().message);
      return std::string();
    }
    return *decoded;
  }
  return json::get_string(*data, "content");
}

Status GitHubSync::pull_file(const GitHubFile& file) {
  if (cfg_.sync_direction == "push_only") {
    log::warn(kLog, "Pull disabled in push_only mode");
    return Error(Errc::Unsupported, "pull disabled in push_only mode");
  }
  LOOM_TRY_ASSIGN(std::string content, fetch_file_content(file));
  if (content.empty() && file.size > 0) return Error(Errc::Http, "empty content for non-empty file " + file.path);

  fs::path local_path = fs::path(cfg_.local_path) / file.path;
  LOOM_TRY(fsutil::ensure_dir(local_path.parent_path()));
  LOOM_TRY(fsutil::write_file(local_path, content));
  log::info(kLog, "Pulled: {}", file.path);
  return {};
}

Status GitHubSync::push_file(const GitHubFile& file, const std::optional<std::string>& message) {
  if (cfg_.sync_direction == "pull_only") {
    log::warn(kLog, "Push disabled in pull_only mode");
    return Error(Errc::Unsupported, "push disabled in pull_only mode");
  }
  if (!file.local_path || file.local_path->empty()) return Error(Errc::InvalidArgument, "file has no local_path");

  auto content = fsutil::read_file(*file.local_path);
  if (!content) {
    log::error(kLog, "Failed to read {}: {}", *file.local_path, content.error().message);
    return content.error();
  }

  Json body{{"message", message.value_or("Update " + file.path + " via ChatADHD")},
           {"content", base64::encode(*content)},
           {"branch", cfg_.branch}};
  if (!file.sha.empty()) body["sha"] = file.sha;

  net::HttpRequest req;
  req.method = "PUT";
  req.url = contents_url(cfg_, file.path);
  req.headers = github_headers(cfg_);
  req.headers.push_back({"Content-Type", "application/json"});
  req.body = json::dump(body);
  req.timeout_ms = 30000;

  auto resp = http_.send(req);
  if (!resp) {
    log::error(kLog, "Failed to push {}: {}", file.path, resp.error().message);
    return resp.error();
  }
  if (!resp->ok()) {
    log::error(kLog, "Failed to push {}: HTTP {}", file.path, resp->status);
    return Error(Errc::Http, "push failed: HTTP " + std::to_string(resp->status));
  }
  log::info(kLog, "Pushed: {}", file.path);
  return {};
}

namespace {
Json tally(int success, int failed) { return Json{{"success", success}, {"failed", failed}}; }
}  // namespace

Result<Json> GitHubSync::pull_all(const OptFiles& files) {
  std::vector<GitHubFile> chosen;
  if (files) {
    chosen = *files;
  } else {
    auto status = get_sync_status();
    if (!status) return status.error();
    for (auto& f : *status) {
      if (f.status == "new_remote" || f.status == "modified") chosen.push_back(std::move(f));
    }
  }
  int ok = 0, failed = 0;
  for (const auto& f : chosen) {
    if (pull_file(f)) ++ok;
    else ++failed;
  }
  return tally(ok, failed);
}

Result<Json> GitHubSync::push_all(const OptFiles& files) {
  std::vector<GitHubFile> chosen;
  if (files) {
    chosen = *files;
  } else {
    auto status = get_sync_status();
    if (!status) return status.error();
    for (auto& f : *status) {
      if (f.status == "new_local" || f.status == "modified") chosen.push_back(std::move(f));
    }
  }
  int ok = 0, failed = 0;
  for (const auto& f : chosen) {
    if (push_file(f)) ++ok;
    else ++failed;
  }
  return tally(ok, failed);
}

Result<Json> GitHubSync::sync(const OptFiles& files) {
  std::vector<GitHubFile> chosen;
  if (files) {
    chosen = *files;
  } else {
    auto status = get_sync_status();
    if (!status) return status.error();
    chosen = *status;
  }
  int pulled_ok = 0, pulled_failed = 0, pushed_ok = 0, pushed_failed = 0;
  std::vector<std::string> conflicts;
  for (const auto& f : chosen) {
    if (f.status == "new_remote") {
      if (pull_file(f)) ++pulled_ok;
      else ++pulled_failed;
    } else if (f.status == "new_local") {
      if (push_file(f)) ++pushed_ok;
      else ++pushed_failed;
    } else if (f.status == "modified") {
      conflicts.push_back(f.path);
    }
  }
  return Json{{"pulled", tally(pulled_ok, pulled_failed)}, {"pushed", tally(pushed_ok, pushed_failed)}, {"conflicts", conflicts}};
}

// ── GitHubSyncManager ───────────────────────────────────────────────

GitHubSyncManager::GitHubSyncManager(std::filesystem::path config_path, const Secrets& secrets, net::HttpTransport& http)
    : path_(std::move(config_path)), secrets_(secrets), http_(http) {
  std::error_code ec;
  if (!fs::exists(path_, ec)) return;
  auto raw = fsutil::read_file(path_);
  if (!raw) {
    log::warn(kLog, "failed to read {}: {}", path_.string(), raw.error().message);
    return;
  }
  auto parsed = json::parse(*raw);
  if (!parsed || !parsed->is_object()) {
    log::warn(kLog, "failed to parse {}", path_.string());
    return;
  }
  for (auto it = parsed->begin(); it != parsed->end(); ++it) {
    auto cfg = SyncConfig::from_json(it.value());
    if (cfg) configs_.emplace(it.key(), *cfg);
  }
}

Status GitHubSyncManager::save() const {
  Json data = Json::object();
  for (const auto& [name, cfg] : configs_) data[name] = cfg.to_json();
  json::DumpOptions opts;
  opts.indent = 2;
  return fsutil::atomic_write(path_, json::py_dumps(data, opts), {});
}

Result<SyncConfig> GitHubSyncManager::add_config(std::string_view name, std::string_view repo, std::string_view local_path,
                                                 std::string_view branch, std::string_view direction) {
  SyncConfig cfg;
  cfg.repo = std::string(repo);
  cfg.branch = std::string(branch);
  cfg.local_path = std::string(local_path);
  cfg.token = secrets_.get_string("github_token");
  cfg.sync_direction = std::string(direction);
  configs_[std::string(name)] = cfg;
  LOOM_TRY(save());
  return cfg;
}

Status GitHubSyncManager::remove_config(std::string_view name) {
  auto it = configs_.find(name);
  if (it == configs_.end()) return {};
  configs_.erase(it);
  return save();
}

std::unique_ptr<GitHubSync> GitHubSyncManager::get_syncer(std::string_view name) const {
  auto it = configs_.find(name);
  if (it == configs_.end()) return nullptr;
  SyncConfig cfg = it->second;
  cfg.token = secrets_.get_string("github_token");
  return std::make_unique<GitHubSync>(std::move(cfg), http_);
}

std::vector<std::string> GitHubSyncManager::list_configs() const {
  std::vector<std::string> names;
  for (const auto& kv : configs_) names.push_back(kv.first);
  return names;
}

Json GitHubSyncManager::to_json() const {
  Json j = Json::object();
  for (const auto& [name, cfg] : configs_) j[name] = cfg.to_json();
  return j;
}

}  // namespace loom
