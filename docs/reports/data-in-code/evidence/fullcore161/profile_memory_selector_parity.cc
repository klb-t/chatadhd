// Same source compiles against frozen 161cc22 and the data-driven revision.
// Only pre-existing public calls are used; all fixture bytes are synthetic.
#include <iostream>
#include <memory>
#include "loom/memory_engine.h"
#include "loom/selector.h"
#include "loom/util/fs.h"

using namespace loom;
class FixtureEmbedding final : public EmbeddingProvider {
 public:
  bool fail = false;
  std::string model_id() const override { return "offline-fixed-embedding"; }
  Result<std::vector<std::vector<float>>> embed(const std::vector<std::string>& texts) override {
    if (fail) return Error(Errc::Unavailable, "saved fixture error");
    std::vector<std::vector<float>> out;
    for (const auto& text : texts)
      out.push_back({static_cast<float>(text.find("alpha") != std::string::npos),
                     static_cast<float>(text.find("beta") != std::string::npos),
                     static_cast<float>(text.find("graph") != std::string::npos)});
    return out;
  }
};
int main() {
  Json result = Json::object();
  const std::vector<std::string> corpus{
    "alpha beta graph graph", "alpha gamma", "beta delta", "a Ą Łódź _ 123", "completely unrelated", "alpha gamma"};
  Json selector_rows = Json::array();
  for (int tier : {SelectorEngine::kTierTfIdf, SelectorEngine::kTierKeyword, SelectorEngine::kTierEmbedding}) {
    auto embedder = std::make_shared<FixtureEmbedding>();
    SelectorEngine selector(tier, embedder);
    if (!selector.index(corpus)) return 1;
    for (const auto& query : {"alpha", "alpha beta", "graph", "gamma", "ŁÓDŹ a _", "123", "", "absent"}) {
      for (int count : {0, 1, 5, 20}) {
        Json hits = Json::array();
        for (const auto& hit : selector.search(query, count)) hits.push_back(hit.to_json());
        selector_rows.push_back(Json{{"tier", selector.tier()}, {"query", query}, {"top_k", count}, {"hits", hits}});
      }
    }
    embedder->fail = true;
    Json hits = Json::array();
    for (const auto& hit : selector.search("alpha", 5)) hits.push_back(hit.to_json());
    selector_rows.push_back(Json{{"tier", selector.tier()}, {"query", "provider-error"}, {"hits", hits}});
  }
  // A corpus above the historical vocabulary cap freezes selection/tie policy.
  std::string large;
  for (int i = 0; i < 5000; ++i) large += "term" + std::to_string(i) + " ";
  large += "zzzz";
  SelectorEngine cap(SelectorEngine::kTierTfIdf);
  if (!cap.index({large})) return 2;
  Json cap_hits = Json::array();
  for (const auto& hit : cap.search("term99 zzzz")) cap_hits.push_back(hit.to_json());
  result["selector"] = selector_rows;
  result["cap"] = cap_hits;

  fsutil::TempDir dir;
  Json nodes = Json::array();
  const auto node = [](std::string id, std::string content, Json parent, std::string type,
                       std::string created, bool active, double weight, Json tags, Json metadata) {
    return Json{{"id", id}, {"content", content}, {"parent_id", parent}, {"node_type", type},
                {"active", active}, {"depth", 0}, {"weight", weight}, {"tags", tags},
                {"created", created}, {"metadata", metadata}};
  };
  nodes.push_back(node("text", "ordinary alpha", nullptr, "text", "2020-01-01T00:00:00Z", true, 1, Json::array(), Json::object()));
  nodes.push_back(node("folder", "Project Ą", nullptr, "folder", "2020-01-02T00:00:00Z", true, 2.5, Json::array({"tag"}), Json::object()));
  nodes.push_back(node("file", "config file", "folder", "file", "2020-01-03T00:00:00Z", true, 1, Json::array({"omitted-tag"}), Json{{"path", "/synthetic/file"}}));
  nodes.push_back(node("dir", "directory", "folder", "dir", "2020-01-04T00:00:00Z", true, .25, Json::array(), Json{{"path", "/synthetic"}}));
  nodes.push_back(node("other", "unknown kind graph alpha {{content}}", "folder", "future_type", "2020-01-05T00:00:00Z", true, -1.25, Json::array({"a", "b"}), Json::object()));
  nodes.push_back(node("hidden", "hidden folder", nullptr, "folder", "2020-01-06T00:00:00Z", false, 1, Json::array(), Json::object()));
  nodes.push_back(node("hidden-child", "child of hidden", "hidden", "text", "2020-01-07T00:00:00Z", true, 1, Json::array(), Json::object()));
  nodes.push_back(node("orphan", "orphan beta", "missing", "text", "2020-01-08T00:00:00Z", true, 1, Json::array(), Json::object()));
  const auto path = dir.path() / "memory.json";
  if (!fsutil::write_file(path, json::dump(nodes))) return 3;
  MemoryEngine memory(path);
  Json contexts = Json::array();
  for (std::size_t limit : {0, 1, 15, 16000}) contexts.push_back(Json{{"limit", limit}, {"text", memory.get_active_context(limit)}});
  result["memory_context"] = contexts;
  result["memory_default_context"] = memory.get_active_context();
  result["memory_graph"] = memory.get_graph_data();
  Json roots = Json::array();
  for (const auto& n : memory.get_children()) roots.push_back(n.to_json());
  result["memory_roots"] = roots;
  Json matches = Json::array();
  for (const auto& q : {"alpha", "", "BETA", "absent"}) {
    for (int limit : {0, 1, 10}) {
      Json found = Json::array();
      for (const auto& n : memory.search(q, limit)) found.push_back(n.to_json());
      matches.push_back(Json{{"query", q}, {"limit", limit}, {"found", found}});
    }
  }
  result["memory_search"] = matches;
  std::cout << json::dump(result) << '\n';
}
