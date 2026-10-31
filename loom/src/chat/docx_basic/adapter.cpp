#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "adapter.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <limits>
#include <string>
#include <vector>

#if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)
#if __GLIBC_PREREQ(2, 34)
#define LOOM_DOCX_SPAWN 1
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#endif

namespace loom::chat {
namespace {
#ifdef LOOM_DOCX_SPAWN
struct Fd {
  int value = -1;
  explicit Fd(int fd = -1) : value(fd) {}
  ~Fd() { if (value >= 0) ::close(value); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
struct Child {
  pid_t pid = -1;
  ~Child() {
    if (pid > 0) {
      ::kill(pid, SIGKILL);
      while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    }
  }
};
struct SpawnActions {
  posix_spawn_file_actions_t actions;
  bool valid = ::posix_spawn_file_actions_init(&actions) == 0;
  ~SpawnActions() { if (valid) ::posix_spawn_file_actions_destroy(&actions); }
};

Result<std::int64_t> quantity(const Json& reader, const char* key, bool positive = false) {
  const auto it = reader.find(key);
  if (it == reader.end() || !it->is_number_integer() ||
      (it->is_number_unsigned() && it->get<std::uint64_t>() >
       static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())))
    return Error(Errc::InvalidArgument, "docx_configuration_invalid");
  const auto value = it->get<std::int64_t>();
  if (value < 0 || (positive && value == 0))
    return Error(Errc::InvalidArgument, "docx_configuration_invalid");
  return value;
}
#endif
}  // namespace

Result<Json> read_docx_basic(const std::filesystem::path& path, const Json& reader) {
#ifdef LOOM_DOCX_SPAWN
  const std::string python = json::get_string(reader, "python");
  const std::string script = json::get_string(reader, "script");
  // Deployment locations are explicit configuration. No PATH lookup, shell,
  // inherited Python site/user hooks, environment secrets or inherited fds.
  if (python.empty() || script.empty() || !std::filesystem::path(python).is_absolute() ||
      !std::filesystem::path(script).is_absolute() || python.find('\0') != std::string::npos ||
      script.find('\0') != std::string::npos)
    return Error(Errc::Unavailable, "docx_reader_unavailable");
  struct stat script_stat{};
  if (::access(python.c_str(), X_OK) != 0 || ::stat(script.c_str(), &script_stat) != 0 ||
      !S_ISREG(script_stat.st_mode) || ::access(script.c_str(), R_OK) != 0)
    return Error(Errc::Unavailable, "docx_reader_unavailable");
  const auto limits = reader.find("limits");
  if (limits == reader.end() || !limits->is_object())
    return Error(Errc::InvalidArgument, "docx_configuration_invalid");
  LOOM_TRY_ASSIGN(const auto max_input, quantity(*limits, "max_input_bytes"));
  LOOM_TRY_ASSIGN(const auto timeout_ms, quantity(reader, "timeout_ms"));
  LOOM_TRY_ASSIGN(const auto max_output, quantity(reader, "max_output_bytes"));
  LOOM_TRY_ASSIGN(const auto poll_ms, quantity(reader, "poll_ms", true));
  Fd input(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
  struct stat st{};
  if (input.value < 0 || ::fstat(input.value, &st) != 0 || !S_ISREG(st.st_mode))
    return Error(Errc::Io, "docx_input_unavailable");
  if (max_input && st.st_size > max_input)
    return Error(Errc::Parse, "docx_limit_max_input_bytes");
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC) != 0) return Error(Errc::Io, "docx_reader_unavailable");
  Fd output(fds[0]);
  Fd writer(fds[1]);
  SpawnActions actions;
  if (!actions.valid ||
      ::posix_spawn_file_actions_adddup2(&actions.actions, input.value, STDIN_FILENO) != 0 ||
      ::posix_spawn_file_actions_adddup2(&actions.actions, writer.value, STDOUT_FILENO) != 0 ||
      ::posix_spawn_file_actions_addopen(&actions.actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0 ||
      ::posix_spawn_file_actions_addclosefrom_np(&actions.actions, 3) != 0)
    return Error(Errc::Io, "docx_reader_unavailable");
  std::vector<std::string> args{python, "-I", "-B", "-S", script, "-", limits->dump()};
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  std::string locale = "LANG=C.UTF-8";
  char* env[] = {locale.data(), nullptr};
  Child child;
  if (::posix_spawn(&child.pid, python.c_str(), &actions.actions, nullptr, argv.data(), env) != 0) {
    child.pid = -1;
    return Error(Errc::Unavailable, "docx_reader_unavailable");
  }
  ::close(writer.value);
  writer.value = -1;
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  std::string raw;
  bool eof = false;
  int status = 0;
  while (true) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    if (timeout_ms && elapsed >= timeout_ms) return Error(Errc::Timeout, "docx_reader_timeout");
    const auto wait = std::min(poll_ms, timeout_ms ? timeout_ms - elapsed : poll_ms);
    const int wait_ms = static_cast<int>(std::min<std::int64_t>(wait, (std::numeric_limits<int>::max)()));
    struct pollfd fd{output.value, POLLIN, 0};
    const int ready = ::poll(eof ? nullptr : &fd, eof ? 0 : 1, wait_ms);
    if (ready < 0 && errno != EINTR) return Error(Errc::Io, "docx_reader_failed");
    if (ready > 0 && !eof) {
      if (fd.revents & (POLLERR | POLLNVAL)) return Error(Errc::Io, "docx_reader_failed");
      char buffer[4096];  // transport buffer, not a resource ceiling
      const auto got = ::read(output.value, buffer, sizeof(buffer));
      if (got < 0 && errno != EINTR) return Error(Errc::Io, "docx_reader_failed");
      if (got == 0) eof = true;
      if (got > 0) {
        const auto n = static_cast<std::size_t>(got);
        if (max_output && (raw.size() > static_cast<std::uint64_t>(max_output) ||
            n > static_cast<std::uint64_t>(max_output) - raw.size()))
          return Error(Errc::Parse, "docx_limit_output_bytes");
        raw.append(buffer, n);
      }
    }
    // Reap only after stdout EOF; no blocking wait beyond the configured timeout.
    if (eof) {
      const auto done = ::waitpid(child.pid, &status, WNOHANG);
      if (done == child.pid) { child.pid = -1; break; }
      if (done < 0 && errno != EINTR) { child.pid = -1; return Error(Errc::Io, "docx_reader_failed"); }
    }
  }
  const auto parsed = json::parse(raw);
  if (!parsed || !parsed->is_object()) return Error(Errc::Parse, "docx_reader_failed");
  if (parsed->contains("error")) {
    std::string code = json::get_string(parsed->at("error"), "code");
    if (code == "document_reader_unavailable") code = "docx_reader_unavailable";
    if (code == "document_no_text") code = "docx_no_text";
    return Error(code == "docx_reader_unavailable" ? Errc::Unavailable : Errc::Parse,
                 code.empty() ? "docx_reader_failed" : code);
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
      json::get_string(*parsed, "schema") != "loom.docx_basic.result/1" ||
      !parsed->contains("text") || !parsed->at("text").is_string() ||
      !parsed->contains("warnings") || !parsed->at("warnings").is_array() ||
      !parsed->contains("source") || !parsed->at("source").is_object() ||
      !parsed->contains("extractor") || !parsed->at("extractor").is_string() ||
      !parsed->contains("partial") || !parsed->at("partial").is_boolean())
    return Error(Errc::Parse, "docx_reader_failed");
  for (const auto& warning : parsed->at("warnings"))
    if (!warning.is_string()) return Error(Errc::Parse, "docx_reader_failed");
  return *parsed;
#else
  (void)path;
  (void)reader;
  return Error(Errc::Unavailable, "docx_reader_unavailable");
#endif
}
}  // namespace loom::chat
