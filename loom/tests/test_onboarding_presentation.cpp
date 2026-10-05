#include <doctest/doctest.h>

#include "loom/onboarding_presentation.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::unwrap;

TEST_SUITE("onboarding presentation catalog") {
  TEST_CASE("generated catalog renders default locale and explicitly selected locale") {
    auto catalog = unwrap(builtin_presentation());
    CHECK(validate_presentation(catalog));
    CHECK(unwrap(presentation_text(catalog, "layer.missing")) ==
          "No current default or user value exists for this key.");
    CHECK(unwrap(presentation_text(catalog, "layer.missing", Json::object(), "pl")) ==
          catalog["locales"]["pl"]["layer.missing"].get<std::string>());
    catalog["default_locale"] = "pl";
    CHECK(unwrap(presentation_text(catalog, "layer.missing")) ==
          catalog["locales"]["pl"]["layer.missing"].get<std::string>());
    CHECK_FALSE(presentation_text(catalog, "missing.message"));
    CHECK_FALSE(presentation_text(catalog, "layer.missing", Json::object(), "missing.locale"));
  }

  TEST_CASE("parameters are inert values and repeated parameters preserve input") {
    auto catalog = unwrap(builtin_presentation());
    catalog["locales"]["en"]["test.template"] = "{{value}} / {{count}} / {{value}}";
    const std::string value = "<script>{{other}}</script>";
    CHECK(unwrap(presentation_text(catalog, "test.template", {{"value", value}, {"count", 7}})) ==
          value + " / 7 / " + value);
    CHECK_FALSE(presentation_text(catalog, "test.template", {{"value", value}}));
    CHECK_FALSE(presentation_text(catalog, "test.template", Json::array()));
    catalog["locales"]["en"]["test.template"] = "broken {{value";
    CHECK_FALSE(presentation_text(catalog, "test.template", {{"value", value}}));
    catalog["locales"]["en"]["test.template"] = "empty {{}}";
    CHECK_FALSE(presentation_text(catalog, "test.template", {{"", value}}));
  }

  TEST_CASE("invalid data never falls back to embedded English") {
    const auto catalog = unwrap(builtin_presentation());
    auto broken = catalog;
    broken["default_locale"] = "absent";
    CHECK_FALSE(validate_presentation(broken));
    CHECK_FALSE(presentation_text(broken, "layer.missing"));
    broken = catalog;
    broken["locales"]["pl"]["layer.missing"] = nullptr;
    CHECK_FALSE(validate_presentation(broken));
    CHECK_FALSE(presentation_text(broken, "layer.missing"));
    broken = catalog;
    broken["defaults"] = Json::array();
    CHECK_FALSE(validate_presentation(broken));
    CHECK_FALSE(presentation_text(broken, "layer.missing"));
    CHECK_FALSE(validate_presentation(nullptr));
    CHECK_FALSE(presentation_text(nullptr, "layer.missing"));
  }
}
