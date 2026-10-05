// Offline provider auxiliary parsing and checkpoint repair must use the same
// caller depth preset as conversation parsing, including zero = unlimited.
#include <doctest/doctest.h>

#include <map>
#include <memory>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "../third_party/miniz/miniz.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
Json nested_value(std::size_t depth) {
  Json value = "preserved leaf";
  for (std::size_t level = 0; level < depth; ++level) value = Json::array({std::move(value)});
  return value;
}

Json conversation(bool openai) {
  if (!openai) return Json{{"uuid", "depth-conversation"}, {"name", "Auxiliary depth preset"},
    {"chat_messages", Json::array({Json{{"uuid", "depth-message"}, {"sender", "human"},
      {"text", "Synthetic message"}}})}};
  return Json{{"conversation_id", "depth-conversation"}, {"title", "Auxiliary depth preset"},
    {"current_node", "depth-node"}, {"mapping", Json{{"depth-node", Json{{"id", "depth-node"},
      {"parent", nullptr}, {"children", Json::array()}, {"message", Json{{"id", "depth-message"},
        {"author", Json{{"role", "user"}}}, {"content", Json{{"content_type", "text"},
          {"parts", Json::array({"Synthetic message"})}}}}}}}}}};
}

struct DepthFixture {
  fsutil::TempDir temporary;
  std::unique_ptr<Database> db = open_db(temporary.path() / "depth.db");
  EventBus bus;
  BlobStore blobs{temporary.path() / "blobs", *db};
  ProvenanceStore provenance{*db};
  ConversationImporter importer{*db, bus, &blobs, &provenance};
  ImportOptions options;
  DepthFixture() { options.export_mode = ExportMode::On; options.resume = true; }

  std::filesystem::path archive(bool openai, const Json& auxiliary) {
    const auto path = temporary.path() / "synthetic.zip";
    const std::string member = openai ? "textdocs/depth.json" : "projects.json";
    const std::map<std::string, std::string> members{
      {"conversations.json", Json::array({conversation(openai)}).dump()},
      {member, openai ? auxiliary.dump() : Json::array({auxiliary}).dump()}};
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    for (const auto& [name, bytes] : members)
      REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    REQUIRE(mz_zip_writer_end(&zip));
    return path;
  }

  std::int64_t count_kind(const std::string& kind) {
    return unwrap(db->conn().query_int("SELECT COUNT(*) FROM nodes WHERE kind=?", kind)).value_or(-1);
  }
};

Json auxiliary_record(bool openai, std::size_t depth) {
  if (openai) return Json{{"conversation_id", "depth-conversation"}, {"name", "Deep artifact"},
    {"content", "Synthetic artifact"}, {"opaque", nested_value(depth)}};
  return Json{{"uuid", "depth-project"}, {"name", "Deep project"}, {"opaque", nested_value(depth)}};
}
}  // namespace

TEST_SUITE("import_aux_depth") {
  TEST_CASE("auxiliary depth honors a low caller preset and zero permits deeper source data") {
    std::size_t depth = 24;
    SUBCASE("payload below the old fixed 512 cutoff still honors a lower preset") {}
    SUBCASE("zero permits payload beyond the old fixed 512 cutoff") { depth = 550; }
    for (const bool openai : {false, true}) {
      CAPTURE(openai);
      CAPTURE(depth);
      DepthFixture fixture;
      const auto auxiliary = auxiliary_record(openai, depth);
      const auto path = fixture.archive(openai, auxiliary);
      const std::string member = openai ? "textdocs/depth.json" : "projects.json";
      const std::string kind = openai ? "export:artifact" : "export:project";
      fixture.options.json_max_depth = 8;
      const auto limited = unwrap(fixture.importer.import_file(path, fixture.options));
      REQUIRE(limited.conversations.size() == 1);
      CHECK(limited.export_report["partial"] == true);
      CHECK(fixture.count_kind(kind) == 0);
      CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM loom_import_checkpoints "
        "WHERE member=? AND source_index=-1", member)).value_or(-1) == 0);
      bool depth_diagnostic = false;
      for (const auto& error : limited.export_report["errors"])
        if (json::get_string(error, "message").find("deeper than 8") != std::string::npos) depth_diagnostic = true;
      CHECK(depth_diagnostic);

      fixture.options.json_max_depth = 0;
      const auto unlimited = unwrap(fixture.importer.import_file(path, fixture.options));
      REQUIRE(unlimited.conversations.size() == 1);
      CHECK(unlimited.resumed); CHECK_FALSE(unlimited.already_imported);
      CHECK(unlimited.source_id == limited.source_id);
      CHECK(unlimited.conversations[0].id == limited.conversations[0].id);
      CHECK(unlimited.export_report["partial"] == false);
      CHECK(fixture.count_kind(kind) == 1);
      const auto metadata_text = unwrap(fixture.db->conn().query_text("SELECT metadata FROM nodes WHERE kind=?", kind));
      REQUIRE(metadata_text);
      const auto metadata = unwrap(json::parse(*metadata_text));
      CHECK(metadata["export"]["record"] == auxiliary);
      CHECK(metadata["export"]["source_id"] == unlimited.source_id);
      const auto cached = unwrap(fixture.importer.import_file(path, fixture.options));
      CHECK(cached.already_imported); CHECK(fixture.count_kind(kind) == 1);
    }
  }

  TEST_CASE("auxiliary checkpoint reconciliation uses zero depth preset and preserves the owned node") {
    DepthFixture fixture;
    fixture.options.json_max_depth = 0;
    const auto path = fixture.archive(true, auxiliary_record(true, 550));
    const auto original = unwrap(fixture.importer.import_file(path, fixture.options));
    REQUIRE(original.conversations.size() == 1);
    CHECK(original.export_report["partial"] == false);
    const auto retained = unwrap(fixture.db->conn().query_text("SELECT id FROM nodes WHERE kind='export:artifact'"));
    REQUIRE(retained);
    LOOM_REQUIRE_OK(fixture.db->conn().exec("DELETE FROM links; "
      "UPDATE loom_import_checkpoints SET metadata=json_remove(metadata,'$.binding_version') "
      "WHERE member='textdocs/depth.json' AND source_index=-1;"));
    const auto repaired = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(repaired.resumed); CHECK_FALSE(repaired.already_imported);
    CHECK(repaired.export_report["partial"] == false);
    CHECK(repaired.source_id == original.source_id);
    CHECK(repaired.conversations[0].id == original.conversations[0].id);
    CHECK(unwrap(fixture.db->conn().query_text("SELECT id FROM nodes WHERE kind='export:artifact'")) == retained);
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM links WHERE src=? "
      "AND dst=? AND link_type='part_of'", *retained, original.conversations[0].id)).value_or(-1) == 1);
    CHECK(fixture.count_kind("export:artifact") == 1);
    CHECK(unwrap(fixture.importer.import_file(path, fixture.options)).already_imported);
  }
}
