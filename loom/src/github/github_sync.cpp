// OWNER: wave 2 crypto/media/github. Stub.
#include "loom/github_sync.h"

#include "stub.h"

namespace loom {

using OptFiles = std::optional<std::vector<GitHubFile>>;

bool fnmatch(std::string_view, std::string_view) { return false; }  // STUB: wave2

Json GitHubFile::to_json() const {
  return Json{{"path", path},
              {"sha", sha},
              {"size", size},
              {"url", url},
              {"local_path", local_path ? Json(*local_path) : Json(nullptr)},
              {"status", status}};
}
Result<GitHubFile> GitHubFile::from_json(const Json&) {
  return LOOM_NOT_IMPLEMENTED("GitHubFile::from_json");  // STUB: wave2
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
Result<SyncConfig> SyncConfig::from_json(const Json&) {
  return LOOM_NOT_IMPLEMENTED("SyncConfig::from_json");  // STUB: wave2
}

GitHubSync::GitHubSync(SyncConfig cfg, net::HttpTransport& http) : cfg_(std::move(cfg)), http_(http) {}
bool GitHubSync::test_connection() { return false; }  // STUB: wave2
Result<std::vector<GitHubFile>> GitHubSync::list_remote_files(std::string_view) {
  return LOOM_NOT_IMPLEMENTED("GitHubSync::list_remote_files");  // STUB: wave2
}
std::vector<GitHubFile> GitHubSync::list_local_files() const { return {}; }  // STUB: wave2
bool GitHubSync::should_include(std::string_view) const { return false; }   // STUB: wave2
Result<std::vector<GitHubFile>> GitHubSync::get_sync_status() {
  return LOOM_NOT_IMPLEMENTED("GitHubSync::get_sync_status");  // STUB: wave2
}
Result<std::string> GitHubSync::fetch_file_content(const GitHubFile&) {
  return LOOM_NOT_IMPLEMENTED("GitHubSync::fetch_file_content");  // STUB: wave2
}
Status GitHubSync::pull_file(const GitHubFile&) { return LOOM_NOT_IMPLEMENTED("GitHubSync::pull_file"); }  // STUB: wave2
Status GitHubSync::push_file(const GitHubFile&, const std::optional<std::string>&) {
  return LOOM_NOT_IMPLEMENTED("GitHubSync::push_file");  // STUB: wave2
}
Result<Json> GitHubSync::pull_all(const OptFiles&) { return LOOM_NOT_IMPLEMENTED("GitHubSync::pull_all"); }  // STUB: wave2
Result<Json> GitHubSync::push_all(const OptFiles&) { return LOOM_NOT_IMPLEMENTED("GitHubSync::push_all"); }  // STUB: wave2
Result<Json> GitHubSync::sync(const OptFiles&) { return LOOM_NOT_IMPLEMENTED("GitHubSync::sync"); }  // STUB: wave2

GitHubSyncManager::GitHubSyncManager(std::filesystem::path config_path, const Secrets& secrets,
                                     net::HttpTransport& http)
    : path_(std::move(config_path)), secrets_(secrets), http_(http) {}
Result<SyncConfig> GitHubSyncManager::add_config(std::string_view, std::string_view, std::string_view, std::string_view,
                                                 std::string_view) {
  return LOOM_NOT_IMPLEMENTED("GitHubSyncManager::add_config");  // STUB: wave2
}
Status GitHubSyncManager::remove_config(std::string_view) {
  return LOOM_NOT_IMPLEMENTED("GitHubSyncManager::remove_config");  // STUB: wave2
}
std::unique_ptr<GitHubSync> GitHubSyncManager::get_syncer(std::string_view) const { return nullptr; }  // STUB: wave2
std::vector<std::string> GitHubSyncManager::list_configs() const { return {}; }  // STUB: wave2
Json GitHubSyncManager::to_json() const { return Json::object(); }  // STUB: wave2
Status GitHubSyncManager::save() const { return LOOM_NOT_IMPLEMENTED("GitHubSyncManager::save"); }  // STUB: wave2

}  // namespace loom
