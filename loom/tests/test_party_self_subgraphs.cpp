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
    // the kinds of the table and the ones added (the interview's other fields)
    CHECK(parts == std::set<std::string>{"identity", "preferences", "policies", "units", "interests", "network", "memory", "forgotten", "slots"});
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
    // (M1-K2c3 e) a slot without a value holds no evidence: evidence is null and the state says why (the words of the screen, not the app's declaration)
    for (const Json* empty : {never, declined, above}) {
      INFO(empty->at("address").get<std::string>());
      CHECK(empty->at("evidence").is_null());
      CHECK(empty->contains("state"));
    }
    CHECK(above->at("state") == "unanswered");
    CHECK(declined->at("state") == "declined");
    CHECK(never->at("state") == "never");
    // the card of such a slot carries no evidence either (and no state of a slot that has a value)
    CHECK(p.card("party:self/identity.description").at("evidence").is_null());
    CHECK_FALSE(style->contains("state"));
    // (M1-K2c3 c) what the person answered is what the person said: class said, not observed; the interview's words and the time of the answer stay
    for (const Json* answered : {style, language}) {
      INFO(answered->at("address").get<std::string>());
      CHECK(answered->at("evidence").at("class") == "said");
      CHECK(answered->at("evidence").at("provenance").at("method") == "interview");
      CHECK(answered->at("evidence").at("human") == "Twoja odpowiedź w wywiadzie");
      CHECK(answered->at("evidence").at("provenance").at("time").is_string());
    }
  }

  TEST_CASE("the slot of the level question says the label of the level in the language asked; the id stays apart, and the chip is what the person said") {
    Person& p = shared_person();
    const Json slots = p.navigate("subgraph:self/slots", "pl");
    const Json* detail = child(slots, "party:self/interview.detail");
    REQUIRE(detail);
    CHECK(detail->at("status") == "known");
    CHECK(detail->at("value") == "minimal");  // the value is what the setting holds
    CHECK(detail->at("value_human") == "Minimalnie");
    CHECK(detail->at("evidence").at("class") == "said");
    const Json card = p.card("party:self/interview.detail");
    CHECK(card.at("value_human") == "Minimalnie");
    CHECK(card.at("attrs").at("value_id") == "minimal");  // the id in its own field
    const Json en = p.navigate("subgraph:self/slots", "en");
    CHECK(child(en, "party:self/interview.detail")->at("value_human") == "Minimal");
    // a free-text answer is not a level: its words are shown as they are, and carry no value_id
    const Json* style = child(slots, "party:self/communication.style");
    REQUIRE(style);
    CHECK(style->at("value_human") == "direct");
    CHECK_FALSE(p.card("party:self/communication.style").at("attrs").contains("value_id"));
  }

  TEST_CASE("a value the person corrected is worded as corrected by the person, with the time; an unanswered slot becomes a first answer, not a correction") {
    Person p;
    p.go("start");
    REQUIRE(p.go("answer", Json{{"slot", "interview.detail"}, {"text", "normal"}}).at("status") == "completed");
    p.confirmed("profile.set", Json{{"slot", "communication.style"}, {"value", "direct"}});
    auto evidence_of_style = [&](const std::string& lang) {
      for (const auto& part : {std::string("subgraph:self/slots"), std::string("subgraph:self/preferences"), std::string("subgraph:self/identity")}) {
        const Json nav = p.navigate(part, lang);
        if (const Json* s = child(nav, "party:self/communication.style")) return s->at("evidence");
      }
      FAIL("communication.style is under no part");
      return Json();
    };
    const Json before = evidence_of_style("pl");
    CHECK(before.at("class") == "said");
    CHECK(before.at("provenance").at("method") == "interview");
    p.confirmed("profile.set", Json{{"slot", "communication.style"}, {"value", "bezpośrednio"}});
    const Json after = evidence_of_style("pl");
    CHECK(after.at("class") == "said");  // the person's own words, still
    CHECK(after.at("provenance").at("method") == "corrected");
    CHECK(after.at("human") == "poprawione przez Ciebie");
    CHECK(after.at("provenance").at("time").is_string());
    CHECK(evidence_of_style("en").at("human") == "corrected by you");
    // what the correction overwrote is kept and told under the details (the earlier value, in words)
    const Json node = p.card("party:self/communication.style");
    REQUIRE(node.contains("attrs"));
    CHECK(node.at("attrs").at("previous").at("value_human") == "direct");
    CHECK(node.at("attrs").at("previous").at("time").is_string());
  }

  TEST_CASE("an action on a node lists the parameters the person still has to give, from the action catalogue; what the node fills is not asked") {
    Person& p = shared_person();
    const Json ident = p.navigate("subgraph:self/preferences");
    const Json* style = child(ident, "party:self/communication.style");
    if (!style) style = child(ident, "preference:communication.style");
    const Json units = p.navigate("subgraph:self/units");
    // units.define is offered on the part itself: kind and id are required, the name is optional and the spec asks for it
    const Json card = p.navigate("party:self");
    const Json part = [&] {
      for (const auto& c : card.at("children"))
        if (c.at("address") == "subgraph:self/units") return c;
      FAIL("no part units");
      return Json();
    }();
    const Json& define = action_of(part, "units.define");
    REQUIRE(define.at("params").is_array());
    std::vector<std::string> names;
    for (const auto& q : define.at("params")) names.push_back(q.at("name"));
    CHECK(names == std::vector<std::string>{"kind", "id", "label"});
    CHECK(define.at("params").at(0).at("required") == true);
    CHECK(define.at("params").at(2).at("required") == false);
    CHECK(define.at("params").at(0).at("label") == "Rodzaj (na przykład projekt)");
    // the unit's own actions get kind and id from the node: nothing is left to ask
    const Json& u = units.at("children").at(0);
    CHECK(action_of(u, "units.forget").at("params").empty());
    // a field of the profile: the slot comes from the node, the value is asked
    bool found = false;
    for (const char* part_id : {"identity", "preferences", "slots"}) {
      const Json nav = p.navigate(std::string("subgraph:self/") + part_id);
      for (const auto& c : nav.at("children"))
        if (has_action(c, "profile.set") && c.at("evidence").is_object()) {
          const Json& set = action_of(c, "profile.set");
          REQUIRE(set.at("params").size() == 1);
          CHECK(set.at("params").at(0).at("name") == "value");
          CHECK(set.at("params").at(0).at("label") == "Nowa wartość");
          found = true;
          break;
        }
      if (found) break;
    }
    CHECK(found);
  }

  TEST_CASE("an inferred topic or contact can be confirmed or rejected; confirming makes it the person's word with the time, rejecting keeps the entry and a record") {
    Person p;
    NodeOptions o;
    const std::string topic = unwrap(p.rt->db().create_node("kuchnia", "topic", o));
    const std::string contact = unwrap(p.rt->db().create_node("Jan Kowalski", "person", o));
    auto row = [&](const std::string& part, const std::string& address) {
      const Json nav = p.navigate(part);
      const Json* r = child(nav, address);
      REQUIRE(r);
      return *r;
    };
    Json t = row("subgraph:self/interests", "interest:kuchnia");
    CHECK(t.at("evidence").at("class") == "inferred");
    CHECK(has_action(t, "inference.confirm"));
    CHECK(has_action(t, "inference.reject"));
    CHECK(action_of(t, "inference.confirm").at("args").at("entry") == topic);
    CHECK(action_of(t, "inference.confirm").at("params").empty());  // the node fills the only argument
    // nothing is written until the person says yes
    const Json waiting = p.call("inference.reject", Json{{"entry", topic}});
    REQUIRE(waiting.at("status") == "awaiting_confirmation");
    CHECK(row("subgraph:self/interests", "interest:kuchnia").at("evidence").at("class") == "inferred");
    const Json done = unwrap(p.rt->actions().confirm(Json{{"run_id", waiting.at("id")}, {"approved", true}, {"receipt_id", waiting.at("pending").at("receipt_id")}, {"ref", "test"}}));
    INFO(done.dump());
    REQUIRE(done.at("status") == "completed");
    t = row("subgraph:self/interests", "interest:kuchnia");  // the entry is still there
    CHECK(t.at("evidence").is_null());
    CHECK(t.at("state") == "rejected");
    CHECK(p.card("interest:kuchnia").at("attrs").at("review").at("decision") == "rejected");
    CHECK(p.card("interest:kuchnia").at("attrs").at("review").at("time").is_string());
    CHECK(p.card("interest:kuchnia").at("attrs").at("review").at("was").at("class") == "inferred");  // what it was before the verdict
    CHECK_FALSE(has_action(t, "inference.reject"));
    CHECK(has_action(t, "inference.confirm"));
    const auto stored = unwrap(p.rt->db().get_node(topic));
    REQUIRE(stored.has_value());
    CHECK(stored->metadata.at("review").at("decision") == "rejected");
    // a later confirmation turns it into the person's word and keeps the earlier verdict in the history
    p.confirmed("inference.confirm", Json{{"entry", topic}});
    t = row("subgraph:self/interests", "interest:kuchnia");
    CHECK(t.at("evidence").at("class") == "said");
    CHECK(t.at("evidence").at("provenance").at("method") == "user_confirmed");
    CHECK(t.at("evidence").at("provenance").at("time").is_string());
    CHECK(t.at("evidence").at("human") == "potwierdzone przez Ciebie");
    CHECK(p.card("interest:kuchnia").at("attrs").at("review").at("history").size() == 1);
    CHECK(has_action(t, "inference.reject"));
    CHECK_FALSE(has_action(t, "inference.confirm"));
    // the same verdict twice changes nothing
    const Json again = p.confirmed("inference.confirm", Json{{"entry", topic}});
    CHECK(again.at("result").at("data").at("written") == false);
    // a contact (person found by the analysis) goes the same way
    Json c = row("subgraph:self/network", "party:" + contact.substr(2));
    CHECK(c.at("evidence").at("class") == "inferred");
    p.confirmed("inference.confirm", Json{{"entry", contact}});
    c = row("subgraph:self/network", "party:" + contact.substr(2));
    CHECK(c.at("evidence").at("class") == "said");
    // an unknown entry is refused with words
    const Json bad = p.confirmed("inference.confirm", Json{{"entry", "n_nope"}});
    CHECK(bad.at("status") != "completed");
    CHECK(p.transport->requests().empty());
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
    CHECK(old->at("evidence").at("human") == "pochodzenie niezapisane - starsza notatka");
    CHECK(old->at("evidence").at("class") == "declared");  // not claimed to be something you said
    const Json note = p.card(known->at("address"));
    CHECK(note.at("attrs").at("provenance").at("known") == true);
    CHECK(note.at("attrs").at("provenance").at("source") == "user");
    CHECK(note.at("attrs").at("provenance").at("ref") == "m_test");
    const Json old_card = p.card(old->at("address"));
    CHECK(old_card.at("attrs").at("provenance").at("known") == false);
    CHECK(old_card.at("attrs").at("provenance").at("words") == "pochodzenie niezapisane - starsza notatka");
    // forgetting is offered on each note, with the note's own id in the arguments (the card shows no id as text)
    CHECK(action_of(old_card, "memory.forget").at("args").at("query") == old_card.at("attrs").at("note_id"));
    // the breadcrumbs lead through the part to You
    const Json crumbs = note.at("breadcrumbs");
    REQUIRE(crumbs.size() >= 2);
    CHECK(crumbs[crumbs.size() - 1].at("address") == "subgraph:self/memory");
    CHECK(crumbs[crumbs.size() - 2].at("address") == "party:self");
    // the same, in English
    const Json en = p.navigate("subgraph:self/memory", "en");
    bool found = false;
    for (const auto& c : en.at("children"))
      if (c.at("evidence").at("human") == "origin not recorded - an older note") found = true;
    CHECK(found);
  }

  TEST_CASE("Zapomnij: the entry leaves the conversations and the memory text, stays on the folded list Zapomniane with the record of forgetting; the words are kept or dropped by a preset") {
    for (const bool keep : {true, false}) {
      fsutil::TempDir td;
      if (!keep) {
        std::filesystem::create_directories(td.path() / "profiles");
        unwrap(fsutil::write_file(td.path() / "profiles" / "chat_ops.pack",
                                  R"({"schema":"loom.runtime_profile_overlay/1","domain":"chat_ops","overrides":{"forget":{"keep_content":false}}})"));
      }
      RuntimeOptions o;
      o.data_dir = td.path().string();
      o.start_workers = false;
      o.http = std::make_shared<net::ScriptedTransport>();
      auto rt = unwrap(Runtime::open(o));
      rt->config().set("model_registry_autorefresh", "off");
      const std::string id = unwrap(rt->memory().add_node("Mam kota Filemona"));
      unwrap(rt->memory().add_node("Lubię herbatę"));
      auto nav = [&](const std::string& part) { return unwrap(rt->actions().invoke("graph.navigate", Json{{"focus", part}, {"lang", "pl"}, {"limit", 0}, {"hops", 1}})); };
      const Json waiting = unwrap(rt->actions().invoke("run", Json{{"action", "memory.forget"}, {"element", Json{{"class", "application"}}}, {"args", Json{{"query", id}}}, {"locale", "pl"}, {"ref", "test"}}));
      REQUIRE(waiting.at("status") == "awaiting_confirmation");
      CHECK(rt->memory().get_node(id)->active);   // nothing before the yes
      const Json done = unwrap(rt->actions().confirm(Json{{"run_id", waiting.at("id")}, {"approved", true}, {"receipt_id", waiting.at("pending").at("receipt_id")}, {"ref", "test"}}));
      REQUIRE(done.at("status") == "completed");
      // not deleted: inactive, with the record; the memory text no longer holds it
      const auto node = rt->memory().get_node(id);
      REQUIRE(node);
      CHECK_FALSE(node->active);
      CHECK(node->metadata.at("forgotten").at("kept") == keep);
      CHECK(node->metadata.at("forgotten").at("by") == "user");
      CHECK(node->metadata.at("forgotten").contains("time"));
      CHECK(node->content.empty() == !keep);
      CHECK(rt->memory().get_active_context(16000).find("Filemona") == std::string::npos);
      CHECK(rt->memory().get_active_context(16000).find("herbat") != std::string::npos);
      // the screen: gone from "Co pamiętam", on the list "Zapomniane" (its own part) with the status and the sentence
      const Json mem = nav("subgraph:self/memory");
      CHECK(mem.at("children").size() == 1);
      const Json gone = nav("subgraph:self/forgotten");
      REQUIRE(gone.at("children").size() == 1);
      const Json& row = gone.at("children")[0];
      CHECK(row.at("status") == "forgotten");
      const Json card = unwrap(rt->actions().invoke("graph.card", Json{{"address", row.at("address")}, {"lang", "pl"}}));
      CHECK(card.at("attrs").at("forgotten").at("kept") == keep);
      CHECK(row.at("why").get<std::string>().find("Zapomniałeś") == 0);
      if (!keep) CHECK(row.at("label") == "(treść niezachowana)");
      const Json me = nav("party:self");
      bool part = false;
      for (const auto& c : me.at("children")) part = part || c.at("address") == "subgraph:self/forgotten";
      CHECK(part);
    }
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
        if (c.at("evidence").is_null()) {
          // an entry that holds no value has no evidence: it says why in its state (unanswered | declined | never)
          CHECK(c.at("kind") == "profile_slot");
          CHECK(c.at("state").is_string());
        } else {
          CHECK(c.at("evidence").at("class").is_string());
          CHECK(c.at("evidence").at("provenance").contains("method"));
        }
        CHECK(c.at("actions").is_array());
        if (carded.insert(c.at("kind").get<std::string>()).second) to_card.push_back(address);
      }
      for (const auto& address : to_card) {
        INFO(lang << " " << address);
        const Json card = p.card(address, lang);
        std::vector<std::string> texts = {card.at("label").get<std::string>(), card.value("why", ""), card.value("value_human", "")};
        if (card.at("evidence").is_object()) texts.push_back(card.at("evidence").value("human", ""));
        else texts.push_back(card.value("state", ""));  // no evidence: the state is its word
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
