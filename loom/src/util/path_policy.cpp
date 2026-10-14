#include "loom/path_policy.h"

#include <cerrno>
#include <cstring>

#if defined(__linux__) || defined(__ANDROID__)
#include <fcntl.h>
#include <unistd.h>
#define LOOM_HAVE_PROC_FD 1
#endif
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

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

OpenedFile& OpenedFile::operator=(OpenedFile&& o) noexcept {
  if (this != &o) {
    this->~OpenedFile();
    fd_ = o.fd_;
    path_ = std::move(o.path_);
    o.fd_ = -1;
  }
  return *this;
}

OpenedFile::~OpenedFile() {
#ifdef LOOM_HAVE_PROC_FD
  if (fd_ >= 0) ::close(fd_);
#endif
  fd_ = -1;
}

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

Result<OpenedFile> PathPolicy::open_read(const fs::path& path, std::string_view what) const {
  if (!confined_) return OpenedFile(path);
  LOOM_TRY(check(path, what));
  const std::string outside = std::string(what) + " is outside the folders this account may use";
#ifdef LOOM_HAVE_PROC_FD
  // O_NOFOLLOW refuses a symlink as the last component; the real path of the descriptor, read back from the
  // kernel, covers links swapped in anywhere above it between check() and this open.
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return Error(errno == ELOOP ? Errc::InvalidArgument : Errc::Io,
                 std::string(what) + (errno == ELOOP ? " is a symbolic link" : ": cannot open: ") +
                     (errno == ELOOP ? "" : std::strerror(errno)));
  const std::string proc = "/proc/self/fd/" + std::to_string(fd);
  std::error_code ec;
  const fs::path real = fs::read_symlink(proc, ec);
  bool ok = false;
  if (!ec && !real.empty()) {
    const fs::path c = canon(real);
    if (!c.empty())
      for (const auto& r : roots_)
        if (inside(c, r)) { ok = true; break; }
  }
  if (!ok) {
    ::close(fd);
    return Error(Errc::InvalidArgument, outside);
  }
  return OpenedFile(fd, fs::path(proc));
#elif !defined(_WIN32)
  struct stat st;
  if (::lstat(path.c_str(), &st) != 0) return Error(Errc::Io, std::string(what) + ": cannot stat: " + std::strerror(errno));
  if (S_ISLNK(st.st_mode)) return Error(Errc::InvalidArgument, std::string(what) + " is a symbolic link");
  return OpenedFile(path);
#else
  return OpenedFile(path);
#endif
}

}  // namespace loom
