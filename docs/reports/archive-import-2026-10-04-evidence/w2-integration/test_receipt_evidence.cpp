#include <doctest/doctest.h>

#include <cstdlib>

#include "import/import_usage.h"
#include "loom/runtime.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("import.usage.integration") {
  TEST_CASE("pinned W2 persists the exact confirmation and measured baseline after import") {
    fsutil::TempDir temp;
    RuntimeOptions runtime_options;
    runtime_options.data_dir = temp.path().string();
    runtime_options.start_workers = false;
    const auto runtime = unwrap(Runtime::open(runtime_options));
    // Actual W2 Config defaults are linked, rather than injected test settings.
    REQUIRE(runtime->config().get("loom_usage_policy") == usage_policy_defaults());
    ImportOptions options;
    options.export_mode = ExportMode::On;
    options.include_result_metadata = false;
    const auto small = temp.path() / "small.json";
    const auto large = temp.path() / "large.json";
    LOOM_REQUIRE_OK(fsutil::write_file(small,
        R"([{"uuid":"small","chat_messages":[{"uuid":"m","sender":"human","text":"hello"}]}])"));
    const Json large_data = Json::array({Json{{"uuid", "large"}, {"chat_messages", Json::array({
        Json{{"uuid", "m"}, {"sender", "human"}, {"text", std::string(10000, 'x')}}})}}});
    LOOM_REQUIRE_OK(fsutil::write_file(large, large_data.dump()));
    auto bind = [&](ImportUsageSession& session) {
      options.expected_source_hash = session.receipt()["estimate"]["source_hash"].get<std::string>();
      options.expected_source_bytes = session.receipt()["estimate"]["resources"]["source_bytes"].get<std::int64_t>();
    };
    const std::string cohort = "external-integration/import/source-bytes";
    auto first = unwrap(ImportUsageSession::open(runtime->config(), temp.path()));
    LOOM_REQUIRE_OK(first->request(small, options, "first", cohort));
    const Json first_admission = first->receipt();
    bind(*first);
    LOOM_REQUIRE_OK(first->complete(unwrap(runtime->importer().import_file(small, options))));
    auto second = unwrap(ImportUsageSession::open(runtime->config(), temp.path()));
    auto paused = second->request(large, options, "growth", cohort);
    REQUIRE_FALSE(paused);
    REQUIRE(second->requires_confirmation());
    REQUIRE(second->receipt()["status"] == "requires_confirmation");
    REQUIRE(second->receipt()["authorized"] == false);
    REQUIRE(second->receipt()["resources"]["source_bytes"]["ratio"].get<double>() >= 10.0);
    REQUIRE(unwrap(runtime->db().conn().query_int("SELECT COUNT(*) FROM conversations")).value() == 1);
    const Json pause_receipt = second->receipt();
    const std::string receipt_id = pause_receipt["receipt_id"].get<std::string>();
    auto mismatched = second->request(large, options, "growth", cohort,
        "usage_receipt-not-shown", "synthetic-owner-confirmation");
    REQUIRE_FALSE(mismatched);
    CHECK(mismatched.error().code == Errc::Conflict);
    CHECK(unwrap(runtime->db().conn().query_int("SELECT COUNT(*) FROM conversations")).value() == 1);
    LOOM_REQUIRE_OK(second->request(large, options, "growth", cohort, receipt_id,
        "synthetic-owner-confirmation"));
    const Json confirmed = second->receipt();
    REQUIRE(confirmed["receipt_id"] == receipt_id);
    REQUIRE(confirmed["estimate"] == pause_receipt["estimate"]);
    REQUIRE(confirmed["resources"] == pause_receipt["resources"]);
    REQUIRE(confirmed["confirmation"] == Json{{"approved", true}, {"ref", "synthetic-owner-confirmation"}});
    REQUIRE(confirmed["authorized"] == true);
    bind(*second);
    LOOM_REQUIRE_OK(second->complete(unwrap(runtime->importer().import_file(large, options))));
    REQUIRE(second->receipt()["status"] == "completed");
    REQUIRE(unwrap(runtime->db().conn().query_int("SELECT COUNT(*) FROM conversations")).value() == 2);
    const auto durable = unwrap(UsagePolicy::open(temp.path() / "usage-policy.sqlite"));
    const Json inspected = unwrap(durable->inspect(cohort));
    REQUIRE(inspected["baseline"]["source_bytes"]["samples"] == 2);
    bool found_confirmation = false;
    for (const auto& event : inspected["events"]) {
      if (event["kind"] == "confirmation" && event["operation_id"] == "growth") {
        found_confirmation = true;
        CHECK(event["payload"] == confirmed["confirmation"]);
      }
    }
    REQUIRE(found_confirmation);
    const char* destination = std::getenv("W2_INTEGRATION_EVIDENCE");
    REQUIRE(destination != nullptr);
    LOOM_REQUIRE_OK(fsutil::write_file(destination, Json{{"provider_calls", 0},
        {"first_admission", first_admission}, {"pause", pause_receipt},
        {"mismatched_confirmation_error", mismatched.error().to_string()},
        {"confirmed", confirmed}, {"completed", second->receipt()}, {"durable_inspection", inspected}}.dump(2)));
  }
}
