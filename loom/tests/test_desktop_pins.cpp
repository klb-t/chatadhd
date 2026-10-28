// R101 D1 (PULPIT_R101_2026-10-28 §4.2, §4.3): pinning on a desktop is the graph link `pinned_on` (thing -> desktop:<id>) in the links table;
// addresses that are not rows get a representative node whose kind comes from graph_views.pack -> node_addresses (an unknown prefix is kept as
// `address`, never refused); the perspective `pinned_on` reads the icons of a desktop; window.unpin removes the link and nothing else.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <set>

#include "loom/action_runner.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/perspectives.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
  }
  Json act(const std::string& action, const Json& args) {
    const Json r = unwrap(rt->actions().run(Json{{"action", action}, {"element", Json{{"class", "application"}}}, {"args", args}}));
    REQUIRE_MESSAGE(r.at("status") == "completed", action << " " << r.dump());
    return r;
  }
  std::vector<Link> pins() { return unwrap(rt->db().get_links(std::nullopt, std::string_view("pinned_on"))); }
  std::set<std::string> icons_of(const std::string& desktop_node) {
    const auto p = unwrap(views::Perspectives::open(td.path()));
    views::RenderContext ctx;
    ctx.db = &rt->db();
    ctx.cfg = &rt->config();
    ctx.root = td.path();
    const Json out = unwrap(p.render(ctx, "pinned_on", Json{{"seeds", Json::array({desktop_node})}}));
    std::set<std::string> ids;
    const Json nodes = out.at("nodes");
    for (const auto& n : nodes) {
      const std::string id = n.is_object() ? n.value("id", std::string()) : n.get<std::string>();
      if (id != desktop_node) ids.insert(id);
    }
    return ids;
  }
};

}  // namespace

TEST_SUITE("desktop_pins") {
  TEST_CASE("pinned_on is a kernel edge kind of the perspective pinned_on; desktop/trash/window_kind are node kinds") {
    Fixture f;
    const auto p = unwrap(views::Perspectives::open(f.td.path()));
    CHECK(p.kinds().canonical("pinned_on") == "pinned_on");
    const auto ek = unwrap(p.edge_kinds_of("pinned_on"));
    CHECK(ek.count("pinned_on") == 1);
    const Json list = p.list();
    CHECK(std::any_of(list.begin(), list.end(), [](const Json& e) { return e.value("id", std::string()) == "pinned_on"; }));
  }

  TEST_CASE("window.pin writes pinned_on node -> desktop with representatives; the perspective reads it; window.unpin reverses it") {
    Fixture f;
    const auto conv = unwrap(f.rt->db().create_conv("Pinned talk"));
    const std::string conv_addr = "conv:" + conv.id.substr(2);
    const Json a = f.act("window.pin", Json{{"target", "kind:chat"}});
    const Json b = f.act("window.pin", Json{{"target", conv_addr}, {"label", "Talk"}});
    const Json c = f.act("window.pin", Json{{"target", "weird:thing"}});
    const auto links = f.pins();
    REQUIRE(links.size() == 3);
    const std::string desk = links.front().dst;
    for (const auto& l : links) CHECK(l.dst == desk);
    const auto dnode = unwrap(f.rt->db().get_node(desk));
    REQUIRE(dnode);
    CHECK(dnode->kind == "desktop");
    CHECK(dnode->label == "desktop:main");
    // representatives: kind from node_addresses, an unknown prefix kept as `address`
    const auto chat = unwrap(f.rt->db().find_node("kind:chat", std::string_view("window_kind")));
    REQUIRE(chat);
    CHECK(chat->metadata.at("address") == "kind:chat");
    CHECK(unwrap(f.rt->db().find_node("weird:thing", std::string_view("address"))));
    CHECK(unwrap(f.rt->db().find_node(conv_addr, std::string_view("conversation"))));
    // metadata of the link: order, label_override, address
    std::set<std::int64_t> orders;
    for (const auto& l : links) {
      orders.insert(l.metadata.at("order").get<std::int64_t>());
      if (l.metadata.at("address") == conv_addr) CHECK(l.metadata.at("label_override") == "Talk");
    }
    CHECK(orders == std::set<std::int64_t>{0, 1, 2});
    // the perspective pinned_on reads the three icons of the desktop
    const auto icons = f.icons_of(desk);
    CHECK(icons.size() == 3);
    CHECK(icons.count(chat->id) == 1);
    // pinning again changes nothing
    f.act("window.pin", Json{{"target", "kind:chat"}});
    CHECK(f.pins().size() == 3);
    // unpin removes exactly that link; the node stays; pinning again restores it
    f.act("window.unpin", Json{{"target", "kind:chat"}});
    CHECK(f.pins().size() == 2);
    CHECK(unwrap(f.rt->db().get_node(chat->id)));
    CHECK(f.icons_of(desk).count(chat->id) == 0);
    f.act("window.unpin", Json{{"target", "kind:chat"}});  // a second unpin is a noop, not an error
    CHECK(f.pins().size() == 2);
    f.act("window.pin", Json{{"target", "kind:chat"}});
    CHECK(f.pins().size() == 3);
    CHECK(f.icons_of(desk).count(chat->id) == 1);
    // another desktop has its own pins
    f.act("window.pin", Json{{"target", "trash:"}, {"desktop", "desktop:work"}});
    const auto work = unwrap(f.rt->db().find_node("desktop:work", std::string_view("desktop")));
    REQUIRE(work);
    CHECK(f.icons_of(work->id).size() == 1);
    CHECK(f.icons_of(desk).size() == 3);
    (void)a; (void)b; (void)c;
  }

  TEST_CASE("generative: any sequence of pin/unpin leaves exactly the pinned set as pinned_on links, one per (thing, desktop)") {
    Fixture f;
    const std::vector<std::string> things = {"kind:chat", "kind:settings", "party:self", "trash:", "conversations:", "sentence:hello", "x:y"};
    const std::vector<std::string> desks = {"desktop:main", "desktop:b"};
    std::set<std::pair<std::string, std::string>> model;
    unsigned seed = 1013;
    auto rnd = [&seed](unsigned n) { seed = seed * 1103515245u + 12345u; return (seed >> 8) % n; };
    for (int i = 0; i < 120; ++i) {
      const std::string t = things[rnd(static_cast<unsigned>(things.size()))], d = desks[rnd(2)];
      const bool pin = rnd(3) != 0;
      f.act(pin ? "window.pin" : "window.unpin", Json{{"target", t}, {"desktop", d}});
      if (pin) model.insert({t, d}); else model.erase({t, d});
    }
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& l : f.pins()) {
      const auto inserted = seen.insert({l.metadata.at("address").get<std::string>(), l.metadata.at("desktop").get<std::string>()}).second;
      CHECK_MESSAGE(inserted, "a duplicate pin " << l.metadata.dump());
    }
    CHECK(seen == model);
  }
}
