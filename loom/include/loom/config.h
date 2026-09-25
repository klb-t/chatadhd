// loom/config.h — ports of engine/paths.py and engine/config.py.
//
// KOD != DANE: code lives anywhere; data lives in one persistent directory
// shared with the Python app (same file names, same formats).
#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

namespace fs = std::filesystem;

// ── Paths (engine/paths.py) ─────────────────────────────────────────

inline constexpr std::string_view kSentinelFile = ".chatadhd_data";

// Inputs of resolve_data_dir, injectable for tests. from_process() reads
// CHATADHD_DATA, XDG_DATA_HOME and HOME, and detects Android at compile time.
struct PathEnv {
  std::optional<std::string> chatadhd_data;
  std::optional<std::string> xdg_data_home;
  std::optional<std::string> home;
  bool is_android = false;
  static PathEnv from_process();
};

// Python _android_candidates / _desktop_candidates.
std::vector<fs::path> data_dir_candidates(const PathEnv& env);

// Python resolve_data_dir(override): explicit override, then CHATADHD_DATA,
// then the first candidate containing the sentinel, then the first candidate
// (created). The returned directory exists and has the sentinel plus
// attachments/, exports/, logs/.
Result<fs::path> resolve_data_dir(std::optional<std::string_view> override_dir = std::nullopt,
                                  const PathEnv& env = PathEnv::from_process());

// Python _ensure_dir(p).
Status ensure_data_dir(const fs::path& p);

// All well-known locations inside a data dir (same names as Python).
struct DataPaths {
  fs::path root;
  fs::path db;            // chatadhd.db
  fs::path fts_index;     // chatadhd.fts.db (Loom, derived)
  fs::path config;        // config.json
  fs::path secrets;       // secrets.json
  fs::path memory;        // memory.json
  fs::path models;        // models.json
  fs::path github_sync;   // github_sync.json
  fs::path attachments;   // attachments/
  fs::path exports;       // exports/
  fs::path logs;          // logs/
  fs::path blobs;         // blobs/ (Loom content-addressed store)
  static DataPaths for_root(const fs::path& root);
};

// ── JSON stores (engine/config.py) ──────────────────────────────────

// Thread-safe JSON-object file store (Python _JsonStore). set()/erase() only
// change memory; save() writes atomically (<name>.tmp then rename).
class JsonStore {
 public:
  JsonStore(fs::path path, Json defaults, bool restrict_perms);
  virtual ~JsonStore() = default;
  JsonStore(const JsonStore&) = delete;
  JsonStore& operator=(const JsonStore&) = delete;

  Json get(std::string_view key, const Json& fallback = nullptr) const;
  bool contains(std::string_view key) const;
  void set(std::string_view key, Json value);
  void erase(std::string_view key);  // Python delete()
  Json all() const;                  // copy of the whole object
  std::vector<std::string> keys() const;
  Status save() const;
  // Re-read from disk (defaults, then file on top), like constructing anew.
  void reload();

  const fs::path& path() const noexcept { return path_; }
  // Last load problem ("" when the file was fine or absent).
  std::string load_error() const;

 protected:
  void load_locked();
  Status save_locked() const;

  fs::path path_;
  Json defaults_;
  bool restrict_;
  mutable std::recursive_mutex mu_;
  Json data_;
  std::string load_error_;
};

// Python DEFAULTS (config.py). Exactly the same keys and values.
const Json& config_defaults();

// Loom-only policy defaults: served by Config::get() as fallbacks but never
// written into config.json by the auto-upgrade (keeps the file identical to
// what the Python app writes).
//   loom_event_log_types: event types persisted into loom_events
//   loom_task_workers:    TaskEngine worker threads
const Json& loom_config_defaults();

class Config : public JsonStore {
 public:
  // Loads, then runs the Python auto-upgrade (missing default keys added,
  // bad "claude-haiku-4" semantic_model reset, _config_version = 3, saved
  // only when the old version was < 3).
  explicit Config(fs::path path);
  // get() falls back to loom_config_defaults() for loom_* keys.
  Json get(std::string_view key, const Json& fallback = nullptr) const;
  // Whether the constructor upgraded + saved the file.
  bool upgraded_on_load() const noexcept { return upgraded_on_load_; }

 private:
  void auto_upgrade();
  bool upgraded_on_load_ = false;
};

// Credentials; file mode 600 on every save. Values must never be logged or
// returned through the C API (only has/set/delete).
class Secrets : public JsonStore {
 public:
  explicit Secrets(fs::path path);
  std::string get_string(std::string_view key) const;  // "" if missing or not a string
  bool has(std::string_view key) const;                // non-empty string value
};

}  // namespace loom
