// loom/github_sync.h — port of engine/github_sync.py.      [OWNER: wave 2 crypto/media/github]
//
// Same REST calls as Python (api.github.com, Accept
// application/vnd.github.v3+json, User-Agent ChatADHD-Sync, Authorization
// "token <token>"):
//   test_connection      GET /repos/{repo} == 200 (timeout 10)
//   list_remote_files    GET /repos/{repo}/contents/{path}?ref={branch},
//                        recursing into "dir" items; files filtered by
//                        should_include; errors logged -> partial list
//   list_local_files     recursive walk of local_path, relative paths
//   should_include       excludes first, then includes (fnmatch); empty
//                        include list = include all
//   get_sync_status      union of paths (sorted): both + same size ->
//                        "synced", both + different size -> "modified",
//                        remote only -> "new_remote", local only -> "new_local"
//   fetch_file_content   download_url when known, else contents API
//                        (base64 decode when encoding == base64)
//   pull_file            refused in push_only; writes local file
//   push_file            refused in pull_only; PUT contents with message
//                        (default "Update {path} via ChatADHD"), base64
//                        content, branch, sha when updating
//   pull_all / push_all  default selection new_remote|modified / new_local|
//                        modified -> {"success","failed"}
//   sync                 new_remote -> pull, new_local -> push, modified ->
//                        conflicts[] -> {"pulled":{...},"pushed":{...},"conflicts":[...]}
// GitHubSyncManager persists configs (without tokens) to a JSON file (Loom:
// <data>/github_sync.json); the token always comes from secrets.github_token.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Secrets;
namespace net {
class HttpTransport;
}

// Python fnmatch.fnmatch on POSIX: case-sensitive; '*' also matches '/';
// supports * ? [seq] [!seq].
bool fnmatch(std::string_view name, std::string_view pattern);

struct GitHubFile {
  std::string path;
  std::string sha;
  std::int64_t size = 0;
  std::optional<std::string> content;
  std::string url;
  std::optional<std::string> local_path;
  std::string status = "unknown";  // synced | modified | new_local | new_remote | unknown
  Json to_json() const;
  static Result<GitHubFile> from_json(const Json& j);
};

struct SyncConfig {
  std::string repo;  // owner/repo
  std::string branch = "main";
  std::string local_path;
  std::string token;  // never serialised
  std::string sync_direction = "bidirectional";  // bidirectional | push_only | pull_only
  bool auto_sync = false;
  std::vector<std::string> include_patterns{"*.py", "*.md", "*.json", "*.txt"};
  // Default preset only; callers may replace it, including with an empty list.
  std::vector<std::string> exclude_patterns{"__pycache__/*", ".git/*", "*.pyc", "secrets.json", "*/secrets.json"};
  Json to_json() const;  // without token
  static Result<SyncConfig> from_json(const Json& j);
};

class GitHubSync {
 public:
  static constexpr std::string_view kBaseUrl = "https://api.github.com";

  GitHubSync(SyncConfig cfg, net::HttpTransport& http);

  bool test_connection();
  Result<std::vector<GitHubFile>> list_remote_files(std::string_view path = "");
  std::vector<GitHubFile> list_local_files() const;
  bool should_include(std::string_view path) const;
  Result<std::vector<GitHubFile>> get_sync_status();
  Result<std::string> fetch_file_content(const GitHubFile& file);
  Status pull_file(const GitHubFile& file);
  Status push_file(const GitHubFile& file, const std::optional<std::string>& message = {});
  Result<Json> pull_all(const std::optional<std::vector<GitHubFile>>& files = {});
  Result<Json> push_all(const std::optional<std::vector<GitHubFile>>& files = {});
  Result<Json> sync(const std::optional<std::vector<GitHubFile>>& files = {});

  const SyncConfig& config() const noexcept { return cfg_; }

 private:
  SyncConfig cfg_;
  net::HttpTransport& http_;
};

class GitHubSyncManager {
 public:
  GitHubSyncManager(std::filesystem::path config_path, const Secrets& secrets, net::HttpTransport& http);
  Result<SyncConfig> add_config(std::string_view name, std::string_view repo, std::string_view local_path,
                                std::string_view branch = "main", std::string_view direction = "bidirectional");
  Status remove_config(std::string_view name);
  // nullptr when unknown; token filled from secrets.github_token.
  std::unique_ptr<GitHubSync> get_syncer(std::string_view name) const;
  std::vector<std::string> list_configs() const;
  Json to_json() const;  // {name: config}

 private:
  Status save() const;
  std::filesystem::path path_;
  const Secrets& secrets_;
  net::HttpTransport& http_;
  std::map<std::string, SyncConfig, std::less<>> configs_;
};

}  // namespace loom
