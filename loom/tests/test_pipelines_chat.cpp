// P17 4: the long pipelines from the conversation. "Analyse this archive", "build knowledge from ...", "scan this catalogue ..." are proposals that say what
// will run and what the model use is (none by default: local and free; llm auto: not known before it runs, and under the guard and the spending limits);
// "yes" starts the pipeline in the background; "how is it going" and "stop it" are sentences too. The same over the API, with the receipt.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_pipelines.h"
#include "loom/action_runner.h"
#include "loom/archive.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Fixture {
  fsutil::TempDir td;
  fsutil::TempDir sources;
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
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    NewMessage m;
    m.conv_id = conv_id;
    m.role = "user";
    m.text = "We should ship on Friday.";
    unwrap(rt->db().create_msg(m));
    write("notes/plan.md", "# Atlas plan\n\nWe decided to ship the Atlas release on Friday. The open question is the migration of the old archive.\n");
    write("notes/ideas.txt", "Idea: build the Atlas importer first.\nDecision: keep the data in one folder.\nBug: the importer drops accents.\n");
  }
  void write(const std::string& relative, const std::string& text) {
    const auto path = sources.path() / relative;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
  }
  std::string folder() const { return (sources.path() / "notes").string(); }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  Json meta(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->metadata; }
  std::string text_of(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->text; }
  // The sentence is read as `action` and waits as a proposal; "yes" runs it.
  chat_route::Outcome say_and_confirm(const std::string& sentence, const std::string& action, const std::string& yes = "yes") {
    const auto proposal = input(sentence);
    REQUIRE_MESSAGE(proposal.handled, sentence);
    const Json p = meta(proposal).at("action_proposal");
    CHECK_MESSAGE(p.at("action") == action, sentence << " -> " << p.value("action", ""));
    CHECK_MESSAGE(p.at("status") == "pending", sentence);
    return input(yes);
  }
  Json proposal_of(const chat_route::Outcome& o) { return meta(o).at("action_proposal"); }
};

}  // namespace

TEST_SUITE("pipelines_chat") {
  TEST_CASE("an archive analysis is proposed with what runs and no model use, started by \"yes\", asked about and stopped in words") {
    Fixture f;
    const auto proposal = f.input("analyse this folder " + f.folder() + " as Atlas with seeds Atlas, importer and decision");
    REQUIRE(proposal.handled);
    const Json p = f.proposal_of(proposal);
    CHECK(p.at("action") == "archive.run");
    CHECK(p.at("status") == "pending");
    CHECK(p.at("args").at("path") == f.folder());
    CHECK(p.at("args").at("project") == "Atlas");
    CHECK(p.at("args").at("seeds") == "Atlas, importer and decision");
    CHECK(p.at("cost").at("model_calls") == 0);
    CHECK(p.at("cost").at("pipeline_model_use") == false);
    CHECK(p.at("cost").at("money").at("known") == true);  // a known zero: no model is called
    CHECK_FALSE(p.value("receipt_id", "").empty());
    // nothing runs before the yes
    CHECK(f.rt->actions().pipelines().status("archive").at("state") == "idle");
    const auto started = f.input("yes");
    REQUIRE(started.handled);
    const std::string said = f.text_of(started);
    CHECK(said.find("Started the archive analysis of " + f.folder() + " as \"Atlas\"") != std::string::npos);
    CHECK(said.find("No model is used (llm off)") != std::string::npos);
    const Json config = f.rt->actions().pipelines().status("archive").at("config");
    CHECK(config.at("sources") == Json::array({f.folder()}));
    CHECK(config.at("project") == "Atlas");
    CHECK(config.at("seed_terms") == Json::array({"Atlas", "importer", "decision"}));
    CHECK_FALSE(config.contains("llm"));  // not said: the pipeline's own preset (off)
    f.rt->actions().pipelines().wait("archive");

    const auto asked = f.say_and_confirm("how is the archive analysis going?", "archive.status");
    const std::string status = f.text_of(asked);
    INFO(status);
    CHECK(status.find("archive analysis: ") != std::string::npos);
    const std::string state = f.rt->actions().pipelines().status("archive").at("state");
    CHECK((state == "done" || state == "failed"));
    CHECK(status.find("the latest archive run in the data") != std::string::npos);
    // a stop with nothing running says so
    const auto stopped = f.say_and_confirm("stop the archive analysis", "archive.cancel");
    CHECK(f.text_of(stopped).find("No archive analysis is running") != std::string::npos);
  }

  TEST_CASE("the same in Polish; a sentence about a message is still a message") {
    Fixture f;
    const auto started = f.say_and_confirm("przeanalizuj ten folder " + f.folder() + " jako Atlas", "archive.run", "tak");
    const std::string said = f.text_of(started);
    CHECK(said.find("Rozpoczęto analizę archiwum " + f.folder() + " jako „Atlas”") != std::string::npos);
    CHECK(said.find("llm wyłączone") != std::string::npos);
    f.rt->actions().pipelines().wait("archive");
    const auto asked = f.say_and_confirm("jak idzie analiza archiwum?", "archive.status", "tak");
    CHECK(f.text_of(asked).find("analiza archiwum: ") != std::string::npos);
    // a command nothing understands is the offer to define it (item 1), never the pipeline: no archive was named
    const auto offer = f.input("przeanalizuj tę wiadomość");
    CHECK((!offer.handled || f.proposal_of(offer).at("action") == "actions.define"));
    const auto stopped = f.say_and_confirm("przerwij analizę archiwum", "archive.cancel", "tak");
    CHECK(f.text_of(stopped).find("Nic takiego nie trwa") != std::string::npos);
  }

  TEST_CASE("the model use is said before: llm auto is not free and not known, under the spending limits; llm off is a known zero") {
    Fixture f;
    const Json off = unwrap(f.rt->actions().preview(Json{{"action", "archive.run"}, {"element", "application"}, {"args", Json{{"path", f.folder()}}}}));
    CHECK(off.at("plan").at("cost").at("pipeline_model_use") == false);
    CHECK(off.at("estimate").at("money").at("known") == true);
    const Json on = unwrap(f.rt->actions().preview(Json{{"action", "archive.run"}, {"element", "application"}, {"args", Json{{"path", f.folder()}, {"llm", "auto"}}}}));
    CHECK(on.at("plan").at("cost").at("pipeline_model_use") == true);
    CHECK(on.at("estimate").at("money").at("known") == false);
    CHECK(on.at("estimate").at("money").at("reason_code") == "pipeline_model_use");
    // in the proposal the sentence says it
    const auto proposal = f.input("analyse this folder " + f.folder() + " as Atlas");
    CHECK(f.proposal_of(proposal).at("cost").at("pipeline_model_use") == false);
    f.input("no");
    // the user's spending limit applies to a pipeline that may use the model, and not to the local one
    {
      auto policy = unwrap(UsagePolicy::open(f.td.path() / "usage-policy.sqlite", Json::object()));
      unwrap(policy->request(Json{{"operation_id", "op1"}, {"baseline_key", "k"}, {"model", "m"}, {"resources", Json{{"cost_usd", nullptr}}}}));
      unwrap(policy->complete("op1", Json{{"resources", Json{{"cost_usd", 0.5}}}, {"provenance", "provider_reported"}}));
    }
    f.rt->config().set("usage_spend_limits", Json::array({Json{{"period", "day"}, {"amount", 0.1}, {"currency", "USD"}, {"on_exceed", "deny"}}}));
    const Json denied = unwrap(f.rt->actions().run(Json{{"action", "archive.run"}, {"element", "application"}, {"args", Json{{"path", f.folder()}, {"llm", "auto"}}}, {"ref", "test"}}));
    CHECK(denied.at("status") == "denied");
    CHECK(f.rt->actions().pipelines().status("archive").at("state") == "idle");  // nothing started
    const Json local = unwrap(f.rt->actions().preview(Json{{"action", "archive.run"}, {"element", "application"}, {"args", Json{{"path", f.folder()}}}}));
    CHECK(local.at("denied").empty());
  }

  TEST_CASE("over the API a pipeline is proposed first, and starts with the receipt of that proposal") {
    Fixture f;
    const Json request{{"action", "knowledge.run"}, {"element", "application"}, {"args", Json{{"path", f.folder()}, {"project", "Atlas"}}}, {"ref", "test"}};
    const Json preview = unwrap(f.rt->actions().preview(request));
    CHECK(preview.at("runnable") == true);
    CHECK(preview.at("estimate").at("money").at("known") == true);  // llm off: no model is called
    Json propose = request;
    propose["propose"] = true;
    const Json proposed = unwrap(f.rt->actions().run(propose));
    CHECK(proposed.at("status") == "proposed");
    CHECK(f.rt->actions().pipelines().status("knowledge").at("state") == "idle");  // nothing runs before the yes
    const std::string run_id = proposed.at("id");
    const std::string receipt = proposed.at("pending").at("receipt_id");
    // another receipt is not the yes of someone who saw this proposal
    CHECK_FALSE(f.rt->actions().confirm(Json{{"run_id", run_id}, {"approved", true}, {"receipt_id", "arr_other"}, {"ref", "test"}}));
    CHECK(f.rt->actions().pipelines().status("knowledge").at("state") == "idle");
    const Json done = unwrap(f.rt->actions().confirm(Json{{"run_id", run_id}, {"approved", true}, {"receipt_id", receipt}, {"ref", "test"}}));
    CHECK(done.at("status") == "completed");
    f.rt->actions().pipelines().wait("knowledge");
    const Json st = f.rt->actions().pipelines().status("knowledge");
    CHECK(st.at("config").at("sources") == Json::array({f.folder()}));
    CHECK(st.at("config").at("project") == "Atlas");
    const std::string state = st.at("state");
    CHECK((state == "done" || state == "failed" || state == "paused"));
  }

  TEST_CASE("a catalogue is scanned in the background and searched in words; nothing is imported") {
    Fixture f;
    const auto started = f.say_and_confirm("scan this catalogue " + f.folder(), "catalog.scan");
    CHECK(f.text_of(started).find("Started the scan of the catalogue") != std::string::npos);
    f.rt->actions().pipelines().wait("catalog");
    const auto asked = f.say_and_confirm("how is the catalogue scan going", "catalog.status");
    INFO(f.text_of(asked));
    CHECK(f.text_of(asked).find("catalogue scan: ") != std::string::npos);
    CHECK(f.text_of(asked).find("the catalogue holds") != std::string::npos);
    const auto found = f.say_and_confirm("search the catalogue for Atlas", "catalog.query");
    CHECK(f.text_of(found).find("catalogue") != std::string::npos);
    // the conversations of the data are untouched: the units are only catalogued
    CHECK(unwrap(f.rt->db().list_convs(10)).size() == 1);
    const auto polish = f.say_and_confirm("szukaj w katalogu Atlas", "catalog.query", "tak");
    INFO(f.text_of(polish));
    CHECK(f.text_of(polish).find("atalog") != std::string::npos);
  }

  TEST_CASE("a pipeline needs somewhere to run: without a path and without a device that picks a file the proposal says so") {
    Fixture f;
    for (const char* sentence : {"analyse this archive", "przeanalizuj to archiwum", "build knowledge from this archive", "przejrzyj ten katalog"}) {
      const auto proposal = f.input(sentence);
      REQUIRE_MESSAGE(proposal.handled, sentence);
      CHECK_MESSAGE(f.proposal_of(proposal).at("status") == "unavailable", sentence);
    }
  }

  TEST_CASE("the first free job per kind: a second start while one runs is Busy; a wrong key is refused before anything runs") {
    Fixture f;
    auto& jobs = f.rt->actions().pipelines();
    CHECK_FALSE(jobs.start("archive", Json{{"sources", Json::array({f.folder()})}, {"bogus", 1}}));
    CHECK(jobs.status("archive").at("state") == "idle");
    CHECK_FALSE(jobs.start("nothing", Json::object()));
    CHECK_FALSE(jobs.cancel("archive"));  // none runs
    unwrap(jobs.start("catalog", Json{{"sources", Json::array({f.folder()})}}));
    jobs.wait("catalog");
    // started again after it ended: allowed
    unwrap(jobs.start("catalog", Json{{"sources", Json::array({f.folder()})}}));
    jobs.wait("catalog");
    CHECK(jobs.status("catalog").at("state") == "done");
  }
}
