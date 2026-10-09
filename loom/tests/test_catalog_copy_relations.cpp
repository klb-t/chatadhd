// CH-RES-N002: catalog copy uses the existing lossless provider writer.
#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "loom/catalog.h"
#include "loom/importer.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "test_helpers.h"
#include "../third_party/miniz/miniz.h"

using namespace loom;
using loom::test::unwrap;

namespace {
constexpr std::string_view fixture = R"([
 {"id":"safe-openai","title":"Synthetic branch","create_time":1700000000,
  "current_node":"b","unknown_conversation":{"nested":[null,7,"preserved"]},
  "mapping":{
   "root":{"id":"root","parent":null,"children":["u"],"message":null},
   "u":{"id":"u","parent":"root","children":["a","b"],"message":{
    "id":"source-u","author":{"role":"user"},"create_time":1700000001,
    "content":{"content_type":"text","parts":["Synthetic question"]},
    "metadata":{"unknown_message":"retained"}}},
   "a":{"id":"a","parent":"u","children":[],"message":{
    "id":"source-a","author":{"role":"assistant"},"create_time":1700000002,
    "content":{"content_type":"text","parts":["Synthetic alternate"]},
    "metadata":{"model_slug":"synthetic-model","unknown_message":[false,3]}}},
   "b":{"id":"b","parent":"u","children":[],"message":{
    "id":"source-b","author":{"role":"assistant"},"create_time":1700000003,
    "content":{"content_type":"text","parts":["Synthetic current"]},
    "metadata":{"unknown_message":{"keep":true}}}}}},
 {"uuid":"safe-anthropic","name":"Synthetic ordered conversation",
  "created_at":"2023-11-14T22:13:20Z","updated_at":"2023-11-14T22:13:25Z",
  "unknown_conversation":{"provider":"anthropic","keep":true},
  "chat_messages":[
   {"uuid":"cl-u","sender":"human","text":"Synthetic Claude question",
    "created_at":"2023-11-14T22:13:21Z","unknown_message":[1,null]},
   {"uuid":"cl-a","sender":"assistant","text":"Synthetic Claude answer",
    "parent_message_uuid":"cl-u","created_at":"2023-11-14T22:13:22Z",
    "unknown_message":{"keep":true}}]}
])";

std::unique_ptr<Runtime> runtime(const std::filesystem::path& path,
                                const std::shared_ptr<net::ScriptedTransport>& transport) {
  RuntimeOptions options;
  options.data_dir = path.string();
  options.start_workers = false;
  options.http = transport;
  return unwrap(Runtime::open(options));
}

void write_archive(const std::filesystem::path& path, std::string_view contents = fixture) {
  mz_zip_archive archive{};
  REQUIRE(mz_zip_writer_init_file(&archive, path.string().c_str(), 0));
  REQUIRE(mz_zip_writer_add_mem(&archive, "conversations.json", contents.data(), contents.size(), MZ_DEFAULT_COMPRESSION));
  REQUIRE(mz_zip_writer_finalize_archive(&archive));
  REQUIRE(mz_zip_writer_end(&archive));
}

Json signatures(Database& db) {
  std::map<std::string, Json> conversations;
  for (const auto& conversation : unwrap(db.list_convs(100))) {
    const auto& exported = conversation.metadata.at("export");
    const auto messages = unwrap(db.get_msgs(conversation.id, true));
    std::map<std::string, std::string> keys;
    std::map<std::string, std::vector<std::string>> groups;
    for (const auto& message : messages) {
      const auto key = message.metadata.at("export").at("key").get<std::string>();
      keys[message.id] = key;
      if (message.version_group_id) groups[*message.version_group_id].push_back(key);
    }
    for (auto& [id, members] : groups) {
      (void)id;
      std::sort(members.begin(), members.end());
    }
    Json normalized = Json::array();
    for (const auto& message : messages) {
      normalized.push_back(Json{{"key", keys.at(message.id)}, {"role", message.role}, {"text", message.text},
        {"parent", message.parent_id ? Json(keys.at(*message.parent_id)) : Json(nullptr)},
        {"model", message.model ? Json(*message.model) : Json(nullptr)}, {"status", message.status},
        {"group", message.version_group_id ? Json(groups.at(*message.version_group_id)) : Json(nullptr)},
        {"version_num", message.version_num}, {"weight", message.weight}, {"attachments", message.attachments},
        {"raw", message.metadata.at("export").at("raw")}});
    }
    conversations[exported.at("key").get<std::string>()] = Json{
      {"title", conversation.title}, {"source", conversation.source}, {"fields", exported.at("fields")},
      {"graph", exported.at("graph")}, {"messages", normalized}};
  }
  Json result = Json::object();
  for (const auto& [key, value] : conversations) result[key] = value;
  return result;
}

catalog::ScanConfig scan_options(const std::filesystem::path& source) {
  catalog::ScanConfig options;
  options.sources = {source.string()};
  options.threads = 1;
  options.retain_raw = "none";
  return options;
}

catalog::ImportOptions copy_options() {
  catalog::ImportOptions options;
  options.mode = "full";
  options.store_mode = "copy";
  return options;
}
}  // namespace

TEST_SUITE("catalog_copy_relations") {
  TEST_CASE("CH-RES-N002 copy matches full provider import and survives reopen") {
    fsutil::TempDir sources, direct_data, copied_data;
    const auto path = sources.path() / "synthetic.zip";
    write_archive(path);
    auto transport = std::make_shared<net::ScriptedTransport>();
    auto direct = runtime(direct_data.path(), transport);
    loom::ImportOptions direct_options;
    direct_options.export_mode = ExportMode::On;
    auto imported = unwrap(direct->importer().import_file(path, direct_options));
    REQUIRE(imported.conversations.size() == 2);
    const auto expected = signatures(direct->db());
    CHECK(expected["safe-openai"]["messages"][1]["status"] == "version");
    CHECK(expected["safe-openai"]["messages"][2]["parent"] == "u");
    CHECK(expected["safe-anthropic"]["messages"][1]["parent"] == "cl-u");
    auto copied = runtime(copied_data.path(), transport);
    {
      catalog::Catalog catalog(*copied, unwrap(copied->knowledge().pack()));
      unwrap(catalog.scan(scan_options(path)));
      const auto result = unwrap(catalog.import_selected(copy_options()));
      CHECK(result["conversations"].size() == 2);
      CHECK(signatures(copied->db()) == expected);
      for (const auto& conversation : unwrap(copied->db().list_convs(100))) {
        const auto& metadata = conversation.metadata.at("export");
        auto original = std::find_if(imported.conversations.begin(), imported.conversations.end(), [&](const auto& item) {
          return item.metadata.at("export").at("key") == metadata.at("key");
        });
        REQUIRE(original != imported.conversations.end());
        CHECK(metadata.at("mapping_index_scope") == "source_array");
        CHECK(metadata.at("source_index") == original->metadata.at("export").at("source_index"));
        CHECK(metadata.at("index") == original->metadata.at("export").at("index"));
        if (metadata.at("key") == "safe-anthropic") CHECK(metadata.at("index") == 1);
        const auto records = unwrap(copied->provenance().for_subject(conversation.id));
        REQUIRE(records.size() == 1);
        CHECK(records.front().transform == "catalog.import.export@export-3");
        CHECK(records.front().locator["member"] == "conversations.json");
        CHECK(records.front().locator["source_conversation_index"] == metadata.at("source_index"));
        const auto source = unwrap(copied->provenance().get_source(records.front().source_id));
        REQUIRE(source.has_value());
        CHECK(source->parser == "loom.catalog.import.export");
        CHECK(source->parser_version == kExportParserVersion);
        LOOM_REQUIRE_OK(copied->blobs().verify(source->blob_hash));
        for (const auto& message : unwrap(copied->db().get_msgs(conversation.id, true))) {
          const auto provenance = unwrap(copied->provenance().for_subject(message.id));
          REQUIRE(provenance.size() == 1);
          CHECK(provenance.front().source_id == source->id);
          CHECK(provenance.front().locator["source_key"] == message.metadata["export"]["key"]);
          CHECK(provenance.front().locator["byte_start"] == records.front().locator["byte_start"]);
        }
      }
    }
    copied.reset();
    std::filesystem::rename(path, sources.path() / "temporarily-unavailable.zip");
    copied = runtime(copied_data.path(), transport);
    CHECK(signatures(copied->db()) == expected);
    catalog::Catalog restored(*copied, unwrap(copied->knowledge().pack()));
    const auto retried = unwrap(restored.import_selected(copy_options()));
    CHECK(retried["skipped"] == 2);
    CHECK(retried["conversations"].empty());
    CHECK(signatures(copied->db()) == expected);
    CHECK(transport->requests().empty());
  }

  TEST_CASE("wrapped conversation selectors and original ordinals survive copy") {
    fsutil::TempDir sources, direct_data, copied_data;
    const auto path = sources.path() / "wrapped.zip";
    const auto contents = Json{{"conversations", Json::parse(fixture)}}.dump();
    write_archive(path, contents);
    auto transport = std::make_shared<net::ScriptedTransport>();
    auto direct = runtime(direct_data.path(), transport);
    const auto imported = unwrap(direct->importer().import_file(path));
    REQUIRE(imported.conversations.size() == 2);
    auto copied = runtime(copied_data.path(), transport);
    catalog::Catalog catalog(*copied, unwrap(copied->knowledge().pack()));
    unwrap(catalog.scan(scan_options(path)));
    unwrap(catalog.import_selected(copy_options()));
    for (const auto& conversation : unwrap(copied->db().list_convs(100))) {
      const auto& metadata = conversation.metadata.at("export");
      const auto original = std::find_if(imported.conversations.begin(), imported.conversations.end(), [&](const auto& item) {
        return item.metadata.at("export").at("key") == metadata.at("key");
      });
      REQUIRE(original != imported.conversations.end());
      CHECK(metadata.at("json_pointer") == original->metadata.at("export").at("json_pointer"));
      CHECK(metadata.at("source_index") == original->metadata.at("export").at("source_index"));
      CHECK(metadata.at("selector").at("json_pointer") == metadata.at("json_pointer"));
      const auto provenance = unwrap(copied->provenance().for_subject(conversation.id));
      REQUIRE(provenance.size() == 1);
      CHECK(provenance.front().locator.at("json_pointer") == metadata.at("json_pointer"));
    }
    CHECK(transport->requests().empty());
  }

  TEST_CASE("legacy missing ordinals remain unit relative and invalid ordinals are rejected") {
    fsutil::TempDir sources, data;
    const auto path = sources.path() / "legacy-catalog.zip";
    write_archive(path);
    auto transport = std::make_shared<net::ScriptedTransport>();
    auto rt = runtime(data.path(), transport);
    catalog::Catalog catalog(*rt, unwrap(rt->knowledge().pack()));
    unwrap(catalog.scan(scan_options(path)));
    const auto units = unwrap(catalog.query(catalog::UnitQuery{}));
    REQUIRE(units.size() == 2);
    for (const Json& invalid : {Json("1"), Json(1.5), Json(-1), Json(2147483648LL)}) {
      for (auto unit : units) {
        unit.unit.attrs["source_index"] = invalid;
        LOOM_REQUIRE_OK(rt->db().conn().run("UPDATE loom_cat_units SET body=? WHERE id=?",
          unit.to_json().dump(), unit.unit.id));
      }
      const auto rejected = catalog.import_selected(copy_options());
      REQUIRE_FALSE(rejected.has_value());
      CHECK(rejected.error().code == Errc::InvalidArgument);
      CHECK(unwrap(rt->db().list_convs(100)).empty());
    }
    for (auto unit : units) {
      unit.unit.attrs.erase("source_index");
      LOOM_REQUIRE_OK(rt->db().conn().run("UPDATE loom_cat_units SET body=? WHERE id=?",
        unit.to_json().dump(), unit.unit.id));
    }
    const auto imported = unwrap(catalog.import_selected(copy_options()));
    CHECK(imported["conversations"].size() == 2);
    for (const auto& conversation : unwrap(rt->db().list_convs(100))) {
      const auto& metadata = conversation.metadata.at("export");
      CHECK(metadata.at("mapping_index_scope") == "unit_relative");
      CHECK_FALSE(metadata.contains("source_index"));
      CHECK(metadata.at("index") == 0);
      const auto provenance = unwrap(rt->provenance().for_subject(conversation.id));
      REQUIRE(provenance.size() == 1);
      CHECK_FALSE(provenance.front().locator.contains("source_conversation_index"));
    }
    CHECK(transport->requests().empty());
  }

  TEST_CASE("copy failure rolls back projection and journal, retry preserves prior legacy rows") {
    fsutil::TempDir sources, data;
    const auto path = sources.path() / "synthetic.zip";
    write_archive(path);
    auto transport = std::make_shared<net::ScriptedTransport>();
    auto rt = runtime(data.path(), transport);
    catalog::Catalog catalog(*rt, unwrap(rt->knowledge().pack()));
    unwrap(catalog.scan(scan_options(path)));
    LOOM_REQUIRE_OK(rt->db().conn().exec("CREATE TRIGGER synthetic_provenance_failure BEFORE INSERT ON loom_provenance "
      "BEGIN SELECT RAISE(ABORT, 'synthetic interrupted provenance'); END"));
    const auto interrupted = catalog.import_selected(copy_options());
    REQUIRE_FALSE(interrupted.has_value());
    CHECK(unwrap(rt->db().list_convs(100)).empty());
    CHECK(unwrap(rt->db().conn().query_int("SELECT count(*) FROM loom_cat_imports")) == 0);
    LOOM_REQUIRE_OK(rt->db().conn().exec("DROP TRIGGER synthetic_provenance_failure"));
    const auto resumed = unwrap(catalog.import_selected(copy_options()));
    CHECK(resumed["conversations"].size() == 2);
    const auto initial = signatures(rt->db());
    const auto conversations = unwrap(rt->db().list_convs(100));
    REQUIRE_FALSE(conversations.empty());
    const auto legacy_id = conversations.front().id;
    // Model an already accepted old copy projection with owner annotations.
    // Retry must never silently rewrite its messages, metadata or identity.
    const auto messages = unwrap(rt->db().get_msgs(legacy_id, true));
    REQUIRE_FALSE(messages.empty());
    MsgPatch old_copy;
    old_copy.metadata = Json{{"catalog_unit", "legacy-unit"}, {"owner_annotation", "preserve"}};
    old_copy.parent_id = std::optional<std::string>{};
    LOOM_REQUIRE_OK(rt->db().update_msg(messages.back().id, old_copy));
    const auto old_message = unwrap(rt->db().get_msg(messages.back().id));
    REQUIRE(old_message.has_value());
    const auto retried = unwrap(catalog.import_selected(copy_options()));
    CHECK(retried["skipped"] == 2);
    CHECK(unwrap(rt->db().list_convs(100)).size() == 2);
    const auto preserved = unwrap(rt->db().get_msg(messages.back().id));
    REQUIRE(preserved.has_value());
    CHECK(preserved->to_json() == old_message->to_json());
    CHECK(initial.size() == 2);
    CHECK(transport->requests().empty());
  }
}
