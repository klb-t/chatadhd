// doctest runner for loom_tests. Quiet logging by default; set
// LOOM_TEST_VERBOSE=1 to see engine logs on stderr.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdlib>

#include "loom/log.h"

int main(int argc, char** argv) {
  const char* verbose = std::getenv("LOOM_TEST_VERBOSE");
  if (!(verbose && verbose[0] == '1')) {
    loom::log::set_stderr(false);
  } else {
    loom::log::set_level(loom::log::Level::Debug);
  }
  doctest::Context ctx;
  ctx.applyCommandLine(argc, argv);
  return ctx.run();
}
