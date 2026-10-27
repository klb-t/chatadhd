// P15, R54: everything the user can do, they can do from the conversation window. The audit is data (runtime/chat_coverage.pack):
// every surface of the kernel - a C ABI function, an HTTP route, a loom_invoke operation, a JNI symbol, a settings group or secret -
// is on exactly one of three lists: reached by an action from the conversation, not from the chat (with a reason), or open (with a
// plan). This test fails when a surface appears that is on none of them, when a list names one that is gone, when an action named
// as the way in is missing, planned or cannot be run, and when an example sentence does not reach its action through the router.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

const std::filesystem::path kLoomDir = std::filesystem::path(__FILE__).parent_path().parent_path();

std::string slurp(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  REQUIRE_MESSAGE(in.good(), "cannot read " << p.string());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

Json read_json(const std::filesystem::path& p) { return unwrap(json::parse(slurp(p))); }

// From `marker` to the closing brace at the start of a line (the end of the function that begins at the marker).
std::string region(const std::string& text, const std::string& marker) {
  const auto at = text.find(marker);
  REQUIRE_MESSAGE(at != std::string::npos, "marker not found: " << marker);
  const auto end = text.find("\n}\n", at);
  return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

std::set<std::string> literals(const std::string& text, const std::string& variable) {
  std::set<std::string> out;
  const std::regex re("\\b" + variable + "\\s*[!=]=\\s*\"([A-Za-z_.]+)\"");
  for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it) out.insert((*it)[1].str());
  return out;
}

// Every operation literal the loom_invoke dispatchers compare with (see runtime/chat_coverage.pack `sources.invoke`).
struct InvokeScan {
  std::set<std::string> ports, ops;
};

InvokeScan scan_invoke() {
  InvokeScan s;
  const std::string invoke = region(slurp(kLoomDir / "src/capi/capi_invoke.cpp"), "LOOM_API const char* loom_invoke(");
  s.ports = literals(invoke, "p");
  for (const auto& o : literals(invoke, "a")) s.ops.insert(o);
  const std::string runner = slurp(kLoomDir / "src/actions/runner.cpp");
  for (const auto& o : literals(region(runner, "Result<Json> ActionRunner::invoke("), "action")) s.ops.insert(o);
  for (const auto& o : literals(region(runner, "bool is_overlay_action("), "action")) s.ops.insert(o);
  for (const auto& o : literals(slurp(kLoomDir / "src/actions/query.cpp"), "action")) s.ops.insert(o);
  for (const auto& o : literals(slurp(kLoomDir / "src/storage/storage_command.cpp"), "action")) s.ops.insert(o);
  for (const auto& o : literals(region(slurp(kLoomDir / "src/capi/capi_media.cpp"), "LOOM_API const char* loom_github_sync("), "action")) s.ops.insert(o);
  return s;
}

// "actions.rules.set" -> "rules.set"; "storage.status" -> "storage.status" is matched whole.
bool op_matches(const std::string& key, const std::string& literal) {
  if (key == literal) return true;
  const auto dot = key.find('.');
  return dot != std::string::npos && key.substr(dot + 1) == literal;
}

using Surfaces = std::map<std::string, std::set<std::string>>;  // c_abi | http | jni | invoke | settings -> names

Surfaces registry_surfaces() {
  Surfaces s;
  const Json reg = read_json(kLoomDir / "data/graph/interfaces.pack").at("interfaces");
  for (const auto& e : reg.at("c_abi").at("entries")) s["c_abi"].insert(e.get<std::string>());
  for (const char* k : {"http", "web"})
    for (const auto& e : reg.at(k).at("entries")) s["http"].insert(e.get<std::string>());
  for (const auto& e : reg.at("jni").at("entries")) s["jni"].insert(e.get<std::string>());
  const Json fields = unwrap(RuntimeProfile::builtin("settings_fields")).values().at("fields");
  for (const auto& f : fields) {
    s["settings"].insert("group:" + f.at("group").get<std::string>());
    const std::string key = f.at("key").get<std::string>();
    if (f.value("scope", "") == "secret" || key.rfind("_", 0) == 0) s["settings"].insert("key:" + key);
  }
  return s;
}

// kind -> name -> {bucket, id}
struct Placement {
  std::string bucket, id;
};
using Placed = std::map<std::string, std::map<std::string, std::vector<Placement>>>;

Placed place(const Json& coverage) {
  Placed placed;
  for (const char* bucket : {"capabilities", "not_from_chat", "open"})
    for (const auto& e : coverage.at(bucket)) {
      if (!e.contains("surfaces")) continue;
      for (auto it = e.at("surfaces").begin(); it != e.at("surfaces").end(); ++it)
        for (const auto& name : it.value()) placed[it.key()][name.get<std::string>()].push_back({bucket, e.at("id").get<std::string>()});
    }
  return placed;
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id;
  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    for (const char* role : {"user", "assistant"}) {
      NewMessage m;
      m.conv_id = conv_id;
      m.role = role;
      m.text = std::string(role) + " says something about Friday.";
      unwrap(rt->db().create_msg(m));
    }
  }
};

}  // namespace

TEST_SUITE("chat_coverage") {
  TEST_CASE("every surface of the kernel is on exactly one list, and no list names a surface that is gone") {
    const Json coverage = unwrap(RuntimeProfile::builtin("chat_coverage")).values();
    Surfaces surfaces = registry_surfaces();
    // loom_invoke: the operations the pack lists must be the ones the dispatchers compare with.
    const InvokeScan scan = scan_invoke();
    std::set<std::string> ports;
    for (auto it = coverage.at("invoke_ports").begin(); it != coverage.at("invoke_ports").end(); ++it) ports.insert(it.key());
    CHECK_MESSAGE(scan.ports == ports, "the ports loom_invoke dispatches on differ from invoke_ports of the coverage pack");
    const Placed placed = place(coverage);
    std::set<std::string> listed_ops;
    if (placed.count("invoke"))
      for (const auto& [name, where] : placed.at("invoke")) listed_ops.insert(name);
    surfaces["invoke"] = listed_ops;
    for (const auto& literal : scan.ops) {
      bool known = false;
      for (const auto& key : listed_ops) known = known || op_matches(key, literal);
      CHECK_MESSAGE(known, "loom_invoke compares with the operation \"" << literal << "\" that no entry of the coverage pack lists (invoke: port.op)");
    }
    for (const auto& key : listed_ops) {
      bool exists = false;
      for (const auto& literal : scan.ops) exists = exists || op_matches(key, literal);
      CHECK_MESSAGE(exists, "the coverage pack lists the operation " << key << " that no dispatcher compares with");
    }
    // Every surface exactly once; nothing listed that does not exist.
    for (const auto& [kind, names] : surfaces) {
      for (const auto& name : names) {
        const std::vector<Placement>* where = nullptr;
        if (auto k = placed.find(kind); k != placed.end())
          if (auto n = k->second.find(name); n != k->second.end()) where = &n->second;
        CHECK_MESSAGE(where, kind << " \"" << name << "\" is neither reached from the conversation (capabilities), nor listed as not from the chat, nor open: decide it in runtime/chat_coverage.pack");
        if (where) CHECK_MESSAGE(where->size() == 1, kind << " \"" << name << "\" is listed " << where->size() << " times");
      }
    }
    for (const auto& [kind, names] : placed)
      for (const auto& [name, where] : names)
        CHECK_MESSAGE(surfaces[kind].count(name), kind << " \"" << name << "\" (" << where[0].id << ") is listed in the coverage pack but no longer exists");
  }

  TEST_CASE("the lists are well formed: reasons in both languages, secrets never reached from the chat") {
    const Json coverage = unwrap(RuntimeProfile::builtin("chat_coverage")).values();
    auto both = [](const Json& v) { return v.is_object() && !v.value("en", "").empty() && !v.value("pl", "").empty(); };
    std::set<std::string> ids;
    for (const char* bucket : {"capabilities", "not_from_chat", "open"})
      for (const auto& e : coverage.at(bucket)) {
        const std::string id = e.at("id").get<std::string>();
        CHECK_MESSAGE(ids.insert(id).second, "id used twice: " << id);
        if (std::string(bucket) == "not_from_chat") {
          CHECK_MESSAGE(both(e.at("reason")), id << ": a reason in English and Polish is required");
          CHECK_MESSAGE((!e.value("category", "").empty()), id << ": a category is required");
        } else if (std::string(bucket) == "open") {
          CHECK_MESSAGE(both(e.at("reason")), id << ": a reason in English and Polish is required");
          CHECK_MESSAGE(both(e.at("plan")), id << ": a plan in English and Polish is required");
          CHECK_MESSAGE(both(e.at("title")), id << ": a title in English and Polish is required");
        } else {
          CHECK_MESSAGE(both(e.at("title")), id << ": a title in English and Polish is required");
        }
      }
    // A credential is never reached from the conversation: its key is on the not_from_chat list.
    const Placed placed = place(coverage);
    const Json fields = unwrap(RuntimeProfile::builtin("settings_fields")).values().at("fields");
    for (const auto& f : fields)
      if (f.value("scope", "") == "secret") {
        const auto& where = placed.at("settings").at("key:" + f.at("key").get<std::string>());
        CHECK_MESSAGE(where[0].bucket == "not_from_chat", f.at("key").get<std::string>() << " is a credential; it must not be reached from the chat");
      }
  }

  TEST_CASE("each action named as the way in exists, is implemented and can be run; each example sentence reaches it through the router") {
    Fixture f;
    const Json coverage = unwrap(RuntimeProfile::builtin("chat_coverage")).values();
    const Json catalogue = unwrap(RuntimeProfile::builtin("actions")).values().at("actions");
    auto entry_of = [&](const std::string& id) -> const Json* { return json::find(catalogue, id); };
    for (const auto& c : coverage.at("capabilities")) {
      const std::string id = c.at("id").get<std::string>();
      const std::string via = c.at("via").get<std::string>();
      if (via == "native") {
        CHECK_MESSAGE(!c.contains("action"), id << ": a native capability names no action");
        continue;
      }
      REQUIRE_MESSAGE(c.contains("action"), id << ": an action is required");
      std::vector<std::string> actions{c.at("action").get<std::string>()};
      for (const auto& a : c.value("also", Json::array())) actions.push_back(a.get<std::string>());
      for (const auto& a : actions) {
        const Json* entry = entry_of(a);
        REQUIRE_MESSAGE(entry, id << ": no action '" << a << "' in the catalogue");
        if (via == "client") {
          // W2 (OKNA): carried out by the client that declares the capability; without a declaration it stays planned, by design
          CHECK_MESSAGE(entry->value("client", Json::object()).value("capability", "") != "", id << ": '" << a << "' is via client but has no client.capability");
          continue;
        }
        CHECK_MESSAGE(f.rt->actions().runnable(*entry), id << ": '" << a << "' is not runnable (planned, or a composite of its own consumer, or no executor)");
      }
      if (via == "emergent") continue;  // offered by the growing catalogue; the emergent tests route it
      const std::string check = c.value("check", c.contains("examples") ? "route" : "phrases");
      if (check == "continuation") {
        // reached from the proposal of another action, never from a sentence of its own: the declaration, its reason and the absence of sentences are checked
        CHECK_MESSAGE(c.value("chat_outcome", "") == "continuation", id << ": check continuation needs chat_outcome continuation");
        const Json reason = c.value("chat_outcome_reason", Json::object());
        CHECK_MESSAGE((!reason.value("pl", "").empty() && !reason.value("en", "").empty()), id << ": a continuation needs its reason in pl and en");
        CHECK_MESSAGE(!c.contains("examples"), id << ": a continuation has no sentences of its own");
        for (const auto& a : actions) CHECK_MESSAGE(!json::find(*entry_of(a), "phrases"), id << ": '" << a << "' is a continuation and carries no phrases");
        continue;
      }
      if (check == "phrases") {
        for (const auto& a : actions) {
          const Json* phrases = json::find(*entry_of(a), "phrases");
          CHECK_MESSAGE((phrases && !phrases->empty()), id << ": '" << a << "' carries no phrases");
        }
        continue;
      }
      REQUIRE_MESSAGE(c.contains("examples"), id << ": check route needs examples");
      for (const char* lang : {"pl", "en"}) {
        const std::string text = c.at("examples").value(lang, "");
        CHECK_MESSAGE(!text.empty(), id << ": an example in " << lang << " is required");
        const Json routed = unwrap(chat_route::route(*f.rt, Json{{"text", text}, {"conv_id", f.conv_id}}));
        const std::string kind = routed.value("kind", "");
        const std::string reached = kind == "command" ? routed.at("command").value("action", "") : "";
        CHECK_MESSAGE((std::find(actions.begin(), actions.end(), reached) != actions.end()),
                      id << " (" << lang << "): \"" << text << "\" reached " << (reached.empty() ? "[a message]" : reached) << ", not " << actions[0]);
      }
      // more sentences of the same capability (any of its actions)
      for (const char* lang : {"pl", "en"})
        for (const auto& more : c.value("examples_more", Json::object()).value(lang, Json::array())) {
          const std::string text = more.get<std::string>();
          const Json routed = unwrap(chat_route::route(*f.rt, Json{{"text", text}, {"conv_id", f.conv_id}}));
          const std::string reached = routed.value("kind", "") == "command" ? routed.at("command").value("action", "") : "";
          CHECK_MESSAGE((std::find(actions.begin(), actions.end(), reached) != actions.end()),
                        id << " (" << lang << "): \"" << text << "\" reached " << (reached.empty() ? "[a message]" : reached));
        }
    }
    // An open entry whose planned action already works is stale: it belongs under capabilities.
    for (const auto& o : coverage.at("open"))
      if (o.contains("planned_action")) {
        const Json* entry = entry_of(o.at("planned_action").get<std::string>());
        CHECK_MESSAGE((!entry || !f.rt->actions().runnable(*entry)), o.at("id").get<std::string>() << " is open but its action works: move it to capabilities");
      }
  }
  TEST_CASE("W7 (OKNA §6.1-6.2): each window and layout sentence reaches its own action with its arguments") {
    Fixture f;
    struct Case { const char* text; const char* action; std::vector<std::pair<const char*, const char*>> args; };
    const std::vector<Case> cases = {
        {"zmaksymalizuj ustawienia", "window.maximize", {{"target", "ustawienia"}}},
        {"zmaksymalizuj", "window.maximize", {}},
        {"maximize the settings", "window.maximize", {{"target", "settings"}}},
        {"zadokuj graf po lewej", "window.dock", {{"target", "graf"}, {"side", "lewej"}}},
        {"przypnij graf do prawej", "window.dock", {{"target", "graf"}, {"side", "prawej"}}},
        {"dock the graph on the left", "window.dock", {{"target", "graph"}, {"side", "left"}}},
        {"przywróć okno graf", "window.restore", {{"target", "graf"}}},
        {"restore the graph window", "window.restore", {{"target", "graph"}}},
        {"zminimalizuj graf", "window.minimize", {{"target", "graf"}}},
        {"minimize the graph", "window.minimize", {{"target", "graph"}}},
        {"odepnij graf", "window.float", {{"target", "graf"}}},
        {"pokaż ustawienia w pływającym oknie", "window.float", {{"target", "ustawienia"}}},
        {"float the graph", "window.float", {{"target", "graph"}}},
        {"dodaj graf jako zakładkę", "window.tab", {{"target", "graf"}}},
        {"put the graph in a tab", "window.tab", {{"target", "graph"}}},
        {"przesuń graf w prawo o 100", "window.move", {{"target", "graf"}, {"direction", "prawo"}, {"amount", "100"}}},
        {"move the graph left", "window.move", {{"target", "graph"}, {"direction", "left"}}},
        {"zmień rozmiar okna graf na 600x400", "window.resize", {{"target", "graf"}, {"w", "600"}, {"h", "400"}}},
        {"powiększ okno graf", "window.resize", {{"target", "graf"}, {"how", "powiększ"}}},
        {"resize the graph to 600x400", "window.resize", {{"target", "graph"}, {"w", "600"}, {"h", "400"}}},
        {"powiąż przybliżenie obu grafów", "window.bind", {{"param", "przybliżenie"}, {"target", "grafów"}}},
        {"link the zoom of both graphs", "window.bind", {{"param", "zoom"}, {"target", "graphs"}}},
        {"rozwiąż przybliżenie obu grafów", "window.unbind", {{"param", "przybliżenie"}, {"target", "grafów"}}},
        {"unlink the zoom of both graphs", "window.unbind", {{"param", "zoom"}, {"target", "graphs"}}},
        {"zmień nazwę układu praca na dom", "layout.rename", {{"name", "praca"}, {"label", "dom"}}},
        {"rename the layout work to home", "layout.rename", {{"name", "work"}, {"label", "home"}}},
        {"usuń układ praca", "layout.delete", {{"name", "praca"}}},
        {"delete the layout work", "layout.delete", {{"name", "work"}}},
    };
    int checked = 0;
    for (const auto& c : cases) {
      const Json routed = unwrap(chat_route::route(*f.rt, Json{{"text", c.text}, {"conv_id", f.conv_id}}));
      const std::string reached = routed.value("kind", "") == "command" ? routed.at("command").value("action", "") : "";
      CHECK_MESSAGE(reached == c.action, "\"" << c.text << "\" reached " << (reached.empty() ? "[a message]" : reached) << ", not " << c.action);
      if (reached != c.action) continue;
      const Json args = routed.at("command").value("args", Json::object());
      for (const auto& [k, v] : c.args) CHECK_MESSAGE(args.value(k, "") == std::string(v), "\"" << c.text << "\": " << k << " = " << args.value(k, "") << ", not " << v);
      ++checked;
    }
    CHECK(checked == static_cast<int>(cases.size()));
  }
}
