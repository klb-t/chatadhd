#include <doctest/doctest.h>

#include "loom/memory_engine.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;

TEST_SUITE("memory_engine") {
  TEST_CASE("add / get / update / delete, depth tracking") {
    fsutil::TempDir dir;
    MemoryEngine mem(dir.path() / "memory.json");

    auto root = mem.add_node("root folder", std::nullopt, "folder");
    REQUIRE(root);
    auto child = mem.add_node("child text", *root, "text");
    REQUIRE(child);

    auto rn = mem.get_node(*root);
    REQUIRE(rn);
    CHECK(rn->depth == 0);
    auto cn = mem.get_node(*child);
    REQUIRE(cn);
    CHECK(cn->depth == 1);
    CHECK(cn->parent_id == *root);

    CHECK(mem.update_node(*child, Json{{"content", "updated"}, {"weight", 2.5}}).has_value());
    auto cn2 = mem.get_node(*child);
    REQUIRE(cn2);
    CHECK(cn2->content == "updated");
    CHECK(cn2->weight == 2.5);

    CHECK(!mem.update_node("does_not_exist", Json{{"content", "x"}}).has_value());

    CHECK(mem.delete_node(*child, false).has_value());
    CHECK(!mem.get_node(*child).has_value());
    CHECK(mem.get_node(*root).has_value());
  }

  TEST_CASE("recursive delete removes descendants") {
    fsutil::TempDir dir;
    MemoryEngine mem(dir.path() / "memory.json");
    auto a = *mem.add_node("a", std::nullopt, "folder");
    auto b = *mem.add_node("b", a, "folder");
    auto c = *mem.add_node("c", b, "text");
    CHECK(mem.delete_node(a, true).has_value());
    CHECK(!mem.get_node(a).has_value());
    CHECK(!mem.get_node(b).has_value());
    CHECK(!mem.get_node(c).has_value());
  }

  TEST_CASE("get_children sorts folders first, then by created") {
    fsutil::TempDir dir;
    MemoryEngine mem(dir.path() / "memory.json");
    auto t1 = *mem.add_node("text1", std::nullopt, "text");
    auto f1 = *mem.add_node("folder1", std::nullopt, "folder");
    auto t2 = *mem.add_node("text2", std::nullopt, "text");
    auto children = mem.get_children(std::nullopt);
    REQUIRE(children.size() == 3);
    CHECK(children[0].id == f1);
    CHECK((children[1].id == t1 && children[2].id == t2));
  }

  TEST_CASE("get_active_context formats folder/file/dir/text with weight and tags") {
    fsutil::TempDir dir;
    MemoryEngine mem(dir.path() / "memory.json");
    auto folder = *mem.add_node("Project", std::nullopt, "folder");
    (void)mem.add_node("some text", folder, "text", Json::object(), {"tag1", "tag2"});
    Json meta = Json{{"path", "/etc/foo"}};
    (void)mem.add_node("config file", folder, "file", meta);
    auto ctx = mem.get_active_context();
    CHECK(ctx.find("[Project]") != std::string::npos);
    CHECK(ctx.find("some text") != std::string::npos);
    CHECK(ctx.find("#tag1 #tag2") != std::string::npos);
    CHECK(ctx.find("FILE: config file (/etc/foo)") != std::string::npos);
  }

  TEST_CASE("inactive nodes are skipped in active context") {
    fsutil::TempDir dir;
    MemoryEngine mem(dir.path() / "memory.json");
    auto id = *mem.add_node("hidden", std::nullopt, "text");
    CHECK(mem.update_node(id, Json{{"active", false}}).has_value());
    auto ctx = mem.get_active_context();
    CHECK(ctx.find("hidden") == std::string::npos);
  }

  TEST_CASE("auto-tagging via SemanticAnalyzer on text nodes") {
    fsutil::TempDir dir;
    auto an = SemanticAnalyzer::create();
    REQUIRE(an);
    MemoryEngine mem(dir.path() / "memory.json", an->get());
    auto id = mem.add_node("We need to review the git deploy pipeline and the database backend server");
    REQUIRE(id);
    auto node = mem.get_node(*id);
    REQUIRE(node);
    bool has_tech = false;
    for (const auto& t : node->tags) {
      if (t == "tech") has_tech = true;
    }
    CHECK(has_tech);
  }

  TEST_CASE("persistence: atomic write + reload, and load survives a fresh instance") {
    fsutil::TempDir dir;
    auto path = dir.path() / "memory.json";
    {
      MemoryEngine mem(path);
      (void)mem.add_node("persisted node", std::nullopt, "text");
    }
    MemoryEngine mem2(path);
    auto all = mem2.get_all();
    REQUIRE(all.size() == 1);
    CHECK(all[0].content == "persisted node");
  }

  TEST_CASE("search: case-insensitive substring, insertion order") {
    fsutil::TempDir dir;
    MemoryEngine mem(dir.path() / "memory.json");
    (void)mem.add_node("Alpha Bravo");
    (void)mem.add_node("charlie DELTA");
    (void)mem.add_node("nothing here");
    auto results = mem.search("delta");
    REQUIRE(results.size() == 1);
    CHECK(results[0].content == "charlie DELTA");
  }

  TEST_CASE("get_graph_data: nodes + child edges") {
    fsutil::TempDir dir;
    MemoryEngine mem(dir.path() / "memory.json");
    auto p = *mem.add_node("Parent", std::nullopt, "folder");
    auto c = *mem.add_node("Child", p, "text");
    auto data = mem.get_graph_data();
    CHECK(data["nodes"].size() == 2);
    REQUIRE(data["edges"].size() == 1);
    CHECK(data["edges"][0]["src"] == p);
    CHECK(data["edges"][0]["dst"] == c);
  }
}
