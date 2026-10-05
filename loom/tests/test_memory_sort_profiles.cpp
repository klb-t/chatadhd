#include <doctest/doctest.h>

#include "loom/memory_engine.h"
#include "loom/util/fs.h"

using namespace loom;

namespace {
void install_sort_profile(const std::filesystem::path& dir, const Json& keys) {
  REQUIRE(fsutil::ensure_dir(dir / "profiles"));
  const Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "memory"},
                     {"overrides", Json{{"sort_keys", keys}}}};
  REQUIRE(fsutil::write_file(dir / "profiles/memory.pack", json::dump(overlay)));
}

void save_nodes(const std::filesystem::path& path, const Json& nodes) {
  REQUIRE(fsutil::write_file(path, json::dump(nodes)));
}

Json node(std::string_view id, std::string_view content, std::string_view created,
          double weight = 1.0, std::string_view type = "text", Json metadata = Json::object()) {
  return Json{{"id", id}, {"content", content}, {"created", created}, {"weight", weight},
              {"node_type", type}, {"metadata", std::move(metadata)}};
}
}  // namespace

TEST_SUITE("memory_sort_profiles") {
  TEST_CASE("type priority precedes weight descending and content ascending") {
    fsutil::TempDir dir;
    install_sort_profile(dir.path(), Json::array({Json{{"field", "weight"}, {"direction", "descending"}},
                                                 Json{{"field", "content"}, {"direction", "ascending"}}}));
    const auto path = dir.path() / "memory.json";
    save_nodes(path, Json::array({node("low", "zeta", "1", 1.0),
                                  node("high_b", "beta", "2", 3.0),
                                  node("folder", "folder", "9", -4.0, "folder"),
                                  node("high_a", "alpha", "3", 3.0),
                                  node("middle", "middle", "0", 2.0)}));
    MemoryEngine memory(path);
    REQUIRE(memory.profile_status());
    const auto children = memory.get_children_checked();
    REQUIRE(children);
    REQUIRE(children->size() == 5);
    CHECK(children->at(0).id == "folder");
    CHECK(children->at(1).id == "high_a");
    CHECK(children->at(2).id == "high_b");
    CHECK(children->at(3).id == "middle");
    CHECK(children->at(4).id == "low");
  }

  TEST_CASE("empty sort keys preserve insertion ties after type priority") {
    fsutil::TempDir dir;
    install_sort_profile(dir.path(), Json::array());
    const auto path = dir.path() / "memory.json";
    save_nodes(path, Json::array({node("first", "zeta", "9", 1.0),
                                  node("folder_first", "zeta folder", "9", 1.0, "folder"),
                                  node("second", "alpha", "0", 100.0),
                                  node("folder_second", "alpha folder", "0", 2.0, "folder")}));
    MemoryEngine memory(path);
    REQUIRE(memory.profile_status());
    const auto children = memory.get_children_checked();
    REQUIRE(children);
    REQUIRE(children->size() == 4);
    CHECK(children->at(0).id == "folder_first");
    CHECK(children->at(1).id == "folder_second");
    CHECK(children->at(2).id == "first");
    CHECK(children->at(3).id == "second");
  }

  TEST_CASE("metadata objects use deterministic JSON order and stable equal keys") {
    fsutil::TempDir dir;
    install_sort_profile(dir.path(), Json::array({Json{{"field", "metadata"}, {"direction", "ascending"}}}));
    const auto path = dir.path() / "memory.json";
    save_nodes(path, Json::array({node("rank2", "first", "0", 1.0, "text", Json{{"rank", 2}}),
                                  node("rank1_first", "second", "9", 1.0, "text", Json{{"rank", 1}}),
                                  node("rank1_second", "third", "1", 1.0, "text", Json{{"rank", 1}})}));
    MemoryEngine memory(path);
    REQUIRE(memory.profile_status());
    const auto children = memory.get_children_checked();
    REQUIRE(children);
    REQUIRE(children->size() == 3);
    CHECK(children->at(0).id == "rank1_first");
    CHECK(children->at(1).id == "rank1_second");
    CHECK(children->at(2).id == "rank2");
  }

  TEST_CASE("default created key preserves chronological stable ordering") {
    fsutil::TempDir dir;
    const auto path = dir.path() / "memory.json";
    save_nodes(path, Json::array({node("late", "first", "9"),
                                  node("early_first", "second", "1"),
                                  node("early_second", "third", "1")}));
    MemoryEngine memory(path);
    REQUIRE(memory.profile_status());
    const auto children = memory.get_children_checked();
    REQUIRE(children);
    REQUIRE(children->size() == 3);
    CHECK(children->at(0).id == "early_first");
    CHECK(children->at(1).id == "early_second");
    CHECK(children->at(2).id == "late");
  }

  TEST_CASE("unknown fields and directions are profile errors") {
    fsutil::TempDir dir;
    install_sort_profile(dir.path(), Json::array({Json{{"field", "private_field"}, {"direction", "ascending"}}}));
    MemoryEngine unknown_field(dir.path() / "memory.json");
    CHECK(!unknown_field.profile_status());
    CHECK(!unknown_field.get_children_checked());
    install_sort_profile(dir.path(), Json::array({Json{{"field", "content"}, {"direction", "sideways"}}}));
    MemoryEngine unknown_direction(dir.path() / "memory.json");
    CHECK(!unknown_direction.profile_status());
    CHECK(!unknown_direction.get_children_checked());
  }
}
