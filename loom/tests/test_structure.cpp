// W11a-1: graph.structure - structure templates as data over graph.navigate (src/graph/structure.h). Normal cases on a real runtime
// (a unit with two conversations, defined through the actions a person confirms), and property cases on the pure evaluate(): any
// template, empty and duplicated expects, cycles of extends, unknown operators, an inverted cardinality, a root that does not exist -
// always an answer, never an exception, and nothing written. The data of these tests is synthetic (temp data dirs, generated templates).
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

#include "graph/projection.h"
#include "graph/structure.h"
#include "loom/action_runner.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Fx {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  Fx() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
  }
  Json call(const std::string& action, Json request = Json::object()) {
    if (!request.contains("locale")) request["locale"] = "pl";
    return unwrap(rt->actions().invoke(action, request));
  }
  Json confirmed(const std::string& action, Json request) {
    const Json waiting = call(action, std::move(request));
    REQUIRE(waiting.at("status") == "awaiting_confirmation");
    return unwrap(rt->actions().confirm(Json{{"run_id", waiting.at("id")}, {"approved", true}, {"receipt_id", waiting.at("pending").at("receipt_id")}, {"ref", "test"}}));
  }
  std::string new_conv(const std::string& title) { return unwrap(rt->db().create_conv(title)).id; }
  Json structure(Json request) {
    if (!request.contains("lang")) request["lang"] = "pl";
    return unwrap(rt->actions().invoke("graph.structure", request));
  }
};

const Json* expectation(const Json& s, const std::string& ref) {
  for (const auto& e : s.at("expectations"))
    if (e.at("ref") == ref) return &e;
  return nullptr;
}
bool has_advisory(const Json& s, const std::string& code) {
  return std::any_of(s.at("advisories").begin(), s.at("advisories").end(), [&](const Json& a) { return a.at("code") == code; });
}

// Every file of the data dir and its bytes (what "nothing is written" compares).
std::map<std::string, std::string> snapshot(const std::filesystem::path& dir) {
  std::map<std::string, std::string> out;
  for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
    if (!e.is_regular_file()) continue;
    const std::string rel = std::filesystem::relative(e.path(), dir).string();
    if (rel.find("logs") == 0 || rel.find("-shm") != std::string::npos) continue;  // the log and SQLite's shared-memory index are not data
    std::ifstream in(e.path(), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    out[rel] = ss.str();
  }
  return out;
}

const Json& presets() {
  static const Json p = unwrap(RuntimeProfile::builtin("structure_templates")).values();
  return p;
}

}  // namespace

TEST_SUITE("structure") {
  TEST_CASE("a unit with two conversations: conversations present, documents and decisions as ghosts; viewing writes nothing") {
    Fx f;
    f.confirmed("units.define", Json{{"kind", "project"}, {"id", "alfa"}, {"label", "Alfa"}});
    for (const char* t : {"Jeden", "Dwa"}) f.confirmed("conversation.set_unit", Json{{"conv_id", f.new_conv(t)}, {"kind", "project"}, {"id", "alfa"}});
    const auto before = snapshot(f.td.path());
    const Json s = f.structure(Json{{"root", "unit:project/alfa"}});
    INFO(s.dump());
    CHECK(s.at("schema") == "loom.structure/1");
    CHECK(s.at("found") == true);
    REQUIRE(s.at("templates_applied").size() == 1);
    CHECK(s.at("templates_applied").at(0).at("id") == "unit_basic");
    const Json* conv = expectation(s, "unit_basic/conversations");
    REQUIRE(conv);
    CHECK(conv->at("state") == "present");
    CHECK(conv->at("count") == 2);
    CHECK(expectation(s, "unit_basic/documents")->at("state") == "missing");
    CHECK(expectation(s, "unit_basic/decisions")->at("state") == "empty");  // many: nothing required, so no ghost
    REQUIRE(s.at("ghosts").size() == 1);
    CHECK(s.at("ghosts").at(0).at("address") == "unit:project/alfa#expect/unit_basic/documents");
    CHECK(s.at("ghosts").at(0).at("stored") == false);
    for (const auto& c : s.at("children"))
      if (c.at("address").get<std::string>().rfind("conv:", 0) == 0) CHECK(c.at("expected_by").size() == 1);
    CHECK(s.at("advisories").empty());
    // show_expected off: the same states, no ghosts listed
    const Json off = f.structure(Json{{"root", "unit:project/alfa"}, {"show_expected", false}});
    CHECK(off.at("ghosts").empty());
    CHECK(expectation(off, "unit_basic/documents")->at("state") == "missing");
    // a name instead of an address (the words of a sentence)
    const Json named = f.structure(Json{{"root", "Alfa"}});
    CHECK(named.at("found") == true);
    CHECK(named.at("root").at("address") == "unit:project/alfa");
    // declined (from the request; W11a-2 stores it)
    const Json declined = f.structure(Json{{"root", "unit:project/alfa"}, {"declined", Json::array({"unit_basic/documents"})}});
    CHECK(expectation(declined, "unit_basic/documents")->at("state") == "declined");
    CHECK(declined.at("ghosts").empty());
    CHECK(snapshot(f.td.path()) == before);
  }

  TEST_CASE("a root that does not exist, an empty root, an unknown template: an answer with the reason") {
    Fx f;
    const Json missing = f.structure(Json{{"root", "unit:project/nie-ma"}});
    INFO(missing.dump());
    CHECK(missing.at("found") == false);
    CHECK(missing.at("reason") == "root_missing");
    CHECK(missing.at("reason_human").get<std::string>().find("unit:project/nie-ma") != std::string::npos);
    const Json empty = f.structure(Json::object());
    CHECK(empty.at("found") == false);
    CHECK(empty.at("reason") == "root_empty");
    CHECK(empty.at("suggestions").is_array());
    CHECK(!empty.at("suggestions").empty());
    // a unit is a node of the graph only while a conversation names it (graph_sources unit, read_units): an area is used here
    const Json unknown = f.structure(Json{{"root", "area:context_projects"}, {"templates", Json::array({"nie_ma_takiego", "import_basic"})}});
    INFO(unknown.dump());
    CHECK(has_advisory(unknown, "unknown_template"));
    CHECK(has_advisory(unknown, "template_not_matching"));
    CHECK(unknown.at("templates_applied").empty());
  }

  TEST_CASE("every field of every shipped template has a reader") {
    const auto& tf = structure::template_fields();
    const auto& ef = structure::expect_fields();
    const Json& shipped = presets().at("templates");
    for (const auto& t : shipped.items()) {
      const Json& tpl = t.value();
      for (const auto& k : tpl.items()) CHECK_MESSAGE(std::find(tf.begin(), tf.end(), k.key()) != tf.end(), t.key() << "." << k.key() << " has no reader");
      const Json& expects = tpl.at("expects");
      for (const auto& e : expects)
        for (const auto& k : e.items()) CHECK_MESSAGE(std::find(ef.begin(), ef.end(), k.key()) != ef.end(), t.key() << ".expects." << k.key() << " has no reader");
    }
    // and a field nobody reads is said, not dropped
    Json data = presets();
    data["templates"] = Json{{"t", Json{{"match", true}, {"colour", "red"}, {"expects", Json::array({Json{{"id", "a"}, {"weird", 1}}})}}}};
    const Json r = structure::evaluate(data, Json{{"address", "x:1"}}, Json::array(), Json::object());
    CHECK(has_advisory(r, "unknown_field"));
  }

  TEST_CASE("little-useful templates are advised, never refused: empty and duplicated expects, cycles, unknown operators, inverted card") {
    Json data = presets();
    data["templates"] = Json{
        {"empty", Json{{"match", true}, {"expects", Json::array()}}},
        {"dup", Json{{"match", true}, {"expects", Json::array({Json{{"id", "a"}, {"child_match", Json{{"kind", "k"}}}}, Json{{"id", "b"}, {"child_match", Json{{"kind", "k"}}}},
                                                             Json{{"id", "a"}, {"child_match", Json{{"kind", "z"}}}}})}}},
        {"c1", Json{{"match", true}, {"extends", Json::array({"c2"})}, {"expects", Json::array({Json{{"id", "x"}}})}}},
        {"c2", Json{{"match", true}, {"extends", Json::array({"c1", "ghost_parent"})}, {"expects", Json::array({Json{{"id", "y"}}})}}},
        {"op", Json{{"match", Json{{"sparkles", 1}}}, {"expects", Json::array()}}},
        {"inv", Json{{"match", true}, {"expects", Json::array({Json{{"id", "i"}, {"card", Json{{"min", 3}, {"max", 1}}}}, Json{{"id", "u"}, {"card", "lots"}}})}}},
    };
    const Json children = Json::array({Json{{"address", "k:1"}, {"kind", "k"}}, Json{{"address", "k:2"}, {"kind", "k"}, {"evidence", Json{{"class", "inferred"}}}}});
    const Json r = structure::evaluate(data, Json{{"address", "root:1"}}, children, Json{{"lang", "pl"}});
    INFO(r.dump());
    for (const char* code : {"empty_expects", "duplicate_expects", "duplicate_expect_id", "extends_cycle", "unknown_extends", "unknown_operator", "card_inverted", "unknown_card"})
      CHECK_MESSAGE(has_advisory(r, code), code);
    for (const auto& a : r.at("advisories")) {
      CHECK(a.at("blocking") == false);
      CHECK(!a.at("fingerprint").get<std::string>().empty());
      CHECK(!a.at("words").at("pl").get<std::string>().empty());
      CHECK(!a.at("words").at("en").get<std::string>().empty());
    }
    // the cycle is cut, the rest still computed: c1 sees y (from c2) and x
    CHECK(expectation(r, "c2/y"));
    CHECK(expectation(r, "c1/x"));
    // the inverted card: 2 children, min 3 -> missing (below_min is the first rule that holds)
    CHECK(expectation(r, "inv/i")->at("state") == "missing");
    // a template whose match uses an unknown operator applies to nothing
    for (const auto& t : r.at("templates_applied")) CHECK(t.at("id") != "op");
  }

  TEST_CASE("states follow the ordered rules of the data: inferred only, excess, a custom rule order") {
    Json data = presets();
    data["templates"] = Json{{"t", Json{{"match", true}, {"expects", Json::array({Json{{"id", "inf"}, {"child_match", Json{{"kind", "i"}}}, {"card", "at_least_one"}},
                                                                                   Json{{"id", "one"}, {"child_match", Json{{"kind", "o"}}}, {"card", "one"}}})}}}};
    const Json children = Json::array({Json{{"address", "i:1"}, {"kind", "i"}, {"evidence", Json{{"class", "inferred"}}}}, Json{{"address", "o:1"}, {"kind", "o"}},
                                       Json{{"address", "o:2"}, {"kind", "o"}}});
    const Json r = structure::evaluate(data, Json{{"address", "r"}}, children, Json::object());
    CHECK(expectation(r, "t/inf")->at("state") == "inferred");
    CHECK(expectation(r, "t/one")->at("state") == "excess");
    // the person's order: present before excess
    data["states"] = Json::array({Json{{"id", "odd"}, {"when", "nonsense"}}, Json{{"id", "present"}, {"when", "any"}}, Json{{"id", "missing"}, {"when", "below_min"}, {"ghost", true}}});
    const Json r2 = structure::evaluate(data, Json{{"address", "r"}}, children, Json::object());
    CHECK(expectation(r2, "t/one")->at("state") == "present");
    CHECK(has_advisory(r2, "unknown_state_rule"));
  }

  TEST_CASE("predicates: all, any, not, prefix, kind lists, field tests") {
    std::set<std::string> u;
    const Json n{{"address", "unit:case/x"}, {"kind", "unit"}, {"attrs", Json{{"k", "v"}, {"n", 2}}}, {"evidence", Json{{"class", "observed"}}}};
    CHECK(structure::matches(Json{{"prefix", "unit:case/"}}, n, u));
    CHECK(!structure::matches(Json{{"prefix", "imp:"}}, n, u));
    CHECK(structure::matches(Json{{"kind", Json::array({"x", "unit"})}}, n, u));
    CHECK(structure::matches(Json{{"all", Json::array({Json{{"kind", "unit"}}, Json{{"not", Json{{"prefix", "imp:"}}}}})}}, n, u));
    CHECK(structure::matches(Json{{"any", Json::array({Json{{"kind", "no"}}, Json{{"field", "attrs.k"}, {"equals", "v"}}})}}, n, u));
    CHECK(structure::matches(Json{{"field", "attrs.n"}, {"in", Json::array({1, 2})}}, n, u));
    CHECK(structure::matches(Json{{"field", "evidence.class"}, {"prefix", "obs"}}, n, u));
    CHECK(structure::matches(Json{{"field", "attrs.none"}, {"exists", false}}, n, u));
    CHECK(structure::matches(Json(), n, u));
    CHECK(u.empty());
    CHECK(!structure::matches(Json{{"not", Json{{"sparkle", 1}}}}, n, u));  // unknown inside not still matches nothing
    CHECK(u.count("sparkle"));
  }

  TEST_CASE("property: any generated template set over any children gives an answer, never an exception, with consistent counts") {
    std::mt19937 rng(20261030);
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
    const std::vector<Json> preds = {Json(), Json(true), Json(false), Json{{"kind", "a"}}, Json{{"kind", Json::array({"a", "b"})}}, Json{{"prefix", "x:"}},
                                     Json{{"not", Json{{"kind", "a"}}}}, Json{{"any", Json::array()}}, Json{{"all", Json::array()}}, Json{{"bogus", 1}},
                                     Json("text"), Json(3), Json{{"field", "attrs.k"}, {"equals", 1}}, Json::array({Json{{"kind", "b"}}})};
    const std::vector<Json> cards = {Json("one"), Json("many"), Json("optional"), Json("at_least_one"), Json("nope"), Json{{"min", 2}}, Json{{"min", 5}, {"max", 0}},
                                     Json{{"max", 1}}, Json(), Json(7), Json{{"min", -3}, {"max", nullptr}}};
    const std::vector<std::string> kinds = {"a", "b", "c"};
    const std::vector<std::string> classes = {"observed", "inferred", "derived", "declared", ""};
    for (int round = 0; round < 300; ++round) {
      Json data = presets();
      Json templates = Json::object();
      const int nt = pick(5);
      for (int t = 0; t < nt; ++t) {
        Json tpl{{"match", preds[pick(static_cast<int>(preds.size()))]}};
        Json ex = Json::array();
        for (int e = pick(5); e > 0; --e) {
          Json one{{"id", "e" + std::to_string(pick(4))}, {"child_match", preds[pick(static_cast<int>(preds.size()))]}};
          if (pick(3)) one["card"] = cards[pick(static_cast<int>(cards.size()))];
          ex.push_back(one);
        }
        if (pick(6) == 0) ex = Json("not a list");
        tpl["expects"] = ex;
        if (pick(2)) tpl["extends"] = Json::array({"t" + std::to_string(pick(nt + 1))});
        templates["t" + std::to_string(t)] = tpl;
      }
      if (pick(10) == 0) templates["broken"] = Json(42);
      data["templates"] = templates;
      Json children = Json::array();
      for (int c = pick(6); c > 0; --c)
        children.push_back(Json{{"address", (pick(2) ? "x:" : "y:") + std::to_string(c)}, {"kind", kinds[pick(3)]}, {"evidence", Json{{"class", classes[pick(5)]}}},
                                {"depth", pick(4) ? 1 : 2}});
      Json req{{"lang", pick(2) ? "pl" : "en"}, {"show_expected", pick(3) != 0}};
      if (pick(3) == 0) req["templates"] = Json::array({"t0", "missing_one"});
      Json r;
      REQUIRE_NOTHROW(r = structure::evaluate(data, Json{{"address", "x:root"}, {"kind", kinds[pick(3)]}}, children, req));
      // consistency: every expectation has a state; ghosts only for missing (preset rule) and only when shown; counts add up
      std::size_t ghosts = 0;
      for (const auto& e : r.at("expectations")) {
        CHECK(!e.at("state").get<std::string>().empty());
        CHECK(e.at("count") == e.at("matched").size());
        if (e.contains("ghost")) ++ghosts;
      }
      CHECK(r.at("counts").at("missing") == ghosts);
      CHECK(r.at("ghosts").size() == (req.at("show_expected").get<bool>() ? ghosts : 0));
      CHECK(r.at("children").size() == children.size());
      std::set<std::string> fps;
      for (const auto& a : r.at("advisories")) CHECK(fps.insert(a.at("fingerprint").get<std::string>()).second);
    }
  }
}
