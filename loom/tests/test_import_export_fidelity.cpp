// Independent regressions for source structure, locators and parser identity.
// W6's frozen fixtures/oracle/results remain untouched.
#include <doctest/doctest.h>

#include <map>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "../third_party/miniz/miniz.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
void write_zip(const std::filesystem::path& path, const std::map<std::string, std::string>& members) {
  mz_zip_archive archive{};
  REQUIRE(mz_zip_writer_init_file(&archive, path.string().c_str(), 0));
  for (const auto& [name, bytes] : members)
    REQUIRE(mz_zip_writer_add_mem(&archive, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION));
  REQUIRE(mz_zip_writer_finalize_archive(&archive));
  REQUIRE(mz_zip_writer_end(&archive));
}

struct FidelityFixture {
  fsutil::TempDir temporary;
  std::unique_ptr<Database> db = open_db(temporary.path() / "source.db");
  EventBus bus;
  BlobStore blobs{temporary.path() / "blobs", *db};
  ProvenanceStore provenance{*db};
  ConversationImporter importer{*db, bus, &blobs, &provenance};

  ImportResult run_import(const Json& source, const std::string& name = "source.json") {
    auto path = temporary.path() / name;
    LOOM_REQUIRE_OK(fsutil::write_file(path, source.dump()));
    ImportOptions options;
    options.export_mode = ExportMode::On;
    return unwrap(importer.import_file(path, options));
  }

  ImportResult run_zip(const std::map<std::string, Json>& members) {
    std::map<std::string, std::string> bytes;
    for (const auto& [name, source] : members) bytes[name] = source.dump();
    return run_zip_bytes(bytes);
  }

  ImportResult run_zip_bytes(const std::map<std::string, std::string>& members) {
    const auto path = temporary.path() / "source.zip";
    write_zip(path, members);
    ImportOptions options;
    options.export_mode = ExportMode::On;
    return unwrap(importer.import_file(path, options));
  }

  void check_locators(const Conversation& conversation, const Json& source, const ImportResult& imported,
                      const std::string& base, const std::string& member = "") {
    auto receipts = unwrap(provenance.for_subject(conversation.id));
    REQUIRE(receipts.size() == 1);
    CHECK(receipts[0].transform == "import.export@export-2");
    CHECK(receipts[0].locator["json_pointer"] == base);
    CHECK(receipts[0].locator["source"] == "sha256:" + imported.blob_hash);
    if (!member.empty()) CHECK(receipts[0].locator["member"] == member);
    CHECK(source.at(Json::json_pointer(base)).dump() ==
          source.at(Json::json_pointer(conversation.metadata["export"]["json_pointer"].get<std::string>())).dump());
    for (const auto& message : unwrap(db->get_msgs(conversation.id, true))) {
      receipts = unwrap(provenance.for_subject(message.id));
      REQUIRE(receipts.size() == 1);
      const auto& locator = receipts[0].locator;
      const auto& metadata = message.metadata["export"];
      const auto pointer = locator["json_pointer"].get<std::string>();
      CHECK(locator["json_path"] == pointer);
      CHECK(locator["source"] == "sha256:" + imported.blob_hash);
      CHECK(locator["source_conversation_index"] == conversation.metadata["export"]["source_index"]);
      if (!member.empty()) CHECK(locator["member"] == member);
      CHECK(metadata["json_pointer"] == pointer);
      CHECK(source.at(Json::json_pointer(pointer)).dump() == metadata["raw"].dump());
      if (metadata["provider"] == "anthropic") {
        CHECK(locator["message_index"] == metadata["source_index"]);
        CHECK(pointer == base + "/chat_messages/" + std::to_string(metadata["source_index"].get<int>()));
      } else {
        CHECK_FALSE(locator.contains("message_index"));
        CHECK(locator["source_key"] == "node/~key");
        CHECK(pointer == base + "/mapping/node~1~0key/message");
      }
    }
  }

  std::vector<Json> messages_by_insertion(const std::string& conversation) {
    auto lock = db->lock();
    auto statement = unwrap(db->conn().prepare("SELECT id,parent_id,metadata FROM messages WHERE conv_id=? ORDER BY rowid"));
    statement.bind(1, conversation);
    std::vector<Json> result;
    while (unwrap(statement.step())) {
      result.push_back(Json{{"id", statement.get_text(0)}, {"parent_id", statement.get_text(1)},
                            {"metadata", unwrap(json::parse(statement.get_text(2)))}});
    }
    return result;
  }
};

Json anthropic_conversation() {
  return Json{{"uuid", "conversation/~one"}, {"name", "Out of order source"},
    {"created_at", "2026-09-30T09:00:00Z"}, {"current_leaf_message_uuid", "child/~current"},
    {"unknown", Json{{"null", nullptr}, {"array", Json::array()}, {"object", Json::object()}, {"flag", false}}},
    {"chat_messages", Json::array({
      Json{{"uuid", "child/~current"}, {"parent_message_uuid", "parent"}, {"sender", "assistant"},
           {"text", "Current child"}, {"created_at", "2026-09-30T09:00:02Z"}},
      Json{{"uuid", "parent"}, {"parent_message_uuid", nullptr}, {"sender", "human"},
           {"text", "Parent"}, {"created_at", "2026-09-30T09:00:00Z"}},
      Json{{"uuid", "child/rejected"}, {"parent_message_uuid", "parent"}, {"sender", "assistant"},
           {"text", "Alternative"}, {"created_at", "2026-09-30T09:00:01Z"}}})}};
}

Json openai_conversation() {
  return Json{{"id", "conversation/~one"}, {"title", "Escaped mapping key"}, {"current_node", "node/~key"},
    {"mapping", Json{{"node/~key", Json{{"id", "node/~key"}, {"parent", nullptr}, {"children", Json::array()},
      {"message", Json{{"id", "different-message-id"}, {"author", Json{{"role", "user"}}},
        {"content", Json{{"content_type", "text"}, {"parts", Json::array({"Message"})}}}}}}}}};
}
}  // namespace

TEST_SUITE("import_export_fidelity") {
  TEST_CASE("Anthropic source array order survives while forward parent links and traversal remain explicit") {
    FidelityFixture fixture;
    const Json original = anthropic_conversation();
    auto result = fixture.run_import(Json::array({original}));
    REQUIRE(result.conversations.size() == 1);
    auto rows = fixture.messages_by_insertion(result.conversations[0].id);
    REQUIRE(rows.size() == 3);
    Json reconstructed = result.conversations[0].metadata["export"]["fields"];
    reconstructed["chat_messages"] = Json::array();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const Json& message = rows[index]["metadata"]["export"];
      CHECK(message["source_index"] == index);
      CHECK(message["raw"].dump() == original["chat_messages"][index].dump());
      reconstructed["chat_messages"].push_back(message["raw"]);
    }
    CHECK(reconstructed.dump() == original.dump());
    CHECK(rows[0]["parent_id"] == rows[1]["id"]);
    CHECK(rows[2]["parent_id"] == rows[1]["id"]);
    CHECK(rows[1]["parent_id"] == "");
    CHECK(rows[0]["metadata"]["export"]["traversal_index"] == 1);
    CHECK(rows[1]["metadata"]["export"]["traversal_index"] == 0);
    auto messages = unwrap(fixture.db->get_msgs(result.conversations[0].id, true));
    std::map<std::string, std::string> statuses;
    for (const auto& message : messages) statuses[message.metadata["export"]["key"].get<std::string>()] = message.status;
    CHECK(statuses["parent"] == "active");
    CHECK(statuses["child/~current"] == "active");
    CHECK(statuses["child/rejected"] == "version");
    CHECK(unwrap(fixture.blobs.read(result.blob_hash)) == Json::array({original}).dump());
  }

  TEST_CASE("corrected parser bypasses older source cache without deleting old records or bytes") {
    FidelityFixture fixture;
    const Json input = Json::array({anthropic_conversation()});
    const auto blob = unwrap(fixture.blobs.put(input.dump(), "application/json"));
    const auto old_conversation = unwrap(fixture.db->create_conv("Previous parser result"));
    SourceRecord old_source;
    old_source.kind = "file";
    old_source.format = "json";
    old_source.blob_hash = blob.hash;
    old_source.parser = "loom.importer.json.export";
    old_source.parser_version = "export-1";
    const auto old_source_id = unwrap(fixture.provenance.add_source(old_source));
    ProvenanceRecord old_receipt;
    old_receipt.subject_id = old_conversation.id;
    old_receipt.subject_kind = "conversation";
    old_receipt.source_id = old_source_id;
    old_receipt.transform = "import.export@export-1";
    const auto old_receipt_id = unwrap(fixture.provenance.add(old_receipt));
    auto imported = fixture.run_import(input);
    CHECK_FALSE(imported.already_imported);
    REQUIRE(imported.conversations.size() == 1);
    CHECK(imported.conversations[0].id != old_conversation.id);
    CHECK(imported.blob_hash == blob.hash);
    CHECK(unwrap(fixture.blobs.read(blob.hash)) == input.dump());
    REQUIRE(unwrap(fixture.db->get_conv(old_conversation.id)).has_value());
    CHECK(unwrap(fixture.db->get_conv(old_conversation.id))->title == "Previous parser result");
    CHECK(unwrap(fixture.provenance.for_subject(old_conversation.id))[0].id == old_receipt_id);
    CHECK(unwrap(fixture.provenance.find_sources_by_hash(blob.hash)).size() == 2);
    auto repeated = fixture.run_import(input);
    CHECK(repeated.already_imported);
    REQUIRE(repeated.conversations.size() == 1);
    CHECK(repeated.conversations[0].id == imported.conversations[0].id);
    CHECK(unwrap(fixture.db->list_convs(100)).size() == 2);
  }

  TEST_CASE("source pointers resolve against actual object array and wrapper roots for both providers") {
    for (const Json& conversation : {anthropic_conversation(), openai_conversation()}) {
      for (const std::string shape : {"object", "array", "wrapper"}) {
        CAPTURE(shape);
        FidelityFixture fixture;
        Json source = conversation;
        std::string base;
        if (shape == "array") {
          source = Json::array({conversation});
          base = "/0";
        } else if (shape == "wrapper") {
          source = Json{{"conversations", Json::array({conversation})},
                        {"unknown_wrapper", Json{{"null", nullptr}, {"empty_array", Json::array()},
                          {"empty_object", Json::object()}, {"flag", false}, {"float", 1.0}, {"integer", 1}}},
                        {"vendor_version", "future"}};
          base = "/conversations/0";
        }
        const auto imported = fixture.run_import(source);
        REQUIRE(imported.conversations.size() == 1);
        fixture.check_locators(imported.conversations[0], source, imported, base);
        CHECK(unwrap(fixture.blobs.read(imported.blob_hash)) == source.dump());
        if (shape == "wrapper") {
          Json reconstructed = imported.conversations[0].metadata["export"]["wrapper_fields"];
          reconstructed["conversations"] = Json::array({conversation});
          // JSON object key order is not structural identity; numeric types,
          // array order, empty containers and values still compare exactly.
          CHECK(json::canonical(reconstructed) == json::canonical(source));
        } else {
          CHECK_FALSE(imported.conversations[0].metadata["export"].contains("wrapper_fields"));
        }
      }
    }
  }

  TEST_CASE("ZIP member locators use member local source indices rather than global conversation order") {
    FidelityFixture fixture;
    const std::map<std::string, Json> members{
      {"conversations-001.json", Json::array({anthropic_conversation(), openai_conversation()})},
      {"conversations-002.json", Json::array({anthropic_conversation()})}};
    const auto imported = fixture.run_zip(members);
    REQUIRE(imported.conversations.size() == 3);
    for (std::size_t index = 0; index < imported.conversations.size(); ++index) {
      const std::string member = index < 2 ? "conversations-001.json" : "conversations-002.json";
      const std::size_t local_index = index < 2 ? index : 0;
      const auto& conversation = imported.conversations[index];
      CHECK(conversation.metadata["export"]["source_index"] == local_index);
      fixture.check_locators(conversation, members.at(member), imported, "/" + std::to_string(local_index), member);
    }
    const auto original = unwrap(fsutil::read_file(fixture.temporary.path() / "source.zip"));
    CHECK(unwrap(fixture.blobs.read(imported.blob_hash)) == original);
  }

  TEST_CASE("nested ZIP locator pairs innermost source hash with its local member name") {
    FidelityFixture fixture;
    const auto inner_path = fixture.temporary.path() / "inner.zip";
    const Json source = Json::array({anthropic_conversation()});
    write_zip(inner_path, {{"conversations.json", source.dump()}});
    const auto inner_bytes = unwrap(fsutil::read_file(inner_path));
    const auto imported = fixture.run_zip_bytes({{"nested/inner.zip", inner_bytes}});
    REQUIRE(imported.conversations.size() == 1);
    const auto receipts = unwrap(fixture.provenance.for_subject(imported.conversations[0].id));
    REQUIRE(receipts.size() == 1);
    const auto source_record = unwrap(fixture.provenance.get_source(receipts[0].source_id));
    REQUIRE(source_record.has_value());
    CHECK(source_record->blob_hash != imported.blob_hash);
    CHECK(unwrap(fixture.blobs.read(source_record->blob_hash)) == inner_bytes);
    CHECK(receipts[0].locator["zip_member"] == "nested/inner.zip!conversations.json");
    auto inner_result = imported;
    inner_result.blob_hash = source_record->blob_hash;
    fixture.check_locators(imported.conversations[0], source, inner_result, "/0", "conversations.json");
    CHECK(unwrap(fixture.blobs.read(imported.blob_hash)) ==
          unwrap(fsutil::read_file(fixture.temporary.path() / "source.zip")));
  }
}
