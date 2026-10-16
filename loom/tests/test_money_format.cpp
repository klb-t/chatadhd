// P31: one formatter for every sum of money shown to people: no sum is floored to zero, unknown is never a zero, the rules are data.
#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "loom/money_format.h"
#include "loom/runtime_profile.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("money_format") {
  TEST_CASE("from a hundredth up: two decimals; below it: two significant digits, never zero for a nonzero sum") {
    CHECK(format_money(0.5) == "0.50");
    CHECK(format_money(12.0) == "12.00");
    CHECK(format_money(0.01) == "0.01");
    CHECK(format_money(0.0123456) == "0.01");
    CHECK(format_money(2.5e-5) == "0.000025");   // the R19 case: it was "0.0000"
    CHECK(format_money(5.2e-5) == "0.000052");
    CHECK(format_money(1.14e-4) == "0.00011");
    CHECK(format_money(3.6e-5) == "0.000036");
    CHECK(format_money(0.00029) == "0.00029");
    CHECK(format_money(1e-5) == "0.00001");
    CHECK(format_money(1.234e-9) == "0.0000000012");
    CHECK(format_money(0.0099999) == "0.01");  // rounding into the unit above is said as the unit
    CHECK(format_money(-0.000025) == "-0.000025");
    for (double v : {1e-3, 4.4e-4, 3e-6, 7.7e-8, 1e-12}) CHECK(format_money(v) != "0");
    for (double v : {1e-3, 4.4e-4, 3e-6, 7.7e-8, 1e-12}) CHECK(format_money(v).find_first_of("123456789") != std::string::npos);
  }

  TEST_CASE("zero is zero, and what is not a number stays unknown - never a zero") {
    CHECK(format_money(0.0) == "0");
    CHECK(format_money(Json(nullptr)) == "unknown");
    CHECK(format_money(Json("n/a")) == "unknown");
    CHECK(format_money(std::numeric_limits<double>::quiet_NaN()) == "unknown");
    CHECK(format_money(std::numeric_limits<double>::infinity()) == "unknown");
    CHECK(format_money(Json(0.000025)) == "0.000025");
  }

  TEST_CASE("the rules are data: partial rules keep the preset, a wrong type is ignored") {
    const Json rules{{"significant", 3}, {"decimals", 3}, {"min_unit", 0.1}, {"zero", "free"}, {"unknown", "?"}};
    CHECK(format_money(0.5, rules) == "0.500");
    CHECK(format_money(0.02, rules) == "0.020");
    CHECK(format_money(2.5e-5, rules) == "0.000025");
    CHECK(format_money(2.57e-5, rules) == "0.0000257");
    CHECK(format_money(0.0, rules) == "free");
    CHECK(format_money(Json(nullptr), rules) == "?");
    CHECK(format_money(2.5e-5, Json{{"significant", "many"}}) == "0.000025");
    CHECK(format_money(2.5e-5, Json("not an object")) == "0.000025");
  }

  TEST_CASE("the profile ships the preset the formatter has built in") {
    const auto profile = unwrap(RuntimeProfile::builtin("action_run"));
    const Json shipped = profile.values().at("spend_limits").at("money_format");
    CHECK(shipped == money_format_preset());
  }
}
