// M1-K2c-2: the projection of You (party:self) into its parts (profile.subgraphs in profiles/user.pack): the interview's fields with their states, the
// preferences, the rules about your data, the interests, the units, the contacts and the memory notes, each with its evidence and the actions that apply.
// No network, no model; synthetic data only.
#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <set>

#include "graph/projection.h"
#include "loom/action_runner.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Person {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  Person() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
  }
  Json call(const std::string& action, Json request = Json::object()) {
    request["locale"] = "pl";
    return unwrap(rt->actions().invoke(action, request));
  }
  Json confirmed(const std::string& action, Json request) {
    const Json waiting = call(action, std::move(request));
    REQUIRE(waiting.at("status") == "awaiting_confirmation");
    return unwrap(rt->actions().confirm(
        Json{{"run_id", waiting.at("id")}, {"approved", true}, {"receipt_id", waiting.at("pending").at("receipt_id")}, {"ref", "test"}}));
  }
  Json go(const std::string& op, Json more = Json::object()) {
    more["op"] = op;
    return call("profile.interview", std::move(more));
  }
  // `conv`: the conversation the screen is opened from (its own values and its units are levels of the scope chain); "" = none.
  Json navigate(const std::string& focus, const std::string& lang = "pl", std::int64_t hops = 1, const std::string& conv = std::string()) {
    Json request{{"focus", focus}, {"lang", lang}, {"limit", 0}, {"hops", hops}};
    if (!conv.empty()) request["conv_id"] = conv;
    return unwrap(rt->actions().invoke("graph.navigate", request));
  }
  Json card(const std::string& address, const std::string& lang = "pl", const std::string& conv = std::string()) {
    Json request{{"address", address}, {"lang", lang}};
    if (!conv.empty()) request["conv_id"] = conv;
    return unwrap(rt->actions().invoke("graph.card", request));
  }
  // The synthetic person: level "minimal", two known slots, one declined, one never, one above the level; a unit with one conversation; two memory
  // notes (one with its origin, one from before it was recorded); one topic.
  void make() {
    go("start");
    REQUIRE(go("answer", Json{{"slot", "interview.detail"}, {"text", "minimal"}}).at("status") == "completed");
    confirmed("profile.set", Json{{"slot", "communication.style"}, {"value", "direct"}});
    confirmed("profile.set", Json{{"slot", "communication.language"}, {"value", "polski"}});
    go("decline", Json{{"slot", "knowledge.remember"}});
    go("never", Json{{"slot", "knowledge.avoid"}});
    const std::string conv = unwrap(rt->db().create_conv("Remont mieszkania")).id;
    ConvPatch patch;
    patch.metadata = Json{{"units", Json{{"project", "remont"}}}};
    LOOM_REQUIRE_OK(rt->db().update_conv(conv, patch));
    unwrap(rt->memory().add_node("Mam kota Filemona", std::nullopt, std::nullopt,
                                 Json{{"provenance", Json{{"source", "user"}, {"time", "2026-10-22T10:00:00Z"}, {"ref", "m_test"}}}}));
    unwrap(rt->memory().add_node("Stara notatka sprzed zapisywania pochodzenia"));
    NodeOptions topic;
    unwrap(rt->db().create_node("kuchnia", "topic", topic));
    unwrap(rt->db().create_node("Jan Kowalski", "person", topic));  // a person the analysis found: a contact, an inference
  }
};

// The synthetic person is made once (the interview is the slow part) and only read by the tests, whatever their order.
Person& shared_person() {
  static Person person;
  static bool made = false;
  if (!made) {
    person.make();
    made = true;
  }
  return person;
}

const Json* child(const Json& nav, const std::string& address) {
  for (const auto& c : nav.at("children"))
    if (c.at("address") == address) return &c;
  return nullptr;
}

bool has_action(const Json& node, const std::string& id) {
  if (!node.contains("actions")) return false;
  for (const auto& a : node.at("actions"))
    if (a.at("id") == id) return true;
  return false;
}

const Json& action_of(const Json& node, const std::string& id) {
  for (const auto& a : node.at("actions"))
    if (a.at("id") == id) return a;
  FAIL("no action " << id << " on " << node.value("address", "?"));
  static const Json none;
  return none;
}

}  // namespace

TEST_SUITE("party_self_subgraphs") {
  TEST_CASE("You has the parts of profile.subgraphs as children, each with its count, evidence and the actions of the part; the added kinds are in the table") {
    Person& p = shared_person();
    const Json you = p.navigate("party:self");
    std::set<std::string> parts;
    for (const auto& c : you.at("children")) {
      INFO(c.dump());
      REQUIRE(c.at("address").get<std::string>().rfind("subgraph:self/", 0) == 0);
      parts.insert(c.at("address").get<std::string>().substr(14));
      CHECK(c.at("kind") == "person_subgraph");
      CHECK_FALSE(c.at("label").get<std::string>().empty());
      CHECK_FALSE(c.at("why").get<std::string>().empty());
      CHECK(c.at("evidence").at("class").is_string());
      CHECK(c.at("evidence").at("provenance").contains("method"));
      CHECK(c.at("actions").is_array());
      CHECK(c.contains("count"));
    }
    // the seven kinds of the table and the one added (the interview's other fields)
    CHECK(parts == std::set<std::string>{"identity", "preferences", "policies", "units", "interests", "network", "memory", "slots"});
    CHECK(child(you, "subgraph:self/memory")->at("label") == "Co pamiętam");
    CHECK(child(you, "subgraph:self/memory")->at("count") == 2);
    CHECK(child(you, "subgraph:self/units")->at("count") == 1);
    CHECK(child(you, "subgraph:self/interests")->at("count") == 1);
    CHECK(child(you, "subgraph:self/network")->at("count") == 1);
    CHECK(child(you, "subgraph:self/policies")->at("count").get<int>() >= 1);
    CHECK(child(you, "subgraph:self/preferences")->at("count").get<int>() >= 4);
    // the memory part offers to remember, the units part to define a unit (the catalogue's own titles, no ids)
    CHECK(has_action(*child(you, "subgraph:self/memory"), "memory.remember"));
    CHECK(has_action(*child(you, "subgraph:self/units"), "units.define"));
    CHECK(has_action(*child(you, "subgraph:self/slots"), "profile.interview"));
    // an inference is counted as an inference: the interests are all inferred, none observed
    const Json classes = child(you, "subgraph:self/interests")->at("evidence_classes");
    CHECK(classes.at("inferred") == 1);
    CHECK_FALSE(classes.contains("observed"));
  }

  TEST_CASE("the interview's fields keep their states; a never or unknown field carries no value; a field above the level says so and why") {
    Person& p = shared_person();
    const Json slots = p.navigate("subgraph:self/slots", "pl");
    const Json ident = p.navigate("subgraph:self/identity", "pl");
    const Json prefs = p.navigate("subgraph:self/preferences", "pl");
    const Json* never = child(slots, "party:self/knowledge.avoid");
    const Json* declined = child(slots, "party:self/knowledge.remember");
    const Json* style = child(slots, "party:self/communication.style");
    const Json* language = child(slots, "party:self/communication.language");
    REQUIRE(never);
    REQUIRE(declined);
    REQUIRE(style);
    REQUIRE(language);
    CHECK(never->at("status") == "never");
    CHECK(never->at("value").is_null());
    CHECK(declined->at("status") == "declined");
    CHECK(declined->at("value").is_null());
    CHECK(style->at("status") == "known");
    CHECK(style->at("value") == "direct");
    CHECK(language->at("status") == "known");
    // the identity field is above the level "minimal": not asked, and the reason is the level
    const Json* above = child(ident, "party:self/identity.description");
    REQUIRE(above);
    CHECK(above->at("status") == "unknown");
    CHECK(above->at("value").is_null());
    const Json card = p.card("party:self/identity.description");
    CHECK(card.at("attrs").at("asked_at_level") == false);
    CHECK(card.at("attrs").at("reason") == "above_level");
    CHECK_FALSE(card.at("attrs").at("reason_text").get<std::string>().empty());
    CHECK(card.at("why") == card.at("attrs").at("reason_text"));
    // a known field is offered to be set again, with its own field in the arguments; a never field is too (the person may reopen it)
    CHECK(action_of(*style, "profile.set").at("args").at("slot") == "communication.style");
    // a never field has no value on its card either
    CHECK(p.card("party:self/knowledge.avoid").at("value").is_null());
    // the preferences of the same interview are named apart from its fields, and carry the field they are the same as
    CHECK(child(prefs, "preference:language"));
  }

  TEST_CASE("Co pamiętam lists the notes with where each came from; an old note says its origin is unknown, in the words of the app") {
    Person& p = shared_person();
    const Json mem = p.navigate("subgraph:self/memory", "pl");
    REQUIRE(mem.at("children").size() == 2);
    const Json* known = nullptr;
    const Json* old = nullptr;
    for (const auto& c : mem.at("children")) {
      if (c.at("label") == "Mam kota Filemona") known = &c;
      if (c.at("label").get<std::string>().rfind("Stara notatka", 0) == 0) old = &c;
    }
    REQUIRE(known);
    REQUIRE(old);
    CHECK(known->at("evidence").at("human") == "napisałeś to Ty");
    CHECK(known->at("evidence").at("class") == "observed");
    CHECK(old->at("evidence").at("human") == "pochodzenie nieznane (zapisane przed M1)");
    CHECK(old->at("evidence").at("class") == "declared");  // not claimed to be something you said
    const Json note = p.card(known->at("address"));
    CHECK(note.at("attrs").at("provenance").at("known") == true);
    CHECK(note.at("attrs").at("provenance").at("source") == "user");
    CHECK(note.at("attrs").at("provenance").at("ref") == "m_test");
    const Json old_card = p.card(old->at("address"));
    CHECK(old_card.at("attrs").at("provenance").at("known") == false);
    CHECK(old_card.at("attrs").at("provenance").at("words") == "pochodzenie nieznane (zapisane przed M1)");
    // forgetting is offered on each note, with the note's own id in the arguments (the card shows no id as text)
    CHECK(action_of(old_card, "memory.forget").at("args").at("id") == old_card.at("attrs").at("note_id"));
    // the breadcrumbs lead through the part to You
    const Json crumbs = note.at("breadcrumbs");
    REQUIRE(crumbs.size() >= 2);
    CHECK(crumbs[crumbs.size() - 1].at("address") == "subgraph:self/memory");
    CHECK(crumbs[crumbs.size() - 2].at("address") == "party:self");
    // the same, in English
    const Json en = p.navigate("subgraph:self/memory", "en");
    bool found = false;
    for (const auto& c : en.at("children"))
      if (c.at("evidence").at("human") == "origin unknown (saved before M1)") found = true;
    CHECK(found);
  }

  TEST_CASE("a preference goes through the scope chain: what ships is the installation level, your answer in the profile the person level, the chip says which") {
    Person& p = shared_person();
    const Json prefs = p.navigate("subgraph:self/preferences", "pl");
    REQUIRE(child(prefs, "preference:style"));
    REQUIRE(child(prefs, "preference:length"));
    // answered in the interview (communication.style = direct): the person level supplies it, the shipped value is overridden, and it is what you said
    const Json style = p.card("preference:style");
    CHECK(style.at("value") == "direct");
    CHECK(style.at("provenance").at("scope") == "person");
    CHECK(style.at("provenance").at("layer") == "profiles/user.pack");
    CHECK(style.at("evidence").at("class") == "observed");
    const Json& chain = style.at("attrs").at("resolved").at("inheritance");
    std::map<std::string, Json> level;
    for (const auto& l : chain) level[l.at("level").get<std::string>()] = l;
    REQUIRE(level.count("installation"));
    REQUIRE(level.count("person"));
    CHECK(level.at("installation").at("role") == "overridden");
    CHECK(level.at("installation").at("value") == "neutral");
    CHECK(level.at("person").at("role") == "supplied");
    CHECK(level.at("person").at("value") == "direct");
    CHECK(level.count("conversation"));  // the lower levels are in the chain even when nothing sets them
    // never answered: the shipped value, a preset (declared, not "you said it"), the installation level supplies it
    const Json length = p.card("preference:length");
    CHECK(length.at("value") == "concise");
    CHECK(length.at("provenance").at("scope") == "installation");
    CHECK(length.at("provenance").at("layer") == "rom");
    CHECK(length.at("evidence").at("class") == "declared");
    // changing a preference is changing the interview's field it is the same as: the node offers that action with that field (GR38)
    const Json& style_row = *child(prefs, "preference:style");
    REQUIRE(has_action(style_row, "profile.set"));
    CHECK(action_of(style_row, "profile.set").at("args").at("slot") == "communication.style");
    CHECK(has_action(*child(prefs, "preference:length"), "profile.set"));
    const Json& length_chain = length.at("attrs").at("resolved").at("inheritance");
    for (const auto& l : length_chain)
      if (l.at("level") == "person") CHECK(l.at("role") == "none");
  }

  TEST_CASE("a conversation's own value and its unit are levels below the person: the value that wins says which level set it") {
    Person q;  // no interview: every preference is the shipped one
    const std::string conv = unwrap(q.rt->db().create_conv("Rozmowa o remoncie")).id;
    ConvPatch patch;
    patch.metadata = Json{{"units", Json{{"project", "remont"}}}, {"settings", Json{{"preference.length", "long"}}}};
    LOOM_REQUIRE_OK(q.rt->db().update_conv(conv, patch));
    // without the conversation the screen shows the person's value
    CHECK(q.card("preference:length").at("value") == "concise");
    CHECK(q.card("preference:length").at("provenance").at("scope") == "installation");
    // from the conversation the conversation's own value wins; the shipped one is overridden; the project level is in the chain with its unit
    const Json nav = q.navigate("subgraph:self/preferences", "pl", 1, conv);
    REQUIRE(child(nav, "preference:length"));
    const Json card = q.card("preference:length", "pl", conv);
    CHECK(card.at("value") == "long");
    CHECK(card.at("provenance").at("scope") == "conversation");
    CHECK(card.at("evidence").at("class") == "observed");
    std::map<std::string, Json> level;
    const Json& card_chain = card.at("attrs").at("resolved").at("inheritance");
    for (const auto& l : card_chain) level[l.at("level").get<std::string>()] = l;
    CHECK(level.at("conversation").at("role") == "supplied");
    CHECK(level.at("installation").at("role") == "overridden");
    CHECK(level.at("installation").at("value") == "concise");
    REQUIRE(level.count("project"));
    CHECK(level.at("project").at("unit_level") == true);
    CHECK(level.at("project").at("scope_id") == "remont");
    // a preference the conversation does not set stays at the level that set it
    CHECK(q.card("preference:style", "pl", conv).at("provenance").at("scope") == "installation");
  }

  TEST_CASE("the units and the interests hang under You with their evidence; a unit is still where it was (Kontekst i projekty)") {
    Person& p = shared_person();
    const Json units = p.navigate("subgraph:self/units");
    REQUIRE(units.at("children").size() == 1);
    const Json& u = units.at("children").at(0);
    CHECK(u.at("address") == "unit:project/remont");
    CHECK(u.at("count") == 1);
    CHECK(u.at("shown_also_in") == "subgraph:self/units");
    CHECK(has_action(u, "units.forget"));
    CHECK(action_of(u, "units.forget").at("args").at("kind") == "project");
    const Json home = p.navigate("area:context_projects");
    CHECK(child(home, "unit:project/remont"));
    // a contact is a person the analysis found: it hangs under the network as an inference, never as something you said
    const Json network = p.navigate("subgraph:self/network");
    REQUIRE(network.at("children").size() == 1);
    CHECK(network.at("children").at(0).at("label") == "Jan Kowalski");
    CHECK(network.at("children").at(0).at("kind") == "person");
    CHECK(network.at("children").at(0).at("evidence").at("class") == "inferred");
    CHECK(network.at("children").at(0).at("address").get<std::string>().rfind("party:", 0) == 0);
    const Json interests = p.navigate("subgraph:self/interests");
    REQUIRE(interests.at("children").size() == 1);
    CHECK(interests.at("children").at(0).at("evidence").at("class") == "inferred");
    CHECK(interests.at("children").at(0).at("actions").is_array());
  }

  TEST_CASE("every node under You has evidence and actions; no card shows a raw id, in either language (all kinds, from the data)") {
    Person& p = shared_person();
    static const std::regex raw("\\b(party|subgraph|memory|policy|interest|unit|preference):[A-Za-z0-9_./-]+");
    for (const char* lang : {"pl", "en"}) {
      const Json deep = p.navigate("party:self", lang, 2);
      std::set<std::string> kinds;
      std::set<std::string> ids;
      for (const auto& c : deep.at("children")) {
        ids.insert(c.at("address").get<std::string>());
        if (c.at("address").get<std::string>().rfind("party:self/", 0) == 0) ids.insert(c.at("address").get<std::string>().substr(11));
      }
      CHECK(deep.at("children").size() >= 12);
      // every node has evidence and actions; the card of the first node of each kind (and of You) is read for raw ids (a card costs a whole catalogue)
      std::set<std::string> carded;
      std::vector<std::string> to_card = {"party:self"};
      for (const auto& c : deep.at("children")) {
        const std::string address = c.at("address");
        INFO(lang << " " << address);
        kinds.insert(c.at("kind").get<std::string>());
        CHECK(c.at("evidence").at("class").is_string());
        CHECK(c.at("evidence").at("provenance").contains("method"));
        CHECK(c.at("actions").is_array());
        if (carded.insert(c.at("kind").get<std::string>()).second) to_card.push_back(address);
      }
      for (const auto& address : to_card) {
        INFO(lang << " " << address);
        const Json card = p.card(address, lang);
        std::vector<std::string> texts = {card.at("label").get<std::string>(), card.value("why", ""), card.value("value_human", "")};
        texts.push_back(card.at("evidence").value("human", ""));
        for (const auto& a : card.at("actions")) texts.push_back(a.at("label").get<std::string>());
        for (const auto& b : card.at("breadcrumbs")) texts.push_back(b.at("label").get<std::string>());
        for (const auto& rel : card.at("relations")) {
          texts.push_back(rel.value("label", ""));
          for (const char* dir : {"out", "in"})
            for (const auto& other : rel.at(dir)) texts.push_back(other.at("label").get<std::string>());
        }
        for (const auto& text : texts) {
          INFO(text);
          CHECK_FALSE(std::regex_search(text, raw));
          CHECK(ids.count(text) == 0);  // a label that is the id of a field or a node is a raw id
        }
        for (const auto& a : card.at("actions")) CHECK(a.at("label").get<std::string>() != a.at("id").get<std::string>());
      }
      // the kinds under You: the parts, the interview's fields, the preferences, the rules, the interests, the units and the notes
      for (const char* k : {"person_subgraph", "profile_slot", "preference", "privacy_rule", "interest", "unit", "memory_note", "person"}) CHECK(kinds.count(k) == 1);
    }
  }

  TEST_CASE("a part you hid is not shown, and the answer says it is hidden by you") {
    Person& p = shared_person();
    const Json pack = unwrap(onboarding::builtin_pack());
    const auto layers = unwrap(onboarding::DefaultLayers::create(pack));
    Json table = unwrap(layers.resolve("profile.subgraphs")).at("value");
    Json& parts = table["subgraphs"];
    for (auto& e : parts)
      if (e.at("id") == "memory") e["hidden"] = true;
    const Json state = unwrap(layers.dispatch(Json{{"op", "override"}, {"key", "profile.subgraphs"}, {"value", table}}));
    projection::Env env = projection::env_of(*p.rt, Json{{"lang", "pl"}});
    env.profile = Json{{"pack", pack}, {"layers", state}, {"fields", Json::object()}};
    const Json you = unwrap(projection::navigate(env, Json{{"focus", "party:self"}, {"limit", 0}}));
    CHECK_FALSE(child(you, "subgraph:self/memory"));
    CHECK(child(you, "subgraph:self/units"));
    CHECK(you.at("hidden_by_you").at("count") == 1);
    CHECK(you.at("hidden_by_you").at("items").at(0).at("id") == "subgraph:self/memory");
    CHECK(you.at("hidden_by_you").at("items").at(0).at("by") == "overlay");
  }

  TEST_CASE("a stored profile whose pack has no parts yet still shows the shipped parts") {
    Person p;
    Json pack = unwrap(onboarding::builtin_pack());
    for (auto& e : pack.at("entries"))
      if (e.at("key") == "profile.subgraphs") {
        Json& value = e["value"];
        Json& parts = value["subgraphs"];
        for (auto& s : parts) {
          s.erase("members");
          s.erase("actions");
          s.erase("actions_here");
        }
      }
    const auto layers = unwrap(onboarding::DefaultLayers::create(pack));
    projection::Env env = projection::env_of(*p.rt, Json{{"lang", "pl"}});
    env.profile = Json{{"pack", pack}, {"layers", layers.snapshot()}, {"fields", Json::object()}};
    const Json you = unwrap(projection::navigate(env, Json{{"focus", "party:self"}, {"limit", 0}}));
    CHECK(child(you, "subgraph:self/memory"));
    CHECK(child(you, "subgraph:self/slots"));
  }

  // A developer aid, not a check: with LOOM_PARTY_DUMP=<file> the answers of the screen Ty (the parts, one of each kind, three cards) are written there as
  // JSON, so that the web work (W2b) and the report show real shapes. Without the variable it does nothing.
  TEST_CASE("dump of the answers for the screen Ty (only with LOOM_PARTY_DUMP)") {
    const char* file = std::getenv("LOOM_PARTY_DUMP");
    if (!file) return;
    Person& p = shared_person();
    Json out = Json::object();
    out["you"] = p.navigate("party:self", "pl");
    for (const char* part : {"slots", "identity", "preferences", "policies", "units", "interests", "network", "memory"})
      out[std::string("part_") + part] = p.navigate(std::string("subgraph:self/") + part, "pl");
    out["card_you"] = p.card("party:self", "pl");
    out["card_slot"] = p.card("party:self/communication.style", "pl");
    out["card_above"] = p.card("party:self/identity.description", "pl");
    out["card_memory"] = p.card(out["part_memory"].at("children").at(0).at("address"), "pl");
    out["card_memory_old"] = p.card(out["part_memory"].at("children").at(1).at("address"), "pl");
    out["card_part"] = p.card("subgraph:self/memory", "pl");
    out["you_en"] = p.navigate("party:self", "en");
    std::ofstream(file) << out.dump(1);
  }
}
