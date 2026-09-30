// Authored synthetic regressions. Frozen W6 datasets/oracles are unchanged.
#include <doctest/doctest.h>

#include <map>
#include <set>
#include <utility>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "../src/import/export_internal.h"
#include "../third_party/miniz/miniz.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;
namespace {
using Members = std::vector<std::pair<std::string, std::string>>;
void zip_write(const std::filesystem::path& path, const Members& members) {
  mz_zip_archive zip{};
  REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
  for (const auto& [name, bytes] : members)
    REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION));
  REQUIRE(mz_zip_writer_finalize_archive(&zip));
  REQUIRE(mz_zip_writer_end(&zip));
}
Json conversation(std::string id = "one") {
  return Json{{"uuid", id}, {"name", id}, {"chat_messages", Json::array({
    Json{{"uuid", "message-" + id}, {"sender", "human"}, {"text", "Żółw 🐢 " + id}}
  })}};
}
Json openai_conversation(std::string id) {
  Json message{{"id", "message-" + id}, {"author", Json{{"role", "user"}}},
               {"content", Json{{"content_type", "text"}, {"parts", Json::array({id})}}}};
  Json node{{"id", "node"}, {"parent", nullptr}, {"children", Json::array()}, {"message", message}};
  return Json{{"id", id}, {"title", id}, {"mapping", Json{{"node", node}}}, {"current_node", "node"}};
}
struct Fx {
  fsutil::TempDir temp;
  std::unique_ptr<Database> db = open_db(temp.path() / "import.db");
  EventBus bus;
  BlobStore blobs{temp.path() / "blobs", *db};
  ProvenanceStore provenance{*db};
  ConversationImporter importer{*db, bus, &blobs, &provenance};
  ImportOptions options;
  Fx() { options.export_mode = ExportMode::On; }
  SourceRecord source(const std::string& id) {
    auto result = unwrap(provenance.get_source(id)); REQUIRE(result.has_value()); return *result;
  }
  std::vector<SourceRecord> members(const std::string& parent) {
    std::vector<SourceRecord> out;
    for (const auto& item : unwrap(provenance.list_sources(1000))) {
      if (item.parser == "loom.importer.zip.member" && json::get_string(item.metadata, "parent_source_id") == parent)
        out.push_back(item);
    }
    return out;
  }
  void check_bytes(const SourceRecord& member, const std::string& parent, const std::string& name,
                   int index, const std::string& bytes) {
    CHECK(member.kind == "zip_member");
    CHECK(unwrap(blobs.read(member.blob_hash)) == bytes);
    LOOM_REQUIRE_OK(blobs.verify(member.blob_hash));
    CHECK(member.metadata["parent_source_id"] == parent);
    const Json locator{{"source", "sha256:" + source(parent).blob_hash}, {"member", name}, {"archive_index", index}};
    CHECK(member.metadata["locator"] == locator);
    auto links = unwrap(provenance.for_subject(member.id)); REQUIRE(links.size() == 1);
    CHECK(links[0].subject_kind == "source"); CHECK(links[0].source_id == parent); CHECK(links[0].locator == locator);
  }
};
}

TEST_SUITE("import_source_materialization") {
  TEST_CASE("bounded prefix scanning keeps array object wrapper and split BOM dispatch") {
    fsutil::TempDir dir;
    const auto path = dir.path() / "prefix.json";
    const Json item = conversation();
    const std::vector<Json> roots{item, Json::array({item, conversation("two")}),
      Json{{"conversations", Json::array({item})}, {"unknown", Json{{"empty", Json::array()}, {"null", nullptr}}}}};
    for (std::size_t padding : {65534U, 65535U, 65536U, 65537U, 196609U}) {
      for (bool bom : {false, true}) {
        for (const auto& root : roots) {
          LOOM_REQUIRE_OK(fsutil::write_file(path, std::string(padding, ' ') + (bom ? "\xEF\xBB\xBF \n" : "") + root.dump()));
          xport::Loader loader;
          std::vector<Json> got;
          loader.element = [&](Json&& value, std::int64_t index) { CHECK(index == static_cast<std::int64_t>(got.size())); got.push_back(std::move(value)); return true; };
          const auto stats = xport::load_json_file(path, loader);
          CHECK_FALSE(stats.invalid); CHECK_FALSE(stats.empty); CHECK_FALSE(stats.truncated); CHECK_FALSE(stats.invalid_utf8);
          if (root.contains("conversations")) {
            CHECK(loader.wrapper); CHECK(loader.top_is_array); REQUIRE(got.size() == 1); CHECK(got[0] == item);
            CHECK(loader.wrapper_fields == Json{{"unknown", root["unknown"]}});
          } else if (root.is_array()) {
            CHECK_FALSE(loader.wrapper); CHECK(loader.top_is_array); CHECK(Json(got) == root);
          } else {
            CHECK_FALSE(loader.wrapper); CHECK_FALSE(loader.top_is_array); REQUIRE(got.size() == 1); CHECK(got[0] == root);
          }
        }
      }
    }
  }

  TEST_CASE("prefix scan retains nonobject values UTF8 errors cancellation and streaming stop") {
    fsutil::TempDir dir; const auto path = dir.path() / "values.json";
    auto load = [&](const std::string& raw, xport::Loader& loader) { LOOM_REQUIRE_OK(fsutil::write_file(path, raw)); return xport::load_json_file(path, loader); };
    xport::Loader loader; std::vector<Json> got; int bad = 0;
    loader.element = [&](Json&& value, std::int64_t) { got.push_back(std::move(value)); return true; };
    loader.bad_element = [&](std::int64_t, const std::string&) { ++bad; };
    // Prefix offset 1 + JSON string prefix 10 + padding 65524 puts the
    // first emoji byte at the last byte of the 64 KiB input chunk.
    const Json values = Json::array({nullptr, 42, std::string(65524, 'x') + "🦉", Json{{"ok", true}}});
    auto stats = load(std::string(131073, '\t') + values.dump(), loader);
    CHECK(loader.top_is_array); CHECK(Json(got) == values); CHECK_FALSE(stats.invalid_utf8);
    got.clear(); loader.top_is_array = false;
    stats = load(std::string(65537, ' ') + "not-json[{}]", loader); CHECK(stats.invalid); CHECK(got.empty()); CHECK_FALSE(loader.top_is_array);
    stats = load(std::string(196608, ' '), loader); CHECK(stats.empty);
    stats = load(std::string(65535, ' ') + "\xEF\xBB", loader); CHECK(stats.invalid); CHECK(stats.invalid_utf8);
    stats = load(std::string(65537, ' ') + "[{bad},{\"ok\":true}]", loader); CHECK(bad == 1); REQUIRE(got.size() == 1); CHECK(got[0]["ok"] == true);
    got.clear(); stats = load(std::string(65537, ' ') + "[{\"ok\":true},{\"tail\":", loader); CHECK(stats.truncated); REQUIRE(got.size() == 1);
    got.clear(); int polls = 0; loader.cancelled = [&] { return ++polls == 3; };
    stats = load(std::string(262144, ' ') + "[{}]", loader); CHECK(stats.cancelled); CHECK(got.empty());
    loader.cancelled = {}; loader.element = [&](Json&& value, std::int64_t) { got.push_back(std::move(value)); return false; };
    stats = load(std::string(65537, ' ') + "[{} ," + std::string(262144, 'x'), loader);
    REQUIRE(got.size() == 1); CHECK_FALSE(stats.truncated); CHECK_FALSE(stats.invalid);
  }

  TEST_CASE("every member has exact independent bytes locator and reusable complete outcome") {
    Fx fx; const auto path = fx.temp.path() / "all.zip";
    const Members input{{"conversations.json", Json::array({conversation()}).dump()}, {"users.json", "[]"},
                        {"chat.html", "<html>independent raw viewer</html>"}, {"arbitrary.bin", std::string("\0\xFF\x80", 3) + "data"},
                        {"folder//note.dat", "same-bytes"}, {"other.dat", "same-bytes"}};
    zip_write(path, input); const auto outer = unwrap(fsutil::read_file(path));
    auto result = unwrap(fx.importer.import_file(path, fx.options)); REQUIRE(result.conversations.size() == 1);
    CHECK(unwrap(fx.blobs.read(result.blob_hash)) == outer); CHECK(fx.source(result.source_id).metadata["import_status"] == "complete");
    auto members = fx.members(result.source_id); REQUIRE(members.size() == input.size());
    for (const auto& member : members) {
      const auto index = member.metadata["locator"]["archive_index"].get<int>();
      fx.check_bytes(member, result.source_id, input[static_cast<std::size_t>(index)].first, index, input[static_cast<std::size_t>(index)].second);
    }
    REQUIRE(result.export_report["members"].size() == input.size());
    for (const auto& member : result.export_report["members"]) CHECK(fx.blobs.has(member["blob_hash"].get<std::string>()));
    const auto before = unwrap(fx.provenance.list_sources(1000)).size();
    auto again = unwrap(fx.importer.import_file(path, fx.options)); CHECK(again.already_imported); CHECK(again.source_id == result.source_id);
    CHECK(again.conversations[0].id == result.conversations[0].id); CHECK(again.export_report == result.export_report);
    CHECK(unwrap(fx.provenance.list_sources(1000)).size() == before);
  }

  TEST_CASE("duplicate member names preserve both conversations and expose asset ambiguity") {
    Fx fx; const auto path = fx.temp.path() / "duplicates.zip";
    const Members input{{"conversations.json", Json::array({openai_conversation("first")}).dump()},
                        {"conversations.json", Json::array({openai_conversation("second")}).dump()},
                        {"file-dup.png", "asset-one"}, {"file-dup.png", "asset-two"}};
    zip_write(path, input); const auto result = unwrap(fx.importer.import_file(path, fx.options));
    REQUIRE(result.conversations.size() == 2); REQUIRE(fx.members(result.source_id).size() == 4);
    for (const auto& member : fx.members(result.source_id)) {
      const auto i = member.metadata["locator"]["archive_index"].get<int>();
      fx.check_bytes(member, result.source_id, input[static_cast<std::size_t>(i)].first, i, input[static_cast<std::size_t>(i)].second);
    }
    for (std::size_t i = 0; i < result.conversations.size(); ++i) {
      auto provenance = unwrap(fx.provenance.for_subject(result.conversations[i].id)); REQUIRE(provenance.size() == 1);
      CHECK(provenance[0].locator["archive_index"] == i); CHECK(provenance[0].locator["member"] == "conversations.json");
      CHECK(result.conversations[i].metadata["export"]["fields"]["id"] == (i == 0 ? "first" : "second"));
    }
    CHECK(result.export_report["partial"] == true); CHECK(fx.source(result.source_id).metadata["import_status"] == "partial");
    bool ambiguity = false; for (const auto& error : result.export_report["errors"]) ambiguity |= error["code"] == "ambiguous_asset_member";
    CHECK(ambiguity);
    xport::AssetIndex assets; assets.add("file-dup.png", {}, 1); assets.add("file-dup.png", {}, 1);
    CHECK_FALSE(assets.resolve("file-dup", "")); CHECK(assets.links.empty()); CHECK(assets.unresolved.count("file-dup") == 1);
  }

  TEST_CASE("nested archive origin chain and zero-conversation member bytes survive dedup") {
    Fx fx; const auto inner = fx.temp.path() / "inner.zip"; const auto outer = fx.temp.path() / "outer.zip";
    zip_write(inner, {{"conversations.json", Json::array({conversation()}).dump()}, {"raw.bin", "inner-binary"}});
    const auto inner_bytes = unwrap(fsutil::read_file(inner)); zip_write(outer, {{"folder/inner.zip", inner_bytes}, {"outer.bin", "outer-binary"}});
    auto result = unwrap(fx.importer.import_file(outer, fx.options)); REQUIRE(result.conversations.size() == 1);
    auto ancestry = unwrap(fx.provenance.for_subject(result.conversations[0].id)); REQUIRE(ancestry.size() == 1);
    const auto inner_source = fx.source(ancestry[0].source_id);
    CHECK(inner_source.metadata["parent_source_id"] == result.source_id);
    CHECK(inner_source.metadata["locator"]["source"] == "sha256:" + result.blob_hash);
    CHECK(inner_source.metadata["locator"]["member"] == "folder/inner.zip"); CHECK(inner_source.metadata["locator"]["archive_index"] == 0);
    CHECK(unwrap(fx.blobs.read(inner_source.blob_hash)) == inner_bytes);
    CHECK(fx.members(inner_source.id).size() == 2); CHECK(fx.members(result.source_id).size() == 2);
    auto again = unwrap(fx.importer.import_file(outer, fx.options)); CHECK(again.already_imported); CHECK(again.conversations[0].id == result.conversations[0].id);
    const auto assets_only = fx.temp.path() / "assets.zip"; zip_write(assets_only, {{"opaque.dat", std::string("\0raw", 4)}});
    auto empty = unwrap(fx.importer.import_file(assets_only, fx.options)); CHECK(empty.conversations.empty());
    auto empty_again = unwrap(fx.importer.import_file(assets_only, fx.options)); CHECK(empty_again.already_imported); CHECK(empty_again.source_id == empty.source_id);
  }

  TEST_CASE("cancel during materialization retains prior bytes and retries without a false cache hit") {
    Fx fx; const auto path = fx.temp.path() / "cancel.zip";
    zip_write(path, {{"first.bin", "first"}, {"conversations.json", Json::array({conversation()}).dump()}});
    CancelToken cancel; fx.options.cancel = &cancel;
    fx.options.progress = [&](std::int64_t, std::int64_t, std::string_view stage) { if (stage == "zip.materialize") cancel.cancel(); };
    auto partial = unwrap(fx.importer.import_file(path, fx.options)); CHECK(partial.cancelled); CHECK(partial.conversations.empty());
    CHECK(fx.source(partial.source_id).metadata["import_status"] == "cancelled"); REQUIRE(fx.members(partial.source_id).size() == 1);
    fx.check_bytes(fx.members(partial.source_id)[0], partial.source_id, "first.bin", 0, "first");
    CHECK(unwrap(fx.blobs.read(partial.blob_hash)) == unwrap(fsutil::read_file(path)));
    cancel.reset(); fx.options.progress = {}; auto retry = unwrap(fx.importer.import_file(path, fx.options));
    CHECK_FALSE(retry.already_imported); REQUIRE(retry.conversations.size() == 1); CHECK(retry.source_id != partial.source_id);
    CHECK(fx.source(partial.source_id).metadata["import_status"] == "cancelled"); CHECK(fx.source(retry.source_id).metadata["import_status"] == "complete");
  }

  TEST_CASE("cancelled and malformed partial imports keep prior conversations and retry explicitly") {
    Fx fx; const auto path = fx.temp.path() / "partial.json";
    LOOM_REQUIRE_OK(fsutil::write_file(path, std::string(131073, ' ') + Json::array({conversation("one"), conversation("two")}).dump()));
    CancelToken cancel; fx.options.cancel = &cancel;
    fx.options.progress = [&](std::int64_t current, std::int64_t, std::string_view stage) { if (stage == "export" && current == 1) cancel.cancel(); };
    const auto stopped = unwrap(fx.importer.import_file(path, fx.options)); REQUIRE(stopped.conversations.size() == 1); CHECK(stopped.cancelled);
    CHECK(fx.source(stopped.source_id).metadata["import_status"] == "cancelled");
    cancel.reset(); fx.options.progress = {}; auto retry = unwrap(fx.importer.import_file(path, fx.options));
    CHECK_FALSE(retry.already_imported); REQUIRE(retry.conversations.size() == 2); CHECK(unwrap(fx.db->list_convs(100)).size() == 3);
    CHECK(unwrap(fx.db->get_conv(stopped.conversations[0].id)).has_value());
    CHECK(unwrap(fx.importer.import_file(path, fx.options)).already_imported);
    const auto bad = fx.temp.path() / "malformed.zip";
    zip_write(bad, {{"conversations.json", Json::array({conversation("bad-member")}).dump()}, {"users.json", "{invalid"}});
    auto first = unwrap(fx.importer.import_file(bad, fx.options)); REQUIRE(first.conversations.size() == 1); CHECK(first.export_report["partial"] == true);
    CHECK(fx.source(first.source_id).metadata["import_status"] == "partial");
    auto second = unwrap(fx.importer.import_file(bad, fx.options)); CHECK_FALSE(second.already_imported); CHECK(second.conversations[0].id != first.conversations[0].id);
    CHECK(fx.source(first.source_id).metadata["import_status"] == "partial");
    const auto early = fx.temp.path() / "error-before-provider.json";
    LOOM_REQUIRE_OK(fsutil::write_file(early, "[{bad}," + conversation("valid-after-bad").dump() + "]"));
    auto prefix = unwrap(fx.importer.import_file(early, fx.options)); REQUIRE(prefix.conversations.size() == 1);
    CHECK(prefix.export_report["partial"] == true); REQUIRE(prefix.export_report["errors"].size() == 1);
    CHECK(prefix.export_report["errors"][0]["index"] == 0); CHECK(fx.source(prefix.source_id).metadata["import_status"] == "partial");
    auto prefix_retry = unwrap(fx.importer.import_file(early, fx.options)); CHECK_FALSE(prefix_retry.already_imported);
    REQUIRE(prefix_retry.conversations.size() == 1); CHECK(prefix_retry.conversations[0].id != prefix.conversations[0].id);
    CHECK(unwrap(fx.db->get_conv(prefix.conversations[0].id)).has_value());
  }

  TEST_CASE("export-2 sources remain unchanged and legacy ZIP mode also materializes opaque members") {
    Fx fx; const auto path = fx.temp.path() / "upgrade.zip";
    zip_write(path, {{"conversations.json", Json::array({conversation()}).dump()}, {"opaque.bin", "unparsed"}});
    auto blob = unwrap(fx.blobs.put_file(path)); SourceRecord old; old.kind = "file"; old.blob_hash = blob.hash;
    old.parser = "loom.importer.zip.export"; old.parser_version = "export-2"; old.metadata = Json{{"historical", true}};
    const auto old_id = unwrap(fx.provenance.add_source(old));
    auto result = unwrap(fx.importer.import_file(path, fx.options)); CHECK_FALSE(result.already_imported); CHECK(result.source_id != old_id);
    CHECK(fx.source(old_id).metadata == old.metadata); CHECK(fx.source(result.source_id).parser_version == "export-3");
    ImportOptions legacy; legacy.export_mode = ExportMode::Off;
    auto imported = unwrap(fx.importer.import_file(path, legacy)); CHECK(imported.export_report.is_null());
    REQUIRE(fx.members(imported.source_id).size() == 2);
    bool opaque = false; for (const auto& source : fx.members(imported.source_id)) opaque |= unwrap(fx.blobs.read(source.blob_hash)) == "unparsed";
    CHECK(opaque);
  }
}
