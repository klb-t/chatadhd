// Ports of engine/paths.py and engine/config.py.
#include "loom/config.h"

#include <cstdlib>

#include "loom/log.h"
#include "loom/util/fs.h"

namespace loom {
namespace {
constexpr std::string_view kLogPaths = "loom.paths";
constexpr std::string_view kLogCfg = "loom.config";

std::optional<std::string> env_opt(const char* name) {
  const char* v = std::getenv(name);
  if (!v || !*v) return std::nullopt;
  return std::string(v);
}
}  // namespace

// ── Paths ──────────────────────────────────────────────────────────
PathEnv PathEnv::from_process() {
  PathEnv e;
  e.chatadhd_data = env_opt("CHATADHD_DATA");
  e.xdg_data_home = env_opt("XDG_DATA_HOME");
  e.home = env_opt("HOME");
#if defined(__ANDROID__)
  e.is_android = true;
#endif
  return e;
}

std::vector<fs::path> data_dir_candidates(const PathEnv& env) {
  if (env.is_android) {
    fs::path base = "/storage/emulated/0";
    return {base / "Documents" / "ChatADHD", base / "Download" / "chatadhd_data"};
  }
  fs::path home = env.home ? fs::path(*env.home) : fs::path("/");
  if (env.xdg_data_home) return {fs::path(*env.xdg_data_home) / "chatadhd", home / ".chatadhd"};
  return {home / ".chatadhd"};
}

Status ensure_data_dir(const fs::path& p) {
  LOOM_TRY(fsutil::ensure_dir(p));
  fs::path sentinel = p / kSentinelFile;
  std::error_code ec;
  if (!fs::exists(sentinel, ec)) LOOM_TRY(fsutil::write_file(sentinel, "ChatADHD data directory\n"));
  for (const char* sub : {"attachments", "exports", "logs"}) LOOM_TRY(fsutil::ensure_dir(p / sub));
  return {};
}

Result<fs::path> resolve_data_dir(std::optional<std::string_view> override_dir, const PathEnv& env) {
  auto resolve = [&](std::string_view raw) -> fs::path {
    // Path(x).expanduser().resolve() with the injected HOME.
    std::string s(raw);
    if (!s.empty() && s[0] == '~' && (s.size() == 1 || s[1] == '/') && env.home) {
      s = *env.home + s.substr(1);
    }
    return fsutil::resolve_path(s);
  };
  if (override_dir && !override_dir->empty()) {
    fs::path p = resolve(*override_dir);
    LOOM_TRY(ensure_data_dir(p));
    return p;
  }
  if (env.chatadhd_data) {
    fs::path p = resolve(*env.chatadhd_data);
    LOOM_TRY(ensure_data_dir(p));
    return p;
  }
  auto candidates = data_dir_candidates(env);
  for (const auto& c : candidates) {
    std::error_code ec;
    if (fs::exists(c / kSentinelFile, ec)) {
      log::info(kLogPaths, "Found existing data dir: {}", c.string());
      return c;
    }
  }
  fs::path target = candidates.front();
  LOOM_TRY(ensure_data_dir(target));
  log::info(kLogPaths, "Initialized new data dir: {}", target.string());
  return target;
}

DataPaths DataPaths::for_root(const fs::path& root) {
  DataPaths d;
  d.root = root;
  d.db = root / "chatadhd.db";
  d.fts_index = root / "chatadhd.fts.db";
  d.config = root / "config.json";
  d.secrets = root / "secrets.json";
  d.memory = root / "memory.json";
  d.models = root / "models.json";
  d.github_sync = root / "github_sync.json";
  d.attachments = root / "attachments";
  d.exports = root / "exports";
  d.logs = root / "logs";
  d.blobs = root / "blobs";
  return d;
}

// ── JsonStore ──────────────────────────────────────────────────────
JsonStore::JsonStore(fs::path path, Json defaults, bool restrict_perms)
    : path_(std::move(path)), defaults_(std::move(defaults)), restrict_(restrict_perms) {
  std::lock_guard lk(mu_);
  load_locked();
}

void JsonStore::load_locked() {
  data_ = defaults_.is_object() ? defaults_ : Json::object();
  load_error_.clear();
  std::error_code ec;
  if (!fs::exists(path_, ec)) {
    log::info(kLogCfg, "{} not found - using defaults", path_.filename().string());
    return;
  }
  auto raw = fsutil::read_file(path_);
  if (!raw) {
    load_error_ = raw.error().message;
    log::error(kLogCfg, "Failed to load {} - using defaults: {}", path_.filename().string(), load_error_);
    return;
  }
  auto parsed = json::parse(*raw);
  if (!parsed || !parsed->is_object()) {
    load_error_ = parsed ? "root must be a JSON object" : "invalid JSON";
    log::error(kLogCfg, "Failed to load {} - using defaults: {}", path_.filename().string(), load_error_);
    return;
  }
  // Python dict.update(): existing keys keep their position, new ones append.
  for (auto it = parsed->begin(); it != parsed->end(); ++it) data_[it.key()] = it.value();
  log::info(kLogCfg, "Loaded {} ({} keys)", path_.filename().string(), parsed->size());
}

void JsonStore::reload() {
  std::lock_guard lk(mu_);
  load_locked();
}

Json JsonStore::get(std::string_view key, const Json& fallback) const {
  std::lock_guard lk(mu_);
  auto it = data_.find(key);
  return it == data_.end() ? fallback : *it;
}

bool JsonStore::contains(std::string_view key) const {
  std::lock_guard lk(mu_);
  return data_.find(key) != data_.end();
}

void JsonStore::set(std::string_view key, Json value) {
  std::lock_guard lk(mu_);
  data_[std::string(key)] = std::move(value);
}

void JsonStore::erase(std::string_view key) {
  std::lock_guard lk(mu_);
  auto it = data_.find(key);
  if (it != data_.end()) data_.erase(it);
}

Json JsonStore::all() const {
  std::lock_guard lk(mu_);
  return data_;
}

std::vector<std::string> JsonStore::keys() const {
  std::lock_guard lk(mu_);
  std::vector<std::string> out;
  for (auto it = data_.begin(); it != data_.end(); ++it) out.push_back(it.key());
  return out;
}

std::string JsonStore::load_error() const {
  std::lock_guard lk(mu_);
  return load_error_;
}

Status JsonStore::save_locked() const {
  // json.dumps(data, indent=2, ensure_ascii=False)
  json::DumpOptions o;
  o.indent = 2;
  o.ensure_ascii = false;
  fsutil::AtomicWriteOptions wo;
  wo.owner_only = restrict_;
  LOOM_TRY(fsutil::atomic_write(path_, json::py_dumps(data_, o), wo));
  log::debug(kLogCfg, "Saved {} ({} keys)", path_.filename().string(), data_.size());
  return {};
}

Status JsonStore::save() const {
  std::lock_guard lk(mu_);
  return save_locked();
}

// ── Config ─────────────────────────────────────────────────────────
const Json& config_defaults() {
  static const Json kDefaults = Json{
      {"base_url", "https://openrouter.ai/api/v1"},
      {"default_model", "anthropic/claude-sonnet-4-20250514"},
      {"semantic_model", ""},
      {"temperature", 0.7},
      {"max_tokens", 4096},
      {"theme", "dark"},
      {"system_prompt", "You are a helpful assistant with access to the user's hierarchical memory."},
      {"auto_title", true},
      {"stream", true},
      {"semantic_analysis", true},
      {"graph_memory_depth", 2},
      {"graph_memory_max_nodes", 20},
  };
  return kDefaults;
}

const Json& loom_config_defaults() {
  static const Json kLoomDefaults = Json{
      {"loom_event_log_types", Json::array({"conv:created", "import:done", "graph:changed"})},
      {"loom_task_workers", 1},
  };
  return kLoomDefaults;
}

Config::Config(fs::path path) : JsonStore(std::move(path), config_defaults(), false) { auto_upgrade(); }

void Config::auto_upgrade() {
  std::lock_guard lk(mu_);
  // Python: cur = data.copy(); add missing DEFAULTS keys; if upgraded:
  // bump _config_version to 3 and save if old < 3. Preserve user model IDs.
  // Note: _JsonStore seeds data with DEFAULTS before reading the file, so the
  // "missing key" branch can only trigger for keys the file deleted... which
  // cannot happen through update(). We mirror the code path exactly anyway.
  bool upgraded = false;
  const Json& defs = config_defaults();
  for (auto it = defs.begin(); it != defs.end(); ++it) {
    if (data_.find(it.key()) == data_.end()) {
      data_[it.key()] = it.value();
      upgraded = true;
    }
  }
  if (upgraded) {
    std::int64_t ver_old = json::get_int(data_, "_config_version", 0);
    data_["_config_version"] = 3;
    if (ver_old < 3) {
      log::info(kLogCfg, "Config auto-upgraded to v3");
      if (auto st = save_locked(); !st) {
        log::error(kLogCfg, "Config save failed: {}", st.error().message);
      } else {
        upgraded_on_load_ = true;
      }
    }
  }
}

Json Config::get(std::string_view key, const Json& fallback) const {
  {
    std::lock_guard lk(mu_);
    auto it = data_.find(key);
    if (it != data_.end()) return *it;
  }
  const Json& ld = loom_config_defaults();
  auto it = ld.find(key);
  if (it != ld.end() && fallback.is_null()) return *it;
  return fallback;
}

// ── Secrets ────────────────────────────────────────────────────────
Secrets::Secrets(fs::path path) : JsonStore(std::move(path), Json::object(), true) {}

std::string Secrets::get_string(std::string_view key) const {
  Json v = get(key);
  return v.is_string() ? v.get<std::string>() : std::string();
}

bool Secrets::has(std::string_view key) const { return !get_string(key).empty(); }

}  // namespace loom
