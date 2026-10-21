// M1-D2: the data of the context selector - scoped values, phrases, open coverage entries, the words of "What we sent", the subgraphs of You.
// These are data checks only: the kernel starts to read most of this with M1-K1 and M1-K2, so every planned part must say so.
#include <doctest/doctest.h>

#include <filesystem>
#include <set>

#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

namespace fs = std::filesystem;

Json builtin(const char* domain) { return unwrap(RuntimeProfile::builtin(domain)).values(); }

Json data_file(const char* relative) {
  const fs::path data = fs::path(LOOM_TEST_FIXTURES).parent_path().parent_path() / "data";
  return unwrap(json::parse(unwrap(fsutil::read_file(data / relative))));
}

bool both(const Json& v) { return v.is_object() && !v.value("pl", "").empty() && !v.value("en", "").empty(); }

}  // namespace

TEST_SUITE("context_data") {
  TEST_CASE("scoped values context.*: labels in both languages, the turn as the default level, the steps as data, partial since the kernel reads them (M1-K1a)") {
    const Json keys = builtin("action_rules").at("scoped_values").at("keys");
    for (const char* key : {"context.profile", "context.breadth_delta", "context.depth_delta"}) {
      INFO(key);
      REQUIRE(keys.contains(key));
      const Json& k = keys.at(key);
      CHECK(both(k.at("label")));
      CHECK(k.at("default_level") == "turn");
      CHECK(k.at("status").at("state") == "partial");  // M1-K1a reads the scope chain and the turn carrier; the action that fills the carrier is M1-K1b
      CHECK(both(k.at("status").at("today")));
    }
    const Json& breadth = keys.at("context.breadth_delta").at("steps");
    CHECK(breadth.at("wider") == Json{{"op", "multiply"}, {"by", 2}, {"label", Json{{"pl", "szerzej"}, {"en", "wider"}}}});
    CHECK(breadth.at("narrower").at("op") == "divide");
    CHECK(breadth.at("narrower").at("by") == 2);
    const Json& depth = keys.at("context.depth_delta").at("steps");
    CHECK(depth.at("deeper").at("by") == 1);
    CHECK(depth.at("shallower").at("by") == -1);
    // the profile key lists its values where the presets are
    CHECK(keys.at("context.profile").at("values_from").at("pointer") == "/selector_profiles");
    // the older keys are untouched
    CHECK(keys.contains("chat_representation"));
    CHECK_FALSE(keys.at("chat_flow").contains("default_level"));
  }

  TEST_CASE("the phrases of the context modifiers exist in Polish and English and the router does not read them yet (status planned)") {
    const Json cm = builtin("intent_routing").at("heuristic").at("context_modifiers");
    CHECK(cm.at("status").at("state") == "planned");
    CHECK(both(cm.at("note")));
    std::set<std::string> ids;
    for (const auto& m : cm.at("modifiers")) {
      ids.insert(m.at("id").get<std::string>());
      CHECK_FALSE(m.at("phrases").at("pl").empty());
      CHECK_FALSE(m.at("phrases").at("en").empty());
    }
    CHECK(ids == std::set<std::string>{"wider", "narrower", "deeper", "shallower"});
    const auto has = [&](const Json& phrases, const char* lang, const char* text) {
      for (const auto& p : phrases.at(lang)) if (p == text) return true;
      return false;
    };
    for (const auto& m : cm.at("modifiers")) {
      const std::string id = m.at("id");
      if (id == "wider") { CHECK(has(m.at("phrases"), "pl", "szerzej")); CHECK(has(m.at("phrases"), "pl", "weź szerszy kontekst")); CHECK(has(m.at("phrases"), "en", "wider")); }
      if (id == "narrower") { CHECK(has(m.at("phrases"), "pl", "węziej")); CHECK(has(m.at("phrases"), "en", "narrower")); }
      if (id == "deeper") { CHECK(has(m.at("phrases"), "pl", "głębiej")); CHECK(has(m.at("phrases"), "pl", "głębiej o jeden")); CHECK(has(m.at("phrases"), "en", "deeper")); }
      if (id == "shallower") { CHECK(has(m.at("phrases"), "pl", "płycej")); CHECK(has(m.at("phrases"), "en", "shallower")); }
      // every phrase of a modifier steps a key the scoped values know
      CHECK(builtin("action_rules").at("scoped_values").at("keys").at(m.at("key").get<std::string>()).at("steps").contains(m.at("step").get<std::string>()));
    }
    CHECK(has(cm.at("profile").at("phrases"), "pl", "profil selektora {profile}"));
    CHECK(has(cm.at("receipt").at("phrases"), "pl", "co wysłaliśmy"));
    CHECK(has(cm.at("receipt").at("phrases"), "en", "what we sent"));
  }

  TEST_CASE("coverage: the modifiers and the receipt are open entries (no action yet) with a plan, not a covered capability") {
    const Json coverage = builtin("chat_coverage");
    std::set<std::string> open;
    for (const auto& o : coverage.at("open")) {
      open.insert(o.at("id").get<std::string>());
      if (o.at("id") == "context_modify" || o.at("id") == "context_receipt") {
        CHECK(both(o.at("title")));
        CHECK(both(o.at("reason")));
        CHECK(both(o.at("plan")));
        CHECK_FALSE(o.at("examples").at("pl").get<std::string>().empty());
      }
    }
    CHECK(open.count("context_modify") == 1);
    CHECK(open.count("context_receipt") == 1);
  }

  TEST_CASE("the words of \"What we sent\": the same keys in Polish and English, the disclaimer and the missing-receipt word are there, planned until the kernel and the sheet exist") {
    const Json pack = data_file("profiles/conversation_view.pack");
    Json entry;
    for (const auto& e : pack.at("entries")) if (e.at("key") == "presentation.context_receipt") entry = e;
    REQUIRE_FALSE(entry.is_null());
    const Json& value = entry.at("value");
    CHECK(value.at("status").at("state") == "planned");
    const Json& pl = value.at("locales").at("pl");
    const Json& en = value.at("locales").at("en");
    std::set<std::string> pl_keys, en_keys;
    for (auto it = pl.begin(); it != pl.end(); ++it) { pl_keys.insert(it.key()); CHECK_FALSE(it.value().get<std::string>().empty()); }
    for (auto it = en.begin(); it != en.end(); ++it) { en_keys.insert(it.key()); CHECK_FALSE(it.value().get<std::string>().empty()); }
    CHECK(pl_keys == en_keys);
    for (const char* key : {"section.system", "section.profile", "section.rules", "section.memory", "section.project", "section.history", "section.graph", "section.task",
                            "chip.profile_slot", "chip.rule", "chip.memory_node", "chip.graph_node", "chip.kb_claim", "dropped.thread_filter", "dropped.representation_level",
                            "dropped.budget", "dropped.history_window", "prefix.stable.yes", "prefix.stable.no", "guard.ok", "disclaimer", "missing"})
      CHECK_MESSAGE(pl_keys.count(key) == 1, key);
    CHECK(pl.at("disclaimer").get<std::string>().find("nie dowód odbioru") != std::string::npos);
    CHECK(pl.at("missing") == "Brak śladu (zapisane przed M1)");
    CHECK(pl.at("prefix.stable.yes") == "prefiks stały: tak");
    CHECK(pl.at("guard.ok").get<std::string>().find("strażnik ×10") != std::string::npos);
  }

  TEST_CASE("the subgraphs of You: every one has labels, an order for the prefix (memory has none: it is its own class) and a distinct stability rank") {
    const Json pack = data_file("profiles/user.pack");
    Json entry;
    for (const auto& e : pack.at("entries")) if (e.at("key") == "profile.subgraphs") entry = e;
    REQUIRE_FALSE(entry.is_null());
    std::set<std::string> ids;
    std::set<int> ranks;
    for (const auto& s : entry.at("value").at("subgraphs")) {
      INFO(s.at("id").get<std::string>());
      ids.insert(s.at("id").get<std::string>());
      CHECK(both(s.at("label")));
      CHECK(both(s.at("why")));
      CHECK(ranks.insert(s.at("stability_rank").get<int>()).second);
      if (s.at("id") == "memory") CHECK(s.at("order_for_prefix").is_null());
      else CHECK(s.at("order_for_prefix").is_number());
      if (s.at("id") == "preferences") CHECK(s.at("per_flow") == true);
    }
    CHECK(ids == std::set<std::string>{"identity", "preferences", "policies", "interests", "network", "units", "memory"});
  }
}
