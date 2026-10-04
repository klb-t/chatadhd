#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
Json definition() {
  return unwrap(json::parse(R"PROFILE({
    "schema":"loom.runtime_profile/1", "domain":"example", "revision":1,
    "defaults":{"count":5000,"names":["first","second"],"nested":{"enabled":true,"weight":0.5}},
    "value_schema":{
      "type":"object","additionalProperties":false,"required":["count","names","nested"],
      "properties":{
        "count":{"type":"integer","minimum":0},
        "names":{"type":"array","items":{"type":"string"}},
        "nested":{"type":"object","additionalProperties":false,"required":["enabled","weight"],
          "properties":{"enabled":{"type":"boolean"},"weight":{"type":"number"}}}
      }
    }
  })PROFILE"));
}
}

TEST_SUITE("runtime_profile") {
  TEST_CASE("partial overlays retain unrelated values and replace arrays; no invented quota") {
    auto base = unwrap(RuntimeProfile::from_definition(definition()));
    auto changed = unwrap(base.with_overrides(Json{{"count", 1000000000}, {"names", Json::array()},
                                                   {"nested", Json{{"weight", -3.0}}}}));
    CHECK(base.is_builtin());
    CHECK_FALSE(changed.is_builtin());
    CHECK(changed.values()["count"] == 1000000000);
    CHECK(changed.values()["names"].empty());
    CHECK(changed.values()["nested"]["enabled"] == true);
    CHECK(changed.values()["nested"]["weight"] == -3.0);
    CHECK(base.values()["count"] == 5000);
    CHECK(changed.hash() != base.hash());
    CHECK(changed.inspection()["values"] == changed.values());
    CHECK(unwrap(changed.with_overrides(Json::object())).hash() == changed.hash());
  }

  TEST_CASE("invalid overrides and unsupported schema assertions are explicit") {
    auto base = unwrap(RuntimeProfile::from_definition(definition()));
    CHECK_FALSE(base.with_overrides(Json{{"count", -1}}));
    CHECK_FALSE(base.with_overrides(Json{{"count", 4.5}}));
    auto floating_count = base.values();
    floating_count["count"] = 5000.0;
    CHECK_FALSE(base.with_values(floating_count));
    CHECK_FALSE(base.with_overrides(Json{{"nested", Json{{"enabled", "yes"}}}}));
    CHECK_FALSE(base.with_overrides(Json{{"silent_typo", 7}}));
    CHECK_FALSE(base.with_overrides(Json::array()));
    auto doc = definition();
    doc["value_schema"]["properties"]["count"]["maximum"] = 20;
    CHECK_FALSE(RuntimeProfile::from_definition(doc));
    doc = definition();
    doc["value_schema"]["properties"]["names"]["uniqueItems"] = true;
    CHECK_FALSE(RuntimeProfile::from_definition(doc));
    doc = definition();
    doc["value_schema"]["title"] = 9;
    CHECK_FALSE(RuntimeProfile::from_definition(doc));
    doc = definition();
    doc["ignored_descriptor_setting"] = true;
    CHECK_FALSE(RuntimeProfile::from_definition(doc));
    CHECK_FALSE(RuntimeProfile::load("../secrets"));
  }

  TEST_CASE("patch removes dictionary entries without restoring defaults and validates final values") {
    auto doc = definition();
    doc["defaults"]["labels"] = Json{{"one", "first"}, {"two", "second"}};
    doc["value_schema"]["properties"]["labels"] = Json{{"type", "object"}, {"additionalProperties", Json{{"type", "string"}}}};
    auto base = unwrap(RuntimeProfile::from_definition(doc));
    auto changed = unwrap(base.with_patch(Json::array({Json{{"op", "remove"}, {"path", "/labels/one"}}})));
    CHECK_FALSE(changed.values()["labels"].contains("one"));
    CHECK_FALSE(unwrap(changed.with_overrides(Json{{"count", 17}})).values()["labels"].contains("one"));
    CHECK_FALSE(base.with_patch(Json::array({Json{{"op", "remove"}, {"path", "/count"}}})));
    CHECK_FALSE(base.with_patch(Json::array({Json{{"op", "replace"}, {"path", "/count"}, {"value", "wrong type"}}})));
    CHECK_FALSE(base.with_patch(Json::array({Json{{"op", "remove"}, {"path", "/absent"}}})));
    CHECK_FALSE(base.with_patch(Json::object()));
    Json saved{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "example"}, {"overrides", Json::object()},
               {"patch", Json::diff(base.values(), changed.values())}};
    auto restored = unwrap(base.with_overlay(saved));
    CHECK(restored.values() == changed.values());
    CHECK(restored.hash() == changed.hash());
    saved["ignored"] = true;
    CHECK_FALSE(base.with_overlay(saved));
  }

  TEST_CASE("integer bounds and enums remain exact across signed, unsigned and floating encodings") {
    auto doc = definition();
    doc["defaults"]["count"] = 0;
    doc["value_schema"]["properties"]["count"]["maximum"] = 0;
    auto zero = unwrap(RuntimeProfile::from_definition(doc));
    CHECK_FALSE(zero.with_overrides(Json{{"count", std::numeric_limits<std::uint64_t>::max()}}));
    CHECK(unwrap(zero.with_overrides(Json{{"count", std::uint64_t{0}}})).is_builtin());
    doc["value_schema"]["properties"]["count"]["maximum"] = 9007199254740992.0;
    auto precise = unwrap(RuntimeProfile::from_definition(doc));
    CHECK(precise.with_overrides(Json{{"count", std::uint64_t{9007199254740992ULL}}}));
    CHECK_FALSE(precise.with_overrides(Json{{"count", std::uint64_t{9007199254740993ULL}}}));
    doc["defaults"]["count"] = -1;
    doc["value_schema"]["properties"]["count"] = Json{{"type", "integer"}, {"enum", Json::array({-1})}};
    auto enumerated = unwrap(RuntimeProfile::from_definition(doc));
    CHECK_FALSE(enumerated.with_overrides(Json{{"count", std::numeric_limits<std::uint64_t>::max()}}));
    doc["value_schema"]["properties"]["count"] = Json{{"type", "integer"}};
    auto unbounded = unwrap(RuntimeProfile::from_definition(doc));
    CHECK_FALSE(unwrap(unbounded.with_overrides(Json{{"count", std::numeric_limits<std::uint64_t>::max()}})).is_builtin());
  }

  TEST_CASE("nullable bounds are a declared type union with explicit validation") {
    auto doc = definition();
    doc["defaults"]["count"] = nullptr;
    doc["value_schema"]["properties"]["count"]["type"] = Json::array({"integer", "null"});
    auto nullable = unwrap(RuntimeProfile::from_definition(doc));
    CHECK(nullable.values()["count"].is_null());
    CHECK(nullable.with_overrides(Json{{"count", 42}}));
    CHECK_FALSE(nullable.with_overrides(Json{{"count", "unlimited"}}));
    doc["value_schema"]["properties"]["count"]["type"] = Json::array();
    CHECK_FALSE(RuntimeProfile::from_definition(doc));
    doc["value_schema"]["properties"]["count"]["type"] = Json::array({"integer", "unknown"});
    CHECK_FALSE(RuntimeProfile::from_definition(doc));
  }

  TEST_CASE("effective definitions match the canonical data source") {
    auto dir = std::filesystem::path(LOOM_TEST_FIXTURES).parent_path().parent_path() / "data/runtime";
    std::vector<std::string> disk;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() != ".pack") continue;
      auto doc = unwrap(json::parse(unwrap(fsutil::read_file(entry.path()))));
      auto name = entry.path().stem().string();
      auto profile = unwrap(RuntimeProfile::builtin(name));
      CHECK(profile.definition() == doc);
      CHECK(profile.values() == doc["defaults"]);
      CHECK(profile.hash().size() == 64);
      disk.push_back(name);
    }
    std::sort(disk.begin(), disk.end());
    REQUIRE_FALSE(disk.empty());
    CHECK(RuntimeProfile::domains() == disk);
  }

  TEST_CASE("existing malformed overlay never falls back and domains are bound") {
    fsutil::TempDir tmp;
    unwrap(fsutil::ensure_dir(tmp.path() / "profiles"));
    auto domains = RuntimeProfile::domains();
    REQUIRE_FALSE(domains.empty());
    auto target = tmp.path() / "profiles" / (domains.front() + ".pack");
    CHECK(unwrap(RuntimeProfile::load(domains.front(), tmp.path())).is_builtin());
    unwrap(fsutil::atomic_write(target, "{broken"));
    CHECK_FALSE(RuntimeProfile::load(domains.front(), tmp.path()));
    unwrap(fsutil::atomic_write(target, json::dump(Json{{"schema", "loom.runtime_profile_overlay/1"},
                                                           {"domain", "wrong"}, {"overrides", Json::object()}})));
    CHECK_FALSE(RuntimeProfile::load(domains.front(), tmp.path()));
    std::filesystem::remove(target);
    std::filesystem::create_symlink(tmp.path() / "absent.pack", target);
    CHECK_FALSE(RuntimeProfile::load(domains.front(), tmp.path()));
  }

  TEST_CASE("template values are substituted once and cannot install expressions") {
    Json variables{{"content", "{{another}}"}, {"another", "not recursively used"},
                   {"nested", Json{{"name", "żółć"}}}, {"count", 23}};
    CHECK(unwrap(render_profile_template("{{content}}: {{/nested/name}} ({{count}})", variables)) ==
          "{{another}}: żółć (23)");
    CHECK_FALSE(render_profile_template("{{missing}}", variables));
    CHECK_FALSE(render_profile_template("{{/missing/path}}", variables));
    CHECK_FALSE(render_profile_template("{{content", variables));
  }
}
