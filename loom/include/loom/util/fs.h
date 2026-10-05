// loom/util/fs.h — filesystem helpers (atomic writes, permissions, temp dirs).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "loom/result.h"
#include "loom/runtime_profile.h"

namespace loom::fsutil {

namespace fs = std::filesystem;

// Python _JsonStore.save(): write `<path with suffix .tmp>` then rename over
// `path` (atomic on POSIX). The temp name follows Path.with_suffix(".tmp"):
// "config.json" -> "config.tmp". Parent directories are created.
struct AtomicWriteOptions {
  bool fsync = true;            // flush file + directory before/after rename
  bool owner_only = false;      // chmod 600 after the rename (secrets)
};
Status atomic_write(const fs::path& path, std::string_view data, const AtomicWriteOptions& opts = {});

Result<std::string> read_file(const fs::path& path);
Status write_file(const fs::path& path, std::string_view data);  // non-atomic
Status ensure_dir(const fs::path& path);

// chmod 0600. Best-effort semantics are the caller's choice: returns an error
// that callers may log and ignore (Android may not support chmod).
Status chmod_owner_only(const fs::path& path);
Status make_read_only(const fs::path& path);  // chmod 0444

// "~" / "~/x" expansion (Python Path.expanduser) using $HOME.
fs::path expand_user(std::string_view p);

// Python Path(p).expanduser().resolve(): absolute + lexically normal +
// symlinks resolved for the existing prefix.
fs::path resolve_path(std::string_view p);

// Unique directory under the system temp dir; removed recursively on
// destruction (RAII). Used by tests and the ZIP importer.
class TempDir {
 public:
  // Retain the historical exported constructor and source-level default for
  // existing C++ consumers. create(profile) uses the data-owned prefix preset.
  explicit TempDir(std::string_view prefix = "loom_");
  // Per-call preset plus caller prefix. An empty temp.base_root discovers
  // the system temp directory; fallback_root is used only on discovery error.
  // Legacy constructors preserve valid()==false on allocation failure;
  // checked creation exposes schema/filesystem failures without global state.
  static Result<TempDir> create(const RuntimeProfile& profile,
                                std::optional<std::string_view> prefix = std::nullopt);
  ~TempDir();
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&& other) noexcept;
  TempDir& operator=(TempDir&& other) noexcept;

  const fs::path& path() const noexcept { return path_; }
  bool valid() const noexcept { return !path_.empty(); }
  // Keep the directory (debugging).
  void release() noexcept { path_.clear(); }

 private:
  struct EmptyTag {};
  explicit TempDir(EmptyTag) {}
  Status initialize(const RuntimeProfile& profile, std::optional<std::string_view> prefix);
  fs::path path_;
};

}  // namespace loom::fsutil
