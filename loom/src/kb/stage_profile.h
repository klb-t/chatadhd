// Opt-in sub-phase profiling of the knowledge stages. Enabled by the
// environment variable LOOM_STAGE_PROFILE; prints "[prof] <name> <ms> ms" and
// "[count] <name> <n>" lines to stderr and never touches any product, hash or
// database row (so it cannot change outputs). Used by tools/eval/kbeval/bench.py.
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace loom::prof {

inline bool enabled() {
  static const bool e = std::getenv("LOOM_STAGE_PROFILE") != nullptr;
  return e;
}

class Scope {
 public:
  explicit Scope(const char* name)
      : name_(name), t0_(enabled() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
  ~Scope() {
    if (!enabled()) return;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_).count();
    std::fprintf(stderr, "[prof] %s %.1f ms\n", name_, ms);
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  const char* name_;
  std::chrono::steady_clock::time_point t0_;
};

inline void count(const char* name, long n) {
  if (enabled()) std::fprintf(stderr, "[count] %s %ld\n", name, n);
}

}  // namespace loom::prof
