#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "loom/graph_entry.h"
#include "loom/rom.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
// The repository root from the fixtures directory (loom/tests/fixtures): __FILE__ is relative under ccache's CCACHE_BASEDIR.
const std::filesystem::path kRepo = std::filesystem::weakly_canonical(std::filesystem::path(LOOM_TEST_FIXTURES) / ".." / ".." / "..");
}

TEST_SUITE("rom") {
  TEST_CASE("the catalogue lists the 13 section-3 items; each has files or is marked missing") {
    auto rom = unwrap(rom::Rom::open());
    const auto ids = rom.ids();
    REQUIRE(ids.size() == 13);
    CHECK(ids.front() == "boot");
    CHECK(ids.back() == "catalogue");
    int missing = 0;
    for (const auto& id : ids) {
      const Json item = unwrap(rom.read(id));
      const std::string status = item["status"].get<std::string>();
      CHECK(item["address"] == "rom:" + id);
      // M1-D1: what the item is for, in both languages (from the generator, never one language only)
      INFO(id);
      REQUIRE(item["why"].is_object());
      CHECK_FALSE(json::get_string(item["why"], "en").empty());
      CHECK_FALSE(json::get_string(item["why"], "pl").empty());
      CHECK(item["why"]["pl"] != item["why"]["en"]);
      CHECK_FALSE(json::get_string(item["label"], "pl").empty());
      if (item["files"].empty()) {
        CHECK(status == "missing");
        CHECK_FALSE(item["gaps"].empty());  // a missing item says why
        ++missing;
      } else {
        CHECK(status != "missing");
        for (const auto& f : item["files"]) {
          const auto path = kRepo / f["path"].get<std::string>();
          CHECK_MESSAGE(std::filesystem::is_regular_file(path), path.string());
          CHECK_FALSE(f.contains("sha256"));  // nothing is pinned in the repo
        }
      }
    }
    CHECK(unwrap(rom.read("rom:rescue_mode"))["status"] == "missing");  // rescue mode is honestly absent
    CHECK(missing >= 1);
    CHECK(rom.status_counts().contains("missing"));
  }

  TEST_CASE("hashes are computed on demand from the tree") {
    auto rom = unwrap(rom::Rom::open());
    const Json h = rom.with_hashes(kRepo);
    for (const auto& item : h)
      for (const auto& f : item["files"]) CHECK(f["sha256"].get<std::string>().size() == 64);
    CHECK(rom.with_hashes(kRepo / "nowhere")["boot"]["files"][0]["unreadable"] == true);
  }

  TEST_CASE("the ROM refuses every write, with a reason") {
    auto rom = unwrap(rom::Rom::open());
    auto w = rom.write("rom:boot", Json::object());
    REQUIRE_FALSE(w.has_value());
    CHECK(w.error().code == Errc::Unsupported);
    CHECK(w.error().message.find("rom_read_only") != std::string::npos);
    CHECK(w.error().message.find("overlay") != std::string::npos);
    auto r = rom.remove("boot");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::Unsupported);
    CHECK_FALSE(rom.read("rom:nope").has_value());

    fsutil::TempDir td;
    auto reg = rom::MountRegistry::standard(td.path());
    auto mw = reg.write("rom", "rom:boot", Json{{"status", "present"}});
    REQUIRE_FALSE(mw.has_value());
    CHECK(mw.error().message.find("rom_read_only") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(td.path() / "profiles"));  // nothing was written
  }

  TEST_CASE("mounting: ROM first and unremovable; a user copy in the writable memory overrides") {
    fsutil::TempDir td;
    auto reg = rom::MountRegistry::standard(td.path());
    REQUIRE(reg.mounts().size() == 2);
    CHECK(reg.mounts()[0].name == "rom");
    CHECK_FALSE(reg.mounts()[0].writable);
    CHECK_FALSE(reg.add(rom::Mount{"rom", true, td.path()}).has_value());   // reserved
    CHECK_FALSE(reg.add(rom::Mount{"user", true, td.path()}).has_value());  // duplicate
    LOOM_REQUIRE_OK(reg.add(rom::Mount{"team", false, {}}));
    CHECK(reg.mounts()[0].name == "rom");                                    // still first

    auto before = unwrap(reg.read("rom:rescue_mode"));
    CHECK(before["source"] == "rom");
    LOOM_REQUIRE_OK(reg.write("user", "rom:rescue_mode", Json{{"note", "my own rescue plan"}}));
    auto after = unwrap(reg.read("rom:rescue_mode"));
    CHECK(after["source"] == "overlay");
    CHECK(after["item"]["note"] == "my own rescue plan");
    CHECK(after["rom_item"] == before["rom_item"]);  // the ROM itself is untouched
    // The overlay is the ordinary graph_entry profile overlay.
    auto profile = unwrap(RuntimeProfile::load("graph_entry", td.path()));
    CHECK(profile.values()["rom"]["items"]["rescue_mode"]["note"] == "my own rescue plan");
    CHECK(profile.values()["rom"]["items"]["rescue_mode"]["status"] == "missing");
    CHECK_FALSE(reg.write("user", "rom:no_such_item", Json::object()).has_value());
    LOOM_REQUIRE_OK(reg.remove("user", "rom:rescue_mode"));
    CHECK(unwrap(reg.read("rom:rescue_mode"))["source"] == "rom");
  }

  TEST_CASE("the graph entry shows the ROM, counted by status, and the overlay can hide an item") {
    fsutil::TempDir td;
    graph_entry::Env env;
    env.data_dir = td.path();
    auto view = unwrap(graph_entry::compute(env));
    const Json* area = nullptr;
    for (const auto& a : view["areas"]) if (a["id"] == "rom") area = &a;
    REQUIRE(area);
    const Json& c = (*area)["counts"]["rom_items"];
    REQUIRE(c["known"] == true);
    CHECK(c["value"] == 13);
    CHECK(c["by"]["missing"].get<int>() >= 1);
    CHECK(view["areas"][0]["id"] == "rom");  // mounted first

    LOOM_REQUIRE_OK(fsutil::ensure_dir(td.path() / "profiles"));
    LOOM_REQUIRE_OK(fsutil::write_file(
        td.path() / "profiles/graph_entry.pack",
        json::dump(Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "graph_entry"},
                        {"overrides", Json{{"rom", Json{{"items", Json{{"boot", Json{{"hidden", true}}}}}}}}}})));
    auto view2 = unwrap(graph_entry::compute(env));
    for (const auto& a : view2["areas"]) if (a["id"] == "rom") CHECK(a["counts"]["rom_items"]["value"] == 12);
  }
}

#include "loom/runtime.h"
TEST_CASE("rom: the Runtime mounts the ROM first") {
  fsutil::TempDir td;
  RuntimeOptions o;
  o.data_dir = td.path().string();
  o.start_workers = false;
  auto rt = unwrap(Runtime::open(o));
  REQUIRE(rt->mounts().mounts().size() == 2);
  CHECK(rt->mounts().mounts()[0].name == "rom");
  CHECK_FALSE(rt->mounts().mounts()[0].writable);
  CHECK(unwrap(rt->mounts().read("rom:boot"))["source"] == "rom");
}
