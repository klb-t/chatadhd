#include "loom/util/fs.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#include "loom/log.h"
#include "loom/util/ids.h"

namespace loom::fsutil {
namespace {

Error errno_error(const std::string& what, const fs::path& p) {
  return Error(Errc::Io, what + " " + p.string() + ": " + std::strerror(errno));
}

Status write_fd_all(int fd, std::string_view data, const fs::path& p) {
  const char* ptr = data.data();
  std::size_t left = data.size();
  while (left > 0) {
    ssize_t n = ::write(fd, ptr, left);
    if (n < 0) {
      if (errno == EINTR) continue;
      return errno_error("write", p);
    }
    ptr += n;
    left -= static_cast<std::size_t>(n);
  }
  return {};
}

}  // namespace

Status ensure_dir(const fs::path& path) {
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec && !fs::is_directory(path)) return Error(Errc::Io, "mkdir " + path.string() + ": " + ec.message());
  return {};
}

Status atomic_write(const fs::path& path, std::string_view data, const AtomicWriteOptions& opts) {
  if (path.has_parent_path()) LOOM_TRY(ensure_dir(path.parent_path()));
  fs::path tmp = path;
  tmp.replace_extension(".tmp");
  int flags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;
  int fd = ::open(tmp.c_str(), flags, opts.owner_only ? 0600 : 0644);
  if (fd < 0) return errno_error("open", tmp);
  Status st = write_fd_all(fd, data, tmp);
  if (st && opts.fsync && ::fsync(fd) != 0) st = errno_error("fsync", tmp);
  if (::close(fd) != 0 && st) st = errno_error("close", tmp);
  if (!st) {
    ::unlink(tmp.c_str());
    return st;
  }
  if (::rename(tmp.c_str(), path.c_str()) != 0) {
    Error e = errno_error("rename", path);
    ::unlink(tmp.c_str());
    return e;
  }
  if (opts.fsync && path.has_parent_path()) {
    int dfd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
      ::fsync(dfd);
      ::close(dfd);
    }
  }
  if (opts.owner_only) {
    if (auto s = chmod_owner_only(path); !s) {
      log::debug("loom.fs", "chmod 600 failed for {}: {}", path.string(), s.error().message);
    }
  }
  return {};
}

Result<std::string> read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Error(Errc::Io, "cannot open " + path.string());
  std::string data;
  in.seekg(0, std::ios::end);
  auto size = in.tellg();
  if (size > 0) {
    data.resize(static_cast<std::size_t>(size));
    in.seekg(0, std::ios::beg);
    in.read(data.data(), size);
  }
  if (in.bad()) return Error(Errc::Io, "read failed: " + path.string());
  return data;
}

Status write_file(const fs::path& path, std::string_view data) {
  if (path.has_parent_path()) LOOM_TRY(ensure_dir(path.parent_path()));
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return Error(Errc::Io, "cannot open for writing " + path.string());
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!out) return Error(Errc::Io, "write failed: " + path.string());
  return {};
}

Status chmod_owner_only(const fs::path& path) {
  if (::chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) return errno_error("chmod", path);
  return {};
}

Status make_read_only(const fs::path& path) {
  if (::chmod(path.c_str(), S_IRUSR | S_IRGRP | S_IROTH) != 0) return errno_error("chmod", path);
  return {};
}

fs::path expand_user(std::string_view p) {
  if (p.empty() || p[0] != '~') return fs::path(p);
  if (p.size() > 1 && p[1] != '/') return fs::path(p);  // ~user not supported (rare)
  const char* home = std::getenv("HOME");
  if (!home) return fs::path(p);
  return fs::path(home) / fs::path(p.size() > 2 ? p.substr(2) : std::string_view{});
}

fs::path resolve_path(std::string_view p) {
  fs::path x = expand_user(p);
  std::error_code ec;
  fs::path abs = fs::absolute(x, ec);
  if (ec) abs = x;
  fs::path canon = fs::weakly_canonical(abs, ec);
  if (ec) return abs.lexically_normal();
  // weakly_canonical keeps a trailing separator for dirs ("/a/b/"); normalise.
  std::string s = canon.string();
  while (s.size() > 1 && s.back() == '/') s.pop_back();
  return fs::path(s);
}

TempDir::TempDir(std::string_view prefix) {
  auto profile = RuntimeProfile::builtin("util");
  if (profile) (void)initialize(*profile, prefix);
}

Result<TempDir> TempDir::create(const RuntimeProfile& profile, std::optional<std::string_view> prefix) {
  TempDir out(EmptyTag{});
  LOOM_TRY(out.initialize(profile, prefix));
  return out;
}

Status TempDir::initialize(const RuntimeProfile& profile, std::optional<std::string_view> prefix) {
  if (profile.domain() != "util") return Error(Errc::InvalidArgument, "expected util profile");
  LOOM_TRY_ASSIGN(auto builtin, RuntimeProfile::builtin("util"));
  LOOM_TRY_ASSIGN(auto checked, builtin.with_values(profile.values()));
  const auto& values = checked.values().at("temp");
  std::error_code ec;
  fs::path base(values.at("base_root").get<std::string>());
  if (base.empty()) {
    base = fs::temp_directory_path(ec);
    if (ec) base = values.at("fallback_root").get<std::string>();
  }
  const auto resolved_prefix = prefix ? std::string(*prefix) : values.at("prefix").get<std::string>();
  const auto attempts = values.at("attempts").get<std::size_t>();
  for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
    fs::path cand = base / (resolved_prefix + random_hex(values.at("id_chars").get<std::size_t>()));
    if (fs::create_directory(cand, ec) && !ec) {
      path_ = cand;
      return {};
    }
  }
  return Error(Errc::Io, "temporary directory creation failed under " + base.string() +
                            (ec ? ": " + ec.message() : ": configured attempts exhausted"));
}

TempDir::~TempDir() {
  if (!path_.empty()) {
    std::error_code ec;
    // Blobs are chmod 444; make everything writable so removal works.
    for (auto it = fs::recursive_directory_iterator(path_, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
      fs::permissions(it->path(), fs::perms::owner_write, fs::perm_options::add, ec);
    }
    fs::remove_all(path_, ec);
  }
}

TempDir::TempDir(TempDir&& other) noexcept : path_(std::move(other.path_)) { other.path_.clear(); }

TempDir& TempDir::operator=(TempDir&& other) noexcept {
  if (this != &other) {
    TempDir tmp(std::move(*this));
    path_ = std::move(other.path_);
    other.path_.clear();
  }
  return *this;
}

}  // namespace loom::fsutil
