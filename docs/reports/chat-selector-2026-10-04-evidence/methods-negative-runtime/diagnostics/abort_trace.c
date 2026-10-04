#define _GNU_SOURCE
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
static void capture_abort(int sig) {
  void *frames[80];
  int n = backtrace(frames, 80);
  backtrace_symbols_fd(frames, n, STDERR_FILENO);
  signal(sig, SIG_DFL);
  raise(sig);
}
__attribute__((constructor)) static void install_trace(void) {
  signal(SIGABRT, capture_abort);
}
