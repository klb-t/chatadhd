// R44 (unrecognised input enters the graph, annotated) and R50 (duplicates are
// allowed, flagged, linking proposed) for the native importer.
#include <doctest/doctest.h>

#include <map>

#include "import/import_recognition.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "../third_party/miniz/miniz.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

struct RecognitionFixture {
  fsutil::TempDir temp;
  std::unique_ptr<Database> db = open_db(temp.path() / "r.db");
  EventBus bus;
  BlobStore blobs{temp.path() / "blobs", *db};
  ProvenanceStore prov{*db};
  ConversationImporter importer{*db, bus, &blobs, &prov};

  std::filesystem::path write(const std::string& name, const std::string& bytes) {
    const auto path = temp.path() / name;
    std::filesystem::create_directories(path.parent_path());
    LOOM_REQUIRE_OK(fsutil::write_file(path, bytes));
    return path;
  }
  std::filesystem::path zip(const std::string& name, const std::map<std::string, std::string>& members) {
    const auto path = temp.path() / name;
    mz_zip_archive archive{};
    REQUIRE(mz_zip_writer_init_file(&archive, path.string().c_str(), 0));
    for (const auto& [member, bytes] : members)
      REQUIRE(mz_zip_writer_add_mem(&archive, member.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&archive));
    REQUIRE(mz_zip_writer_end(&archive));
    return path;
  }
  std::vector<Node> fragments() { return unwrap(db->list_nodes("import:unrecognized", 100000)); }
  static const Json& rec(const Node& node) { return node.metadata["recognition"]; }
  static const Json& locator(const Node& node) { return node.metadata["source"]["locator"]; }
  // The single fragment whose recognition status is `status` (and which satisfies `pick`).
  template <class Pick>
  std::optional<Node> one(std::string_view status, Pick pick) {
    std::optional<Node> found;
    for (auto& node : fragments()) {
      if (rec(node)["status"] == status && pick(node)) {
        REQUIRE_FALSE(found.has_value());
        found = node;
      }
    }
    return found;
  }
  std::vector<Link> duplicate_links() {
    std::vector<Link> out;
    for (auto& link : unwrap(db->get_links(std::nullopt, "derived_from")))
      if (link.metadata.value("relation", "") == "duplicate_of") out.push_back(link);
    return out;
  }
};

const char* kConversations =
    R"([{"alien":1},{"title":"T","messages":[{"role":"user","content":"hello"},{"role":"assistant","content":"hi there"}],"extra":{"k":1}}])";

ImportOptions with_policy(std::string_view policy) {
  ImportOptions opts;
  opts.duplicate_policy = std::string(policy);
  return opts;
}

}  // namespace

TEST_SUITE("import_recognition") {
  TEST_CASE("preset default is reimport_and_link and policies are validated") {
    CHECK(default_duplicate_policy() == kDuplicateReimportAndLink);
    CHECK(ImportOptions{}.duplicate_policy == kDuplicateReimportAndLink);
    CHECK(is_duplicate_policy("reuse_prior"));
    CHECK_FALSE(is_duplicate_policy("forbid"));
    RecognitionFixture fx;
    auto result = fx.importer.import_file(fx.write("a.json", kConversations), with_policy("forbid"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Errc::InvalidArgument);
  }

  TEST_CASE("unknown JSON element and partial record are kept verbatim with locators") {
    for (const auto threshold : {default_import_preset().stream_threshold_bytes, std::int64_t{1}}) {
      RecognitionFixture fx;
      ImportOptions opts;
      opts.stream_threshold_bytes = threshold;
      auto r = unwrap(fx.importer.import_file(fx.write("a.json", kConversations), opts));
      REQUIRE(r.conversations.size() == 1);  // the import succeeds
      CHECK(unwrap(fx.db->get_msgs(r.conversations[0].id, true)).size() == 2);
      REQUIRE(fx.fragments().size() == 2);

      auto unknown = fx.one("unrecognized", [](const Node&) { return true; });
      REQUIRE(unknown);
      CHECK(unknown->content == R"({"alien":1})");  // verbatim bytes
      CHECK(RecognitionFixture::locator(*unknown)["json_pointer"] == "/0");
      CHECK(RecognitionFixture::rec(*unknown)["recognizer"] == "loom.importer.json");
      CHECK(RecognitionFixture::rec(*unknown)["recognizer_version"] == std::string(kImporterParserVersion));
      CHECK_FALSE(RecognitionFixture::rec(*unknown)["reason"].get<std::string>().empty());
      CHECK(RecognitionFixture::rec(*unknown)["attempts"].size() == 1);
      CHECK(unknown->metadata["source"]["source_id"] == r.source_id);
      if (threshold > 1) {  // whole-file parse knows the byte range
        CHECK(RecognitionFixture::locator(*unknown)["byte_start"] == 1);
        CHECK(RecognitionFixture::locator(*unknown)["byte_end"] == 12);
      }

      auto partial = fx.one("partial", [](const Node&) { return true; });
      REQUIRE(partial);
      CHECK(partial->content == R"({"extra": {"k": 1}})");  // only the unmapped fields (py_dumps spelling)
      CHECK(partial->metadata["source"]["conversation_id"] == r.conversations[0].id);

      REQUIRE(r.recognition.is_object());
      CHECK(r.recognition["counts"]["unrecognized"] == 1);
      CHECK(r.recognition["counts"]["partial"] == 1);
      CHECK(r.recognition["counts"]["opaque"] == 0);
      CHECK(r.recognition["counts"]["recognized"] == 0);  // the only conversation is partial
      CHECK(r.recognition["nodes"].size() == 2);
      CHECK(r.to_json()["recognition"]["counts"]["unrecognized"] == 1);
    }
  }

  TEST_CASE("an entirely unknown JSON document enters the graph instead of failing the import") {
    RecognitionFixture fx;
    const std::vector<std::string> documents{R"({"alien_turns":{"message":"SYNTHETIC_UNKNOWN"}})", R"("unknown")", "42"};
    for (const auto& document : documents) {
      auto r = unwrap(fx.importer.import_file(fx.write("u.json", document)));
      CHECK(r.conversations.empty());
      auto node = fx.one("unrecognized", [&](const Node& n) { return n.content == document; });
      CHECK(node.has_value());
    }
    // A scalar array entry is an element-level fragment.
    unwrap(fx.importer.import_file(fx.write("s.json", "[42]")));
    auto element = fx.one("unrecognized", [](const Node& n) { return n.content == "42" && RecognitionFixture::locator(n)["json_pointer"] == "/0"; });
    CHECK(element.has_value());
  }

  TEST_CASE("JSONL: unknown line is unrecognized, a corrupt line is opaque with its hash") {
    RecognitionFixture fx;
    const std::string corrupt = "{not json";
    const std::string text = "{\"role\":\"user\",\"content\":\"hello\"}\n{\"weather\":\"sunny\"}\n" + corrupt + "\n";
    auto r = unwrap(fx.importer.import_file(fx.write("l.jsonl", text)));
    REQUIRE(r.conversations.size() == 1);
    auto unknown = fx.one("unrecognized", [](const Node&) { return true; });
    REQUIRE(unknown);
    CHECK(unknown->content == R"({"weather":"sunny"})");
    CHECK(RecognitionFixture::locator(*unknown)["line"] == 2);
    auto opaque = fx.one("opaque", [](const Node&) { return true; });
    REQUIRE(opaque);
    CHECK(opaque->content == corrupt);
    CHECK(RecognitionFixture::locator(*opaque)["line"] == 3);
    CHECK(RecognitionFixture::rec(*opaque)["sha256"] == Sha256::hex(corrupt));
    CHECK(r.recognition["counts"]["opaque"] == 1);
  }

  TEST_CASE("HTML without conversation structure is partial; unused MHT parts are kept with byte ranges") {
    RecognitionFixture fx;
    const std::string page = "<html><body><p>plain page text without any chat markers</p></body></html>";
    auto h = unwrap(fx.importer.import_file(fx.write("page.html", page)));
    REQUIRE(h.conversations.size() == 1);
    auto partial = fx.one("partial", [](const Node& n) { return n.content.find("plain page text") != std::string::npos; });
    REQUIRE(partial);
    CHECK(partial->content == page);
    CHECK(partial->metadata["source"]["conversation_id"] == h.conversations[0].id);

    const std::string mht =
        "From: saved\r\n\r\n------=_part0\r\nContent-Type: text/html\r\n\r\n<html><body><p>mht page text without chat markers</p></body></html>\r\n"
        "------=_part1\r\nContent-Type: image/png\r\nContent-Transfer-Encoding: base64\r\n\r\niVBORw0KGgo=\r\n";
    auto m = unwrap(fx.importer.import_file(fx.write("saved.mht", mht)));
    REQUIRE(m.conversations.size() == 1);
    auto image = fx.one("unrecognized", [](const Node& n) { return n.content.find("iVBORw0KGgo=") != std::string::npos; });
    REQUIRE(image);
    CHECK(RecognitionFixture::locator(*image)["content_type"] == "image/png");
    const auto start = RecognitionFixture::locator(*image)["byte_start"].get<std::size_t>();
    const auto end = RecognitionFixture::locator(*image)["byte_end"].get<std::size_t>();
    CHECK(mht.substr(start, end - start) == image->content);  // the range addresses the verbatim bytes
  }

  TEST_CASE("legacy ZIP: unknown, binary and corrupt members are kept; the import succeeds") {
    RecognitionFixture fx;
    const std::string binary("\x00\x01\x02\xff\xfe binary", 12);
    const std::string unknown_text = "just some notes in a format nobody knows";
    const std::string corrupt = R"({"messages": [)";
    ImportOptions opts;
    opts.export_mode = ExportMode::Off;
    auto r = unwrap(fx.importer.import_file(fx.zip("legacy.zip", {{"a/chat.txt", "Human: hi\nAssistant: hello"},
                                                                  {"b/data.bin", binary},
                                                                  {"c/notes.dat", unknown_text},
                                                                  {"d/broken.json", corrupt}}), opts));
    CHECK(r.conversations.size() == 1);
    auto bin = fx.one("opaque", [](const Node& n) { return RecognitionFixture::locator(n)["member"] == "b/data.bin"; });
    REQUIRE(bin);
    CHECK(RecognitionFixture::rec(*bin)["sha256"] == Sha256::hex(binary));
    CHECK(RecognitionFixture::rec(*bin)["stored"] == "blob");
    CHECK(unwrap(fx.blobs.read(RecognitionFixture::rec(*bin)["blob_hash"].get<std::string>())) == binary);
    auto notes = fx.one("unrecognized", [](const Node& n) { return RecognitionFixture::locator(n)["member"] == "c/notes.dat"; });
    REQUIRE(notes);
    CHECK(notes->content == unknown_text);
    auto broken = fx.one("opaque", [](const Node& n) { return RecognitionFixture::locator(n)["member"] == "d/broken.json"; });
    REQUIRE(broken);
    CHECK(broken->content == corrupt);
    CHECK(RecognitionFixture::rec(*broken)["sha256"] == Sha256::hex(corrupt));
    CHECK(RecognitionFixture::locator(*broken).contains("archive_index"));
    CHECK(r.recognition["counts"]["opaque"] == 2);
    CHECK(r.recognition["counts"]["unrecognized"] == 1);
  }

  TEST_CASE("provider-export ZIP path annotates unknown members and keeps the others too") {
    RecognitionFixture fx;
    const std::string blob("\x00\x01\x02 blob", 8);
    auto r = unwrap(fx.importer.import_file(fx.zip("generic.zip", {{"x/chat.txt", "Human: hi\nAssistant: hello"},
                                                                   {"x/notes.dat", "just some notes in a format nobody knows"},
                                                                   {"x/blob.bin", blob}})));
    CHECK(r.conversations.size() == 1);
    bool notes = false, binary = false;
    for (auto& node : unwrap(fx.db->list_nodes("export:member", 1000))) {
      const auto& recognition = node.metadata["recognition"];
      if (node.label == "x/notes.dat") {
        notes = true;
        CHECK(recognition["status"] == "unrecognized");
        CHECK(node.metadata["source"]["locator"]["member"] == "x/notes.dat");
      }
      if (node.label == "x/blob.bin") {
        binary = true;
        CHECK(recognition["status"] == "opaque");
        CHECK(recognition["sha256"] == Sha256::hex(blob));
      }
    }
    CHECK(notes);
    CHECK(binary);
    CHECK(r.recognition["counts"]["unrecognized"] == 1);
    CHECK(r.recognition["counts"]["opaque"] == 1);
  }

  TEST_CASE("formats that do not classify input report no recognition counts") {
    RecognitionFixture fx;
    auto r = unwrap(fx.importer.import_file(fx.write("chat.md", "## Human\nhello there\n\n## Assistant\nhi\n")));
    CHECK(r.recognition.is_null());  // nothing measured, nothing invented
    CHECK(fx.fragments().empty());
  }

  TEST_CASE("same source twice: reported always, linked / reused / plain according to duplicate_policy") {
    const std::string source =
        R"([{"title":"Dup","messages":[{"role":"user","content":"same words"},{"role":"assistant","content":"same reply"}]}])";
    SUBCASE("reimport_and_link (default)") {
      RecognitionFixture fx;
      const auto path = fx.write("d.json", source);
      auto first = unwrap(fx.importer.import_file(path));
      CHECK(first.duplicates.is_null());
      auto cached = unwrap(fx.importer.import_file(path));  // unchanged: immutable-source cache hit
      CHECK(cached.already_imported);
      REQUIRE(cached.duplicates.is_object());
      CHECK(cached.duplicates["items"][0]["level"] == "source");
      CHECK(cached.duplicates["items"][0]["cache_hit"] == true);
      CHECK(cached.duplicates["items"][0]["proposal"]["action"] == "pin_immutable_data");

      ImportOptions forced;
      forced.force = true;
      auto again = unwrap(fx.importer.import_file(path, forced));
      REQUIRE(again.conversations.size() == 1);
      CHECK(again.conversations[0].id != first.conversations[0].id);  // imported again
      CHECK(unwrap(fx.db->list_convs(100)).size() == 2);
      REQUIRE(again.duplicates.is_object());
      CHECK(again.duplicates["policy"] == "reimport_and_link");
      bool conversation_item = false;
      for (const auto& item : again.duplicates["items"]) {
        if (item["level"] != "conversation") continue;
        conversation_item = true;
        CHECK(item["action"] == "linked");
        CHECK(item["prior_conversation_id"] == first.conversations[0].id);
        CHECK(item["conversation_id"] == again.conversations[0].id);
        CHECK(item["proposal"]["subjects"].size() == 2);
      }
      CHECK(conversation_item);
      CHECK(fx.duplicate_links().size() == 1);
    }
    SUBCASE("reuse_prior") {
      RecognitionFixture fx;
      const auto path = fx.write("d.json", source);
      auto first = unwrap(fx.importer.import_file(path, with_policy("reuse_prior")));
      ImportOptions forced = with_policy("reuse_prior");
      forced.force = true;
      auto again = unwrap(fx.importer.import_file(path, forced));
      CHECK(unwrap(fx.db->list_convs(100)).size() == 1);  // no second copy
      REQUIRE(again.conversations.size() == 1);
      CHECK(again.conversations[0].id == first.conversations[0].id);
      CHECK(again.messages == 2);
      bool reused = false;
      for (const auto& item : again.duplicates["items"]) reused = reused || (item["level"] == "conversation" && item["action"] == "reused");
      CHECK(reused);
      CHECK(fx.duplicate_links().empty());
      // This source is now recorded against the prior conversation.
      auto records = unwrap(fx.prov.for_subject(first.conversations[0].id));
      bool new_source = false;
      for (const auto& record : records) new_source = new_source || record.source_id == again.source_id;
      CHECK(new_source);
    }
    SUBCASE("reimport (previous behaviour: no linking, still reported)") {
      RecognitionFixture fx;
      const auto path = fx.write("d.json", source);
      unwrap(fx.importer.import_file(path, with_policy("reimport")));
      ImportOptions forced = with_policy("reimport");
      forced.force = true;
      auto again = unwrap(fx.importer.import_file(path, forced));
      CHECK(unwrap(fx.db->list_convs(100)).size() == 2);
      CHECK(fx.duplicate_links().empty());
      REQUIRE(again.duplicates.is_object());
      bool plain = false;
      for (const auto& item : again.duplicates["items"]) plain = plain || (item["level"] == "conversation" && item["action"] == "none");
      CHECK(plain);
    }
  }

  TEST_CASE("the same conversation in two different files is a conversation-level duplicate") {
    RecognitionFixture fx;
    const std::string a = R"([{"role":"user","content":"hello   there"},{"role":"assistant","content":"hi"}])";
    const std::string b = R"([{"role":"user","content":"hello there"},{"role":"assistant","content":"hi"}])";  // whitespace-normalised equal
    auto first = unwrap(fx.importer.import_file(fx.write("a.json", a)));
    auto second = unwrap(fx.importer.import_file(fx.write("b.json", b)));
    CHECK_FALSE(second.already_imported);  // different bytes, different source
    REQUIRE(second.duplicates.is_object());
    CHECK(second.duplicates["items"][0]["level"] == "conversation");
    CHECK(second.duplicates["items"][0]["prior_conversation_id"] == first.conversations[0].id);
    CHECK(fx.duplicate_links().size() == 1);
  }

  TEST_CASE("an unrecognised fragment seen twice follows the policy as well") {
    const std::string unknown = R"({"alien":"same bytes"})";
    SUBCASE("linked") {
      RecognitionFixture fx;
      unwrap(fx.importer.import_file(fx.write("1.json", unknown)));
      auto second = unwrap(fx.importer.import_file(fx.write("2.json", unknown)));
      CHECK(fx.fragments().size() == 2);
      CHECK(fx.duplicate_links().size() == 1);
      bool fragment_item = false;
      for (const auto& item : second.duplicates["items"]) fragment_item = fragment_item || (item["level"] == "fragment" && item["action"] == "linked");
      CHECK(fragment_item);
    }
    SUBCASE("reuse_prior keeps one node and records the new occurrence as provenance") {
      RecognitionFixture fx;
      unwrap(fx.importer.import_file(fx.write("1.json", unknown), with_policy("reuse_prior")));
      auto second = unwrap(fx.importer.import_file(fx.write("2.json", unknown), with_policy("reuse_prior")));
      REQUIRE(fx.fragments().size() == 1);
      bool reused = false;
      for (const auto& item : second.duplicates["items"]) reused = reused || (item["level"] == "fragment" && item["action"] == "reused");
      CHECK(reused);
      CHECK(unwrap(fx.prov.for_subject(fx.fragments()[0].id)).size() == 2);
    }
  }
}
