// P25: graph views as data from the conversation. The sentences (Polish and English) for the effective view, perspectives side by side, the scope
// resolution ("where does this setting come from"), edge kinds and the write into the user's overlay are catalogue actions: each is read as the
// action, "yes" runs it, and a write waits for the confirmation like every other writing action.
#include <doctest/doctest.h>

#include <filesystem>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/effective_view.h"
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
}
