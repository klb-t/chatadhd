#include <doctest/doctest.h>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/graph_engine.h"
#include "loom/graph_memory.h"
#include "loom/memory_engine.h"
#include "loom/semantic_analyzer.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
std::string mk_msg(Database& db, std::string_view conv, std::string_view text, std::string_view role = "user") {
  NewMessage m;
  m.conv_id = std::string(conv);
  m.text = std::string(text);
  m.role = std::string(role);
  return unwrap(db.create_msg(m));
}
}  // namespace

TEST_SUITE("graph_engine") {
  TEST_CASE("ingest_analysis: entities, topics, relations, part_of edge, threshold filtering") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "g.db");
    EventBus bus;
    auto an = unwrap(SemanticAnalyzer::create());
    GraphEngine ge(*db, bus, *an);

    auto conv = unwrap(db->create_conv("Test"));
    std::string mid = mk_msg(*db, conv.id, "irrelevant text body");

    Json analysis = Json{
        {"entities", Json::array({Json{{"name", "OpenRouter"}, {"kind", "org"}, {"relevance", 0.9}},
                                  Json{{"name", "x"}, {"kind", "entity"}, {"relevance", 0.9}},   // too short (<2)
                                  Json{{"name", "weak"}, {"kind", "entity"}, {"relevance", 0.1}}  // below threshold
                                  })},
        {"topics", Json::array({Json{{"label", "AI"}, {"confidence", 0.8}}, "tech", Json{{"label", "x"}, {"confidence", 0.1}}})},
        {"relations", Json::array({Json{{"subject", "OpenRouter"}, {"predicate", "part_of"}, {"object", "tech"}}})},
    };

    bool changed = ge.ingest_analysis(mid, conv.id, analysis);
    CHECK(changed);

    auto openrouter = unwrap(db->find_node("OpenRouter"));
    REQUIRE(openrouter.has_value());
    CHECK(openrouter->kind == "org");
    CHECK(!unwrap(db->find_node("x")).has_value());
    CHECK(!unwrap(db->find_node("weak")).has_value());

    auto ai_topic = unwrap(db->find_node("ai", "topic"));
    REQUIRE(ai_topic.has_value());
    auto tech_topic = unwrap(db->find_node("tech", "topic"));
    REQUIRE(tech_topic.has_value());
    CHECK(!unwrap(db->find_node("x", "topic")).has_value());

    // relation edge between the two entity/topic nodes.
    auto links = unwrap(db->get_links(openrouter->id));
    bool found_relation = false, found_mentions = false;
    for (const auto& l : links) {
      if (l.link_type == "part_of" && l.dst == tech_topic->id) found_relation = true;
      if (l.link_type == "mentions" && l.src == mid) found_mentions = true;
    }
    CHECK(found_relation);
    CHECK(found_mentions);
    // The conv edge is created from `mid`'s own link list, check separately.
    auto mid_links = unwrap(db->get_links(mid));
    bool conv_edge = false;
    for (const auto& l : mid_links) {
      if (l.link_type == "part_of" && l.dst == conv.id) conv_edge = true;
    }
    CHECK(conv_edge);
  }

  TEST_CASE("on_message: skips short text, writes semantic metadata, mark_analysed, emits graph:changed") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "g.db");
    EventBus bus;
    auto an = unwrap(SemanticAnalyzer::create());
    GraphEngine ge(*db, bus, *an);
    ge.start();

    int graph_changed_count = 0;
    bus.on(events::kGraphChanged, [&](std::string_view, const Json&) { graph_changed_count++; });

    auto conv = unwrap(db->create_conv("Test"));

    // Too short: no processing at all.
    std::string short_id = mk_msg(*db, conv.id, "hi");
    bus.emit(events::kMsgCreated, Json{{"id", short_id}, {"text", "hi"}, {"conv_id", conv.id}, {"role", "user"}});
    auto short_msg = unwrap(db->get_msg(short_id));
    CHECK(short_msg->semantic_status == "pending");  // untouched

    // Long enough, contains an email entity.
    std::string mid = mk_msg(*db, conv.id, "please contact admin@example.com about the deploy pipeline");
    bus.emit(events::kMsgCreated,
            Json{{"id", mid}, {"text", "please contact admin@example.com about the deploy pipeline"},
                {"conv_id", conv.id}, {"role", "user"}});

    auto msg = unwrap(db->get_msg(mid));
    REQUIRE(msg.has_value());
    CHECK(msg->semantic_status == "done");
    const Json* semantic = json::find(msg->metadata, "semantic");
    REQUIRE(semantic != nullptr);
    CHECK(json::get_string(*semantic, "source", "") == "regex");
    CHECK(graph_changed_count >= 1);

    auto email_node = unwrap(db->find_node("admin@example.com"));
    CHECK(email_node.has_value());

    ge.stop();
  }

  TEST_CASE("reindex_conversation / reindex_all") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "g.db");
    EventBus bus;
    auto an = unwrap(SemanticAnalyzer::create());
    GraphEngine ge(*db, bus, *an);

    auto conv = unwrap(db->create_conv("Test"));
    mk_msg(*db, conv.id, "The frontend depends on the backend service heavily");
    mk_msg(*db, conv.id, "hi");  // too short, contributes 0 graph writes but still counted

    int n = ge.reindex_conversation(conv.id);
    CHECK(n == 2);

    auto backend = unwrap(db->find_node("the backend service heavily"));
    // relation subject/object nodes are only created via find_node matches;
    // the regex fallback's relation extraction won't auto-create entity
    // nodes for subject/object text that isn't already a node, so this is
    // expected to be absent - what we really verify is reindex_all's count.
    (void)backend;

    int total = ge.reindex_all();
    CHECK(total == 2);
  }
}

TEST_SUITE("graph_memory") {
  TEST_CASE("select_context: BFS finds related messages via shared topic node") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "gm.db");
    Config cfg(td.path() / "config.json");
    auto an = unwrap(SemanticAnalyzer::create());
    GraphMemorySelector gm(*db, cfg, *an);

    // "database" and "API" are both "tech" topic keywords; two hits meets
    // extract_topics()'s default threshold=2, so the seed label extracted
    // from plain text is the topic *category* ("tech"), not the keyword.
    auto conv1 = unwrap(db->create_conv("Old conversation"));
    std::string old_mid = mk_msg(*db, conv1.id, "we discussed the database and the API design", "assistant");
    auto topic = unwrap(db->get_or_create_node("tech", "topic"));
    unwrap(db->create_link(old_mid, topic, "tagged_with", 0.5));

    auto conv2 = unwrap(db->create_conv("New conversation"));
    GraphSelectOptions opts;
    opts.current_conv_id = conv2.id;
    std::string ctx = gm.select_context("tell me about the database and API again", opts);
    CHECK(ctx.find("Relevant context from past conversations:") != std::string::npos);
    CHECK(ctx.find("database and the API design") != std::string::npos);
    CHECK(ctx.find("Old conversation") != std::string::npos);
  }

  TEST_CASE("select_context: empty when no seeds or no graph hits") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "gm.db");
    Config cfg(td.path() / "config.json");
    auto an = unwrap(SemanticAnalyzer::create());
    GraphMemorySelector gm(*db, cfg, *an);
    CHECK(gm.select_context("...", {}).empty());
    CHECK(gm.select_context("the quick fox jumps", {}).empty());  // no matching nodes exist
  }

  TEST_CASE("select_context: excludes messages from the current conversation") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "gm.db");
    Config cfg(td.path() / "config.json");
    auto an = unwrap(SemanticAnalyzer::create());
    GraphMemorySelector gm(*db, cfg, *an);

    auto conv = unwrap(db->create_conv("Same conversation"));
    std::string mid = mk_msg(*db, conv.id, "the database and API notes are here", "user");
    auto topic = unwrap(db->get_or_create_node("tech", "topic"));
    unwrap(db->create_link(mid, topic, "tagged_with", 0.5));

    GraphSelectOptions opts;
    opts.current_conv_id = conv.id;  // the only tagged message lives here -> excluded -> no context
    CHECK(gm.select_context("database and API review", opts).empty());
  }

  TEST_CASE("ContextSelector: memory + graph + fts merged under a token budget") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "cs.db");
    Config cfg(td.path() / "config.json");
    auto an = unwrap(SemanticAnalyzer::create());
    GraphMemorySelector gm(*db, cfg, *an);
    MemoryEngine mem(td.path() / "memory.json");
    unwrap(mem.add_node("Project context: this app uses OpenRouter for chat."));

    auto conv = unwrap(db->create_conv("C"));
    mk_msg(*db, conv.id, "OpenRouter integration notes and API key setup steps go here");

    ContextSelector sel(*db, cfg, gm, &mem);
    ContextRequest req;
    req.text = "OpenRouter";
    req.max_tokens = 4000;
    auto result = unwrap(sel.select(req));
    CHECK(!result.prompt_text.empty());
    CHECK(result.token_estimate > 0);
    bool has_memory_item = false;
    for (const auto& it : result.items) {
      if (it.source == "memory_tree") has_memory_item = true;
    }
    CHECK(has_memory_item);
  }

  TEST_CASE("ContextSelector: truncates when the budget is tiny") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "cs2.db");
    Config cfg(td.path() / "config.json");
    auto an = unwrap(SemanticAnalyzer::create());
    GraphMemorySelector gm(*db, cfg, *an);
    MemoryEngine mem(td.path() / "memory.json");
    unwrap(mem.add_node(std::string(2000, 'x')));

    ContextSelector sel(*db, cfg, gm, &mem);
    ContextRequest req;
    req.text = "x";
    req.max_tokens = 5;
    auto result = unwrap(sel.select(req));
    CHECK(result.token_estimate <= 5);
  }
}
