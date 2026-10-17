// M1-K0: the graph projection (src/graph/projection.h) - the ROM, settings, You, conversations, flows, roles and perspectives as nodes of
// one navigable graph, read on demand from runtime/graph_sources.pack; graph.navigate / graph.card / settings.resolved / roles.resolved /
// rom.override through loom_invoke("actions", ...).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>

#include "graph/projection.h"
#include "loom/action_runner.h"
#include "loom/action_table.h"
#include "loom/annotation_layer.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/effective_view.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/perspectives.h"
#include "loom/port_profile.h"
#include "loom/rom.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/scope_resolver.h"
#include "loom/settings_fields.h"
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
  }
  projection::Env env(const std::string& lang = "en") {
    return projection::env_of(*rt, Json{{"lang", lang}});
  }
  Json navigate(const Json& request) { return unwrap(rt->actions().invoke("graph.navigate", request)); }
};

const Json* child(const Json& nav, const std::string& address) {
  for (const auto& c : nav.at("children"))
    if (c.at("address") == address) return &c;
  return nullptr;
}

const std::set<std::string> kEvidence = {"declared", "observed", "derived", "inferred", "extrapolated"};

void check_node(const Json& n) {
  INFO(n.dump());
  CHECK_FALSE(n.at("address").get<std::string>().empty());
  CHECK(n.at("provenance").is_object());
  CHECK_FALSE(json::get_string(n.at("provenance"), "layer").empty());
  CHECK(kEvidence.count(n.at("evidence").at("class").get<std::string>()));
  CHECK(n.at("evidence").at("provenance").contains("method"));
}

}  // namespace

TEST_SUITE("graph_projection") {
  TEST_CASE("the root lists the areas of the entry view with You as a row of its own; an area no source lists says why, never 0") {
    Fixture f;
    const Json root = f.navigate(Json::object());
    CHECK(root.at("schema") == "loom.graph_navigate/1");
    CHECK(root.at("focus").at("address") == "");
    for (const char* a : {"area:rom", "area:party_self", "area:profiles_settings", "area:flows", "area:providers_connectors", "area:devices_io",
                          "area:tools_agent", "area:environments", "area:automations", "area:experiments", "area:context_projects"})
      CHECK_MESSAGE(child(root, a), a);
    CHECK(root.at("children").at(0).at("address") == "area:rom");  // the ROM is mounted first
    for (const auto& c : root.at("children")) {
      check_node(c);
      // a count is known or says why it is not
      CHECK((c.contains("count") || c.contains("count_unknown_reason")));
      if (c.contains("count_unknown_reason")) CHECK_FALSE(c.contains("count"));
    }
    CHECK(child(root, "area:rom")->at("count") == 13);
    // automations has counters (worker.status) but no source lists its items yet
    CHECK(child(root, "area:automations")->at("count_unknown_reason") == "no_projection_source");
    CHECK(child(root, "area:automations")->at("counts").contains("messages_waiting"));
    // the root's containment edges carry the meaning of their kind
    for (const auto& e : root.at("edges")) CHECK(e.at("meaning").is_string());
  }

  TEST_CASE("every ROM item, settings field, profile value, role, flow and perspective is a node with provenance and evidence") {
    Fixture f;
    auto cat = unwrap(projection::Catalogue::open(f.env()));
    auto rom = unwrap(rom::Rom::open());
    for (const auto& id : rom.ids()) {
      auto n = unwrap(cat.node("rom:" + id));
      REQUIRE_MESSAGE(n, id);
      check_node(*n);
      CHECK(n->at("evidence").at("class") == "declared");
      CHECK(n->at("provenance").at("layer") == "rom");
    }
    const Json doc = unwrap(settings_fields());
    std::size_t fields = 0;
    for (const auto& field : doc.at("fields")) {
      auto n = unwrap(cat.node("setting:" + field.at("key").get<std::string>()));
      REQUIRE_MESSAGE(n, field.at("key"));
      check_node(*n);
      CHECK(n->at("parent") == "group:" + field.at("human").at("group").get<std::string>());
      ++fields;
    }
    for (const auto& pv : doc.at("profile_values")) {
      const std::string key = pv.at("profile").get<std::string>() + pv.at("pointer").get<std::string>();
      auto n = unwrap(cat.node("setting:" + key));
      REQUIRE_MESSAGE(n, key);
      check_node(*n);
    }
    CHECK(fields == doc.at("fields").size());
    auto ports = unwrap(PortProfile::load(f.td.path()));
    for (const auto& role : ports.roles()) {
      auto n = unwrap(cat.node("role:" + role.get<std::string>()));
      REQUIRE_MESSAGE(n, role);
      check_node(*n);
    }
    for (const auto& a : unwrap(action_catalogue(f.td.path())).at("actions")) {
      auto n = unwrap(cat.node("flow:" + a.at("id").get<std::string>()));
      REQUIRE_MESSAGE(n, a.at("id"));
      check_node(*n);
    }
    auto persp = unwrap(views::Perspectives::open(f.td.path()));
    for (const auto& p : persp.list()) {
      auto n = unwrap(cat.node("perspective:" + p.at("id").get<std::string>()));
      REQUIRE_MESSAGE(n, p.at("id"));
      check_node(*n);
    }
    // a flow carries its steps as children (a description, not an execution) and its roles and settings as edges
    const Json flow = f.navigate(Json{{"focus", "flow:chat.send"}});
    CHECK(flow.at("counts").at("total").get<int>() >= 1);
    for (const auto& c : flow.at("children")) CHECK(c.at("kind") == "flow_step");
    bool uses_role = false;
    for (const auto& e : unwrap(cat.edges())) uses_role = uses_role || (e.at("src") == "flow:chat.send" && e.at("kind") == "uses_role" && e.at("dst") == "role:chat.default");
    CHECK(uses_role);
  }

  TEST_CASE("navigating the root and the settings opens the scope-chain Resolver at most once, however many fields are shown") {
    Fixture f;
    for (const char* focus : {"", "area:profiles_settings", "group:modele", "group:interfejs"}) {
      const auto before = scopes::Resolver::opens();
      const Json nav = f.navigate(Json{{"focus", focus}});
      const auto opened = scopes::Resolver::opens() - before;
      MESSAGE("navigate '" << std::string(focus) << "' opened the Resolver " << opened << " time(s) for " << nav.at("children").size() << " children");
      CHECK(opened <= 1);
      CHECK(nav.at("resolver_opens").get<int>() <= 1);
    }
    const auto before = scopes::Resolver::opens();
    const Json all = unwrap(f.rt->actions().invoke("settings.resolved", Json::object()));
    const auto opened = scopes::Resolver::opens() - before;
    MESSAGE("settings.resolved opened the Resolver " << opened << " time(s) for " << all.at("counts").at("fields") << " fields");
    CHECK(opened == 1);
  }

  TEST_CASE("settings.resolved: every field of the pack and every profile value, each in its group; the chip is the level the scope chain names") {
    Fixture f;
    const std::string conv = unwrap(f.rt->db().create_conv("Work")).id;
    f.rt->config().set("temperature", 0.3);
    ConvPatch patch;
    patch.metadata = Json{{"settings", Json{{"max_tokens", 1234}}}};
    LOOM_REQUIRE_OK(f.rt->db().update_conv(conv, patch));
    const Json r = unwrap(f.rt->actions().invoke("settings.resolved", Json{{"conv_id", conv}, {"lang", "pl"}}));
    const Json doc = unwrap(settings_fields(f.td.path()));
    std::size_t in_groups = 0;
    for (const auto& g : r.at("groups")) in_groups += g.at("fields").size();
    // counted against the pack, never a literal
    CHECK(in_groups + r.at("hidden_keys").size() == doc.at("fields").size() + doc.at("profile_values").size());
    CHECK(r.at("profile_values").size() == doc.at("profile_values").size());
    CHECK(r.at("counts").at("fields") == in_groups);
    auto resolver = unwrap(scopes::Resolver::open(f.td.path(), &f.rt->config(), &f.rt->db()));
    std::map<std::string, Json> by_key;
    for (const auto& g : r.at("groups"))
      for (const auto& field : g.at("fields")) {
        by_key[field.at("key").get<std::string>()] = field;
        CHECK(field.at("proposals").empty());
        CHECK(field.at("human").at("label").is_string());
        if (field.contains("secret") || field.contains("profile_value")) continue;
        scopes::Binding b;
        b.key = field.at("key").get<std::string>();
        b.conv_id = conv;
        for (const auto& d : doc.at("fields"))
          if (d.at("key") == b.key && d.contains("default_value")) b.installation = d.at("default_value");
        const auto res = unwrap(resolver.resolve(b));
        INFO(b.key);
        const std::string scope = res.defined ? res.scope : std::string("none");
        CHECK(field.at("provenance").at("scope") == scope);
        if (res.defined) CHECK(field.at("provenance").at("chip").at("id") == scope);
        CHECK(field.at("value") == (res.defined ? res.value : Json(nullptr)));
      }
    CHECK(by_key.at("temperature").at("provenance").at("scope") == "person");
    CHECK(by_key.at("temperature").at("evidence").at("class") == "observed");
    CHECK(by_key.at("temperature").at("value_human") == "0,3");  // the value in words, in the language asked
    CHECK(by_key.at("max_tokens").at("provenance").at("scope") == "conversation");
    // the whole chain, with the unit levels that have no unit ("bez projektu")
    bool no_project = false;
    for (const auto& l : by_key.at("temperature").at("inheritance")) no_project = no_project || (l.at("level") == "project" && l.value("no_unit", false));
    CHECK(no_project);
    // a secret never shows a value
    CHECK(by_key.at("api_key").at("value").is_null());
    CHECK(by_key.at("api_key").at("provenance").at("chip").at("id") == "secret");
    // the preset kept in config.json is said, and stays declared
    const Json& base = by_key.at("base_url");
    if (base.at("provenance").at("scope") == "person" && base.at("provenance").value("same_as_preset", false))
      CHECK(base.at("evidence").at("class") == "declared");
    CHECK(r.at("proposals_status").at("state") == "planned");
  }

  TEST_CASE("conv:<id> is c_<id>, msg:<id> is m_<id>; a nodes row of kind message is the same node and is counted once") {
    Fixture f;
    const std::string conv = unwrap(f.rt->db().create_conv("Identity")).id;
    std::vector<std::string> msgs;
    for (const char* role : {"user", "assistant"}) {
      NewMessage m;
      m.conv_id = conv;
      m.role = role;
      m.text = std::string(role) + " writes about Friday.";
      msgs.push_back(unwrap(f.rt->db().create_msg(m)));
    }
    NodeOptions o;
    o.metadata = Json{{"message_id", msgs[0]}};
    const std::string row = unwrap(f.rt->db().create_node(msgs[0], "message", o));
    REQUIRE(conv.rfind("c_", 0) == 0);
    const std::string address = "conv:" + conv.substr(2);
    // the database id and the projection address name the same node
    const Json by_db = f.navigate(Json{{"focus", conv}});
    CHECK(by_db.at("focus").at("address") == address);
    const Json by_address = f.navigate(Json{{"focus", address}});
    CHECK(by_address.at("children") == by_db.at("children"));
    CHECK(by_db.at("counts").at("total") == 2);  // two messages, the nodes row is not a third
    CHECK(by_db.at("children").size() == 2);
    const Json* first = child(by_db, "msg:" + msgs[0].substr(2));
    REQUIRE(first);
    CHECK(first->at("same_as").size() == 2);  // m_<id> and the nodes row
    // the nodes row is joined by a same_as edge in the card's relations
    const Json card = unwrap(projection::card(f.env(), Json{{"address", "msg:" + msgs[0].substr(2)}}));
    REQUIRE(card.at("relations").contains("same_as"));
    CHECK(card.at("relations").at("same_as").at("in").at(0).at("address") == row);
    // the nodes row and m_<id> are that message
    CHECK(f.navigate(Json{{"focus", row}}).at("focus").at("address") == "msg:" + msgs[0].substr(2));
    CHECK(f.navigate(Json{{"focus", msgs[1]}}).at("focus").at("address") == "msg:" + msgs[1].substr(2));
    // the conversation is listed once in its area, with its messages counted
    const Json area = f.navigate(Json{{"focus", "area:context_projects"}});
    const Json* c = child(area, address);
    REQUIRE(c);
    CHECK(c->at("count") == 2);
    CHECK(c->at("evidence").at("class") == "observed");
  }

  TEST_CASE("conversations are paged by loom_graph_data_limit; the total counts all of them") {
    Fixture f;
    for (int i = 0; i < 3; ++i) unwrap(f.rt->db().create_conv("Conversation " + std::to_string(i)));
    f.rt->config().set("loom_graph_data_limit", 2);
    const Json area = f.navigate(Json{{"focus", "area:context_projects"}, {"limit", 0}});
    std::size_t convs = 0;
    for (const auto& c : area.at("children")) convs += c.at("kind") == "conversation";
    CHECK(convs == 2);
    CHECK(area.at("counts").at("total").get<int>() >= 3);
    CHECK(area.at("pages").at("conv").at("next_offset") == 2);
    const Json next = f.navigate(Json{{"focus", "area:context_projects"}, {"limit", 0}, {"offset", 2}});
    convs = 0;
    for (const auto& c : next.at("children")) convs += c.at("kind") == "conversation";
    CHECK(convs == 1);
  }

  TEST_CASE("an inference is never rendered as observed: interview slots, memory notes and topics carry their own evidence") {
    Fixture f;
    projection::Env env = f.env("pl");
    env.profile = Json{{"fields", Json{{"communication.style", Json{{"status", "known"}, {"value", "direct"}, {"category", "communication"},
                                                                 {"provenance", Json{{"method", "model_inferred"}, {"time", "2026-10-16T10:00:00Z"}}}}},
                                       {"identity.description", Json{{"status", "known"}, {"value", "a developer"}, {"category", "identity"},
                                                                    {"provenance", Json{{"method", "explicit"}, {"message_id", "m_1"}}}}},
                                       {"knowledge.avoid", Json{{"status", "never"}, {"category", "knowledge"}}}}}};
    unwrap(f.rt->memory().add_node("I prefer short answers", std::nullopt, std::nullopt, Json{{"provenance", Json{{"method", "model_inferred"}}}}));
    unwrap(f.rt->memory().add_node("Call me Marcin"));
    NodeOptions topic;
    unwrap(f.rt->db().create_node("rust", "topic", topic));
    const Json you = unwrap(projection::navigate(env, Json{{"focus", "party:self"}, {"limit", 0}}));
    std::size_t inferred = 0, observed = 0;
    for (const auto& c : you.at("children")) {
      const std::string address = c.at("address");
      const std::string cls = c.at("evidence").at("class");
      const std::string method = c.at("evidence").at("provenance").at("method");
      INFO(c.dump());
      if (method == "model_inferred") {
        CHECK(cls == "inferred");
        ++inferred;
      }
      if (address == "party:self/identity.description") {
        CHECK(cls == "observed");
        ++observed;
      }
      if (address == "party:self/knowledge.avoid") CHECK(c.at("status") == "never");
      if (address.rfind("interest:", 0) == 0) CHECK(cls == "inferred");
      CHECK_FALSE((cls == "observed" && method == "model_inferred"));
    }
    CHECK(inferred == 2);  // the slot a model proposed and the note a model inferred
    CHECK(observed == 1);
    CHECK(child(you, "interest:rust"));
    CHECK(child(you, "party:self/communication.style")->at("label") == "Jak do Ciebie mówić");
  }

  TEST_CASE("graph.card: value, why, the whole chain, relations by edge kind, the actions that fit and the evidence of each property") {
    Fixture f;
    const Json card = unwrap(f.rt->actions().invoke("graph.card", Json{{"address", "setting:temperature"}, {"lang", "pl"}}));
    CHECK(card.at("schema") == "loom.graph_card/1");
    CHECK(card.at("why").is_string());
    CHECK(card.at("effect").is_string());
    CHECK(card.at("edit_rule").is_string());
    CHECK_FALSE(card.at("inheritance").empty());
    CHECK(card.at("relations").contains("grouped_in"));
    CHECK(card.at("relations").at("grouped_in").at("out").at(0).at("address") == "group:rozmowa");
    CHECK(card.at("actions").is_array());
    CHECK(card.at("evidence_per_property").contains("value"));
    CHECK(card.at("breadcrumbs").size() >= 2);
    // a role's card is its row of roles.resolved
    const Json role = unwrap(projection::card(f.env(), Json{{"address", "role:analysis.fast"}}));
    REQUIRE(role.contains("role"));
    CHECK(role.at("role").at("policy") == "analyzer");
    CHECK_FALSE(role.at("role").at("chain").empty());
    // a ROM item's card says where your copy would go
    const Json item = unwrap(projection::card(f.env(), Json{{"address", "rom:boot"}}));
    CHECK(item.at("can_override").at("action") == "rom.override");
    CHECK_FALSE(projection::card(f.env(), Json{{"address", "rom:no_such_item"}}));
  }

  TEST_CASE("roles.resolved: every role of the ports profile with its policy chain, the model resolve_model gives and an honest cost") {
    Fixture f;
    f.rt->config().set("semantic_model", "anthropic/claude-haiku-4-5");
    const Json r = unwrap(f.rt->actions().invoke("roles.resolved", Json::object()));
    auto ports = unwrap(PortProfile::load(f.td.path()));
    CHECK(r.at("roles").size() == ports.roles().size());
    const Json* analysis = nullptr;
    for (const auto& role : r.at("roles"))
      if (role.at("id") == "analysis.fast") analysis = &role;
    REQUIRE(analysis);
    const auto choice = annotations::resolve_model(f.rt->config(), f.td.path(), "analyzer", "");
    CHECK(analysis->at("model") == choice.model);
    CHECK(analysis->at("tier").at("id") == choice.tier);
    CHECK_FALSE(analysis->at("tier").at("reason").get<std::string>().empty());
    CHECK(analysis->at("flow").at("id") == choice.flow);
    CHECK(analysis->at("chain").at(0).at("setting") == "semantic_model");
    CHECK(analysis->at("chain").at(0).at("decides") == true);
    CHECK(analysis->contains("mode"));
    // the cost is a price or says why not; the week is unknown with its reason (the ledger records actions)
    const Json cost = analysis->at("cost");
    CHECK((cost.contains("per_million") || cost.contains("per_million_unknown_reason")));
    CHECK(cost.at("last_7d_unknown_reason") == "ledger_records_actions_not_roles");
    bool reads = false;
    for (const auto& fl : analysis->at("flows_reading")) reads = reads || fl.at("id") == "semantic.analyse";
    CHECK(reads);
  }

  TEST_CASE("rom.override writes your copy into the overlay after a confirmation; the ROM node then says overlay") {
    Fixture f;
    const Json patch{{"label", Json{{"pl", "Mój rozruch"}, {"en", "My boot"}}}};
    const std::filesystem::path overlay = f.td.path() / "profiles" / "graph_entry.pack";
    const Json asked = unwrap(f.rt->actions().invoke("rom.override", Json{{"id", "boot"}, {"patch", patch}}));
    INFO(asked.dump());
    CHECK(asked.at("status") != "completed");  // confirm:rom.override
    CHECK_FALSE(std::filesystem::exists(overlay));
    const Json done = unwrap(f.rt->actions().confirm(Json{{"run_id", asked.at("id")}, {"approved", true}, {"receipt_id", asked.at("pending").at("receipt_id")}, {"ref", "api"}}));
    CHECK(done.at("status") == "completed");
    CHECK(std::filesystem::exists(overlay));
    const Json rom = f.navigate(Json{{"focus", "area:rom"}, {"lang", "pl"}});
    const Json* boot = child(rom, "rom:boot");
    REQUIRE(boot);
    CHECK(boot->at("label") == "Mój rozruch");
    CHECK(boot->at("provenance").at("layer") == "overlay:graph_entry");
    CHECK(boot->contains("evidence_overrides"));
    // the ROM itself is unchanged
    CHECK(unwrap(rom::Rom::open()).read("boot")->at("label").at("pl") == "Rozruch");
    // a patch that is not an object is refused before anything is written
    CHECK_FALSE(projection::rom_override(f.td.path(), Json{{"id", "boot"}, {"patch", "nope"}}));
  }

  TEST_CASE("the budget collapses children and says how many; what you hid is counted, never dropped silently") {
    Fixture f;
    const Json flows = f.navigate(Json{{"focus", "area:flows"}, {"limit", 5}});
    CHECK(flows.at("counts").at("shown") == 5);
    CHECK(flows.at("counts").at("collapsed").get<int>() == flows.at("counts").at("total").get<int>() - 5);
    CHECK(flows.at("limit").at("from") == "request");
    f.rt->config().set("navigator_children_limit", 3);
    const Json by_setting = f.navigate(Json{{"focus", "area:flows"}});
    CHECK(by_setting.at("counts").at("shown") == 3);
    CHECK(by_setting.at("limit").at("from") == "navigator_children_limit");
    LOOM_REQUIRE_OK(fsutil::ensure_dir(f.td.path() / "profiles"));
    LOOM_REQUIRE_OK(fsutil::write_file(f.td.path() / "profiles/graph_entry.pack",
                                       json::dump(Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "graph_entry"},
                                                       {"overrides", Json{{"areas", Json{{"knowledge", Json{{"hidden", true}}}}}}}})));
    const Json root = f.navigate(Json::object());
    CHECK_FALSE(child(root, "area:knowledge"));
    CHECK(root.at("hidden_by_you").at("count") == 1);
    CHECK(root.at("hidden_by_you").at("items").at(0).at("id") == "area:knowledge");
    // a query narrows a level
    const Json q = f.navigate(Json{{"focus", "area:rom"}, {"query", "boot"}});
    CHECK(q.at("children").size() == 1);
  }

  TEST_CASE("an area whose expand names graph.navigate opens its focus; perspectives choose other edges; the catalogue renders a perspective") {
    Fixture f;
    const Json you = f.navigate(Json{{"focus", "area:party_self"}});
    CHECK(you.at("focus").at("address") == "party:self");
    CHECK(you.at("handed_from") == "area:party_self");
    // through a perspective: the roles a flow uses
    LOOM_REQUIRE_OK(views::put_op(f.td.path(), "mine",
                                  Json{{"view", "perspectives"}, {"op", "add"}, {"id", "flow_roles"},
                                       {"entry", Json{{"label", Json{{"en", "Flow roles"}}}, {"relations", Json::array({"uses_role"})}, {"direction", "both"}}}}));
    const Json persp = f.navigate(Json{{"focus", "flow:chat.send"}, {"perspectives", Json::array({"flow_roles"})}});
    CHECK(child(persp, "role:chat.default"));
    // Perspectives::render over the catalogue
    auto ps = unwrap(views::Perspectives::open(f.td.path()));
    views::RenderContext rc;
    rc.db = &f.rt->db();
    rc.cfg = &f.rt->config();
    rc.root = f.td.path();
    rc.memory = &f.rt->memory();
    const Json r = unwrap(ps.render(rc, "structure", Json{{"source", "catalogue"}, {"seeds", Json::array({"area:rom"})}, {"direction", "in"}, {"hops", 1}}));
    CHECK(r.at("source") == "catalogue");
    std::size_t items = 0;
    for (const auto& n : r.at("nodes")) items += n.at("id").get<std::string>().rfind("rom:", 0) == 0;
    CHECK(items == 13);
  }

  TEST_CASE("the chat reaches the new actions and answers in words; the sentences name areas by their names") {
    Fixture f;
    auto addr = unwrap(projection::find_address(f.env("pl"), "ROM"));
    CHECK(addr == "area:rom");
    CHECK(unwrap(projection::find_address(f.env("pl"), "Przepływy")) == "area:flows");
    CHECK(unwrap(projection::find_address(f.env("pl"), "rom:boot")) == "rom:boot");
    CHECK(unwrap(projection::find_address(f.env("pl"), "nic takiego nie ma")).empty());
    const auto catalogue = unwrap(RuntimeProfile::builtin("actions"));
    const Json& actions = catalogue.values().at("actions");
    for (const char* id : {"graph.navigate", "graph.card", "settings.resolved", "roles.resolved", "rom.override"}) {
      const Json* entry = json::find(actions, id);
      REQUIRE_MESSAGE(entry, id);
      CHECK(f.rt->actions().runnable(*entry));
      CHECK(entry->at("port") == "actions");
      CHECK(entry->at("http").at("verb") == "POST");
      CHECK_FALSE(entry->at("phrases").at("pl").empty());
      CHECK_FALSE(entry->at("examples").at("en").empty());
    }
  }
}
