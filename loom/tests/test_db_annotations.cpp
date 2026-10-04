#include <doctest/doctest.h>

#include <atomic>
#include <set>
#include <thread>

#include "loom/db.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
struct AnnotationFixture {
  fsutil::TempDir temporary;
  std::unique_ptr<Database> db = open_db(temporary.path() / "annotations.db");
  std::string conversation = unwrap(db->create_conv("synthetic annotation fixture")).id;
  std::string node = unwrap(db->create_node("synthetic node", "concept"));
  std::string message;

  explicit AnnotationFixture(std::string text = "Aż😀é!") {
    NewMessage input;
    input.conv_id = conversation;
    input.text = std::move(text);
    input.role = "user";
    input.attachments = Json::array({"synthetic.txt"});
    input.metadata = Json{{"source", Json{{"pointer", "/synthetic/0"}, {"unknown", Json::array({1, 2})}}}};
    message = unwrap(db->create_msg(input));
  }

  NewMessageAnnotation annotation(std::int64_t start = 1, std::int64_t end = 3) const {
    NewMessageAnnotation result;
    result.message_id = message;
    result.node_id = node;
    result.start_char = start;
    result.end_char = end;
    result.metadata = Json{{"source_locator", "/synthetic/0"}};
    return result;
  }

  std::int64_t snapshots() {
    return *unwrap(db->conn().query_int("SELECT COUNT(*) FROM loom_message_annotation_sources"));
  }
};
}  // namespace

TEST_SUITE("db.annotations") {
  TEST_CASE("extension migration is forward-only, additive and preserves core rows") {
    AnnotationFixture fixture;
    const auto path = fixture.db->path();
    const Json before = unwrap(fixture.db->get_msg(fixture.message))->to_json();
    LOOM_REQUIRE_OK(fixture.db->conn().exec(
        "DROP TABLE loom_message_annotations;"
        "DROP TABLE loom_message_annotation_sources;"
        "DROP TABLE loom_import_checkpoints;"
        "DELETE FROM _meta WHERE key IN ('loom_message_annotation_schema_version','loom_import_checkpoint_schema_version');"));
    fixture.db.reset();
    fixture.db = open_db(path);
    CHECK(unwrap(fixture.db->get_msg(fixture.message))->to_json() == before);
    CHECK(unwrap(fixture.db->schema_version()) == 4);
    CHECK(unwrap(fixture.db->get_meta("loom_schema_version")) == std::optional<std::string>("1"));
    CHECK(unwrap(fixture.db->get_meta("loom_message_annotation_schema_version")) == std::optional<std::string>("1"));
    CHECK(unwrap(fixture.db->get_meta("loom_import_checkpoint_schema_version")) == std::optional<std::string>("1"));
    CHECK(fixture.db->conn().has_table("loom_message_annotations"));
    CHECK(fixture.db->conn().has_table("loom_import_checkpoints"));
    CHECK(*unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM sqlite_master WHERE type='trigger'")) == 0);
    const auto annotation = unwrap(fixture.db->create_message_annotation(fixture.annotation()));
    fixture.db.reset();
    fixture.db = open_db(path);
    CHECK(unwrap(fixture.db->get_message_annotation(annotation.id))->excerpt == "ż😀");
    CHECK(fixture.snapshots() == 1);

    LOOM_REQUIRE_OK(fixture.db->set_meta("loom_message_annotation_schema_version", "2"));
    fixture.db.reset();
    auto newer = Database::open(path);
    REQUIRE_FALSE(newer);
    CHECK(newer.error().code == Errc::Unsupported);
    auto raw = unwrap(sql::Connection::open(path));
    CHECK(unwrap(raw.query_text("SELECT value FROM _meta WHERE key='loom_message_annotation_schema_version'")) ==
          std::optional<std::string>("2"));
    CHECK(*unwrap(raw.query_int("SELECT COUNT(*) FROM loom_message_annotations")) == 1);
  }

  TEST_CASE("Unicode codepoint ranges retain exact bytes and complete source payload") {
    AnnotationFixture fixture;
    const Json original = unwrap(fixture.db->get_msg(fixture.message))->to_json();
    auto input = fixture.annotation();
    input.expected_text_hash = Sha256::hex(original["text"].get_ref<const std::string&>());
    const auto annotation = unwrap(fixture.db->create_message_annotation(input));
    CHECK(annotation.start_char == 1);
    CHECK(annotation.end_char == 3);
    CHECK(annotation.start_byte == 1);
    CHECK(annotation.end_byte == 7);
    CHECK(annotation.excerpt == "ż😀");
    CHECK(annotation.origin == "recorded");
    CHECK(annotation.message_exists);
    CHECK(annotation.node_exists);
    CHECK(annotation.source_matches_current);
    CHECK(annotation.to_json()["range_unit"] == "unicode_codepoint");
    const auto snapshot = unwrap(fixture.db->get_message_annotation_source(annotation.source_snapshot_id));
    REQUIRE(snapshot);
    CHECK(snapshot->message == original);
    CHECK(snapshot->text_hash == *input.expected_text_hash);
    CHECK(snapshot->record_hash == Sha256::hex(json::py_dumps(original)));

    auto combining = fixture.annotation(3, 5);
    combining.origin = "model";
    combining.metadata = Json{{"model", "offline-fixture"}, {"generation", Json{{"prompt_hash", "synthetic"}}}};
    auto second = unwrap(fixture.db->create_message_annotation(combining));
    CHECK(second.excerpt == "é");
    CHECK(second.start_byte == 7);
    CHECK(second.end_byte == 10);
    CHECK(second.source_snapshot_id == annotation.source_snapshot_id);
    CHECK(fixture.snapshots() == 1);
    CHECK(unwrap(fixture.db->get_msg(fixture.message))->to_json() == original);

    MsgPatch metadata_edit;
    metadata_edit.metadata = Json{{"different", true}};
    LOOM_REQUIRE_OK(fixture.db->update_msg(fixture.message, metadata_edit));
    auto third = unwrap(fixture.db->create_message_annotation(fixture.annotation()));
    CHECK(third.source_snapshot_id != annotation.source_snapshot_id);
    CHECK(fixture.snapshots() == 2);
    CHECK(unwrap(fixture.db->get_message_annotation_source(annotation.source_snapshot_id))->message == original);
  }

  TEST_CASE("embedded NUL text preserves codepoint and byte offsets") {
    AnnotationFixture fixture(std::string("a\0ż", 4));
    const auto annotation = unwrap(fixture.db->create_message_annotation(fixture.annotation(1, 3)));
    CHECK(annotation.excerpt == std::string("\0ż", 3));
    CHECK(annotation.start_byte == 1);
    CHECK(annotation.end_byte == 4);
    CHECK(unwrap(fixture.db->get_message_annotation_source(annotation.source_snapshot_id))->message["text"] ==
        std::string("a\0ż", 4));
  }

  TEST_CASE("message edits and deletions never retarget or erase annotations") {
    AnnotationFixture fixture;
    auto annotation = unwrap(fixture.db->create_message_annotation(fixture.annotation()));
    const auto version = unwrap(fixture.db->edit_msg(fixture.message, "new version"));
    REQUIRE(version);
    CHECK(unwrap(fixture.db->list_message_annotations(*version)).empty());
    CHECK(unwrap(fixture.db->get_message_annotation(annotation.id))->source_matches_current);
    MsgPatch direct_edit;
    direct_edit.text = "shifted incompatible source";
    LOOM_REQUIRE_OK(fixture.db->update_msg(fixture.message, direct_edit));
    auto changed = unwrap(fixture.db->get_message_annotation(annotation.id));
    REQUIRE(changed);
    CHECK_FALSE(changed->source_matches_current);
    CHECK(changed->excerpt == "ż😀");
    CHECK(changed->source_snapshot_id == annotation.source_snapshot_id);

    MessageAnnotationPatch old_source_correction;
    old_source_correction.start_char = 3;
    old_source_correction.end_char = 5;
    old_source_correction.origin = "user";
    auto corrected = unwrap(fixture.db->revise_message_annotation(annotation.id, 1, old_source_correction));
    CHECK(corrected.excerpt == "é");
    CHECK_FALSE(corrected.source_matches_current);
    LOOM_REQUIRE_OK(fixture.db->delete_conv(fixture.conversation));
    LOOM_REQUIRE_OK(fixture.db->delete_node(fixture.node));
    const auto orphaned = unwrap(fixture.db->get_message_annotation(annotation.id));
    REQUIRE(orphaned);
    CHECK_FALSE(orphaned->message_exists);
    CHECK_FALSE(orphaned->node_exists);
    CHECK_FALSE(orphaned->source_matches_current);
    CHECK(orphaned->excerpt == "é");
    CHECK(unwrap(fixture.db->list_message_annotations(fixture.message)).size() == 1);
    CHECK(unwrap(fixture.db->get_message_annotation_history(annotation.id)).size() == 2);
    CHECK(unwrap(fixture.db->get_message_annotation_source(annotation.source_snapshot_id))->message["text"] == "Aż😀é!");
  }

  TEST_CASE("append-only corrections and retractions preserve provenance and reject lost updates") {
    AnnotationFixture fixture;
    auto original = unwrap(fixture.db->create_message_annotation(fixture.annotation()));
    MessageAnnotationPatch patch;
    patch.origin = "model";
    patch.metadata = Json{{"model", "offline"}, {"analysis_hash", "synthetic"}};
    auto second = unwrap(fixture.db->revise_message_annotation(original.id, 1, patch));
    CHECK(second.revision == 2);
    auto stale = fixture.db->revise_message_annotation(original.id, 1, patch);
    REQUIRE_FALSE(stale);
    CHECK(stale.error().code == Errc::Conflict);
    CHECK(unwrap(fixture.db->get_message_annotation_history(original.id)).size() == 2);
    MessageAnnotationPatch retract;
    retract.origin = "user";
    retract.state = "retracted";
    retract.metadata = Json{{"reason", "synthetic correction"}};
    auto third = unwrap(fixture.db->revise_message_annotation(original.id, 2, retract));
    CHECK(third.revision == 3);
    CHECK(unwrap(fixture.db->list_message_annotations(fixture.message)).empty());
    MessageAnnotationListOptions all;
    all.include_retracted = true;
    CHECK(unwrap(fixture.db->list_message_annotations(fixture.message, all)).size() == 1);
    const auto history = unwrap(fixture.db->get_message_annotation_history(original.id));
    REQUIRE(history.size() == 3);
    CHECK(history[0].origin == "recorded");
    CHECK(history[0].metadata == original.metadata);
    CHECK(history[1].origin == "model");
    CHECK(history[1].metadata == *patch.metadata);
    CHECK(history[2].origin == "user");
    CHECK(history[2].state == "retracted");
    CHECK(history[2].metadata == *retract.metadata);
    CHECK(history[0].source_snapshot_id == history[2].source_snapshot_id);
    MessageAnnotationPatch restore;
    restore.state = "active";
    restore.origin = "user";
    CHECK(unwrap(fixture.db->revise_message_annotation(original.id, 3, restore)).revision == 4);
    CHECK(unwrap(fixture.db->list_message_annotations(fixture.message)).size() == 1);
    const auto paged_history = unwrap(fixture.db->get_message_annotation_history(original.id, 1, 2));
    REQUIRE(paged_history.size() == 2);
    CHECK(paged_history[0].revision == 2);
    CHECK(paged_history[1].revision == 3);
  }

  TEST_CASE("invalid references, origins, spans and stale source hashes leave no partial snapshot") {
    AnnotationFixture fixture;
    for (const auto& range : {std::pair{-1, 2}, std::pair{3, 2}, std::pair{1, 7}}) {
      CHECK_FALSE(fixture.db->create_message_annotation(fixture.annotation(range.first, range.second)));
    }
    auto input = fixture.annotation();
    input.origin = "invented";
    CHECK_FALSE(fixture.db->create_message_annotation(input));
    input = fixture.annotation();
    input.message_id = "m_missing";
    CHECK_FALSE(fixture.db->create_message_annotation(input));
    input = fixture.annotation();
    input.node_id = "n_missing";
    CHECK_FALSE(fixture.db->create_message_annotation(input));
    input = fixture.annotation();
    input.expected_text_hash = Sha256::hex("stale source");
    auto stale = fixture.db->create_message_annotation(input);
    REQUIRE_FALSE(stale);
    CHECK(stale.error().code == Errc::Conflict);
    CHECK(fixture.snapshots() == 0);
    CHECK(*unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM loom_message_annotations")) == 0);
    const auto point = unwrap(fixture.db->create_message_annotation(fixture.annotation(6, 6)));
    CHECK(point.excerpt.empty());
    CHECK(point.start_byte == 11);
    CHECK(point.end_byte == 11);
    MessageAnnotationPatch invalid;
    invalid.end_char = 7;
    CHECK_FALSE(fixture.db->revise_message_annotation(point.id, 1, invalid));
    invalid = {};
    invalid.origin = "invented";
    CHECK_FALSE(fixture.db->revise_message_annotation(point.id, 1, invalid));
    CHECK(unwrap(fixture.db->get_message_annotation_history(point.id)).size() == 1);

    AnnotationFixture invalid_utf8(std::string(1, static_cast<char>(0xff)));
    CHECK_FALSE(invalid_utf8.db->create_message_annotation(invalid_utf8.annotation(0, 1)));
    CHECK(invalid_utf8.snapshots() == 0);
    CHECK(unwrap(invalid_utf8.db->get_msg(invalid_utf8.message))->text == std::string(1, static_cast<char>(0xff)));
  }

  TEST_CASE("latest annotations have configurable keyset pagination and node filters") {
    AnnotationFixture fixture;
    std::set<std::string> expected;
    for (int index = 0; index < 5; ++index)
      expected.insert(unwrap(fixture.db->create_message_annotation(fixture.annotation())).id);
    std::set<std::string> actual;
    MessageAnnotationListOptions page;
    page.limit = 2;
    for (int index = 0; index < 3; ++index) {
      auto annotations = unwrap(fixture.db->list_message_annotations(fixture.message, page));
      REQUIRE(annotations.size() == (index < 2 ? 2 : 1));
      for (const auto& annotation : annotations) CHECK(actual.insert(annotation.id).second);
      page.after_id = annotations.back().id;
    }
    CHECK(unwrap(fixture.db->list_message_annotations(fixture.message, page)).empty());
    CHECK(actual == expected);
    page.after_id.reset();
    page.limit.reset();
    CHECK(unwrap(fixture.db->list_message_annotations(fixture.message, page)).size() == 5);
    page.node_id = "n_missing";
    CHECK(unwrap(fixture.db->list_message_annotations(fixture.message, page)).empty());
    page.limit = -1;
    CHECK_FALSE(fixture.db->list_message_annotations(fixture.message, page));
    CHECK(fixture.snapshots() == 1);
  }

  TEST_CASE("outer transaction rollback also rolls back immutable snapshots and revisions") {
    AnnotationFixture fixture;
    std::string annotation_id;
    {
      auto lock = fixture.db->lock();
      sql::Txn outer(fixture.db->conn());
      LOOM_REQUIRE_OK(outer.begin_status());
      annotation_id = unwrap(fixture.db->create_message_annotation(fixture.annotation())).id;
      CHECK(fixture.snapshots() == 1);
    }
    CHECK(fixture.snapshots() == 0);
    CHECK_FALSE(unwrap(fixture.db->get_message_annotation(annotation_id)));
    CHECK(unwrap(fixture.db->get_msg(fixture.message)));
  }

  TEST_CASE("snapshot corruption is reported rather than silently rebinding evidence") {
    AnnotationFixture fixture;
    const auto annotation = unwrap(fixture.db->create_message_annotation(fixture.annotation()));
    const auto source = unwrap(fixture.db->get_message_annotation_source(annotation.source_snapshot_id));
    REQUIRE(source);
    LOOM_REQUIRE_OK(fixture.db->conn().run(
        "UPDATE loom_message_annotation_sources SET source_text=? WHERE id=?", "different text", source->id));
    auto damaged_text = fixture.db->get_message_annotation(annotation.id);
    REQUIRE_FALSE(damaged_text);
    CHECK(damaged_text.error().code == Errc::Database);
    LOOM_REQUIRE_OK(fixture.db->conn().run("UPDATE loom_message_annotation_sources SET source_text=?,message_json=? WHERE id=?",
        source->message["text"].get<std::string>(), "{}", source->id));
    auto damaged_record = fixture.db->get_message_annotation_source(source->id);
    REQUIRE_FALSE(damaged_record);
    CHECK(damaged_record.error().code == Errc::Database);
    auto damaged_annotation = fixture.db->get_message_annotation(annotation.id);
    REQUIRE_FALSE(damaged_annotation);
    CHECK(damaged_annotation.error().code == Errc::Database);
    auto damaged_list = fixture.db->list_message_annotations(fixture.message);
    REQUIRE_FALSE(damaged_list);
    CHECK(damaged_list.error().code == Errc::Database);
    auto damaged_history = fixture.db->get_message_annotation_history(annotation.id);
    REQUIRE_FALSE(damaged_history);
    CHECK(damaged_history.error().code == Errc::Database);
    LOOM_REQUIRE_OK(fixture.db->conn().run(
        "UPDATE loom_message_annotation_sources SET message_json=?,message_id=? WHERE id=?",
        json::py_dumps(source->message), "m_retargeted", source->id));
    auto retargeted = fixture.db->get_message_annotation(annotation.id);
    REQUIRE_FALSE(retargeted);
    CHECK(retargeted.error().code == Errc::Database);
    CHECK_FALSE(fixture.db->list_message_annotations("m_retargeted"));
    CHECK_FALSE(fixture.db->get_message_annotation_history(annotation.id));
    CHECK_FALSE(fixture.db->get_message_annotation_source(source->id));
    // The FK protects a snapshot still referenced by annotation history.
    CHECK_FALSE(fixture.db->conn().run("DELETE FROM loom_message_annotation_sources WHERE id=?", source->id));
    CHECK(unwrap(fixture.db->get_msg(fixture.message))->text == "Aż😀é!");
  }

  TEST_CASE("independent connections deduplicate source snapshots atomically") {
    AnnotationFixture fixture;
    auto second_connection = open_db(fixture.db->path());
    std::atomic<int> failures{0};
    auto worker = [&](Database& database) {
      for (int index = 0; index < 5; ++index)
        if (!database.create_message_annotation(fixture.annotation())) ++failures;
    };
    std::thread first(worker, std::ref(*fixture.db));
    std::thread second(worker, std::ref(*second_connection));
    first.join();
    second.join();
    CHECK(failures.load() == 0);
    CHECK(fixture.snapshots() == 1);
    CHECK(unwrap(fixture.db->list_message_annotations(fixture.message)).size() == 10);
  }
}
