#include <doctest/doctest.h>

#include "import/import_audit.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("import.audit") {
  TEST_CASE("all statuses and Unicode NUL are counted without touching message source") {
    fsutil::TempDir temp;
    auto db = test::open_db(temp.path() / "audit.db");
    const auto conv = unwrap(db->create_conv("synthetic audit"));
    std::string text = "ż😀";
    text.push_back('\0');
    text += "ab";
    std::vector<std::string> ids;
    for (const auto& [role, status] : std::vector<std::pair<std::string, std::string>>{
        {"user", "active"}, {"assistant", "version"}, {"tool", "excluded"}, {"system", "deleted"}}) {
      NewMessage message;
      message.conv_id = conv.id; message.role = role; message.text = text;
      auto id = unwrap(db->create_msg(message));
      LOOM_REQUIRE_OK(db->set_msg_status(id, status));
      ids.push_back(id);
    }
    const auto report = unwrap(audit_import(*db, {conv, conv}));
    CHECK(report["raw"]["conversations"] == 1);
    CHECK(report["raw"]["messages"] == 4);
    CHECK(report["raw"]["characters"] == 20);
    CHECK(report["projected"]["messages"] == 4);
    CHECK(report["by_role"]["tool"]["characters"] == 5);
    CHECK(report["by_status"]["deleted"]["messages"] == 1);
    CHECK(report["estimates"]["low"]["input_tokens_estimate"] == 5.0);
    CHECK(report["estimates"]["low"]["model_cost_usd_estimate"].is_null());
    for (const auto& id : ids) CHECK(unwrap(db->get_msg(id))->text == text);
  }
  TEST_CASE("active projection and caller prices remain explicit") {
    fsutil::TempDir temp;
    auto db = test::open_db(temp.path() / "audit.db");
    const auto conv = unwrap(db->create_conv("cost"));
    NewMessage message;
    message.conv_id = conv.id; message.role = "user"; message.text = "12345678";
    unwrap(db->create_msg(message));
    const auto hidden = unwrap(db->create_msg(message));
    LOOM_REQUIRE_OK(db->set_msg_status(hidden, "excluded"));
    ImportAuditOptions options;
    options.active_only = true; options.input_price = 2; options.output_price = 4; options.output_ratio = 0.5;
    const auto report = unwrap(audit_import(*db, {conv}, options));
    CHECK(report["raw"]["messages"] == 2);
    CHECK(report["projected"]["messages"] == 1);
    CHECK(report["estimates"]["low"]["model_cost_usd_estimate"].get<double>() == doctest::Approx(8e-6));
    CHECK_FALSE(report["assumptions"]["prices_verified_current"].get<bool>());
  }
  TEST_CASE("invalid estimates fail explicitly and empty archive has zero tokens") {
    ImportAuditOptions options;
    options.input_price = 1;
    CHECK_FALSE(validate_import_audit_options(options));
    options.output_price = 2; options.chars_per_token_high = 5;
    CHECK_FALSE(validate_import_audit_options(options));
    fsutil::TempDir temp;
    auto db = test::open_db(temp.path() / "audit.db");
    auto report = unwrap(audit_import(*db, {}));
    CHECK(report["raw"]["messages"] == 0);
    CHECK(report["estimates"]["high"]["input_tokens_estimate"] == 0.0);
  }
}
