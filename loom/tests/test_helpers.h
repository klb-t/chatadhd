// Shared helpers for loom unit tests.
#pragma once

#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "loom/db.h"
#include "loom/result.h"
#include "loom/util/fs.h"

namespace loom::test {

// REQUIRE that a Result/Status succeeded, printing the error otherwise.
#define LOOM_REQUIRE_OK(expr)                                                         \
  do {                                                                                \
    auto&& loom_t_r_ = (expr);                                                        \
    INFO((loom_t_r_ ? std::string("ok") : loom_t_r_.error().to_string()));            \
    REQUIRE(static_cast<bool>(loom_t_r_));                                            \
  } while (0)

// Unwrap a Result<T> or fail the test.
template <class T>
T unwrap(Result<T>&& r) {
  if (!r) FAIL("unexpected error: " << r.error().to_string());
  return std::move(r).value();
}

inline std::unique_ptr<Database> open_db(const std::filesystem::path& p, const DbOptions& o = {}) {
  auto r = Database::open(p, o);
  if (!r) FAIL("Database::open failed: " << r.error().to_string());
  return std::move(r).value();
}

}  // namespace loom::test
