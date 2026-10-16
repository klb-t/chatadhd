// P33: what the + menu next to the message field hands the kernel. A file the web composer uploaded travels as `blob:<hash>/<name>`
// and is read from the blob store; the model of a conversation is a setting of the one scope chain (P24), read when a request names none.
#include <doctest/doctest.h>

#include "loom/chat_engine.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})},
              {"usage", Json{{"total_tokens", 7}}}};
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/person-model");
    rt->config().set("model_registry_autorefresh", "off");
    rt->config().set("semantic_analysis", false);
  }
  // One send without streaming; returns the body the provider received.
  Json send(const std::string& text, ChatOptions opts) {
    transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("ok")));
    opts.stream = false;
    const auto before = transport->requests().size();
    REQUIRE(rt->chat().send(text, opts));
    const auto requests = transport->requests();
    REQUIRE(requests.size() == before + 1);
    return unwrap(json::parse(requests.back().body));
  }
  void set_conversation_model(const std::string& conv_id, const std::string& model) {
    ConvPatch patch;
    patch.metadata = Json{{"settings", Json{{"default_model", model}}}};
    REQUIRE(rt->db().update_conv(conv_id, patch));
  }
};

}  // namespace

TEST_SUITE("chat_composer") {
  TEST_CASE("a blob reference is read from the blob store; its name decides the kind and is shown to the model") {
    Fixture f;
    const auto text_blob = unwrap(f.rt->blobs().put("Sekretne slowo: PAPUGA", "text/plain"));
    const std::string png("\x89PNG\r\n\x1a\n-pixel", 14);
    const auto image_blob = unwrap(f.rt->blobs().put(png, "image/png"));
    ChatOptions opts;
    opts.conv_id = unwrap(f.rt->db().create_conv("Files")).id;
    opts.attachments = {"blob:" + text_blob.hash + "/notatki.txt", "blob:" + image_blob.hash + "/pixel.png"};
    const Json body = f.send("Co jest w plikach?", opts);
    const Json& content = body.at("messages").back().at("content");
    REQUIRE(content.is_array());
    const std::string dumped = content.dump();
    CHECK(dumped.find("--- FILE: notatki.txt ---") != std::string::npos);
    CHECK(dumped.find("PAPUGA") != std::string::npos);
    CHECK(dumped.find("image_url") != std::string::npos);
    CHECK(dumped.find(image_blob.hash) == std::string::npos);  // the image goes as data, the store's file name is not shown
    // The message keeps the references, so the conversation shows which files went with it.
    const auto msgs = unwrap(f.rt->db().get_msgs(*opts.conv_id));
    REQUIRE_FALSE(msgs.empty());
    const Json stored = msgs.front().to_json();
    CHECK(stored.at("attachments").dump().find("notatki.txt") != std::string::npos);
  }

  TEST_CASE("a file that cannot go to the model is refused before anything is stored, and the refusal names it") {
    Fixture f;
    const std::string conv = unwrap(f.rt->db().create_conv("Refused")).id;
    const auto pdf = unwrap(f.rt->blobs().put("%PDF-1.7 raport", "application/pdf"));
    struct Case { std::string ref; Errc code; };
    for (const Case& c : {Case{"blob:" + std::string(64, 'a') + "/gone.txt", Errc::NotFound}, Case{"blob:../../config.json/x.txt", Errc::NotFound},
                          Case{"blob:", Errc::NotFound}, Case{"blob:" + pdf.hash + "/raport.pdf", Errc::Unsupported},
                          Case{(f.td.path() / "missing.txt").string(), Errc::NotFound}}) {
      INFO(c.ref);
      ChatOptions opts;
      opts.conv_id = conv;
      opts.stream = false;
      opts.attachments = {c.ref};
      const auto sent = f.rt->chat().send("Tekst z plikiem", opts);
      REQUIRE_FALSE(sent);
      CHECK(sent.error().code == c.code);
      const std::string& said = sent.error().message;
      CHECK(said.size() >= c.ref.size());
      CHECK(said.compare(said.size() - c.ref.size(), c.ref.size(), c.ref) == 0);  // the client marks the chip it names
      const auto preview = f.rt->chat().preview_usage("Tekst z plikiem", opts);
      REQUIRE_FALSE(preview);
      CHECK(preview.error().code == c.code);
    }
    CHECK(unwrap(f.rt->db().get_msgs(conv)).empty());  // nothing was stored
    CHECK(f.transport->requests().empty());           // nothing was sent
    CHECK(f.rt->chat().check_attachments({}));
  }

  TEST_CASE("a message of files alone goes without an empty text part") {
    Fixture f;
    const auto text_blob = unwrap(f.rt->blobs().put("Sekretne slowo: PAPUGA", "text/plain"));
    ChatOptions opts;
    opts.conv_id = unwrap(f.rt->db().create_conv("Files only")).id;
    opts.attachments = {"blob:" + text_blob.hash + "/notatki.txt"};
    const Json body = f.send("", opts);
    const Json& content = body.at("messages").back().at("content");
    REQUIRE(content.is_array());
    REQUIRE(content.size() == 1);
    CHECK(content[0].at("text").get<std::string>().find("PAPUGA") != std::string::npos);
  }

  TEST_CASE("an action named by the composer: the message is its command, through the proposal and the rules") {
    Fixture f;
    const std::string conv = unwrap(f.rt->db().create_conv("Actions")).id;
    ChatOptions opts;
    opts.conv_id = conv;
    // agent.run: the words are its prompt; it acts on the application.
    const auto agent = unwrap(chat_route::handle(*f.rt, "uporzadkuj moje notatki", opts, Json{{"message", "uporzadkuj moje notatki"}, {"action", "agent.run"}}, nullptr));
    REQUIRE(agent.handled);
    CHECK(agent.chunk.at("type").get<std::string>().rfind("action_", 0) == 0);
    const Json agent_proposal = agent.result.at("routed").at("proposal");
    CHECK(agent_proposal.at("action") == "agent.run");
    CHECK(agent_proposal.at("args").at("prompt") == "uporzadkuj moje notatki");
    const auto user = unwrap(f.rt->db().get_msg(agent.user_message_id));
    REQUIRE(user);
    CHECK(user->metadata.at("composer_action") == "agent.run");
    // depict paints the message itself; without an image model it is said what is missing, nothing is guessed.
    const auto image = unwrap(chat_route::handle(*f.rt, "kot na dachu o zachodzie", opts, Json{{"message", "kot na dachu o zachodzie"}, {"action", "depict"}}, nullptr));
    REQUIRE(image.handled);
    const Json image_proposal = image.result.at("routed").at("proposal");
    CHECK(image_proposal.at("action") == "depict");
    CHECK(image_proposal.at("status") == "unavailable");
    CHECK(image_proposal.at("element").dump().find("message") != std::string::npos);
    // Files do not go with an action, an unknown action is not guessed: refused before anything is stored.
    const auto before = unwrap(f.rt->db().get_msgs(conv)).size();
    ChatOptions with_file = opts;
    with_file.attachments = {"blob:" + std::string(64, 'b') + "/x.txt"};
    const auto files = unwrap(chat_route::handle(*f.rt, "zrob cos", with_file, Json{{"message", "zrob cos"}, {"action", "agent.run"}}, nullptr));
    CHECK(files.handled);
    CHECK(files.chunk.at("type") == "error");
    const auto unknown = unwrap(chat_route::handle(*f.rt, "zrob cos", opts, Json{{"message", "zrob cos"}, {"action", "no.such.action"}}, nullptr));
    CHECK(unknown.chunk.at("type") == "error");
    CHECK(unwrap(f.rt->db().get_msgs(conv)).size() == before);
    CHECK(f.transport->requests().empty());
    // The request field is read by the conversation window, and must be a string.
    CHECK(ChatOptions::from_json(Json{{"message", "x"}, {"action", "agent.run"}}));
    CHECK_FALSE(ChatOptions::from_json(Json{{"message", "x"}, {"action", 7}}));
  }

  TEST_CASE("a composer action from a blank page that asks for a new conversation (R35) never lands in the remembered one") {
    Fixture f;
    // An earlier send left this conversation remembered by the kernel (ChatEngine::current_conv).
    const std::string older = unwrap(f.rt->db().create_conv("Older")).id;
    REQUIRE(f.rt->chat().load_conv(older));
    const auto before = unwrap(f.rt->db().get_msgs(older)).size();
    ChatOptions fresh;
    fresh.new_conversation = true;
    const auto run = unwrap(chat_route::handle(*f.rt, "uporzadkuj moje notatki", fresh,
                                               Json{{"message", "uporzadkuj moje notatki"}, {"action", "agent.run"}, {"new_conversation", true}}, nullptr));
    REQUIRE(run.handled);
    CHECK(run.conv_id != older);
    CHECK_FALSE(run.conv_id.empty());
    CHECK(unwrap(f.rt->db().get_msgs(older)).size() == before);
    const auto user = unwrap(f.rt->db().get_msg(run.user_message_id));
    REQUIRE(user);
    CHECK(user->conv_id == run.conv_id);
    // Without the request the remembered conversation (now the new one) is continued, as before.
    ChatOptions keep;
    const auto again = unwrap(chat_route::handle(*f.rt, "zrob to jeszcze raz", keep, Json{{"message", "zrob to jeszcze raz"}, {"action", "agent.run"}}, nullptr));
    REQUIRE(again.handled);
    CHECK(again.conv_id == run.conv_id);
    CHECK(unwrap(f.rt->db().get_msgs(older)).size() == before);
  }

  TEST_CASE("the model of a conversation comes from the scope chain when the request names none; a named model wins") {
    Fixture f;
    const std::string with_own = unwrap(f.rt->db().create_conv("Own model")).id;
    const std::string without = unwrap(f.rt->db().create_conv("Person's model")).id;
    f.set_conversation_model(with_own, "test/conversation-model");

    ChatOptions a;
    a.conv_id = with_own;
    CHECK(f.send("pierwsza", a).at("model") == "test/conversation-model");

    ChatOptions b;
    b.conv_id = without;
    CHECK(f.send("druga", b).at("model") == "test/person-model");

    ChatOptions named;
    named.conv_id = with_own;
    named.model = "test/named";
    CHECK(f.send("trzecia", named).at("model") == "test/named");

    // "" at the conversation level is not a model: the chain goes on to the person.
    f.set_conversation_model(with_own, "");
    ChatOptions again;
    again.conv_id = with_own;
    CHECK(f.send("czwarta", again).at("model") == "test/person-model");
  }

  TEST_CASE("the usage preview of a conversation names the same model as its send") {
    Fixture f;
    const std::string conv = unwrap(f.rt->db().create_conv("Preview")).id;
    f.set_conversation_model(conv, "test/conversation-model");
    ChatOptions opts;
    opts.conv_id = conv;
    opts.stream = false;
    const Json preview = unwrap(f.rt->chat().preview_usage("podglad", opts));
    CHECK(preview.dump().find("test/conversation-model") != std::string::npos);
    CHECK(preview.dump().find("test/person-model") == std::string::npos);
  }
}
