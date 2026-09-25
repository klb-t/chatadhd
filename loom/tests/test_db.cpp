#include <doctest/doctest.h>

#include <atomic>
#include <set>
#include <thread>

#include "loom/db.h"
#include "loom/util/ids.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
std::string msg(Database& db, const std::string& conv, const std::string& text, const std::string& role = "user",
                std::optional<std::string> vg = std::nullopt, std::optional<std::string> parent = std::nullopt) {
  NewMessage m;
  m.conv_id = conv;
  m.text = text;
  m.role = role;
  m.version_group_id = std::move(vg);
  m.parent_id = std::move(parent);
  return unwrap(db.create_msg(m));
}

std::set<std::string> index_names(sql::Connection& c) {
  std::set<std::string> out;
  auto st = unwrap(c.prepare("SELECT name FROM sqlite_master WHERE type='index' AND name GLOB 'idx_*' AND name NOT GLOB 'idx_loom_*'"));
  while (unwrap(st.step())) out.insert(st.get_text(0));
  return out;
}
}  // namespace

TEST_SUITE("db") {
  TEST_CASE("open creates schema v4 and loom tables") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "nested" / "chatadhd.db");
    CHECK(unwrap(db->schema_version()) == 4);
    CHECK(unwrap(db->get_meta("loom_schema_version")) == std::optional<std::string>("1"));
    auto& c = db->conn();
    for (const char* t : {"_meta", "conversations", "messages", "nodes", "links", "loom_blobs", "loom_sources",
                          "loom_provenance", "loom_events", "loom_tasks", "loom_artifacts", "loom_relation_types"}) {
      INFO(t);
      CHECK(c.has_table(t));
    }
    auto idx = index_names(c);
    for (const char* i : {"idx_msg_conv", "idx_msg_parent", "idx_msg_vgroup", "idx_msg_status", "idx_msg_semantic",
                          "idx_nodes_kind", "idx_links_src", "idx_links_dst", "idx_links_type"}) {
      INFO(i);
      CHECK(idx.count(i) == 1);
    }
    CHECK(unwrap(c.query_text("PRAGMA journal_mode")) == std::optional<std::string>("wal"));
    CHECK(unwrap(c.query_int("PRAGMA foreign_keys")) == std::optional<std::int64_t>(1));
    // No triggers and no virtual tables in the main DB (Python compatibility).
    CHECK(unwrap(c.query_int("SELECT COUNT(*) FROM main.sqlite_master WHERE type='trigger'")) ==
          std::optional<std::int64_t>(0));
    CHECK(unwrap(c.query_int("SELECT COUNT(*) FROM main.sqlite_master WHERE sql LIKE '%VIRTUAL%'")) ==
          std::optional<std::int64_t>(0));
    // Reopen is idempotent.
    db.reset();
    auto db2 = open_db(td.path() / "nested" / "chatadhd.db");
    CHECK(unwrap(db2->schema_version()) == 4);
  }

  TEST_CASE("conversations CRUD") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "c.db");
    auto c1 = unwrap(db->create_conv());
    CHECK(c1.title == "New Chat");
    CHECK(is_generated_id(c1.id, "c_"));
    CHECK(c1.created == c1.updated);
    auto c2 = unwrap(db->create_conv("Second"));
    auto list = unwrap(db->list_convs());
    REQUIRE(list.size() == 2);
    CHECK(list[0].id == c2.id);  // updated DESC
    CHECK(list[0].source == "user");
    CHECK(list[0].metadata == Json::object());
    CHECK(unwrap(db->list_convs(1)).size() == 1);

    ConvPatch p;
    p.title = "Renamed";
    p.metadata = Json{{"pinned", true}};
    LOOM_REQUIRE_OK(db->update_conv(c1.id, p));
    auto got = unwrap(db->get_conv(c1.id));
    REQUIRE(got);
    CHECK(got->title == "Renamed");
    CHECK(got->metadata["pinned"] == true);
    CHECK(got->updated >= c1.updated);
    CHECK(unwrap(db->list_convs())[0].id == c1.id);  // touched -> first

    CHECK(!unwrap(db->get_conv("c_missing")));
    auto bad = ConvPatch::from_json(Json{{"bogus", 1}});
    CHECK(!bad);

    msg(*db, c1.id, "hello there");
    LOOM_REQUIRE_OK(db->delete_conv(c1.id));
    CHECK(!unwrap(db->get_conv(c1.id)));
    CHECK(unwrap(db->get_msgs(c1.id, true)).empty());
  }

  TEST_CASE("messages: create, get, statuses, update") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "m.db");
    auto conv = unwrap(db->create_conv("t"));
    NewMessage nm;
    nm.conv_id = conv.id;
    nm.text = "What is Loom?";
    nm.role = "user";
    nm.attachments = Json::array({"/tmp/a.png"});
    nm.metadata = Json{{"k", "v"}};
    std::string m1 = unwrap(db->create_msg(nm));
    CHECK(is_generated_id(m1, "m_"));
    auto got = unwrap(db->get_msg(m1));
    REQUIRE(got);
    CHECK(got->text == "What is Loom?");
    CHECK(got->status == "active");
    CHECK(got->version_num == 1);
    CHECK(got->version_group_id.has_value());
    CHECK(is_generated_id(*got->version_group_id, "vg_"));
    CHECK(got->attachments == Json::array({"/tmp/a.png"}));
    CHECK(got->metadata["k"] == "v");
    CHECK(got->semantic_status == "pending");
    CHECK(!got->model);
    CHECK(!got->parent_id);

    // Raw JSON columns use Python json.dumps formatting.
    auto raw = unwrap(db->conn().query_text("SELECT metadata FROM messages WHERE id=?", m1));
    CHECK(*raw == R"({"k": "v"})");
    auto raw_att = unwrap(db->conn().query_text("SELECT attachments FROM messages WHERE id=?", m1));
    CHECK(*raw_att == R"(["/tmp/a.png"])");

    std::string m2 = msg(*db, conv.id, "An answer", "assistant", std::nullopt, m1);
    auto msgs = unwrap(db->get_msgs(conv.id));
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[0].id == m1);
    CHECK(msgs[1].parent_id == std::optional<std::string>(m1));

    LOOM_REQUIRE_OK(db->set_msg_status(m2, "excluded"));
    CHECK(unwrap(db->get_msgs(conv.id)).size() == 1);
    CHECK(unwrap(db->get_msgs(conv.id, true)).size() == 2);
    auto bad = db->set_msg_status(m2, "weird");
    REQUIRE(!bad);
    CHECK(bad.error().code == Errc::InvalidArgument);

    MsgPatch mp;
    mp.weight = 2.0;
    mp.metadata = Json{{"note", "important"}};
    mp.model = std::optional<std::string>("x/model");
    LOOM_REQUIRE_OK(db->update_msg(m2, mp));
    auto m2r = unwrap(db->get_msg(m2));
    CHECK(m2r->weight == doctest::Approx(2.0));
    CHECK(m2r->metadata == Json{{"note", "important"}});
    CHECK(m2r->model == std::optional<std::string>("x/model"));
    CHECK(!db->update_msg(m2, MsgPatch{}));
    auto from_json = MsgPatch::from_json(Json{{"text", "t"}, {"parent_id", nullptr}});
    REQUIRE(from_json);
    CHECK(from_json->parent_id.has_value());
    CHECK(!from_json->parent_id->has_value());
    CHECK(!MsgPatch::from_json(Json{{"nope", 1}}));
    CHECK(!unwrap(db->get_msg("m_missing")));

    // Foreign key: message for an unknown conversation fails.
    NewMessage orphan;
    orphan.conv_id = "c_doesnotexist";
    orphan.text = "x";
    orphan.role = "user";
    CHECK(!db->create_msg(orphan));
  }

  TEST_CASE("versioning: edit_msg / restore_version / get_versions") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "v.db");
    auto conv = unwrap(db->create_conv("t"));
    std::string m1 = msg(*db, conv.id, "first draft");
    auto e1 = unwrap(db->edit_msg(m1, "second draft"));
    REQUIRE(e1);
    auto e2 = unwrap(db->edit_msg(*e1, "third draft"));
    REQUIRE(e2);
    CHECK(!unwrap(db->edit_msg("m_missing", "x")));

    auto orig = unwrap(db->get_msg(m1));
    auto vers = unwrap(db->get_versions(*orig->version_group_id));
    REQUIRE(vers.size() == 3);
    CHECK(vers[0].version_num == 1);
    CHECK(vers[1].version_num == 2);
    CHECK(vers[2].version_num == 3);
    CHECK(vers[0].status == "version");
    CHECK(vers[1].status == "version");
    CHECK(vers[2].status == "active");
    CHECK(vers[2].text == "third draft");
    auto active = unwrap(db->get_msgs(conv.id));
    REQUIRE(active.size() == 1);
    CHECK(active[0].id == *e2);

    CHECK(unwrap(db->restore_version(m1)));
    vers = unwrap(db->get_versions(*orig->version_group_id));
    CHECK(vers[0].status == "active");
    CHECK(vers[2].status == "version");
    CHECK(!unwrap(db->restore_version("m_missing")));
  }

  TEST_CASE("batch_create_msgs skips blanks and chunks transactions") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "b.db");
    auto conv = unwrap(db->create_conv("bulk"));
    std::vector<BatchMessage> batch;
    for (int i = 0; i < 2500; ++i) {
      BatchMessage b;
      b.role = i % 2 ? "assistant" : "user";
      b.text = (i % 500 == 7) ? "   \n\t" : "message number " + std::to_string(i);
      batch.push_back(std::move(b));
    }
    batch.push_back(BatchMessage::from_json(Json{{"role", "user"}, {"content", "from content key"}}));
    batch.push_back(BatchMessage::from_json(Json{{"role", "user"}, {"text", nullptr}}));
    int n = unwrap(db->batch_create_msgs(conv.id, batch, 1000));
    CHECK(n == 2500 - 5 + 1);
    auto msgs = unwrap(db->get_msgs(conv.id));
    REQUIRE(msgs.size() == static_cast<std::size_t>(n));
    CHECK(msgs[0].text == "message number 0");  // insertion order kept
    CHECK(msgs.back().text == "from content key");
    std::set<std::string> vgs;
    for (const auto& m : msgs) {
      vgs.insert(*m.version_group_id);
      CHECK(m.semantic_status == "pending");
      CHECK(m.created == msgs[0].created);  // one timestamp per batch call (Python)
    }
    CHECK(vgs.size() == msgs.size());
    CHECK(unwrap(db->count_pending_semantic()) == n);
  }

  TEST_CASE("semantic queue: get_unanalysed_msgs / mark_analysed") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "s.db");
    auto conv = unwrap(db->create_conv());
    std::string shortm = msg(*db, conv.id, "too short");
    std::string longm = msg(*db, conv.id, "this message is definitely long enough");
    std::string excl = msg(*db, conv.id, "this excluded message is long enough too");
    LOOM_REQUIRE_OK(db->set_msg_status(excl, "excluded"));
    auto pending = unwrap(db->get_unanalysed_msgs());
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].id == longm);
    CHECK(unwrap(db->count_pending_semantic()) == 3);  // counts every pending row

    Json analysis{{"source", "llm"},
                  {"entities", Json::array({Json{{"name", "Loom"}}})},
                  {"topics", Json::array({"tech", "ai"})},
                  {"summary", std::string(300, 'x')},
                  {"sentiment", "positive"}};
    LOOM_REQUIRE_OK(db->mark_analysed(longm, analysis));
    auto m = unwrap(db->get_msg(longm));
    CHECK(m->semantic_status == "done");
    CHECK(m->metadata["semantic_source"] == "llm");
    CHECK(m->metadata["entity_count"] == 1);
    CHECK(m->metadata["topic_count"] == 2);
    CHECK(m->metadata["summary"].get<std::string>().size() == 200);
    CHECK(m->metadata["sentiment"] == "positive");
    LOOM_REQUIRE_OK(db->mark_analysed(shortm, Json{{"source", "error"}}));
    auto s = unwrap(db->get_msg(shortm));
    CHECK(s->metadata["semantic_source"] == "error");
    CHECK(s->metadata["entity_count"] == 0);
    CHECK(s->metadata["sentiment"] == "neutral");
    LOOM_REQUIRE_OK(db->mark_analysed("m_missing", analysis));  // no-op
    CHECK(unwrap(db->get_unanalysed_msgs()).empty());
  }

  TEST_CASE("links upsert and queries") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "l.db");
    std::string l1 = unwrap(db->create_link("m_a", "n_b", "mentions", 0.5));
    std::string l2 = unwrap(db->create_link("m_a", "n_b", "mentions", 0.9, Json{{"x", 1}}));
    CHECK(l1 == l2);
    auto links = unwrap(db->get_links());
    REQUIRE(links.size() == 1);
    CHECK(links[0].weight == doctest::Approx(0.9));
    CHECK(links[0].metadata == Json{{"x", 1}});
    std::string l3 = unwrap(db->create_link("m_a", "n_b", "tagged_with"));
    CHECK(l3 != l1);
    unwrap(db->create_link("n_c", "m_a"));
    CHECK(unwrap(db->get_links("m_a")).size() == 3);
    CHECK(unwrap(db->get_links("m_a", "mentions")).size() == 1);
    CHECK(unwrap(db->get_links(std::nullopt, "related")).size() == 1);
    CHECK(unwrap(db->get_links("n_c")).at(0).link_type == "related");
    LOOM_REQUIRE_OK(db->delete_link(l3));
    CHECK(unwrap(db->get_links("m_a")).size() == 2);
  }

  TEST_CASE("nodes: create/find/get_or_create/update/delete") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "n.db");
    std::string n1 = unwrap(db->create_node("Loom", "concept"));
    CHECK(is_generated_id(n1, "n_"));
    NodeOptions o;
    o.node_id = "n_fixed";
    o.tags = Json::array({"a"});
    o.content = "c";
    CHECK(unwrap(db->create_node("Fixed", "entity", o)) == "n_fixed");
    CHECK(unwrap(db->create_node("Other label", "entity", o)) == "n_fixed");  // INSERT OR IGNORE
    CHECK(unwrap(db->get_node("n_fixed"))->label == "Fixed");
    CHECK(unwrap(db->get_node("n_fixed"))->tags == Json::array({"a"}));
    CHECK(unwrap(db->find_node("Loom"))->id == n1);
    CHECK(unwrap(db->find_node("Loom", "concept"))->id == n1);
    CHECK(!unwrap(db->find_node("Loom", "topic")));
    CHECK(unwrap(db->get_or_create_node("Loom", "concept")) == n1);
    std::string t = unwrap(db->get_or_create_node("tech", "topic"));
    CHECK(t != n1);
    CHECK(unwrap(db->list_nodes()).size() == 3);
    CHECK(unwrap(db->list_nodes("topic")).size() == 1);
    CHECK(unwrap(db->list_nodes(std::nullopt, 2)).size() == 2);
    NodePatch np;
    np.content = "a kernel";
    np.metadata = Json{{"w", 2}};
    LOOM_REQUIRE_OK(db->update_node(n1, np));
    CHECK(unwrap(db->get_node(n1))->content == "a kernel");
    unwrap(db->create_link("m_x", n1, "mentions"));
    unwrap(db->create_link(n1, t, "related"));
    LOOM_REQUIRE_OK(db->delete_node(n1));
    CHECK(!unwrap(db->get_node(n1)));
    CHECK(unwrap(db->get_links()).empty());
  }

  TEST_CASE("get_graph_data shape matches Python") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "g.db");
    auto conv = unwrap(db->create_conv());
    std::string m1 = msg(*db, conv.id, "A question about the Loom kernel design?");
    std::string m2 = msg(*db, conv.id, "answer", "assistant", std::nullopt, m1);
    std::string n = unwrap(db->create_node("Loom", "concept"));
    unwrap(db->create_link(m1, n, "mentions", 0.8));
    auto g = unwrap(db->get_graph_data(conv.id));
    REQUIRE(g["nodes"].size() == 3);
    CHECK(g["nodes"][0]["id"] == m1);
    CHECK(g["nodes"][0]["label"] == "A question about the Loom");  // text[:25]
    CHECK(g["nodes"][0]["kind"] == "message");
    CHECK(g["nodes"][0]["type"] == "user");
    CHECK(g["nodes"][0].contains("version_group"));
    CHECK(g["nodes"][2]["kind"] == "concept");
    CHECK(g["nodes"][2]["status"] == "active");
    REQUIRE(g["edges"].size() == 2);
    CHECK(g["edges"][0] == Json{{"src", m1}, {"dst", m2}, {"type", "reply"}, {"weight", 1.0}});
    CHECK(g["edges"][1]["type"] == "mentions");
    auto g2 = unwrap(db->get_graph_data());
    CHECK(g2["nodes"].size() == 1);
  }

  TEST_CASE("migration from legacy v0 schema (missing columns, tables, indexes)") {
    fsutil::TempDir td;
    auto path = td.path() / "legacy.db";
    {
      auto c = unwrap(sql::Connection::open(path));
      LOOM_REQUIRE_OK(c.exec(
          "CREATE TABLE conversations (id TEXT PRIMARY KEY, title TEXT NOT NULL, created TEXT NOT NULL, "
          "updated TEXT NOT NULL, metadata TEXT NOT NULL DEFAULT '{}');"
          "CREATE TABLE messages (id TEXT PRIMARY KEY, conv_id TEXT NOT NULL, parent_id TEXT, role TEXT NOT NULL, "
          "text TEXT NOT NULL, model TEXT, version_num INTEGER NOT NULL DEFAULT 1, weight REAL NOT NULL DEFAULT 1.0, "
          "attachments TEXT NOT NULL DEFAULT '[]', metadata TEXT NOT NULL DEFAULT '{}', created TEXT NOT NULL);"
          "CREATE TABLE links (id TEXT PRIMARY KEY, src TEXT NOT NULL, dst TEXT NOT NULL, weight REAL NOT NULL "
          "DEFAULT 1.0, metadata TEXT NOT NULL DEFAULT '{}', created TEXT NOT NULL);"
          "INSERT INTO conversations VALUES ('c_000000000001', 'Old chat', '2024-01-01T00:00:00Z', "
          "'2024-01-01T00:00:00Z', '{}');"
          "INSERT INTO messages (id, conv_id, role, text, created) VALUES ('m_000000000001', 'c_000000000001', "
          "'user', 'legacy message text here', '2024-01-01T00:00:01Z');"
          "INSERT INTO links VALUES ('l_000000000001', 'm_000000000001', 'x', 1.0, '{}', '2024-01-01T00:00:02Z');"));
    }
    auto db = open_db(path);
    auto& c = db->conn();
    CHECK(c.has_table("_meta"));
    CHECK(c.has_table("nodes"));
    CHECK(c.has_column("conversations", "source"));
    CHECK(c.has_column("messages", "status"));
    CHECK(c.has_column("messages", "version_group_id"));
    CHECK(c.has_column("messages", "semantic_status"));
    CHECK(c.has_column("links", "link_type"));
    CHECK(unwrap(db->schema_version()) == 4);
    CHECK(index_names(c).size() == 9);
    // Legacy rows got column defaults and are fully usable.
    auto conv = unwrap(db->get_conv("c_000000000001"));
    CHECK(conv->source == "user");
    auto msgs = unwrap(db->get_msgs("c_000000000001"));
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0].status == "active");
    CHECK(msgs[0].semantic_status == "pending");
    CHECK(!msgs[0].version_group_id);
    CHECK(unwrap(db->get_links())[0].link_type == "related");
    // Editing a legacy message (NULL version group) starts a fresh group.
    auto edited = unwrap(db->edit_msg("m_000000000001", "new text"));
    REQUIRE(edited);
    CHECK(unwrap(db->get_msgs("c_000000000001")).size() == 2);
  }

  TEST_CASE("migration when an index cannot be created is skipped, not fatal") {
    fsutil::TempDir td;
    auto path = td.path() / "weird.db";
    {
      auto c = unwrap(sql::Connection::open(path));
      // nodes without `kind`: ensure_index must skip idx_nodes_kind.
      LOOM_REQUIRE_OK(c.exec("CREATE TABLE nodes (id TEXT PRIMARY KEY, label TEXT NOT NULL, created TEXT NOT NULL)"));
    }
    auto db = open_db(path);
    CHECK(index_names(db->conn()).count("idx_nodes_kind") == 0);
    CHECK(index_names(db->conn()).count("idx_msg_conv") == 1);
  }

  TEST_CASE("concurrent writers are serialised") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "t.db");
    auto conv = unwrap(db->create_conv("threads"));
    std::atomic<int> failures{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 6; ++t) {
      ts.emplace_back([&, t] {
        for (int i = 0; i < 40; ++i) {
          NewMessage m;
          m.conv_id = conv.id;
          m.text = "thread " + std::to_string(t) + " msg " + std::to_string(i);
          m.role = "user";
          if (!db->create_msg(m)) failures++;
          if (!db->create_link("m_" + std::to_string(t), "n_" + std::to_string(i % 5), "mentions")) failures++;
          if (!db->list_convs()) failures++;
        }
      });
    }
    for (auto& t : ts) t.join();
    CHECK(failures.load() == 0);
    CHECK(unwrap(db->get_msgs(conv.id)).size() == 240);
    CHECK(unwrap(db->get_links()).size() == 30);
  }

  TEST_CASE("vacuum and close") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "vac.db");
    auto conv = unwrap(db->create_conv());
    msg(*db, conv.id, "x");
    LOOM_REQUIRE_OK(db->vacuum());
    db->close();
    db->close();  // idempotent
  }
}
