#include <doctest/doctest.h>

#include <array>
#include <map>

#include "import/import_preset.h"
#include "import/import_preset_identity.h"
#include "import/import_usage.h"
#include "loom/event_bus.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "../third_party/miniz/miniz.h"
#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#endif
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
Json provider_conversation(int index) {
  return Json{{"uuid", "binding-conversation-" + std::to_string(index)}, {"name", "Synthetic preset binding"},
    {"chat_messages", Json::array({Json{{"uuid", "binding-message"}, {"sender", "human"},
      {"text", "Synthetic message"}}})}};
}

struct BindingFixture {
  fsutil::TempDir temporary;
  std::unique_ptr<Database> db = open_db(temporary.path() / "binding.db");
  EventBus bus;
  BlobStore blobs{temporary.path() / "blobs", *db};
  ProvenanceStore provenance{*db};
  ConversationImporter importer{*db, bus, &blobs, &provenance};
  ImportOptions options;

  BindingFixture() { options.export_mode = ExportMode::On; options.resume = true; }

  std::filesystem::path write(const Json& data) {
    const auto path = temporary.path() / "source.json";
    LOOM_REQUIRE_OK(fsutil::write_file(path, data.dump()));
    return path;
  }
  std::filesystem::path zip(const std::map<std::string, std::string>& members) {
    const auto path = temporary.path() / "source.zip";
    mz_zip_archive archive{};
    REQUIRE(mz_zip_writer_init_file(&archive, path.string().c_str(), 0));
    for (const auto& [name, bytes] : members)
      REQUIRE(mz_zip_writer_add_mem(&archive, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&archive));
    REQUIRE(mz_zip_writer_end(&archive));
    return path;
  }
  Json source_metadata(const std::string& source_id) {
    const auto source = unwrap(provenance.get_source(source_id));
    REQUIRE(source);
    return source->metadata;
  }
};

std::array<ImportOptions, 5> changed_values(const ImportOptions& original) {
  std::array<ImportOptions, 5> changed{original, original, original, original, original};
  changed[0].stream_threshold_bytes = original.stream_threshold_bytes ? 0 : 1;
  changed[1].json_read_chunk_bytes = original.json_read_chunk_bytes == 1 ? 2 : 1;
  changed[2].json_max_depth = original.json_max_depth ? 0 : 1;
  changed[3].json_inline_threshold_bytes = original.json_inline_threshold_bytes ? 0 : 1;
  changed[4].generic_inference_max_bytes = original.generic_inference_max_bytes ? 0 : 1;
  return changed;
}

void bind_resource_changes(ImportOptions& options) {
  options.stream_threshold_bytes = 0;
  options.json_read_chunk_bytes = 1;
  options.json_max_depth = 0;
}
}  // namespace

TEST_SUITE("import_presets_binding") {
  TEST_CASE("canonical preset identity preserves historical tokens and separates resources from results") {
    const auto document = unwrap(json::parse(unwrap(compiled_import_preset_source("import"))));
    ImportOptions options;
    CHECK(import_preset_values(options) == document["values"]);
    CHECK(import_preset_hash(options) == document["compatibility"]["legacy_values_sha256"].get<std::string>());
    CHECK(import_projection_hash(options) == document["compatibility"]["legacy_projection_values_sha256"].get<std::string>());
    CHECK(import_preset_is_legacy(options));
    CHECK(import_projection_is_legacy(options));
    const auto variants = changed_values(options);
    for (std::size_t index = 0; index < variants.size(); ++index) {
      CAPTURE(index);
      CHECK(import_preset_hash(variants[index]) != import_preset_hash(options));
      CHECK_FALSE(import_preset_is_legacy(variants[index]));
      CHECK((import_projection_hash(variants[index]) == import_projection_hash(options)) == (index < 3));
      CHECK(import_projection_is_legacy(variants[index]) == (index < 3));
    }
  }

  TEST_CASE("inline representation changes get a new source while resource changes preserve legacy cache") {
    BindingFixture fixture;
    const std::string opaque = "{\"note\":\"Synthetic retained source\"}";
    const auto path = fixture.zip({{"conversations.json", Json::array({provider_conversation(0)}).dump()},
      {"opaque.json", opaque}});
    const auto legacy = unwrap(fixture.importer.import_file(path, fixture.options));
    REQUIRE(legacy.conversations.size() == 1);
    const auto metadata = fixture.source_metadata(legacy.source_id);
    CHECK_FALSE(metadata.contains("import_projection_hash"));
    CHECK(unwrap(fixture.db->conn().query_text("SELECT content FROM nodes WHERE label='opaque.json'")) ==
      std::optional<std::string>(opaque));
    auto resources = fixture.options;
    bind_resource_changes(resources);
    const auto cached = unwrap(fixture.importer.import_file(path, resources));
    CHECK(cached.already_imported);
    CHECK(cached.source_id == legacy.source_id);
    CHECK(cached.conversations[0].id == legacy.conversations[0].id);
    CHECK(fixture.source_metadata(legacy.source_id) == metadata);

    auto different = fixture.options;
    different.json_inline_threshold_bytes = 0;
    const auto projected = unwrap(fixture.importer.import_file(path, different));
    CHECK_FALSE(projected.already_imported);
    CHECK_FALSE(projected.resumed);
    CHECK(projected.source_id != legacy.source_id);
    REQUIRE(projected.conversations.size() == 1);
    CHECK(projected.conversations[0].id != legacy.conversations[0].id);
    CHECK(fixture.source_metadata(projected.source_id)["import_projection_hash"] == import_projection_hash(different));
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM nodes WHERE label='opaque.json' AND content=''")) ==
      std::optional<std::int64_t>(1));
    const auto projected_cache = unwrap(fixture.importer.import_file(path, different));
    CHECK(projected_cache.already_imported);
    CHECK(projected_cache.source_id == projected.source_id);
    const auto restored = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(restored.already_imported);
    CHECK(restored.source_id == legacy.source_id);
  }

  TEST_CASE("inference scope changes are not hidden by a completed source cache") {
    BindingFixture fixture;
    const Json generic{{"threads", Json::array({Json{{"subject", "Synthetic unknown provider"},
      {"turns", Json::array({Json{{"speaker", "user"}, {"body", "Synthetic inferred message"}}})}}})}};
    const auto path = fixture.zip({{"unknown.json", generic.dump()}});
    const auto legacy = unwrap(fixture.importer.import_file(path, fixture.options));
    REQUIRE(legacy.conversations.size() == 1);
    CHECK(legacy.export_report["inferred"] == true);
    auto bounded = fixture.options;
    bounded.generic_inference_max_bytes = 1;
    const auto changed = unwrap(fixture.importer.import_file(path, bounded));
    CHECK_FALSE(changed.already_imported);
    CHECK_FALSE(changed.resumed);
    CHECK(changed.source_id != legacy.source_id);
    CHECK(changed.conversations.empty());
    CHECK(changed.export_report["inferred"] == false);
    CHECK(changed.export_report["partial"] == false);
    CHECK(changed.export_report["errors"].empty());
    CHECK(changed.export_report["unknown_members"] == Json::array({"unknown.json"}));
    REQUIRE(changed.export_report["members"].size() == 1);
    CHECK(changed.export_report["members"][0]["disposition"] == "unrecognized");
    CHECK(changed.export_report["members"][0]["code"] == "unsupported");
    CHECK(unwrap(fixture.db->conn().query_text("SELECT content FROM nodes WHERE kind='export:member' AND label='unknown.json'")) ==
      std::optional<std::string>(generic.dump()));
    CHECK(fixture.source_metadata(changed.source_id)["import_projection_hash"] == import_projection_hash(bounded));
    const auto cached = unwrap(fixture.importer.import_file(path, bounded));
    CHECK(cached.already_imported);
    CHECK(cached.source_id == changed.source_id);
    CHECK(cached.conversations.empty());
    CHECK(cached.export_report == changed.export_report);
  }

  TEST_CASE("retaining an unsupported mapping does not hide a malformed archive member") {
    BindingFixture fixture;
    auto bounded = fixture.options;
    bounded.generic_inference_max_bytes = 1;
    const auto path = fixture.zip({{"unknown.json", R"({"alien_turns":[]})"},
                                  {"broken.json", "{broken"}});
    const auto result = unwrap(fixture.importer.import_file(path, bounded));
    CHECK(result.conversations.empty());
    CHECK(result.export_report["partial"] == true);
    REQUIRE(result.export_report["errors"].size() == 1);
    CHECK(result.export_report["errors"][0]["member"] == "broken.json");
    CHECK(result.export_report["errors"][0]["code"] == "import_failed");
    CHECK(result.export_report["unknown_members"] == Json::array({"unknown.json"}));
    CHECK_FALSE(unwrap(fixture.importer.import_file(path, bounded)).already_imported);
  }

  TEST_CASE("partial journals resume across resources but not across semantic projections") {
    bool semantic_change = false;
    SUBCASE("resource-only change retains old conversation and source IDs") {}
    SUBCASE("semantic change starts its own journal") { semantic_change = true; }
    BindingFixture fixture;
    const auto path = fixture.write(Json::array({provider_conversation(0), provider_conversation(1)}));
    CancelToken cancelled;
    auto interrupted = fixture.options;
    interrupted.cancel = &cancelled;
    interrupted.progress = [&](std::int64_t current, std::int64_t, std::string_view stage) {
      if (stage == "export" && current == 1) cancelled.cancel();
    };
    const auto first = unwrap(fixture.importer.import_file(path, interrupted));
    REQUIRE(first.conversations.size() == 1);
    CHECK(first.cancelled);
    CHECK_FALSE(fixture.source_metadata(first.source_id).contains("import_projection_hash"));
    auto changed = fixture.options;
    bind_resource_changes(changed);
    if (semantic_change) changed.json_inline_threshold_bytes = 0;
    const auto resumed = unwrap(fixture.importer.import_file(path, changed));
    REQUIRE(resumed.conversations.size() == 2);
    CHECK_FALSE(resumed.already_imported);
    CHECK(resumed.resumed == !semantic_change);
    CHECK((resumed.source_id == first.source_id) == !semantic_change);
    CHECK((resumed.conversations[0].id == first.conversations[0].id) == !semantic_change);
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM conversations")) ==
      std::optional<std::int64_t>(semantic_change ? 3 : 2));
  }

  TEST_CASE("explicit malformed projection metadata never aliases a historical source") {
    Json malformed = nullptr;
    SUBCASE("null") {}
    SUBCASE("wrong type") { malformed = 42; }
    SUBCASE("wrong digest") { malformed = "not-a-projection-digest"; }
    BindingFixture fixture;
    const auto path = fixture.write(Json::array({provider_conversation(0)}));
    const auto original = unwrap(fixture.importer.import_file(path, fixture.options));
    auto metadata = fixture.source_metadata(original.source_id);
    metadata["import_projection_hash"] = malformed;
    LOOM_REQUIRE_OK(fixture.db->conn().run("UPDATE loom_sources SET metadata=? WHERE id=?",
      json::py_dumps(metadata), original.source_id));
    const auto replacement = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK_FALSE(replacement.already_imported);
    CHECK_FALSE(replacement.resumed);
    CHECK(replacement.source_id != original.source_id);
    CHECK(fixture.source_metadata(original.source_id) == metadata);
    CHECK_FALSE(fixture.source_metadata(replacement.source_id).contains("import_projection_hash"));
  }

#if __has_include("loom/usage_policy.h")
  TEST_CASE("real W2 confirmation preserves historical estimates and binds every effective numeric value") {
    bool custom_values = false;
    SUBCASE("historical pending receipt remains byte-compatible") {}
    SUBCASE("nonlegacy pending receipt records all five values") { custom_values = true; }
    BindingFixture fixture;
    Config config(fixture.temporary.path() / "config.json");
    const auto path = fixture.write(Json::array({provider_conversation(0)}));
    const std::string cohort = "synthetic/preset-binding/source-bytes";
    const auto policy_options = unwrap(effective_usage_policy_options(config));
    auto policy = unwrap(UsagePolicy::open(fixture.temporary.path() / "usage-policy.sqlite", policy_options));
    unwrap(policy->request(Json{{"operation_id", "measured-baseline"}, {"baseline_key", cohort},
      {"resources", Json{{"source_bytes", 1}}}}));
    unwrap(policy->complete("measured-baseline", Json{{"resources", Json{{"source_bytes", 1}}},
      {"provenance", "instrument_measured"}}));
    auto options = fixture.options;
    if (custom_values) options.json_max_depth = 0;
    Json historical{{"operation_id", "pending-import"}, {"baseline_key", cohort},
      {"resources", Json{{"source_bytes", std::filesystem::file_size(path)}}},
      {"source_hash", unwrap(sha256_file_hex(path))}, {"parser_version", std::string(kExportParserVersion)},
      {"import_options", Json{{"export_mode", "on"}, {"force", false}, {"resume", true}}}};
    if (custom_values) {
      historical["import_options"]["preset_values"] = import_preset_values(options);
      historical["import_options"]["preset_values_sha256"] = import_preset_hash(options);
      historical["import_options"]["projection_values_sha256"] = import_projection_hash(options);
    }
    const auto pending = unwrap(policy->request(historical));
    REQUIRE(pending["status"] == "requires_confirmation");
    auto usage = unwrap(ImportUsageSession::open(config, fixture.temporary.path()));
    const auto pause = usage->request(path, options, "pending-import", cohort);
    REQUIRE_FALSE(pause);
    CHECK(pause.error().code == Errc::Cancelled);
    CHECK(usage->receipt().dump() == pending.dump());
    CHECK(usage->receipt()["estimate"].dump() == historical.dump());
    const auto receipt = pending["receipt_id"].get<std::string>();
    for (const auto& changed : changed_values(options)) {
      const auto conflict = usage->request(path, changed, "pending-import", cohort, receipt, "synthetic-owner");
      REQUIRE_FALSE(conflict);
      CHECK(conflict.error().code == Errc::Conflict);
      CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM conversations")) == std::optional<std::int64_t>(0));
    }
    LOOM_REQUIRE_OK(usage->request(path, options, "pending-import", cohort, receipt, "synthetic-owner"));
    CHECK(usage->receipt()["confirmation"]["approved"] == true);
    options.expected_source_hash = usage->receipt()["estimate"]["source_hash"].get<std::string>();
    options.expected_source_bytes = usage->receipt()["estimate"]["resources"]["source_bytes"].get<std::int64_t>();
    const auto imported = unwrap(fixture.importer.import_file(path, options));
    REQUIRE(imported.conversations.size() == 1);
    LOOM_REQUIRE_OK(usage->complete(imported));
    CHECK(usage->receipt()["status"] == "completed");
  }
#endif
}
