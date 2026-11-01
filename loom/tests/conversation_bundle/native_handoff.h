// Integrator-only native gate. Requires integration.patch and the existing
// test runner; intentionally outside the root test glob in the author delta.
#pragma once
#include <doctest/doctest.h>
#include "../../third_party/miniz/miniz.h"

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/util/sha256.h"
#include "import/import_preset_identity.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
  inline void conversation_bundle_native_handoff_fixture() {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "restore.db");
    EventBus bus;
    BlobStore blobs(td.path() / "blobs", *db);
    ProvenanceStore provenance(*db);
    ConversationImporter importer(*db, bus, &blobs, &provenance);
    const std::string snapshot = R"({"conversation":{"id":"c_fixture","title":"Synthetic"},"messages":[{"role":"user","text":"First"},{"role":"assistant","text":"Second"}]})";
    const std::string original = R"({"messages":[{"role":"user","text":"This attachment is not a second conversation"}]})";
    const std::string original_hash = Sha256::hex(original);
    const auto path = td.path() / "bundle.zip";
    mz_zip_archive archive{};
    REQUIRE(mz_zip_writer_init_file(&archive, path.string().c_str(), 0));
    const std::vector<std::pair<std::string, std::string>> members{
        {"conversation.json", snapshot}, {"originals/" + original_hash, original},
        {"rendered/conversation.md", "## User\nA presentation, not another import\n"},
        {"bundle.index.json", R"({"fixture":"native member-selection mechanism only"})"}};
    for (const auto& [name, bytes] : members)
      REQUIRE(mz_zip_writer_add_mem(&archive, name.c_str(), bytes.data(), bytes.size(), MZ_NO_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&archive));
    REQUIRE(mz_zip_writer_end(&archive));

    ImportOptions options;
    options.export_mode = ExportMode::Off;
    options.zip_interpret_members = std::vector<std::string>{"conversation.json"};
    options.expected_source_hash = unwrap(sha256_file_hex(path));
    options.expected_source_bytes = static_cast<std::int64_t>(std::filesystem::file_size(path));
    const auto result = unwrap(importer.import_file(path, options));
    REQUIRE(result.conversations.size() == 1);
    const auto messages = unwrap(db->get_msgs(result.conversations[0].id, true));
    REQUIRE(messages.size() == 2);
    CHECK(messages[0].text == "First");
    CHECK(messages[1].text == "Second");
    CHECK(unwrap(blobs.read(original_hash)) == original);
    LOOM_REQUIRE_OK(blobs.verify(original_hash));
    CHECK_FALSE(unwrap(provenance.find_sources_by_hash(original_hash)).empty());

    const auto cached = unwrap(importer.import_file(path, options));
    CHECK(cached.already_imported);
    ImportOptions all;
    all.export_mode = ExportMode::Off;
    CHECK(import_projection_hash(all) != import_projection_hash(options));
    CHECK(import_preset_hash(all) != import_preset_hash(options));
    CHECK_FALSE(import_projection_is_legacy(options));
    const auto unfiltered = unwrap(importer.import_file(path, all));
    CHECK_FALSE(unfiltered.already_imported);
    CHECK(unfiltered.conversations.size() > 1);

    const auto before = unwrap(db->list_convs()).size();
    auto invalid = options;
    invalid.export_mode = ExportMode::Auto;
    CHECK_FALSE(importer.import_file(path, invalid));
    CHECK(unwrap(db->list_convs()).size() == before);

    auto missing = options;
    missing.zip_interpret_members = std::vector<std::string>{"absent.json"};
    CHECK_FALSE(importer.import_file(path, missing));
    CHECK(unwrap(db->list_convs()).size() == before);

    // Selection order/duplicates have no semantic effect on projection identity.
    auto equivalent = options;
    equivalent.zip_interpret_members = std::vector<std::string>{"conversation.json", "conversation.json"};
    CHECK(import_projection_hash(equivalent) == import_projection_hash(options));
  }
}
