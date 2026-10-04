#include <doctest/doctest.h>

#include "loom/log.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;

TEST_SUITE("util profiles") {
  TEST_CASE("temporary directory allocation accepts per-call root prefix and identifier length") {
    fsutil::TempDir base;
    REQUIRE(base.valid());
    auto builtin = test::unwrap(RuntimeProfile::builtin("util"));
    auto custom = test::unwrap(builtin.with_overrides(Json{{"temp", Json{{"base_root", base.path().string()},
        {"prefix", "note_"}, {"id_chars", 20}, {"attempts", 1}}}}));
    std::filesystem::path allocated;
    {
      auto dir = test::unwrap(fsutil::TempDir::create(custom));
      allocated = dir.path();
      CHECK(allocated.parent_path() == base.path());
      CHECK(allocated.filename().string().starts_with("note_"));
      CHECK(allocated.filename().string().size() == 25);
      CHECK(std::filesystem::is_directory(allocated));
      auto explicit_prefix = test::unwrap(fsutil::TempDir::create(custom, "call_"));
      CHECK(explicit_prefix.path().filename().string().starts_with("call_"));
    }
    CHECK_FALSE(std::filesystem::exists(allocated));
    auto disabled = test::unwrap(builtin.with_overrides(Json{{"temp", Json{{"attempts", 0}}}}));
    auto no_attempt = fsutil::TempDir::create(disabled);
    REQUIRE_FALSE(no_attempt);
    CHECK(no_attempt.error().code == Errc::Io);
    auto missing = test::unwrap(builtin.with_overrides(Json{{"temp", Json{{"base_root", (base.path() / "missing").string()}}}}));
    auto no_parent = fsutil::TempDir::create(missing);
    REQUIRE_FALSE(no_parent);
    CHECK(no_parent.error().code == Errc::Io);
  }

  TEST_CASE("temporary directory creation rejects foreign or permissive malformed profiles") {
    auto foreign = test::unwrap(RuntimeProfile::builtin("memory"));
    auto bad_domain = fsutil::TempDir::create(foreign);
    REQUIRE_FALSE(bad_domain);
    CHECK(bad_domain.error().code == Errc::InvalidArgument);
    Json definition = test::unwrap(RuntimeProfile::builtin("util")).definition();
    definition["value_schema"] = Json{{"type", "object"}, {"additionalProperties", true}};
    definition["defaults"]["temp"]["id_chars"] = -1;
    auto malformed = test::unwrap(RuntimeProfile::from_definition(definition));
    auto rejected = fsutil::TempDir::create(malformed);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
  }

  TEST_CASE("record templates and level names are inert per-call data") {
    auto builtin = test::unwrap(RuntimeProfile::builtin("util"));
    auto custom = test::unwrap(builtin.with_overrides(Json{{"log", Json{{"level_names", Json{{"20", "NOTE"}}},
        {"level_width", 0}, {"record_template", "{{level}}|{{logger}}|{{message}}|{{time}}"}}}}));
    const log::Record record{log::Level::Info, "fixture", "literal {{time}}", "12:34:56"};
    CHECK(test::unwrap(record.formatted_checked(custom)) == "NOTE|fixture|literal {{time}}|12:34:56");
    CHECK(test::unwrap(log::level_name(log::Level::Info, custom)) == "NOTE");
    CHECK(record.formatted() == "12:34:56 [INFO ] fixture: literal {{time}}");
    CHECK(log::level_name(log::Level::Info) == "INFO");
    auto broken = test::unwrap(builtin.with_overrides(Json{{"log", Json{{"record_template", "{{unknown}}"}}}}));
    auto result = record.formatted_checked(broken);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::InvalidArgument);
    CHECK(record.message == "literal {{time}}");
    CHECK_FALSE(record.formatted_checked(test::unwrap(RuntimeProfile::builtin("memory"))));
  }

  TEST_CASE("checked record publication uses the scoped template and preserves the ring on renderer failure") {
    auto builtin = test::unwrap(RuntimeProfile::builtin("util"));
    auto custom = test::unwrap(builtin.with_overrides(Json{{"log", Json{{"record_template", "{{logger}}={{message}}"}}}}));
    const auto old_level = log::level();
    log::set_level(log::Level::Info);
    log::set_stderr(false);
    log::set_ring_capacity(500);
    log::clear_recent();
    LOOM_REQUIRE_OK(log::write_checked(log::Level::Info, "fixture", "scoped", custom));
    CHECK(log::recent() == std::vector<std::string>{"fixture=scoped"});
    auto broken = test::unwrap(builtin.with_overrides(Json{{"log", Json{{"record_template", "{{unknown}}"}}}}));
    auto failure = log::write_checked(log::Level::Info, "fixture", "source remains", broken);
    REQUIRE_FALSE(failure);
    CHECK(log::recent() == std::vector<std::string>{"fixture=scoped"});
    log::clear_recent();
    log::set_level(old_level);
  }

  TEST_CASE("profile removal and initialization defaults remain effective without a mutable global profile") {
    auto builtin = test::unwrap(RuntimeProfile::builtin("util"));
    auto custom = test::unwrap(builtin.with_patch(Json::array({
        Json{{"op", "remove"}, {"path", "/log/level_names/20"}},
        Json{{"op", "replace"}, {"path", "/log/default_level_name"}, {"value", "CUSTOM"}},
        Json{{"op", "replace"}, {"path", "/log/recent_lines"}, {"value", 1}},
        Json{{"op", "replace"}, {"path", "/log/default_level"}, {"value", 40}},
        Json{{"op", "replace"}, {"path", "/log/level_environment"}, {"value", ""}},
        Json{{"op", "replace"}, {"path", "/log/stderr_environment"}, {"value", ""}},
        Json{{"op", "replace"}, {"path", "/log/stderr_enabled"}, {"value", false}}})));
    CHECK(test::unwrap(log::level_name(log::Level::Info, custom)) == "CUSTOM");
    CHECK(test::unwrap(log::preset_level(custom)) == log::Level::Error);
    CHECK_FALSE(test::unwrap(log::preset_stderr(custom)));
    const auto old_level = log::level();
    log::set_level(log::Level::Info);
    log::set_stderr(false);
    log::clear_recent();
    LOOM_REQUIRE_OK(log::write_checked(log::Level::Info, "fixture", "one", builtin));
    LOOM_REQUIRE_OK(log::write_checked(log::Level::Info, "fixture", "two", builtin));
    auto recent = test::unwrap(log::recent_checked(custom));
    REQUIRE(recent.size() == 1);
    CHECK(recent[0].find("two") != std::string::npos);
    CHECK(log::recent().size() == 2);
    auto no_retention = test::unwrap(builtin.with_overrides(Json{{"log", Json{{"ring_capacity", 0}}}}));
    LOOM_REQUIRE_OK(log::set_ring_capacity_checked(no_retention));
    LOOM_REQUIRE_OK(log::write_checked(log::Level::Info, "fixture", "unretained", builtin));
    CHECK(log::recent().empty());
    LOOM_REQUIRE_OK(log::set_ring_capacity_checked(builtin));
    log::clear_recent();
    log::set_level(old_level);
  }
}
