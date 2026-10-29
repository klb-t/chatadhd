// R101 D4 (PULPIT_R101_2026-10-28 §5, §17 B1/B2): "to the trash" is a reversible step, "empty" is today's irreversible one.
// A conversation in the trash is metadata.trash written by read-merge-write of the whole metadata (ConvPatch.metadata replaces the JSON);
// memory.trash is a NEW intermediate step (words kept, out of the context) and memory.forget stays irreversible as catalogued.
#include <doctest/doctest.h>

#include <random>
#include <set>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/db.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
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
  Json run(const std::string& action, const Json& args, const Json& element = Json{{"class", "application"}}) {
    return unwrap(rt->actions().run(Json{{"action", action}, {"element", element}, {"args", args}}));
  }
  Json act(const std::string& action, const Json& args, const Json& element = Json{{"class", "application"}}) {
    const Json r = run(action, args, element);
    REQUIRE_MESSAGE(r.at("status") == "completed", action << " " << r.dump());
    return r;
  }
  std::set<std::string> listed(const Json& args = Json::object()) {
    const Json r = act("conversations.list", args);
    std::set<std::string> ids;
    const Json items = r.at("result").at("data").at("items");
    for (const auto& it : items) ids.insert(it.at("id").get<std::string>());
    return ids;
  }
  Json meta(const std::string& id) { return unwrap(rt->db().get_conv(id))->metadata; }
};

}  // namespace

TEST_SUITE("trash") {
  TEST_CASE("conversation.trash takes it off the list, untrash brings it back, nothing else of it changes") {
    Fixture f;
    const auto a = unwrap(f.rt->db().create_conv("Plan wakacji"));
    const auto b = unwrap(f.rt->db().create_conv("Budżet domowy"));
    CHECK(f.listed().count(a.id) == 1);
    const std::string updated_before = unwrap(f.rt->db().get_conv(a.id))->updated;
    const Json t = f.act("conversation.trash", Json{{"query", "wakacji"}, {"from", "desktop"}});
    CHECK(t.at("result").at("data").at("trash").at("from") == "desktop");
    CHECK(f.listed().count(a.id) == 0);
    CHECK(f.listed().count(b.id) == 1);
    CHECK(f.listed(Json{{"trashed", "only"}}) == std::set<std::string>{a.id});
    CHECK(f.listed(Json{{"trashed", "show"}}).count(a.id) == 1);
    CHECK(unwrap(f.rt->db().get_conv(a.id))->updated == updated_before);   // the trash is not talking in it
    // the setting: false shows trashed conversations on the list too
    f.rt->config().set("conversations_hide_trashed", false);
    CHECK(f.listed().count(a.id) == 1);
    f.rt->config().set("conversations_hide_trashed", nullptr);
    // the trash lists it with date and origin
    const Json items = f.act("trash.list", Json::object()).at("result").at("data").at("items");
    REQUIRE(items.size() == 1);
    CHECK(items[0].at("kind") == "conversation");
    CHECK(items[0].at("from") == "desktop");
    CHECK_FALSE(items[0].at("at").get<std::string>().empty());
    f.act("conversation.untrash", Json{{"query", a.id}});
    CHECK(f.listed().count(a.id) == 1);
    CHECK_FALSE(f.meta(a.id).contains("trash"));
  }

  TEST_CASE("emptying is today's irreversible step: conversation.delete and trash.empty ask first and delete for good") {
    Fixture f;
    const Json cat = unwrap(RuntimeProfile::builtin("actions")).values().at("actions");
    CHECK(cat.at("conversation.delete").at("effects").at("irreversible") == true);
    CHECK(cat.at("trash.empty").at("effects").at("irreversible") == true);
    CHECK(cat.at("conversation.trash").at("effects").at("irreversible") == false);
    CHECK(cat.at("memory.forget").at("effects").at("irreversible") == true);   // B1: forgetting stays as catalogued
    CHECK(cat.at("memory.trash").at("effects").at("irreversible") == false);
    const auto a = unwrap(f.rt->db().create_conv("Stara rozmowa"));
    { NewMessage nm; nm.conv_id = a.id; nm.text = "hej"; nm.role = "user"; unwrap(f.rt->db().create_msg(nm)); }
    f.act("conversation.trash", Json{{"query", a.id}});
    const Json asks = f.run("trash.empty", Json::object());
    CHECK(asks.at("status") != "completed");
    CHECK(unwrap(f.rt->db().get_conv(a.id)).has_value());
    REQUIRE(asks.contains("pending"));
    const Json done = unwrap(f.rt->actions().confirm(Json{{"run_id", asks.at("id")}, {"approved", true}, {"receipt_id", asks.at("pending").at("receipt_id")}}));
    CHECK(done.at("status") == "completed");
    CHECK_FALSE(unwrap(f.rt->db().get_conv(a.id)).has_value());
    CHECK(f.act("trash.list", Json::object()).at("result").at("data").at("count") == 0);
    // conversation.delete keeps asking as today
    const auto b = unwrap(f.rt->db().create_conv("Inna"));
    const Json del = f.run("conversation.delete", Json::object(), b.id);
    CHECK(del.at("status") != "completed");
    CHECK(unwrap(f.rt->db().get_conv(b.id)).has_value());
  }

  TEST_CASE("B2, generative: random metadata and random trash/untrash sequences never lose another metadata key") {
    Fixture f;
    std::mt19937 rng(20261029);
    for (int round = 0; round < 40; ++round) {
      const auto c = unwrap(f.rt->db().create_conv("Rozmowa " + std::to_string(round)));
      Json m = Json::object();
      const int keys = static_cast<int>(rng() % 7);
      for (int k = 0; k < keys; ++k) {
        const std::string key = "k" + std::to_string(rng() % 100);
        switch (rng() % 5) {
          case 0: m[key] = static_cast<int>(rng() % 1000); break;
          case 1: m[key] = "tekst " + std::to_string(rng()); break;
          case 2: m[key] = Json{{"nested", Json::array({1, "a", nullptr})}}; break;
          case 3: m[key] = (rng() % 2) == 0; break;
          default: m[key] = nullptr; break;
        }
      }
      if (rng() % 3 == 0) m["unit"] = "u_" + std::to_string(rng() % 50);
      ConvPatch p;
      p.metadata = m;
      unwrap(f.rt->db().update_conv(c.id, p));
      bool trashed = false;
      const int steps = 1 + static_cast<int>(rng() % 6);
      for (int s = 0; s < steps; ++s) {
        const bool to_trash = (rng() % 2) == 0;
        const Json r = f.run(to_trash ? "conversation.trash" : "conversation.untrash", Json{{"query", c.id}});
        if (r.at("status") == "completed") trashed = to_trash;
        Json now = f.meta(c.id);
        CHECK(now.contains("trash") == trashed);
        now.erase("trash");
        CHECK_MESSAGE(now == m, "round " << round << " step " << s << ": " << now.dump() << " != " << m.dump());
      }
    }
  }

  TEST_CASE("B1: memory.trash keeps the words and takes the entry out of the context; untrash gives it back; forget is unchanged") {
    Fixture f;
    auto& mem = f.rt->memory();
    const std::string id = unwrap(mem.add_node("lubię kawę z kardamonem"));
    const std::string other = unwrap(mem.add_node("mam psa o imieniu Burek"));
    CHECK(mem.get_active_context().find("kardamonem") != std::string::npos);
    f.act("memory.trash", Json{{"query", "kardamonem"}});
    auto n = mem.get_node(id);
    REQUIRE(n);
    CHECK(n->content == "lubię kawę z kardamonem");
    CHECK_FALSE(n->active);
    CHECK(n->metadata.contains("trash"));
    CHECK_FALSE(n->metadata.contains("forgotten"));
    CHECK(mem.get_active_context().find("kardamonem") == std::string::npos);
    const Json items = f.act("trash.list", Json::object()).at("result").at("data").at("items");
    REQUIRE(items.size() == 1);
    CHECK(items[0].at("kind") == "memory");
    f.act("trash.restore", Json{{"query", "kardamon"}});
    n = mem.get_node(id);
    CHECK(n->active);
    CHECK_FALSE(n->metadata.contains("trash"));
    CHECK(n->metadata.at("trash_history").size() == 1);
    CHECK(mem.get_active_context().find("kardamonem") != std::string::npos);
    // forget: the same irreversible step as before (asks first, writes the record forgotten)
    const Json asks = f.run("memory.forget", Json{{"query", other}});
    CHECK(asks.at("status") != "completed");
    const Json done = unwrap(f.rt->actions().confirm(Json{{"run_id", asks.at("id")}, {"approved", true}, {"receipt_id", asks.at("pending").at("receipt_id")}}));
    CHECK(done.at("status") == "completed");
    CHECK(mem.get_node(other)->metadata.contains("forgotten"));
    CHECK_FALSE(mem.get_node(other)->metadata.contains("trash"));
  }

  TEST_CASE("a pin to the trash keeps the link with metadata.trash; untrash erases it; empty removes the link") {
    Fixture f;
    f.act("window.pin", Json{{"target", "kind:chat"}});
    f.act("pin.trash", Json{{"target", "kind:chat"}});
    auto links = unwrap(f.rt->db().get_links(std::nullopt, std::string_view("pinned_on")));
    REQUIRE(links.size() == 1);
    CHECK(links[0].metadata.contains("trash"));
    CHECK(links[0].metadata.at("address") == "kind:chat");   // the other keys of the link stay
    f.act("pin.untrash", Json{{"target", "kind:chat"}});
    links = unwrap(f.rt->db().get_links(std::nullopt, std::string_view("pinned_on")));
    CHECK_FALSE(links[0].metadata.contains("trash"));
    f.act("pin.trash", Json{{"target", "kind:chat"}});
    const Json asks = f.run("trash.empty", Json{{"kind", "pin"}});
    unwrap(f.rt->actions().confirm(Json{{"run_id", asks.at("id")}, {"approved", true}, {"receipt_id", asks.at("pending").at("receipt_id")}}));
    CHECK(unwrap(f.rt->db().get_links(std::nullopt, std::string_view("pinned_on"))).empty());
  }

  TEST_CASE("trash_retention_days: unset = never; a number only marks older things as due") {
    Fixture f;
    const auto a = unwrap(f.rt->db().create_conv("Dawna"));
    ConvPatch p;
    p.metadata = Json{{"trash", Json{{"at", "2020-01-01T00:00:00Z"}, {"from", "desktop"}}}};
    unwrap(f.rt->db().update_conv(a.id, p));
    Json r = f.act("trash.list", Json::object()).at("result").at("data");
    CHECK(r.at("retention").at("days").is_null());
    CHECK(r.at("retention").at("due") == 0);
    f.rt->config().set("trash_retention_days", 30);
    r = f.act("trash.list", Json::object()).at("result").at("data");
    CHECK(r.at("retention").at("due") == 1);
    CHECK(unwrap(f.rt->db().get_conv(a.id)).has_value());   // marked, never emptied by itself
  }

  TEST_CASE("sentences pl+en reach the trash actions with their arguments") {
    Fixture f;
    const auto conv = unwrap(f.rt->db().create_conv("x"));
    struct Case { const char* text; const char* action; const char* arg; const char* value; };
    const std::vector<Case> cases = {
        {"przenieś rozmowę o wakacjach do kosza", "conversation.trash", "query", "wakacjach"},
        {"przenieś tę rozmowę do kosza", "conversation.trash", nullptr, nullptr},
        {"move the conversation about holidays to the trash", "conversation.trash", "query", "holidays"},
        {"przenieś wpis pamięci o kawie do kosza", "memory.trash", "query", "kawie"},
        {"przywróć z kosza Plan wakacji", "trash.restore", "query", "Plan wakacji"},
        {"przywróć Plan wakacji z kosza", "trash.restore", "query", "Plan wakacji"},
        {"restore from the trash holidays", "trash.restore", "query", "holidays"},
        {"przywróć rozmowę o wakacjach z kosza", "conversation.untrash", "query", "wakacjach"},
        {"opróżnij kosz", "trash.empty", nullptr, nullptr},
        {"empty the trash", "trash.empty", nullptr, nullptr},
        {"pokaż kosz", "trash.list", nullptr, nullptr},
        {"przenieś przypięcie kind:chat do kosza", "pin.trash", "target", "kind:chat"},
    };
    for (const auto& c : cases) {
      const Json routed = unwrap(chat_route::route(*f.rt, Json{{"text", c.text}, {"conv_id", conv.id}}));
      const std::string reached = routed.value("kind", "") == "command" ? routed.at("command").value("action", "") : "";
      CHECK_MESSAGE(reached == c.action, "\"" << c.text << "\" reached " << (reached.empty() ? "[a message]" : reached));
      if (reached != c.action || !c.arg) continue;
      const Json args = routed.at("command").value("args", Json::object());
      CHECK_MESSAGE(args.value(c.arg, "") == std::string(c.value), "\"" << c.text << "\": " << args.dump());
    }
  }
}
