// Wave 2 (GRAF_HUB_KOD_SILNIK §5-§7, §9): which actions apply to which element.
// Traits on classes, requirements and flags on actions, scoped rules, extrapolation proposals.
#include <doctest/doctest.h>

#include <filesystem>

#include "loom/settings_fields.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("action_rules") {
  TEST_CASE("settings_fields output is byte-identical to the pinned golden (field edit rules are a special case of the rules)") {
    const auto path = std::filesystem::path(LOOM_TEST_FIXTURES) / "golden" / "settings_fields.json";
    auto golden = fsutil::read_file(path);
    REQUIRE(golden);
    CHECK(unwrap(settings_fields()).dump() == *golden);
  }
}
