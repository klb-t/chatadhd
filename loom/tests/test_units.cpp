// M1-K2c-1: the units a conversation belongs to (units.define / units.list / units.forget, conversation.set_unit / conversation.clear_unit) and the
// provenance of memory notes. Every write of the person's data asks first (the confirmation gate of the runner), and until the yes nothing is written.
// No network, no model; synthetic data only.
#include <doctest/doctest.h>

#include <filesystem>
#include <regex>

#include "loom/action_runner.h"
#include "loom/config.h"
#include "loom/loom.h"
#include "loom/memory_engine.h"
#include "loom/memory_folder.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/scope_resolver.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Fx {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  explicit Fx(const std::string& memory_json = std::string()) {
    if (!memory_json.empty()) REQUIRE(fsutil::write_file(td.path() / "memory.json", memory_json));
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
  }
  Json call(const std::string& action, Json request = Json::object(), const std::string& conv = std::string()) {
    if (!request.contains("locale")) request["locale"] = "pl";
    if (!conv.empty()) request["context"] = Json{{"conv_id", conv}};
    return unwrap(rt->actions().invoke(action, request));
  }
  // An action that waits for the person: it asks first and writes nothing.
  Json ask(const std::string& action, Json request, const std::string& conv = std::string()) {
    const Json waiting = call(action, std::move(request), conv);
    INFO(waiting.dump());
    REQUIRE(waiting.at("status") == "awaiting_confirmation");
    return waiting;
  }
  Json yes(const Json& waiting) {
    return unwrap(rt->actions().confirm(Json{{"run_id", waiting.at("id")}, {"approved", true}, {"receipt_id", waiting.at("pending").at("receipt_id")}, {"ref", "test"}}));
  }
  Json confirmed(const std::string& action, Json request, const std::string& conv = std::string()) { return yes(ask(action, std::move(request), conv)); }
  static Json data_of(const Json& run) {
    INFO(run.dump());
    REQUIRE(run.at("status") == "completed");
    return run.at("result").value("data", Json::object());
  }
  static std::string text_of(const Json& run) {
    std::string out;
    const Json parts = run.at("result").value("parts", Json::array());
    for (const auto& p : parts)
      if (p.is_object() && p.contains("text") && p.at("text").is_string()) out += p.at("text").get<std::string>() + "\n";
    return out;
  }
  static std::string failure_of(const Json& run) { return run.contains("error") && !run.at("error").is_null() ? run.at("error").dump() : run.dump(); }
  Database& db() { return rt->db(); }
  Conversation conv(const std::string& id) {
    auto c = unwrap(db().get_conv(id));
    REQUIRE(c.has_value());
    return *c;
  }
  std::string new_conv(const std::string& title, const Json& meta = Json::object()) {
    const std::string id = unwrap(db().create_conv(title)).id;
    if (!meta.empty()) {
      ConvPatch patch;
      patch.metadata = meta;
      LOOM_REQUIRE_OK(db().update_conv(id, patch));
    }
    return id;
  }
  std::optional<Node> unit_node(const std::string& kind, const std::string& id) { return unwrap(db().get_node("unit:" + kind + "/" + id)); }
  void no_network() { CHECK(transport->requests().empty()); }
  // An action that is no gate's business (memory.remember / memory.recall): a run of the kernel, in Polish, from a conversation and a message.
  Json run(const std::string& action, Json args, Json context = Json::object()) {
    args["lang"] = "pl";
    return unwrap(rt->actions().run(Json{{"action", action}, {"element", "application"}, {"args", args}, {"context", context}}));
  }
};

// Words that are internal names: keys with an underscore, dotted ids, true/false, key=value, the ids of operations.
bool has_internal_name(const std::string& visible, std::string* what = nullptr) {
  static const std::regex bad("[a-z]+_[a-z]+|\\b[a-z]+\\.[a-z]+\\b|\\btrue\\b|\\bfalse\\b|=|\\bop\\b");
  std::smatch m;
  if (std::regex_search(visible, m, bad)) {
    if (what) *what = m.str(0);
    return true;
  }
  return false;
}

const char* kOldMemory = R"([{"id":"aaaaaaaaaaaa","content":"stara notatka sprzed M1","parent_id":null,"node_type":"text","active":true,"depth":0,"weight":1.0,"tags":[],"created":"2025-01-01T00:00:00Z","metadata":{}}])";

}  // namespace

TEST_SUITE("units") {
  TEST_CASE("units.define asks first and writes nothing until the yes; then the node is unit:<kind>/<id>, and the same unit again changes only the label after another yes") {
    Fx f;
    const Json waiting = f.ask("units.define", Json{{"kind", "project"}, {"id", "alfa"}, {"label", "Projekt Alfa"}});
    CHECK_FALSE(f.unit_node("project", "alfa"));  // asked, not yet written
    CHECK(waiting.dump().find("potwierdzasz sam") != std::string::npos);  // the question says why, in the person's words
    const Json done = Fx::data_of(f.yes(waiting));
    CHECK(done.at("node") == "unit:project/alfa");
    CHECK(done.at("created") == true);
    const auto node = f.unit_node("project", "alfa");
    REQUIRE(node);
    CHECK(node->kind == "unit");
    CHECK(node->label == "Projekt Alfa");
    CHECK(node->metadata.at("unit_kind") == "project");
    CHECK(node->metadata.at("unit_id") == "alfa");
    CHECK_FALSE(node->created.empty());
    // the same kind and id again, the same label: idempotent (still asks, changes nothing)
    const Json same = f.confirmed("units.define", Json{{"kind", "project"}, {"id", "alfa"}, {"label", "Projekt Alfa"}});
    CHECK(Fx::data_of(same).at("created") == false);
    CHECK(Fx::text_of(same).find("już jest") != std::string::npos);
    // another label: not silently - the label stays until the yes
    const Json waiting2 = f.ask("units.define", Json{{"kind", "project"}, {"id", "alfa"}, {"label", "Alfa 2"}});
    CHECK(f.unit_node("project", "alfa")->label == "Projekt Alfa");
    const Json relabelled = Fx::data_of(f.yes(waiting2));
    CHECK(relabelled.at("previous_label") == "Projekt Alfa");
    CHECK(f.unit_node("project", "alfa")->label == "Alfa 2");
    CHECK(f.unit_node("project", "alfa")->metadata.at("unit_kind") == "project");  // nothing else touched
    f.no_network();
  }

  TEST_CASE("units.define refuses an unknown kind (naming the known ones), an empty or slashed id; the kind may be said by its Polish label; kinds are data and include the group of teams") {
    Fx f;
    const Json bad_kind = f.confirmed("units.define", Json{{"kind", "planeta"}, {"id", "x"}});
    CHECK(bad_kind.at("status") != "completed");
    const std::string why = Fx::failure_of(bad_kind);
    CHECK(why.find("Nie znam rodzaju jednostki") != std::string::npos);
    for (const char* kind_label : {"Projekt", "Zespół", "Organizacja", "Grupa zespołów"}) CHECK_MESSAGE(why.find(kind_label) != std::string::npos, kind_label);
    CHECK(Fx::failure_of(f.confirmed("units.define", Json{{"kind", "project"}, {"id", "a/b"}})).find("znaku „/”") != std::string::npos);
    CHECK(Fx::failure_of(f.confirmed("units.define", Json{{"kind", "project"}, {"id", "  "}})).find("znaku „/”") != std::string::npos);
    // by the label, any letter case; the group of teams is a kind (the chain already had its level)
    CHECK(Fx::data_of(f.confirmed("units.define", Json{{"kind", "zespół"}, {"id", "backend"}})).at("kind") == "team");
    CHECK(Fx::data_of(f.confirmed("units.define", Json{{"kind", "grupa zespołów"}, {"id", "dzial-it"}})).at("kind") == "group_of_groups");
    CHECK(f.unit_node("group_of_groups", "dzial-it"));
    // a name said once is the id and the label
    const Json named = Fx::data_of(f.confirmed("units.define", Json{{"kind", "project"}, {"label", "Remont"}}));
    CHECK(named.at("id") == "Remont");
    CHECK(f.unit_node("project", "Remont")->label == "Remont");
  }

  TEST_CASE("conversation.set_unit asks first, merges into the metadata (flow and settings stay, `updated` stays), and clear_unit takes out only that kind") {
    Fx f;
    const std::string cid = f.new_conv("Wywiad", Json{{"flow", "profile.interview"}, {"settings", Json{{"default_model", "m/x"}}}});
    const std::string before_updated = f.conv(cid).updated;
    f.confirmed("units.define", Json{{"kind", "project"}, {"id", "alfa"}, {"label", "Alfa"}});
    const Json req{{"conv_id", cid}, {"kind", "project"}, {"id", "alfa"}};
    const Json waiting = f.ask("conversation.set_unit", req);
    CHECK_FALSE(f.conv(cid).metadata.contains("units"));  // asked, not yet written
    const Json done = Fx::data_of(f.yes(waiting));
    CHECK(done.at("changed") == true);
    CHECK(done.at("defined") == true);
    Json meta = f.conv(cid).metadata;
    CHECK(meta.at("units") == Json{{"project", "alfa"}});
    CHECK(meta.at("flow") == "profile.interview");  // the other keys are never touched
    CHECK(meta.at("settings").at("default_model") == "m/x");
    CHECK(f.conv(cid).updated == before_updated);  // assigning is not talking: the list order does not move
    // a second kind goes next to the first; the open conversation is the default
    f.confirmed("conversation.set_unit", Json{{"kind", "team"}, {"id", "backend"}}, cid);
    CHECK(f.conv(cid).metadata.at("units") == Json{{"project", "alfa"}, {"team", "backend"}});
    // the same again: nothing to write
    CHECK(Fx::data_of(f.confirmed("conversation.set_unit", req)).at("changed") == false);
    // an unknown unit is accepted and said so
    const Json undef = f.confirmed("conversation.set_unit", Json{{"conv_id", cid}, {"kind", "organization"}, {"id", "firma"}});
    CHECK(Fx::data_of(undef).at("defined") == false);
    CHECK(Fx::text_of(undef).find("nie ma jeszcze na Twojej liście") != std::string::npos);
    // clear: only the kind named
    const Json clearing = f.ask("conversation.clear_unit", Json{{"kind", "project"}}, cid);
    CHECK(f.conv(cid).metadata.at("units").contains("project"));  // asked, not yet written
    CHECK(Fx::data_of(f.yes(clearing)).at("previous") == "alfa");
    meta = f.conv(cid).metadata;
    CHECK(meta.at("units") == Json{{"team", "backend"}, {"organization", "firma"}});
    CHECK(meta.at("flow") == "profile.interview");
    // nothing left of that kind: said, not written
    CHECK(Fx::data_of(f.confirmed("conversation.clear_unit", Json{{"kind", "project"}}, cid)).at("changed") == false);
    // the last unit gone: the conversation looks like one that never had units
    f.confirmed("conversation.clear_unit", Json{{"kind", "team"}}, cid);
    f.confirmed("conversation.clear_unit", Json{{"kind", "organization"}}, cid);
    CHECK_FALSE(f.conv(cid).metadata.contains("units"));
    CHECK(f.conv(cid).metadata.at("flow") == "profile.interview");
    // no conversation named and none open: asked for, not guessed
    const Json none = f.confirmed("conversation.set_unit", Json{{"kind", "project"}, {"id", "alfa"}});
    CHECK(none.at("status") != "completed");
    CHECK(Fx::failure_of(none).find("o którą rozmowę") != std::string::npos);
    f.no_network();
  }

  TEST_CASE("the scope chain sees the unit after set_unit and stops seeing it after clear_unit (a rule of the project applies in the conversation only while it belongs to it)") {
    Fx f;
    const std::string cid = f.new_conv("Rozmowa");
    // a rule of the unit "alfa" (kind project) and one of the group of teams "dzial": the persistent way to give a unit its own value
    Json rules = Json::object();
    rules["alfa_preset"] = Json{{"id", "alfa_preset"}, {"scope", "project"}, {"scope_id", "alfa"}, {"target", Json{{"kind", "setting"}, {"key", "extrapolation.preset"}}},
                                {"effect", "set_value"}, {"value", "generous"}};
    rules["dzial_cost"] = Json{{"id", "dzial_cost"}, {"scope", "group_of_groups"}, {"scope_id", "dzial"}, {"target", Json{{"kind", "setting"}, {"key", "extrapolation.preset"}}},
                               {"effect", "set_value"}, {"value", "careful"}};
    std::filesystem::create_directories(f.td.path() / "profiles");
    REQUIRE(fsutil::write_file(f.td.path() / "profiles" / "action_rules.pack",
                               Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "action_rules"}, {"overrides", Json{{"rules", rules}}}}.dump()));
    scopes::Binding b;
    b.key = "extrapolation.preset";
    b.installation = "standard";
    b.conv_id = cid;
    auto resolve = [&] {
      auto r = unwrap(scopes::Resolver::open(f.td.path(), &f.rt->config(), &f.db()));
      return unwrap(r.resolve(b));
    };
    const auto outside_before = resolve();
    CHECK(outside_before.scope == "installation");  // not in a project yet: the project's rule does not apply
    CHECK(outside_before.value == "standard");
    f.confirmed("units.define", Json{{"kind", "project"}, {"id", "alfa"}});
    f.confirmed("conversation.set_unit", Json{{"conv_id", cid}, {"kind", "project"}, {"id", "alfa"}});
    const auto inside = resolve();
    CHECK(inside.value == "generous");
    CHECK(inside.scope == "project");
    CHECK(inside.scope_id == "alfa");
    f.confirmed("conversation.clear_unit", Json{{"conv_id", cid}, {"kind", "project"}});
    const auto outside = resolve();
    CHECK(outside.scope == "installation");
    CHECK(outside.value == "standard");
    // another unit of the same kind is not this one
    f.confirmed("conversation.set_unit", Json{{"conv_id", cid}, {"kind", "project"}, {"id", "beta"}});
    CHECK(resolve().scope == "installation");
    // the group of teams (a kind added next to the seeds) reaches the chain the same way
    f.confirmed("conversation.set_unit", Json{{"conv_id", cid}, {"kind", "grupa zespołów"}, {"id", "dzial"}});
    const auto group = resolve();
    CHECK(group.scope == "group_of_groups");
    CHECK(group.value == "careful");
  }

  TEST_CASE("units.list counts the conversations of each unit, marks a unit the list does not know, filters by kind; units.forget asks first and says how many conversations it touched") {
    Fx f;
    f.confirmed("units.define", Json{{"kind", "project"}, {"id", "alfa"}, {"label", "Alfa"}});
    f.confirmed("units.define", Json{{"kind", "team"}, {"id", "backend"}, {"label", "Backend"}});
    const std::string c1 = f.new_conv("Jeden");
    const std::string c2 = f.new_conv("Dwa", Json{{"flow", "x"}});
    const std::string c3 = f.new_conv("Trzy");
    for (const std::string& c : {c1, c2}) f.confirmed("conversation.set_unit", Json{{"conv_id", c}, {"kind", "project"}, {"id", "alfa"}});
    f.confirmed("conversation.set_unit", Json{{"conv_id", c3}, {"kind", "team"}, {"id", "ghost"}});  // not on the list
    const Json listed = f.call("units.list");
    const Json data = Fx::data_of(listed);
    std::map<std::string, Json> by_node;
    for (const auto& it : data.at("items")) by_node[it.at("node").get<std::string>()] = it;
    REQUIRE(by_node.size() == 3);
    CHECK(by_node.at("unit:project/alfa").at("conversations") == 2);
    CHECK(by_node.at("unit:team/backend").at("conversations") == 0);
    CHECK(by_node.at("unit:team/backend").at("defined") == true);
    CHECK(by_node.at("unit:team/ghost").at("conversations") == 1);
    CHECK(by_node.at("unit:team/ghost").at("defined") == false);
    const std::string text = Fx::text_of(listed);
    CHECK(text.find("Projekt „Alfa” - rozmów: 2") != std::string::npos);
    CHECK(text.find("nie ma jej na liście jednostek") != std::string::npos);
    CHECK(Fx::data_of(f.call("units.list", Json{{"kind", "Zespół"}})).at("items").size() == 2);
    // forget: asks, writes nothing; then both conversations lose the unit, the node goes, the others stay
    const Json waiting = f.ask("units.forget", Json{{"kind", "project"}, {"id", "alfa"}});
    CHECK(f.unit_node("project", "alfa"));
    CHECK(f.conv(c1).metadata.contains("units"));
    const Json forgotten = f.yes(waiting);
    const Json fd = Fx::data_of(forgotten);
    CHECK(fd.at("conversations") == 2);
    CHECK(fd.at("removed_node") == true);
    CHECK(Fx::text_of(forgotten).find("Rozmów, które z niej zdjąłem: 2") != std::string::npos);
    CHECK_FALSE(f.unit_node("project", "alfa"));
    CHECK_FALSE(f.conv(c1).metadata.contains("units"));
    CHECK_FALSE(f.conv(c2).metadata.contains("units"));
    CHECK(f.conv(c2).metadata.at("flow") == "x");
    CHECK(f.conv(c3).metadata.at("units") == Json{{"team", "ghost"}});
    CHECK(f.unit_node("team", "backend"));
    // a unit nobody knows is not forgotten into silence
    const Json nobody = f.confirmed("units.forget", Json{{"kind", "project"}, {"id", "alfa"}});
    CHECK(nobody.at("status") != "completed");
    CHECK(Fx::failure_of(nobody).find("Nie znam jednostki") != std::string::npos);
    // a unit that is only pointed to (no node) is forgotten too, and the count is said
    CHECK(Fx::data_of(f.confirmed("units.forget", Json{{"kind", "team"}, {"id", "ghost"}})).at("removed_node") == false);
    CHECK_FALSE(f.conv(c3).metadata.contains("units"));
    f.no_network();
  }

  TEST_CASE("every visible sentence of the unit and provenance words, in Polish and in English, is free of internal names") {
    const Json pack = unwrap(RuntimeProfile::builtin("chat_ops")).values();
    static const std::regex placeholder("\\{\\{[^}]*\\}\\}");
    int checked = 0;
    for (const char* lang : {"pl", "en"}) {
      const Json texts = pack.at("texts").at(lang);
      for (auto it = texts.begin(); it != texts.end(); ++it) {
        if (it.key().rfind("unit_", 0) != 0 && it.key().rfind("memory_prov", 0) != 0 && it.key() != "memory_line_prov") continue;
        std::string what;
        const std::string visible = std::regex_replace(it.value().get<std::string>(), placeholder, "");
        CHECK_MESSAGE(!has_internal_name(visible, &what), lang << " " << it.key() << ": internal name \"" << what << "\" in: " << visible);
        ++checked;
      }
    }
    CHECK(checked >= 40);
    // the questions (the gates) and the catalogue labels too
    const Json rules = unwrap(RuntimeProfile::builtin("action_rules")).values();
    const Json catalogue = unwrap(RuntimeProfile::builtin("actions")).values().at("actions");
    for (const char* id : {"units.define", "units.forget", "conversation.set_unit", "conversation.clear_unit"}) {
      const Json rule = rules.at("rules").at(std::string("confirm:") + id);
      for (const char* lang : {"pl", "en"}) {
        std::string what;
        CHECK_MESSAGE(!has_internal_name(rule.at("reason").at(lang).get<std::string>(), &what), id << " " << lang << ": " << what);
      }
    }
    for (const char* id : {"units.define", "units.list", "units.forget", "conversation.set_unit", "conversation.clear_unit"}) {
      std::string what;
      CHECK_MESSAGE(!has_internal_name(catalogue.at(id).at("labels").at("pl").get<std::string>(), &what), id << ": " << what);
    }
  }

  TEST_CASE("memory provenance: memory.remember stamps {source user, time, ref = the message}, memory.recall answers it in words, and an old note says it is unknown and is not given an origin") {
    Fx f(kOldMemory);
    auto& mem = f.rt->memory();
    // the old note: nothing recorded, nothing invented
    const auto old_note = mem.get_node("aaaaaaaaaaaa");
    REQUIRE(old_note);
    CHECK_FALSE(old_note->metadata.contains("provenance"));
    // a new one, from a message of the conversation
    const Json remembered = Fx::data_of(f.run("memory.remember", Json{{"content", "lubię herbatę"}}, Json{{"conv_id", "c_x"}, {"message_id", "m_123456789abc"}}));
    const std::string rid = remembered.at("id").get<std::string>();
    const auto fresh = mem.get_node(rid);
    REQUIRE(fresh);
    const Json p = fresh->metadata.at("provenance");
    CHECK(p.at("source") == "user");
    CHECK(p.at("ref") == "m_123456789abc");
    CHECK(std::regex_match(p.at("time").get<std::string>(), std::regex("\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d.*Z")));
    CHECK(fresh->metadata.at("origin") == "chat");  // what was there stays
    // without a message: no ref (absent, not empty)
    const Json bare = Fx::data_of(f.run("memory.remember", Json{{"content", "druga"}}));
    CHECK_FALSE(mem.get_node(bare.at("id").get<std::string>())->metadata.at("provenance").contains("ref"));
    // the read side
    const Json recall = f.run("memory.recall", Json::object());
    const Json data = Fx::data_of(recall);
    std::map<std::string, Json> items;
    for (const auto& it : data.at("items")) items[it.at("id").get<std::string>()] = it;
    REQUIRE(items.count("aaaaaaaaaaaa"));
    CHECK(items.at("aaaaaaaaaaaa").at("provenance").at("known") == false);
    CHECK(items.at("aaaaaaaaaaaa").at("provenance").at("words") == "pochodzenie niezapisane - starsza notatka");
    CHECK(items.at(rid).at("provenance").at("known") == true);
    CHECK(items.at(rid).at("provenance").at("source") == "user");
    const std::string text = Fx::text_of(recall);
    CHECK(text.find("stara notatka sprzed M1 (aaaaaaaaaaaa) - pochodzenie niezapisane - starsza notatka") != std::string::npos);
    CHECK(text.find("lubię herbatę") != std::string::npos);
    CHECK(text.find("pochodzenie: napisałeś to Ty, ") != std::string::npos);
    CHECK(text.find("(m_123456789abc)") != std::string::npos);
    // an edit that replaces the metadata keeps the origin of a new note and gives none to an old one
    LOOM_REQUIRE_OK(mem.update_node(rid, Json{{"metadata", Json{{"note", "x"}}}}));
    CHECK(mem.get_node(rid)->metadata.at("provenance").at("source") == "user");
    CHECK(mem.get_node(rid)->metadata.at("note") == "x");
    LOOM_REQUIRE_OK(mem.update_node("aaaaaaaaaaaa", Json{{"metadata", Json{{"note", "y"}}}}));
    CHECK_FALSE(mem.get_node("aaaaaaaaaaaa")->metadata.contains("provenance"));
    // the file stays what the Python app reads: provenance is just a key of the free metadata
    const Json on_disk = unwrap(json::parse(unwrap(fsutil::read_file(f.td.path() / "memory.json"))));
    REQUIRE(on_disk.is_array());
    CHECK(on_disk.size() == 3);
    f.no_network();
  }

  TEST_CASE("stamped(): a client may name its source if the memory profile knows it (user | import | folder | guardian); anything else is written as the default; the time is always the engine's") {
    Fx f;
    auto& mem = f.rt->memory();
    const Json imp = mem.stamped(Json{{"provenance", Json{{"source", "import"}, {"ref", "claude.ai/2025-03"}, {"time", "1999-01-01T00:00:00Z"}}}}, "user");
    CHECK(imp.at("provenance").at("source") == "import");
    CHECK(imp.at("provenance").at("ref") == "claude.ai/2025-03");
    CHECK(imp.at("provenance").at("time") != "1999-01-01T00:00:00Z");
    const Json forged = mem.stamped(Json{{"provenance", Json{{"source", "bogus"}}}}, "user");
    CHECK(forged.at("provenance").at("source") == "user");
    CHECK_FALSE(forged.at("provenance").contains("ref"));
    CHECK(mem.stamped(Json(nullptr), "guardian").at("provenance").at("source") == "guardian");
  }

  TEST_CASE("every write path of the kernel stamps: the C ABI (web panel and HTTP) as the person, a folder as folder with the file as ref") {
    // the C ABI: loom_create_memory
    {
      fsutil::TempDir dir;
      const std::string opts = Json{{"data_dir", dir.path().string()}, {"start_workers", false}}.dump();
      const char* err = nullptr;
      LoomContext* ctx = loom_init_ex(opts.c_str(), &err);
      if (err) loom_free_string(err);
      REQUIRE(ctx != nullptr);
      const char* made = loom_create_memory(ctx, R"({"content":"z panelu"})");
      REQUIRE(made != nullptr);
      const Json node = unwrap(json::parse(made));
      loom_free_string(made);
      CHECK(node.at("metadata").at("provenance").at("source") == "user");
      CHECK(node.at("metadata").at("provenance").contains("time"));
      const char* imported = loom_create_memory(ctx, R"({"content":"z importu","metadata":{"provenance":{"source":"import","ref":"plik.json"}}})");
      REQUIRE(imported != nullptr);
      const Json inode = unwrap(json::parse(imported));
      loom_free_string(imported);
      CHECK(inode.at("metadata").at("provenance").at("source") == "import");
      CHECK(inode.at("metadata").at("provenance").at("ref") == "plik.json");
      loom_shutdown(ctx);
    }
    // a folder: the root, a directory and a file
    {
      Fx f;
      const std::filesystem::path root = f.td.path() / "folder";
      std::filesystem::create_directories(root / "pod");
      REQUIRE(fsutil::write_file(root / "pod" / "a.txt", "tresc"));
      REQUIRE(fsutil::write_file(root / "b.txt", "inna"));
      memory_folder::Options o;
      const Json r = unwrap(memory_folder::attach(f.rt->memory(), root, o));
      int files = 0, dirs = 0;
      for (const auto& n : f.rt->memory().get_all()) {
        REQUIRE(n.metadata.contains("provenance"));
        CHECK(n.metadata.at("provenance").at("source") == "folder");
        CHECK(n.metadata.at("provenance").contains("ref"));
        if (n.node_type == "file") {
          ++files;
          CHECK(n.metadata.at("provenance").at("ref") == n.metadata.at("path"));
        } else {
          ++dirs;
        }
      }
      CHECK(files == 2);
      CHECK(dirs == 2);
      (void)r;
    }
  }
}
