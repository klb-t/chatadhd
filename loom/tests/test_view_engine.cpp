// P28 step 1: the visualization engine - declarations as graph data evaluated into a layout-independent view model.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>

#include "loom/db.h"
#include "loom/effective_view.h"
#include "loom/util/fs.h"
#include "loom/view_engine.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json el(const char* id, const char* kind, Json extra = Json::object()) {
  Json o{{"id", id}, {"kind", kind}};
  for (auto it = extra.begin(); it != extra.end(); ++it) o[it.key()] = it.value();
  return o;
}

// A version line v1 <- v2 <- v3 <- v4 with a branch b1 forked from v2 (focus v2 sees two forks), a caller of v2, and a goal.
struct Fixture {
  fsutil::TempDir td;
  std::unique_ptr<Database> db;
  std::string v1, v2, v3, v4, b1, b2, caller, goal;
  Fixture() {
    db = loom::test::open_db(td.path() / "g.db");
    auto node = [&](const char* label, const char* kind, const char* created) {
      const std::string id = unwrap(db->create_node(label, kind));
      NodePatch patch;
      patch.created = created;
      LOOM_REQUIRE_OK(db->update_node(id, patch));
      return id;
    };
    v1 = node("v1", "version", "2026-01-01T00:00:00Z");
    v2 = node("v2", "version", "2026-01-11T00:00:00Z");
    v3 = node("v3", "version", "2026-01-21T00:00:00Z");
    v4 = node("v4", "version", "2026-01-31T00:00:00Z");
    b1 = node("b1", "version", "2026-01-15T00:00:00Z");
    b2 = node("b2", "version", "2026-01-25T00:00:00Z");
    caller = node("caller()", "function", "2026-02-01T00:00:00Z");
    goal = node("be fast", "goal", "2026-02-02T00:00:00Z");
    unwrap(db->create_link(v2, v1, "previous_version"));
    unwrap(db->create_link(v3, v2, "previous_version"));
    unwrap(db->create_link(v4, v3, "previous_version"));
    unwrap(db->create_link(b1, v2, "forked_from"));
    unwrap(db->create_link(b2, b1, "previous_version"));
    unwrap(db->create_link(caller, v2, "calls"));
    unwrap(db->create_link(v2, goal, "serves_goal"));
  }
  views::RenderContext ctx() const {
    views::RenderContext c;
    c.db = db.get();
    c.root = td.path();
    return c;
  }
};

const Json* by_id(const Json& list, const std::string& id) {
  for (const auto& e : list)
    if (e.value("id", "") == id) return &e;
  return nullptr;
}

Json history(Json extra_elements = Json::array(), bool with_encodings = true) {
  Json elements = Json::array({el("history", "source", {{"perspective", "version_history"}, {"weight", 1.0}, {"depth", 6}, {"role", "main"}}),
                               el("distance", "measure", {{"measure", "distance_from_focus"}}), el("age", "measure", {{"measure", "age"}}),
                               el("divergence", "measure", {{"measure", "branch_divergence"}}), el("doi", "measure", {{"measure", "degree_of_interest"}})});
  if (with_encodings) elements.push_back(el("e_size", "encoding", {{"channel", "size"}, {"measure", "doi"}}));
  for (const auto& x : extra_elements) elements.push_back(x);
  return Json{{"label", "test"}, {"elements", elements}};
}


// label -> channels of every element (ids are random; labels are stable in the fixture), for golden comparisons.
Json channels_by_label(const Json& model) {
  Json out = Json::object();
  for (const auto& e : model.at("elements")) out[e.value("label", std::string())] = e.value("channels", Json::object());
  return out;
}

// Element order follows the (random) node ids; compare label by label.
void check_same_by_label(const Json& got, const Json& want) {
  CHECK(got.size() == want.size());
  for (auto it = want.begin(); it != want.end(); ++it) {
    REQUIRE_MESSAGE(got.contains(it.key()), it.key());
    CHECK_MESSAGE(got.at(it.key()) == it.value(), it.key());
  }
}

// The scale specs that existed before G3 (range / domain / type / invert): their output is pinned by a golden (G3 must keep it identical).
Json legacy_scale_declaration() {
  return history(Json::array({el("kindm", "measure", {{"measure", "node_kind"}}),
                              el("e_alpha_inv", "encoding", {{"channel", "alpha"}, {"measure", "age"}, {"scale", Json{{"invert", true}}}}),
                              el("e_light_rng", "encoding", {{"channel", "lightness"}, {"measure", "distance"}, {"scale", Json{{"range", Json::array({0.1, 0.9})}}}}),
                              el("e_sat_dom", "encoding", {{"channel", "saturation"}, {"measure", "age"}, {"scale", Json{{"domain", Json::array({0, 40})}, {"range", Json::array({1, 0})}}}}),
                              el("e_hue_cat", "encoding", {{"channel", "hue"}, {"measure", "kindm"}}),
                              el("e_badge", "encoding", {{"channel", "badge"}, {"measure", "divergence"}}),
                              el("e_blur", "encoding", {{"channel", "blur"}, {"measure", "divergence"}, {"scale", Json{{"invert", true}, {"domain", Json::array({1, 1})}}}})}));
}

}  // namespace

TEST_SUITE("view_engine") {
  TEST_CASE("the ROM carries the presets, measure kinds, systems, channels and rules as graph data") {
    fsutil::TempDir td;
    auto eng = unwrap(views::ViewEngine::open(td.path()));
    const Json l = eng.list();
    CHECK(l["declarations"]["entries"].size() >= 2);
    CHECK(by_id(l["declarations"]["entries"], "history_cone"));
    for (const char* k : {"distance_from_focus", "age", "branch_divergence", "degree_of_interest"}) CHECK_MESSAGE(by_id(l["measure_kinds"]["entries"], k), k);
    for (const char* k : {"cartesian", "sector", "depth_from_distance", "tree_branch_angles"}) CHECK_MESSAGE(by_id(l["coordinate_systems"]["entries"], k), k);
    for (const char* k : {"position", "hue", "saturation", "lightness", "alpha", "size", "badge"}) CHECK_MESSAGE(by_id(l["channels"]["entries"], k), k);
    CHECK(by_id(l["rules"]["entries"], "suitability"));
    CHECK(by_id(l["rules"]["entries"], "budget"));
    CHECK(l["issues"].empty());
  }

  TEST_CASE("measures: distance (weighted sum / min), age, branch divergence, degree of interest") {
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    const Json m = unwrap(eng.evaluate_declaration(f.ctx(), history(), Json{{"focus", f.v2}}));
    const Json& els = m["elements"];
    auto at = [&](const std::string& id, const char* measure) { return by_id(els, id)->at("measures").at(measure).get<double>(); };
    CHECK(at(f.v2, "distance") == 0);
    CHECK(at(f.v3, "distance") == 1);
    CHECK(at(f.v4, "distance") == 2);
    CHECK(at(f.b2, "distance") == 2);
    // age is relative to the focus: positive = older, negative = newer
    CHECK(at(f.v1, "age") == doctest::Approx(10.0));
    CHECK(at(f.v3, "age") == doctest::Approx(-10.0));
    // v2 has three neighbours (v1, v3, b1): all three are forks off the focus
    CHECK(at(f.v1, "divergence") == 1);
    CHECK(at(f.v3, "divergence") == 1);
    CHECK(at(f.v4, "divergence") == 1);  // v3 has one child: no new fork
    CHECK(at(f.b2, "divergence") == 1);
    CHECK(at(f.v2, "divergence") == 0);
    // degree of interest = degree - distance: the focus has degree 3
    CHECK(at(f.v2, "doi") == doctest::Approx(3.0));
    CHECK(at(f.v4, "doi") < at(f.v3, "doi"));
    CHECK(m["measure_stats"]["distance"]["max"] == 2);
    CHECK(m["measure_stats"]["age"]["unit"] == "days");
    CHECK(m["unsupported"].empty());
    // channel values: size from interest; the focus is the largest
    const double sf = by_id(els, f.v2)->at("channels").at("size").get<double>();
    CHECK(sf == doctest::Approx(28.0));
    CHECK(by_id(els, f.v4)->at("channels").at("size").get<double>() < sf);
  }

  TEST_CASE("the combination rule is data: weighted sum adds hops / weight over sources, min takes the smallest") {
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    auto decl = [&](const char* combine) {
      return Json{{"elements", Json::array({el("a", "source", {{"perspective", "version_history"}, {"weight", 1.0}, {"depth", 3}}),
                                            el("b", "source", {{"perspective", "version_history"}, {"weight", 0.5}, {"depth", 3}}),
                                            el("distance", "measure", {{"measure", "distance_from_focus"}, {"params", Json{{"combine", combine}}}})})}};
    };
    const Json sum = unwrap(eng.evaluate_declaration(f.ctx(), decl("weighted_sum"), Json{{"focus", f.v2}}));
    const Json mn = unwrap(eng.evaluate_declaration(f.ctx(), decl("min"), Json{{"focus", f.v2}}));
    CHECK(by_id(sum["elements"], f.v3)->at("measures").at("distance") == doctest::Approx(1.0 / 1.0 + 1.0 / 0.5));
    CHECK(by_id(mn["elements"], f.v3)->at("measures").at("distance") == doctest::Approx(1.0));
    // an unknown combination rule is not silently replaced: the measure is unsupported, with the reason
    const Json bad = unwrap(eng.evaluate_declaration(f.ctx(), decl("geometric"), Json{{"focus", f.v2}}));
    REQUIRE(bad["unsupported"].size() == 1);
    CHECK(bad["unsupported"][0]["element"] == "distance");
    CHECK(bad["unsupported"][0]["reason"]["code"] == "unsupported_combine");
    CHECK_FALSE(bad["unsupported"][0]["reason"]["text"]["pl"].get<std::string>().empty());
  }

  TEST_CASE("open lists: unknown kinds pass through to unsupported with a visible reason, the rest still evaluates") {
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    Json d = history(Json::array({el("m", "motion", {{"channel", "time"}}), el("h", "measure", {{"measure", "haptic_pressure"}}),
                                  el("c", "coordinates", {{"system", "mds"}}), el("e", "encoding", {{"channel", "smell"}, {"measure", "age"}}),
                                  el("e2", "encoding", {{"channel", "alpha"}, {"measure", "nonexistent"}}), el("k", "composition", {{"composition", "orbit"}})}));
    const Json m = unwrap(eng.evaluate_declaration(f.ctx(), d, Json{{"focus", f.v2}}));
    std::map<std::string, std::string> codes;
    for (const auto& u : m["unsupported"]) {
      codes[u["element"]] = u["reason"]["code"];
      CHECK_FALSE(u["reason"]["text"]["en"].get<std::string>().empty());
    }
    CHECK(codes["m"] == "unsupported_element_kind");
    CHECK(codes["h"] == "unsupported_measure_kind");
    CHECK(codes["c"] == "unsupported_coordinate_system");
    CHECK(codes["e"] == "unsupported_channel");
    CHECK(codes["e2"] == "unknown_measure");
    CHECK(codes["k"] == "unsupported_composition");
    CHECK(m["elements"].size() == 6);  // v1 v2 v3 v4 b1 b2: the unsupported elements do not stop the rest
  }

  TEST_CASE("sectors: direction to perspective, the angle blended by the strength of each source, a fixed angle per branch") {
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    Json d{{"elements", Json::array({el("history", "source", {{"perspective", "version_history"}, {"weight", 1.0}, {"depth", 5}, {"role", "main"}}),
                                     el("callers", "source", {{"perspective", "calls"}, {"direction", "in"}, {"weight", 0.6}, {"depth", 2}}),
                                     el("distance", "measure", {{"measure", "distance_from_focus"}}),
                                     el("sector", "measure", {{"measure", "membership"}}),
                                     el("plane", "coordinates", {{"system", "sector"}, {"bind", Json{{"radius", "distance"}}}, {"branch_step_deg", 10},
                                                                 {"sectors", Json::array({Json{{"id", "past"}, {"sources", Json::array({"history"})}, {"center_deg", 0}, {"width_deg", 90}},
                                                                                          Json{{"id", "left"}, {"sources", Json::array({"callers"})}, {"center_deg", -90}, {"width_deg", 60}}})}}),
                                     el("by_sector", "composition", {{"composition", "sectors"}})})}};
    const Json m = unwrap(eng.evaluate_declaration(f.ctx(), d, Json{{"focus", f.v2}}));
    const Json* caller = by_id(m["elements"], f.caller);
    REQUIRE(caller);
    CHECK((*caller)["sector"] == "left");
    CHECK((*caller)["coords"]["plane"]["angle_deg"].get<double>() == doctest::Approx(-90.0));
    // v1, v3, b1 are three branches off the focus: fixed angles -10, 0, +10 (sorted by creation time: v1, b1, v3), inherited by their descendants
    auto angle = [&](const std::string& id) { return by_id(m["elements"], id)->at("coords").at("plane").at("angle_deg").get<double>(); };
    CHECK(angle(f.v1) == doctest::Approx(-10.0));
    CHECK(angle(f.b1) == doctest::Approx(0.0));
    CHECK(angle(f.v3) == doctest::Approx(10.0));
    CHECK(angle(f.v4) == doctest::Approx(10.0));
    CHECK(angle(f.b2) == doctest::Approx(0.0));
    CHECK(by_id(m["elements"], f.v4)->at("coords").at("plane").at("radius") == 2);
    // groups per sector with their members
    const Json* past = by_id(m["groups"], "past");
    REQUIRE(past);
    CHECK((*past)["members"].size() == 5);
    CHECK((*past)["center_deg"] == 0);
    // an element reached by two sectors is blended: the focus-adjacent node reached by both history and the caller source
    unwrap(f.db->create_link(f.v1, f.caller, "calls"));  // v1 calls caller(): caller is now a callee of v1 as well
    const Json m2 = unwrap(eng.evaluate_declaration(f.ctx(), d, Json{{"focus", f.v2}, {"sources", Json{{"callers", Json{{"weight", 1.0}, {"depth", 3}}}}}}));
    const Json* v1n = by_id(m2["elements"], f.v1);
    REQUIRE(v1n);
    const double a = (*v1n)["coords"]["plane"]["angle_deg"].get<double>();
    CHECK(a < 0.0);
    CHECK(a > -90.0);  // between the past (0) and the callers (-90)
  }

  TEST_CASE("depth from distance is signed per sector (the future is behind) and the tree system gives a fixed angle per branch") {
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    Json d{{"elements", Json::array({el("history", "source", {{"perspective", "version_history"}, {"depth", 4}, {"role", "main"}}),
                                     el("future", "source", {{"perspective", "teleology"}, {"depth", 2}}),
                                     el("distance", "measure", {{"measure", "distance_from_focus"}}),
                                     el("plane", "coordinates", {{"system", "sector"}, {"sectors", Json::array({Json{{"id", "past"}, {"sources", Json::array({"history"})}, {"center_deg", 0}},
                                                                                                         Json{{"id", "future"}, {"sources", Json::array({"future"})}, {"center_deg", 180}, {"depth_sign", -1}}})}}),
                                     el("depth", "coordinates", {{"system", "depth_from_distance"}}),
                                     el("tree", "coordinates", {{"system", "tree_branch_angles"}, {"step_deg", 5}, {"base_deg", 90}})})}};
    const Json m = unwrap(eng.evaluate_declaration(f.ctx(), d, Json{{"focus", f.v2}}));
    CHECK(by_id(m["elements"], f.v3)->at("coords").at("depth").at("depth") == 1);
    CHECK(by_id(m["elements"], f.goal)->at("coords").at("depth").at("depth") == -1);
    CHECK(by_id(m["elements"], f.v1)->at("coords").at("tree").at("angle_deg").get<double>() == doctest::Approx(85.0));
    CHECK(by_id(m["elements"], f.v3)->at("coords").at("tree").at("angle_deg").get<double>() == doctest::Approx(95.0));
    CHECK(by_id(m["elements"], f.b2)->at("coords").at("tree").at("branch") == "1");
  }

  TEST_CASE("rules: channel suitability per data type promotes or allows with an explanation, never blocks") {
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    Json d = history(Json::array({el("sector", "measure", {{"measure", "node_kind"}}), el("e_hue_n", "encoding", {{"channel", "hue"}, {"measure", "sector"}}),
                                  el("e_hue_q", "encoding", {{"channel", "hue"}, {"measure", "age"}})}),
                     false);
    const Json m = unwrap(eng.evaluate_declaration(f.ctx(), d, Json{{"focus", f.v2}}));
    const Json* nominal = by_id(m["encodings"], "e_hue_n");
    const Json* quant = by_id(m["encodings"], "e_hue_q");
    REQUIRE((nominal && quant));
    CHECK((*nominal)["status"] == "promoted");
    CHECK((*nominal)["rank"] == 2);
    CHECK((*quant)["status"] == "allowed");
    REQUIRE((*quant)["reasons"].size() >= 1);
    CHECK((*quant)["reasons"][0]["code"] == "channel_low_rank");
    CHECK_FALSE((*quant)["reasons"][0]["text"]["pl"].get<std::string>().empty());
    // never blocks: the allowed encoding still produced channel values
    CHECK(by_id(m["elements"], f.v3)->at("channels").contains("hue"));
    // the table is data: an overlay that puts hue first for quantitative data promotes it
    LOOM_REQUIRE_OK(views::put_op(f.td.path(), "u", Json{{"view", "view_rules"}, {"op", "modify"}, {"id", "suitability"},
                                                         {"patch", Json{{"ranking", Json{{"quantitative", Json::array({"hue", "position"})}}}}}}));
    auto eng2 = unwrap(views::ViewEngine::open(f.td.path()));
    const Json m2 = unwrap(eng2.evaluate_declaration(f.ctx(), d, Json{{"focus", f.v2}}));
    CHECK((*by_id(m2["encodings"], "e_hue_q"))["status"] == "promoted");
    // and a view can switch a rule off for itself
    Json d2 = d;
    d2["elements"].push_back(el("r", "rules", {{"promote_top", 1}}));
    const Json m3 = unwrap(eng.evaluate_declaration(f.ctx(), d2, Json{{"focus", f.v2}}));
    CHECK((*by_id(m3["encodings"], "e_hue_n"))["status"] == "allowed");
  }

  TEST_CASE("rules: interference pairs hold only when the interfering dimension grows with importance, checked on the data") {
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    auto decl = [&](const char* saturation_measure, bool invert) {
      return history(Json::array({el("sector", "measure", {{"measure", "node_kind"}}), el("e_hue", "encoding", {{"channel", "hue"}, {"measure", "sector"}}),
                                  el("e_sat", "encoding", {{"channel", "saturation"}, {"measure", saturation_measure}, {"scale", Json{{"invert", invert}}}})}),
                     false);
    };
    // saturation from interest: low saturation = low importance -> the pair is cleared, hue promoted
    const Json ok = unwrap(eng.evaluate_declaration(f.ctx(), decl("doi", false), Json{{"focus", f.v2}}));
    const Json* hue = by_id(ok["encodings"], "e_hue");
    REQUIRE(hue);
    CHECK((*hue)["status"] == "promoted");
    bool cleared = false;
    for (const auto& r : (*hue)["reasons"]) cleared = cleared || r["code"] == "interference_cleared";
    CHECK(cleared);
    // saturation from distance (grows with distance, importance falls with it): the condition is not met
    const Json bad = unwrap(eng.evaluate_declaration(f.ctx(), decl("distance", false), Json{{"focus", f.v2}}));
    const Json* hue2 = by_id(bad["encodings"], "e_hue");
    REQUIRE(hue2);
    CHECK((*hue2)["status"] == "allowed");
    std::string code;
    double rho = 1;
    for (const auto& r : (*hue2)["reasons"])
      if (r["code"] == "interference_unmet") { code = r["code"]; rho = r["params"]["rho"]; }
    CHECK(code == "interference_unmet");
    CHECK(rho < 0.5);
    // without any importance in the view the condition cannot be checked: allowed, explained, not blocked
    Json noimp{{"elements", Json::array({el("history", "source", {{"perspective", "version_history"}, {"depth", 4}}), el("age", "measure", {{"measure", "age"}}),
                                         el("sector", "measure", {{"measure", "node_kind"}}), el("e_hue", "encoding", {{"channel", "hue"}, {"measure", "sector"}}),
                                         el("e_sat", "encoding", {{"channel", "saturation"}, {"measure", "age"}})})}};
    const Json un = unwrap(eng.evaluate_declaration(f.ctx(), noimp, Json{{"focus", f.v2}}));
    const Json* hue3 = by_id(un["encodings"], "e_hue");
    REQUIRE(hue3);
    CHECK((*hue3)["status"] == "allowed");
    CHECK((*hue3)["reasons"][0]["code"] == "interference_unverifiable");
  }

  TEST_CASE("rules: the node budget collapses the farthest elements first into group nodes and never the focus; budget 0 = none") {
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    const Json full = unwrap(eng.evaluate_declaration(f.ctx(), history(), Json{{"focus", f.v2}}));
    CHECK(full["elements"].size() == 6);
    CHECK(full["budget"]["collapsed"] == 0);
    const Json cut = unwrap(eng.evaluate_declaration(f.ctx(), history(), Json{{"focus", f.v2}, {"budget", 3}}));
    CHECK(cut["elements"].size() == 3);
    CHECK(by_id(cut["elements"], f.v2));
    CHECK(by_id(cut["elements"], f.v3));  // the near ones stay; of the three at distance 1 the least interesting (v1, degree 1) goes
    CHECK(by_id(cut["elements"], f.b1));
    CHECK_FALSE(by_id(cut["elements"], f.v1));
    CHECK_FALSE(by_id(cut["elements"], f.v4));
    CHECK_FALSE(by_id(cut["elements"], f.b2));
    CHECK(cut["budget"]["collapsed"] == 3);
    REQUIRE(cut["groups"].size() == 1);
    CHECK(cut["groups"][0]["kind"] == "collapsed");
    CHECK(cut["groups"][0]["count"] == 3);
    CHECK(cut["budget"]["reason"]["code"] == "budget_applied");
    for (const auto& e : cut["edges"]) {
      CHECK(by_id(cut["elements"], e["src"]));
      CHECK(by_id(cut["elements"], e["dst"]));
    }
    // the preset is a preset: the declaration's rules element changes it, 0 removes it
    Json d = history(Json::array({el("r", "rules", {{"budget", Json{{"max_nodes", 2}}}})}));
    CHECK(unwrap(eng.evaluate_declaration(f.ctx(), d, Json{{"focus", f.v2}}))["elements"].size() == 2);
    CHECK(unwrap(eng.evaluate_declaration(f.ctx(), d, Json{{"focus", f.v2}, {"budget", 0}}))["elements"].size() == 6);
  }

  TEST_CASE("declarations compose through the effective view: an overlay adds a preset with provenance, a missing focus is an error") {
    Fixture f;
    LOOM_REQUIRE_OK(views::put_op(f.td.path(), "u", Json{{"view", "view_declarations"}, {"op", "add"}, {"id", "mine"}, {"entry", history()}}));
    LOOM_REQUIRE_OK(views::put_op(f.td.path(), "u", Json{{"view", "measure_kinds"}, {"op", "disable"}, {"id", "age"}}));
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    const Json m = unwrap(eng.evaluate(f.ctx(), "mine", Json{{"focus", f.v2}}));
    CHECK(m["declaration"]["id"] == "mine");
    CHECK(m["declaration"]["origin"] == "overlay:u");
    CHECK(m["declaration"]["provenance"].size() >= 1);
    // a disabled measure kind is unsupported with a reason; the others still work
    bool age_unsupported = false;
    for (const auto& u : m["unsupported"]) age_unsupported = age_unsupported || (u["element"] == "age" && u["reason"]["code"] == "unsupported_measure_kind");
    CHECK(age_unsupported);
    CHECK(by_id(m["elements"], f.v3)->at("measures").contains("distance"));
    CHECK_FALSE(eng.evaluate(f.ctx(), "mine", Json::object()));
    CHECK_FALSE(eng.evaluate(f.ctx(), "mine", Json{{"focus", "n_nope"}}));
    CHECK_FALSE(eng.evaluate(f.ctx(), "no_such_view", Json{{"focus", f.v2}}));
  }

  TEST_CASE("the shipped preset history_cone evaluates on a version line with its ecosystem and future") {
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    const Json m = unwrap(eng.evaluate(f.ctx(), "history_cone", Json{{"focus", f.v2}}));
    CHECK(m["schema"] == "loom.view_model/1");
    CHECK(m["unsupported"].empty());
    CHECK((*by_id(m["elements"], f.caller))["sector"] == "callers");
    CHECK((*by_id(m["elements"], f.goal))["sector"] == "future");
    CHECK((*by_id(m["elements"], f.v3))["sector"] == "past");
    CHECK((*by_id(m["elements"], f.v2))["focus"] == true);
    CHECK((*by_id(m["elements"], f.v1))["channels"].contains("badge"));
    CHECK(m["groups"].size() == 4);
    CHECK(m["encodings"].size() >= 5);
    for (const auto& e : m["encodings"]) CHECK_MESSAGE((e["status"] == "promoted" || e["status"] == "allowed"), e["id"].get<std::string>());
  }

  TEST_CASE("G3: scale specs from before G3 (range / domain / type / invert) and the shipped presets give the identical output (golden)") {
    // captured on 6a89adc3 before the general scale; any difference here is a regression of backward compatibility
    const Json golden = Json::parse(R"GOLDEN({"legacy":{"b2":{"size":4.0,"alpha":0.84,"lightness":0.9,"saturation":1.0,"hue":0.0,"badge":"1","blur":3.0},"v3":{"size":16.0,"alpha":0.733333,"lightness":0.5,"saturation":1.0,"hue":0.0,"badge":"1","blur":3.0},"v1":{"size":10.0,"alpha":0.2,"lightness":0.5,"saturation":0.75,"hue":0.0,"badge":"1","blur":3.0},"b1":{"size":16.0,"alpha":0.573333,"lightness":0.5,"saturation":1.0,"hue":0.0,"badge":"1","blur":3.0},"v2":{"size":28.0,"alpha":0.466667,"lightness":0.1,"saturation":1.0,"hue":0.0,"badge":"0","blur":3.0},"v4":{"size":4.0,"alpha":1.0,"lightness":0.9,"saturation":1.0,"hue":0.0,"badge":"1","blur":3.0}},"history_cone":{"b2":{"hue":225.0,"saturation":1.0,"lightness":0.7,"size":4.0,"badge":"version"},"v3":{"hue":225.0,"saturation":1.0,"lightness":0.625,"size":12.0,"badge":"version"},"caller()":{"hue":0.0,"lightness":0.83125,"size":5.333332,"badge":"function"},"v1":{"hue":225.0,"saturation":1.0,"lightness":0.25,"size":8.0,"badge":"version"},"b1":{"hue":225.0,"saturation":1.0,"lightness":0.5125,"size":12.0,"badge":"version"},"be fast":{"hue":150.0,"lightness":0.85,"size":4.0,"badge":"goal"},"v2":{"hue":75.0,"saturation":0.15,"lightness":0.4375,"size":28.0,"badge":"version"},"v4":{"hue":225.0,"saturation":1.0,"lightness":0.8125,"size":4.0,"badge":"version"}},"age_lanes":{"b2":{"size":4.0},"v3":{"size":16.0},"v1":{"size":10.0},"b1":{"size":16.0},"v2":{"size":28.0},"v4":{"size":4.0}}})GOLDEN");
    Fixture f;
    auto eng = unwrap(views::ViewEngine::open(f.td.path()));
    check_same_by_label(channels_by_label(unwrap(eng.evaluate_declaration(f.ctx(), legacy_scale_declaration(), Json{{"focus", f.v2}}))), golden["legacy"]);
    check_same_by_label(channels_by_label(unwrap(eng.evaluate(f.ctx(), "history_cone", Json{{"focus", f.v2}}))), golden["history_cone"]);
    check_same_by_label(channels_by_label(unwrap(eng.evaluate(f.ctx(), "age_lanes", Json{{"focus", f.v2}}))), golden["age_lanes"]);
  }
}
