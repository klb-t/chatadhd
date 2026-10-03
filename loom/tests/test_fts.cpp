#include <doctest/doctest.h>

#include "loom/db.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
std::string add(Database& db, const std::string& conv, const std::string& text) {
  NewMessage m;
  m.conv_id = conv;
  m.text = text;
  m.role = "user";
  return unwrap(db.create_msg(m));
}
std::vector<std::string> ids(const SearchResult& r) {
  std::vector<std::string> out;
  for (const auto& h : r.hits) out.push_back(h.message.id);
  return out;
}
}  // namespace

TEST_SUITE("fts") {
  TEST_CASE("incremental sync after Loom writes") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "chatadhd.db");
    REQUIRE(db->fts_status().available);
    CHECK(std::filesystem::exists(td.path() / "chatadhd.fts.db"));
    auto conv = unwrap(db->create_conv());
    std::string a = add(*db, conv.id, "The quick brown fox jumps over the lazy dog");
    std::string b = add(*db, conv.id, "Zażółć gęślą jaźń - Polish pangram");
    auto r = unwrap(db->search_messages("fox"));
    CHECK(r.mode == "fts5");
    CHECK(ids(r) == std::vector<std::string>{a});
    CHECK(r.hits[0].snippet.find("[fox]") != std::string::npos);
    // remove_diacritics 2: ASCII query matches accented text.
    CHECK(ids(unwrap(db->search_messages("gesla"))) == std::vector<std::string>{b});  // ł has no decomposition, ę/ś/ą do
    CHECK(ids(unwrap(db->search_messages("gęślą"))) == std::vector<std::string>{b});
    // Prefix and multi-token AND.
    CHECK(ids(unwrap(db->search_messages("pang*"))) == std::vector<std::string>{b});
    CHECK(unwrap(db->search_messages("fox pangram")).hits.empty());
    // Operators are treated literally (no FTS syntax errors).
    CHECK(unwrap(db->search_messages("\"fox\" OR (")).hits.empty());

    // Edit -> new version active, old version excluded by default.
    auto e = unwrap(db->edit_msg(a, "The quick brown cat"));
    REQUIRE(e);
    CHECK(unwrap(db->search_messages("fox")).hits.empty());
    SearchOptions all;
    all.include_inactive = true;
    CHECK(ids(unwrap(db->search_messages("fox", all))) == std::vector<std::string>{a});
    CHECK(ids(unwrap(db->search_messages("cat"))) == std::vector<std::string>{*e});

    // update_msg(text) re-indexes.
    MsgPatch p;
    p.text = "renamed to hedgehog";
    LOOM_REQUIRE_OK(db->update_msg(*e, p));
    CHECK(ids(unwrap(db->search_messages("hedgehog"))) == std::vector<std::string>{*e});
    CHECK(unwrap(db->search_messages("cat")).hits.empty());

    // conv filter + delete_conv.
    auto conv2 = unwrap(db->create_conv());
    std::string c = add(*db, conv2.id, "another hedgehog");
    SearchOptions only2;
    only2.conv_id = conv2.id;
    CHECK(ids(unwrap(db->search_messages("hedgehog", only2))) == std::vector<std::string>{c});
    LOOM_REQUIRE_OK(db->delete_conv(conv2.id));
    CHECK(unwrap(db->search_messages("hedgehog")).hits.size() == 1);
    auto st = db->fts_status();
    CHECK(st.indexed == st.messages);
  }

  TEST_CASE("batch insert is indexed") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "chatadhd.db");
    auto conv = unwrap(db->create_conv());
    std::vector<BatchMessage> batch;
    for (int i = 0; i < 300; ++i) {
      BatchMessage b;
      b.text = "bulk message " + std::to_string(i) + (i == 150 ? " needle" : "");
      batch.push_back(b);
    }
    unwrap(db->batch_create_msgs(conv.id, batch));
    auto r = unwrap(db->search_messages("needle"));
    REQUIRE(r.hits.size() == 1);
    CHECK(r.hits[0].message.text == "bulk message 150 needle");
    SearchOptions lim;
    lim.limit = 10;
    CHECK(unwrap(db->search_messages("bulk", lim)).hits.size() == 10);
  }

  TEST_CASE("external writer (other connection, e.g. Python) is reconciled") {
    fsutil::TempDir td;
    auto path = td.path() / "chatadhd.db";
    auto db = open_db(path);
    auto conv = unwrap(db->create_conv());
    std::string a = add(*db, conv.id, "original apple text");
    CHECK(unwrap(db->search_messages("apple")).hits.size() == 1);
    {
      // A second connection without any Loom knowledge: insert, edit, delete.
      auto ext = unwrap(sql::Connection::open(path));
      LOOM_REQUIRE_OK(ext.run(
          "INSERT INTO messages (id, conv_id, role, text, created) VALUES ('m_ext000000001', ?, 'user', "
          "'external banana', '2030-01-01T00:00:00Z')",
          conv.id));
      LOOM_REQUIRE_OK(ext.run("UPDATE messages SET text = 'edited cherry' WHERE id = ?", a));
    }
    CHECK(unwrap(db->search_messages("apple")).hits.empty());
    CHECK(unwrap(db->search_messages("cherry")).hits.size() == 1);
    CHECK(unwrap(db->search_messages("banana")).hits.size() == 1);
    {
      auto ext = unwrap(sql::Connection::open(path));
      LOOM_REQUIRE_OK(ext.run("DELETE FROM messages WHERE id = 'm_ext000000001'"));
    }
    CHECK(unwrap(db->search_messages("banana")).hits.empty());
  }

  TEST_CASE("self-heal: deleted, corrupted and stale index files") {
    fsutil::TempDir td;
    auto path = td.path() / "chatadhd.db";
    std::string a;
    {
      auto db = open_db(path);
      auto conv = unwrap(db->create_conv());
      a = add(*db, conv.id, "durable kiwi");
      CHECK(unwrap(db->search_messages("kiwi")).hits.size() == 1);
    }
    // 1) Index deleted: rebuilt from messages on open.
    for (const char* s : {"", "-wal", "-shm"}) std::filesystem::remove(td.path() / (std::string("chatadhd.fts.db") + s));
    {
      auto db = open_db(path);
      CHECK(unwrap(db->search_messages("kiwi")).hits.size() == 1);
    }
    // 2) Index replaced with garbage: detected and rebuilt.
    for (const char* s : {"-wal", "-shm"}) std::filesystem::remove(td.path() / (std::string("chatadhd.fts.db") + s));
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "chatadhd.fts.db", std::string(8192, 'Z')));
    {
      auto db = open_db(path);
      CHECK(db->fts_status().available);
      CHECK(unwrap(db->search_messages("kiwi")).hits.size() == 1);
    }
    // 3) Main DB vacuumed + messages changed while index was offline.
    {
      auto ext = unwrap(sql::Connection::open(path));
      LOOM_REQUIRE_OK(ext.run("UPDATE messages SET text = 'durable mango' WHERE id = ?", a));
      LOOM_REQUIRE_OK(ext.exec("VACUUM"));
    }
    {
      auto db = open_db(path);
      CHECK(unwrap(db->search_messages("kiwi")).hits.empty());
      CHECK(unwrap(db->search_messages("mango")).hits.size() == 1);
      LOOM_REQUIRE_OK(db->fts_rebuild());
      CHECK(unwrap(db->search_messages("mango")).hits.size() == 1);
      CHECK(db->fts_status().rebuilds >= 1);
    }
  }

  TEST_CASE("LIKE fallback when FTS is disabled or requested") {
    fsutil::TempDir td;
    DbOptions o;
    o.enable_fts = false;
    auto db = open_db(td.path() / "chatadhd.db", o);
    CHECK(!db->fts_status().available);
    CHECK(!std::filesystem::exists(td.path() / "chatadhd.fts.db"));
    auto conv = unwrap(db->create_conv());
    std::string a = add(*db, conv.id, "Searching 100% of the_text with LIKE");
    add(*db, conv.id, "unrelated");
    auto r = unwrap(db->search_messages("like 100%"));
    CHECK(r.mode == "like");
    CHECK(ids(r) == std::vector<std::string>{a});
    CHECK(r.hits[0].snippet.find("[") != std::string::npos);
    CHECK(unwrap(db->search_messages("the_text")).hits.size() == 1);
    CHECK(unwrap(db->search_messages("theXtext")).hits.empty());  // '_' is literal
    SearchOptions f;
    f.mode = FtsMode::Fts5;
    CHECK(!db->search_messages("x", f));
    CHECK(unwrap(db->search_messages("   ")).hits.empty());
  }
}
