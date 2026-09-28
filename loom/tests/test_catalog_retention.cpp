// R14/R18/R20: copy means durable byte retention, independently of the
// normalized chat projection. Link remains an explicit external dependency.
#include <doctest/doctest.h>

#include <fstream>
#include <map>

#include "../third_party/miniz/miniz.h"
#include "loom/catalog.h"
#include "loom/knowledge.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::catalog;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<Runtime> retention_runtime(const fs::path& path) {
  RuntimeOptions options;
  options.data_dir = path.string();
  options.start_workers = false;
  return unwrap(Runtime::open(options));
}

void retention_write(const fs::path& path, std::string_view bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  REQUIRE(output.good());
}

std::string retention_fixture() {
  // Unknown conversation/message fields and attachment references must
  // survive even when no interpreter understands them yet.
  return R"([
 {"id":"copy-a","title":"Zorblex archive","provider_extension":{"unknown":[1,{"future":true}]},
  "mapping":{"n1":{"id":"n1","parent":null,"children":[],"message":{
   "id":"m-original-a","author":{"role":"user"},"create_time":1700000001,
   "content":{"content_type":"text","parts":["Zorblex must preserve every source byte."]},
   "metadata":{"attachments":[{"id":"asset-a","filename":"attachments/asset.bin"}],"unknown_flag":"keep"}}}}},
 {"id":"copy-b","title":"unrelated weekend","unknown_provider_key":"also keep",
  "mapping":{"n2":{"id":"n2","parent":null,"children":[],"message":{
   "id":"m-original-b","author":{"role":"assistant"},"create_time":1700000100,
   "content":{"content_type":"text","parts":["A separate conversation outside the selected scope."]},
   "metadata":{"model_slug":"fixture/model"}}}}}
]
 )";
}

std::string retention_text(Runtime& rt, std::string_view sql, const std::string& id) {
  auto lock = rt.db().lock();
  auto value = unwrap(rt.db().conn().query_text(sql, id));
  REQUIRE(value.has_value());
  return *value;
}

std::int64_t retention_count(Runtime& rt, std::string_view table) {
  auto lock = rt.db().lock();
  return unwrap(rt.db().conn().query_int("SELECT COUNT(*) FROM " + std::string(table))).value_or(0);
}

std::vector<CatalogUnit> retention_scan(Catalog& catalog, const fs::path& path) {
  ScanConfig scan;
  scan.sources = {path.string()};
  const auto result = unwrap(catalog.scan(scan));
  CHECK(result["warnings"].empty());
  UnitQuery query;
  query.limit = 100;
  query.sort = "id";
  return unwrap(catalog.query(query));
}

void check_unit_copy(Runtime& rt, const CatalogUnit& unit, std::string_view original) {
  const auto raw = retention_text(rt, "SELECT raw_blob FROM loom_cat_imports WHERE unit_id = ?", unit.unit.id);
  REQUIRE_FALSE(raw.empty());
  CHECK(raw == unit.content_hash);
  CHECK(rt.blobs().has(raw));
  CHECK(unwrap(rt.blobs().read(raw)) == original);
  LOOM_REQUIRE_OK(rt.blobs().verify(raw));
  const auto source = retention_text(rt, "SELECT source_id FROM loom_cat_imports WHERE unit_id = ?", unit.unit.id);
  const auto source_record = unwrap(rt.provenance().get_source(source));
  REQUIRE(source_record.has_value());
  CHECK(source_record->blob_hash == raw);
}

void retention_zip(const fs::path& path, const std::map<std::string, std::string>& members) {
  mz_zip_archive archive{};
  REQUIRE(mz_zip_writer_init_file(&archive, path.string().c_str(), 0) == MZ_TRUE);
  for (const auto& [name, bytes] : members) {
    REQUIRE(mz_zip_writer_add_mem(&archive, name.c_str(), bytes.data(), bytes.size(), MZ_BEST_SPEED) == MZ_TRUE);
  }
  REQUIRE(mz_zip_writer_finalize_archive(&archive) == MZ_TRUE);
  REQUIRE(mz_zip_writer_end(&archive) == MZ_TRUE);
}

}  // namespace

TEST_SUITE("catalog_retention") {
  TEST_CASE("full copy retains exact source and units with chat import disabled; reads survive source deletion") {
    fsutil::TempDir data;
    fsutil::TempDir input;
    const auto source = input.path() / "conversations.json";
    const auto original = retention_fixture();
    retention_write(source, original);
    auto rt = retention_runtime(data.path());
    Catalog catalog(*rt, unwrap(rt->knowledge().pack()));
    const auto units = retention_scan(catalog, source);
    REQUIRE(units.size() == 2);
    std::map<std::string, std::string> bytes;
    for (const auto& unit : units) bytes[unit.unit.id] = unwrap(catalog.read_unit(unit.unit.id));

    ImportOptions options;
    options.mode = "full";
    options.store_mode = "copy";
    options.import_messages = false;
    const auto imported = unwrap(catalog.import_selected(options));
    CHECK(json::get_int(imported, "imported") == 2);
    CHECK(retention_count(*rt, "conversations") == 0);
    CHECK(retention_count(*rt, "messages") == 0);
    const auto source_hash = Sha256::hex(original);
    REQUIRE(rt->blobs().has(source_hash));
    CHECK(unwrap(rt->blobs().read(source_hash)) == original);
    for (const auto& unit : units) check_unit_copy(*rt, unit, bytes.at(unit.unit.id));

    REQUIRE(fs::remove(source));
    for (const auto& unit : units) CHECK(unwrap(catalog.read_unit(unit.unit.id)) == bytes.at(unit.unit.id));

    // A missing per-unit reference must still be reconstructible from the
    // retained complete source, not from the external pathname.
    {
      auto lock = rt->db().lock();
      LOOM_REQUIRE_OK(rt->db().conn().run("UPDATE loom_cat_imports SET raw_blob = '' WHERE unit_id = ?", units[0].unit.id));
    }
    CHECK(unwrap(catalog.read_unit(units[0].unit.id)) == bytes.at(units[0].unit.id));
    rt->shutdown();
    auto reopened = retention_runtime(data.path());
    Catalog restored(*reopened, unwrap(reopened->knowledge().pack()));
    for (const auto& unit : units) CHECK(unwrap(restored.read_unit(unit.unit.id)) == bytes.at(unit.unit.id));
  }

  TEST_CASE("full ZIP copy preserves original archive and non-conversation binary members") {
    fsutil::TempDir data;
    fsutil::TempDir input;
    const auto source = input.path() / "archive.zip";
    const std::string binary("asset\0unknown\xff\x01", 15);
    retention_zip(source, {{"conversations.json", retention_fixture()}, {"attachments/asset.bin", binary}});
    const auto original = unwrap(fsutil::read_file(source));
    auto rt = retention_runtime(data.path());
    Catalog catalog(*rt, unwrap(rt->knowledge().pack()));
    const auto units = retention_scan(catalog, source);
    REQUIRE(units.size() == 3);
    std::map<std::string, std::string> bytes;
    for (const auto& unit : units) bytes[unit.unit.id] = unwrap(catalog.read_unit(unit.unit.id));
    ImportOptions options;
    options.mode = "full";
    options.import_messages = false;
    CHECK(json::get_int(unwrap(catalog.import_selected(options)), "imported") == 3);
    REQUIRE(rt->blobs().has(Sha256::hex(original)));
    CHECK(unwrap(rt->blobs().read(Sha256::hex(original))) == original);
    bool found_attachment = false;
    for (const auto& unit : units) {
      check_unit_copy(*rt, unit, bytes.at(unit.unit.id));
      if (unit.unit.locator.member == "attachments/asset.bin") {
        found_attachment = true;
        CHECK(bytes.at(unit.unit.id) == binary);
      }
    }
    REQUIRE(found_attachment);
    REQUIRE(fs::remove(source));
    for (const auto& unit : units) CHECK(unwrap(catalog.read_unit(unit.unit.id)) == bytes.at(unit.unit.id));
    {
      auto lock = rt->db().lock();
      LOOM_REQUIRE_OK(rt->db().conn().run("UPDATE loom_cat_imports SET raw_blob = ''"));
    }
    for (const auto& unit : units) CHECK(unwrap(catalog.read_unit(unit.unit.id)) == bytes.at(unit.unit.id));
  }

  TEST_CASE("selective copy retains chosen unit without copying unrelated source content") {
    fsutil::TempDir data;
    fsutil::TempDir input;
    const auto source = input.path() / "conversations.json";
    const auto original = retention_fixture();
    retention_write(source, original);
    auto rt = retention_runtime(data.path());
    Catalog catalog(*rt, unwrap(rt->knowledge().pack()));
    const auto units = retention_scan(catalog, source);
    REQUIRE(units.size() == 2);
    const auto selected_bytes = unwrap(catalog.read_unit(units[0].unit.id));
    unwrap(catalog.build_profile(ProfileConfig{}));
    const auto score = unwrap(catalog.score(ScoreConfig{}));
    for (std::size_t index = 0; index < units.size(); ++index) {
      Override decision;
      decision.unit_id = units[index].unit.id;
      decision.action = index == 0 ? "include" : "exclude";
      decision.reason = "retention scope regression";
      LOOM_REQUIRE_OK(catalog.set_override(decision));
    }
    const auto run = json::get_string(score, "run_id");
    unwrap(catalog.select(run));
    ImportOptions options;
    options.run_id = run;
    options.import_messages = false;
    CHECK(json::get_int(unwrap(catalog.import_selected(options)), "imported") == 1);
    check_unit_copy(*rt, units[0], selected_bytes);
    CHECK_FALSE(rt->blobs().has(Sha256::hex(original)));
    CHECK_FALSE(rt->blobs().has(units[1].content_hash));
    REQUIRE(fs::remove(source));
    CHECK(unwrap(catalog.read_unit(units[0].unit.id)) == selected_bytes);
    CHECK_FALSE(catalog.read_unit(units[1].unit.id).has_value());
  }

  TEST_CASE("link remains external; upgrading to copy backfills retention without duplicating conversations") {
    fsutil::TempDir data;
    fsutil::TempDir input;
    const auto source = input.path() / "conversations.json";
    const auto original = retention_fixture();
    retention_write(source, original);
    auto rt = retention_runtime(data.path());
    Catalog catalog(*rt, unwrap(rt->knowledge().pack()));
    const auto units = retention_scan(catalog, source);
    REQUIRE(units.size() == 2);
    std::map<std::string, std::string> bytes;
    for (const auto& unit : units) bytes[unit.unit.id] = unwrap(catalog.read_unit(unit.unit.id));
    ImportOptions options;
    options.mode = "full";
    options.store_mode = "link";
    unwrap(catalog.import_selected(options));
    std::map<std::string, std::string> conversations;
    for (const auto& unit : units) {
      CHECK(retention_text(*rt, "SELECT raw_blob FROM loom_cat_imports WHERE unit_id = ?", unit.unit.id).empty());
      CHECK_FALSE(rt->blobs().has(unit.content_hash));
      conversations[unit.unit.id] = retention_text(*rt, "SELECT conv_id FROM loom_cat_imports WHERE unit_id = ?", unit.unit.id);
      REQUIRE_FALSE(conversations.at(unit.unit.id).empty());
    }
    CHECK_FALSE(rt->blobs().has(Sha256::hex(original)));
    REQUIRE(fs::remove(source));
    for (const auto& unit : units) CHECK_FALSE(catalog.read_unit(unit.unit.id).has_value());

    retention_write(source, original);
    options.store_mode = "copy";
    unwrap(catalog.import_selected(options));
    CHECK(retention_count(*rt, "conversations") == 2);
    for (const auto& unit : units) {
      check_unit_copy(*rt, unit, bytes.at(unit.unit.id));
      CHECK(retention_text(*rt, "SELECT conv_id FROM loom_cat_imports WHERE unit_id = ?", unit.unit.id) == conversations.at(unit.unit.id));
    }
    const auto messages = retention_count(*rt, "messages");
    const auto sources = retention_count(*rt, "loom_sources");
    const auto blobs = retention_count(*rt, "loom_blobs");
    const auto repeated = unwrap(catalog.import_selected(options));
    CHECK(json::get_int(repeated, "imported") == 0);
    CHECK(retention_count(*rt, "messages") == messages);
    CHECK(retention_count(*rt, "loom_sources") == sources);
    CHECK(retention_count(*rt, "loom_blobs") == blobs);
    REQUIRE(fs::remove(source));
    for (const auto& unit : units) CHECK(unwrap(catalog.read_unit(unit.unit.id)) == bytes.at(unit.unit.id));
  }

  TEST_CASE("adding linked chat projection does not discard an earlier selective raw copy") {
    fsutil::TempDir data;
    fsutil::TempDir input;
    const auto source = input.path() / "conversations.json";
    const auto original = retention_fixture();
    retention_write(source, original);
    auto rt = retention_runtime(data.path());
    Catalog catalog(*rt, unwrap(rt->knowledge().pack()));
    const auto units = retention_scan(catalog, source);
    REQUIRE(units.size() == 2);
    const auto selected_bytes = unwrap(catalog.read_unit(units[0].unit.id));
    unwrap(catalog.build_profile(ProfileConfig{}));
    const auto score = unwrap(catalog.score(ScoreConfig{}));
    for (std::size_t index = 0; index < units.size(); ++index) {
      Override decision;
      decision.unit_id = units[index].unit.id;
      decision.action = index == 0 ? "include" : "exclude";
      decision.reason = "copy-to-link projection regression";
      LOOM_REQUIRE_OK(catalog.set_override(decision));
    }
    const auto run = json::get_string(score, "run_id");
    unwrap(catalog.select(run));
    ImportOptions options;
    options.run_id = run;
    options.import_messages = false;
    unwrap(catalog.import_selected(options));
    REQUIRE(retention_count(*rt, "conversations") == 0);
    REQUIRE_FALSE(rt->blobs().has(Sha256::hex(original)));
    check_unit_copy(*rt, units[0], selected_bytes);
    REQUIRE(fs::remove(source));

    // Retention already exists. Adding a lightweight chat projection later
    // must not erase the only durable raw-byte reference.
    options.store_mode = "link";
    options.import_messages = true;
    unwrap(catalog.import_selected(options));
    CHECK(retention_count(*rt, "conversations") == 1);
    check_unit_copy(*rt, units[0], selected_bytes);
    CHECK(unwrap(catalog.read_unit(units[0].unit.id)) == selected_bytes);
    CHECK_FALSE(catalog.read_unit(units[1].unit.id).has_value());
    const auto repeated = unwrap(catalog.import_selected(options));
    CHECK(json::get_int(repeated, "imported") == 0);
    CHECK(retention_count(*rt, "conversations") == 1);
  }
}
