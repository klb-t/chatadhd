#include "loom/log.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>

using namespace loom;
namespace {
std::atomic<int> assertions{0};
void check(bool b, const char* why) {
  ++assertions;
  if (!b) throw std::runtime_error(why);
}
struct Capture {
  FILE* file = std::tmpfile();
  int saved = -1;
  Capture() {
    if (!file) throw std::runtime_error("tmpfile failed");
    std::fflush(stderr); saved = dup(STDERR_FILENO);
    if (saved < 0 || dup2(fileno(file), STDERR_FILENO) < 0)
      throw std::runtime_error("stderr capture failed");
  }
  ~Capture() { std::fflush(stderr); dup2(saved, STDERR_FILENO); close(saved); fclose(file); }
  std::string text() {
    std::fflush(stderr);
    // stderr and tmpfile share an fd offset, but their FILE buffers do not.
    // Inspect fd size/read with pread; ftell(file) can cache the previous offset.
    struct stat st{}; check(fstat(fileno(file), &st) == 0, "capture stat");
    std::string out(static_cast<size_t>(st.st_size), '\0');
    check(pread(fileno(file), out.data(), out.size(), 0) == static_cast<ssize_t>(out.size()), "capture read");
    return out;
  }
};
void emit(const RuntimeProfile& p, const std::string& prefix) {
  log::write(log::Level::Info, "synthetic.probe", prefix + "-direct");
  check(bool(log::write_checked(log::Level::Error, "synthetic.probe", prefix + "-checked", p)), "checked write status");
  log::debug("synthetic.probe", "{}-debug", prefix);
  log::info("synthetic.probe", "{}-info", prefix);
  log::warn("synthetic.probe", "{}-warn", prefix);
  log::error("synthetic.probe", "{}-error", prefix);
}
}
int main(int argc, char** argv) {
  try {
    check(argc == 3, "arguments");
    const bool expected = std::string(argv[1]) == "on";
    const bool stderr_expected = std::string(argv[2]) == "on";
    Capture capture;
    check(log::is_enabled() == expected, "startup admission");
    auto profile = RuntimeProfile::builtin("util"); check(bool(profile), "real builtin util");
    log::clear_recent(); log::set_ring_capacity(1000); log::set_level(log::Level::Debug);
    std::vector<std::string> seen; std::mutex seen_mu;
    int token = log::add_sink([&](const log::Record& r) { std::lock_guard lk(seen_mu); seen.push_back(r.message); });
    emit(*profile, "CANARY-start");
    check(log::recent(1000).size() == (expected ? 6u : 0u), "startup ring");
    check(seen.size() == (expected ? 6u : 0u), "startup extra sink");
    auto initial_stderr = capture.text();
    check((!initial_stderr.empty()) == (expected && stderr_expected), "startup stderr");
    if (!expected) {
      log::set_enabled(true); log::set_stderr(true); log::set_level(log::Level::Debug);
      auto override_profile = profile->with_overrides(Json{{"log", {{"default_level", 10}, {"stderr_enabled", true}}}});
      check(bool(override_profile), "valid caller profile"); emit(*override_profile, "CANARY-bypass");
      check(!log::is_enabled(), "deployment ceiling survives setters");
      check(log::recent(1000).empty() && seen.empty() && capture.text().empty(), "all sinks stay empty");
    } else {
      const char* suffixes[] = {"direct", "checked", "debug", "info", "warn", "error"};
      auto ring = log::recent(1000);
      for (const auto* suffix : suffixes) {
        auto marker = std::string("CANARY-start-") + suffix;
        check(std::find(seen.begin(), seen.end(), marker) != seen.end(), "exact callback marker");
        check(std::any_of(ring.begin(), ring.end(), [&](auto& line) { return line.find(marker) != std::string::npos; }), "exact ring marker");
        check((initial_stderr.find(marker) != std::string::npos) == stderr_expected, "exact stderr marker");
      }
      log::set_enabled(false);
      auto old_ring = log::recent(1000); auto old_stderr = capture.text(); auto old_count = seen.size();
      log::set_stderr(true); log::set_level(log::Level::Debug);
      auto override_profile = profile->with_overrides(Json{{"log", {{"default_level", 10}, {"stderr_enabled", true}}}});
      check(bool(override_profile), "caller profile"); emit(*override_profile, "CANARY-disabled");
      check(!log::enabled(log::Level::Error), "level cannot reopen admission");
      check(log::recent(1000) == old_ring, "disabled preserves retained ring");
      check(seen.size() == old_count && capture.text() == old_stderr, "disabled all sinks quiet");
      log::set_enabled(true); emit(*profile, "CANARY-reenabled");
      check(seen.size() == old_count + 6 && log::recent(1000).size() == old_ring.size() + 6, "reenable ring and callback");
      check(capture.text().find("CANARY-reenabled-checked") != std::string::npos, "reenable stderr");
      log::set_level(log::Level::Warning);
      auto n = seen.size(); auto before = capture.text(); auto r = log::recent(1000);
      log::info("synthetic.probe", "CANARY-below-threshold");
      check(seen.size() == n && capture.text() == before && log::recent(1000) == r, "legacy threshold");
      log::set_stderr(false); log::warn("synthetic.probe", "CANARY-stderr-only-off");
      check(seen.size() == n + 1 && log::recent(1000).size() == r.size()+1 && capture.text() == before, "legacy stderr switch");
      log::set_stderr(true); log::set_level(log::Level::Debug);
      // A blocked callback demonstrates that disabling is deliberately not a drain.
      std::mutex mu; std::condition_variable cv; bool entered=false, release=false;
      int blocking = log::add_sink([&](const log::Record& rec) {
        if (rec.message != "CANARY-inflight") return;
        std::unique_lock lk(mu); entered=true; cv.notify_all(); cv.wait(lk,[&]{return release;});
      });
      std::thread inflight([] { log::info("synthetic.probe", "CANARY-inflight"); });
      { std::unique_lock lk(mu); cv.wait(lk,[&]{return entered;}); }
      log::set_enabled(false);
      old_ring=log::recent(1000); old_stderr=capture.text(); old_count=seen.size();
      std::vector<std::thread> workers;
      for(int i=0;i<4;++i) workers.emplace_back([&]{ for(int j=0;j<20;++j) emit(*profile,"CANARY-concurrent-disabled"); });
      for(auto& worker:workers) worker.join();
      check(log::recent(1000)==old_ring && seen.size()==old_count && capture.text()==old_stderr, "post-disable concurrent producers quiet");
      { std::lock_guard lk(mu); release=true; } cv.notify_all(); inflight.join(); log::remove_sink(blocking);
      log::set_enabled(true);
      int self = log::add_sink([](const log::Record&) { log::set_enabled(false); });
      log::warn("synthetic.probe", "CANARY-self-disable");
      check(!log::is_enabled(), "callback can disable without deadlock"); log::remove_sink(self);
      log::clear_recent(); check(log::recent(1000).empty(), "explicit ring clear while disabled");
    }
    log::remove_sink(token);
    std::cout << "PASS assertions=" << assertions.load() << "\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
