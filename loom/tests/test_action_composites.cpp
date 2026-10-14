// P10 B: translate, parody, narrate and depict are composites in the action catalogue (steps, prompts in Polish and
// English, parameters as data); they run through use_model on the existing adapters, under the rules and the x10 usage
// guard like every other action. What no adapter can serve yet stays planned and says why.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_result.h"
#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/use_model.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})},
              {"usage", Json{{"total_tokens", 7}}}};
}

// A speech adapter for the tests: one more family registered through register_adapter, no edit to any call site.
class TestSpeechAdapter final : public llm::Adapter {
 public:
  std::string_view family() const noexcept override { return "test_speech"; }
  llm::Capabilities capabilities() const override { return {{"text"}, {"audio"}, {}}; }
  std::string default_endpoint(llm::Operation, const Json&) const override { return "/audio/speech"; }
  bool needs_output_store(const llm::CallSpec&) const override { return true; }
  Result<net::HttpRequest> build(const llm::Route& route, llm::Operation, const llm::CallSpec& spec) const override {
    std::string input;
    for (auto it = spec.messages.rbegin(); it != spec.messages.rend(); ++it)
      if (json::get_string(*it, "role") == "user" && it->contains("content") && (*it)["content"].is_string()) {
        input = (*it)["content"].get<std::string>();
        break;
      }
    net::HttpRequest req;
    req.method = "POST";
    req.url = route.base_url + (route.endpoint.empty() ? default_endpoint(llm::Operation::Complete, route.metadata) : route.endpoint);
    LOOM_TRY(llm::apply_auth(route, req));
    req.headers.emplace_back("Content-Type", "application/json");
    Json body{{"model", spec.model}, {"input", input}};
    for (auto it = spec.parameters.begin(); it != spec.parameters.end(); ++it) body[it.key()] = it.value();
    req.body = json::dump(body);
    return req;
  }
  bool success(llm::Operation, int status) const noexcept override { return status == 200; }
  Result<llm::Parsed> parse(std::string_view body) const override {
    llm::Parsed out;
    llm::Part audio;
    audio.kind = "audio";
    audio.bytes = std::string(body);
    audio.mime = "audio/wav";
    out.outputs.push_back(std::move(audio));
    return out;
  }
  std::unique_ptr<llm::StreamDecoder> stream_decoder(const llm::StreamHooks&, const CancelToken*) const override { return nullptr; }
};

struct Registered {
  Registered() { llm::register_adapter("test_speech", [] { return std::make_unique<TestSpeechAdapter>(); }); }
};
const Registered registered;

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id;
  std::string message_id;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    message_id = add("user", "We should ship on Friday.");
    add("assistant", "Agreed, Friday works if the tests pass.");
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void chat(const std::string& content) {
    transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content)));
  }
  Json body(std::size_t i) { return unwrap(json::parse(transport->requests().at(i).body)); }
  Json run(const std::string& action, const std::string& element, const Json& args = Json::object(), const Json& context = Json::object()) {
    return unwrap(rt->actions().run(Json{{"action", action}, {"element", element}, {"args", args}, {"context", context}, {"ref", "test"}}));
  }
  void overlay(const std::string& domain, const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(td.path() / "profiles" / (domain + ".pack"))
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
  }
  void manifest(const Json& entry) {
    Json list = rt->config().get("provider_manifests");
    if (!list.is_array()) list = Json::array();
    list.push_back(entry);
    rt->config().set("provider_manifests", list);
  }
  std::size_t nodes_of(const std::string& kind) { return unwrap(rt->db().list_nodes(kind, 1000)).size(); }
};

const std::string kPng = std::string("\x89PNG\r\n\x1a\n") + "FAKEPIXELS";

}  // namespace

TEST_SUITE("action_composites") {
  TEST_CASE("translate: the prompt is data in the language of the command, the target is a parameter; the bytes sent are pinned") {
    Fixture f;
    f.chat("Wir sollten am Freitag ausliefern.");
    const Json run = f.run("translate", f.message_id, Json{{"target_language", "German"}});
    REQUIRE(run.at("status") == "completed");
    CHECK(run.at("result").at("parts").at(0).at("text") == "Wir sollten am Freitag ausliefern.");
    const auto requests = f.transport->requests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].url == "https://api.test/chat/completions");
    const Json body = f.body(0);
    CHECK(body.at("model") == "test/model");
    CHECK(body.at("temperature") == 0.2);
    REQUIRE(body.at("messages").size() == 3);
    CHECK(body.at("messages").at(0).at("role") == "system");
    CHECK(body.at("messages").at(0).at("content").get<std::string>().find("precise translator") != std::string::npos);
    CHECK(body.at("messages").at(1) == Json{{"role", "user"}, {"content", "We should ship on Friday."}});
    CHECK(body.at("messages").at(2) == Json{{"role", "user"}, {"content", "Translate the text above into German."}});
    // the default target is the language the command was written in
    f.chat("Powinniśmy wdrożyć w piątek.");
    const Json plain = f.run("translate", f.message_id);
    REQUIRE(plain.at("status") == "completed");
    CHECK(f.body(1).at("messages").at(2).at("content") == "Translate the text above into English.");
    // Polish: the same steps, the prompts of the Polish entries
    f.chat("Powinniśmy wdrożyć w piątek.");
    const Json polish = f.run("translate", f.message_id, Json::object(), Json{{"lang", "pl"}});
    REQUIRE(polish.at("status") == "completed");
    CHECK(f.body(2).at("messages").at(0).at("content").get<std::string>().find("precyzyjnym tłumaczem") != std::string::npos);
    CHECK(f.body(2).at("messages").at(2).at("content") == "Przetłumacz powyższy tekst na język: polski.");
    // an explicit argument beats the context; a regional tag counts as its language
    f.chat("Wir sollten am Freitag ausliefern.");
    f.run("translate", f.message_id, Json{{"target_language", "niemiecki"}, {"lang", "pl-PL"}}, Json{{"lang", "en"}});
    CHECK(f.body(3).at("messages").at(2).at("content") == "Przetłumacz powyższy tekst na język: niemiecki.");
    // The result is a text form of the same content, produced by a run of `translate`, with the fidelity of a translation.
    const Json& produced = run.at("result").at("produced");
    const auto expresses = unwrap(f.rt->db().get_links(produced.at("items").at(0).at("form").get<std::string>(), "expresses"));
    REQUIRE(expresses.size() == 1);
    CHECK(expresses[0].weight == doctest::Approx(0.95));
    CHECK(expresses[0].metadata.at("content_op") == "preserve");
  }

  TEST_CASE("parody: the style is a parameter with a preset") {
    Fixture f;
    f.chat("We shall deliver on Friday, arr.");
    const Json run = f.run("parody", f.message_id, Json{{"style", "a pirate"}});
    REQUIRE(run.at("status") == "completed");
    const Json body = f.body(0);
    CHECK(body.at("temperature") == 0.9);
    CHECK(body.at("messages").at(0).at("content").get<std::string>().find("You write parodies") != std::string::npos);
    CHECK(body.at("messages").back().at("content") == "Write a parody of the text above in this style: a pirate.");
    f.chat("Parodia.");
    f.run("parody", f.message_id, Json::object(), Json{{"lang", "pl"}});
    CHECK(f.body(1).at("messages").back().at("content") == "Napisz parodię powyższego tekstu w tym stylu: lekka, życzliwa satyra.");
    const Node form = *unwrap(f.rt->db().get_node(run.at("result").at("produced").at("items").at(0).at("form").get<std::string>()));
    CHECK(form.kind == "text_form");
    const auto expresses = unwrap(f.rt->db().get_links(form.id, "expresses"));
    CHECK(expresses.at(0).metadata.at("content_op") == "restyle");
    CHECK(expresses.at(0).metadata.at("added") == Json::array({"style"}));
  }

  TEST_CASE("narrate: a script for reading aloud; with a speech model the script is spoken through an audio adapter") {
    Fixture f;
    f.chat("We should ship on Friday. Short sentences, calm voice.");
    const Json script = f.run("narrate", f.message_id);
    REQUIRE(script.at("status") == "completed");
    CHECK(f.transport->requests().size() == 1);  // no speech model: the second step is skipped, nothing more is sent
    CHECK(script.at("steps").at(1).at("status") == "skipped");
    CHECK(script.at("result").at("parts").at(0).at("text") == "We should ship on Friday. Short sentences, calm voice.");
    CHECK(f.body(0).at("messages").back().at("content") == "Prepare the text above for narration. Tone: calm and clear.");

    // A speech model is set through the settings: the script goes to the audio adapter, the result is the recording.
    f.manifest(Json{{"id", "voice"}, {"base_url", "https://voice.test/v1"}, {"auth_secret", "voice_key"}, {"auth_scheme", "bearer"},
                    {"metadata", Json{{"adapter", "test_speech"}}}});
    f.rt->secrets().set("voice_key", "voice-secret");
    f.rt->config().set("speech_model", "voice-1");
    f.rt->config().set("speech_provider", "voice");
    f.chat("Short sentences, spoken.");
    const std::string wav = "RIFF....WAVEfakeaudio";
    f.transport->expect("POST", "https://voice.test/v1/audio/speech", net::ScriptedTransport::Reply::text(200, wav));
    const Json spoken = f.run("narrate", f.message_id, Json{{"speech_parameters", Json{{"voice", "calm"}}}});
    REQUIRE(spoken.at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 3);
    CHECK(f.transport->requests()[2].url == "https://voice.test/v1/audio/speech");
    CHECK(f.body(2) == Json{{"model", "voice-1"}, {"input", "Short sentences, spoken."}, {"voice", "calm"}});
    const Json& part = spoken.at("result").at("parts").at(0);
    CHECK(part.at("kind") == "audio");
    CHECK(unwrap(f.rt->blobs().read(part.at("blob_hash").get<std::string>())) == wav);
    // The recording is an audio form of the content of the message, produced by the run, stored as a blob.
    const Json& item = spoken.at("result").at("produced").at("items").at(0);
    CHECK(item.at("class") == "audio_form");
    const Node form = *unwrap(f.rt->db().get_node(item.at("form").get<std::string>()));
    CHECK(form.kind == "audio_form");
    CHECK(form.metadata.at("blob_hash") == part.at("blob_hash"));
    CHECK(unwrap(f.rt->db().get_links(form.id, "stored_as")).size() == 1);
    CHECK(unwrap(f.rt->db().get_links(form.id, "derived_from")).at(0).dst == f.message_id);
    CHECK(f.nodes_of("content") == 1);  // the script and the recording are forms of ONE content of the message
    CHECK(spoken.at("steps").at(1).at("variants").at(0).at("provenance").at(0).at("adapter") == "test_speech");
    CHECK(spoken.dump().find("voice-secret") == std::string::npos);
  }

  TEST_CASE("depict: the text becomes an image prompt, the prompt becomes an image through the images adapter") {
    Fixture f;
    // Without an image model the plan does not start: nothing is sent, the missing setting is named.
    auto none = f.rt->actions().run(Json{{"action", "depict"}, {"element", f.message_id}});
    REQUIRE_FALSE(none);
    CHECK(none.error().message.find("model") != std::string::npos);
    CHECK(none.error().message.find("image_model") != std::string::npos);
    CHECK(f.transport->requests().empty());

    f.manifest(Json{{"id", "img"}, {"base_url", "https://img.test/v1"}, {"auth_secret", "img_key"}, {"auth_scheme", "bearer"},
                    {"metadata", Json{{"adapter", "openai_images"}}}});
    f.rt->secrets().set("img_key", "img-secret");
    f.rt->config().set("image_model", "image-1");
    f.rt->config().set("image_provider", "img");
    f.chat("A release calendar with Friday circled in red, watercolour.");
    f.transport->expect("POST", "https://img.test/v1/images/generations",
                        net::ScriptedTransport::Reply::json(200, Json{{"created", 1}, {"data", Json::array({Json{{"b64_json", base64::encode(kPng)}}})}}));
    const Json run = f.run("depict", f.message_id, Json{{"style", "watercolour"}, {"size", "512x512"}});
    REQUIRE(run.at("status") == "completed");
    REQUIRE(f.transport->requests().size() == 2);
    // 1. the chat model writes the prompt
    CHECK(f.transport->requests()[0].url == "https://api.test/chat/completions");
    CHECK(f.body(0).at("messages").back().at("content") == "Write an image prompt that depicts the text above. Style: watercolour.");
    CHECK(f.body(0).at("messages").at(1).at("content") == "We should ship on Friday.");
    // 2. the images adapter paints it: the bytes of the request are pinned
    CHECK(test::request_json(f.transport->requests()[1]) ==
          Json{{"method", "POST"}, {"url", "https://img.test/v1/images/generations"},
               {"headers", Json::array({Json::array({"Authorization", "Bearer img-secret"}), Json::array({"Content-Type", "application/json"}),
                                    Json::array({"HTTP-Referer", "https://github.com/chatadhd"}), Json::array({"X-Title", "ChatADHD"})})},
               {"body", R"({"model":"image-1","prompt":"A release calendar with Friday circled in red, watercolour.","n":1,"size":"512x512","response_format":"b64_json"})"},
               {"timeout_ms", 180000}, {"stream", false}});
    const Json& part = run.at("result").at("parts").at(0);
    CHECK(part.at("kind") == "image");
    CHECK(part.at("mime") == "image/png");
    CHECK(unwrap(f.rt->blobs().read(part.at("blob_hash").get<std::string>())) == kPng);
    // the image is an image form of the message's content, produced by a run that used two models
    const Json& produced = run.at("result").at("produced");
    const Node form = *unwrap(f.rt->db().get_node(produced.at("items").at(0).at("form").get<std::string>()));
    CHECK(form.kind == "image_form");
    const Node run_node = *unwrap(f.rt->db().get_node(produced.at("run_node").get<std::string>()));
    CHECK(run_node.metadata.at("action") == "depict");
    CHECK(run_node.metadata.at("steps").at(0).at("calls").at(0).at("requested_model") == "test/model");
    CHECK(run_node.metadata.at("steps").at(1).at("calls").at(0).at("requested_model") == "image-1");
    CHECK(run_node.metadata.at("steps").at(1).at("calls").at(0).at("adapter") == "openai_images");
    CHECK(run_node.metadata.at("model") == "image-1");  // the step that made the result
    CHECK(run.dump().find("img-secret") == std::string::npos);
    // the picture can be the element of the next action: its class is image_form with its bytes behind it
    CHECK(unwrap(f.rt->actions().invoke("for", Json{{"element", form.id}})).at("element").at("class") == "image_form");
  }

  TEST_CASE("what no adapter can serve yet stays planned and says why; nothing is sent") {
    Fixture f;
    NodeOptions o;
    o.metadata = Json{{"blob_hash", Sha256::hex("a picture")}, {"mime", "image/png"}};
    const std::string picture = unwrap(f.rt->db().create_node("A picture", "image_form", o));
    auto refused = f.rt->actions().run(Json{{"action", "describe_image"}, {"element", picture}});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message.find("planned") != std::string::npos);
    CHECK(refused.error().message.find("No adapter of use_model accepts an image") != std::string::npos);
    CHECK(f.transport->requests().empty());
    const Json view = unwrap(f.rt->actions().invoke("for", Json{{"element", picture}}));
    bool seen = false;
    for (const auto& row : view.at("actions"))
      if (row.at("action") == "describe_image") {
        seen = true;
        CHECK(row.at("callable") == false);
        CHECK(row.at("planned_reason").at("pl").get<std::string>().find("adapter") != std::string::npos);
      }
    CHECK(seen);
    const Json preview = unwrap(f.rt->actions().preview(Json{{"action", "describe_image"}, {"element", picture}}));
    CHECK(preview.at("runnable") == false);
    CHECK(preview.at("not_runnable").at(0).at("why") == "planned");
    CHECK(preview.at("not_runnable").at(0).at("reason").at("en").get<std::string>().find("image") != std::string::npos);
    // transcribe and ocr are implemented since P12 (use_model step with the media providers behind it): see test_media_actions.cpp
  }

  TEST_CASE("the rules and the x10 usage guard apply to these actions like to every other") {
    Fixture f;
    f.overlay("action_rules",
              Json{{"rules", Json{{"no_parody", Json{{"id", "no_parody"}, {"scope", "installation"}, {"target", Json{{"kind", "any"}}}, {"action", "parody"},
                                                      {"effect", "deny"}, {"reason", "Parodies are off in this installation."}, {"initiator", "the owner"}}}}}});
    const Json denied = f.run("parody", f.message_id);
    CHECK(denied.at("status") == "denied");
    CHECK(denied.at("error").at("gates").at(0).at("initiator") == "the owner");
    CHECK(f.transport->requests().empty());
    // x10 growth: three small translations set the baseline, a message 100 times bigger is held with the guard's receipt
    for (int i = 0; i < 3; ++i) f.chat("ok");
    for (int i = 0; i < 3; ++i) REQUIRE(f.run("translate", f.message_id).at("status") == "completed");
    const std::string big = f.add("user", std::string(60000, 'x'));
    const Json held = f.run("translate", big);
    REQUIRE(held.at("status") == "awaiting_usage");
    CHECK(held.at("pending").at("kind") == "usage");
    CHECK(f.transport->requests().size() == 3);  // nothing left the machine
    f.chat("done");
    const Json done = unwrap(f.rt->actions().confirm(Json{{"run_id", held.at("id")}, {"approved", true},
                                                          {"receipt_id", held.at("pending").at("receipt_id")}, {"ref", "test"}}));
    CHECK(done.at("status") == "completed");
    CHECK(f.transport->requests().size() == 4);
    CHECK(done.at("result").at("produced").at("items").at(0).at("form").is_string());
    CHECK(unwrap(f.rt->db().get_node(done.at("result").at("produced").at("run_node").get<std::string>()))->metadata.at("usage_receipt_id") ==
          held.at("pending").at("receipt_id"));  // the receipt of the guard's question is in the provenance
  }

  TEST_CASE("from the chat window: a Polish command is answered with the Polish prompts and the result is linked") {
    Fixture f;
    ChatOptions opts;
    opts.conv_id = f.conv_id;
    opts.stream = false;
    auto input = [&](const std::string& text) {
      Json req{{"message", text}};
      return unwrap(chat_route::handle(*f.rt, text, opts, req, nullptr));
    };
    f.chat("We should ship on Friday.");
    const auto proposed = input("przetłumacz tę wiadomość");
    REQUIRE(proposed.handled);
    CHECK(f.transport->requests().empty());  // a proposal sends nothing
    const auto done = input("tak");
    REQUIRE(done.handled);
    REQUIRE(f.transport->requests().size() == 1);
    CHECK(f.body(0).at("messages").at(0).at("content").get<std::string>().find("precyzyjnym tłumaczem") != std::string::npos);
    CHECK(f.body(0).at("messages").back().at("content").get<std::string>().find("na język: polski") != std::string::npos);
    const Message result = *unwrap(f.rt->db().get_msg(done.assistant_message_id));
    CHECK(result.metadata.at("action_run").at("produced").at("forms").size() == 1);
  }
}
