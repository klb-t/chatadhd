#include <algorithm>
#include <filesystem>

#include "loom/path_policy.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "storage_internal.h"

namespace loom::storage::detail {
namespace {
namespace sfs = std::filesystem;

class LocalBackend final : public StorageBackend {
 public:
  LocalBackend(std::string id, sfs::path root) : id_(std::move(id)), root_(std::move(root)) {}
  const std::string& id() const noexcept override { return id_; }
  std::string kind() const override { return "local"; }

  Status put(std::string_view key, std::string_view bytes, std::string_view) override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    sfs::path p = root_ / std::string(key);
    LOOM_TRY(fsutil::ensure_dir(p.parent_path()));
    std::error_code ec;
    if (sfs::exists(p, ec)) sfs::permissions(p, sfs::perms::owner_write, sfs::perm_options::add, ec);  // CAS files are 0444
    return fsutil::atomic_write(p, bytes);
  }
  Result<std::string> get(std::string_view key) const override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    sfs::path p = root_ / std::string(key);
    std::error_code ec;
    if (!sfs::is_regular_file(p, ec)) return Error(Errc::NotFound, "no such object: " + std::string(key));
    return fsutil::read_file(p);
  }
  Result<ObjectStat> stat(std::string_view key) const override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    sfs::path p = root_ / std::string(key);
    std::error_code ec;
    if (!sfs::is_regular_file(p, ec)) return Error(Errc::NotFound, "no such object: " + std::string(key));
    ObjectStat s;
    s.key = std::string(key);
    s.size = static_cast<std::int64_t>(sfs::file_size(p, ec));
    LOOM_TRY_ASSIGN(s.sha256, sha256_file_hex(p));
    return s;
  }
  Result<std::vector<ObjectStat>> list(std::string_view prefix) const override {
    std::vector<ObjectStat> out;
    std::error_code ec;
    if (!sfs::is_directory(root_, ec)) return out;
    for (sfs::recursive_directory_iterator it(root_, sfs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      std::string rel = sfs::relative(it->path(), root_, ec).generic_string();
      if (rel.rfind("tmp/", 0) == 0 || rel.size() < prefix.size() || rel.compare(0, prefix.size(), prefix) != 0) continue;
      ObjectStat s;
      s.key = rel;
      s.size = static_cast<std::int64_t>(it->file_size(ec));
      out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const ObjectStat& a, const ObjectStat& b) { return a.key < b.key; });
    return out;
  }
  Status remove(std::string_view key) override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    std::error_code ec;
    sfs::path p = root_ / std::string(key);
    sfs::permissions(p, sfs::perms::owner_write, sfs::perm_options::add, ec);
    sfs::remove(p, ec);
    return {};
  }
  Status probe() const override {
    std::error_code ec;
    if (sfs::is_directory(root_, ec)) return {};
    if (sfs::exists(root_, ec)) return Error(Errc::Io, "storage root is not a directory");
    return {};  // an absent root is an empty store; it is created on the first write
  }

 private:
  std::string id_;
  sfs::path root_;
};
}  // namespace

Result<std::unique_ptr<StorageBackend>> make_local(const std::string& id, const Json& cfg, const BackendEnv& env) {
  std::string path = json::get_string(cfg, "path", "{dir}");
  sfs::path root(expand_dir(path, env.data_dir));
  // Accounts (R18): a user's placement overlay must not aim a local target at another user's data or the server.
  if (env.path_policy) LOOM_TRY(env.path_policy->check(root, "storage target '" + id + "' path"));
  return std::unique_ptr<StorageBackend>(new LocalBackend(id, std::move(root)));
}

}  // namespace loom::storage::detail
