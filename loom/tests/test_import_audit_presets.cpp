#include <doctest/doctest.h>

#include <limits>
#include <type_traits>

#include "import/import_audit.h"
#include "import/import_preset.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
Json effective_audit_values() {
  return unwrap(inspect_import_audit_preset())["values"];
}
}

TEST_SUITE("import.audit-presets") {
  TEST_CASE("exact source bytes determine native defaults without changing audit shape") {
    static_assert(std::is_aggregate_v<ImportAuditOptions>);
    const auto source = unwrap(compiled_import_preset_source("import_audit"));
    const auto inspected = unwrap(inspect_import_audit_preset());
    CHECK(inspected["source_sha256"] == Sha256::hex(source));
    CHECK(inspected["raw_source"] == std::string(source));
    const auto decoded = unwrap(import_audit_preset_from_values(inspected["values"]));
    ImportAuditOptions options;
    CHECK(options.active_only == decoded.active_only);
    CHECK(options.chars_per_token_low == decoded.chars_per_token_low);
    CHECK(options.chars_per_token_high == decoded.chars_per_token_high);
    CHECK(options.output_ratio == decoded.output_ratio);
    CHECK_FALSE(options.input_price);
    CHECK_FALSE(options.output_price);
    fsutil::TempDir temp;
    auto db = test::open_db(temp.path() / "audit.db");
    const auto report = unwrap(audit_import(*db, {}));
    CHECK_FALSE(report.contains("source_sha256"));
    CHECK_FALSE(report.contains("preset"));
    CHECK(report["assumptions"]["chars_per_token_low"] == decoded.chars_per_token_low);
    CHECK(report["estimates"]["low"]["model_cost_usd_estimate"].is_null());
  }

  TEST_CASE("complete custom values affect the real audit and preserve explicit prices") {
    auto values = effective_audit_values();
    values["native_active_only"] = true;
    values["chars_per_token_low"] = 8.0;
    values["chars_per_token_high"] = 2.0;
    values["output_ratio"] = 2.0;
    ImportAuditOptions options;
    options.input_price = 3.0;
    options.output_price = 5.0;
    LOOM_REQUIRE_OK(apply_import_audit_preset_values(options, values));
    REQUIRE(options.input_price);
    CHECK(*options.input_price == 3.0);
    fsutil::TempDir temp;
    auto db = test::open_db(temp.path() / "audit.db");
    const auto conv = unwrap(db->create_conv("synthetic audit preset"));
    NewMessage message;
    message.conv_id = conv.id; message.role = "user"; message.text = "12345678";
    unwrap(db->create_msg(message));
    const auto excluded = unwrap(db->create_msg(message));
    LOOM_REQUIRE_OK(db->set_msg_status(excluded, "excluded"));
    const auto report = unwrap(audit_import(*db, {conv}, options));
    CHECK(report["raw"]["messages"] == 2);
    CHECK(report["projected"]["messages"] == 1);
    CHECK(report["estimates"]["low"]["input_tokens_estimate"] == 1.0);
    CHECK(report["estimates"]["low"]["output_tokens_estimate"] == 2.0);
    CHECK(report["estimates"]["low"]["model_cost_usd_estimate"].get<double>() == doctest::Approx(13e-6));
  }

  TEST_CASE("suppressed or malformed effective fields fail without restoring defaults") {
    const auto valid = effective_audit_values();
    for (const auto field : {"native_active_only", "chars_per_token_low", "chars_per_token_high", "output_ratio"}) {
      auto missing = valid;
      missing.erase(field);
      CHECK_FALSE(import_audit_preset_from_values(missing));
      missing[field] = nullptr;
      CHECK_FALSE(import_audit_preset_from_values(missing));
      missing[field] = "invalid";
      CHECK_FALSE(import_audit_preset_from_values(missing));
    }
    auto bad = valid;
    bad["native_active_only"] = 1;
    CHECK_FALSE(import_audit_preset_from_values(bad));
    for (const auto value : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
      bad = valid;
      bad["chars_per_token_high"] = value;
      CHECK_FALSE(import_audit_preset_from_values(bad));
    }
    bad = valid;
    bad["output_ratio"] = true;
    CHECK_FALSE(import_audit_preset_from_values(bad));
    bad["output_ratio"] = -0.1;
    CHECK_FALSE(import_audit_preset_from_values(bad));
    CHECK_FALSE(import_audit_preset_from_values(Json::array()));
  }

  TEST_CASE("atomic application validates caller rates and imposes no cost ceiling") {
    auto values = effective_audit_values();
    values["native_active_only"] = true;
    values["chars_per_token_low"] = 1e30;
    values["chars_per_token_high"] = 1e30;
    values["output_ratio"] = 1e30;
    ImportAuditOptions options;
    const auto before = options;
    options.input_price = 7;
    CHECK_FALSE(apply_import_audit_preset_values(options, values));
    CHECK(options.active_only == before.active_only);
    CHECK(options.chars_per_token_low == before.chars_per_token_low);
    CHECK(options.output_ratio == before.output_ratio);
    options.output_price = 9;
    LOOM_REQUIRE_OK(apply_import_audit_preset_values(options, values));
    CHECK(options.chars_per_token_low == 1e30);
    CHECK(options.output_ratio == 1e30);
    CHECK(options.input_price == std::optional<double>(7));
    CHECK(options.output_price == std::optional<double>(9));
  }
}
