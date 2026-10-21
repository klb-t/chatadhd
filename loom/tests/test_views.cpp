// P24 (c, d, b): the effective view (ROM + memory overlays), edge kinds as data, perspectives side by side.
#include <doctest/doctest.h>

#include <filesystem>

#include "loom/effective_view.h"
#include "loom/perspectives.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json op(const char* view, const char* verb, const char* id, Json extra = Json::object()) {
  Json o{{"view", view}, {"op", verb}, {"id", id}};
  for (auto it = extra.begin(); it != extra.end(); ++it) o[it.key()] = it.value();
  return o;
}

views::Overlay overlay(const char* name, Json ops, double order = 0) {
  views::Overlay o;
  o.name = name;
  o.ops = std::move(ops);
  o.order = order;
  return o;
}

}  // namespace

TEST_SUITE("views") {
  TEST_CASE("compose: add, modify (deep patch, null removes), hide, disable, alias - with provenance, ROM untouched") {
    const Json rom = Json{{"a", Json{{"x", 1}, {"n", Json{{"p", 1}, {"q", 2}}}}}, {"b", Json{{"x", 2}}}, {"c", Json{{"x", 3}}}};
    const Json rom_before = rom;
    auto eff = views::compose("things", rom, {
        overlay("one", Json::array({op("things", "add", "d", Json{{"entry", Json{{"x", 4}}}}),
                                    op("things", "modify", "a", Json{{"patch", Json{{"n", Json{{"p", 9}, {"q", nullptr}}}}}}),
                                    op("things", "hide", "b"), op("things", "disable", "c"),
                                    op("things", "alias", "a", Json{{"as", "alpha"}}),
                                    op("other", "add", "zzz", Json{{"entry", Json::object()}})})),
        overlay("two", Json::array({op("things", "modify", "d", Json{{"patch", Json{{"y", true}}}})}), 1)});
    CHECK(rom == rom_before);  // the ROM is data that nobody modifies
    CHECK(eff.entries.size() == 3);  // a, c, d (b hidden)
    const auto* a = eff.find("alpha");
    REQUIRE(a);
    CHECK(a->id == "a");
    CHECK(a->value["n"] == Json{{"p", 9}});
    CHECK(a->origin == "rom");
    REQUIRE(a->provenance.size() == 3);  // rom, modify, alias
    CHECK(a->provenance[1]["layer"] == "overlay:one");
    CHECK(a->provenance[1]["op"] == "modify");
    CHECK(eff.find("b") == nullptr);
    REQUIRE(eff.hidden.size() == 1);
    CHECK(eff.hidden[0]["by"] == "overlay:one");
    const auto* c = eff.find("c");
    REQUIRE(c);
    CHECK(c->disabled);
    CHECK(eff.active().size() == 2);
    const auto* d = eff.find("d");
    REQUIRE(d);
    CHECK(d->origin == "overlay:one");
    CHECK(d->value["y"] == true);
    CHECK(d->provenance.back()["layer"] == "overlay:two");
    CHECK(eff.issues.empty());  // the op for another view is not ours
  }

  TEST_CASE("policy per kind is data: add_existing keep / issue, modify shallow / replace; bad operations are reported") {
    const Json rom = Json{{"a", Json{{"n", Json{{"p", 1}}}, {"k", 1}}}};
    views::Policy keep;
    keep.add_existing = "keep";
    auto e1 = views::compose("t", rom, {overlay("o", Json::array({op("t", "add", "a", Json{{"entry", Json{{"new", 1}}}})}))}, keep);
    CHECK(e1.find("a")->value.contains("k"));
    CHECK(e1.issues[0]["code"] == "add_existing_kept");
    views::Policy issue;
    issue.add_existing = "issue";
    CHECK(views::compose("t", rom, {overlay("o", Json::array({op("t", "add", "a", Json{{"entry", Json::object()}})}))}, issue).issues[0]["code"] == "add_existing");
    views::Policy shallow;
    shallow.modify = "shallow";
    auto e2 = views::compose("t", rom, {overlay("o", Json::array({op("t", "modify", "a", Json{{"patch", Json{{"n", Json{{"q", 2}}}}}})}))}, shallow);
    CHECK(e2.find("a")->value["n"] == Json{{"q", 2}});
    views::Policy replace;
    replace.modify = "replace";
    CHECK(views::compose("t", rom, {overlay("o", Json::array({op("t", "modify", "a", Json{{"patch", Json{{"z", 1}}}})}))}, replace).find("a")->value == Json{{"z", 1}});
    auto bad = views::compose("t", rom, {overlay("o", Json::array({op("t", "modify", "nope", Json{{"patch", Json::object()}}), op("t", "frobnicate", "a")}))});
    CHECK(bad.issues.size() == 2);
    CHECK(bad.issues[0]["code"] == "modify_unknown");
    CHECK(bad.issues[1]["code"] == "unknown_op");
  }

  TEST_CASE("overlays on disk: put_op / load / effective over the shipped ROM; remove_ops undoes; a malformed file is an error") {
    fsutil::TempDir td;
    const auto base = unwrap(views::effective(td.path(), "edge_kinds"));
    REQUIRE(base.find("calls"));
    CHECK(base.find("calls")->origin == "rom");
    LOOM_REQUIRE_OK(views::put_op(td.path(), "mine", op("edge_kinds", "add", "reviews", Json{{"entry", Json{{"meaning", Json{{"en", "Source reviews target"}}}, {"parent", "relation"}, {"perspectives", Json::array({"calls"})}}}})));
    LOOM_REQUIRE_OK(views::put_op(td.path(), "mine", op("edge_kinds", "hide", "mentions")));
    LOOM_REQUIRE_OK(views::put_op(td.path(), "mine", op("edge_kinds", "alias", "calls", Json{{"as", "invokes"}})));
    auto eff = unwrap(views::effective(td.path(), "edge_kinds"));
    CHECK(eff.find("reviews")->origin == "overlay:mine");
    CHECK(eff.find("mentions") == nullptr);
    CHECK(eff.find("invokes")->id == "calls");
    CHECK(eff.entries.size() == base.entries.size());  // +1 added, -1 hidden
    // ROM file is unchanged: a fresh data dir sees the old list
    fsutil::TempDir other;
    CHECK(unwrap(views::effective(other.path(), "edge_kinds")).find("mentions") != nullptr);
    CHECK(unwrap(views::remove_ops(td.path(), "mine", "edge_kinds", "mentions")) == 1);
    CHECK(unwrap(views::effective(td.path(), "edge_kinds")).find("mentions") != nullptr);
    CHECK_FALSE(views::put_op(td.path(), "../x", op("edge_kinds", "hide", "calls")));
    CHECK_FALSE(views::put_op(td.path(), "mine", op("edge_kinds", "add", "nothing")));  // add needs an entry
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "views" / "broken.json", "{not json"));
    auto broken = views::effective(td.path(), "edge_kinds");
    REQUIRE_FALSE(broken);
    CHECK(broken.error().message.find("broken.json") != std::string::npos);
  }

  TEST_CASE("the user's profile overlay of a source is a layer of its own (true origin); an unreadable one leaves the shipped entries and an issue") {
    fsutil::TempDir td;
    const auto shipped = unwrap(views::effective({}, "perspectives"));
    REQUIRE_FALSE(shipped.entries.empty());
    const std::string some = shipped.entries.front().id;
    // a perspective added and one changed by profiles/graph_views.pack
    unwrap(fsutil::write_file(td.path() / "profiles" / "graph_views.pack",
        Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "graph_views"},
             {"overrides", Json{{"perspectives", Json{{"moja", Json{{"label", "Moja"}, {"relations", Json::array({"mentions"})}}},
                                                      {some, Json{{"label", Json{{"pl", "Przemianowana"}}}}}}}}}}.dump()));
    const auto eff = unwrap(views::effective(td.path(), "perspectives"));
    REQUIRE(eff.find("moja"));
    CHECK(eff.find("moja")->origin == "overlay:profile:graph_views");
    CHECK(eff.find("moja")->provenance[0]["layer"] == "overlay:profile:graph_views");
    REQUIRE(eff.find(some));
    CHECK(eff.find(some)->origin == "rom");
    CHECK(eff.find(some)->value["label"]["pl"] == "Przemianowana");
    CHECK(eff.find(some)->value["label"]["en"] == shipped.find(some)->value["label"]["en"]);  // deep: only the renamed text
    CHECK(eff.find(some)->provenance.back()["fields"] == Json::array({"label.pl"}));
    CHECK(eff.find(some)->provenance.back()["layer"] == "overlay:profile:graph_views");
    CHECK(eff.find(some)->provenance.back()["op"] == "modify");
    CHECK(eff.issues.empty());
    // the same overlay, unreadable: every kind still has its shipped entries, and the issue names the layer
    unwrap(fsutil::write_file(td.path() / "profiles" / "graph_views.pack", "{not json"));
    for (const char* kind : {"perspectives", "interaction_bindings", "edge_kinds", "view_kinds"}) {
      INFO(kind);
      const auto broken = unwrap(views::effective(td.path(), kind));
      const auto base = unwrap(views::effective({}, kind));
      CHECK(broken.entries.size() == base.entries.size());
      REQUIRE_FALSE(broken.issues.empty());
      CHECK(broken.issues[0]["code"] == "profile_overlay_unreadable");
      CHECK(broken.issues[0]["layer"] == "overlay:profile:graph_views");
    }
    CHECK_FALSE(unwrap(views::effective(td.path(), "perspectives")).find("moja"));
  }

  TEST_CASE("N memory overlays compose in (order, name) order; extra memories come after the data dir") {
    fsutil::TempDir a, b;
    LOOM_REQUIRE_OK(views::put_op(a.path(), "z", op("perspectives", "modify", "calls", Json{{"patch", Json{{"hops", 1}}}})));
    LOOM_REQUIRE_OK(views::put_op(b.path(), "a", op("perspectives", "modify", "calls", Json{{"patch", Json{{"hops", 7}}}})));
    auto eff = unwrap(views::effective(a.path(), "perspectives", {b.path()}));
    CHECK(eff.find("calls")->value["hops"] == 7);  // the extra memory is later
    CHECK(eff.layers.size() == 3);
    CHECK(eff.find("calls")->provenance.size() == 3);
  }

  TEST_CASE("view_kinds is a kind itself: a user invents a new kind of thing and fills it by operations") {
    fsutil::TempDir td;
    LOOM_REQUIRE_OK(views::put_op(td.path(), "u", op("view_kinds", "add", "moods", Json{{"entry", Json{{"policy", Json{{"add_existing", "issue"}}}}}})));
    LOOM_REQUIRE_OK(views::put_op(td.path(), "u", op("moods", "add", "calm", Json{{"entry", Json{{"w", 1}}}})));
    LOOM_REQUIRE_OK(views::put_op(td.path(), "v", op("moods", "add", "calm", Json{{"entry", Json{{"w", 2}}}})));
    auto eff = unwrap(views::effective(td.path(), "moods"));
    CHECK(eff.policy.add_existing == "issue");
    CHECK(eff.find("calm")->value["w"] == 1);
    REQUIRE(eff.issues.size() == 1);  // the policy of the new kind refuses to replace an existing id
    CHECK(eff.issues[0]["code"] == "add_existing");
    // a kind nobody declared is still composed (emergent)
    LOOM_REQUIRE_OK(views::put_op(td.path(), "u", op("undeclared", "add", "x", Json{{"entry", Json::object()}})));
    CHECK(unwrap(views::effective(td.path(), "undeclared")).find("x") != nullptr);
  }

  TEST_CASE("the shipped kinds are consistent: parents, inverses and perspectives resolve; the S2 environment kinds are classified") {
    fsutil::TempDir td;
    auto kinds = unwrap(views::EdgeKinds::open(td.path()));
    auto persp = unwrap(views::effective(td.path(), "perspectives"));
    for (const auto& e : kinds.view().entries) {
      const Json d = kinds.describe(e.id, persp);
      CHECK_MESSAGE(!d.contains("issues"), e.id << ": " << d.value("issues", Json::array()).dump());
    }
    for (const char* k : {"has_capability", "accessed_via", "requires_access", "stored_in", "hosted_on", "provided_by"}) CHECK_MESSAGE(!kinds.canonical(k).empty(), k);
    for (const char* n : {"environment", "capability", "access", "secret_vault"}) CHECK_MESSAGE(unwrap(views::effective(td.path(), "node_kinds")).find(n), n);
    // sub-kinds inherit membership: imports is a dependency of a dependency perspective
    CHECK(kinds.expand("dependency").count("imports"));
    CHECK(kinds.perspectives_of("imports", persp).count("dependencies"));
    CHECK(kinds.ancestors("imports") == std::vector<std::string>{"depends_on", "dependency", "relation"});
  }

  TEST_CASE("unknown link types in the data are accepted and land in the unclassified bucket") {
    fsutil::TempDir td;
    auto db = loom::test::open_db(td.path() / "g.db");
    const auto a = unwrap(db->create_node("a", "file")), b = unwrap(db->create_node("b", "file"));
    unwrap(db->create_link(a, b, "imports"));
    unwrap(db->create_link(a, b, "frobnicates"));
    unwrap(db->create_link(b, a, "frobnicates"));
    auto kinds = unwrap(views::EdgeKinds::open(td.path()));
    auto persp = unwrap(views::effective(td.path(), "perspectives"));
    const Json c = kinds.classify(unwrap(views::link_types_in_use(*db)), persp);
    REQUIRE(c["unclassified"].size() == 1);
    CHECK(c["unclassified"][0]["type"] == "frobnicates");
    CHECK(c["unclassified"][0]["count"] == 2);
    CHECK(c["known"][0]["type"] == "imports");
    CHECK(c["known"][0]["perspectives"].dump().find("dependencies") != std::string::npos);
    // an overlay classifies it: it leaves the bucket
    LOOM_REQUIRE_OK(views::put_op(td.path(), "u", op("edge_kinds", "add", "frobnicates", Json{{"entry", Json{{"parent", "calls"}}}})));
    auto kinds2 = unwrap(views::EdgeKinds::open(td.path()));
    CHECK(kinds2.classify(unwrap(views::link_types_in_use(*db)), persp)["unclassified"].empty());
  }

  TEST_CASE("perspectives of a code project side by side: dependencies, calls, version history, teleology") {
    fsutil::TempDir td;
    auto db = loom::test::open_db(td.path() / "g.db");
    auto node = [&](const char* label, const char* kind) { return unwrap(db->create_node(label, kind)); };
    const auto main_c = node("main.c", "file"), util_c = node("util.c", "file"), lib = node("lib", "module");
    const auto f_main = node("main()", "function"), f_util = node("util()", "function");
    const auto v1 = node("v1", "version"), v2 = node("v2", "version"), goal = node("be fast", "goal");
    unwrap(db->create_link(main_c, util_c, "imports"));
    unwrap(db->create_link(util_c, lib, "requires"));
    unwrap(db->create_link(f_main, f_util, "calls"));
    unwrap(db->create_link(v2, v1, "previous_version"));
    unwrap(db->create_link(v2, main_c, "version_of"));
    unwrap(db->create_link(util_c, goal, "serves_goal"));
    unwrap(db->create_link(main_c, goal, "mentions"));
    auto p = unwrap(views::Perspectives::open(td.path()));
    views::RenderContext ctx;
    ctx.db = db.get();
    ctx.root = td.path();
    const Json dep = unwrap(p.render(ctx, "dependencies", Json::object()));
    CHECK(dep["edges"].size() == 2);
    CHECK(dep["nodes"].size() == 3);
    const Json calls = unwrap(p.render(ctx, "calls", Json{{"seeds", Json::array({f_main})}}));
    CHECK(calls["edges"].size() == 1);
    CHECK(calls["edges"][0]["inverse"] == "called_by");
    const Json both = unwrap(p.render_many(ctx, {"dependencies", "calls", "version_history", "teleology"}, Json::object()));
    CHECK(both["perspectives"].size() == 4);
    CHECK(both["perspectives"]["version_history"]["edges"].size() == 2);
    CHECK(both["perspectives"]["teleology"]["edges"].size() == 1);  // mentions is not teleology
    CHECK(both["perspectives"]["teleology"]["render"]["layout"] == "force");
    // depth, direction and node filter from the request
    const Json deep = unwrap(p.render(ctx, "dependencies", Json{{"seeds", Json::array({main_c})}, {"hops", 1}}));
    CHECK(deep["edges"].size() == 1);
    const Json inbound = unwrap(p.render(ctx, "dependencies", Json{{"seeds", Json::array({lib})}, {"direction", "in"}, {"hops", nullptr}}));
    CHECK(inbound["edges"].size() == 2);
    // an overlay adds a kind to a perspective by data
    unwrap(db->create_link(f_main, main_c, "reviews"));
    LOOM_REQUIRE_OK(views::put_op(td.path(), "u", op("edge_kinds", "add", "reviews", Json{{"entry", Json{{"perspectives", Json::array({"calls"})}}}})));
    auto p2 = unwrap(views::Perspectives::open(td.path()));
    CHECK(unwrap(p2.render(ctx, "calls", Json::object()))["edges"].size() == 2);
    // a perspective can be disabled; list() carries the traversal layer of graph_perspectives
    LOOM_REQUIRE_OK(views::put_op(td.path(), "u", op("perspectives", "disable", "teleology")));
    auto p3 = unwrap(views::Perspectives::open(td.path()));
    CHECK_FALSE(p3.render(ctx, "teleology", Json::object()));
    bool found = false;
    for (const auto& e : p3.list())
      if (e["id"] == "calls") { found = true; CHECK(e["traversal"]["direction"] == "out"); CHECK(e["traversal"]["hops"] == 3); }
    CHECK(found);
  }
  TEST_CASE("M1-D2: the selector profiles, the new edge kinds, perspectives and the long-press binding are in the effective views with their origin rom") {
    fsutil::TempDir td;
    // the view kind selector_profiles reads chat_context /selector_profiles; each preset has labels, a why, a note and a status
    const auto profiles = unwrap(views::effective(td.path(), "selector_profiles"));
    CHECK(profiles.entries.size() == 8);
    for (const char* id : {"loom", "loom_legacy", "compat", "lean", "graph_in", "like_claude", "like_chatgpt", "like_gemini"}) {
      INFO(id);
      const auto* e = profiles.find(id);
      REQUIRE(e);
      CHECK(e->origin == "rom");
      for (const char* field : {"label", "why", "note"}) {
        CHECK_FALSE(e->value.at(field).at("pl").get<std::string>().empty());
        CHECK_FALSE(e->value.at(field).at("en").get<std::string>().empty());
      }
      const std::string status = e->value.at("status").get<std::string>();
      CHECK((status == "preset" || status == "calibration"));
      for (const char* source : {"profile", "rules", "memory", "project", "history", "graph", "search", "kb"}) CHECK(e->value.at("sources").at(source).is_boolean());
    }
    CHECK(profiles.find("loom")->value.at("order") == "stability");
    CHECK(profiles.find("loom_legacy")->value.at("order") == "legacy");
    CHECK(profiles.find("loom_legacy")->value.at("sources").at("profile") == false);  // today's bytes: no profile prefix
    for (const char* id : {"graph_in", "like_claude", "like_chatgpt", "like_gemini"}) CHECK(profiles.find(id)->value.at("status") == "calibration");
    // the view kind itself is listed
    const auto kinds = unwrap(views::effective(td.path(), "view_kinds"));
    REQUIRE(kinds.find("selector_profiles"));
    CHECK(kinds.find("selector_profiles")->value.at("source").at("pointer") == "/selector_profiles");
    // the edge kinds say what they mean in both languages and hang under relation
    const auto edges = unwrap(views::effective(td.path(), "edge_kinds"));
    for (const char* id : {"grouped_in", "reads_setting", "uses_role", "bound_to", "step_of", "has_default", "proposed_by", "sent_to_model", "applies_to_flow", "interested_in", "member_of", "knows"}) {
      INFO(id);
      const auto* e = edges.find(id);
      REQUIRE(e);
      CHECK(e->value.at("parent") == "relation");
      CHECK_FALSE(e->value.at("meaning").at("pl").get<std::string>().empty());
      CHECK_FALSE(e->value.at("meaning").at("en").get<std::string>().empty());
    }
    // the perspectives: every one is reachable (its edge kinds exist), the receipt one needs a message and says it is planned
    const auto persp = unwrap(views::effective(td.path(), "perspectives"));
    for (const char* id : {"settings_structure", "flow", "sent_to_model", "evidence", "me_and_surroundings"}) {
      INFO(id);
      const auto* e = persp.find(id);
      REQUIRE(e);
      CHECK(e->origin == "rom");
      CHECK_FALSE(e->value.at("label").at("pl").get<std::string>().empty());
      CHECK_FALSE(e->value.at("relations").empty());
      for (const auto& r : e->value.at("relations")) CHECK(edges.find(r.get<std::string>()));
    }
    CHECK(persp.find("sent_to_model")->value.at("source") == "receipt");
    CHECK(persp.find("sent_to_model")->value.at("params").at("msg_id").at("required") == true);
    CHECK(persp.find("sent_to_model")->value.at("status").at("state") == "planned");
    CHECK(persp.find("evidence")->value.at("render").at("color_by") == "evidence.class");
    // the binding is recorded, and it says it is planned
    const auto bindings = unwrap(views::effective(td.path(), "interaction_bindings"));
    REQUIRE(bindings.find("touch.drag.hold"));
    CHECK(bindings.find("touch.drag.hold")->value.at("status").at("state") == "planned");
  }
}
