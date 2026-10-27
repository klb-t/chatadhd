// Import as graph nodes (IMPORT_JAKO_PLIKI_W_GRAFIE P1, src/import/import_graph.h): every import writes nodes for the file,
// the export, its entries, conversations and attachments; the counters of the independent oracle
// tests/fixtures/exports/EXPECTED.json equal the number of nodes by class; importing the same bytes again finds the same nodes;
// the summary is a sentence from the templates of runtime/import_graph.pack, never raw JSON; the graph area "Importy" shows the
// export with its conversations.
#include <doctest/doctest.h>

#include <map>
#include <string>

#include "graph/projection.h"
#include "import/import_graph.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

std::filesystem::path xfix(const std::string& name) { return std::filesystem::path(LOOM_TEST_FIXTURES) / "exports" / name; }
std::filesystem::path ifix(const std::string& name) { return std::filesystem::path(LOOM_TEST_FIXTURES) / "import" / name; }

const Json& expected() {
  static Json j = [] {
    auto txt = fsutil::read_file(xfix("EXPECTED.json"));
    REQUIRE(txt.has_value());
    auto p = json::parse(*txt);
    REQUIRE(p.has_value());
    return *p;
  }();
  return j;
}

struct Fx {
  fsutil::TempDir td;
  std::unique_ptr<Database> db;
  EventBus bus;
  std::unique_ptr<BlobStore> blobs;
  std::unique_ptr<ProvenanceStore> prov;
  std::unique_ptr<ConversationImporter> imp;
  Fx() {
    db = open_db(td.path() / "chatadhd.db");
    blobs = std::make_unique<BlobStore>(td.path() / "blobs", *db);
    prov = std::make_unique<ProvenanceStore>(*db);
    imp = std::make_unique<ConversationImporter>(*db, bus, blobs.get(), prov.get());
  }
  ImportResult run(const std::filesystem::path& p, ExportMode mode = ExportMode::On, bool force = false) {
    ImportOptions o;
    o.export_mode = mode;
    o.force = force;
    return unwrap(imp->import_file(p, o));
  }
  // Rows of the nodes table that the import graph wrote or adopted, by class.
  std::map<std::string, std::int64_t> tally() {
    std::map<std::string, std::int64_t> out;
    auto lock = db->lock();
    auto st = unwrap(db->conn().prepare(
        "SELECT json_extract(metadata,'$.import_graph.class'), COUNT(*) FROM nodes WHERE json_type(metadata,'$.import_graph')='object' "
        "GROUP BY 1"));
    while (unwrap(st.step())) out[st.get_text(0)] = st.get_int(1);
    return out;
  }
  std::int64_t total_nodes() {
    auto lock = db->lock();
    return unwrap(db->conn().query_int("SELECT COUNT(*) FROM nodes")).value_or(-1);
  }
};

std::int64_t n_of(const Json& j, const char* k) { return json::get_int(j, k, 0); }

void no_raw_json(const std::string& text) {
  CHECK(!text.empty());
  for (const char ch : {'{', '}', '[', ']', '"'}) {
    INFO("summary: " << text);
    CHECK(text.find(ch) == std::string::npos);
  }
}

}  // namespace

TEST_SUITE("import_graph") {
  TEST_CASE("oracle: counters of EXPECTED.json equal the import nodes by class, for every fixture") {
    std::int64_t fixtures = 0;
    for (const auto& item : expected().items()) {
      const std::string name = item.key();
      const Json want = item.value();
      const Json counts = want.value("counts", Json::object());
      SUBCASE(name.c_str()) {
        Fx fx;
        const ImportResult r = fx.run(xfix(name));
        REQUIRE(r.import_graph.is_object());
        const Json g = r.import_graph;
        const Json nodes = g.value("nodes", Json::object());
        INFO(name << " import_graph: " << g.dump());
        CHECK(n_of(nodes, "conversation") == n_of(counts, "conversation"));
        CHECK(n_of(nodes, "attachment") == n_of(counts, "attachment"));
        CHECK(n_of(nodes, "export") == 1);
        if (want.contains("members")) {
          CHECK(n_of(nodes, "archive_member") == want["members"].get<std::int64_t>());
          CHECK(n_of(nodes, "zip_archive") == 1);
        } else {
          CHECK(n_of(nodes, "archive_member") == 0);
          CHECK(n_of(nodes, "json_file") == 1);
        }
        const Json entities = g.value("entities", Json::object());
        CHECK(n_of(entities, "account") == n_of(counts, "account"));
        CHECK(n_of(entities, "project") == n_of(counts, "project"));
        // A memory is a described entity (Claude: memories.json) or a message of the conversation (ChatGPT: model_editable_context);
        // the second is already a message, not a separate node.
        std::int64_t memory_messages = 0;
        {
          auto lock = fx.db->lock();
          memory_messages = unwrap(fx.db->conn().query_int(
              "SELECT COUNT(*) FROM messages WHERE json_extract(metadata,'$.export.memory') = 1")).value_or(0);
        }
        CHECK(n_of(entities, "memory_note") + memory_messages == n_of(counts, "memory"));
        // The database holds exactly these nodes (plus the adopted entities), nothing more.
        auto tally = fx.tally();
        for (const auto& it : nodes.items()) CHECK(tally[it.key()] == it.value().get<std::int64_t>() + n_of(entities, it.key().c_str()));
        CHECK(g["created"].get<std::int64_t>() > 0);
        CHECK(g["reused"].get<std::int64_t>() == 0);
        // Summary: a sentence in Polish (preset) with the other language beside it, no raw JSON.
        const Json s = g["summary"];
        CHECK(s["lang"] == "pl");
        const std::string pl = s["text"].get<std::string>();
        no_raw_json(pl);
        no_raw_json(s["texts"]["en"].get<std::string>());
        CHECK(pl.find("Rozmowy: " + std::to_string(n_of(counts, "conversation"))) != std::string::npos);
        CHECK(pl.find("Importy") != std::string::npos);
        // Links: every conversation hangs on the export, every entry on the file.
        auto links = unwrap(fx.db->get_links(std::nullopt, std::string_view("part_of")));
        std::int64_t to_export = 0, to_file = 0;
        for (const auto& l : links) {
          if (l.dst == g["export_node"].get<std::string>()) ++to_export;
          if (l.dst == g["file_node"].get<std::string>()) ++to_file;
        }
        CHECK(to_export == n_of(counts, "conversation") + n_of(entities, "account") + n_of(entities, "project") +
                               n_of(entities, "memory_note") + n_of(entities, "document") + n_of(entities, "observation") +
                               n_of(entities, "graph_link"));
        CHECK(to_file == n_of(nodes, "archive_member"));
      }
      ++fixtures;
    }
    CHECK(fixtures >= 6);
  }

  TEST_CASE("re-import of the same bytes finds the same nodes (cache hit and forced re-import)") {
    Fx fx;
    const ImportResult first = fx.run(xfix("openai_2026_sharded.zip"));
    const auto before = fx.tally();
    const std::int64_t all_before = fx.total_nodes();
    const ImportResult again = fx.run(xfix("openai_2026_sharded.zip"));
    CHECK(again.already_imported);
    REQUIRE(again.import_graph.is_object());
    CHECK(again.import_graph["created"] == 0);
    CHECK(again.import_graph["reused"] == first.import_graph["created"]);
    CHECK(again.import_graph["file_node"] == first.import_graph["file_node"]);
    CHECK(again.import_graph["summary"]["text"].get<std::string>().find("odnaleziono te same węzły") != std::string::npos);
    CHECK(fx.tally() == before);
    CHECK(fx.total_nodes() == all_before);
    const ImportResult forced = fx.run(xfix("openai_2026_sharded.zip"), ExportMode::On, true);
    REQUIRE(forced.import_graph.is_object());
    CHECK(forced.import_graph["export_node"] == first.import_graph["export_node"]);
    const auto after = fx.tally();
    for (const char* cls : {"export", "zip_archive", "archive_member", "conversation", "attachment"}) CHECK(after.at(cls) == before.at(cls));
    // The forced copy's conversations are recorded on the same conversation nodes.
    auto lock = fx.db->lock();
    auto n = unwrap(fx.db->conn().query_int(
        "SELECT COUNT(*) FROM nodes WHERE json_extract(metadata,'$.import_graph.class')='conversation' AND "
        "json_array_length(json_extract(metadata,'$.import_graph.attrs.conv_ids'))=2"));
    CHECK(n.value_or(0) == 7);
  }

  TEST_CASE("every mode writes nodes: legacy flattening of a ZIP and plain files") {
    Fx fx;
    const ImportResult off = fx.run(xfix("openai_legacy.zip"), ExportMode::Off);
    REQUIRE(off.import_graph.is_object());
    CHECK(off.import_graph["export_node"] == "");
    CHECK(n_of(off.import_graph["nodes"], "zip_archive") == 1);
    CHECK(n_of(off.import_graph["nodes"], "conversation") == static_cast<std::int64_t>(off.conversations.size()));
    no_raw_json(off.import_graph["summary"]["text"].get<std::string>());
    const ImportResult md = fx.run(ifix("chat.md"));
    REQUIRE(md.import_graph.is_object());
    CHECK(n_of(md.import_graph["nodes"], "text_file") == 1);
    CHECK(n_of(md.import_graph["nodes"], "conversation") == 1);
    CHECK(md.import_graph["root_node"] == md.import_graph["file_node"]);
    CHECK(md.import_graph["summary"]["text"].get<std::string>().find("Plik z pliku") == std::string::npos);
  }

  TEST_CASE("the preset turns the projection off without touching the import") {
    Fx fx;
    Json preset = unwrap(RuntimeProfile::builtin("import_graph")).values();
    preset["enabled"] = false;
    ImportOptions o;
    o.export_mode = ExportMode::On;
    const ImportResult r = unwrap(fx.imp->import_file(xfix("openai_single_conversations.json"), o));
    const std::int64_t before = fx.total_nodes();
    auto off = unwrap(import_graph::project_with(*fx.db, xfix("openai_single_conversations.json"), r, preset));
    CHECK(off.is_null());
    CHECK(fx.total_nodes() == before);
  }

  TEST_CASE("graph area Importy: the export is a node, its conversations and entries hang under it") {
    Fx fx;
    const ImportResult r = fx.run(xfix("anthropic_2026_full.zip"));
    projection::Env env;
    env.root = fx.td.path();
    env.db = fx.db.get();
    env.lang = "pl";
    auto cat = unwrap(projection::Catalogue::open(env));
    auto top = unwrap(cat.children("area:imports", Json::object()));
    REQUIRE(top.nodes.size() == 1);
    const Json exp = top.nodes[0];
    CHECK(exp["kind"] == "export");
    CHECK(exp["address"] == "imp:" + r.import_graph["export_node"].get<std::string>().substr(2));
    auto under = unwrap(cat.children(exp["address"].get<std::string>(), Json::object()));
    std::map<std::string, std::int64_t> kinds;
    for (const auto& n : under.nodes) ++kinds[n["kind"].get<std::string>()];
    const Json counts = expected()["anthropic_2026_full.zip"]["counts"];
    CHECK(kinds["conversation"] == n_of(counts, "conversation"));
    CHECK(kinds["zip_archive"] == 1);
    CHECK(kinds["account"] == n_of(counts, "account"));
    CHECK(kinds["project"] == n_of(counts, "project"));
    // A conversation node is the same node as the conversation (conv:<id>) and shows its attachments under it.
    bool same_as_conv = false;
    for (const auto& n : under.nodes)
      if (n["kind"] == "conversation")
        for (const auto& a : n["same_as"])
          if (a.get<std::string>().rfind("conv:", 0) == 0) same_as_conv = true;
    CHECK(same_as_conv);
    // The area counts the import nodes by class.
    auto area = unwrap(cat.node("area:imports"));
    REQUIRE(area.has_value());
  }
}
