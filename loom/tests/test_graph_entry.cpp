#include <cctype>
#include <doctest/doctest.h>

#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>

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

const std::filesystem::path kLoomDir = std::filesystem::path(__FILE__).parent_path().parent_path();

std::string slurp(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  REQUIRE(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
Json read_json(const std::filesystem::path& p) { return Json::parse(slurp(p)); }

}  // namespace

TEST_SUITE("graph_entry") {
  TEST_CASE("interface, method and perspective counters equal the size of their source registries") {
    fsutil::TempDir td;
    graph_entry::Env env;
    env.data_dir = td.path();
    auto view = unwrap(graph_entry::compute(env));

    // Perspectives: the perspective settings of the shipped default-layers source, counted independently here.
    const Json persp_pack = read_json(kLoomDir / "data/profiles/graph_perspective.pack");
    std::set<std::string> persp_keys;
    for (const auto& e : persp_pack["entries"])
      if (e.value("area", "") == "graph/perspectives") persp_keys.insert(e["key"].get<std::string>());
    const Json& persp = (*area(view, "perspectives"))["counts"]["perspectives.store"];
    REQUIRE(persp["known"] == true);
    CHECK(persp["value"].get<std::int64_t>() == static_cast<std::int64_t>(persp_keys.size()));
    CHECK(persp["value"].get<std::int64_t>() > 0);
    CHECK(persp["by"].size() == persp_keys.size());

    // Methods: distinct entities of kind method:method across all runtime profile sources.
    std::set<std::string> methods;
    std::function<void(const Json&)> walk = [&](const Json& n) {
      if (n.is_object()) {
        if (n.contains("kind") && n["kind"].is_string() && n["kind"] == "method:method" && n.contains("id") && n["id"].is_string())
          methods.insert(n["id"].get<std::string>());
        for (const auto& it : n.items()) walk(it.value());
      } else if (n.is_array()) {
        for (const auto& it : n) walk(it);
      }
    };
    for (const auto& f : std::filesystem::directory_iterator(kLoomDir / "data/runtime")) walk(read_json(f.path()));
    const Json& meth = (*area(view, "methods"))["counts"]["methods.registry"];
    REQUIRE(meth["known"] == true);
    CHECK(meth["value"].get<std::int64_t>() == static_cast<std::int64_t>(methods.size()));
    CHECK(meth["value"].get<std::int64_t>() > 0);

    // Interfaces: C ABI = LOOM_API functions in loom.h, HTTP = /api routes in server/src, CLI = cli commands.
    const std::string header = slurp(kLoomDir / "include/loom/loom.h");
    std::set<std::string> abi;
    const std::regex abi_re(R"re(LOOM_API\s+[^;(]*?\b(loom_\w+)\s*\()re");
    for (std::sregex_iterator it(header.begin(), header.end(), abi_re), end; it != end; ++it) abi.insert((*it)[1]);
    std::set<std::string> routes;
    const std::regex route_re(R"re(\.(Get|Post|Put|Patch|Delete)\(\s*"(/api/[^"]*)")re");
    for (const auto& f : std::filesystem::directory_iterator(kLoomDir / "server/src")) {
      const std::string text = slurp(f.path());
      for (std::sregex_iterator it(text.begin(), text.end(), route_re), end; it != end; ++it) {
        std::string verb = (*it)[1].str();
        for (auto& ch : verb) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        routes.insert(verb + " " + (*it)[2].str());
      }
    }
    // Routes registered from the action catalogue (R3).
    const Json catalogue = read_json(kLoomDir / "data/runtime/actions.pack");
    for (const auto& [id, a] : catalogue["defaults"]["actions"].items())
      if (a.contains("http") && a["http"].value("path", "").rfind("/api/", 0) == 0)
        routes.insert(a["http"].value("verb", "GET") + " " + a["http"]["path"].get<std::string>());
    const Json cli = read_json(kLoomDir / "data/runtime/cli.pack");
    const Json& ifc = (*area(view, "interfaces"))["counts"]["interfaces"];
    REQUIRE(ifc["known"] == true);
    // The interface count is the registry's own: every key of data/graph/interfaces.pack (c_abi, http, jni, web, cli).
    const Json registry = read_json(kLoomDir / "data/graph/interfaces.pack");
    CHECK(ifc["value"].get<std::size_t>() == registry["interfaces"].size());
    CHECK(registry["interfaces"].size() >= 5);
    // JNI = JNIEXPORT symbols of the Android bridge; web = "VERB", "/api/..." calls of web/src/api (path params as :id).
    std::set<std::string> jni;
    const std::string jni_src = slurp(kLoomDir / "android/app/src/main/cpp/loom_jni.cpp");
    const std::regex jni_re(R"re(JNIEXPORT\s+\w+\s+JNICALL\s+(Java_\w+)\s*\()re");
    for (std::sregex_iterator it(jni_src.begin(), jni_src.end(), jni_re), end; it != end; ++it) jni.insert((*it)[1]);
    std::set<std::string> web;
    const std::regex web_re(R"re("(GET|POST|PUT|PATCH|DELETE)"\s*,\s*[`"](/api/[^`"]*))re");
    const std::regex param_re(R"re(\$\{encodeURIComponent\([^)]*\)\})re");
    for (const auto& f : std::filesystem::directory_iterator(kLoomDir / "web/src/api")) {
      if (f.path().extension() != ".ts") continue;
      const std::string text = slurp(f.path());
      for (std::sregex_iterator it(text.begin(), text.end(), web_re), end; it != end; ++it) {
        std::string route = std::regex_replace((*it)[2].str(), param_re, ":id");
        route = route.substr(0, route.find("${"));
        web.insert((*it)[1].str() + " " + route);
      }
    }
    CHECK(ifc["by"]["jni"].get<std::size_t>() == jni.size());
    CHECK(ifc["by"]["web"].get<std::size_t>() == web.size());
    CHECK(jni.size() > 5);
    CHECK(web.size() > 20);
    CHECK(ifc["by"]["c_abi"].get<std::size_t>() == abi.size());
    CHECK(ifc["by"]["http"].get<std::size_t>() == routes.size());
    CHECK(ifc["by"]["cli"].get<std::size_t>() == cli["defaults"]["commands"].size());
    CHECK(abi.size() > 100);
  }

  TEST_CASE("the interface registry is data: the overlay hides or adds an interface") {
    fsutil::TempDir td;
    write_overlay(td.path(), Json{{"interfaces", Json{{"http", Json{{"hidden", true}}},
                                                      {"extra", Json{{"entries", Json::array({"a", "b"})}}}}}});
    graph_entry::Env env;
    env.data_dir = td.path();
    auto view = unwrap(graph_entry::compute(env));
    const Json& ifc = (*area(view, "interfaces"))["counts"]["interfaces"];
    REQUIRE(ifc["known"] == true);
    CHECK(ifc["by"].contains("http") == false);
    CHECK(ifc["by"]["extra"] == 2);
    // registry keys, minus the hidden http, plus the overlay's extra
    CHECK(ifc["value"].get<std::size_t>() == read_json(kLoomDir / "data/graph/interfaces.pack")["interfaces"].size());
  }

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
    // Registries enumerated from the shipped data/code are real counts, not unknown (see the registry test below).
    CHECK((*area(view, "methods"))["counts"]["methods.registry"]["known"] == true);
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
