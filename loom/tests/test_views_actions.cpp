// P25: graph views as data from the conversation. The sentences (Polish and English) for the effective view, perspectives side by side, the scope
// resolution ("where does this setting come from"), edge kinds and the write into the user's overlay are catalogue actions: each is read as the
// action, "yes" runs it, and a write waits for the confirmation like every other writing action.
#include <doctest/doctest.h>

#include <filesystem>

#include "loom/action_runner.h"
#include "loom/advisory.h"
#include "loom/view_engine.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/effective_view.h"
#include "loom/interaction.h"
#include "loom/perspectives.h"
#include "loom/net/http.h"
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
  std::string conv_id;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
    conv_id = unwrap(rt->db().create_conv("Views")).id;
  }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  Json proposal_of(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->metadata.at("action_proposal"); }
  std::string text_of(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->text; }
  chat_route::Outcome say_and_confirm(const std::string& sentence, const std::string& action, const std::string& yes = "yes") {
    const auto proposal = input(sentence);
    REQUIRE_MESSAGE(proposal.handled, sentence);
    const Json p = proposal_of(proposal);
    CHECK_MESSAGE(p.at("action") == action, sentence << " -> " << p.value("action", ""));
    CHECK_MESSAGE(p.at("status") == "pending", sentence);
    return input(yes);
  }
};

}  // namespace

TEST_SUITE("views_actions") {
  TEST_CASE("the reads are sentences in both languages: effective view, perspectives, scope resolution, edge kinds") {
    Fixture f;
    const auto view = f.say_and_confirm("show the effective view of edge kinds", "views.effective");
    INFO(f.text_of(view));
    CHECK(f.text_of(view).find("edge_kinds") != std::string::npos);
    const auto view_pl = f.say_and_confirm("pokaż widok efektywny rodzajów krawędzi", "views.effective", "tak");
    INFO(f.text_of(view_pl));
    CHECK(f.text_of(view_pl).find("edge_kinds") != std::string::npos);

    const auto list = f.say_and_confirm("show perspectives", "perspectives.show");
    INFO(f.text_of(list));
    CHECK(f.text_of(list).find("Perspectives") != std::string::npos);

    const auto scope = f.say_and_confirm("where does temperature come from", "scopes.explain");
    INFO(f.text_of(scope));
    CHECK(f.text_of(scope).find("temperature") != std::string::npos);
    const auto scope_pl = f.say_and_confirm("skąd jest ustawienie temperature", "scopes.explain", "tak");
    INFO(f.text_of(scope_pl));
    CHECK(f.text_of(scope_pl).find("temperature") != std::string::npos);

    const auto kinds = f.say_and_confirm("show edge kinds", "edge_kinds.list");
    CHECK(f.text_of(kinds).find("Edge kinds") != std::string::npos);
    const auto kinds_pl = f.say_and_confirm("pokaż rodzaje krawędzi", "edge_kinds.list", "tak");
    CHECK(f.text_of(kinds_pl).find("Rodzaje krawędzi") != std::string::npos);
    const auto classified = f.say_and_confirm("classify the link types", "edge_kinds.classify");
    INFO(f.text_of(classified));
    CHECK(!f.text_of(classified).empty());
  }

  TEST_CASE("perspectives are shown side by side from the ids in the sentence") {
    Fixture f;
    auto persp = unwrap(views::Perspectives::open(f.rt->paths().root));
    const Json list = persp.list();
    REQUIRE(list.size() >= 2);
    const std::string a = list[0].at("id").get<std::string>(), b = list[1].at("id").get<std::string>();
    const auto shown = f.say_and_confirm("show perspectives " + a + " and " + b + " side by side", "perspectives.show");
    INFO(f.text_of(shown));
    CHECK(f.text_of(shown).find("side by side") != std::string::npos);
    CHECK(f.text_of(shown).find(a) != std::string::npos);
    CHECK(f.text_of(shown).find(b) != std::string::npos);
    const auto shown_pl = f.say_and_confirm("pokaż perspektywy " + a + " i " + b + " obok siebie", "perspectives.show", "tak");
    CHECK(f.text_of(shown_pl).find("obok siebie") != std::string::npos);
  }

  TEST_CASE("a write to the overlay waits for the confirmation, goes only to the user's overlay and can be seen in the effective view") {
    Fixture f;
    const auto overlay = f.td.path() / "views" / "user.json";
    CHECK(!std::filesystem::exists(overlay));
    const auto proposal = f.input("hide edge kind mentions");
    REQUIRE(proposal.handled);
    const Json p = f.proposal_of(proposal);
    CHECK(p.at("action") == "views.write");
    CHECK(p.at("status") == "pending");
    CHECK(!std::filesystem::exists(overlay));  // nothing is written before "yes"
    const auto done = f.input("yes");
    INFO(f.text_of(done));
    CHECK(std::filesystem::exists(overlay));
    const auto eff = unwrap(views::effective(f.rt->paths().root, "edge_kinds"));
    bool hidden = false;
    for (const auto& e : eff.hidden)
      if (e.value("id", "") == "mentions") hidden = true;
    CHECK(hidden);
    CHECK(f.text_of(done).find("mentions") != std::string::npos);
  }

  TEST_CASE("P28: a view declaration is listed and evaluated around a focus from a sentence (both languages)") {
    Fixture f;
    const std::string v1 = unwrap(f.rt->db().create_node("main.c", "file")), v2 = unwrap(f.rt->db().create_node("util.c", "file"));
    unwrap(f.rt->db().create_link(v2, v1, "previous_version"));
    const auto list = f.say_and_confirm("show view declarations", "view_model.show");
    INFO(f.text_of(list));
    CHECK(f.text_of(list).find("history_cone") != std::string::npos);
    const auto shown = f.say_and_confirm("show view history_cone around main.c", "view_model.show");
    INFO(f.text_of(shown));
    CHECK(f.text_of(shown).find("history_cone") != std::string::npos);
    CHECK(f.text_of(shown).find("2 elements") != std::string::npos);
    const auto shown_pl = f.say_and_confirm("pokaż widok history_cone wokół main.c", "view_model.show", "tak");
    INFO(f.text_of(shown_pl));
    CHECK(f.text_of(shown_pl).find("2 elementów") != std::string::npos);
    const auto none = f.say_and_confirm("show view history_cone around nothing_here", "view_model.show");
    INFO(f.text_of(none));
    CHECK(f.text_of(none).find("nothing_here") != std::string::npos);
  }

  TEST_CASE("P28 step 2: interaction bindings are listed in the chat and rebound through the confirmed overlay write") {
    Fixture f;
    const auto shown = f.say_and_confirm("show keyboard bindings", "interaction.show");
    INFO(f.text_of(shown));
    CHECK(f.text_of(shown).find("kbd.plane.left") != std::string::npos);
    CHECK(f.text_of(shown).find("kbd.lock.caps") != std::string::npos);
    const auto shown_pl = f.say_and_confirm("pokaż przypisania dotyku", "interaction.show", "tak");
    INFO(f.text_of(shown_pl));
    CHECK(f.text_of(shown_pl).find("touch.hold.shift") != std::string::npos);
    const auto all = f.say_and_confirm("show the input bindings", "interaction.show");
    CHECK(f.text_of(all).find("xr.focus.gaze") != std::string::npos);
    CHECK(f.text_of(all).find("recorded as data") != std::string::npos);
    // a lock to zoom: written to the user's overlay (confirmed like views.write), the preset itself is untouched
    const auto rebound = f.say_and_confirm("bind capslock to zoom", "interaction.rebind");
    INFO(f.text_of(rebound));
    CHECK(f.text_of(rebound).find("kbd.lock.caps") != std::string::npos);
    auto eff = unwrap(views::Interaction::open(f.rt->paths().root));
    const Json r = eff.resolve(Json{{"device", "keyboard"}});
    for (const auto& b : r["bindings"])
      if (b["id"] == "kbd.lock.caps") {
        CHECK(b["action"]["kind"] == "zoom");
        CHECK(!b["action"].contains("declaration"));
        CHECK(b["origin"] == "rom");
        CHECK(b["provenance"].size() == 2);
      }
    const auto missing = f.say_and_confirm("bind nonsense to zoom", "interaction.rebind");
    CHECK(f.text_of(missing).find("nonsense") != std::string::npos);
  }

  TEST_CASE("G3b: an advisory is answered through the action runner - keep is recorded and asks again, dont_ask is remembered, forget reverses it; the policy is a preset the person edits") {
    Fixture f;
    auto run = [&](const std::string& action, const Json& args) {
      const Json r = unwrap(f.rt->actions().run(Json{{"action", action}, {"element", Json{{"class", "application"}}}, {"args", args}}));
      INFO(action << " " << r.dump());
      CHECK(r.at("status") == "completed");
      return r;
    };
    const std::string fp = advisory::fingerprint(Json{{"encodings", Json::array({Json{{"id", "z"}, {"channel", "alpha"}}})}});
    const Json record{{"code", "nothing_visible"}, {"ask", Json::object()}};
    auto state = [&]() {
      const auto store = advisory::AckStore::open(f.td.path());
      const auto eng = unwrap(views::ViewEngine::open(f.td.path()));
      return store.annotate(record, fp, eng.advisory_preset())["ask"];
    };
    CHECK(state()["state"] == "ask");
    // keep: recorded (shown) but asks again; no confirmation is needed (reversible, writes only the person's answers)
    run("advisory.answer", Json{{"code", "nothing_visible"}, {"fingerprint", fp}, {"answer", "keep"}, {"context", Json{{"view", "test"}}}});
    Json s = state();
    CHECK(s["asking"] == true);
    CHECK(s["ack"]["choice"] == "keep");
    CHECK(s["ack"]["context"]["view"] == "test");
    CHECK(s["answered"] == true);
    // keep_dont_ask is an alias of the preset choice dont_ask: remembered for this code + fingerprint
    const Json r = run("advisory.answer", Json{{"code", "nothing_visible"}, {"fingerprint", fp}, {"answer", "keep_dont_ask"}});
    CHECK(r["result"]["data"]["answer"] == "dont_ask");
    s = state();
    CHECK(s["asking"] == false);
    CHECK(s["state"] == "acknowledged");
    CHECK(s["ack"]["choice"] == "dont_ask");
    // the list (settings view): the answer, the person's policy, the preset and the effective policy
    const Json list = run("advisory.list", Json::object());
    const Json data = list["result"]["data"];
    REQUIRE(data["acks"].size() == 1);
    CHECK(data["acks"][0]["fingerprint"] == fp);
    CHECK(data["preset"]["transient_choices"] == Json::array({"keep"}));
    CHECK(data["effective"]["ask"] == true);
    // forget = the reversal: the advisory asks again
    const Json forgot = run("advisory.answer", Json{{"code", "nothing_visible"}, {"fingerprint", fp}, {"answer", "forget"}});
    CHECK(forgot["result"]["data"]["removed"] == 1);
    s = state();
    CHECK(s["state"] == "ask");
    CHECK_FALSE(s.contains("ack"));
    CHECK(run("advisory.answer", Json{{"code", "nothing_visible"}, {"fingerprint", fp}, {"answer", "forget"}})["result"]["data"]["removed"] == 0);
    // without a fingerprint the answer is about the code in every configuration (policy codes.<code>.ask false) and forget puts the preset back
    run("advisory.answer", Json{{"code", "nothing_visible"}, {"answer", "dont_ask"}});
    CHECK(state()["state"] == "muted");
    run("advisory.answer", Json{{"code", "nothing_visible"}, {"answer", "forget"}});
    CHECK(state()["state"] == "ask");
    // the policy: remember_days (a number said with words around it, or "forever"), ask, reset back to the preset
    run("advisory.policy", Json{{"remember_days", "30 dni"}});
    CHECK(advisory::AckStore::open(f.td.path()).document()["policy"]["remember_days"] == 30.0);
    CHECK(state()["policy"]["remember_days"] == 30.0);
    run("advisory.policy", Json{{"remember_days", "zawsze"}});
    CHECK_FALSE(advisory::AckStore::open(f.td.path()).document()["policy"].contains("remember_days"));
    run("advisory.policy", Json{{"ask", "nie"}});
    CHECK(state()["state"] == "muted");
    run("advisory.policy", Json{{"ask", false}, {"code", "marks_identical"}});
    CHECK(advisory::AckStore::open(f.td.path()).document()["policy"]["codes"]["marks_identical"]["ask"] == false);
    run("advisory.policy", Json{{"reset", true}});
    CHECK(state()["state"] == "ask");
    const Json bad = unwrap(f.rt->actions().run(Json{{"action", "advisory.policy"}, {"element", Json{{"class", "application"}}}, {"args", Json{{"remember_days", "kiedyś"}}}}));
    CHECK(bad.at("status") == "failed");
    const Json no_code = unwrap(f.rt->actions().run(Json{{"action", "advisory.answer"}, {"element", Json{{"class", "application"}}}, {"args", Json{{"answer", "keep"}}}}));
    CHECK(no_code.at("status") == "failed");

    // the sentences (Polish and English) reach the same actions
    const auto pl = f.say_and_confirm("nie pytaj więcej o nothing_visible", "advisory.answer", "tak");
    INFO(f.text_of(pl));
    CHECK(f.text_of(pl).find("nothing_visible") != std::string::npos);
    CHECK(state()["state"] == "muted");
    f.say_and_confirm("don't ask again about marks_identical", "advisory.answer");
    const auto shown = f.say_and_confirm("pokaż zapamiętane odpowiedzi na podpowiedzi", "advisory.list", "tak");
    INFO(f.text_of(shown));
    const auto days = f.say_and_confirm("remember advisory answers for 30 days", "advisory.policy");
    INFO(f.text_of(days));
    CHECK(f.text_of(days).find("30") != std::string::npos);
  }
}
