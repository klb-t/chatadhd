// P13 1-2: a client declares which device capabilities it can carry out right now; the device actions of the catalogue are runnable for
// THAT client (and planned, with their reason, for everyone else); a plan step that is such an action pauses the run with a client_step
// receipt, durable like the confirmation pauses; the client carries it out and answers with client_result, which continues the chain;
// the results land in the graph (the recording is kept as an audio form derived from the device, the transcript derives from it).
#include <doctest/doctest.h>

#include <fstream>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/client_devices.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/media_providers.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::actions::ClientRegistry;
using loom::test::unwrap;

namespace {

const char* kGroq = "https://api.groq.com/openai/v1/audio/transcriptions";

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;

  Fixture() { open(); }
  void open() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("model_registry_autorefresh", "off");  // these tests count the transport's requests; fetching the model list for a price is tested in test_estimate_registry
    rt->secrets().set("groq_api_key", "gk_test");
    rt->media().refresh();
  }
  void restart() {
    rt.reset();
    open();
  }
  void expect_asr(const std::string& text) {
    transport->expect("POST", kGroq, net::ScriptedTransport::Reply::json(200, Json{{"text", text}, {"language", "en"}, {"duration", 2.5}}));
  }
  void expect_chat(const std::string& text) {
    transport->expect("POST", "https://api.test/chat/completions",
                      net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", text}}}}})},
                                                                     {"usage", Json{{"total_tokens", 7}}}}));
  }
  Json declare(const Json& capabilities, const std::string& id = "web-1", const char* kind = "web") {
    return unwrap(rt->actions().invoke("client.declare", Json{{"client_id", id}, {"client", Json{{"kind", kind}, {"label", "test client"}}}, {"capabilities", capabilities}}));
  }
  Json for_element(const std::string& element, const std::string& client = "") {
    Json r{{"element", element}};
    if (!client.empty()) r["client_id"] = client;
    return unwrap(rt->actions().invoke("for", r));
  }
  static Json row(const Json& rows, const std::string& action) {
    for (const auto& r : rows.at("actions"))
      if (r.at("action") == action) return r;
    return Json();
  }
  Json chain_to_text(const std::string& client) {
    const Json proposal = unwrap(rt->actions().invoke("propose_chain", Json{{"element", "device:microphone"}, {"target_trait", "has_text"}, {"client_id", client}}));
    REQUIRE(proposal.at("chains").size() >= 1);
    return proposal.at("chains").at(0);
  }
  std::string audio_b64() { return base64::encode("AUDIO"); }
  Json answer_audio(const Json& run, const std::string& state = "ok") {
    Json a{{"run_id", run.at("id")}, {"receipt_id", run.at("pending").at("receipt_id")}, {"state", state}, {"ref", "web:web-1"}};
    if (state == "ok") a["parts"] = Json::array({Json{{"kind", "audio"}, {"mime", "audio/mp4"}, {"data_base64", audio_b64()}}});
    return a;
  }
  // The links that start at the node.
  std::vector<Link> links(const std::string& node, const std::string& type) {
    std::vector<Link> out;
    for (auto& l : unwrap(rt->db().get_links(node, type)))
      if (l.src == node) out.push_back(std::move(l));
    return out;
  }
  void overlay(const std::string& domain, const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(td.path() / "profiles" / (domain + ".pack"))
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
  }
};

}  // namespace

TEST_SUITE("client_devices") {
  TEST_CASE("a client declares its capabilities with their permission state; the declaration is kept for that client only") {
    Fixture f;
    const Json web = f.declare(Json{{"captures_audio", "prompt"}, {"plays_audio", "granted"}, {"clipboard", "granted"}, {"shows_notifications", "denied"}});
    CHECK(web.at("schema") == "loom.client_declaration/1");
    CHECK(web.at("client_id") == "web-1");
    CHECK(web.at("client").at("kind") == "web");
    CHECK(web.at("capabilities").at("captures_audio").at("permission") == "prompt");
    CHECK(web.at("capabilities").at("captures_audio").at("runnable") == true);
    CHECK(web.at("capabilities").at("captures_audio").at("actions") == Json::array({"device.record_audio"}));
    CHECK(web.at("capabilities").contains("clipboard_read"));  // the alias `clipboard` stands for read and write
    CHECK(web.at("capabilities").contains("clipboard_write"));
    CHECK(web.at("capabilities").at("shows_notifications").at("runnable") == false);  // denied on the device: declared, but not runnable
    CHECK(web.at("refused").at("shows_notifications") == "denied");
    const Json runnable = web.at("runnable_actions");
    CHECK(std::find(runnable.begin(), runnable.end(), "device.record_audio") != runnable.end());
    CHECK(std::find(runnable.begin(), runnable.end(), "device.notify") == runnable.end());

    // another client, another declaration; the first is not touched; a list of names means granted
    const Json android = f.declare(Json::array({"captures_image", "picks_files"}), "android-1", "android");
    CHECK(android.at("capabilities").at("captures_image").at("permission") == "granted");
    const Json again = unwrap(f.rt->actions().invoke("client.capabilities", Json{{"client_id", "web-1"}}));
    CHECK(again.at("known") == true);
    CHECK(again.at("capabilities").contains("captures_audio"));
    CHECK_FALSE(again.at("capabilities").contains("captures_image"));
    const Json listed = unwrap(f.rt->actions().invoke("client.capabilities", Json::object()));
    CHECK(listed.at("clients").size() == 2);

    // declaring again replaces the declaration; an unknown client says so
    f.declare(Json{{"plays_audio", "granted"}});
    CHECK_FALSE(unwrap(f.rt->actions().invoke("client.capabilities", Json{{"client_id", "web-1"}})).at("capabilities").contains("captures_audio"));
    const Json unknown = unwrap(f.rt->actions().invoke("client.capabilities", Json{{"client_id", "nobody"}}));
    CHECK(unknown.at("known") == false);
    CHECK(unknown.contains("message"));

    // a client that gives no id gets one; bad input is refused with the reason
    const Json anonymous = unwrap(f.rt->actions().invoke("client.declare", Json{{"capabilities", Json::array({"plays_audio"})}}));
    CHECK(anonymous.at("client_id").get<std::string>().rfind("cl_", 0) == 0);
    auto bad_state = f.rt->actions().invoke("client.declare", Json{{"capabilities", Json{{"captures_audio", "maybe"}}}});
    REQUIRE_FALSE(bad_state);
    CHECK(bad_state.error().message.find("granted") != std::string::npos);  // the known states are said
    CHECK_FALSE(f.rt->actions().invoke("client.declare", Json{{"client_id", "has space"}, {"capabilities", Json::array({"plays_audio"})}}));
    CHECK_FALSE(f.rt->actions().invoke("client.declare", Json::object()));
    // the declaration is not shipped data and not in the event log: nothing about it on disk
    CHECK_FALSE(std::filesystem::exists(f.td.path() / "profiles" / "client_devices.pack"));
  }

  TEST_CASE("a declaration expires with the time to live of the profile; a client that is gone is forgotten") {
    ClientRegistry registry;
    std::int64_t now = 1'000'000;
    registry.set_clock([&] { return now; });
    const auto profile = unwrap(RuntimeProfile::load("client_devices", {}));
    const auto catalogue = unwrap(RuntimeProfile::load("actions", {}));
    const Json actions = catalogue.values().at("actions");
    const Json declared = unwrap(registry.declare(Json{{"client_id", "tab-1"}, {"capabilities", Json::array({"plays_audio"})}}, profile.values(), actions));
    CHECK(declared.contains("expires"));
    CHECK_FALSE(registry.find("tab-1").is_null());
    now += 43200 * 1000 - 1;
    CHECK_FALSE(registry.find("tab-1").is_null());
    now += 2;
    CHECK(registry.find("tab-1").is_null());
    CHECK(registry.list().empty());
  }

  TEST_CASE("a device action is runnable for the client that declared it, planned (with its reason) for every other") {
    Fixture f;
    // before anyone declares: as before, planned, with the reason
    const Json none = f.for_element("device:microphone");
    CHECK(Fixture::row(none, "device.record_audio").at("callable") == false);
    CHECK(Fixture::row(none, "device.record_audio").at("planned_reason").at("en").get<std::string>().find("client") != std::string::npos);
    f.declare(Json{{"captures_audio", "granted"}});
    f.declare(Json{{"plays_audio", "granted"}}, "other");
    f.declare(Json{{"captures_audio", "denied"}}, "denied-1");
    f.declare(Json{{"captures_audio", "prompt"}}, "asks-1");

    const Json mine = f.for_element("device:microphone", "web-1");
    const Json record = Fixture::row(mine, "device.record_audio");
    CHECK(record.at("callable") == true);
    CHECK(record.at("implementation") == "implemented");
    CHECK(record.at("runs_on") == "client");
    CHECK(record.at("client_permission") == "granted");
    CHECK(record.at("runnable_note").at("en").get<std::string>().find("this device") != std::string::npos);  // "runnable on this device"
    CHECK_FALSE(record.contains("planned_reason"));
    CHECK(Fixture::row(f.for_element("device:microphone", "asks-1"), "device.record_audio").at("runnable_note").at("en").get<std::string>().find("permission") != std::string::npos);
    // the others: a client that declared something else, one that denied it, one that is not known, the server side
    for (const char* client : {"other", "denied-1", "never-declared", ""}) {
      const Json theirs = Fixture::row(f.for_element("device:microphone", client), "device.record_audio");
      INFO(client);
      CHECK(theirs.at("callable") == false);
      CHECK(theirs.contains("planned_reason"));
      CHECK_FALSE(theirs.contains("runs_on"));
    }
    CHECK(Fixture::row(f.for_element("device:microphone", "denied-1"), "device.record_audio").at("planned_reason").at("en").get<std::string>().find("denied") != std::string::npos);
    // an action the client did not declare stays planned for it
    CHECK(Fixture::row(f.for_element("device:camera", "web-1"), "device.capture_image").at("callable") == false);
    // explain and propose_chain see the same catalogue
    const Json explained = unwrap(f.rt->actions().invoke("explain", Json{{"element", "device:microphone"}, {"action", "device.record_audio"}, {"client_id", "web-1"}}));
    CHECK(explained.at("action").at("implementation") == "implemented");
    CHECK(explained.at("action").at("runs_on") == "client");
    const Json chain = unwrap(f.rt->actions().invoke("propose_chain", Json{{"element", "device:microphone"}, {"target_trait", "has_text"}, {"client_id", "web-1"}}));
    CHECK(chain.at("chains").at(0).at("executable") == true);
    CHECK(chain.at("chains").at(0).at("steps").at(0).at("action") == "device.record_audio");
    CHECK(unwrap(f.rt->actions().invoke("propose_chain", Json{{"element", "device:microphone"}, {"target_trait", "has_text"}})).at("chains").at(0).at("executable") == false);

    // preview: runnable for the client, with the capture confirmation the preset rule asks; not runnable for the server side
    const Json plan = f.chain_to_text("web-1");
    const Json preview = unwrap(f.rt->actions().preview(Json{{"chain", plan}, {"element", "device:microphone"}, {"client_id", "web-1"}}));
    CHECK(preview.at("runnable") == true);
    CHECK(preview.at("needs_confirmation").at(0).at("action") == "device.record_audio");
    CHECK(preview.at("needs_confirmation").at(0).at("initiator") == "application preset");
    const Json server = unwrap(f.rt->actions().preview(Json{{"chain", plan}, {"element", "device:microphone"}}));
    CHECK(server.at("runnable") == false);
    CHECK(server.at("not_runnable").at(0).at("action") == "device.record_audio");
    // a run for a client that is not known stays refused, as before
    auto refused = f.rt->actions().run(Json{{"chain", plan}, {"element", "device:microphone"}, {"client_id", "never-declared"}, {"confirmed", true}});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message.find("planned") != std::string::npos);
  }

  TEST_CASE("record -> transcribe: the run pauses on a client_step, the client answers, the chain continues, the graph has the recording and the transcript") {
    Fixture f;
    f.declare(Json{{"captures_audio", "granted"}});
    const Json plan = f.chain_to_text("web-1");
    // the preset rule asks for the confirmation of a capture; the run says so, then the client step follows
    const Json asked = unwrap(f.rt->actions().run(Json{{"chain", plan}, {"element", "device:microphone"}, {"client_id", "web-1"}, {"ref", "test"}}));
    CHECK(asked.at("status") == "awaiting_confirmation");
    CHECK(asked.at("pending").at("gates").at(0).at("action") == "device.record_audio");
    CHECK(asked.at("client").at("client_id") == "web-1");
    const Json paused = unwrap(f.rt->actions().confirm(Json{{"run_id", asked.at("id")}, {"approved", true}, {"receipt_id", asked.at("pending").at("receipt_id")}, {"ref", "test"}}));
    INFO(paused.value("error", Json(nullptr)).dump());
    REQUIRE(paused.at("status") == "awaiting_client");
    const Json pending = paused.at("pending");
    CHECK(pending.at("kind") == "client_step");
    CHECK(pending.at("action") == "device.record_audio");
    CHECK(pending.at("client_step").at("schema") == "loom.client_step/1");
    CHECK(pending.at("client_step").at("capability") == "captures_audio");
    CHECK(pending.at("client_step").at("permission") == "microphone");
    CHECK(pending.at("client_step").at("expects") == Json::array({"audio"}));
    CHECK(pending.at("client_step").at("prompt").at("pl").get<std::string>().find("mikrofon") != std::string::npos);
    CHECK(pending.at("receipt_id").get<std::string>().rfind("arr_", 0) == 0);
    CHECK(f.transport->requests().empty());  // nothing is sent while the device has not answered

    // a wrong receipt changes nothing; nor does a state the interpreter does not know
    Json wrong = f.answer_audio(paused);
    wrong["receipt_id"] = "arr_invented";
    auto refused = f.rt->actions().client_result(wrong);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == Errc::Conflict);
    CHECK(unwrap(f.rt->actions().status(Json{{"run_id", paused.at("id")}})).at("status") == "awaiting_client");
    CHECK_FALSE(f.rt->blobs().has(Sha256::hex("AUDIO")));  // nothing was stored for an answer that was refused

    // the client records and answers: the run goes on with the next step (the transcription) and completes
    f.expect_asr("Remember the milk.");
    const Json done = unwrap(f.rt->actions().client_result(f.answer_audio(paused)));
    INFO(done.value("error", Json(nullptr)).dump());
    REQUIRE(done.at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 1);
    CHECK(f.transport->requests()[0].url == kGroq);
    CHECK(f.transport->requests()[0].body.find("AUDIO") != std::string::npos);  // the bytes the client brought are what is sent
    CHECK(done.at("result").at("parts").at(0).at("text") == "Remember the milk.");
    CHECK(done.at("steps").at(0).at("status") == "ok");
    CHECK(done.at("steps").at(0).at("variants").at(0).at("provenance").at(0).at("schema") == "loom.client_result/1");
    CHECK(done.at("confirmations").at(1).at("kind") == "client_step");

    // the graph: the recording is an audio form derived from the device node, the transcript is a text form derived from the same device and
    // from the recording; both are produced by the run and express one content
    const Json& produced = done.at("result").at("produced");
    INFO(produced.at("issues").dump());
    REQUIRE(produced.at("items").size() == 2);
    const Json audio_item = produced.at("items").at(0), text_item = produced.at("items").at(1);
    CHECK(audio_item.at("class") == "audio_form");
    CHECK(audio_item.at("kept") == true);
    CHECK(text_item.at("class") == "text_form");
    const std::string audio_form = audio_item.at("form"), text_form = text_item.at("form");
    const auto audio_derived = f.links(audio_form, "derived_from");
    REQUIRE(audio_derived.size() == 1);
    const Node device = *unwrap(f.rt->db().get_node(audio_derived[0].dst));
    CHECK(device.label == "device:microphone");
    CHECK(device.kind == "microphone");
    std::vector<std::string> text_sources;
    for (const auto& l : f.links(text_form, "derived_from")) text_sources.push_back(l.dst);
    CHECK(std::find(text_sources.begin(), text_sources.end(), device.id) != text_sources.end());
    CHECK(std::find(text_sources.begin(), text_sources.end(), audio_form) != text_sources.end());
    const std::string run_node = produced.at("run_node");
    CHECK(f.links(audio_form, "produced_by").at(0).dst == run_node);
    CHECK(f.links(text_form, "produced_by").at(0).dst == run_node);
    CHECK(f.links(audio_form, "expresses").at(0).dst == produced.at("content").get<std::string>());
    const Node audio_node = *unwrap(f.rt->db().get_node(audio_form));
    CHECK(audio_node.kind == "audio_form");
    CHECK(audio_node.metadata.at("mime") == "audio/mp4");
    const std::string hash = audio_node.metadata.at("blob_hash");
    CHECK(unwrap(f.rt->blobs().read(hash)) == "AUDIO");  // the bytes are in the blob store, not in the run document
    CHECK(done.dump().find(f.audio_b64()) == std::string::npos);
    CHECK(unwrap(f.rt->actions().status(Json{{"run_id", done.at("id")}})).dump().find(f.audio_b64()) == std::string::npos);
    // the transcription made inside the run is a port run of the graph, part of that run (port_graph.h: ActionRunScope + reconcile), and the
    // runtime's own sink for media calls is still in place after the step
    const auto port_runs = unwrap(f.rt->db().list_nodes("port_run", 100));
    REQUIRE(port_runs.size() == 1);
    CHECK(port_runs[0].metadata.at("action_run_id") == done.at("id"));
    bool part_of_run = false;
    for (const auto& l : unwrap(f.rt->db().get_links(port_runs[0].id, "part_of"))) part_of_run = part_of_run || (l.src == port_runs[0].id && l.dst == run_node);
    CHECK(part_of_run);
    CHECK(static_cast<bool>(f.rt->media().provenance_sink()));
    // the content is the recording's own, per run (every recording is another content), not "the microphone"
    CHECK(unwrap(f.rt->db().get_node(produced.at("content").get<std::string>()))->label == "content:" + done.at("id").get<std::string>());
  }

  TEST_CASE("a chain goes on from a clipboard read: text comes back as text") {
    Fixture f;
    f.declare(Json{{"clipboard", "granted"}});
    const Json chain = unwrap(f.rt->actions().invoke("propose_chain", Json{{"element", "device:clipboard"}, {"target_action", "summarise"}, {"client_id", "web-1"}}));
    REQUIRE(chain.at("chains").size() >= 1);
    const Json run = unwrap(f.rt->actions().run(Json{{"chain", chain.at("chains").at(0)}, {"element", "device:clipboard"}, {"client_id", "web-1"}, {"confirmed", true}}));
    REQUIRE(run.at("status") == "awaiting_client");
    CHECK(run.at("pending").at("action") == "device.clipboard_read");
    f.expect_chat("A short version.");
    const Json done = unwrap(f.rt->actions().client_result(Json{{"run_id", run.at("id")}, {"receipt_id", run.at("pending").at("receipt_id")}, {"state", "ok"},
                                                                  {"parts", Json::array({Json{{"kind", "text"}, {"text", "A long text that was on the clipboard."}}})}}));
    INFO(done.value("error", Json(nullptr)).dump());
    REQUIRE(done.at("status") == "completed");
    CHECK(unwrap(json::parse(f.transport->requests().at(0).body)).at("messages").dump().find("A long text that was on the clipboard.") != std::string::npos);
    CHECK(done.at("result").at("parts").at(0).at("text") == "A short version.");
  }

  TEST_CASE("the pause is durable: the run waits across a restart, and a client that never answers leaves it paused, not failed") {
    Fixture f;
    f.declare(Json{{"captures_audio", "granted"}});
    const Json plan = f.chain_to_text("web-1");
    const Json run = unwrap(f.rt->actions().run(Json{{"chain", plan}, {"element", "device:microphone"}, {"client_id", "web-1"}, {"confirmed", true}}));
    REQUIRE(run.at("status") == "awaiting_client");
    const std::string id = run.at("id"), receipt = run.at("pending").at("receipt_id");
    f.restart();  // the declaration is gone with the process; the run is not
    const Json waiting = unwrap(f.rt->actions().status(Json{{"run_id", id}}));
    CHECK(waiting.at("status") == "awaiting_client");
    CHECK(waiting.at("pending").at("receipt_id") == receipt);
    CHECK(waiting.at("pending").at("client_step").at("action") == "device.record_audio");
    CHECK(waiting.at("live") == false);
    CHECK(unwrap(f.rt->actions().invoke("client.capabilities", Json{{"client_id", "web-1"}})).at("known") == false);
    const Json listed = unwrap(f.rt->actions().status(Json{{"list", true}, {"status", "awaiting_client"}}));
    CHECK(listed.at("runs").size() == 1);
    // the answer after the restart continues the run: the declaration is not needed for a step that is already planned
    f.expect_asr("After the restart.");
    const Json done = unwrap(f.rt->actions().client_result(Json{{"run_id", id}, {"receipt_id", receipt}, {"state", "ok"}, {"ref", "web:web-1"},
                                                                  {"parts", Json::array({Json{{"kind", "audio"}, {"mime", "audio/mp4"}, {"data_base64", f.audio_b64()}}})}}));
    REQUIRE(done.at("status") == "completed");
    CHECK(done.at("result").at("parts").at(0).at("text") == "After the restart.");
    // a second answer finds nothing waiting
    auto twice = f.rt->actions().client_result(Json{{"run_id", id}, {"receipt_id", receipt}, {"state", "ok"}});
    REQUIRE_FALSE(twice);
    CHECK(twice.error().code == Errc::Conflict);
  }

  TEST_CASE("a paused client step can be cancelled; the answer that comes later changes nothing") {
    Fixture f;
    f.declare(Json{{"captures_audio", "granted"}});
    const Json run = unwrap(f.rt->actions().run(Json{{"chain", f.chain_to_text("web-1")}, {"element", "device:microphone"}, {"client_id", "web-1"}, {"confirmed", true}}));
    REQUIRE(run.at("status") == "awaiting_client");
    const Json cancelled = unwrap(f.rt->actions().cancel(Json{{"run_id", run.at("id")}, {"ref", "test"}}));
    CHECK(cancelled.at("status") == "cancelled");
    CHECK(cancelled.at("pending").is_null());
    auto late = f.rt->actions().client_result(f.answer_audio(run));
    REQUIRE_FALSE(late);
    CHECK(late.error().code == Errc::Conflict);
    CHECK(f.transport->requests().empty());
    // nothing of the late recording was kept
    CHECK_FALSE(f.rt->blobs().has(Sha256::hex("AUDIO")));
  }

  TEST_CASE("the person may decline on the device, the device may fail: the run ends with the reason and nothing more runs") {
    Fixture f;
    f.declare(Json{{"captures_audio", "prompt"}});
    auto start = [&] { return unwrap(f.rt->actions().run(Json{{"chain", f.chain_to_text("web-1")}, {"element", "device:microphone"}, {"client_id", "web-1"}, {"confirmed", true}})); };
    Json run = start();
    const Json no = unwrap(f.rt->actions().client_result(Json{{"run_id", run.at("id")}, {"receipt_id", run.at("pending").at("receipt_id")}, {"state", "declined"}}));
    CHECK(no.at("status") == "rejected");
    CHECK(no.at("error").at("code") == "declined");
    CHECK(no.at("steps").at(0).at("status") == "declined");
    run = start();
    const Json failed = unwrap(f.rt->actions().client_result(Json{{"run_id", run.at("id")}, {"receipt_id", run.at("pending").at("receipt_id")}, {"state", "failed"},
                                                                    {"error", Json{{"message", "the microphone is busy"}}}}));
    CHECK(failed.at("status") == "failed");
    CHECK(failed.at("error").at("message") == "the microphone is busy");
    run = start();
    const Json unavailable = unwrap(f.rt->actions().client_result(Json{{"run_id", run.at("id")}, {"receipt_id", run.at("pending").at("receipt_id")}, {"state", "unavailable"}}));
    CHECK(unavailable.at("status") == "failed");
    // ok without the recording the action is meant to bring is a failure of the step, not a silent success
    run = start();
    const Json empty = unwrap(f.rt->actions().client_result(Json{{"run_id", run.at("id")}, {"receipt_id", run.at("pending").at("receipt_id")}, {"state", "ok"}}));
    CHECK(empty.at("status") == "failed");
    CHECK(empty.at("error").at("message").get<std::string>().find("returned nothing") != std::string::npos);
    CHECK(f.transport->requests().empty());
  }

  TEST_CASE("the preset rule asks for the confirmation of a capture, and it is the user's to change") {
    Fixture f;
    f.declare(Json{{"captures_audio", "granted"}, {"plays_audio", "granted"}});
    // without the confirmation nothing is asked of the device
    const Json asked = unwrap(f.rt->actions().run(Json{{"action", "device.record_audio"}, {"element", "device:microphone"}, {"client_id", "web-1"}}));
    CHECK(asked.at("status") == "awaiting_confirmation");
    CHECK(asked.at("pending").at("gates").at(0).at("rule") == "confirm:device.record_audio");
    CHECK(asked.at("pending").at("gates").at(0).at("initiator") == "application preset");
    CHECK(asked.at("steps").empty());
    // playing a recording is not a capture: nothing is asked
    const std::string hash = unwrap(f.rt->blobs().put("AUDIO", "audio/mp4")).hash;
    const Json play = unwrap(f.rt->actions().run(Json{{"action", "device.play_audio"}, {"element", "device:speaker"}, {"args", Json{{"audio", hash}}}, {"client_id", "web-1"}}));
    INFO(play.value("error", Json(nullptr)).dump());
    REQUIRE(play.at("status") == "awaiting_client");
    CHECK(play.at("pending").at("client_step").at("with").at("audio") == hash);
    CHECK(play.at("pending").at("client_step").at("expects").empty());
    const Json played = unwrap(f.rt->actions().client_result(Json{{"run_id", play.at("id")}, {"receipt_id", play.at("pending").at("receipt_id")}, {"state", "ok"}}));
    CHECK(played.at("status") == "completed");
    // the user switches the rule off (an own rule, the overlay): the capture then goes straight to the device
    const Json off = unwrap(f.rt->actions().invoke("rules.disable", Json{{"id", "confirm:device.record_audio"}, {"confirmed", true}}));
    INFO(off.value("status", ""));
    const Json direct = unwrap(f.rt->actions().run(Json{{"action", "device.record_audio"}, {"element", "device:microphone"}, {"client_id", "web-1"}}));
    CHECK(direct.at("status") == "awaiting_client");
  }

  TEST_CASE("client.blob hands a stored blob to the client; a hash that is not stored is not found") {
    Fixture f;
    const std::string hash = unwrap(f.rt->blobs().put("AUDIO", "audio/mp4")).hash;
    const Json blob = unwrap(f.rt->actions().invoke("client.blob", Json{{"hash", hash}}));
    CHECK(blob.at("data_base64") == f.audio_b64());
    CHECK(blob.at("size") == 5);
    auto missing = f.rt->actions().invoke("client.blob", Json{{"hash", std::string(64, 'a')}});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == Errc::NotFound);
    CHECK_FALSE(f.rt->actions().invoke("client.blob", Json{{"hash", "../etc/passwd"}}));
  }
}

namespace {

// The chat window as a client uses it: the conversation, the declared client, the words typed.
struct ChatFixture : Fixture {
  std::string conv_id;
  ChatFixture() {
    conv_id = unwrap(rt->db().create_conv("Voice")).id;
    NewMessage m;
    m.conv_id = conv_id;
    m.role = "user";
    m.text = "Hello.";
    unwrap(rt->db().create_msg(m));
  }
  chat_route::Outcome input(const std::string& text, const std::string& client = "web-1") {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    Json req{{"message", text}};
    if (!client.empty()) req["client_id"] = client;
    return unwrap(chat_route::handle(*rt, text, opts, req, nullptr));
  }
  Json route(const std::string& text, const std::string& client = "web-1") {
    Json req{{"text", text}, {"conv_id", conv_id}};
    if (!client.empty()) req["client_id"] = client;
    return unwrap(chat_route::route(*rt, req));
  }
  Json message(const std::string& id) { return unwrap(rt->db().get_msg(id))->to_json(); }
  std::vector<Json> assistant_messages() {
    std::vector<Json> out;
    for (const auto& m : unwrap(rt->db().get_msgs(conv_id)))
      if (m.role == "assistant") out.push_back(m.to_json());
    return out;
  }
};

}  // namespace

TEST_SUITE("client_devices_chat") {
  TEST_CASE("\"nagraj notatkę głosową i ją streść\" proposes record -> transcribe -> summarise; after \"tak\" the recording is the browser's") {
    ChatFixture f;
    f.declare(Json{{"captures_audio", "granted"}});
    const Json routed = f.route("Nagraj notatkę głosową i ją streść");
    REQUIRE(routed.at("kind") == "command");
    const Json cmd = routed.at("command");
    CHECK(cmd.at("action") == "summarise");
    CHECK(cmd.at("chain") == Json::array({"device.record_audio", "transcribe", "summarise"}));
    CHECK(cmd.at("element").at("id") == "device:microphone");
    CHECK(cmd.at("runnable") == true);
    CHECK(cmd.at("client_steps").at(0).at("action") == "device.record_audio");
    CHECK(cmd.at("needs_confirmation").at(0).at("action") == "device.record_audio");
    // the words of the English reading, and one action alone (the microphone is implied by the action)
    const Json english = f.route("record a voice note and summarise it");
    CHECK(english.at("command").at("chain") == Json::array({"device.record_audio", "transcribe", "summarise"}));
    // the microphone named in the words, then a second action: the same chain
    CHECK(f.route("Nagraj dźwięk z mikrofonu i streść go").at("command").at("chain") == Json::array({"device.record_audio", "transcribe", "summarise"}));
    CHECK(f.route("Nagraj dźwięk").at("command").at("action") == "device.record_audio");
    CHECK(f.route("Nagraj dźwięk").at("command").at("element").at("id") == "device:microphone");
    // a client that declared nothing is told it cannot, as before
    const Json server = f.route("Nagraj notatkę głosową i ją streść", "");
    CHECK(server.at("command").at("runnable") == false);
    CHECK(server.at("command").at("not_runnable").at(0).at("action") == "device.record_audio");

    // the proposal in the conversation: what runs where
    const auto proposed = f.input("Nagraj notatkę głosową i ją streść");
    REQUIRE(proposed.handled);
    const Json proposal = f.message(proposed.assistant_message_id);
    const std::string text = proposal.at("text");
    CHECK(text.find("Na Twoim urządzeniu") != std::string::npos);
    CHECK(text.find("Nagraj dźwięk") != std::string::npos);
    CHECK(proposal.at("metadata").at("action_proposal").at("status") == "pending");
    CHECK(proposal.at("metadata").at("action_proposal").at("client_steps").at(0).at("action") == "device.record_audio");

    // "tak": the run goes as far as the device: the answer says it waits, the question is in the message
    const auto yes = f.input("tak");
    REQUIRE(yes.handled);
    const Json waiting = f.message(yes.assistant_message_id);
    CHECK(waiting.at("text").get<std::string>().find("Czekam na Twoje urządzenie") != std::string::npos);
    const Json meta = waiting.at("metadata").at("action_run");
    CHECK(meta.at("status") == "awaiting_client");
    CHECK(meta.at("pending").at("kind") == "client_step");
    CHECK(meta.at("pending").at("client_step").at("action") == "device.record_audio");
    const std::string run_id = meta.at("run_id");
    CHECK(f.message(proposed.assistant_message_id).at("metadata").at("action_proposal").at("status") == "waiting");
    CHECK(f.transport->requests().empty());

    // the browser recorded: the transcription and the summary follow, the conversation hears the result, the old question is closed
    f.expect_asr("We ship on Friday if the tests pass.");
    f.expect_chat("Ship Friday, tests permitting.");
    const Json done = unwrap(chat_route::client_result(*f.rt, Json{{"run_id", run_id}, {"receipt_id", meta.at("pending").at("receipt_id")}, {"state", "ok"},
                                                                      {"parts", Json::array({Json{{"kind", "audio"}, {"mime", "audio/mp4"}, {"data_base64", f.audio_b64()}}})}}));
    REQUIRE(done.at("status") == "completed");
    CHECK(done.contains("message_id"));
    const Json result = f.message(done.at("message_id"));
    CHECK(result.at("text").get<std::string>().find("Ship Friday, tests permitting.") != std::string::npos);
    CHECK(result.at("metadata").at("action_run").at("status") == "completed");
    CHECK(result.at("metadata").at("action_run").at("produced").at("forms").size() == 2);  // the recording and the summary
    CHECK(f.message(yes.assistant_message_id).at("metadata").at("action_run").at("pending").is_null());
    CHECK(f.message(proposed.assistant_message_id).at("metadata").at("action_proposal").at("status") == "executed");
    // the model was asked to condense the transcript, nothing else
    CHECK(f.transport->requests().size() == 2);
    CHECK(unwrap(json::parse(f.transport->requests()[1].body)).at("messages").dump().find("We ship on Friday if the tests pass.") != std::string::npos);
  }

  TEST_CASE("cancelling the paused step closes its question in the conversation; the device answer that comes later changes nothing") {
    ChatFixture f;
    f.declare(Json{{"captures_audio", "granted"}});
    f.input("Nagraj notatkę głosową i ją streść");
    const auto yes = f.input("tak");
    const Json meta = f.message(yes.assistant_message_id).at("metadata").at("action_run");
    REQUIRE(meta.at("status") == "awaiting_client");
    const Json cancelled = unwrap(chat_route::cancel(*f.rt, Json{{"run_id", meta.at("run_id")}, {"ref", "web"}}));
    CHECK(cancelled.at("status") == "cancelled");
    CHECK(f.message(yes.assistant_message_id).at("metadata").at("action_run").at("pending").is_null());
    CHECK(f.message(yes.assistant_message_id).at("metadata").at("action_run").at("status") == "cancelled");
    auto late = chat_route::client_result(*f.rt, Json{{"run_id", meta.at("run_id")}, {"receipt_id", meta.at("pending").at("receipt_id")}, {"state", "ok"},
                                                      {"parts", Json::array({Json{{"kind", "audio"}, {"mime", "audio/mp4"}, {"data_base64", f.audio_b64()}}})}});
    REQUIRE_FALSE(late);
    CHECK(f.transport->requests().empty());
  }

  // P14 4: the pages that show a conversation learn of a new client step from the event bus (SSE /api/events/stream, JNI subscribe), not by polling.
  TEST_CASE("a question for the device is announced on the event bus with ids only; answering or cancelling it announces that it is closed") {
    ChatFixture f;
    f.declare(Json{{"captures_audio", "granted"}});
    std::vector<Json> seen;
    const auto sub = f.rt->bus().on(events::kActionClientStep, [&](std::string_view event, const Json& data) {
      CHECK(event == "action:client_step");
      seen.push_back(data);
    });
    (void)sub;
    const auto proposed = f.input("Nagraj notatkę głosową i ją streść");
    REQUIRE(proposed.handled);
    CHECK(seen.empty());  // a proposal is not a step of the device
    const auto yes = f.input("tak");
    REQUIRE(yes.handled);
    const Json meta = f.message(yes.assistant_message_id).at("metadata").at("action_run");
    REQUIRE(meta.at("status") == "awaiting_client");
    REQUIRE(seen.size() == 1);
    CHECK(seen[0] == Json{{"conv_id", f.conv_id}, {"message_id", yes.assistant_message_id}, {"run_id", meta.at("run_id")}, {"state", "waiting"},
                          {"receipt_id", meta.at("pending").at("receipt_id")}, {"step", meta.at("pending").at("step")}, {"action", "device.record_audio"}});
    // the page that gets it reads the message and finds the question there
    CHECK(f.message(seen[0].at("message_id")).at("metadata").at("action_run").at("pending").at("kind") == "client_step");
    // cancelled elsewhere: the question is closed, and the event says so (the page refreshes and the block goes)
    const Json cancelled = unwrap(chat_route::cancel(*f.rt, Json{{"run_id", meta.at("run_id")}, {"ref", "api"}}));
    REQUIRE(cancelled.at("status") == "cancelled");
    REQUIRE(seen.size() == 2);  // the message that says it was cancelled asks nothing: no second waiting event
    CHECK(seen[1].at("state") == "closed");
    CHECK(seen[1].at("status") == "cancelled");
    CHECK(seen[1].at("message_id") == yes.assistant_message_id);
    CHECK(seen[1].at("run_id") == meta.at("run_id"));
  }

  TEST_CASE("the client id rides on the chat request: a request that names a client is not an unknown chat option") {
    CHECK(ChatOptions::from_json(Json{{"message", "hi"}, {"client_id", "web-1"}}));
  }
}
