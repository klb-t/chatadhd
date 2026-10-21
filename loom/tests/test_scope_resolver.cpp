// P24 (a): one resolution chain for any setting: conversation -> units -> user -> installation, with the "dziedziczenie" perspective.
#include <doctest/doctest.h>

#include "loom/annotation_layer.h"
#include "loom/config.h"
#include "loom/perspectives.h"
#include "loom/scope_resolver.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
struct Fixture {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  std::unique_ptr<Database> db = loom::test::open_db(td.path() / "c.db");
  std::string conv = unwrap(db->create_conv("t")).id;
  void conv_meta(const Json& m) {
    ConvPatch patch;
    patch.metadata = m;
    LOOM_REQUIRE_OK(db->update_conv(conv, patch));
  }
};
std::string role_of(const Json& chain, const char* level) {
  for (const auto& l : chain)
    if (l["level"] == level) return l["role"].get<std::string>();
  return "?";
}
}  // namespace

TEST_SUITE("scope_resolver") {
  TEST_CASE("installation, user, unit, conversation: the most specific level supplies, the others are overridden") {
    Fixture f;
    auto r = unwrap(scopes::Resolver::open(f.td.path(), &f.cfg, f.db.get()));
    scopes::Binding b;
    b.key = "my_setting";
    b.installation = "preset";
    b.conv_id = f.conv;
    auto one = unwrap(r.resolve(b));
    CHECK(one.value == "preset");
    CHECK(one.scope == "installation");
    f.cfg.set("my_setting", "user");
    one = unwrap(r.resolve(b));
    CHECK(one.value == "user");
    CHECK(one.scope == "person");
    CHECK(role_of(one.chain, "installation") == "overridden");
    // the conversation belongs to a project (a unit kind from data) which has a value
    f.conv_meta(Json{{"units", Json{{"project", "p_1"}}}});
    b.unit_values = Json{{"project", "project-value"}};
    auto two = unwrap(r.resolve(b));
    CHECK(two.value == "project-value");
    CHECK(two.scope == "project");
    CHECK(two.scope_id == "p_1");
    CHECK(two.units == Json{{"project", "p_1"}});
    CHECK(role_of(two.chain, "person") == "overridden");
    CHECK(role_of(two.chain, "project") == "supplied");
    CHECK(role_of(two.chain, "turn") == "inactive");
    // the conversation's own value is the most specific
    f.conv_meta(Json{{"units", Json{{"project", "p_1"}}}, {"settings", Json{{"my_setting", "conv"}}}});
    auto three = unwrap(r.resolve(b));
    CHECK(three.value == "conv");
    CHECK(three.scope == "conversation");
    CHECK(role_of(three.chain, "project") == "overridden");
    // another conversation, not in the project, does not see the project's value
    auto other = unwrap(f.db->create_conv("o")).id;
    b.conv_id = other;
    b.unit_values = nullptr;
    CHECK(unwrap(r.resolve(b)).value == "user");
  }

  TEST_CASE("M1-K1b: the value of this turn is the most specific level: it wins against the conversation, the inheritance shows `turn`, and nothing is kept") {
    Fixture f;
    f.cfg.set("my_setting", "user");
    f.conv_meta(Json{{"settings", Json{{"my_setting", "conversation-value"}}}});
    auto r = unwrap(scopes::Resolver::open(f.td.path(), &f.cfg, f.db.get()));
    scopes::Binding b;
    b.key = "my_setting";
    b.installation = "preset";
    b.conv_id = f.conv;
    auto without = unwrap(r.resolve(b));
    CHECK(without.value == "conversation-value");
    CHECK(without.scope == "conversation");
    CHECK(role_of(without.chain, "turn") == "inactive");
    b.turn = "this-turn";
    auto with = unwrap(r.resolve(b));
    CHECK(with.value == "this-turn");
    CHECK(with.scope == "turn");
    CHECK(role_of(with.chain, "turn") == "supplied");
    CHECK(role_of(with.chain, "conversation") == "overridden");
    CHECK(role_of(with.chain, "person") == "overridden");
    // the inheritance perspective shows the level and its value
    const Json inh = unwrap(r.inheritance(b));
    CHECK(inh["supplied_by"] == "turn");
    bool turn_node = false;
    for (const auto& n : inh["nodes"])
      if (n["level"] == "turn") { turn_node = true; CHECK(n["value"] == "this-turn"); }
    CHECK(turn_node);
    // value_or tells which level supplied the value; without a turn value the answer is what it always was
    std::string level;
    CHECK(scopes::value_or(f.td.path(), f.cfg, f.db.get(), b, Json("fallback"), &level) == "this-turn");
    CHECK(level == "turn");
    b.turn = nullptr;
    CHECK(scopes::value_or(f.td.path(), f.cfg, f.db.get(), b, Json("fallback"), &level) == "conversation-value");
    CHECK(level == "conversation");
    scopes::Binding none;
    none.key = "no_such_setting";
    CHECK(scopes::value_or(f.td.path(), f.cfg, f.db.get(), none, Json("fallback"), &level) == "fallback");
    CHECK(level.empty());
  }

  TEST_CASE("unit kinds are data: a new kind maps to a chain level by an overlay; an unknown unit is reported, not dropped silently") {
    Fixture f;
    LOOM_REQUIRE_OK(views::put_op(f.td.path(), "u", Json{{"view", "unit_kinds"}, {"op", "add"}, {"id", "workstream"}, {"entry", Json{{"level", "team"}}}}));
    f.conv_meta(Json{{"units", Json{{"workstream", "w_1"}, {"nonsense", "x"}}}});
    auto r = unwrap(scopes::Resolver::open(f.td.path(), &f.cfg, f.db.get()));
    scopes::Binding b;
    b.key = "k";
    b.installation = 1;
    b.conv_id = f.conv;
    b.unit_values = Json{{"team", 2}};
    auto res = unwrap(r.resolve(b));
    CHECK(res.units == Json{{"team", "w_1"}});
    CHECK(res.scope == "team");
    CHECK(res.value == 2);
    REQUIRE(res.ignored.size() == 1);
    CHECK(res.ignored[0]["unit"] == "nonsense");
    CHECK(res.to_json().contains("ignored_units"));
  }

  TEST_CASE("the dziedziczenie perspective: levels as nodes, inherits_from and overrides as edges, rendered through the perspective") {
    Fixture f;
    f.cfg.set("guardian_x", "user");
    f.conv_meta(Json{{"settings", Json{{"guardian_x", "conv"}}}});
    auto p = unwrap(views::Perspectives::open(f.td.path()));
    views::RenderContext ctx;
    ctx.db = f.db.get();
    ctx.cfg = &f.cfg;
    ctx.root = f.td.path();
    const Json g = unwrap(p.render(ctx, "dziedziczenie", Json{{"key", "guardian_x"}, {"conversation_id", f.conv}, {"installation", "preset"}}));
    CHECK(g["perspective"] == "dziedziczenie");
    CHECK(g["supplied_by"] == "conversation");
    CHECK(g["value"] == "conv");
    bool overrides_person = false, inherits = false;
    for (const auto& e : g["edges"]) {
      if (e["type"] == "overrides" && e["dst"] == "scope:person" && e["src"] == "scope:conversation") overrides_person = true;
      if (e["type"] == "inherits_from" && e["src"] == "scope:conversation") inherits = true;
    }
    CHECK(overrides_person);
    CHECK(inherits);
    CHECK(g["overridden"].dump().find("person") != std::string::npos);
    CHECK_FALSE(p.render(ctx, "dziedziczenie", Json::object()));  // needs a key
    // the kinds it uses are classified
    auto kinds = unwrap(views::EdgeKinds::open(f.td.path()));
    CHECK(!kinds.canonical("inherits_from").empty());
    CHECK(!kinds.canonical("overrides").empty());
  }

  TEST_CASE("annotations::resolve_model reads its settings through the chain: the conversation's own setting beats the user's") {
    Fixture f;
    f.cfg.set("guardian_model", "user/model");
    auto plain = annotations::resolve_model(f.cfg, f.td.path(), "guardian", f.conv, "", -1, f.db.get());
    CHECK(plain.model == "user/model");
    f.conv_meta(Json{{"settings", Json{{"guardian_model", "conv/model"}}}});
    auto scoped = annotations::resolve_model(f.cfg, f.td.path(), "guardian", f.conv, "", -1, f.db.get());
    CHECK(scoped.model == "conv/model");
    CHECK(scoped.via == "explicit:guardian_model");
    // another conversation keeps the user's value
    auto other = unwrap(f.db->create_conv("o")).id;
    CHECK(annotations::resolve_model(f.cfg, f.td.path(), "guardian", other, "", -1, f.db.get()).model == "user/model");
  }
}
