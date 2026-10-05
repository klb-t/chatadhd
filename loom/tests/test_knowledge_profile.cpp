#include <doctest/doctest.h>

#include <type_traits>

#include "loom/knowledge.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::knowledge;
using loom::test::unwrap;

TEST_SUITE("knowledge.profile") {
  TEST_CASE("builtin configuration data preserves aggregate defaults and exact historical DTO bytes") {
    static_assert(std::is_aggregate_v<KnowledgeConfig>);
    const auto builtin = unwrap(RuntimeProfile::builtin("knowledge"));
    const KnowledgeConfig aggregate;
    CHECK(aggregate.priors);
    CHECK(aggregate.llm == "off");
    const Json historical{{"sources", Json::array()}, {"repo", nullptr}, {"stages", Json::array()},
                          {"prior_cut", ""}, {"priors", true}, {"llm", "off"}, {"out_dir", ""},
                          {"project", ""}, {"force", false}, {"stage_params", Json::object()}};
    CHECK(json::dump(aggregate.to_json()) == json::dump(historical));
    CHECK(json::dump(unwrap(KnowledgeConfig::from_json(Json::object())).to_json()) == json::dump(historical));
    CHECK(json::dump(unwrap(KnowledgeConfig::from_json_with_profile(Json::object(), builtin)).to_json()) ==
          json::dump(historical));
  }

  TEST_CASE("configuration overlays fill omitted fields while explicit flags and recorded DTOs win") {
    fsutil::TempDir data;
    REQUIRE_FALSE(data.path().empty());
    unwrap(fsutil::ensure_dir(data.path() / "profiles"));
    const auto overlay_path = data.path() / "profiles/knowledge.pack";
    unwrap(fsutil::write_file(overlay_path, json::dump(Json{
        {"schema", "loom.runtime_profile_overlay/1"}, {"domain", "knowledge"},
        {"overrides", Json{{"config_defaults", Json{{"priors", false}, {"llm", "auto"}}}}}})));
    const auto profile = unwrap(RuntimeProfile::load("knowledge", data.path()));
    const auto omitted = unwrap(KnowledgeConfig::from_json_with_profile(Json::object(), profile));
    CHECK_FALSE(omitted.priors);
    CHECK(omitted.llm == "auto");
    CHECK(omitted.fingerprint()["priors"] == false);
    CHECK(omitted.fingerprint()["llm"] == "auto");
    const auto explicit_flags = unwrap(KnowledgeConfig::from_json_with_profile(Json{{"priors", true}, {"llm", "off"}}, profile));
    CHECK(explicit_flags.priors);
    CHECK(explicit_flags.llm == "off");
    const auto one_explicit = unwrap(KnowledgeConfig::from_json_with_profile(Json{{"priors", true}}, profile));
    CHECK(one_explicit.priors);
    CHECK(one_explicit.llm == "auto");
    CHECK(unwrap(KnowledgeConfig::from_json(omitted.to_json())).to_json() == omitted.to_json());
    CHECK(unwrap(KnowledgeConfig::from_json(Json::object())).priors);
    CHECK(unwrap(KnowledgeConfig::from_json(Json::object())).llm == "off");
    unwrap(fsutil::write_file(overlay_path, "{invalid"));
    CHECK_FALSE(RuntimeProfile::load("knowledge", data.path()));
  }

  TEST_CASE("checked configuration parser rejects foreign and permissive incompatible profiles") {
    const auto foreign = unwrap(RuntimeProfile::builtin("model"));
    CHECK_FALSE(KnowledgeConfig::from_json_with_profile(Json::object(), foreign));
    const Json definition{{"schema", "loom.runtime_profile/1"}, {"domain", "knowledge"}, {"revision", 1},
                          {"defaults", Json{{"config_defaults", Json{{"priors", "wrong"}, {"llm", "auto"}}}}},
                          {"value_schema", Json{{"type", "object"}}}};
    const auto permissive = unwrap(RuntimeProfile::from_definition(definition));
    CHECK_FALSE(KnowledgeConfig::from_json_with_profile(Json::object(), permissive));
    CHECK_FALSE(KnowledgeConfig::from_json_with_profile(Json{{"priors", true}, {"llm", "off"}}, permissive));
    const auto builtin = unwrap(RuntimeProfile::builtin("knowledge"));
    CHECK_FALSE(builtin.with_overrides(Json{{"config_defaults", Json{{"llm", "unimplemented"}}}}));
    CHECK_FALSE(KnowledgeConfig::from_json_with_profile(Json{{"llm", "unimplemented"}}, builtin));
    CHECK_FALSE(KnowledgeConfig::from_json_with_profile(Json{{"priors", 1}}, builtin));
  }
}
