#pragma once
// Per-phase wall/CPU timing for measurements without a profiler (perf/gdb need ptrace, which some containers lack).
// Off by default: a timer records nothing and prints nothing unless the environment variable LOOM_PHASE_TIMING is set
// (any non-empty value other than "0") when the process first asks. Then every finished timer writes one line to
// stderr: "loom-timing <scope> total=<ms> cpu=<ms> | <phase>=<wall ms>:<cpu ms> …" (cpu = this thread's CPU time; wall includes
// waiting for locks, I/O and other threads, so on a loaded machine the CPU column is the comparable one).
// Header-only so the kernel and the server use the same switch. Thread-safe: a timer is a local object of one thread;
// the switch is a function-local static read once.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace loom {

inline bool phase_timing_enabled() {
  static const bool on = [] {
    const char* v = std::getenv("LOOM_PHASE_TIMING");
    return v && *v && std::string(v) != "0";
  }();
  return on;
}

class PhaseTimer {
 public:
  explicit PhaseTimer(std::string scope) : on_(phase_timing_enabled()) {
    if (!on_) return;
    scope_ = std::move(scope);
    start_ = last_ = std::chrono::steady_clock::now();
    cpu0_ = last_cpu_ = thread_cpu_ms();
  }
  PhaseTimer(const PhaseTimer&) = delete;
  PhaseTimer& operator=(const PhaseTimer&) = delete;
  ~PhaseTimer() { finish(); }

  // Closes the phase that ran since the previous mark (or the start) under `name`.
  void mark(const char* name) {
    if (!on_) return;
    const auto now = std::chrono::steady_clock::now();
    const double cpu = thread_cpu_ms();
    phases_.push_back({name, std::chrono::duration<double, std::milli>(now - last_).count(), cpu - last_cpu_});
    last_ = now;
    last_cpu_ = cpu;
  }
  void note(std::string extra) {
    if (on_) extra_ += " " + std::move(extra);
  }
  void finish() {
    if (!on_ || done_) return;
    done_ = true;
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    std::string line = "loom-timing " + scope_ + " total=" + fmt(total) + " cpu=" + fmt(thread_cpu_ms() - cpu0_) + " |";
    for (const auto& p : phases_) line += " " + p.name + "=" + fmt(p.wall) + ":" + fmt(p.cpu);
    line += extra_;
    line += "\n";
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
  }

 private:
  static double thread_cpu_ms() {
    timespec ts{};
#ifdef CLOCK_THREAD_CPUTIME_ID
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
#endif
    return 0;
  }
  static std::string fmt(double ms) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", ms);
    return b;
  }
  bool on_ = false;
  bool done_ = false;
  std::string scope_;
  std::string extra_;
  std::chrono::steady_clock::time_point start_{}, last_{};
  double cpu0_ = 0, last_cpu_ = 0;
  struct Phase {
    std::string name;
    double wall, cpu;
  };
  std::vector<Phase> phases_;
};

}  // namespace loom
