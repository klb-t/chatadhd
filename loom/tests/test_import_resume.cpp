#include <doctest/doctest.h>

#include <fstream>
#include <barrier>
#include <future>
#include <thread>
#include <map>
#include <stdexcept>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "../third_party/miniz/miniz.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
Json resume_conversation(int index) {
  return Json{{"uuid", "conversation-" + std::to_string(index)}, {"name", "Conversation " + std::to_string(index)},
    {"created_at", "2026-10-04T10:00:00Z"}, {"chat_messages", Json::array({
      Json{{"uuid", "child"}, {"parent_message_uuid", "parent"}, {"sender", "assistant"}, {"text", "Child \\\" {} [] ż"}},
      Json{{"uuid", "parent"}, {"parent_message_uuid", nullptr}, {"sender", "human"}, {"text", "Parent"}}})}};
}
Json resume_openai_conversation() {
  return Json{{"conversation_id", "openai-conversation"}, {"title", "Synthetic OpenAI conversation"},
    {"current_node", "message-node"}, {"mapping", Json{{"message-node", Json{{"id", "message-node"},
      {"parent", nullptr}, {"children", Json::array()}, {"message", Json{{"id", "message-id"},
        {"author", Json{{"role", "user"}}}, {"content", Json{{"content_type", "text"},
          {"parts", Json::array({"Synthetic OpenAI message"})}}}}}}}}}};
}
std::map<std::string, std::string> resume_openai_members() {
  return {{"conversations.json", Json::array({resume_openai_conversation()}).dump()},
    {"message_feedback.json", Json::array({Json{{"id", "feedback"}, {"conversation_id", "openai-conversation"},
      {"message_id", "message-id"}, {"rating", "good"}, {"content", "Synthetic feedback"}}}).dump()},
    {"shared_conversations.json", Json::array({Json{{"conversation_id", "openai-conversation"},
      {"title", "Synthetic shared link"}}}).dump()},
    {"textdocs/artifact.json", Json{{"conversation_id", "openai-conversation"}, {"name", "Synthetic artifact"},
      {"content", "Synthetic artifact content"}}.dump()}};
}
struct ResumeFixture {
  fsutil::TempDir temporary;
  std::unique_ptr<Database> db = open_db(temporary.path() / "source.db");
  EventBus bus;
  BlobStore blobs{temporary.path() / "blobs", *db};
  ProvenanceStore provenance{*db};
  ConversationImporter importer{*db, bus, &blobs, &provenance};
  ImportOptions options;
  ResumeFixture() { options.export_mode = ExportMode::On; options.resume = true; }
  std::filesystem::path write(const std::string& bytes, const std::string& name = "source.json") {
    auto path = temporary.path() / name;
    LOOM_REQUIRE_OK(fsutil::write_file(path, bytes)); return path;
  }
  std::int64_t count(const std::string& table) {
    auto lock = db->lock();
    return unwrap(db->conn().query_int("SELECT COUNT(*) FROM " + table)).value_or(0);
  }
};
void resume_zip(const std::filesystem::path& path, const std::map<std::string, std::string>& members) {
  mz_zip_archive archive{};
  REQUIRE(mz_zip_writer_init_file(&archive, path.string().c_str(), 0));
  for (const auto& [name, bytes] : members)
    REQUIRE(mz_zip_writer_add_mem(&archive, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION));
  REQUIRE(mz_zip_writer_finalize_archive(&archive)); REQUIRE(mz_zip_writer_end(&archive));
}
}  // namespace

TEST_SUITE("import_resume") {
  TEST_CASE("wrapper streams tiny chunks and preserves suffix fields and source order") {
    ResumeFixture fixture;
    fixture.options.json_read_chunk_bytes = 1;
    const Json conversations = Json::array({resume_conversation(0), resume_conversation(1)});
    const auto path = fixture.write("\xEF\xBB\xBF  {\"before\":{\"flag\":false},\"conversations\":" +
        conversations.dump() + ",\"suffix\":{\"escaped\":\"[]{}\\\"\",\"number\":9876543210}}");
    auto result = unwrap(fixture.importer.import_file(path, fixture.options));
    REQUIRE(result.conversations.size() == 2);
    CHECK_FALSE(result.export_report["partial"].get<bool>());
    CHECK(result.export_report["scanner_buffer_bytes"] == 1);
    CHECK(result.export_report["largest_json_value_bytes"].get<std::int64_t>() < 600);
    for (std::size_t i = 0; i < result.conversations.size(); ++i) {
      const auto& metadata = result.conversations[i].metadata["export"];
      CHECK(metadata["json_pointer"] == "/conversations/" + std::to_string(i));
      CHECK(metadata["wrapper_fields"]["suffix"]["number"] == 9876543210LL);
      CHECK(metadata["wrapper_fields"]["before"]["flag"] == false);
      const auto messages = unwrap(fixture.db->get_msgs(result.conversations[i].id, true));
      REQUIRE(messages.size() == 2);
      for (const auto& message : messages) CHECK(message.metadata["export"]["raw"].is_object());
    }
  }

  TEST_CASE("mixed prefixes do not hide later provider conversations in JSON or ZIP") {
    ResumeFixture fixture;
    const auto bytes = Json::array({Json::object(), resume_conversation(0)}).dump();
    const auto source = fixture.write(bytes);
    auto bare = unwrap(fixture.importer.import_file(source, fixture.options));
    REQUIRE(bare.conversations.size() == 1); CHECK(bare.export_report["partial"] == true);
    const auto archive = fixture.temporary.path() / "mixed.zip";
    resume_zip(archive, {{"conversations.json", bytes}});
    auto zipped = unwrap(fixture.importer.import_file(archive, fixture.options));
    REQUIRE(zipped.conversations.size() == 1); CHECK(zipped.export_report["provider"] == "anthropic");
    CHECK(zipped.export_report["partial"] == true);
  }

  TEST_CASE("truncated wrapper retains completed elements and marks partial") {
    ResumeFixture fixture;
    const auto source = fixture.write("{\"before\":true,\"conversations\":[" + resume_conversation(0).dump() + ",{\"uuid\":");
    auto result = unwrap(fixture.importer.import_file(source, fixture.options));
    REQUIRE(result.conversations.size() == 1); CHECK(result.export_report["partial"] == true);
    CHECK(result.conversations[0].metadata["export"]["wrapper_fields"]["before"] == true);
  }

  TEST_CASE("summary results omit metadata projection while database and cache remain lossless") {
    ResumeFixture fixture;
    fixture.options.include_result_metadata = false;
    auto original = resume_conversation(0); original["opaque"] = std::string(100'000, 'x');
    const auto source = fixture.write(Json::array({original}).dump());
    auto first = unwrap(fixture.importer.import_file(source, fixture.options));
    REQUIRE(first.conversations.size() == 1); CHECK(first.conversations[0].metadata.empty());
    CHECK(first.export_report["include_result_metadata"] == false);
    const auto stored = unwrap(fixture.db->get_conv(first.conversations[0].id));
    REQUIRE(stored); CHECK(stored->metadata["export"]["fields"]["opaque"] == original["opaque"]);
    auto cached = unwrap(fixture.importer.import_file(source, fixture.options));
    CHECK(cached.already_imported); CHECK(cached.conversations[0].metadata.empty());
    fixture.options.include_result_metadata = true;
    auto full = unwrap(fixture.importer.import_file(source, fixture.options));
    CHECK(full.already_imported); CHECK(full.conversations[0].metadata["export"]["fields"]["opaque"] == original["opaque"]);
    CHECK(full.export_report["include_result_metadata"] == true);
  }

  TEST_CASE("failed unknown-member write is partial and retried without a false success marker") {
    ResumeFixture fixture;
    const auto archive = fixture.temporary.path() / "unknown.zip";
    resume_zip(archive, {{"conversations.json", Json::array({resume_conversation(0)}).dump()}, {"opaque.json", "{\"unknown\":true}"}});
    LOOM_REQUIRE_OK(fixture.db->conn().exec("CREATE TRIGGER fail_member BEFORE INSERT ON nodes "
      "BEGIN SELECT RAISE(FAIL,'injected entity failure'); END;"));
    auto failed = unwrap(fixture.importer.import_file(archive, fixture.options));
    CHECK(failed.export_report["partial"] == true); CHECK(fixture.count("nodes") == 0);
    CHECK(fixture.count("loom_import_checkpoints") == 1);
    LOOM_REQUIRE_OK(fixture.db->conn().exec("DROP TRIGGER fail_member"));
    auto repaired = unwrap(fixture.importer.import_file(archive, fixture.options));
    CHECK(repaired.resumed); CHECK(fixture.count("nodes") == 1); CHECK(fixture.count("conversations") == 1);
    CHECK(fixture.count("loom_import_checkpoints") == 2); CHECK(repaired.export_report["partial"] == false);
  }

  TEST_CASE("failed project interpretation retains raw bytes without suppressing a valid retry") {
    ResumeFixture fixture;
    const auto archive = fixture.temporary.path() / "projects.zip";
    const Json project{{"uuid", "project"}, {"name", "A project"}, {"docs", Json::array({
      Json{{"uuid", "document"}, {"filename", "Synthetic.txt"}, {"content", "Synthetic document"}}})}};
    resume_zip(archive, {{"conversations.json", Json::array({resume_conversation(0)}).dump()},
                        {"memories.json", Json{{"project_memories", Json{{"project", "Synthetic memory"}}}}.dump()},
                        {"projects.json", Json::array({project}).dump()}});
    LOOM_REQUIRE_OK(fixture.db->conn().exec("CREATE TRIGGER fail_project_link BEFORE INSERT ON links "
      "WHEN NEW.link_type='part_of' BEGIN SELECT RAISE(FAIL,'injected project link failure'); END;"));
    const auto failed = unwrap(fixture.importer.import_file(archive, fixture.options));
    REQUIRE(failed.conversations.size() == 1);
    CHECK(failed.export_report["partial"] == true);
    CHECK(failed.export_report["counts"]["project"] == 0);
    CHECK(failed.export_report["counts"]["project_doc"] == 0);
    CHECK(failed.export_report["counts"]["memory"] == 0);
    CHECK(fixture.count("nodes") == 2); // only independently retained raw members
    CHECK(fixture.count("links") == 0);
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM loom_import_checkpoints "
      "WHERE member='projects.json' AND source_index=-1")).value_or(-1) == 0);
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM loom_import_checkpoints "
      "WHERE member='projects.json' AND source_index=-2")).value_or(-1) == 1);
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM loom_import_checkpoints "
      "WHERE member='memories.json' AND source_index=-1")).value_or(-1) == 0);
    LOOM_REQUIRE_OK(fixture.db->conn().exec("DROP TRIGGER fail_project_link"));
    const auto repaired = unwrap(fixture.importer.import_file(archive, fixture.options));
    REQUIRE(repaired.conversations.size() == 1);
    CHECK(repaired.resumed); CHECK_FALSE(repaired.already_imported);
    CHECK(repaired.source_id == failed.source_id);
    CHECK(repaired.conversations[0].id == failed.conversations[0].id);
    CHECK(repaired.export_report["partial"] == false);
    CHECK(repaired.export_report["counts"]["project"] == 1);
    CHECK(repaired.export_report["counts"]["project_doc"] == 1);
    CHECK(repaired.export_report["counts"]["memory"] == 1);
    CHECK(fixture.count("conversations") == 1); CHECK(fixture.count("nodes") == 5);
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM links l "
      "JOIN nodes d ON d.id=l.src JOIN nodes p ON p.id=l.dst "
      "WHERE l.link_type='part_of' AND d.kind='export:project_doc' AND p.kind='export:project'"))
      .value_or(-1) == 1);
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM links l "
      "JOIN nodes m ON m.id=l.src JOIN nodes p ON p.id=l.dst "
      "WHERE l.link_type='part_of' AND m.kind='export:memory' AND p.kind='export:project'"))
      .value_or(-1) == 1);
    const auto project_id = unwrap(fixture.db->conn().query_text("SELECT id FROM nodes WHERE kind='export:project'"));
    const auto document_id = unwrap(fixture.db->conn().query_text("SELECT id FROM nodes WHERE kind='export:project_doc'"));
    REQUIRE(project_id); REQUIRE(document_id);
    const auto cached = unwrap(fixture.importer.import_file(archive, fixture.options));
    CHECK(cached.already_imported); CHECK(cached.export_report["partial"] == false);
    CHECK(cached.conversations[0].id == failed.conversations[0].id);
    CHECK(unwrap(fixture.db->conn().query_text("SELECT id FROM nodes WHERE kind='export:project'")) == project_id);
    CHECK(unwrap(fixture.db->conn().query_text("SELECT id FROM nodes WHERE kind='export:project_doc'")) == document_id);
    CHECK(fixture.count("nodes") == 5); CHECK(fixture.count("links") == 2);
  }

  TEST_CASE("legacy journal repair preserves source-owned project memory IDs behind a completed cache") {
    ResumeFixture fixture;
    const auto archive = fixture.temporary.path() / "legacy-projects.zip";
    const Json project{{"uuid", "project"}, {"name", "A project"}, {"docs", Json::array({
      Json{{"uuid", "document"}, {"filename", "Synthetic.txt"}, {"content", "Synthetic document"}}})}};
    resume_zip(archive, {{"conversations.json", Json::array({resume_conversation(0)}).dump()},
                        {"memories.json", Json{{"project_memories", Json{{"project", "Synthetic memory"}}}}.dump()},
                        {"projects.json", Json::array({project}).dump()}});
    LOOM_REQUIRE_OK(fixture.db->conn().exec("CREATE TRIGGER fail_project_link BEFORE INSERT ON links "
      "WHEN NEW.link_type='part_of' BEGIN SELECT RAISE(FAIL,'injected project link failure'); END;"));
    const auto failed = unwrap(fixture.importer.import_file(archive, fixture.options));
    REQUIRE(failed.conversations.size() == 1); CHECK(failed.export_report["partial"] == true);
    const auto raw_id = unwrap(fixture.db->conn().query_text(
      "SELECT id FROM nodes WHERE kind='export:member' AND label='projects.json'"));
    REQUIRE(raw_id);
    // Reproduce the original writer's shared-phase marker and false completed
    // cache using its real raw node/report, without changing source identities.
    LOOM_REQUIRE_OK(fixture.db->conn().exec("DROP TRIGGER fail_project_link; "
      "UPDATE loom_import_checkpoints SET source_index=-1,metadata=json_set(metadata,'$.kind','member') "
      "WHERE member='projects.json' AND source_index=-2;"));
    auto memory_checkpoint = unwrap(fixture.db->conn().query_text(
      "SELECT metadata FROM loom_import_checkpoints WHERE member='memories.json' AND source_index=-2"));
    REQUIRE(memory_checkpoint);
    auto memory_metadata = unwrap(json::parse(*memory_checkpoint));
    memory_metadata["kind"] = "member";
    memory_metadata["report"]["unknown_members"] = Json::array();
    memory_metadata["report"]["counts"]["memory"] = 1;
    LOOM_REQUIRE_OK(fixture.db->conn().run("UPDATE loom_import_checkpoints SET source_index=-1,metadata=? "
      "WHERE member='memories.json' AND source_index=-2", json::py_dumps(memory_metadata)));
    LOOM_REQUIRE_OK(fixture.db->conn().exec("DELETE FROM nodes WHERE kind='export:member' AND label='memories.json'"));
    NodeOptions memory_options;
    memory_options.content = "Synthetic memory";
    memory_options.metadata = Json{{"export", Json{{"provider", "anthropic"}, {"member", "memories.json"},
      {"source_id", failed.source_id},
      {"record", Json{{"scope", "project"}, {"project_uuid", "project"}, {"container_fields", Json::object()}}}}}};
    const auto memory_id = unwrap(fixture.db->create_node("project memory project", "export:memory", memory_options));
    auto source = unwrap(fixture.provenance.get_source(failed.source_id));
    REQUIRE(source);
    source->metadata["import_status"] = "complete";
    source->metadata["export_report"]["partial"] = false;
    source->metadata["export_report"]["errors"] = Json::array();
    source->metadata["export_report"]["counts"]["memory"] = 1;
    LOOM_REQUIRE_OK(fixture.db->conn().run("UPDATE loom_sources SET metadata=? WHERE id=?",
                                         json::py_dumps(source->metadata), source->id));

    auto reopened = open_db(fixture.temporary.path() / "source.db");
    EventBus bus;
    BlobStore blobs{fixture.temporary.path() / "blobs", *reopened};
    ProvenanceStore provenance{*reopened};
    ConversationImporter importer{*reopened, bus, &blobs, &provenance};
    const auto repaired = unwrap(importer.import_file(archive, fixture.options));
    CHECK_FALSE(repaired.already_imported); CHECK(repaired.resumed);
    REQUIRE(repaired.conversations.size() == 1);
    CHECK(repaired.source_id == failed.source_id);
    CHECK(repaired.conversations[0].id == failed.conversations[0].id);
    CHECK(repaired.export_report["partial"] == false);
    CHECK(repaired.export_report["counts"]["project"] == 1);
    CHECK(repaired.export_report["counts"]["project_doc"] == 1);
    CHECK(repaired.export_report["counts"]["memory"] == 1);
    CHECK(fixture.count("nodes") == 4); CHECK(fixture.count("links") == 2);
    CHECK(unwrap(reopened->conn().query_text("SELECT id FROM nodes WHERE kind='export:member'")) == raw_id);
    CHECK(unwrap(reopened->conn().query_text("SELECT id FROM nodes WHERE kind='export:memory'")) == memory_id);
    CHECK(unwrap(reopened->conn().query_int("SELECT COUNT(*) FROM links l JOIN nodes p ON p.id=l.dst "
      "WHERE l.src=? AND l.link_type='part_of' AND p.kind='export:project'", memory_id)).value_or(-1) == 1);
    CHECK(unwrap(reopened->conn().query_int("SELECT COUNT(*) FROM loom_import_checkpoints "
      "WHERE member='projects.json' AND source_index=-2 AND json_extract(metadata,'$.kind')='raw_member'"))
      .value_or(-1) == 1);
    CHECK(unwrap(reopened->conn().query_int("SELECT COUNT(*) FROM loom_import_checkpoints "
      "WHERE member='projects.json' AND source_index=-1 "
      "AND json_array_length(metadata,'$.report.unknown_members')=0")).value_or(-1) == 1);
    source = unwrap(provenance.get_source(repaired.source_id));
    REQUIRE(source); CHECK(source->metadata["import_status"] == "complete");
    const auto cached = unwrap(importer.import_file(archive, fixture.options));
    CHECK(cached.already_imported); CHECK(cached.export_report["partial"] == false);
    CHECK(cached.conversations[0].id == failed.conversations[0].id);
    CHECK(fixture.count("nodes") == 4); CHECK(fixture.count("links") == 2);
  }

  TEST_CASE("malformed known provider member remains partial across raw-retention replay") {
    ResumeFixture fixture;
    bool nested = false;
    SUBCASE("direct provider archive") {}
    SUBCASE("provider archive inside a container") { nested = true; }
    auto archive = fixture.temporary.path() / "malformed-projects.zip";
    resume_zip(archive, {{"conversations.json", Json::array({resume_conversation(0)}).dump()},
                        {"projects.json", "[{\"uuid\":\"unfinished\""}});
    if (nested) {
      const auto container = fixture.temporary.path() / "malformed-container.zip";
      resume_zip(container, {{"nested.zip", unwrap(fsutil::read_file(archive))}});
      archive = container;
    }
    std::string source_id, conversation_id;
    for (int attempt = 0; attempt < 3; ++attempt) {
      const auto result = unwrap(fixture.importer.import_file(archive, fixture.options));
      const auto& provider_report = nested ? result.export_report["parts"][0]["report"] : result.export_report;
      REQUIRE(result.conversations.size() == 1);
      CHECK_FALSE(result.already_imported); CHECK(result.export_report["partial"] == true);
      CHECK_FALSE(provider_report["errors"].empty());
      if (attempt == 0) { source_id = result.source_id; conversation_id = result.conversations[0].id; }
      else {
        CHECK(result.resumed); CHECK(result.source_id == source_id);
        CHECK(result.conversations[0].id == conversation_id);
        CHECK(provider_report["resumed_members"] == 1);
      }
      const auto source = unwrap(fixture.provenance.get_source(result.source_id));
      REQUIRE(source); CHECK(source->metadata["import_status"] == "partial");
      CHECK(fixture.count("nodes") == 1); CHECK(fixture.count("links") == 0);
      CHECK(fixture.count("conversations") == 1); CHECK(fixture.count("loom_import_checkpoints") == 2);
      CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM loom_import_checkpoints "
        "WHERE member='projects.json' AND source_index=-1")).value_or(-1) == 0);
    }
  }

  TEST_CASE("OpenAI auxiliary bindings remain pending after a transient conversation failure") {
    ResumeFixture fixture;
    const auto archive = fixture.temporary.path() / "openai-pending.zip";
    resume_zip(archive, resume_openai_members());
    LOOM_REQUIRE_OK(fixture.db->conn().exec("CREATE TRIGGER fail_imported_message BEFORE INSERT ON messages "
      "BEGIN SELECT RAISE(FAIL,'injected conversation message failure'); END;"));
    const auto failed = unwrap(fixture.importer.import_file(archive, fixture.options));
    CHECK(failed.export_report["partial"] == true); CHECK(failed.conversations.empty());
    CHECK(fixture.count("conversations") == 0); CHECK(fixture.count("messages") == 0);
    CHECK(fixture.count("nodes") == 3); CHECK(fixture.count("links") == 0);
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM loom_import_checkpoints WHERE source_index=-1"))
      .value_or(-1) == 0);
    LOOM_REQUIRE_OK(fixture.db->conn().exec("DROP TRIGGER fail_imported_message"));
    const auto repaired = unwrap(fixture.importer.import_file(archive, fixture.options));
    CHECK(repaired.resumed); CHECK(repaired.source_id == failed.source_id);
    REQUIRE(repaired.conversations.size() == 1); CHECK(repaired.messages == 1);
    CHECK(repaired.export_report["partial"] == false);
    CHECK(repaired.export_report["counts"]["feedback"] == 1);
    CHECK(repaired.export_report["counts"]["shared_link"] == 1);
    CHECK(repaired.export_report["counts"]["artifact"] == 1);
    CHECK(fixture.count("nodes") == 6); CHECK(fixture.count("links") == 3);
    const auto cached = unwrap(fixture.importer.import_file(archive, fixture.options));
    CHECK(cached.already_imported); CHECK(cached.conversations[0].id == repaired.conversations[0].id);
    CHECK(fixture.count("nodes") == 6); CHECK(fixture.count("links") == 3);
  }

  TEST_CASE("legacy OpenAI journals repair source-owned references while preserving node IDs") {
    ResumeFixture fixture;
    bool nested = false;
    SUBCASE("direct provider archive") {}
    SUBCASE("provider archive inside a completed container") { nested = true; }
    auto archive = fixture.temporary.path() / "openai-legacy-bindings.zip";
    auto members = resume_openai_members();
    auto feedback = unwrap(json::parse(members["message_feedback.json"]));
    feedback.push_back(Json{{"id", "external-message"}, {"conversation_id", "openai-conversation"},
      {"message_id", "message-outside-export"}, {"content", "External message reference"}});
    feedback.push_back(Json{{"id", "external-conversation"}, {"conversation_id", "conversation-outside-export"},
      {"message_id", "external"}, {"content", "External conversation reference"}});
    members["message_feedback.json"] = feedback.dump();
    resume_zip(archive, members);
    if (nested) {
      const auto container = fixture.temporary.path() / "container.zip";
      resume_zip(container, {{"nested.zip", unwrap(fsutil::read_file(archive))}});
      archive = container;
    }
    const auto original = unwrap(fixture.importer.import_file(archive, fixture.options));
    REQUIRE(original.conversations.size() == 1); CHECK(fixture.count("nodes") == 5); CHECK(fixture.count("links") == 3);
    const auto& original_report = nested ? original.export_report["parts"][0]["report"] : original.export_report;
    CHECK(original_report["warnings"].size() == 2);
    std::map<std::string, std::string> node_ids;
    for (const auto* kind : {"export:feedback", "export:shared_link", "export:artifact"}) {
      const auto id = unwrap(fixture.db->conn().query_text("SELECT id FROM nodes WHERE kind=?", kind));
      REQUIRE(id); node_ids[kind] = *id;
    }
    LOOM_REQUIRE_OK(fixture.db->conn().exec("DELETE FROM links; "
      "UPDATE loom_import_checkpoints SET metadata=json_remove(metadata,'$.binding_version') WHERE source_index=-1;"));
    auto reopened = open_db(fixture.temporary.path() / "source.db");
    EventBus bus; BlobStore blobs{fixture.temporary.path() / "blobs", *reopened};
    ProvenanceStore provenance{*reopened}; ConversationImporter importer{*reopened, bus, &blobs, &provenance};
    const auto repaired = unwrap(importer.import_file(archive, fixture.options));
    CHECK_FALSE(repaired.already_imported); CHECK(repaired.resumed);
    CHECK(repaired.source_id == original.source_id);
    REQUIRE(repaired.conversations.size() == 1); CHECK(repaired.conversations[0].id == original.conversations[0].id);
    CHECK(repaired.export_report["partial"] == false);
    const auto& repaired_report = nested ? repaired.export_report["parts"][0]["report"] : repaired.export_report;
    CHECK(repaired_report["warnings"].size() == 2);
    CHECK(fixture.count("nodes") == 5); CHECK(fixture.count("links") == 3);
    for (const auto& [kind, id] : node_ids)
      CHECK(unwrap(reopened->conn().query_text("SELECT id FROM nodes WHERE kind=?", kind)) == id);
    CHECK(unwrap(reopened->conn().query_int("SELECT COUNT(*) FROM loom_import_checkpoints "
      "WHERE source_index=-1 AND json_extract(metadata,'$.binding_version')=1")).value_or(-1) == 3);
    const auto cached = unwrap(importer.import_file(archive, fixture.options));
    CHECK(cached.already_imported); CHECK(cached.conversations[0].id == original.conversations[0].id);
    CHECK(fixture.count("nodes") == 5); CHECK(fixture.count("links") == 3);
  }

  TEST_CASE("uncertified OpenAI journal cannot retarget an unowned or foreign-source lookalike") {
    ResumeFixture fixture;
    bool foreign = false;
    SUBCASE("historical node has no source ownership") {}
    SUBCASE("identical node belongs to another imported source") { foreign = true; }
    const auto archive = fixture.temporary.path() / "ownership.zip";
    resume_zip(archive, resume_openai_members());
    const auto original = unwrap(fixture.importer.import_file(archive, fixture.options));
    const auto retained = unwrap(fixture.db->conn().query_text("SELECT id FROM nodes WHERE kind='export:feedback'"));
    REQUIRE(retained);
    if (foreign) {
      const auto other = unwrap(fixture.importer.import_file(
        fixture.write(Json::array({resume_conversation(9)}).dump(), "other-source.json"), fixture.options));
      REQUIRE(other.source_id != original.source_id);
      LOOM_REQUIRE_OK(fixture.db->conn().run("UPDATE nodes SET metadata=json_set(metadata,'$.export.source_id',?) "
        "WHERE id=?", other.source_id, *retained));
    } else {
      LOOM_REQUIRE_OK(fixture.db->conn().run("UPDATE nodes SET metadata=json_remove(metadata,'$.export.source_id') "
        "WHERE id=?", *retained));
    }
    LOOM_REQUIRE_OK(fixture.db->conn().exec("DELETE FROM links; "
      "UPDATE loom_import_checkpoints SET metadata=json_remove(metadata,'$.binding_version') WHERE source_index=-1;"));
    for (int attempt = 0; attempt < 2; ++attempt) {
      const auto pending = unwrap(fixture.importer.import_file(archive, fixture.options));
      CHECK_FALSE(pending.already_imported); CHECK(pending.resumed);
      CHECK(pending.export_report["partial"] == true); CHECK_FALSE(pending.export_report["errors"].empty());
      CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM links WHERE src=?", *retained)).value_or(-1) == 0);
      CHECK(unwrap(fixture.db->conn().query_text("SELECT id FROM nodes WHERE kind='export:feedback'")) == retained);
      const auto source = unwrap(fixture.provenance.get_source(original.source_id));
      REQUIRE(source); CHECK(source->metadata["import_status"] == "partial");
      CHECK(fixture.count("nodes") == 4); // preserved three records and one raw fallback, never replacements
    }
  }

  TEST_CASE("cancelled provider import resumes committed IDs and completed source cache") {
    ResumeFixture fixture;
    const auto path = fixture.write(Json::array({resume_conversation(0), resume_conversation(1), resume_conversation(2)}).dump());
    CancelToken cancel;
    fixture.options.cancel = &cancel;
    fixture.options.progress = [&](auto, auto, std::string_view status) { if (status == "export") cancel.cancel(); };
    const auto first = unwrap(fixture.importer.import_file(path, fixture.options));
    REQUIRE(first.conversations.size() == 1);
    CHECK(first.cancelled);
    CHECK(fixture.count("conversations") == 1);
    fixture.options.cancel = nullptr; fixture.options.progress = nullptr;
    const auto resumed = unwrap(fixture.importer.import_file(path, fixture.options));
    REQUIRE(resumed.conversations.size() == 3);
    CHECK(resumed.resumed);
    CHECK(resumed.source_id == first.source_id);
    CHECK(resumed.conversations[0].id == first.conversations[0].id);
    CHECK(resumed.export_report["resumed_conversations"] == 1);
    CHECK(resumed.export_report["counts"]["conversation"] == 3);
    CHECK(resumed.messages == 6);
    CHECK(fixture.count("conversations") == 3);
    CHECK(fixture.count("messages") == 6);
    CHECK(fixture.count("loom_import_checkpoints") == 3);
    CHECK(fixture.count("loom_sources") == 1);
    const auto cached = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(cached.already_imported); CHECK(cached.conversations[0].id == first.conversations[0].id);
  }

  TEST_CASE("admission binds snapshot hash and rejects size changes before source rows") {
    ResumeFixture fixture;
    const auto bytes = Json::array({resume_conversation(0)}).dump();
    const auto path = fixture.write(bytes);
    fixture.options.expected_source_hash = Sha256::hex(bytes);
    fixture.options.expected_source_bytes = static_cast<std::int64_t>(bytes.size());
    fixture.options.preflight = [&](const auto&) -> Status { return fsutil::write_file(path, bytes + " "); };
    auto changed_size = fixture.importer.import_file(path, fixture.options);
    CHECK_FALSE(changed_size); CHECK(fixture.count("conversations") == 0); CHECK(fixture.count("loom_sources") == 0);
    CHECK(fixture.count("loom_blobs") == 0);
    auto same_size = bytes; const auto position = same_size.find("Parent"); REQUIRE(position != std::string::npos);
    same_size.replace(position, 6, "Edited");
    fixture.options.preflight = [&](const auto&) -> Status { return fsutil::write_file(path, same_size); };
    auto changed_hash = fixture.importer.import_file(path, fixture.options);
    CHECK_FALSE(changed_hash); CHECK(fixture.count("conversations") == 0); CHECK(fixture.count("loom_sources") == 0);
    CHECK(fixture.count("loom_import_checkpoints") == 0);
    fixture.options.record_provenance = false;
    CHECK_FALSE(fixture.importer.import_file(path, fixture.options));
    CHECK(fixture.count("conversations") == 0);
  }

  TEST_CASE("in-place source mutation after first commit cannot change the admitted snapshot") {
    ResumeFixture fixture;
    const Json original = Json::array({resume_conversation(0), resume_conversation(1)});
    Json changed = original;
    changed[1]["chat_messages"][1]["text"] = "Mutant";
    const auto original_bytes = original.dump();
    const auto changed_bytes = changed.dump();
    REQUIRE(changed_bytes.size() == original_bytes.size());
    const auto path = fixture.write(original_bytes);
    fixture.options.json_read_chunk_bytes = 1; // later bytes must be fetched after the callback
    fixture.options.expected_source_hash = Sha256::hex(original_bytes);
    fixture.options.expected_source_bytes = static_cast<std::int64_t>(original_bytes.size());
    bool mutated = false;
    fixture.options.progress = [&](auto, auto, std::string_view status) {
      if (status != "export" || mutated) return;
      // Rewrite the existing inode so an importer reading the mutable source
      // with a tiny buffer would observe changed bytes on its next refill.
      std::fstream writable(path, std::ios::in | std::ios::out | std::ios::binary);
      REQUIRE(writable.is_open());
      writable.write(changed_bytes.data(), static_cast<std::streamsize>(changed_bytes.size()));
      writable.flush(); REQUIRE(writable.good()); mutated = true;
    };
    const auto result = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(mutated); REQUIRE(result.conversations.size() == 2);
    CHECK_FALSE(result.export_report["partial"].get<bool>());
    CHECK(result.blob_hash == Sha256::hex(original_bytes));
    CHECK(unwrap(fsutil::read_file(path)) == changed_bytes);
    CHECK(unwrap(fixture.blobs.read(result.blob_hash)) == original_bytes);
    const auto source = unwrap(fixture.provenance.get_source(result.source_id));
    REQUIRE(source); CHECK(source->blob_hash == result.blob_hash); CHECK(source->uri == path.string());
    for (std::size_t conversation_index = 0; conversation_index < result.conversations.size(); ++conversation_index) {
      const auto stored = unwrap(fixture.db->get_conv(result.conversations[conversation_index].id));
      REQUIRE(stored);
      CHECK(stored->metadata["export"]["fields"]["uuid"] == original[conversation_index]["uuid"]);
      const auto messages = unwrap(fixture.db->get_msgs(stored->id, true));
      REQUIRE(messages.size() == 2);
      for (const auto& message : messages) {
        const auto& metadata = message.metadata["export"];
        const auto source_index = metadata["source_index"].get<std::size_t>();
        CHECK(metadata["raw"] == original[conversation_index]["chat_messages"][source_index]);
        const auto receipts = unwrap(fixture.provenance.for_subject(message.id));
        REQUIRE(receipts.size() == 1); CHECK(receipts[0].locator["source"] == "sha256:" + result.blob_hash);
        if (metadata["raw"]["uuid"] == "parent") CHECK(message.text == "Parent");
      }
    }
  }

  TEST_CASE("live SQLite imports preserve committed WAL rows while raw main capture stays explicit") {
    ResumeFixture source_fixture;
    const auto source_path = source_fixture.temporary.path() / "live-archive.db";
    auto source = unwrap(sql::Connection::open(source_path));
    LOOM_REQUIRE_OK(source.exec("PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint=0; "
                               "CREATE TABLE archive_messages(role TEXT,content TEXT);"));
    LOOM_REQUIRE_OK(source.exec("PRAGMA wal_checkpoint(TRUNCATE)"));
    const auto main_hash = unwrap(sha256_file_hex(source_path));
    LOOM_REQUIRE_OK(source.run("INSERT INTO archive_messages(role,content) VALUES(?,?)",
                              "user", "Committed only in the live WAL"));
    CHECK(unwrap(sha256_file_hex(source_path)) == main_hash);
    REQUIRE(std::filesystem::file_size(source_path.string() + "-wal") > 0);

    // Both public routes use fresh destination runtimes so existing main-file
    // hash caching cannot mask which source connection they actually read.
    ResumeFixture file_fixture;
    auto imported = unwrap(file_fixture.importer.import_file(source_path, file_fixture.options));
    REQUIRE(imported.conversations.size() == 1); CHECK(imported.messages == 1);
    REQUIRE(imported.warnings.size() == 1);
    CHECK(imported.warnings[0].find("excludes WAL bytes") != std::string::npos);
    CHECK(imported.blob_hash == main_hash);
    CHECK(unwrap(file_fixture.blobs.read(imported.blob_hash)) == unwrap(fsutil::read_file(source_path)));
    const auto file_messages = unwrap(file_fixture.db->get_msgs(imported.conversations[0].id, true));
    REQUIRE(file_messages.size() == 1); CHECK(file_messages[0].text == "Committed only in the live WAL");
    const auto source_receipt = unwrap(file_fixture.provenance.get_source(imported.source_id));
    REQUIRE(source_receipt); CHECK(source_receipt->blob_hash == main_hash); CHECK(source_receipt->uri == source_path.string());

    ResumeFixture direct_fixture;
    const auto direct = unwrap(direct_fixture.importer.import_sqlite(source_path, direct_fixture.options));
    REQUIRE(direct.size() == 1);
    const auto direct_messages = unwrap(direct_fixture.db->get_msgs(direct[0].id, true));
    REQUIRE(direct_messages.size() == 1); CHECK(direct_messages[0].text == "Committed only in the live WAL");
    CHECK(unwrap(direct_fixture.blobs.read(main_hash)) == unwrap(fsutil::read_file(source_path)));
    const auto direct_sources = unwrap(direct_fixture.provenance.find_sources_by_hash(main_hash));
    REQUIRE(direct_sources.size() == 1); CHECK(direct_sources[0].uri == source_path.string());
    CHECK(unwrap(sha256_file_hex(source_path)) == main_hash);
  }

  TEST_CASE("immutable text Markdown HTML and MHT reads retain original fallback titles in both routes") {
    using Handler = Result<std::vector<Conversation>> (ConversationImporter::*)(
        const std::filesystem::path&, const ImportOptions&);
    struct Example { const char* extension; const char* bytes; Handler handler; };
    const Example examples[] = {
      {"txt", "A plain text note without speaker labels.\n", &ConversationImporter::import_text},
      {"md", "A plain Markdown note without a heading.\n", &ConversationImporter::import_markdown},
      {"html", "<html><body>A plain HTML note without a title.</body></html>", &ConversationImporter::import_html},
      {"mht", "MIME-Version: 1.0\nContent-Type: multipart/related\n\n------=_example\n"
              "Content-Type: text/html\n\n<html><body>A plain MHT note.</body></html>\n------=_example--",
              &ConversationImporter::import_mht}
    };
    for (const auto& example : examples) {
      CAPTURE(example.extension);
      ResumeFixture file_fixture;
      const auto path = file_fixture.write(example.bytes, std::string("Original.filename.") + example.extension);
      const auto file_result = unwrap(file_fixture.importer.import_file(path, file_fixture.options));
      REQUIRE(file_result.conversations.size() == 1);
      CHECK(file_result.conversations[0].title == "[Import] Original.filename");
      CHECK(file_result.blob_hash == Sha256::hex(example.bytes));
      CHECK(file_result.conversations[0].title.find(file_result.blob_hash) == std::string::npos);
      CHECK(unwrap(file_fixture.blobs.read(file_result.blob_hash)) == example.bytes);
      const auto file_messages = unwrap(file_fixture.db->get_msgs(file_result.conversations[0].id, true));
      REQUIRE(file_messages.size() == 1); CHECK_FALSE(file_messages[0].text.empty());

      ResumeFixture direct_fixture;
      const auto direct_result = unwrap((direct_fixture.importer.*example.handler)(path, direct_fixture.options));
      REQUIRE(direct_result.size() == 1); CHECK(direct_result[0].title == "[Import] Original.filename");
      CHECK(unwrap(direct_fixture.blobs.read(file_result.blob_hash)) == example.bytes);
      const auto direct_messages = unwrap(direct_fixture.db->get_msgs(direct_result[0].id, true));
      REQUIRE(direct_messages.size() == 1); CHECK(direct_messages[0].text == file_messages[0].text);
    }
  }

  TEST_CASE("malformed generic arrays and empty wrappers cannot become completed legacy imports") {
    ResumeFixture fixture;
    const std::vector<std::string> malformed{
      "[" + std::string(5'000'001, ' ') + "truX]", // above the legacy streaming preset
      "{\"conversations\":[truX]}",
      "{\"conversations\":[],\"suffix\":truX}",
      "{\"conversations\":[,]}"
    };
    for (std::size_t index = 0; index < malformed.size(); ++index) {
      CAPTURE(index);
      const auto path = fixture.write(malformed[index], "malformed-" + std::to_string(index) + ".json");
      auto result = fixture.importer.import_file(path, fixture.options);
      REQUIRE_FALSE(result.has_value()); CHECK(result.error().code == Errc::Parse);
      CHECK(fixture.count("conversations") == 0); CHECK(fixture.count("messages") == 0);
      const auto sources = unwrap(fixture.provenance.find_sources_by_hash(Sha256::hex(malformed[index])));
      REQUIRE(sources.size() == 1); CHECK(sources[0].metadata["import_status"] == "failed");
    }
    const auto valid = fixture.write(Json::array({Json{{"role", "user"}, {"content", "Valid generic API message"}}}).dump(), "valid-generic.json");
    const auto accepted = unwrap(fixture.importer.import_file(valid, fixture.options));
    REQUIRE(accepted.conversations.size() == 1); CHECK(accepted.messages == 1);
    CHECK(accepted.export_report.is_null()); // valid non-provider probes still route to legacy
  }

  TEST_CASE("two concurrent importer instances share source identity and item checkpoints") {
    ResumeFixture fixture;
    const auto path = fixture.write(Json::array({resume_conversation(0), resume_conversation(1)}).dump());
    auto other_db = open_db(fixture.temporary.path() / "source.db"); EventBus other_bus;
    BlobStore other_blobs(fixture.temporary.path() / "blobs", *other_db); ProvenanceStore other_provenance(*other_db);
    ConversationImporter other(*other_db, other_bus, &other_blobs, &other_provenance);
    std::barrier start(2);
    std::promise<void> first_commit, second_commit;
    auto first_ready = first_commit.get_future(); auto second_ready = second_commit.get_future();
    ImportOptions first_options = fixture.options, second_options = fixture.options;
    first_options.preflight = second_options.preflight = [&](const auto&) -> Status { start.arrive_and_wait(); return {}; };
    bool first_seen = false, second_seen = false;
    first_options.progress = [&](auto, auto, std::string_view status) {
      if (status == "export" && !first_seen) { first_seen = true; first_commit.set_value(); second_ready.wait(); }
    };
    second_options.progress = [&](auto, auto, std::string_view status) {
      if (status == "export" && !second_seen) { second_seen = true; first_ready.wait(); second_commit.set_value(); }
    };
    auto first = std::async(std::launch::async, [&] { return fixture.importer.import_file(path, first_options); });
    auto second = std::async(std::launch::async, [&] { return other.import_file(path, second_options); });
    const auto first_result = unwrap(first.get()); const auto second_result = unwrap(second.get());
    REQUIRE(first_result.conversations.size() == 2); REQUIRE(second_result.conversations.size() == 2);
    CHECK(first_result.source_id == second_result.source_id);
    CHECK(first_result.conversations[0].id == second_result.conversations[0].id);
    CHECK(first_result.conversations[1].id == second_result.conversations[1].id);
    CHECK(fixture.count("conversations") == 2); CHECK(fixture.count("messages") == 4); CHECK(fixture.count("loom_sources") == 1);
  }

  TEST_CASE("postcommit callback exception leaves a resumable checkpoint") {
    ResumeFixture fixture;
    const auto path = fixture.write(Json::array({resume_conversation(0), resume_conversation(1)}).dump());
    fixture.options.progress = [](auto, auto, std::string_view status) {
      if (status == "export") throw std::runtime_error("injected callback failure");
    };
    CHECK_THROWS_AS(fixture.importer.import_file(path, fixture.options), std::runtime_error);
    CHECK(fixture.count("conversations") == 1);
    CHECK(fixture.count("loom_import_checkpoints") == 1);
    fixture.options.progress = nullptr;
    const auto result = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(result.resumed); CHECK(result.conversations.size() == 2); CHECK(fixture.count("messages") == 4);
  }

  TEST_CASE("failed checkpoint rolls back rows and provenance before a retry") {
    ResumeFixture fixture;
    const auto path = fixture.write(Json::array({resume_conversation(0)}).dump());
    LOOM_REQUIRE_OK(fixture.db->conn().exec("CREATE TRIGGER fail_checkpoint BEFORE INSERT ON loom_import_checkpoints "
      "BEGIN SELECT RAISE(FAIL,'injected checkpoint failure'); END;"));
    const auto first = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(first.export_report["partial"] == true); CHECK(first.conversations.empty());
    CHECK(fixture.count("conversations") == 0); CHECK(fixture.count("messages") == 0);
    CHECK(fixture.count("loom_provenance") == 0);
    LOOM_REQUIRE_OK(fixture.db->conn().exec("DROP TRIGGER fail_checkpoint"));
    const auto retry = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(retry.resumed); CHECK(retry.conversations.size() == 1); CHECK(fixture.count("loom_import_checkpoints") == 1);
  }

  TEST_CASE("auxiliary ZIP member checkpoint survives cancellation without duplicate projects") {
    ResumeFixture fixture;
    const auto path = fixture.temporary.path() / "provider.zip";
    resume_zip(path, {{"conversations.json", Json::array({resume_conversation(0)}).dump()},
      {"projects.json", Json::array({Json{{"uuid", "project"}, {"name", "A project"}, {"docs", Json::array()}}}).dump()},
      {"users.json", Json::array({Json{{"uuid", "account"}, {"email", "synthetic@example.invalid"}}}).dump()}});
    CancelToken cancel;
    fixture.options.cancel = &cancel;
    fixture.options.progress = [&](auto, auto, std::string_view status) { if (status == "export.member") cancel.cancel(); };
    auto first = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(first.cancelled); CHECK(fixture.count("nodes") == 1);
    fixture.options.cancel = nullptr; fixture.options.progress = nullptr;
    auto result = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(result.resumed); CHECK(result.export_report["resumed_members"] == 1);
    CHECK(result.export_report["counts"]["project"] == 1); CHECK(result.export_report["counts"]["account"] == 1);
    CHECK(fixture.count("nodes") == 2); CHECK(fixture.count("conversations") == 1);
    CHECK(fixture.count("loom_sources") == 4); // archive and three distinct members
  }

  TEST_CASE("parser depth is a caller preset and input hooks wrap immutable source work") {
    ResumeFixture fixture;
    auto conversation = resume_conversation(0);
    std::string nested = "0";
    for (int i = 0; i < 530; ++i) nested = "[" + nested + "]";
    const auto path = fixture.write("[" + conversation.dump().substr(0, conversation.dump().size()-1) + ",\"nested\":" + nested + "}]");
    const auto limited = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(limited.conversations.empty());
    fixture.options.json_max_depth = 600;
    bool before = false, after = false;
    fixture.options.preflight = [&](const auto&) -> Status { before = true; return {}; };
    fixture.options.completed = [&](const auto& result) { after = true; CHECK(result.conversations.size() == 1); };
    const auto enlarged = unwrap(fixture.importer.import_file(path, fixture.options));
    CHECK(before); CHECK(after); CHECK(enlarged.conversations.size() == 1);
  }

#ifndef _WIN32
  TEST_CASE("hard process exit after commit resumes with a new Database instance") {
    fsutil::TempDir temporary;
    const auto source = temporary.path() / "export.json";
    LOOM_REQUIRE_OK(fsutil::write_file(source, Json::array({resume_conversation(0), resume_conversation(1)}).dump()));
    const pid_t child = fork(); REQUIRE(child >= 0);
    if (child == 0) {
      auto db = open_db(temporary.path() / "restart.db"); EventBus bus;
      BlobStore blobs(temporary.path() / "blobs", *db); ProvenanceStore provenance(*db);
      ConversationImporter importer(*db, bus, &blobs, &provenance);
      ImportOptions options; options.export_mode = ExportMode::On; options.resume = true;
      options.progress = [](auto, auto, std::string_view status) { if (status == "export") _exit(71); };
      (void)importer.import_file(source, options); _exit(72);
    }
    int status = 0; REQUIRE(waitpid(child, &status, 0) == child); REQUIRE(WIFEXITED(status)); CHECK(WEXITSTATUS(status) == 71);
    auto db = open_db(temporary.path() / "restart.db"); EventBus bus;
    BlobStore blobs(temporary.path() / "blobs", *db); ProvenanceStore provenance(*db);
    ConversationImporter importer(*db, bus, &blobs, &provenance); ImportOptions options; options.export_mode = ExportMode::On; options.resume = true;
    auto resumed = unwrap(importer.import_file(source, options));
    CHECK(resumed.resumed); CHECK(resumed.conversations.size() == 2); CHECK(resumed.messages == 4);
    CHECK(unwrap(db->conn().query_int("SELECT COUNT(*) FROM conversations")).value_or(0) == 2);
  }
#endif
}
