#include <doctest/doctest.h>

#include "loom/onboarding_layers.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::unwrap;

namespace {
Json layers_pack() {
  return Json{{"schema", "loom.default_layers_pack/1"}, {"pack_id", "test.user.defaults"}, {"revision", 1},
              {"policy", {{"excluded_area_new_defaults", "proposal"}}},
              {"entries", Json::array({
                Json{{"id", "preference.length"}, {"key", "preference.length"}, {"area", "communication"},
                     {"revision", 1}, {"value", "concise"}},
                Json{{"id", "method.lexical"}, {"key", "method.lexical"}, {"area", "analysis"},
                     {"revision", 1}, {"value", {{"recipe", "lexical"}}}},
                Json{{"id", "consent.provider"}, {"key", "privacy.provider"}, {"area", "privacy"},
                     {"revision", 1}, {"requires_explicit_consent", true}, {"value", {{"state", "unknown"}}}}
              })}};
}
DefaultLayers act(const DefaultLayers& layers, const Json& action) {
  return unwrap(DefaultLayers::create(layers.pack(), unwrap(layers.dispatch(action))));
}
Json status(const DefaultLayers& layers, const char* key) { return unwrap(layers.resolve(key))["status"]; }
Json newer_pack() {
  Json pack = layers_pack();
  pack["revision"] = 2;
  pack["entries"][0]["revision"] = 2;
  pack["entries"][0]["value"] = "detailed";
  pack["entries"].push_back(Json{{"id", "preference.language"}, {"key", "preference.language"},
                               {"area", "communication"}, {"revision", 1}, {"value", "auto"}});
  return pack;
}
}  // namespace

TEST_SUITE("onboarding default layers") {
  TEST_CASE("fresh state resolves all default kinds and preserves explicit consent metadata") {
    auto layers = unwrap(DefaultLayers::create(layers_pack()));
    for (const char* key : {"preference.length", "method.lexical", "privacy.provider"})
      CHECK(status(layers, key) == "effective");
    auto consent = unwrap(layers.resolve("privacy.provider"));
    CHECK(consent["value"]["state"] == "unknown");
    CHECK(consent["entity"]["requires_explicit_consent"] == true);
    CHECK(consent["layer"] == "builtin");
    CHECK_FALSE(consent["explanation"].get<std::string>().empty());
    CHECK(status(layers, "unseen") == "missing");
  }

  TEST_CASE("overrides null and arbitrary user values are immutable and explain their source") {
    auto original = unwrap(DefaultLayers::create(layers_pack()));
    auto changed = act(original, Json{{"op", "override"}, {"key", "preference.length"}, {"value", nullptr},
                                     {"provenance", "form"}, {"time", "2026-10-04"}});
    CHECK(unwrap(original.resolve("preference.length"))["value"] == "concise");
    auto effective = unwrap(changed.resolve("preference.length"));
    CHECK(effective["value"].is_null());
    CHECK(effective["layer"] == "user");
    CHECK(effective["source"]["provenance"] == "form");
    auto own = act(changed, Json{{"op", "override"}, {"key", "user.custom.preference"}, {"value", 99}});
    CHECK(unwrap(own.resolve("user.custom.preference"))["value"] == 99);
    own = act(own, Json{{"op", "clear_override"}, {"key", "preference.length"}});
    CHECK(unwrap(own.resolve("preference.length"))["layer"] == "builtin");
  }

  TEST_CASE("disable retains state while permanent exclusion wins over pack upgrade and direct area mode") {
    auto layers = unwrap(DefaultLayers::create(layers_pack()));
    layers = act(layers, Json{{"op", "disable"}, {"key", "preference.length"}});
    CHECK(status(layers, "preference.length") == "disabled");
    CHECK_FALSE(unwrap(layers.resolve("preference.length")).contains("value"));
    layers = act(layers, Json{{"op", "exclude"}, {"key", "preference.length"}});
    CHECK(status(layers, "preference.length") == "excluded");
    auto proposal_pack = newer_pack();
    auto updated_state = unwrap(layers.update_pack(proposal_pack));
    CHECK(status(layers, "preference.language") == "missing");
    layers = unwrap(DefaultLayers::create(proposal_pack, updated_state));
    CHECK(status(layers, "preference.length") == "excluded");
    CHECK(status(layers, "preference.language") == "proposal");
    CHECK_FALSE(unwrap(layers.resolve("preference.language")).contains("value"));
    layers = act(layers, Json{{"op", "set_area_mode"}, {"area", "communication"}, {"mode", "direct"}});
    CHECK(status(layers, "preference.language") == "effective");
    CHECK(status(layers, "preference.length") == "excluded");
    auto override_excluded = layers.dispatch(Json{{"op", "override"}, {"key", "preference.length"}, {"value", "brief"}});
    REQUIRE_FALSE(override_excluded);
    CHECK(override_excluded.error().code == Errc::Conflict);
    layers = act(layers, Json{{"op", "reenable"}, {"key", "preference.length"}});
    CHECK(unwrap(layers.resolve("preference.length"))["value"] == "detailed");
  }

  TEST_CASE("proposal accepts explicitly, defaults direct from data apply only new ids") {
    auto pack = layers_pack();
    auto layers = unwrap(DefaultLayers::create(pack));
    layers = act(layers, Json{{"op", "exclude"}, {"key", "preference.length"}});
    layers = unwrap(DefaultLayers::create(newer_pack(), layers.snapshot()));
    layers = act(layers, Json{{"op", "accept_proposal"}, {"key", "preference.language"}});
    CHECK(status(layers, "preference.language") == "effective");
    auto direct = layers_pack();
    direct["policy"]["excluded_area_new_defaults"] = "direct";
    auto direct_layers = unwrap(DefaultLayers::create(direct));
    direct_layers = act(direct_layers, Json{{"op", "exclude"}, {"key", "preference.length"}});
    auto direct_new = newer_pack();
    direct_new["policy"]["excluded_area_new_defaults"] = "direct";
    direct_layers = unwrap(DefaultLayers::create(direct_new, direct_layers.snapshot()));
    CHECK(status(direct_layers, "preference.language") == "effective");
    CHECK(status(direct_layers, "preference.length") == "excluded");
  }

  TEST_CASE("existing unknown state and custom values survive forward migration and reload") {
    Json existing{{"future_component", {{"raw_source", Json::array({"unchanged", 4})}}},
                  {"overrides", {{"preference.length", {{"value", "long"}, {"custom_metadata", "keep"}}}}},
                  {"legacy_user", {{"never_rewrite", true}}}};
    auto layers = unwrap(DefaultLayers::create(layers_pack(), existing));
    CHECK(layers.snapshot()["future_component"] == existing["future_component"]);
    CHECK(layers.snapshot()["legacy_user"] == existing["legacy_user"]);
    auto newer = unwrap(DefaultLayers::create(newer_pack(), layers.snapshot()));
    CHECK(newer.snapshot()["future_component"] == existing["future_component"]);
    CHECK(newer.snapshot()["overrides"] == existing["overrides"]);
    CHECK(unwrap(newer.resolve("preference.length"))["value"] == "long");
    auto reloaded = unwrap(DefaultLayers::create(newer_pack(), json::parse_or(newer.snapshot().dump(), Json{})));
    CHECK(reloaded.snapshot() == newer.snapshot());
    auto reverse = reloaded.update_pack(layers_pack());
    REQUIRE_FALSE(reverse);
    CHECK(reverse.error().code == Errc::Conflict);
  }

  TEST_CASE("permanent marker survives disappearance and reappearance of a default") {
    auto layers = unwrap(DefaultLayers::create(layers_pack()));
    layers = act(layers, Json{{"op", "exclude"}, {"key", "preference.length"}});
    auto removed = layers_pack();
    removed["revision"] = 2;
    removed["entries"].erase(removed["entries"].begin());
    layers = unwrap(DefaultLayers::create(removed, layers.snapshot()));
    CHECK(status(layers, "preference.length") == "excluded");
    auto returns = newer_pack(); returns["revision"] = 3;
    layers = unwrap(DefaultLayers::create(returns, layers.snapshot()));
    CHECK(status(layers, "preference.length") == "excluded");
    auto recycled = returns; recycled["revision"] = 4; recycled["entries"][0]["id"] = "replacement.bypass";
    auto rejected = layers.update_pack(recycled);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::Conflict);
  }

  TEST_CASE("malformed records collisions and revision mutation fail without state changes") {
    auto original = unwrap(DefaultLayers::create(layers_pack()));
    const auto saved = original.snapshot();
    auto bad = layers_pack(); bad["entries"].push_back(bad["entries"][0]);
    CHECK_FALSE(original.update_pack(bad));
    bad = layers_pack(); bad["entries"][0]["value"] = "revision bypass";
    CHECK_FALSE(original.update_pack(bad));
    bad["revision"] = 2;
    CHECK_FALSE(original.update_pack(bad)); // entry revision must also advance
    bad = layers_pack(); bad["policy"].erase("excluded_area_new_defaults");
    CHECK_FALSE(DefaultLayers::create(bad));
    bad = layers_pack(); bad["entries"][0]["revision"] = -1;
    CHECK_FALSE(DefaultLayers::create(bad));
    CHECK_FALSE(original.dispatch(Json{{"op", "invented"}, {"key", "preference.length"}}));
    auto invalid_state = saved;
    invalid_state["exclusions"]["not_a_real_id"] = Json{{"key", "preference.length"}, {"area", "communication"}};
    CHECK_FALSE(DefaultLayers::create(layers_pack(), invalid_state));
    CHECK(original.snapshot() == saved);
  }

  TEST_CASE("RuntimeProfile adapter delegates validation or reports unavailable honestly") {
    auto pack = layers_pack();
    auto layers = unwrap(DefaultLayers::create(pack));
    Json definition{{"schema", "loom.runtime_profile/1"}, {"domain", "onboarding-test"}, {"revision", 1},
                    {"defaults", {{"style", "concise"}, {"optional", true}}},
                    {"value_schema", {{"type", "object"}, {"required", Json::array({"style"})},
                                      {"properties", {{"style", {{"type", "string"}}},
                                                      {"optional", {{"type", "boolean"}}}}},
                                      {"additionalProperties", false}}}};
    auto result = runtime_profile_values(definition, layers, Json{{"/style", "preference.length"}});
#if __has_include("loom/runtime_profile.h")
    REQUIRE(result);
    CHECK(result->at("values")["style"] == "concise");
    layers = act(layers, Json{{"op", "override"}, {"key", "preference.length"}, {"value", 12}});
    CHECK_FALSE(runtime_profile_values(definition, layers, Json{{"/style", "preference.length"}}));
    layers = act(layers, Json{{"op", "exclude"}, {"key", "preference.length"}});
    CHECK_FALSE(runtime_profile_values(definition, layers, Json{{"/style", "preference.length"}}));
    auto optional_removed = runtime_profile_values(definition, layers, Json{{"/optional", "preference.length"}});
    REQUIRE(optional_removed);
    CHECK_FALSE(optional_removed->at("values").contains("optional"));
    CHECK(optional_removed->at("values")["style"] == "concise");
    auto collision = runtime_profile_values(definition, layers,
                      Json{{"/style", "preference.length"}, {"/style/child", "method.lexical"}});
    REQUIRE_FALSE(collision);
    CHECK(collision.error().code == Errc::Conflict);
#else
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::Unavailable);
#endif
  }
}
