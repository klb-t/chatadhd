#include <doctest/doctest.h>

#include "import/import_usage.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("import.usage") {
  TEST_CASE("shared admission reports its real build capability") {
    fsutil::TempDir temp;
    RuntimeOptions runtime_options;
    runtime_options.data_dir = temp.path().string(); runtime_options.start_workers = false;
    const auto runtime = unwrap(Runtime::open(runtime_options));
    auto usage = unwrap(ImportUsageSession::open(runtime->config(), temp.path()));
    ImportOptions options;
    const auto source = temp.path() / "source.json";
    LOOM_REQUIRE_OK(fsutil::write_file(source, R"([{"uuid":"c","chat_messages":[{"uuid":"m","sender":"human","text":"hello"}]}])"));
    LOOM_REQUIRE_OK(usage->request(source, options, "", "test/import/source-bytes"));
#if __has_include("loom/usage_policy.h")
    CHECK(usage->receipt()["status"] == "allowed");
    CHECK(usage->receipt()["resources"]["source_bytes"]["baseline_status"] == "unavailable");
#else
    CHECK(usage->receipt()["status"] == "unavailable");
    CHECK_FALSE(usage->requires_confirmation());
    CHECK_FALSE(usage->request(source, options, "caller-operation", "test/import/source-bytes"));
#endif
  }
#if __has_include("loom/usage_policy.h")
  TEST_CASE("import volume growth pauses before rows and binds explicit owner confirmation") {
    fsutil::TempDir temp;
    RuntimeOptions runtime_options;
    runtime_options.data_dir = temp.path().string(); runtime_options.start_workers = false;
    const auto runtime = unwrap(Runtime::open(runtime_options));
    ImportOptions options;
    options.export_mode = ExportMode::On; options.include_result_metadata = false;
    const auto small = temp.path() / "small.json", large = temp.path() / "large.json";
    LOOM_REQUIRE_OK(fsutil::write_file(small, R"([{"uuid":"a","chat_messages":[{"uuid":"m","sender":"human","text":"hello"}]}])"));
    Json data = Json::array({Json{{"uuid", "b"}, {"chat_messages", Json::array({Json{{"uuid", "m"}, {"sender", "human"},
        {"text", std::string(10000, 'x')}}})}}});
    LOOM_REQUIRE_OK(fsutil::write_file(large, data.dump()));
    auto bind = [&](ImportUsageSession& usage) {
      options.expected_source_hash = usage.receipt()["estimate"]["source_hash"].get<std::string>();
      options.expected_source_bytes = usage.receipt()["estimate"]["resources"]["source_bytes"].get<std::int64_t>();
    };
    const std::string cohort = "test/import/source-bytes";
    auto first = unwrap(ImportUsageSession::open(runtime->config(), temp.path()));
    LOOM_REQUIRE_OK(first->request(small, options, "first", cohort));
    bind(*first);
    const auto one = unwrap(runtime->importer().import_file(small, options));
    LOOM_REQUIRE_OK(first->complete(one));
    CHECK(first->receipt()["status"] == "completed");
    const auto replay = first->request(small, options, "first", cohort);
    REQUIRE_FALSE(replay);
    CHECK(replay.error().code == Errc::Conflict);
    CHECK(unwrap(runtime->db().conn().query_int("SELECT COUNT(*) FROM conversations")).value() == 1);
    auto second = unwrap(ImportUsageSession::open(runtime->config(), temp.path()));
    CHECK_FALSE(second->request(large, options, "second", cohort));
    CHECK(second->requires_confirmation());
    CHECK(unwrap(runtime->db().conn().query_int("SELECT COUNT(*) FROM conversations")).value() == 1);
    const auto receipt = second->receipt()["receipt_id"].get<std::string>();
    LOOM_REQUIRE_OK(second->request(large, options, "second", cohort, receipt, "synthetic-owner-confirmation"));
    bind(*second);
    const auto two = unwrap(runtime->importer().import_file(large, options));
    LOOM_REQUIRE_OK(second->complete(two));
    CHECK(second->receipt()["status"] == "completed");
    CHECK(unwrap(runtime->db().conn().query_int("SELECT COUNT(*) FROM conversations")).value() == 2);
  }
#endif
}
