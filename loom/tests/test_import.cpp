#include <doctest/doctest.h>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
std::filesystem::path fixture(const char* rel) {
  return std::filesystem::path(LOOM_TEST_FIXTURES) / "import" / rel;
}
}  // namespace

TEST_SUITE("import") {
  TEST_CASE("detect_format: extension and content sniffing, matching Python's rules") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    CHECK(imp.detect_format(fixture("chatgpt_conversations.json")) == "json");
    CHECK(imp.detect_format(fixture("messages.jsonl")) == "jsonl");
    CHECK(imp.detect_format(fixture("chat.html")) == "html");
    CHECK(imp.detect_format(fixture("chat.mht")) == "mht");
    CHECK(imp.detect_format(fixture("chat.md")) == "markdown");
    CHECK(imp.detect_format(fixture("chat.txt")) == "text");
    CHECK(imp.detect_format(fixture("note.log")) == "text");
    CHECK(imp.detect_format(fixture("claude.db")) == "sqlite");
    CHECK(imp.detect_format(fixture("bundle.zip")) == "zip");
    CHECK(imp.detect_format(fixture("export_without_extension")) == "json");  // sniffed: '[' after lstrip
    CHECK(imp.detect_format(td.path() / "does-not-exist.zzz") == "unknown");

    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "weird.dat", "   \n\t{\"a\":1}"));
    CHECK(imp.detect_format(td.path() / "weird.dat") == "json");
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "page.dat", "<!DOCTYPE HTML>\n<html></html>"));
    CHECK(imp.detect_format(td.path() / "page.dat") == "html");
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "plain.dat", "just some prose, no markers"));
    CHECK(imp.detect_format(td.path() / "plain.dat") == "unknown");
  }

  TEST_CASE("import_file: ChatGPT mapping tree (roots, forks, skipped system/tool roles)") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("chatgpt_conversations.json")));
    REQUIRE(r.conversations.size() == 2);  // "Empty mapping" element yields no conversation
    CHECK(r.format == "json");
    CHECK(r.conversations[0].title == "[Import] Loom design \xe2\x9c\x93");  // "Loom design ✓"
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 3);  // system root skipped; b -> user; c, c2 -> both assistant forks
    CHECK(msgs[0].role == "user");
    CHECK(msgs[1].role == "assistant");
    CHECK(msgs[1].text == "Content-addressed by SHA-256.\nNever rewrite.");
    CHECK(msgs[2].text == "An alternative branch (fork).");
    // "Zażółć 😀"
    CHECK(r.conversations[1].title ==
         "[Import] Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 \xf0\x9f\x98\x80");
  }

  TEST_CASE("import_file: Claude export (chat_messages, content blocks, blank turn dropped)") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("claude_export.json")));
    REQUIRE(r.conversations.size() == 1);  // "No messages" element dropped
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[0].role == "user");
    CHECK(msgs[1].role == "assistant");
    CHECK(msgs[1].text == "Pass 1: lexical.\nPass 2: expanded vocabulary.");
  }

  TEST_CASE("import_file: direct OpenAI-style message list (system skipped, non-string content stringified)") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("openai_messages.json")));
    REQUIRE(r.conversations.size() == 1);
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 3);  // 'system' message dropped
    CHECK(msgs[0].text == "Describe this\n[image]");
    CHECK(msgs[1].text == "A tiny image.");
    CHECK(msgs[2].text == "42");  // content: 42 -> str(42)
  }

  TEST_CASE("import_file: HTML tag parser drops a too-short message, keeps the rest") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("chat.html")));
    REQUIRE(r.conversations.size() == 1);
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 3);  // the 5-char "Short" div is filtered (len > 5 required)
    CHECK(msgs[0].role == "user");
    CHECK(msgs[1].text == "It is the architecture constitution & roadmap.");
  }

  TEST_CASE("import_file: HTML label fallback groups by pattern, not chronologically") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("chat_labels.htm")));
    REQUIRE(r.conversations.size() == 1);
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 4);
    CHECK(msgs[0].role == "user");
    CHECK(msgs[1].role == "user");
    CHECK(msgs[2].role == "assistant");
    CHECK(msgs[3].role == "assistant");
    // The script tag's "Human: not this" must never surface.
    for (const auto& m : msgs) CHECK(m.text.find("not this") == std::string::npos);
  }

  TEST_CASE("import_file: MHT unwraps the base64 HTML part") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("chat.mht")));
    REQUIRE(r.conversations.size() == 1);
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    CHECK(msgs.size() == 3);
  }

  TEST_CASE("import_file: markdown headers and separators") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("chat.md")));
    REQUIRE(r.conversations.size() == 1);
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 4);
    CHECK(msgs[0].role == "user");
    CHECK(msgs[0].text == "How do tasks resume after a crash?");
    CHECK(msgs[3].text == "Every import records source bytes, parser and transforms.");
  }

  TEST_CASE("import_file: plain text labels split into user-first then assistant-first groups") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("chat.txt")));
    REQUIRE(r.conversations.size() == 1);
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 4);
    CHECK(msgs[0].role == "user");
    CHECK(msgs[1].role == "user");
    CHECK(msgs[2].role == "assistant");
    CHECK(msgs[2].text == "answer one\nspanning two lines");
  }

  TEST_CASE("import_file: text with no speaker labels becomes one assistant message") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("note.log")));
    REQUIRE(r.conversations.size() == 1);
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0].role == "assistant");
    CHECK(msgs[0].text == "Just a note without any speaker labels.\nSecond line.");
  }

  TEST_CASE("import_file: SQLite Claude layout (role mapping, per-conversation title fallback)") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("claude.db")));
    REQUIRE(r.conversations.size() == 2);
    bool found_named = false;
    for (const auto& c : r.conversations) {
      if (c.title == "[Import] SQLite conv one") {
        found_named = true;
        auto msgs = unwrap(db->get_msgs(c.id, true));
        REQUIRE(msgs.size() == 2);
        CHECK(msgs[0].role == "user");
        CHECK(msgs[1].role == "assistant");
      }
    }
    CHECK(found_named);
  }

  TEST_CASE("import_file: SQLite generic table (role not in user/human -> assistant)") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("generic.db")));
    REQUIRE(r.conversations.size() == 1);
    CHECK(r.conversations[0].title == "[Import] Import from chat_log");
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[0].role == "user");
    CHECK(msgs[1].role == "assistant");  // 'bot' is not 'user'/'human'
  }

  TEST_CASE("import_file: ZIP walks members in sorted order, skips unimportable files") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("bundle.zip")));
    CHECK(r.conversations.size() == 4);  // chat.md(1) + claude_export.json(1) + messages.jsonl(2); ignored.bin skipped
  }

  TEST_CASE("import_file: nested ZIP recurses into an inner .zip") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    auto r = unwrap(imp.import_file(fixture("nested.zip")));
    CHECK(r.conversations.size() == 2);  // inner.zip -> inner_chat.txt (1) + outer/note.log (1)
  }

  TEST_CASE("import_file: unknown format is Errc::Unsupported") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "nope.xyz", "not any known shape"));
    auto r = imp.import_file(td.path() / "nope.xyz");
    REQUIRE(!r);
    CHECK(r.error().code == Errc::Unsupported);
  }

  TEST_CASE("import_file: explicit title overrides any title found in the source") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    ImportOptions opts;
    opts.title = "Forced Title";
    auto r = unwrap(imp.import_file(fixture("claude_export.json"), opts));
    REQUIRE(r.conversations.size() == 1);
    CHECK(r.conversations[0].title == "[Import] Forced Title");
  }

  TEST_CASE("import_file: cancellation stops between conversations, keeps what was made") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    CancelToken cancel;
    cancel.cancel();
    ImportOptions opts;
    opts.cancel = &cancel;
    auto r = unwrap(imp.import_file(fixture("bundle.zip"), opts));
    CHECK(r.cancelled);
    CHECK(r.conversations.empty());  // cancelled before the first member
  }

  TEST_CASE("import_file: MSG_CREATED-equivalent import:done events fire with Python's payload shapes") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);

    std::vector<Json> events;
    bus.on(loom::events::kImportDone, [&](std::string_view, const Json& data) { events.push_back(data); });
    auto r = unwrap(imp.import_file(fixture("claude_export.json")));
    REQUIRE(r.conversations.size() == 1);
    // One "conv_id/count/title" event from import_message_list, one
    // "count/source/format" aggregate event from import_file.
    REQUIRE(events.size() == 2);
    CHECK(events[0].contains("conv_id"));
    CHECK(events[0]["count"] == 2);
    CHECK(events[1]["format"] == "json");
    CHECK(events[1]["count"] == 1);
  }

  TEST_CASE("import_file: provenance is recorded and dedup detects a re-import of identical bytes") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    BlobStore blobs(td.path() / "blobs", *db);
    ProvenanceStore prov(*db);
    ConversationImporter imp(*db, bus, &blobs, &prov);

    auto r1 = unwrap(imp.import_file(fixture("claude_export.json")));
    REQUIRE(r1.conversations.size() == 1);
    CHECK(!r1.source_id.empty());
    CHECK(!r1.blob_hash.empty());
    CHECK(!r1.already_imported);

    auto sources = unwrap(prov.find_sources_by_hash(r1.blob_hash));
    REQUIRE(sources.size() == 1);
    CHECK(sources[0].kind == "file");
    CHECK(sources[0].parser == "loom.importer.json");
    CHECK(sources[0].parser_version == std::string(kImporterParserVersion));

    auto conv_prov = unwrap(prov.for_subject(r1.conversations[0].id));
    REQUIRE(conv_prov.size() == 1);
    // All JSON handlers funnel through import_message_list(), which is
    // where provenance is actually recorded (see importer.h's own comment:
    // "All paths converge on import_message_list()").
    CHECK(conv_prov[0].transform == "import.message_list@1");

    auto msgs = unwrap(db->get_msgs(r1.conversations[0].id, true));
    for (const auto& m : msgs) {
      auto mp = unwrap(prov.for_subject(m.id));
      REQUIRE(mp.size() == 1);
      CHECK(mp[0].source_id == r1.source_id);
    }

    // Re-importing the identical bytes reports already_imported and returns
    // the prior conversation instead of creating a second one.
    auto r2 = unwrap(imp.import_file(fixture("claude_export.json")));
    CHECK(r2.already_imported);
    REQUIRE(r2.conversations.size() == 1);
    CHECK(r2.conversations[0].id == r1.conversations[0].id);
    CHECK(unwrap(db->list_convs(1000)).size() == 1);  // nothing new created

    // force=true re-imports anyway.
    ImportOptions forced;
    forced.force = true;
    auto r3 = unwrap(imp.import_file(fixture("claude_export.json"), forced));
    CHECK(!r3.already_imported);
    CHECK(unwrap(db->list_convs(1000)).size() == 2);
  }

  TEST_CASE("import_file: works with provenance disabled (blobs/prov left null)") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);  // no BlobStore / ProvenanceStore
    auto r = unwrap(imp.import_file(fixture("claude_export.json")));
    CHECK(r.source_id.empty());
    CHECK(r.blob_hash.empty());
    REQUIRE(r.conversations.size() == 1);
  }

  TEST_CASE("JsonArrayStreamer: incremental feed across arbitrary chunk boundaries") {
    std::string doc = R"([{"a":1,"s":"x,y]}"},{"b":[1,2,{"c":3}]},"bare"])";
    for (std::size_t chunk_size : {1u, 3u, 7u, 64u}) {
      JsonArrayStreamer streamer;
      std::vector<std::string> elements;
      for (std::size_t i = 0; i < doc.size(); i += chunk_size) {
        std::string_view chunk(doc.data() + i, std::min(chunk_size, doc.size() - i));
        bool ok = streamer.feed(chunk, [&](std::string_view e) { elements.emplace_back(e); });
        REQUIRE(ok);
      }
      INFO("chunk_size=" << chunk_size);
      REQUIRE(elements.size() == 2);  // the trailing bare "bare" scalar has no following comma, so it is dropped
      auto e0 = unwrap(json::parse(elements[0]));
      CHECK(e0["a"] == 1);
      CHECK(e0["s"] == "x,y]}");
      auto e1 = unwrap(json::parse(elements[1]));
      CHECK(e1["b"][2]["c"] == 3);
      CHECK(streamer.finished());
    }
  }

  TEST_CASE("JsonArrayStreamer: non-array input is reported as such") {
    JsonArrayStreamer streamer;
    bool got_element = false;
    bool ok = streamer.feed(R"({"not":"an array"})", [&](std::string_view) { got_element = true; });
    CHECK(!ok);
    CHECK(!got_element);
  }

  TEST_CASE("ConversationExporter: json/markdown/text/html round-trip through their matching importer") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);
    ConversationExporter exp(*db);

    auto r = unwrap(imp.import_file(fixture("chat.md")));
    REQUIRE(r.conversations.size() == 1);
    const std::string& cid = r.conversations[0].id;

    CHECK(exp.formats() == std::vector<std::string>{"json", "markdown", "text", "html"});

    auto md = unwrap(exp.export_conversation(cid, "markdown"));
    CHECK(md.find("## User") != std::string::npos);
    CHECK(md.find("## Assistant") != std::string::npos);

    auto txt = unwrap(exp.export_conversation(cid, "text"));
    CHECK(txt.find("User: ") != std::string::npos);

    auto html = unwrap(exp.export_conversation(cid, "html"));
    CHECK(html.find("data-role=\"user\"") != std::string::npos);

    auto js = unwrap(exp.export_conversation(cid, "json"));
    auto parsed = unwrap(json::parse(js));
    CHECK(parsed["conversation"]["id"] == cid);
    CHECK(parsed["messages"].size() == 4);

    CHECK(!exp.export_conversation(cid, "bogus"));
    CHECK(!exp.export_conversation("c_doesnotexist000", "json"));

    auto out_path = unwrap(exp.export_to_file(cid, "markdown", td.path() / "exports"));
    CHECK(std::filesystem::exists(out_path));

    // Re-import what we exported: same message texts and roles come back.
    ConversationImporter imp2(*db, bus);
    auto r2 = unwrap(imp2.import_file(out_path));
    REQUIRE(r2.conversations.size() == 1);
    auto msgs2 = unwrap(db->get_msgs(r2.conversations[0].id, true));
    REQUIRE(msgs2.size() == 4);
    CHECK(msgs2[0].role == "user");
  }
}
