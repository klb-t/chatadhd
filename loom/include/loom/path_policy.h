// loom/path_policy.h — which server-side file paths a runtime may touch (R18 security review).
//
// A single-user runtime is unconfined (the owner may point Loom at any folder of their own machine). A
// runtime opened for an account of loom-server is confined: every filesystem path that arrives from a
// request (catalog/archive/knowledge sources, a repo, a "local" storage target, a prompt directory) must
// lie inside the user's own data directory or one of the roots the operator allows (server_accounts
// profile: path_roots). Without it any signed-in user could read other users' data directories and the
// accounts database. Paths are compared after symlinks of their existing prefix are resolved, so a link
// inside the data dir cannot lead out of it. A check is made when the path is used; a directory entry
// swapped between check and use is outside what this class defends against.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"

namespace loom {

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

 private:
  bool confined_ = false;
  std::vector<std::filesystem::path> roots_;  // canonical
};

}  // namespace loom
