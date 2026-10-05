// Native import defaults are a typed projection of the exact data resource;
// applying complete effective values never resurrects absent/disabled fields.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <ios>
#include <limits>
#include <type_traits>

#include "import/import_preset.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

static_assert(std::is_aggregate_v<ImportOptions>);
static_assert(std::is_aggregate_v<ImportPresetValues>);

namespace {
constexpr std::array<std::string_view, 5> kFields{
    "stream_threshold_bytes", "json_read_chunk_bytes", "json_max_depth",
    "json_inline_threshold_bytes", "generic_inference_max_bytes"};

Json projection(const ImportOptions& options) {
  return Json{{"stream_threshold_bytes", options.stream_threshold_bytes},
              {"json_read_chunk_bytes", options.json_read_chunk_bytes},
              {"json_max_depth", options.json_max_depth},
              {"json_inline_threshold_bytes", options.json_inline_threshold_bytes},
              {"generic_inference_max_bytes", options.generic_inference_max_bytes}};
}

std::filesystem::path resource_path(std::string_view filename) {
  return std::filesystem::path(LOOM_TEST_FIXTURES).parent_path().parent_path() /
         "data" / "presets" / filename;
}
}  // namespace

TEST_SUITE("import_presets") {
  TEST_CASE("aggregate defaults and inspection match authoritative exact source bytes") {
    const auto inspector = unwrap(inspect_import_preset());
    const auto raw = unwrap(compiled_import_preset_source("import"));
    const auto audit_raw = unwrap(compiled_import_preset_source("import_audit"));
    CHECK(raw == unwrap(fsutil::read_file(resource_path("import.pack"))));
    CHECK(audit_raw == unwrap(fsutil::read_file(resource_path("import_audit.pack"))));
    const auto source = unwrap(json::parse(raw));
    CHECK(inspector["schema"] == source["schema"]);
    CHECK(inspector["id"] == source["id"]);
    CHECK(inspector["version"] == source["version"]);
    CHECK(inspector["source_path"] == "loom/data/presets/import.pack");
    CHECK(inspector["source_sha256"] == Sha256::hex(raw));
    CHECK(inspector["raw_source"] == raw);
    CHECK(inspector["values"] == source["values"]);
    const ImportOptions options{};
    CHECK(projection(options) == source["values"]);
    const auto& defaults = default_import_preset();
    CHECK(options.stream_threshold_bytes == defaults.stream_threshold_bytes);
    CHECK(options.json_read_chunk_bytes == defaults.json_read_chunk_bytes);
    CHECK(options.json_max_depth == defaults.json_max_depth);
    CHECK(options.json_inline_threshold_bytes == defaults.json_inline_threshold_bytes);
    CHECK(options.generic_inference_max_bytes == defaults.generic_inference_max_bytes);
    const auto unknown = compiled_import_preset_source("absent");
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == Errc::NotFound);
  }

  TEST_CASE("representation boundaries are accepted without preset ceilings") {
    auto values = unwrap(inspect_import_preset())["values"];
    const auto signed_max = std::numeric_limits<std::int64_t>::max();
    const auto size_max = std::numeric_limits<std::size_t>::max();
    const auto chunk_max = std::min(static_cast<std::uint64_t>(size_max),
                                  static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()));
    values["stream_threshold_bytes"] = signed_max;
    values["json_read_chunk_bytes"] = chunk_max;
    values["json_max_depth"] = size_max;
    values["json_inline_threshold_bytes"] = signed_max;
    values["generic_inference_max_bytes"] = signed_max;
    const auto decoded = unwrap(import_preset_from_values(values));
    CHECK(decoded.stream_threshold_bytes == signed_max);
    CHECK(decoded.json_read_chunk_bytes == chunk_max);
    CHECK(decoded.json_max_depth == size_max);
    CHECK(decoded.json_inline_threshold_bytes == signed_max);
    CHECK(decoded.generic_inference_max_bytes == signed_max);
    ImportOptions options;
    LOOM_REQUIRE_OK(apply_import_preset_values(options, values));
    CHECK(projection(options) == values);

    for (const auto field : kFields) values[std::string(field)] = 0;
    values["json_read_chunk_bytes"] = 1;
    LOOM_REQUIRE_OK(apply_import_preset_values(options, values));
    CHECK(projection(options) == values);
    CHECK(options.json_max_depth == 0);  // Existing unlimited interpretation.
    CHECK(options.generic_inference_max_bytes == 0);
    values["json_read_chunk_bytes"] = 0;
    CHECK_FALSE(import_preset_from_values(values));
  }

  TEST_CASE("successful application changes only five values and failure changes none") {
    ImportOptions options;
    options.title = "Caller title";
    options.export_mode = ExportMode::Off;
    options.record_provenance = false;
    options.force = true;
    options.resume = true;
    options.include_result_metadata = false;
    options.expected_source_hash = "caller-bound-source";
    options.expected_source_bytes = 71;
    CancelToken cancellation;
    options.cancel = &cancellation;
    int callback_calls = 0;
    options.progress = [&](std::int64_t, std::int64_t, std::string_view) { ++callback_calls; };
    options.preflight = [&](const std::filesystem::path&) -> Status { ++callback_calls; return {}; };
    options.completed = [&](const ImportResult&) { ++callback_calls; };
    auto values = unwrap(inspect_import_preset())["values"];
    values["stream_threshold_bytes"] = 17;
    values["json_read_chunk_bytes"] = 19;
    values["json_max_depth"] = 0;
    values["json_inline_threshold_bytes"] = 23;
    values["generic_inference_max_bytes"] = 0;
    LOOM_REQUIRE_OK(apply_import_preset_values(options, values));
    CHECK(projection(options) == values);
    CHECK(options.title == "Caller title");
    CHECK(options.export_mode == ExportMode::Off);
    CHECK_FALSE(options.record_provenance);
    CHECK(options.force);
    CHECK(options.resume);
    CHECK_FALSE(options.include_result_metadata);
    CHECK(options.expected_source_hash == "caller-bound-source");
    CHECK(options.expected_source_bytes == 71);
    CHECK(options.cancel == &cancellation);
    options.progress(0, 0, "probe");
    LOOM_REQUIRE_OK(options.preflight("source.json"));
    options.completed(ImportResult{});
    CHECK(callback_calls == 3);

    const auto applied = projection(options);
    values["stream_threshold_bytes"] = 0;
    values["generic_inference_max_bytes"] = nullptr;  // Last field fails.
    CHECK_FALSE(apply_import_preset_values(options, values));
    CHECK(projection(options) == applied);
  }

  TEST_CASE("disabled missing unknown and wrong typed fields never fall back") {
    const auto baseline = unwrap(inspect_import_preset())["values"];
    ImportOptions options;
    const auto original = projection(options);
    const std::array<Json, 10> invalid_values{
        nullptr, true, "17", Json::array(), Json::object(), -1,
        1.0, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto field : kFields) {
      CAPTURE(field);
      auto missing = baseline;
      missing.erase(std::string(field));
      const auto absent = apply_import_preset_values(options, missing);
      REQUIRE_FALSE(absent);
      CHECK(absent.error().code == Errc::InvalidArgument);
      CHECK(projection(options) == original);
      for (const auto& invalid : invalid_values) {
        auto values = baseline;
        values[std::string(field)] = invalid;
        const auto result = apply_import_preset_values(options, values);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == Errc::InvalidArgument);
        CHECK(projection(options) == original);
      }
    }
    auto unknown = baseline;
    unknown["unrecognized_field"] = 0;
    CHECK_FALSE(import_preset_from_values(unknown));
    for (const Json& wrong_root : {Json(nullptr), Json::array(), Json(1), Json("preset")})
      CHECK_FALSE(import_preset_from_values(wrong_root));
  }

  TEST_CASE("overflow is diagnosed before narrowing integer values") {
    const auto baseline = unwrap(inspect_import_preset())["values"];
    const auto signed_overflow = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
    for (const auto field : {"stream_threshold_bytes", "json_inline_threshold_bytes", "generic_inference_max_bytes"}) {
      auto values = baseline;
      values[field] = signed_overflow;
      const auto result = import_preset_from_values(values);
      REQUIRE_FALSE(result);
      CHECK(result.error().code == Errc::InvalidArgument);
    }
    auto chunk_overflow = baseline;
    chunk_overflow["json_read_chunk_bytes"] =
        static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()) + 1;
    CHECK_FALSE(import_preset_from_values(chunk_overflow));
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
      auto depth_overflow = baseline;
      depth_overflow["json_max_depth"] =
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) + 1;
      CHECK_FALSE(import_preset_from_values(depth_overflow));
    }
  }
}
