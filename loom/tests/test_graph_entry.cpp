#include <doctest/doctest.h>

#include "loom/graph_entry.h"
#include "loom/loom.h"
#include "loom/runtime_profile.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

const Json* area(const Json& view, const std::string& id) {
  for (const auto& a : view["areas"]) if (a["id"] == id) return &a;
  return nullptr;
}
const Json* seed(const Json& view, const std::string& id) {
  for (const auto& a : view["seed"]) if (a["id"] == id) return &a;
  return nullptr;
}
void write_overlay(const std::filesystem::path& dir, const Json& overrides) {
  LOOM_REQUIRE_OK(fsutil::ensure_dir(dir / "profiles"));
  LOOM_REQUIRE_OK(fsutil::write_file(dir / "profiles/graph_entry.pack",
      json::dump(Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "graph_entry"}, {"overrides", overrides}})));
}

}  // namespace

TEST_SUITE("graph_entry") {
  TEST_CASE("empty data dir: the seed and calibration areas exist, unavailable counts are unknown not zero") {
    fsutil::TempDir td;
    graph_entry::Env env;  // no database at all
    env.data_dir = td.path();
    auto view = unwrap(graph_entry::compute(env));
    CHECK(view["schema"] == "loom.graph_entry/1");
    CHECK(view["read_only"] == true);
    REQUIRE(area(view, "data_corpora"));
    CHECK(view["areas"].size() >= 14);
    const Json& corpora = (*area(view, "data_corpora"))["counts"];
    CHECK(corpora["db.conversations"]["known"] == false);
    CHECK(corpora["db.conversations"].contains("value") == false);
    CHECK(corpora["db.messages"]["known"] == false);
    // Counts that come from data stay known with no database.
    CHECK((*area(view, "ontology"))["counts"]["classes"]["known"] == true);
    CHECK((*area(view, "methods"))["counts"]["methods.registry"]["known"] == false);
    // Zero seed (R52): computed from packs.
    for (const char* id : {"seed:self_model", "seed:ontology", "seed:methods", "seed:layers", "seed:party"})
      CHECK(seed(view, id) != nullptr);
    const Json& party = (*seed(view, "seed:party"))["node"];
    CHECK(party["class"] == "person");
    REQUIRE(party["slots"].size() > 0);
    for (const auto& s : party["slots"]) {
      CHECK(s["state"] == "unknown");
      CHECK(s["value"].is_null());
    }
    CHECK((*seed(view, "seed:layers"))["scope_chain"].size() > 3);
    CHECK((*seed(view, "seed:ontology"))["classes"].size() > 3);
    CHECK(view["issues"].empty());
    // Polish labels come from data too.
    auto pl = unwrap(graph_entry::compute(env, Json{{"lang", "pl"}}));
    CHECK((*area(pl, "self_model"))["label"] == "Samomodel");
  }

  TEST_CASE("opened but empty database reports real zeros; populated counts match the registries") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "e.db");
    graph_entry::Env env;
    env.db = db.get();
    env.data_dir = td.path();
    auto empty = unwrap(graph_entry::compute(env));
    const Json& c0 = (*area(empty, "data_corpora"))["counts"];
    CHECK(c0["db.conversations"]["known"] == true);
    CHECK(c0["db.conversations"]["value"] == 0);
      CHECK(c0["db.recognition"]["value"] == 0);

    auto conv = unwrap(db->create_conv("one"));
    unwrap(db->create_conv("two"));
    NewMessage m;
    m.conv_id = conv.id;
    m.text = "a";
    auto m1 = unwrap(db->create_msg(m));
    m.text = "b";
    unwrap(db->create_msg(m));
    LOOM_REQUIRE_OK(db->set_msg_status(m1, "excluded"));
    NodeOptions opt;
    opt.metadata = Json{{"recognition", {{"status", "unrecognized"}}}};
    unwrap(db->create_node("frag", "import:unrecognized", opt));
    opt.metadata = Json{{"recognition", {{"status", "partial"}}}};
    unwrap(db->create_node("frag2", "import:unrecognized", opt));

    auto view = unwrap(graph_entry::compute(env));
    const Json& c = (*area(view, "data_corpora"))["counts"];
    CHECK(c["db.conversations"]["value"] == 2);
    CHECK(c["db.messages"]["value"] == 2);
    CHECK(c["db.messages"]["by"]["active"] == 1);
    CHECK(c["db.messages"]["by"]["excluded"] == 1);
    CHECK(c["db.recognition"]["value"] == 2);
    CHECK(c["db.recognition"]["by"]["unrecognized"] == 1);
    CHECK(c["db.recognition"]["by"]["partial"] == 1);
    CHECK((*area(view, "resource_economy"))["counts"]["usage.ledger"]["known"] == true);
    CHECK((*area(view, "resource_economy"))["counts"]["economy.redundant_bytes"]["known"] == true);
    // The view never writes: a second computation changes nothing.
    CHECK(unwrap(graph_entry::compute(env)) == view);
  }

  TEST_CASE("the user's overlay reorders, hides and adds areas; seed elements can be hidden") {
    fsutil::TempDir td;
    write_overlay(td.path(), Json{{"areas", {
        {"knowledge", {{"hidden", true}}},
        {"self_model", {{"order", 99}}},
        {"my_area", {{"order", 0}, {"label", {{"en", "Mine"}}}, {"axes", {{"about", "party"}, {"kind", "data"}, {"time", "plan"}}},
                     {"bindings", {"data.classes"}}}}}},
        {"seed", {{"elements", {{"party", {{"hidden", true}}}}}}}});
    graph_entry::Env env;
    env.data_dir = td.path();
    auto view = unwrap(graph_entry::compute(env));
    CHECK(area(view, "knowledge") == nullptr);
    REQUIRE(area(view, "my_area"));
    CHECK((*area(view, "my_area"))["label"] == "Mine");
    CHECK((*area(view, "my_area"))["counts"]["classes"]["known"] == true);
    CHECK(view["areas"][0]["id"] == "my_area");
    CHECK(view["areas"][view["areas"].size() - 1 - 0]["origin"] == "cell");  // generated cells come last
    CHECK(seed(view, "seed:party") == nullptr);
    CHECK(seed(view, "seed:ontology") != nullptr);
    auto all = unwrap(graph_entry::compute(env, Json{{"include_hidden", true}}));
    REQUIRE(area(all, "knowledge"));
    CHECK((*area(all, "knowledge"))["hidden"] == true);
    CHECK((*seed(all, "seed:party"))["hidden"] == true);
  }

  TEST_CASE("the area space is generated: a provider binding on an unoccupied cell appears without an area") {
    fsutil::TempDir td;
    graph_entry::Env env;
    env.data_dir = td.path();
    // Shipped data already has one: kb.runs fills world/knowledge/history.
    auto view = unwrap(graph_entry::compute(env));
    const Json* cell = area(view, "cell:world.knowledge.history");
    REQUIRE(cell);
    CHECK((*cell)["origin"] == "cell");
    CHECK((*cell)["label"] == "World / Knowledge / History");
    // Via overlay + a custom provider registered in code.
    write_overlay(td.path(), Json{{"bindings", {{"probe", {{"provider", "test.probe"}, {"as", "probes"},
        {"cells", {{{"about", "party"}, {"kind", "resource"}, {"time", "history"}}}}}}}}});
    graph_entry::Providers custom;
    custom.add("test.probe", [](const graph_entry::Context&, const Json&) { return Json{{"", graph_entry::known(7)}}; });
    auto v2 = unwrap(graph_entry::compute(env, Json::object(), &custom));
    const Json* probe = area(v2, "cell:party.resource.history");
    REQUIRE(probe);
    CHECK((*probe)["counts"]["probes"]["value"] == 7);
    // An unknown provider is reported, not invented.
    auto v3 = unwrap(graph_entry::compute(env));
    const Json* missing = area(v3, "cell:party.resource.history");
    REQUIRE(missing);
    CHECK((*missing)["counts"]["probes"]["known"] == false);
    CHECK(v3["issues"].size() >= 1);
    // A cell occupied by a calibration area attaches the binding to that area instead.
    write_overlay(td.path(), Json{{"bindings", {{"probe", {{"provider", "test.probe"}, {"as", "probes"},
        {"cells", {{{"about", "application"}, {"kind", "method"}, {"time", "state"}}}}}}}}});
    auto v4 = unwrap(graph_entry::compute(env, Json::object(), &custom));
    CHECK((*area(v4, "methods"))["counts"]["probes"]["value"] == 7);
    CHECK(area(v4, "cell:application.method.state") == nullptr);
  }

  TEST_CASE("C ABI loom_graph_entry returns the view and leaves the database untouched") {
    fsutil::TempDir td;
    std::string opts = Json{{"data_dir", td.path().string()}, {"start_workers", false}}.dump();
    const char* err = nullptr;
    LoomContext* ctx = loom_init_ex(opts.c_str(), &err);
    if (err) loom_free_string(err);
    REQUIRE(ctx != nullptr);
    const char* raw = loom_graph_entry(ctx, nullptr);
    REQUIRE(raw != nullptr);
    Json view = unwrap(json::parse(raw));
    loom_free_string(raw);
    CHECK(view["schema"] == "loom.graph_entry/1");
    CHECK((*area(view, "data_corpora"))["counts"]["db.conversations"]["value"] == 0);
    raw = loom_graph_entry(ctx, "{\"lang\":\"pl\"}");
    Json pl = unwrap(json::parse(raw));
    loom_free_string(raw);
    CHECK((*area(pl, "self_model"))["label"] == "Samomodel");
    loom_shutdown(ctx);
  }
}
