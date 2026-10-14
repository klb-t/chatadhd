#include "loom/path_policy.h"

namespace loom {
namespace fs = std::filesystem;
namespace {

fs::path canon(const fs::path& p) {
  std::error_code ec;
  fs::path abs = fs::absolute(p, ec);
  if (ec) return {};
  fs::path c = fs::weakly_canonical(abs, ec);
  return ec ? fs::path() : c;  // unresolvable (e.g. a symlink loop) = not allowed
}

bool inside(const fs::path& path, const fs::path& root) {
  auto pi = path.begin(), ri = root.begin();
  for (; ri != root.end(); ++ri, ++pi) {
    if (ri->empty()) continue;  // trailing separator
    if (pi == path.end() || *pi != *ri) return false;
  }
  return true;
}

}  // namespace

void PathPolicy::confine(std::vector<fs::path> roots) {
  roots_.clear();
  for (auto& r : roots) {
    fs::path c = r.empty() ? fs::path() : canon(r);
    if (!c.empty()) roots_.push_back(std::move(c));
  }
  confined_ = true;
}

Status PathPolicy::check(const fs::path& path, std::string_view what) const {
  if (!confined_) return {};
  if (path.empty()) return Error(Errc::InvalidArgument, std::string(what) + " must not be empty");
  const fs::path c = canon(path);
  if (!c.empty())
    for (const auto& r : roots_)
    if (inside(c, r)) return {};
  return Error(Errc::InvalidArgument, std::string(what) + " is outside the folders this account may use");
}

}  // namespace loom
