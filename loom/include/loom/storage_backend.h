// loom/storage_backend.h — storage backends as interchangeable plugins (P4, R51).
//
// A StorageBackend holds opaque objects under relative keys (content-addressed
// objects use the BlobStore layout "aa/bb/<sha256>"; named objects use any
// other key). Which backends exist, how they are configured and which data
// category lives where is DATA (storage_security pack + <data>/profiles
// overlay); callers only see the registry and the service. Credentials come
// from Secrets by reference and are never logged or returned.
//
// SQLite stays local on purpose: WAL over a network or sync-client mount is
// unsafe. A database is protected by `StorageService::backup`, which copies a
// consistent snapshot to any backend.
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Database;
class Secrets;
namespace net { class HttpTransport; }
}  // namespace loom

namespace loom::storage {

struct ObjectStat {
  std::string key;
  std::int64_t size = 0;
  std::string sha256;  // lowercase hex; empty when the backend cannot tell without reading
  std::string mime;
  Json to_json() const;
};

class StorageBackend {
 public:
  virtual ~StorageBackend() = default;
  virtual const std::string& id() const noexcept = 0;
  virtual std::string kind() const = 0;
  // Creates or replaces. Atomic from a reader's point of view where the medium allows.
  virtual Status put(std::string_view key, std::string_view bytes, std::string_view mime = "") = 0;
  virtual Result<std::string> get(std::string_view key) const = 0;       // Errc::NotFound
  virtual Result<ObjectStat> stat(std::string_view key) const = 0;       // Errc::NotFound
  virtual Result<std::vector<ObjectStat>> list(std::string_view prefix) const = 0;  // sorted by key
  virtual Status remove(std::string_view key) = 0;                       // idempotent
  virtual Status probe() const = 0;                                      // reachability + credentials
};

// Keys are relative, "/"-separated, without "", "." or ".." segments, NUL or backslash.
bool valid_key(std::string_view key) noexcept;
// BlobStore layout for a sha256 hex: "aa/bb/<hash>".
std::string cas_key(std::string_view hash);
// Reads the object and compares its SHA-256 with `expected_sha256`.
Status verify_object(const StorageBackend& b, std::string_view key, std::string_view expected_sha256);

struct BackendEnv {
  std::shared_ptr<net::HttpTransport> http;  // nullptr: network kinds report Errc::Unavailable
  const Secrets* secrets = nullptr;          // credentials by reference (cfg "secret_ref")
  std::filesystem::path data_dir;            // expands {dir} in local paths
};

using BackendFactory = std::function<Result<std::unique_ptr<StorageBackend>>(
    const std::string& id, const Json& cfg, const BackendEnv& env)>;

struct KindDescriptor {
  std::string kind;
  std::string status;  // "implemented" | "planned"
  BackendFactory factory;  // empty for planned kinds
};

class BackendRegistry {
 public:
  static const BackendRegistry& builtin();  // local, s3, webdav, gdrive (planned)
  void add(KindDescriptor d);
  const KindDescriptor* find(std::string_view kind) const;
  std::vector<std::string> kinds() const;
  Result<std::unique_ptr<StorageBackend>> create(const std::string& id, const Json& cfg,
                                                 const BackendEnv& env) const;

 private:
  std::map<std::string, KindDescriptor, std::less<>> kinds_;
};

// ── Placement + service ─────────────────────────────────────────────
struct Target {
  std::string storage;  // key in pack "storages"
  std::string role;     // primary | replica | cache | egress
};

// Ordered targets of a category: the order is the read preference.
class StorageService {
 public:
  // `values` = effective storage_security profile values.
  StorageService(Json values, BackendEnv env, const BackendRegistry* registry = nullptr);
  // Loads the profile (shipped pack + <data_dir>/profiles overlay).
  static Result<std::unique_ptr<StorageService>> load(const BackendEnv& env, Database* db = nullptr,
                                                      const Json& overrides = Json::object());

  void set_database(Database* db) noexcept { db_ = db; }

  std::vector<Target> placement(std::string_view category) const;
  // True when the category is served by the default single local_dir target.
  bool is_default_local(std::string_view category) const;
  // Backend instance for a storage id (cached). "local_dir"-class storages resolve to a LocalBackend at {dir}.
  Result<std::shared_ptr<StorageBackend>> backend(const std::string& storage_id) const;

  // Object operations of a category. Key = "<category>/<key>" in the backend.
  // put: succeeds when >= write_quorum targets accepted; an unreachable one degrades, never blocks.
  // `skip_storage`: a target another component serves itself (BlobStore keeps "local_dir" for its own layout).
  Status put(std::string_view category, std::string_view key, std::string_view bytes, std::string_view mime = "",
             std::string_view skip_storage = "");
  // Tries targets in read preference order; a hit on a later target is a fallback.
  Result<std::string> get(std::string_view category, std::string_view key, std::string_view skip_storage = "") const;
  bool has(std::string_view category, std::string_view key, std::string_view skip_storage = "") const;
  bool placed_on(std::string_view category, std::string_view storage_id) const;

  // CAS convenience (key = cas_key(hash)); records provenance links when a Database is attached.
  Result<std::string> put_cas(std::string_view category, std::string_view bytes, std::string_view mime = "");
  // Provenance only: records "blob:<sha> stored_in <storage>" for every target that holds the object.
  void note_stored(std::string_view category, std::string_view sha256, std::int64_t size);

  struct MoveOptions {
    std::string category;
    std::string to;           // storage id
    bool move = false;        // true: switch placement to `to` only, afterwards
    bool delete_source = false;  // explicit flag: remove from the old targets after verification
    bool dry_run = false;
    bool persist = true;      // write the new placement to the profile overlay
  };
  // copy: add `to` as replica after copying and verifying sha256 of every object;
  // move: additionally makes it the only (primary) target. Source removal only with delete_source.
  // On any failure nothing is switched and nothing is removed (objects already
  // copied to the target are removed again).
  Result<Json> transfer(const MoveOptions& o);

  // Snapshot a file (e.g. a consistent SQLite copy) to a backend as a named object
  // "backups/<name>". Returns {storage,key,sha256,size}.
  Result<Json> backup(const std::filesystem::path& file, const std::string& storage_id, std::string_view name);
  // Consistent snapshot of the attached database via VACUUM INTO, then backup().
  Result<Json> backup_database(const std::string& storage_id);

  // Machine-readable state for the graph entry area and the API.
  Json describe() const;          // backends, categories, placement
  Json probe_all() const;         // + reachability per storage (no secrets)
  Json locate(std::string_view hash) const;  // where an object is held

  const Json& values() const noexcept { return values_; }
  void set_values(Json v);

 private:
  Result<std::vector<std::string>> list_category_keys(std::string_view category, const std::string& storage_id) const;
  Status record_location(std::string_view category, std::string_view key, const std::string& storage_id,
                         std::string_view sha, std::int64_t size);
  Json values_;
  BackendEnv env_;
  const BackendRegistry* registry_;
  Database* db_ = nullptr;
  mutable std::map<std::string, std::shared_ptr<StorageBackend>> cache_;
  mutable std::mutex mu_;
};

}  // namespace loom::storage
