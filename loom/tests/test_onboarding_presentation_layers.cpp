#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "loom/onboarding_layers.h"
#include "loom/onboarding_presentation.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::unwrap;

namespace {
Json presentation_layers_pack() {
  return Json{{"schema", "loom.default_layers_pack/1"}, {"pack_id", "presentation.parity"},
      {"revision", 1}, {"policy", {{"excluded_area_new_defaults", "proposal"}}},
      {"entries", Json::array({Json{{"id", "sample/v1"}, {"key", "sample.value"},
          {"area", "sample"}, {"revision", 1}, {"value", 77}}})}};
}

DefaultLayers presentation_act(const DefaultLayers& layers, const Json& action) {
  return unwrap(DefaultLayers::create(layers.pack(), unwrap(layers.dispatch(action))));
}

Json presentation_pack(const Json& catalog) {
  Json pack = presentation_layers_pack();
  pack["presentation_key"] = "sample.presentation";
  pack["entries"].push_back(Json{{"id", "presentation/v1"}, {"key", "sample.presentation"},
      {"area", "presentation"}, {"revision", 1}, {"value", catalog}});
  return pack;
}

struct LayerPresentationCase {
  DefaultLayers layers;
  std::string key;
  std::string message;
  Json expected;
};

// Exact foundation envelopes, including unchanged explanation strings. Tests
// retain the former literals as independent compatibility fixtures.
std::vector<LayerPresentationCase> foundation_cases() {
  const auto pack = presentation_layers_pack();
  const auto original = unwrap(DefaultLayers::create(pack));
  Json builtin{{"key", "sample.value"}, {"status", "effective"}, {"layer", "builtin"},
      {"explanation", "The built-in graph default applies because no user suppression or override exists."},
      {"id", "sample/v1"}, {"area", "sample"}, {"revision", 1},
      {"entity", {{"id", "sample/v1"}, {"key", "sample.value"}, {"area", "sample"}, {"revision", 1}}},
      {"value", 77}, {"source", {{"pack_id", "presentation.parity"}, {"pack_revision", 1},
          {"entry_id", "sample/v1"}, {"entry_revision", 1}}}};
  std::vector<LayerPresentationCase> cases;
  cases.push_back({original, "sample.value", "layer.builtin", builtin});
  cases.push_back({original, "absent.value", "layer.missing",
      Json{{"key", "absent.value"}, {"status", "missing"}, {"layer", "none"},
          {"explanation", "No current default or user value exists for this key."}}});

  auto user = presentation_act(original, Json{{"op", "override"}, {"key", "sample.value"},
      {"value", 99}, {"provenance", "form"}, {"time", "2026-10-05"}});
  auto expected = builtin;
  expected["layer"] = "user"; expected["value"] = 99;
  expected["source"] = {{"value", 99}, {"provenance", "form"}, {"time", "2026-10-05"}};
  expected["explanation"] = "An explicit user value overrides the built-in graph layer.";
  cases.push_back({user, "sample.value", "layer.user", expected});

  auto disabled = presentation_act(original, Json{{"op", "disable"}, {"key", "sample.value"}});
  expected = builtin;
  expected.erase("value"); expected["status"] = "disabled"; expected["layer"] = "user_disabled";
  expected["source"] = {{"key", "sample.value"}, {"pack_revision", 1}};
  expected["explanation"] = "The user retained this default but disabled its use.";
  cases.push_back({disabled, "sample.value", "layer.disabled", expected});

  auto excluded = presentation_act(original, Json{{"op", "exclude"}, {"key", "sample.value"}});
  expected["status"] = "excluded"; expected["layer"] = "user_exclusion";
  expected["source"] = {{"key", "sample.value"}, {"area", "sample"}, {"pack_revision", 1}};
  expected["explanation"] = "A durable user exclusion blocks this stable id across pack updates.";
  cases.push_back({excluded, "sample.value", "layer.excluded", expected});

  auto upgrade = pack;
  upgrade["revision"] = 2;
  upgrade["entries"].push_back(Json{{"id", "new/v1"}, {"key", "new.value"}, {"area", "sample"},
      {"revision", 1}, {"value", "new preset"}});
  auto proposal = unwrap(DefaultLayers::create(upgrade, excluded.snapshot()));
  expected = {{"key", "new.value"}, {"status", "proposal"}, {"layer", "pack_proposal"},
      {"explanation", "This new default touches an excluded area and awaits user acceptance."},
      {"id", "new/v1"}, {"area", "sample"}, {"revision", 1},
      {"entity", {{"id", "new/v1"}, {"key", "new.value"}, {"area", "sample"}, {"revision", 1}}},
      {"source", {{"key", "new.value"}, {"area", "sample"}, {"created_pack_revision", 2}}}};
  cases.push_back({proposal, "new.value", "layer.proposal", expected});
  return cases;
}
}  // namespace

TEST_SUITE("onboarding layer presentation") {
  TEST_CASE("legacy six envelopes retain exact canonical output and state") {
    for (const auto& item : foundation_cases()) {
      INFO(item.message);
      const auto saved = item.layers.snapshot();
      CHECK(json::canonical(unwrap(item.layers.resolve(item.key))) == json::canonical(item.expected));
      CHECK(item.layers.snapshot() == saved);
    }
  }

  TEST_CASE("explicit locale changes only explanation in all six paths") {
    const auto catalog = unwrap(builtin_presentation());
    for (const auto& item : foundation_cases()) {
      INFO(item.message);
      auto expected = item.expected;
      expected["explanation"] = catalog.at("locales").at("pl").at(item.message);
      CHECK(expected["explanation"] != item.expected["explanation"]);
      CHECK(json::canonical(unwrap(item.layers.resolve(item.key, "pl"))) == json::canonical(expected));
      CHECK(json::canonical(unwrap(item.layers.resolve(item.key, "en"))) == json::canonical(item.expected));
      CHECK_FALSE(item.layers.resolve(item.key, "unregistered-locale"));
    }
  }

  TEST_CASE("custom catalog locale and templates survive an exact restart") {
    auto catalog = unwrap(builtin_presentation());
    catalog["locales"]["custom"] = catalog.at("locales").at("en");
    catalog["locales"]["custom"]["layer.builtin"] = "Configured {{key}}";
    catalog["default_locale"] = "custom";
    auto layers = unwrap(DefaultLayers::create(presentation_pack(catalog)));
    auto resolved = unwrap(layers.resolve("sample.value"));
    CHECK(resolved["explanation"] == "Configured sample.value");
    CHECK(resolved["value"] == 77);

    auto replacement = catalog;
    replacement["locales"]["custom"]["layer.builtin"] = "User catalog {{key}}";
    layers = presentation_act(layers, Json{{"op", "override"}, {"key", "sample.presentation"},
        {"value", replacement}, {"provenance", "form"}});
    resolved = unwrap(layers.resolve("sample.value"));
    CHECK(resolved["explanation"] == "User catalog sample.value");
    CHECK(unwrap(layers.resolve("sample.presentation"))["layer"] == "user");
    const auto persisted = json::parse_or(layers.snapshot().dump(), Json{});
    const auto restarted = unwrap(DefaultLayers::create(layers.pack(), persisted));
    CHECK(restarted.snapshot() == layers.snapshot());
    CHECK(unwrap(restarted.resolve("sample.value")) == resolved);
  }

  TEST_CASE("malformed bound presentation rejects creation and upgrade atomically") {
    const auto catalog = unwrap(builtin_presentation());
    const auto layers = unwrap(DefaultLayers::create(presentation_pack(catalog)));
    const auto saved = layers.snapshot();
    std::vector<Json> bad_catalogs{nullptr, "not a catalog", Json::object()};
    auto bad = catalog; bad["default_locale"] = "absent"; bad_catalogs.push_back(bad);
    bad = catalog; bad["locales"]["en"].erase("layer.excluded"); bad_catalogs.push_back(bad);
    bad = catalog; bad["locales"]["pl"]["layer.builtin"] = 14; bad_catalogs.push_back(bad);
    for (const auto& invalid_catalog : bad_catalogs) {
      CHECK_FALSE(DefaultLayers::create(presentation_pack(invalid_catalog)));
      auto upgrade = layers.pack(); upgrade["revision"] = 2;
      upgrade["entries"][1]["revision"] = 2;
      upgrade["entries"][1]["value"] = invalid_catalog;
      CHECK_FALSE(layers.update_pack(upgrade));
      CHECK(layers.snapshot() == saved);
    }
    auto invalid_binding = layers.pack(); invalid_binding["presentation_key"] = 17;
    CHECK_FALSE(DefaultLayers::create(invalid_binding));
    invalid_binding["presentation_key"] = "";
    CHECK_FALSE(DefaultLayers::create(invalid_binding));
  }

  TEST_CASE("disabled and missing presentation never restore text or block core values") {
    auto catalog = unwrap(builtin_presentation());
    auto pack = presentation_pack(catalog);
    auto layers = unwrap(DefaultLayers::create(pack));
    layers = presentation_act(layers, Json{{"op", "disable"}, {"key", "sample.presentation"}});
    auto value = unwrap(layers.resolve("sample.value", "unregistered-locale"));
    CHECK(value["value"] == 77);
    CHECK(value["explanation"].is_null());
    CHECK(unwrap(layers.resolve("sample.presentation"))["status"] == "disabled");
    CHECK(unwrap(layers.resolve("absent.value"))["explanation"].is_null());

    // A retained malformed override is inert while presentation is suppressed.
    layers = presentation_act(layers, Json{{"op", "override"}, {"key", "sample.presentation"}, {"value", 99}});
    CHECK(unwrap(layers.resolve("sample.value"))["value"] == 77);
    auto reenabled = unwrap(layers.dispatch(Json{{"op", "reenable"}, {"key", "sample.presentation"}}));
    CHECK_FALSE(DefaultLayers::create(pack, reenabled));
    CHECK(unwrap(layers.resolve("sample.value"))["explanation"].is_null());

    pack = presentation_layers_pack(); pack["presentation_key"] = "removed.presentation";
    auto missing = unwrap(DefaultLayers::create(pack));
    CHECK(unwrap(missing.resolve("sample.value"))["value"] == 77);
    CHECK(unwrap(missing.resolve("sample.value"))["explanation"].is_null());
  }

  TEST_CASE("durable presentation exclusion survives upgrade and direct area mode") {
    auto pack = presentation_pack(unwrap(builtin_presentation()));
    auto layers = unwrap(DefaultLayers::create(pack));
    layers = presentation_act(layers, Json{{"op", "exclude"}, {"key", "sample.presentation"}});
    auto next = pack; next["revision"] = 2;
    next["entries"][1]["revision"] = 2;
    next["entries"][1]["value"]["locales"]["en"]["layer.builtin"] = "Updated preset";
    next["entries"].push_back(Json{{"id", "next.presentation/v1"}, {"key", "next.presentation"},
        {"area", "presentation"}, {"revision", 1}, {"value", pack["entries"][1]["value"]}});
    auto state = unwrap(layers.update_pack(next));
    layers = unwrap(DefaultLayers::create(next, json::parse_or(state.dump(), Json{})));
    CHECK(unwrap(layers.resolve("sample.value"))["explanation"].is_null());
    CHECK(unwrap(layers.resolve("next.presentation"))["status"] == "proposal");
    layers = presentation_act(layers, Json{{"op", "set_area_mode"}, {"area", "presentation"}, {"mode", "direct"}});
    CHECK(unwrap(layers.resolve("next.presentation"))["status"] == "effective");
    CHECK(unwrap(layers.resolve("sample.presentation"))["status"] == "excluded");
    CHECK(unwrap(layers.resolve("sample.value"))["explanation"].is_null());
    layers = presentation_act(layers, Json{{"op", "reenable"}, {"key", "sample.presentation"}});
    CHECK(unwrap(layers.resolve("sample.value"))["explanation"] == "Updated preset");
  }

  TEST_CASE("a proposed bound presentation stays absent until explicit acceptance") {
    auto pack = presentation_pack(unwrap(builtin_presentation()));
    auto layers = unwrap(DefaultLayers::create(pack));
    layers = presentation_act(layers, Json{{"op", "exclude"}, {"key", "sample.presentation"}});
    auto next = pack; next["revision"] = 2; next["presentation_key"] = "next.presentation";
    next["entries"].push_back(Json{{"id", "next.presentation/v1"}, {"key", "next.presentation"},
        {"area", "presentation"}, {"revision", 1}, {"value", pack["entries"][1]["value"]}});
    layers = unwrap(DefaultLayers::create(next, unwrap(layers.update_pack(next))));
    CHECK(unwrap(layers.resolve("sample.value"))["explanation"].is_null());
    CHECK(unwrap(layers.resolve("next.presentation"))["status"] == "proposal");
    layers = presentation_act(layers, Json{{"op", "accept_proposal"}, {"key", "next.presentation"}});
    CHECK(unwrap(layers.resolve("sample.value"))["explanation"].is_string());
    CHECK(unwrap(layers.resolve("sample.presentation"))["status"] == "excluded");
  }
}
