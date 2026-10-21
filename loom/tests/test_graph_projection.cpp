// M1-K0: the graph projection (src/graph/projection.h) - the ROM, settings, You, conversations, flows, roles and perspectives as nodes of
// one navigable graph, read on demand from runtime/graph_sources.pack; graph.navigate / graph.card / settings.resolved / roles.resolved /
// rom.override through loom_invoke("actions", ...).
#include <doctest/doctest.h>

#include <filesystem>
#include <functional>
#include <map>
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
    // automations and experiments list their items (what runs on its own, the background tasks, the items in calibration) and keep
    // their counters; every area the M1 root added opens in the navigator (expand graph.navigate)
    CHECK(child(root, "area:automations")->at("count").get<int>() > 0);
    CHECK(child(root, "area:automations")->at("counts").contains("messages_waiting"));
    CHECK(child(root, "area:experiments")->at("count").get<int>() > 0);
    const Json entry = unwrap(RuntimeProfile::builtin("graph_entry")).values().at("areas");
    for (const char* a : {"party_self", "flows", "devices_io", "tools_agent", "providers_connectors", "environments", "automations", "experiments"}) {
      INFO(a);
      CHECK(entry.at(a).at("expand").at("view") == "graph.navigate");
      CHECK_FALSE(child(root, std::string("area:") + a)->value("why", "").empty());  // each area says what it is for
    }
    // a count the entry view cannot give says why, in words
    for (const auto& c : root.at("children")) {
      const Json counts = c.value("counts", Json::object());
      for (const auto& count : counts.items())
        if (!count.value().value("known", true)) CHECK_FALSE(count.value().value("reason_human", "").empty());
    }
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
        for (const auto& d : doc.at("fields")) {
          if (d.at("key") != b.key) continue;
          if (d.contains("default_value")) b.installation = d.at("default_value");
          else if (d.contains("preset_value")) b.installation = d.at("preset_value");  // the preset its reader takes from a profile
        }
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
    // the navigator's budget caps the page read (never more rows than a level can show); the total still counts every conversation
    const Json budget = f.navigate(Json{{"focus", "area:context_projects"}, {"limit", 1}});
    CHECK(budget.at("pages").at("conv").at("limit") == 1);
    CHECK(budget.at("counts").at("shown") == 1);
    CHECK(budget.at("counts").at("collapsed").get<int>() == budget.at("counts").at("total").get<int>() - 1);
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
    // the flow of every class (not only the current one) with its words in the language asked: the owner's "why can I choose a model for
    // the analysis and how do the flows differ" is answered by the data, in Polish and in English
    const Json by_class = analysis->at("flow_by_class");
    REQUIRE(by_class.is_array());
    CHECK(by_class.size() == 4);
    bool current_among = false;
    for (const auto& entry : by_class) {
      CHECK_FALSE(entry.at("tier").at("label").get<std::string>().empty());
      CHECK_FALSE(entry.at("flow").at("does_human").empty());
      CHECK_FALSE(entry.at("flow").at("title").get<std::string>().empty());
      current_among = current_among || entry.at("tier").at("id") == choice.tier;
    }
    CHECK(current_among);
    CHECK(analysis->at("flow").at("does_human").size() == analysis->at("flow").at("does").size());
    const Json pl = unwrap(f.rt->actions().invoke("roles.resolved", Json{{"lang", "pl"}}));
    const Json en = unwrap(f.rt->actions().invoke("roles.resolved", Json{{"lang", "en"}}));
    std::string pl_title, en_title;
    for (const auto& role : pl.at("roles")) if (role.at("id") == "analysis.fast") pl_title = role.at("flow").at("title").get<std::string>();
    for (const auto& role : en.at("roles")) if (role.at("id") == "analysis.fast") en_title = role.at("flow").at("title").get<std::string>();
    CHECK(pl_title != en_title);
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

  // ── review of M1-K0 ───────────────────────────────────────────────────────────────

  TEST_CASE("a conversation's count is what opening it lists: its active messages") {
    Fixture f;
    const std::string conv = unwrap(f.rt->db().create_conv("Statuses")).id;
    std::vector<std::string> ids;
    for (int i = 0; i < 4; ++i) {
      NewMessage m;
      m.conv_id = conv;
      m.role = "user";
      m.text = "message " + std::to_string(i);
      ids.push_back(unwrap(f.rt->db().create_msg(m)));
    }
    LOOM_REQUIRE_OK(f.rt->db().set_msg_status(ids[1], "deleted"));
    LOOM_REQUIRE_OK(f.rt->db().set_msg_status(ids[2], "version"));
    LOOM_REQUIRE_OK(f.rt->db().set_msg_status(ids[3], "excluded"));
    const std::string gone = unwrap(f.rt->db().create_conv("All deleted")).id;
    NewMessage m;
    m.conv_id = gone;
    m.role = "user";
    m.text = "deleted";
    LOOM_REQUIRE_OK(f.rt->db().set_msg_status(unwrap(f.rt->db().create_msg(m)), "deleted"));
    const Json area = f.navigate(Json{{"focus", "area:context_projects"}});
    const Json* row = child(area, "conv:" + conv.substr(2));
    REQUIRE(row);
    const Json opened = f.navigate(Json{{"focus", "conv:" + conv.substr(2)}});
    CHECK(row->at("count") == opened.at("counts").at("total"));
    CHECK(row->at("count") == 1);
    const Json* empty = child(area, "conv:" + gone.substr(2));
    REQUIRE(empty);
    CHECK(empty->at("count") == 0);
    CHECK(empty->at("has_children") == false);
  }

  TEST_CASE("hops 2: the budget is per level and parent, no orphans, and the counts are the first level's") {
    Fixture f;
    const Json nav = f.navigate(Json{{"focus", "area:profiles_settings"}, {"hops", 2}, {"limit", 5}});
    std::set<std::string> first;
    std::map<std::string, int> per_parent;
    for (const auto& c : nav.at("children"))
      if (c.at("depth") == 1) first.insert(c.at("address").get<std::string>());
    CHECK(first.size() == 5);
    for (const auto& c : nav.at("children")) {
      if (c.at("depth") != 2) continue;
      std::string parent;
      for (const auto& e : nav.at("edges"))
        if (e.at("src") == c.at("address") && first.count(e.at("dst").get<std::string>())) parent = e.at("dst");
      INFO(c.at("address"));
      CHECK_FALSE(parent.empty());  // every deeper node hangs under a node that is shown
      per_parent[parent] += 1;
    }
    for (const auto& [parent, n] : per_parent) CHECK(n <= 5);
    CHECK(nav.at("counts").at("shown") == 5);
    CHECK(nav.at("counts").at("collapsed") == nav.at("counts").at("total").get<int>() - 5);
    // a query looks at the first level: its total is the first level's matches
    const Json q = f.navigate(Json{{"focus", "area:profiles_settings"}, {"hops", 2}, {"query", "model"}});
    int matches = 0;
    for (const auto& c : q.at("children")) matches += c.at("depth") == 1 ? 1 : 0;
    CHECK(q.at("counts").at("total") == matches);
    CHECK(q.at("counts").at("collapsed") == 0);
  }

  TEST_CASE("the budget of a level is read through the scope chain: a conversation's own budget applies in it") {
    Fixture f;
    const std::string conv = unwrap(f.rt->db().create_conv("Small budget")).id;
    ConvPatch patch;
    patch.metadata = Json{{"settings", Json{{"navigator_children_limit", 3}}}};
    LOOM_REQUIRE_OK(f.rt->db().update_conv(conv, patch));
    const Json in_conv = f.navigate(Json{{"focus", "area:flows"}, {"conv_id", conv}});
    CHECK(in_conv.at("limit").at("value") == 3);
    CHECK(in_conv.at("counts").at("shown") == 3);
    const Json outside = f.navigate(Json{{"focus", "area:flows"}});
    CHECK(outside.at("limit").at("value") != 3);
    // the usage fuse is said by its factor, not as a count of members
    const Json r = unwrap(f.rt->actions().invoke("settings.resolved", Json{{"lang", "pl"}}));
    for (const auto& g : r.at("groups"))
      for (const auto& field : g.at("fields"))
        if (field.at("key") == "loom_usage_policy") CHECK(field.at("value_human").get<std::string>().rfind("×10", 0) == 0);
  }

  TEST_CASE("a large source never crowds the others out of a level: every source keeps a share, the collapsed are said per source") {
    Fixture f;
    const Json nav = f.navigate(Json{{"focus", "area:devices_io"}, {"limit", 20}});
    std::map<std::string, int> by;
    for (const auto& c : nav.at("children")) by[c.at("source").get<std::string>()] += 1;
    CHECK(by["device"] > 0);
    CHECK(by["binding"] > 0);
    CHECK(nav.at("counts").at("shown") == 20);
    CHECK(nav.at("counts").at("by_source").contains("binding"));
    CHECK(nav.at("counts").at("by_source").at("binding").at("shown") == by["binding"]);
  }

  TEST_CASE("a fresh install: nothing is yours, a preset kept in config.json says so, every chip has words") {
    Fixture f;
    const Json r = unwrap(f.rt->actions().invoke("settings.resolved", Json{{"lang", "pl"}}));
    int yours = 0, preset_copies = 0;
    for (const auto& g : r.at("groups"))
      for (const auto& field : g.at("fields")) {
        const Json prov = field.at("provenance");
        INFO(field.at("key"));
        CHECK_FALSE(prov.at("chip").at("label").get<std::string>().empty());
        const std::string scope = prov.at("scope");
        if (prov.value("same_as_preset", false)) {
          ++preset_copies;
          CHECK(prov.at("chip").at("label") == "preset zapisany w Twoich ustawieniach");
          CHECK(field.at("evidence").at("class") == "declared");
        } else if (scope != "installation" && scope != "none") {
          ++yours;
        }
      }
    CHECK(yours == 0);
    MESSAGE("presets kept in config.json: " << preset_copies);
  }

  TEST_CASE("a field whose reader acts on a preset never says it is not set: the profile's preset, or what the code does") {
    Fixture f;
    const Json doc = unwrap(settings_fields(f.td.path()));
    const Json r = unwrap(f.rt->actions().invoke("settings.resolved", Json{{"lang", "pl"}}));
    std::map<std::string, Json> by_key;
    for (const auto& g : r.at("groups"))
      for (const auto& field : g.at("fields")) by_key[field.at("key").get<std::string>()] = field;
    int presets = 0, in_code = 0;
    for (const auto& d : doc.at("fields")) {
      const std::string key = d.at("key");
      if (!by_key.count(key)) continue;
      const Json& field = by_key.at(key);
      INFO(key);
      if (d.contains("preset_value")) {
        ++presets;
        CHECK(field.at("value") == d.at("preset_value"));
        CHECK(field.at("provenance").at("scope") == "installation");
      }
      if (d.value("human", Json::object()).contains("when_unset") && field.at("value").is_null()) {
        ++in_code;
        CHECK(field.at("value_human") == d.at("human").at("when_unset").at("pl"));
        CHECK(field.at("provenance").at("chip").at("id") == "code_preset");
      }
      if (d.contains("preset_value") || d.value("human", Json::object()).contains("when_unset")) CHECK(field.at("value_human") != "nie ustawiono");
    }
    CHECK(presets > 0);
    CHECK(in_code > 0);
    CHECK(by_key.at("tool_loop_max_steps").at("value") == unwrap(RuntimeProfile::builtin("tool_loop")).values().at("max_steps"));
    CHECK(by_key.at("loom_zip_max_total_bytes").at("value_human").get<std::string>().find("GB") != std::string::npos);  // bytes in words
    // a profile value has an edit rule and its two levels (the shipped preset, your overlay)
    const Json& pv = by_key.at("worker/batch_threshold");
    CHECK(pv.at("edit_rule") == "optional");
    CHECK(pv.at("inheritance").size() == 2);
  }

  TEST_CASE("a card offers what can be done in words, with the rules' decision; a ROM item offers your copy") {
    Fixture f;
    for (const char* address : {"rom:rescue_mode", "setting:temperature"}) {
      const Json card = unwrap(f.rt->actions().invoke("graph.card", Json{{"address", address}, {"lang", "pl"}}));
      INFO(address);
      REQUIRE_FALSE(card.at("actions").empty());
      for (const auto& a : card.at("actions")) {
        CHECK_FALSE(a.at("label").get<std::string>().empty());
        CHECK(a.at("decision").is_string());
      }
    }
    const Json rom = unwrap(f.rt->actions().invoke("graph.card", Json{{"address", "rom:rescue_mode"}, {"lang", "pl"}}));
    bool copy = false;
    for (const auto& a : rom.at("actions"))
      if (a.at("id") == "rom.override") {
        copy = true;
        CHECK(a.at("decision") == "needs_confirmation");
        CHECK(a.at("label") == "Przesłoń element ROM swoją kopią");
      }
    CHECK(copy);
  }

  TEST_CASE("evidence in words: a model's reply or an import is never what you said; an unanswered slot is a question") {
    Fixture f;
    const std::string conv = unwrap(f.rt->db().create_conv("Words")).id;
    for (const char* role : {"user", "assistant"}) {
      NewMessage m;
      m.conv_id = conv;
      m.role = role;
      m.text = std::string(role) + " text";
      unwrap(f.rt->db().create_msg(m));
    }
    const Json nav = f.navigate(Json{{"focus", "conv:" + conv.substr(2)}, {"lang", "pl"}});
    for (const auto& c : nav.at("children")) {
      const std::string human = c.at("evidence").at("human");
      if (c.at("label").get<std::string>().rfind("Model:", 0) == 0) {
        CHECK(human == "odpowiedź modelu");
        CHECK(human != "powiedziałeś lub zrobiłeś");
      } else {
        CHECK(c.at("label").get<std::string>().rfind("Ty:", 0) == 0);
        CHECK(human == "powiedziałeś to");
      }
    }
    const std::string imported = unwrap(f.rt->db().create_conv("Imported")).id;
    ConvPatch patch;
    patch.source = "claude";
    LOOM_REQUIRE_OK(f.rt->db().update_conv(imported, patch));
    NewMessage m;
    m.conv_id = imported;
    m.role = "user";
    m.text = "imported text";
    unwrap(f.rt->db().create_msg(m));
    const Json in = f.navigate(Json{{"focus", "conv:" + imported.substr(2)}, {"lang", "pl"}});
    REQUIRE(in.at("children").size() == 1);
    CHECK(in.at("children").at(0).at("evidence").at("human").get<std::string>().rfind("import (claude", 0) == 0);
    const Json you = f.navigate(Json{{"focus", "party:self"}, {"lang", "pl"}});
    const Json* slot = child(you, "party:self/identity.description");
    REQUIRE(slot);
    CHECK(slot->at("evidence").at("human") == "pytanie z wywiadu — bez odpowiedzi");
    // the preferences are named apart from the interview's slots, their values in words
    const Json* language = child(you, "preference:language");
    REQUIRE(language);
    CHECK(language->at("label") != child(you, "party:self/communication.language")->at("label"));
    CHECK(child(you, "preference:style")->at("value_human") == "neutralny");
  }

  TEST_CASE("every reason the graph gives has words, and the sources say their names") {
    Fixture f;
    std::vector<Json> answers;
    const Json root = f.navigate(Json{{"lang", "pl"}});
    answers.push_back(root);
    for (const auto& c : root.at("children")) answers.push_back(f.navigate(Json{{"focus", c.at("address")}, {"lang", "pl"}}));
    answers.push_back(unwrap(f.rt->actions().invoke("roles.resolved", Json{{"lang", "pl"}})));
    answers.push_back(unwrap(f.rt->actions().invoke("settings.resolved", Json{{"lang", "pl"}})));
    int reasons = 0;
    std::function<void(const Json&)> walk = [&](const Json& v) {
      if (v.is_object()) {
        for (const auto& item : v.items()) {
          const std::string k = item.key();
          if (item.value().is_string() && (k == "count_unknown_reason" || k == "per_million_unknown_reason" || k == "last_7d_unknown_reason")) {
            ++reasons;
            const std::string human = k == "count_unknown_reason" ? v.value("count_unknown_human", "") : v.value(k.substr(0, k.size() - 6) + "human", "");
            INFO(k << " = " << item.value());
            CHECK_FALSE(human.empty());
            CHECK(human != item.value().get<std::string>());
          }
          if (k == "reason" && item.value().is_string() && v.contains("reason_human")) {
            ++reasons;
            INFO(item.value());
            CHECK(v.at("reason_human") != item.value());
          }
          walk(item.value());
        }
      } else if (v.is_array()) {
        for (const auto& x : v) walk(x);
      }
    };
    for (const auto& a : answers) walk(a);
    CHECK(reasons > 0);
    for (const auto& a : answers)
      for (const auto& u : a.value("unknown", Json::array())) CHECK(u.at("source_label") != u.at("source"));
  }

  TEST_CASE("flows say what they are for in the language asked; steps and edge kinds are named in words, never by their ids") {
    Fixture f;
    const Json pl = f.navigate(Json{{"focus", "area:flows"}, {"lang", "pl"}, {"limit", 0}});
    for (const auto& c : pl.at("children")) {
      INFO(c.at("address"));
      CHECK_FALSE(c.at("why").get<std::string>().empty());
      CHECK_FALSE(c.contains("no_translation"));  // every flow has its Polish words
    }
    const Json de = f.navigate(Json{{"focus", "flow:chat.send"}, {"lang", "de"}});
    CHECK(de.at("focus").at("why").is_string());
    const Json flow_de = unwrap(f.rt->actions().invoke("graph.card", Json{{"address", "flow:chat.send"}, {"lang", "de"}}));
    REQUIRE(flow_de.contains("no_translation"));  // a language without its words says which one is shown
    CHECK(flow_de.at("no_translation").at("shown_lang") == "en");
    const Json steps = f.navigate(Json{{"focus", "flow:import.file"}, {"lang", "pl"}});
    const Json catalogue = unwrap(RuntimeProfile::builtin("actions")).values().at("actions");
    for (const auto& c : steps.at("children")) CHECK_FALSE(catalogue.contains(c.at("label").get<std::string>()));  // a title, not an action id
    const Json persp = f.navigate(Json{{"focus", "area:perspectives"}, {"lang", "pl"}, {"limit", 0}});
    int kinds = 0;
    for (const auto& c : persp.at("children"))
      if (c.at("source") == "edge_kind") {
        ++kinds;
        CHECK(c.at("label") != c.at("address").get<std::string>().substr(10));
      }
    CHECK(kinds > 0);
  }

  TEST_CASE("automations, background tasks and items in calibration are listed; tools and the agent are not empty") {
    Fixture f;
    const Json root = f.navigate(Json{{"lang", "pl"}});
    const Json autos = f.navigate(Json{{"focus", "area:automations"}, {"lang", "pl"}});
    CHECK(child(autos, "automation:semantic_analysis"));
    CHECK(child(autos, "automation:semantic_analysis")->contains("value_human"));
    const Json exp = f.navigate(Json{{"focus", "area:experiments"}, {"lang", "pl"}, {"limit", 0}});
    CHECK(child(exp, "calibration:graph/entry.pack"));
    std::int64_t counter = -1;
    for (const auto& c : root.at("children"))
      if (c.at("address") == "area:experiments") counter = c.at("counts").at("calibration_items").at("value");
    CHECK(counter == exp.at("counts").at("total"));  // the counter and the list are one walk
    const Json tools = f.navigate(Json{{"focus", "area:tools_agent"}, {"lang", "pl"}, {"limit", 0}});
    CHECK(child(tools, "flow:agent.run"));
    CHECK(child(tools, "setting:tool_loop_max_steps"));
    CHECK(child(tools, "tool_kind:mcp"));
    // shown here too, its own place stays its parent
    const Json card = unwrap(f.rt->actions().invoke("graph.card", Json{{"address", "flow:agent.run"}}));
    CHECK(card.at("parent") == "area:flows");
  }

  TEST_CASE("a group, a card and settings.resolved compose the unit kinds once per render, not once per field") {
    Fixture f;
    for (const Json& req : {Json{{"focus", "group:interfejs"}}, Json{{"focus", "area:profiles_settings"}}}) {
      const auto before = views::effective_loads();
      (void)f.navigate(req);
      const auto loads = views::effective_loads() - before;
      MESSAGE("navigate " << req.dump() << ": " << loads << " effective view(s)");
      CHECK(loads < 10);
    }
    const auto before = views::effective_loads();
    const Json all = unwrap(f.rt->actions().invoke("settings.resolved", Json::object()));
    const auto loads = views::effective_loads() - before;
    MESSAGE("settings.resolved: " << loads << " effective view(s) for " << all.at("counts").at("fields") << " fields");
    CHECK(loads < all.at("counts").at("fields").get<std::uint64_t>() / 4);
    const auto card_before = views::effective_loads();
    (void)unwrap(f.rt->actions().invoke("graph.card", Json{{"address", "setting:temperature"}}));
    CHECK(views::effective_loads() - card_before < 10);
  }

  TEST_CASE("find_address: names, aliases and stems, never a part of another word") {
    Fixture f;
    CHECK(unwrap(projection::find_address(f.env("pl"), "ustawień")) == "area:profiles_settings");
    CHECK(unwrap(projection::find_address(f.env("pl"), "rozmowy")) == "area:context_projects");
    CHECK(unwrap(projection::find_address(f.env("pl"), "role")) == "area:providers_connectors");
    CHECK(unwrap(projection::find_address(f.env("pl"), "przepływów")) == "area:flows");
    CHECK(unwrap(projection::find_address(f.env("pl"), "rzeczy", true)).empty());
    CHECK(unwrap(projection::find_address(f.env("pl"), "szczegóły", true)).empty());
    CHECK(unwrap(projection::find_address(f.env("en"), "more detail about the second point", true)).empty());
    CHECK(unwrap(projection::find_address(f.env("pl"), "przeczyta")).empty() == false);  // the word itself still finds its node
  }
}
