// P12 4: transcribe and ocr are implemented actions. Each is a composite with one use_model step of the media kind: the recording or the
// picture of the element goes to the media providers, which put it on the wire through the one use_model (the adapter of the provider's
// family, the usage guard, the provenance record with its operation id). The request bytes are pinned on the test doubles of the
// transport (no network); the result lands in the graph as every other result; the receipt of the call is on the run node.
#include <doctest/doctest.h>

#include <fstream>

#include "loom/action_runner.h"
#include "loom/provenance.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/media_providers.h"
#include "loom/net/http.h"
#include "loom/port_graph.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

// The request as the test doubles saw it, with the random multipart boundary replaced.
Json wire(const net::HttpRequest& r) {
  Json j = loom::test::request_json(r);
  std::string boundary;
  for (const auto& [name, value] : r.headers) {
    const auto at = value.find("boundary=");
    if (name == "Content-Type" && at != std::string::npos) boundary = value.substr(at + 9);
  }
  if (boundary.empty()) return j;
  auto swap = [&](std::string s) {
    for (auto p = s.find(boundary); p != std::string::npos; p = s.find(boundary, p + 8)) s.replace(p, boundary.size(), "BOUNDARY");
    return s;
  };
  j["body"] = swap(r.body);
  for (auto& h : j["headers"]) h[1] = swap(h[1].get<std::string>());
  return j;
}

const char* kGroq = "https://api.groq.com/openai/v1/audio/transcriptions";
const char* kOcr = "https://api.ocr.space/parse/image";

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;

  Fixture(bool keys = true) {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    loom::test::measured_baseline_only(rt->config());  // P17 6: this test is about the x10 hold over three tiny samples, so the floor of the baseline is off
    if (keys) {
      rt->secrets().set("groq_api_key", "gk_test");
      rt->secrets().set("ocr_space_api_key", "ocrkey");
    }
    rt->media().refresh();
  }
  void expect_asr(const std::string& text) {
    transport->expect("POST", kGroq, net::ScriptedTransport::Reply::json(200, Json{{"text", text}, {"language", "pl"}, {"duration", 2.5}}));
  }
  void expect_ocr(const std::string& text) {
    transport->expect("POST", kOcr, net::ScriptedTransport::Reply::json(200, Json{{"IsErroredOnProcessing", false},
                                                                                     {"ParsedResults", Json::array({Json{{"ParsedText", text}, {"TextOverlay", Json{{"Lines", Json::array()}}}}})}}));
  }
  void expect_chat(const std::string& text) {
    transport->expect("POST", "https://api.test/chat/completions",
                      net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", text}}}}})},
                                                                     {"usage", Json{{"total_tokens", 7}}}}));
  }
  std::string recording() { return unwrap(rt->blobs().put("AUDIO", "audio/mp4")).hash; }
  std::string picture() { return unwrap(rt->blobs().put("PNGDATA", "image/png")).hash; }
  Json run(const Json& request) { return unwrap(rt->actions().run(request)); }
  std::vector<Link> links(const std::string& node, const std::string& type) { return unwrap(rt->db().get_links(node, type)); }
  void overlay(const std::string& domain, const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(td.path() / "profiles" / (domain + ".pack"))
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
  }
};

const Json kAsrGolden{
    {"method", "POST"},
    {"url", kGroq},
    {"headers", Json::array({Json::array({"Authorization", "Bearer gk_test"}), Json::array({"Content-Type", "multipart/form-data; boundary=BOUNDARY"})})},
    {"body", "--BOUNDARY\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.mp4\"\r\n"
             "Content-Type: audio/mp4\r\n\r\nAUDIO\r\n"
             "--BOUNDARY\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-large-v3-turbo\r\n"
             "--BOUNDARY\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\nverbose_json\r\n"
             "--BOUNDARY\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\npl\r\n"
             "--BOUNDARY--\r\n"},
    {"timeout_ms", 60000},
    {"stream", false}};

const Json kOcrGolden{
    {"method", "POST"},
    {"url", kOcr},
    {"headers", Json::array({Json::array({"Content-Type", "application/x-www-form-urlencoded"})})},
    {"body", "apikey=ocrkey&base64Image=data%3Aimage%2Fpng%3Bbase64%2CUE5HREFUQQ%3D%3D&language=pol&OCREngine=2&isTable=True&scale=True"},
    {"timeout_ms", 60000},
    {"stream", false}};

}  // namespace

TEST_SUITE("media_actions") {
  TEST_CASE("transcribe is implemented; its request bytes are those of the media providers, pinned, through the one use_model") {
    Fixture f;
    f.expect_asr("Hello from the recording.");
    const std::string audio = f.recording();
    const Json run = f.run(Json{{"action", "transcribe"}, {"element", audio}, {"args", Json{{"language", "pl"}}}, {"ref", "test"}});
    INFO(run.dump());
    REQUIRE(run.at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 1);
    CHECK(wire(f.transport->requests()[0]) == kAsrGolden);
    // the text is the result: parts and structured data
    REQUIRE(run.at("result").at("parts").size() == 1);
    CHECK(run.at("result").at("parts").at(0).at("kind") == "text");
    CHECK(run.at("result").at("parts").at(0).at("text") == "Hello from the recording.");
    // without a language argument the language is the provider's own: the field is not on the wire
    Fixture g;
    g.expect_asr("x");
    REQUIRE(g.run(Json{{"action", "transcribe"}, {"element", g.recording()}, {"ref", "test"}}).at("status") == "completed");
    CHECK(wire(g.transport->requests()[0]).at("body").get<std::string>().find("name=\"language\"") == std::string::npos);
    // the same recording asked of the media providers directly makes the very same request
    Fixture h;
    h.expect_asr("x");
    REQUIRE(h.rt->media().transcribe_bytes("AUDIO", "m4a", std::nullopt, std::string("pl")));
    CHECK(wire(h.transport->requests()[0]) == kAsrGolden);
  }

  TEST_CASE("ocr is implemented; the picture goes out as the pinned form") {
    Fixture f;
    f.expect_ocr("Printed words");
    const Json run = f.run(Json{{"action", "ocr"}, {"element", f.picture()}, {"args", Json{{"language", "pl"}}}, {"ref", "test"}});
    INFO(run.dump());
    REQUIRE(run.at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 1);
    CHECK(wire(f.transport->requests()[0]) == kOcrGolden);
    CHECK(run.at("result").at("parts").at(0).at("text") == "Printed words");
  }

  TEST_CASE("the result is a text form that expresses the content of the recording, derived from it and produced by a run node that names the receipt") {
    Fixture f;
    f.expect_asr("Hello from the recording.");
    const std::string audio = f.recording();
    const Json run = f.run(Json{{"action", "transcribe"}, {"element", audio}, {"args", Json{{"language", "pl"}}}, {"ref", "test"}});
    REQUIRE(run.at("status") == "completed");
    const Json& produced = run.at("result").at("produced");
    INFO(produced.dump());
    REQUIRE(produced.at("items").size() == 1);
    const std::string form = produced.at("items").at(0).at("form");
    const Node form_node = *unwrap(f.rt->db().get_node(form));
    CHECK(form_node.kind == "text_form");  // audio_form -> text_form: one content in two forms
    CHECK(form_node.content == "Hello from the recording.");
    CHECK(form_node.metadata.at("action") == "transcribe");
    const auto expresses = f.links(form, "expresses");
    REQUIRE(expresses.size() == 1);
    CHECK(expresses[0].dst == produced.at("content").get<std::string>());
    CHECK(expresses[0].metadata.at("content_op") == "preserve");  // a recording read into text keeps the content
    CHECK(expresses[0].weight == doctest::Approx(0.95));
    const auto derived = f.links(form, "derived_from");
    REQUIRE(derived.size() == 1);
    CHECK(derived[0].dst == produced.at("source").get<std::string>());  // derived from the recording (its element, a node over the blob)
    CHECK(unwrap(f.rt->db().get_node(derived[0].dst))->metadata.dump().find(audio) != std::string::npos);
    // the run node: the call, the provider's family, and the receipt of the admission
    const std::string run_node = produced.at("run_node");
    const Node node = *unwrap(f.rt->db().get_node(run_node));
    CHECK(node.kind == "action_run");
    CHECK(node.metadata.at("action") == "transcribe");
    const Json call = node.metadata.at("steps").at(0).at("calls").at(0);
    CHECK(call.at("adapter") == "openai_transcriptions");
    CHECK(call.at("provider") == "groq");
    REQUIRE(call.at("operation_id").is_string());
    CHECK(call.at("operation_id").get<std::string>().rfind("model_usage_", 0) == 0);
    CHECK(node.metadata.at("usage_receipt_id") == call.at("operation_id"));
    CHECK(node.metadata.at("usage_operation_ids") == Json::array({call.at("operation_id")}));
    // never a secret or the bytes
    const std::string everything = node.metadata.dump() + form_node.metadata.dump();
    CHECK(everything.find("gk_test") == std::string::npos);
    CHECK(everything.find("AUDIO") == std::string::npos);
    // the run document carries the provenance record, attributed to the action it served
    const Json record = run.at("steps").at(0).at("variants").at(0).at("provenance").at(0);
    CHECK(record.at("schema") == "loom.model_use/1");
    CHECK(record.at("action") == "transcribe");
    CHECK(record.at("operation_id") == call.at("operation_id"));
  }

  TEST_CASE("a chain from a recording to a summary runs: the transcript is what the model is asked to condense") {
    Fixture f;
    const std::string audio = f.recording();
    const Json proposal = unwrap(f.rt->actions().invoke("propose_chain", Json{{"element", audio}, {"target_trait", "summary"}}));
    REQUIRE(proposal.at("chains").size() >= 1);
    const Json chain = proposal.at("chains").at(0);
    CHECK(chain.at("executable") == true);
    f.expect_asr("We ship on Friday.");
    f.expect_chat("They ship on Friday.");
    const Json run = f.run(Json{{"chain", chain}, {"element", audio}, {"ref", "test"}});
    INFO(run.dump());
    REQUIRE(run.at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 2);
    CHECK(f.transport->requests()[0].url == kGroq);
    const Json chat = unwrap(json::parse(f.transport->requests()[1].body));
    CHECK(chat.at("messages").dump().find("We ship on Friday.") != std::string::npos);
    CHECK(run.at("result").at("parts").at(0).at("text") == "They ship on Friday.");
  }

  // P14: a call the usage guard holds (x10 growth) is a pause with the guard's receipt, not a failed step (R16).
  TEST_CASE("x10 growth in a transcription pauses the run with the guard's receipt; confirming sends the same request once; the result lands in the graph") {
    Fixture f;
    for (int i = 0; i < 3; ++i) {
      f.expect_asr("small " + std::to_string(i));
      const std::string tiny = unwrap(f.rt->blobs().put("tiny" + std::to_string(i), "audio/mp4")).hash;
      REQUIRE(f.run(Json{{"action", "transcribe"}, {"element", tiny}, {"args", Json{{"language", "pl"}}}, {"ref", "test"}}).at("status") == "completed");
    }
    REQUIRE(f.transport->requests().size() == 3);
    const std::string big(60000, 'a');
    const std::string audio = unwrap(f.rt->blobs().put(big, "audio/mp4")).hash;
    const Json held = f.run(Json{{"action", "transcribe"}, {"element", audio}, {"args", Json{{"language", "pl"}}}, {"ref", "test"}});
    INFO(held.dump());
    REQUIRE(held.at("status") == "awaiting_usage");
    CHECK(held.at("pending").at("kind") == "usage");
    CHECK(held.at("pending").at("usage_pending").at("schema") == "loom.action_usage_pending/1");
    CHECK(held.at("pending").at("usage_pending").at("media") == "media.asr");
    CHECK(held.at("pending").at("usage_pending").at("sent") == false);
    CHECK(held.at("pending").at("receipt_id").is_string());
    CHECK(f.transport->requests().size() == 3);  // nothing left the machine, no other provider was tried
    // a wrong receipt answers nothing
    CHECK_FALSE(f.rt->actions().confirm(Json{{"run_id", held.at("id")}, {"approved", true}, {"receipt_id", "arr_made_up"}}));
    CHECK(f.transport->requests().size() == 3);
    f.expect_asr("Big recording read.");
    const Json done = unwrap(f.rt->actions().confirm(
        Json{{"run_id", held.at("id")}, {"approved", true}, {"receipt_id", held.at("pending").at("receipt_id")}, {"ref", "web"}}));
    INFO(done.dump());
    REQUIRE(done.at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 4);  // sent once
    // the very request an unguarded call of the same recording makes (the boundary is derived from the parts)
    Fixture ref;
    ref.expect_asr("x");
    REQUIRE(ref.rt->media().transcribe_bytes(big, "m4a", std::nullopt, std::string("pl")));
    CHECK(wire(f.transport->requests()[3]) == wire(ref.transport->requests()[0]));
    CHECK(f.transport->requests()[3].body == ref.transport->requests()[0].body);
    // the text is the result, in the graph as any other
    CHECK(done.at("result").at("parts").at(0).at("text") == "Big recording read.");
    const Json& produced = done.at("result").at("produced");
    REQUIRE(produced.at("items").size() == 1);
    const Node form = *unwrap(f.rt->db().get_node(produced.at("items").at(0).at("form").get<std::string>()));
    CHECK(form.kind == "text_form");
    CHECK(form.content == "Big recording read.");
    CHECK(done.at("confirmations").size() == 1);
    // the step's record carries the call: the receipt of the confirmed admission is on the run node
    const Node run_node = *unwrap(f.rt->db().get_node(produced.at("run_node").get<std::string>()));
    CHECK(run_node.metadata.at("usage_receipt_id").is_string());
  }

  TEST_CASE("declining the held transcription ends the run rejected; nothing is sent") {
    Fixture f;
    for (int i = 0; i < 3; ++i) {
      f.expect_asr("small");
      const std::string tiny = unwrap(f.rt->blobs().put("tiny" + std::to_string(i), "audio/mp4")).hash;
      REQUIRE(f.run(Json{{"action", "transcribe"}, {"element", tiny}, {"ref", "test"}}).at("status") == "completed");
    }
    const std::string audio = unwrap(f.rt->blobs().put(std::string(60000, 'a'), "audio/mp4")).hash;
    const Json held = f.run(Json{{"action", "transcribe"}, {"element", audio}, {"ref", "test"}});
    REQUIRE(held.at("status") == "awaiting_usage");
    const Json no = unwrap(f.rt->actions().confirm(
        Json{{"run_id", held.at("id")}, {"approved", false}, {"receipt_id", held.at("pending").at("receipt_id")}, {"ref", "web"}}));
    CHECK(no.at("status") == "rejected");
    CHECK(no.at("error").at("code") == "rejected");
    CHECK(f.transport->requests().size() == 3);
    // nothing was produced in the graph
    CHECK(no.at("result").is_null());
  }

  TEST_CASE("the same for a picture: the held read is a pause, the confirmed one is sent once") {
    Fixture f;
    for (int i = 0; i < 3; ++i) {
      f.expect_ocr("small");
      const std::string tiny = unwrap(f.rt->blobs().put("PNG" + std::to_string(i), "image/png")).hash;
      REQUIRE(f.run(Json{{"action", "ocr"}, {"element", tiny}, {"ref", "test"}}).at("status") == "completed");
    }
    const std::string picture = unwrap(f.rt->blobs().put(std::string(60000, 'p'), "image/png")).hash;
    const Json held = f.run(Json{{"action", "ocr"}, {"element", picture}, {"ref", "test"}});
    INFO(held.dump());
    REQUIRE(held.at("status") == "awaiting_usage");
    CHECK(held.at("pending").at("usage_pending").at("media") == "media.ocr");
    CHECK(f.transport->requests().size() == 3);
    f.expect_ocr("Words in the big picture");
    const Json done = unwrap(f.rt->actions().confirm(
        Json{{"run_id", held.at("id")}, {"approved", true}, {"receipt_id", held.at("pending").at("receipt_id")}, {"ref", "web"}}));
    REQUIRE(done.at("status") == "completed");
    CHECK(f.transport->requests().size() == 4);
    CHECK(done.at("result").at("parts").at(0).at("text") == "Words in the big picture");
  }

  // P14 5: finishing a run links the port runs of ITS calls, found by operation id; it does not read the port runs of every run (R12's reconcile).
  TEST_CASE("a finished run links its own port runs to its node and reads no others") {
    Fixture f;
    // a history: port runs of other runs, one of them with its run node already written (a link that is missing there is not this run's business)
    for (int i = 0; i < 40; ++i) {
      port_graph::PortRun decoy;
      decoy.port = "asr";
      decoy.adapter = "openai_transcriptions";
      decoy.operation_id = "model_usage_decoy_" + std::to_string(i);
      decoy.action_run_id = "ar_other_" + std::to_string(i % 2);
      (void)unwrap(port_graph::record_port_run(f.rt->db(), decoy));
    }
    (void)unwrap(f.rt->db().get_or_create_node("run:ar_other_0", "action_run"));
    f.expect_asr("Hello from the recording.");
    const Json run = f.run(Json{{"action", "transcribe"}, {"element", f.recording()}, {"ref", "test"}});
    REQUIRE(run.at("status") == "completed");
    const std::string run_node = run.at("result").at("produced").at("run_node");
    const std::string operation = run.at("steps").at(0).at("variants").at(0).at("provenance").at(0).at("operation_id");
    const Node own = *unwrap(f.rt->db().find_node("port_run:" + operation, "port_run"));
    const auto own_links = f.links(own.id, "part_of");
    REQUIRE(own_links.size() == 1);
    CHECK(own_links[0].src == own.id);
    CHECK(own_links[0].dst == run_node);
    CHECK(own_links[0].metadata.at("run_id") == run.at("id"));
    // the others stay as they were: no part_of anywhere else, not even for the run whose node exists
    std::size_t part_of = 0;
    for (const auto& n : unwrap(f.rt->db().list_nodes("port_run", 1000)))
      part_of += f.links(n.id, "part_of").size();
    CHECK(part_of == 1);
  }

  TEST_CASE("a microphone is a device: recording from it stays declared, with its reason, until a client owns the driver") {
    Fixture f;
    const Json proposal = unwrap(f.rt->actions().invoke("propose_chain", Json{{"element", "device:microphone"}, {"target_trait", "has_text"}}));
    REQUIRE(proposal.at("chains").size() >= 1);
    CHECK(proposal.at("chains").at(0).at("steps").at(0).at("action") == "device.record_audio");
    CHECK(proposal.at("chains").at(0).at("steps").at(1).at("action") == "transcribe");
    CHECK(proposal.at("chains").at(0).at("executable") == false);  // the first step is the client's
    auto refused = f.rt->actions().run(Json{{"chain", proposal.at("chains").at(0)}, {"element", "device:microphone"}});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message.find("planned") != std::string::npos);
    CHECK(f.transport->requests().empty());
    // and it says why, in the catalogue, for the action itself
    const Json record = unwrap(f.rt->actions().invoke("for", Json{{"element", "device:microphone"}}));
    CHECK(record.dump().find("no driver") != std::string::npos);
  }

  TEST_CASE("without a key the step fails with the provider's reason and nothing is sent; a recording that is not audio is not read") {
    Fixture f(false);
    const std::string audio = f.recording();
    const Json run = f.run(Json{{"action", "transcribe"}, {"element", audio}, {"ref", "test"}});
    CHECK(run.at("status") == "failed");
    CHECK(run.at("error").at("message").get<std::string>().find("No ASR providers configured") != std::string::npos);
    CHECK(f.transport->requests().empty());
    // a text is not a recording: transcribe does not apply to it (the planner says what is missing)
    const std::string text = unwrap(f.rt->blobs().put("just text", "text/plain")).hash;
    const Json for_text = unwrap(f.rt->actions().invoke("for", Json{{"element", text}}));
    for (const auto& a : for_text.at("actions"))
      if (a.at("action") == "transcribe") CHECK(a.at("status") != "available");
  }

  TEST_CASE("the estimate does not guess a price for recognition: money is unknown, with the reason, never zero") {
    Fixture f;
    const Json preview = unwrap(f.rt->actions().preview(Json{{"action", "transcribe"}, {"element", f.recording()}}));
    REQUIRE(preview.at("runnable") == true);
    const Json money = preview.at("estimate").at("money");
    CHECK(money.at("known") == false);
    CHECK(money.at("reason").get<std::string>().find("recognition") != std::string::npos);
    CHECK(preview.at("estimate").at("model_calls") == 1);
  }

  TEST_CASE("calls from the media providers are attributed to the action they serve; the catalogue governs them, and can be reversed by data") {
    Fixture f;
    Json seen = Json::array();
    f.rt->media().set_provenance_sink([&](const Json& record) { seen.push_back(record); });
    f.expect_asr("x");
    REQUIRE(f.rt->media().transcribe_bytes("AUDIO", "m4a", std::nullopt, std::string("pl")));
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].at("action") == "transcribe");
    CHECK(seen[0].at("kind") == "media.asr");
    f.expect_ocr("y");
    REQUIRE(f.rt->media().ocr_bytes("PNGDATA", "png", std::nullopt, std::string("pl")));
    CHECK(seen[1].at("action") == "ocr");
    // reverse: the action declared planned again by the user's overlay - the call is refused before a byte leaves
    f.overlay("actions", Json{{"actions", Json{{"transcribe", Json{{"status", "planned"}}}}}});
    const std::size_t sent = f.transport->requests().size();
    auto refused = f.rt->media().transcribe_bytes("AUDIO", "m4a", std::nullopt, std::string("pl"));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message.find("All ASR providers failed") != std::string::npos);  // each provider was refused by use_model: the action is planned
    CHECK(f.transport->requests().size() == sent);
    // and by data again: declared implemented, it works as before
    f.overlay("actions", Json{{"actions", Json{{"transcribe", Json{{"status", "implemented"}}}}}});
    f.expect_asr("again");
    CHECK(f.rt->media().transcribe_bytes("AUDIO", "m4a", std::nullopt, std::string("pl")));
  }
}
