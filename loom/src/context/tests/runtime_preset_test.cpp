// Private offline regression entry. Production's recursive source glob sees
// an empty translation unit unless the explicit runner enables this guard.
#ifdef LOOM_RUNTIME_PRESET_TEST_MAIN
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

#include "context/runtime_preset.h"
#include "chat/reasoning_profile.h"

namespace {
using namespace loom;
template<class T> T checked(Result<T> result) {
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(result).value();
}
context::RuntimePresetSnapshot resolve(const Json& definition, const Json& options = Json::object()) {
  return checked(context::resolve_runtime_preset(definition, options));
}
}

TEST_CASE("runtime preset uses canonical generated descriptors with stable exact identities") {
  for (const auto* definition : {&context::builtin_context_goal_cues_definition(), &context::builtin_chat_reasoning_definition()}) {
    const auto original = *definition;
    const auto preset = resolve(*definition);
    CHECK(preset.values == (*definition)["defaults"]);
    CHECK(preset.hash.size() == 64);
    CHECK(resolve(*definition).hash == preset.hash);
    const auto exact = resolve(*definition, Json{{"effective_values", preset.values}});
    CHECK(exact.values == preset.values);
    CHECK(exact.hash == preset.hash);
    CHECK(exact.inspection()["hash"] == preset.hash);
    CHECK(exact.inspection()["values"] == preset.values);
    CHECK(exact.inspection()["value_schema"] == (*definition)["value_schema"]);
    CHECK(exact.inspection()["domain"] == (*definition)["domain"]);
    CHECK(exact.inspection()["is_builtin"] == true);
    CHECK(*definition == original);
#if __has_include("loom/runtime_profile.h")
    const auto authoritative = checked(RuntimeProfile::from_definition(*definition));
    CHECK(preset.hash == authoritative.hash());
    CHECK(preset.values == authoritative.values());
    CHECK(preset.inspection()["validation_basis"] == "runtime_profile_value_schema");
#else
    CHECK(preset.inspection()["validation_basis"] == "native_consumed_fields");
#endif
    auto next_revision = *definition; next_revision["revision"] = next_revision["revision"].get<int>() + 1;
    CHECK(resolve(next_revision).hash != preset.hash);
  }
}

TEST_CASE("runtime preset historical cue identity stays anchored without hiding changed definitions") {
  const auto definition = context::builtin_context_goal_cues_definition();
  const auto anchors = context::builtin_runtime_preset_legacy_identity_hashes();
  const auto historical_hash = anchors.at("context_goal_cues").get<std::string>();
  const auto entries = context::builtin_runtime_preset_layer_entries();
  CHECK(resolve(definition).hash == historical_hash);

  auto next_revision = definition;
  next_revision["revision"] = next_revision["revision"].get<int>() + 1;
  CHECK(resolve(next_revision).hash != historical_hash);
  auto changed_values = definition;
  changed_values["defaults"]["cue_weight_base"] = definition["defaults"]["cue_weight_base"].get<double>() + 1.0;
  CHECK(resolve(changed_values).hash != historical_hash);
  CHECK(context::builtin_runtime_preset_legacy_identity_hashes() == anchors);
  CHECK(context::builtin_context_goal_cues_definition() == definition);
  CHECK(resolve(definition).hash == historical_hash);

  // Identity metadata must not become another default, or change the existing
  // generated layer values and their bindings to the canonical descriptors.
  CHECK(entries.size() == 13);
  const auto& bindings = context::builtin_runtime_preset_layer_bindings();
  std::size_t matched = 0;
  for (const auto* descriptor : {&context::builtin_context_goal_cues_definition(), &context::builtin_chat_reasoning_definition()}) {
    const auto& domain_bindings = bindings.at((*descriptor)["domain"].get<std::string>());
    for (auto binding = domain_bindings.begin(); binding != domain_bindings.end(); ++binding) {
      std::size_t matches = 0;
      for (const auto& entry : entries) if (entry["key"] == binding.value()) {
        CHECK(entry["value"] == (*descriptor)["defaults"].at(Json::json_pointer(binding.key())));
        ++matches;
      }
      CHECK(matches == 1);
      matched += matches;
    }
  }
  CHECK(matched == entries.size());
  CHECK(context::builtin_runtime_preset_layer_entries() == entries);
}

TEST_CASE("runtime preset rejects invalid options and ambiguous snapshot ownership") {
  const auto& definition = context::builtin_context_goal_cues_definition();
  for (const auto& options : {Json(nullptr), Json::array(), Json{{"overrides", Json::object()}},
      Json{{"effective_values", nullptr}}, Json{{"effective_values", Json::array()}},
      Json{{"effective_values", Json::object()}, {"layer_snapshot", Json::object()}}}) {
    const auto result = context::resolve_runtime_preset(definition, options);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::InvalidArgument);
  }
  for (const char* key : {"schema", "defaults", "value_schema", "domain", "revision"}) {
    auto invalid = definition; invalid.erase(key);
    CHECK_FALSE(context::resolve_runtime_preset(invalid, Json::object()));
  }
  auto unsupported = definition; unsupported["schema"] = "loom.runtime_profile/unsupported";
  CHECK_FALSE(context::resolve_runtime_preset(unsupported, Json::object()));
}

TEST_CASE("runtime preset exact values never merge removed settings back from defaults") {
  const auto& definition = context::builtin_context_goal_cues_definition();
  auto values = definition["defaults"];
  values["cue_weight_base"] = -7.25;
  const auto exact = resolve(definition, Json{{"effective_values", values}});
  CHECK(exact.values == values);
  CHECK(exact.inspection()["is_builtin"] == false);
  CHECK(exact.hash != resolve(definition).hash);
  CHECK(resolve(definition, Json{{"effective_values", values}}).hash == exact.hash);
  values.erase("fallback_goal_type");
  const auto missing = context::resolve_runtime_preset(definition, Json{{"effective_values", values}});
#if __has_include("loom/runtime_profile.h")
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == Errc::InvalidArgument);
#else
  REQUIRE(missing);
  CHECK(missing->values == values);
  CHECK_FALSE(missing->values.contains("fallback_goal_type"));
  CHECK(missing->inspection()["validation_basis"] == "native_consumed_fields");
#endif
  CHECK(definition["defaults"].contains("fallback_goal_type"));
}

TEST_CASE("runtime preset absent layering reports unavailable without falling back") {
  const auto& definition = context::builtin_context_goal_cues_definition();
  const auto invalid = context::resolve_runtime_preset(definition, Json{{"layer_snapshot", Json::object()}});
  REQUIRE_FALSE(invalid);
#if __has_include("loom/runtime_profile.h") && __has_include("loom/onboarding_layers.h")
  CHECK(invalid.error().code == Errc::InvalidArgument);
#else
  CHECK(invalid.error().code == Errc::Unavailable);
#endif
}

TEST_CASE("runtime preset native reasoning consumes exact user recipe and rejects a removed required field") {
  auto values = context::builtin_chat_reasoning_definition()["defaults"];
  values["thinking_indicators"] = Json::array({"synthetic"});
  values["budget_model_markers"] = Json::array({"version"});
  values["budget_by_effort"] = Json::object();
  values["unknown_effort_budget"] = std::uint64_t{9000000000};
  const auto recipe = checked(chat::reasoning_recipe(Json{{"effective_values", values}}));
  Json payload = Json::object();
  chat::apply_reasoning_recipe(payload, "synthetic-version", std::string("owner-effort"), recipe);
  CHECK(payload["reasoning"]["enabled"] == true);
  CHECK(payload["reasoning"]["max_tokens"] == values["unknown_effort_budget"]);
  CHECK_FALSE(recipe.budget_by_effort.contains("medium"));
  CHECK(recipe.snapshot.values == values);
  values["thinking_indicators"] = Json::array();
  const auto disabled = checked(chat::reasoning_recipe(Json{{"effective_values", values}}));
  payload = Json::object();
  chat::apply_reasoning_recipe(payload, "synthetic-version", std::string("owner-effort"), disabled);
  CHECK_FALSE(payload.contains("reasoning"));
  values.erase("unknown_effort_budget");
  const auto missing = chat::reasoning_recipe(Json{{"effective_values", values}});
  REQUIRE_FALSE(missing); CHECK(missing.error().code == Errc::InvalidArgument);
}

#if __has_include("loom/runtime_profile.h") && __has_include("loom/onboarding_layers.h")
namespace {
struct LayerFixture {
  Json definition = context::builtin_context_goal_cues_definition();
  Json pack{{"schema", "loom.default_layers_pack/1"}, {"pack_id", "synthetic/context-cues"}, {"revision", 1},
      {"policy", Json{{"excluded_area_new_defaults", "proposal"}}}, {"entries", Json::array()}};
  Json bindings = Json::object();
  LayerFixture() {
    pack["entries"] = context::builtin_runtime_preset_layer_entries();
    bindings = context::builtin_runtime_preset_layer_bindings()["context_goal_cues"];
  }
  Json options(const onboarding::DefaultLayers& layers) const {
    return Json{{"layer_snapshot", Json{{"pack", layers.pack()}, {"state", layers.snapshot()}, {"bindings", bindings}}}};
  }
};
}

TEST_CASE("runtime preset actual W11 W12 keeps durable exclusion through removal reappearance and override") {
  LayerFixture fixture;
  const auto cue_key = fixture.bindings["/cue_weight_base"].get<std::string>();
  auto layers = checked(onboarding::DefaultLayers::create(fixture.pack, Json{{"owner_metadata", Json{{"preserve", true}}}}));
  CHECK(resolve(fixture.definition, fixture.options(layers)).values == fixture.definition["defaults"]);
  const auto before = layers.snapshot();
  const auto overridden = checked(layers.dispatch(Json{{"op", "override"}, {"key", cue_key}, {"value", 37.125},
      {"source_refs", Json::array({"synthetic/owner-override"})}}));
  CHECK(layers.snapshot() == before);
  layers = checked(onboarding::DefaultLayers::create(layers.pack(), overridden));
  const auto selected = resolve(fixture.definition, fixture.options(layers));
  CHECK(selected.values["cue_weight_base"] == 37.125);
  CHECK(selected.inspection()["layer_resolutions"]["/cue_weight_base"]["layer"] == "user");
  const auto authoritative = checked(onboarding::runtime_profile_values(fixture.definition, layers, fixture.bindings));
  CHECK(selected.hash == authoritative["hash"].get<std::string>());
  CHECK(selected.values == authoritative["values"]);

  layers = checked(onboarding::DefaultLayers::create(layers.pack(), checked(layers.dispatch(
      Json{{"op", "exclude"}, {"key", cue_key}, {"source_refs", Json::array({"synthetic/owner-exclusion"})}}))));
  const auto excluded_state = layers.snapshot();
  CHECK_FALSE(excluded_state["overrides"].contains(cue_key));
  CHECK(excluded_state["owner_metadata"]["preserve"] == true);
  const auto assert_excluded = [&]() {
    const auto excluded = checked(layers.resolve(cue_key));
    CHECK(excluded["status"] == "excluded"); CHECK_FALSE(excluded.contains("value"));
    // This setting is required by the canonical descriptor. Removing it must
    // fail validation explicitly, never resurrect a default or user value.
    const auto invalid = context::resolve_runtime_preset(fixture.definition, fixture.options(layers));
    REQUIRE_FALSE(invalid); CHECK(invalid.error().code == Errc::InvalidArgument);
  };
  assert_excluded();
  CHECK_FALSE(layers.dispatch(Json{{"op", "override"}, {"key", cue_key}, {"value", 100}}));

  auto removed_pack = layers.pack(); removed_pack["revision"] = 2;
  for (auto entry = removed_pack["entries"].begin(); entry != removed_pack["entries"].end(); ++entry)
    if ((*entry)["key"] == cue_key) { removed_pack["entries"].erase(entry); break; }
  layers = checked(onboarding::DefaultLayers::create(removed_pack, checked(layers.update_pack(removed_pack))));
  assert_excluded();
  auto reappeared_pack = fixture.pack; reappeared_pack["revision"] = 3;
  for (auto& entry : reappeared_pack["entries"]) if (entry["key"] == cue_key) {
    entry["revision"] = 2; entry["value"] = 200;
  }
  layers = checked(onboarding::DefaultLayers::create(reappeared_pack, checked(layers.update_pack(reappeared_pack))));
  assert_excluded();
  CHECK(layers.snapshot()["exclusions"] == excluded_state["exclusions"]);
  const auto before_rejected_override = layers.snapshot();
  const auto forbidden = layers.dispatch(Json{{"op", "override"}, {"key", cue_key}, {"value", 300}});
  REQUIRE_FALSE(forbidden); CHECK(forbidden.error().code == Errc::Conflict);
  CHECK(layers.snapshot() == before_rejected_override);
  layers = checked(onboarding::DefaultLayers::create(layers.pack(), checked(layers.dispatch(
      Json{{"op", "reenable"}, {"key", cue_key}}))));
  layers = checked(onboarding::DefaultLayers::create(layers.pack(), checked(layers.dispatch(
      Json{{"op", "override"}, {"key", cue_key}, {"value", 401.75}}))));
  const auto restored = resolve(fixture.definition, fixture.options(layers));
  CHECK(restored.values["cue_weight_base"] == 401.75);
  CHECK(restored.hash != selected.hash);
  CHECK(restored.inspection()["layer_resolutions"]["/cue_weight_base"]["layer"] == "user");
  CHECK(layers.snapshot()["owner_metadata"]["preserve"] == true);
}

TEST_CASE("runtime preset actual layering rejects partial bindings instead of restoring unbound defaults") {
  LayerFixture fixture;
  const auto layers = checked(onboarding::DefaultLayers::create(fixture.pack));
  auto options = fixture.options(layers);
  options["layer_snapshot"]["bindings"].erase("/cue_weight_base");
  const auto invalid = context::resolve_runtime_preset(fixture.definition, options);
  REQUIRE_FALSE(invalid); CHECK(invalid.error().code == Errc::InvalidArgument);
  auto overlap = fixture.options(layers);
  overlap["layer_snapshot"]["bindings"]["/cue_weight_base/child"] = "cue_weight_base";
  const auto conflicting = context::resolve_runtime_preset(fixture.definition, overlap);
  REQUIRE_FALSE(conflicting); CHECK(conflicting.error().code == Errc::Conflict);
}
#endif
#endif
