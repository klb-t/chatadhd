#include <doctest/doctest.h>

#include "loom/memory_engine.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;

TEST_CASE("memory profile controls weight visibility independently from default creation weight") {
  fsutil::TempDir dir;
  MemoryEngine memory(dir.path() / "memory.json");
  const auto id = test::unwrap(memory.add_node("fixture"));
  CHECK(memory.get_active_context() == "fixture");
  auto overlay = Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "memory"},
                      {"overrides", Json{{"show_default_weight", true}}}};
  LOOM_REQUIRE_OK(fsutil::write_file(dir.path() / "profiles" / "memory.pack", json::dump(overlay)));
  LOOM_REQUIRE_OK(memory.reload());
  CHECK(memory.get_active_context() == "fixture [w:1.0]");
  LOOM_REQUIRE_OK(memory.update_node(id, Json{{"weight", 2.5}}));
  CHECK(memory.get_active_context() == "fixture [w:2.5]");
  overlay["overrides"]["show_weights"] = false;
  LOOM_REQUIRE_OK(fsutil::write_file(dir.path() / "profiles" / "memory.pack", json::dump(overlay)));
  LOOM_REQUIRE_OK(memory.reload());
  CHECK(memory.get_active_context() == "fixture");
  CHECK(memory.get_node(id)->weight == 2.5);
}
