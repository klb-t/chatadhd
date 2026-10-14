// loom/path_policy.h — which server-side file paths a runtime may touch (R18 security review).
//
// A single-user runtime is unconfined (the owner may point Loom at any folder of their own machine). A
// runtime opened for an account of loom-server is confined: every filesystem path that arrives from a
// request (catalog/archive/knowledge sources, a repo, a "local" storage target, a prompt directory) must
// lie inside the user's own data directory or one of the roots the operator allows (server_accounts
// profile: path_roots). Without it any signed-in user could read other users' data directories and the
// accounts database. Paths are compared after symlinks of their existing prefix are resolved, so a link
// inside the data dir cannot lead out of it.
//
// check() alone is a check made at one moment: a directory entry swapped for a symlink between check and
// open would escape it. open_read() closes that for files (R20): it opens with O_NOFOLLOW and then verifies
// what the kernel actually opened (the real path of the descriptor), and hands back a path that names that
// very file (/proc/self/fd/N), so later swaps of the original name change nothing. Where /proc is not
// available it falls back to check() + refusing a symlink as the last component; directory walks are
// refused for symlinks by the callers. Not covered: directory listings (a listed name can be swapped
// before it is opened -- but opening goes through open_read) and code that still opens a checked path
// by name (knowledge/storage paths, see docs/reports/S2_R20_2026-10-14.md).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"

namespace loom {

// A file opened through PathPolicy::open_read. path() is what to read (may differ from the requested name);
// the descriptor, when there is one, is closed with the object.
class OpenedFile {
 public:
  OpenedFile() = default;
  explicit OpenedFile(std::filesystem::path path) : path_(std::move(path)) {}
  OpenedFile(int fd, std::filesystem::path path) : fd_(fd), path_(std::move(path)) {}
  OpenedFile(OpenedFile&& o) noexcept : fd_(o.fd_), path_(std::move(o.path_)) { o.fd_ = -1; }
  OpenedFile& operator=(OpenedFile&& o) noexcept;
  OpenedFile(const OpenedFile&) = delete;
  OpenedFile& operator=(const OpenedFile&) = delete;
  ~OpenedFile();
  const std::filesystem::path& path() const noexcept { return path_; }
  int fd() const noexcept { return fd_; }

 private:
  int fd_ = -1;
  std::filesystem::path path_;
};

class PathPolicy {
 public:
  PathPolicy() = default;
  // Confines to `roots` (the first one is normally the runtime's own data dir). Empty roots + confine
  // = nothing is allowed.
  void confine(std::vector<std::filesystem::path> roots);
  bool confined() const noexcept { return confined_; }
  // Ok when unconfined or when `path` (a request value; "~" is not expanded here) is inside an allowed root.
  // `what` names the field for the error message.
  Status check(const std::filesystem::path& path, std::string_view what = "path") const;
  bool allows(const std::filesystem::path& path) const { return bool(check(path)); }
  // Unconfined: returns `path` unchanged (nothing opened). Confined: see the header comment.
  Result<OpenedFile> open_read(const std::filesystem::path& path, std::string_view what = "file") const;

 private:
  bool confined_ = false;
  std::vector<std::filesystem::path> roots_;  // canonical
};

}  // namespace loom
