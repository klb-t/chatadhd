// Shared helpers for loom unit tests.
#pragma once

#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "loom/db.h"
#include "loom/net/http.h"
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

// One HttpRequest as ordered JSON (method, url, headers in wire order, body,
// timeout, stream flag): the golden-bytes view the use-model migration pins.
inline Json request_json(const net::HttpRequest& r) {
  Json headers = Json::array();
  for (const auto& [name, value] : r.headers) headers.push_back(Json::array({name, value}));
  return Json{{"method", r.method}, {"url", r.url}, {"headers", headers},
              {"body", r.body}, {"timeout_ms", r.timeout_ms}, {"stream", r.stream}};
}

#ifdef LOOM_TEST_FIXTURES
// A pinned request from tests/fixtures/golden/<name>.json, in request_json()'s shape.
inline Json golden_request(const std::string& name) {
  const auto path = std::filesystem::path(LOOM_TEST_FIXTURES) / "golden" / (name + ".json");
  auto text = fsutil::read_file(path);
  if (!text) FAIL("golden fixture missing: " << path.string());
  auto parsed = json::parse(*text);
  if (!parsed) FAIL("golden fixture is not JSON: " << path.string());
  return *parsed;
}
#endif

// Declares an action's status through the user overlay of the actions profile.
inline void declare_action_status(const std::filesystem::path& data_dir, const std::string& name,
                                    const std::string& status) {
  std::filesystem::create_directories(data_dir / "profiles");
  Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "actions"},
               {"overrides", Json{{"actions", Json{{name, Json{{"status", status}}}}}}}};
  auto written = fsutil::write_file(data_dir / "profiles" / "actions.pack", overlay.dump());
  if (!written) FAIL("write overlay failed: " << written.error().to_string());
}

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
