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
    ChatOptions next;
    next.conv_id = opts.conv_id;
    const auto followup = f.send("Co jeszcze widać?", next).at("messages").dump();
    CHECK(followup.find("PAPUGA") != std::string::npos);
    CHECK(followup.find("image_url") != std::string::npos);
    next.include_history = false;
    const auto no_history = f.send("Tylko to pytanie", next).at("messages").dump();
    CHECK(no_history.find("PAPUGA") == std::string::npos);
    CHECK(no_history.find("image_url") == std::string::npos);
  }

  TEST_CASE("a file that cannot go to the model is refused before anything is stored, and the refusal names it") {
    Fixture f;
    const std::string conv = unwrap(f.rt->db().create_conv("Refused")).id;
    const auto binary = unwrap(f.rt->blobs().put("unread binary", "application/octet-stream"));
    struct Case { std::string ref; Errc code; };
    for (const Case& c : {Case{"blob:" + std::string(64, 'a') + "/gone.txt", Errc::NotFound}, Case{"blob:../../config.json/x.txt", Errc::NotFound},
                          Case{"blob:", Errc::NotFound}, Case{"blob:" + binary.hash + "/raport.exe", Errc::Unsupported},
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


#if defined(__linux__) && !defined(__ANDROID__)
  TEST_CASE("PDF: compressed Unicode text from both pages reaches the provider via a blob, with original bytes intact") {
    Fixture f;
    const auto source = unwrap(fsutil::read_file(std::filesystem::path(LOOM_TEST_FIXTURES) / "documents/unicode-compressed.pdf"));
    const auto blob = unwrap(f.rt->blobs().put(source, "application/pdf"));
    ChatOptions opts;
    opts.conv_id = unwrap(f.rt->db().create_conv("PDF")).id;
    opts.attachments = {"blob:" + blob.hash + "/Zażółć.PDF"};
    const Json body = f.send("Co jest w dokumencie?", opts);
    const auto text = body.at("messages").back().at("content").dump();
    CHECK(text.find("Zażółć gęślą jaźń — PDF PAPUGA. Strona 1.") != std::string::npos);
    CHECK(text.find("Druga strona: Łódź, żółw, ąćęłńóśźż.") != std::string::npos);
    CHECK(text.find("%PDF") == std::string::npos);
    CHECK(unwrap(f.rt->blobs().read(blob.hash)) == source);
    CHECK(f.rt->blobs().verify(blob.hash));
    const auto msgs = unwrap(f.rt->db().get_msgs(*opts.conv_id));
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[0].attachments == Json(opts.attachments));
    const auto notes = msgs[1].metadata.at("attachment_notes");
    REQUIRE(notes.size() == 1);
    CHECK(notes[0]["pages"] == 2);
    CHECK(notes[0]["pages_without_text"].empty());
  }


  TEST_CASE("PDF: follow-up and a reopened conversation retain the document text with the original message") {
    Fixture f;
    const auto blob = unwrap(f.rt->blobs().put(unwrap(fsutil::read_file(std::filesystem::path(LOOM_TEST_FIXTURES) / "documents/unicode-compressed.pdf")), "application/pdf"));
    ChatOptions first;
    first.conv_id = unwrap(f.rt->db().create_conv("PDF follow-up")).id;
    first.attachments = {"blob:" + blob.hash + "/document.pdf"};
    (void)f.send("Przeczytaj", first);
    ChatOptions next;
    next.conv_id = first.conv_id;
    auto check_history = [&](const Json& body) {
      bool found = false;
      for (const auto& message : body.at("messages")) {
        if (message.at("role") == "user" && message.at("content").dump().find("Przeczytaj") != std::string::npos) {
          const auto wire = message.at("content").dump();
          CHECK(wire.find("Zażółć gęślą jaźń") != std::string::npos);
          CHECK(wire.find("Druga strona: Łódź") != std::string::npos);
          found = true;
        }
      }
      CHECK(found);
    };
    check_history(f.send("A druga strona?", next));
    REQUIRE(f.rt->config().save());
    REQUIRE(f.rt->secrets().save());
    f.rt.reset();
    RuntimeOptions o;
    o.data_dir = f.td.path().string(); o.start_workers = false; o.http = f.transport;
    f.rt = unwrap(Runtime::open(o));
    check_history(f.send("Wróć do dokumentu", next));
  }

  TEST_CASE("PDF: extracted text participates in the existing secret gate before storage or provider calls") {
    Fixture f;
    // An exact stored secret in the document, synthetic test bytes only.
    f.rt->secrets().set("fixture_secret", "PAPUGA. Strona 1.");
    const auto blob = unwrap(f.rt->blobs().put(unwrap(fsutil::read_file(std::filesystem::path(LOOM_TEST_FIXTURES) / "documents/unicode-compressed.pdf")), "application/pdf"));
    ChatOptions opts;
    opts.attachments = {"blob:" + blob.hash + "/secret.pdf"};
    const auto held = unwrap(f.rt->chat().send("Sprawdź", opts));
    REQUIRE(held.secret_pending);
    CHECK((*held.secret_pending).dump().find("secret.pdf") != std::string::npos);
    CHECK((*held.secret_pending).dump().find("PAPUGA") == std::string::npos);
    CHECK(f.transport->requests().empty());
    CHECK(unwrap(f.rt->db().list_convs()).empty());
  }

  TEST_CASE("PDF: blank, encrypted and malformed documents refuse before persistence, preview and model usage") {
    Fixture f;
    f.rt->config().set("ui_language", "pl");
    const auto conv = unwrap(f.rt->db().create_conv("Unreadable PDF")).id;
    for (const auto& filename : {"no-text.pdf", "password.pdf", "broken.pdf"}) {
      INFO(filename);
      const std::string source = std::string(filename) == "broken.pdf" ? "%PDF-1.7 invalid" :
          unwrap(fsutil::read_file(std::filesystem::path(LOOM_TEST_FIXTURES) / "documents" / filename));
      const auto blob = unwrap(f.rt->blobs().put(source, "application/pdf"));
      ChatOptions opts;
      opts.conv_id = conv;
      opts.attachments = {"blob:" + blob.hash + "/" + filename};
      const auto sent = f.rt->chat().send("Przeczytaj", opts);
      REQUIRE_FALSE(sent);
      CHECK(sent.error().message.find(filename) != std::string::npos);
      CHECK(sent.error().message.find("nie została dołączona") != std::string::npos);
      CHECK_FALSE(f.rt->chat().preview_usage("Przeczytaj", opts));
      CHECK(unwrap(f.rt->db().get_msgs(conv)).empty());
      CHECK(f.transport->requests().empty());
      CHECK(unwrap(f.rt->blobs().read(blob.hash)) == source);
    }
  }

  TEST_CASE("PDF: partial text extraction names unreadable pages in the request and persistent notice") {
    Fixture f;
    f.rt->config().set("ui_language", "pl");
    const auto blob = unwrap(f.rt->blobs().put(unwrap(fsutil::read_file(std::filesystem::path(LOOM_TEST_FIXTURES) / "documents/mixed-text-blank.pdf")), "application/pdf"));
    ChatOptions opts;
    opts.conv_id = unwrap(f.rt->db().create_conv("Mixed PDF")).id;
    opts.attachments = {"blob:" + blob.hash + "/mixed.pdf"};
    const auto body = f.send("", opts);
    CHECK(body.at("messages").back().at("content").dump().find("strony bez odczytywalnego tekstu: 3") != std::string::npos);
    const auto msgs = unwrap(f.rt->db().get_msgs(*opts.conv_id));
    const auto note = msgs.back().metadata.at("attachment_notes").at(0);
    CHECK(note["pages"] == 3);
    CHECK(note["pages_without_text"] == Json::array({3}));
    CHECK(note["message"].get<std::string>().find("skany nie zostały odczytane") != std::string::npos);
  }

  TEST_CASE("PDF: the configured character limit applies to extracted Unicode, with visible exact counts") {
    Fixture f;
    f.rt->config().set("chat_attachment_max_chars", 10);
    f.rt->config().set("ui_language", "pl");
    const auto path = std::filesystem::path(LOOM_TEST_FIXTURES) / "documents/unicode-compressed.pdf";
    Json notes;
    const auto content = unwrap(f.rt->chat().build_content_checked("", {path.string()}, &notes));
    const auto text = content.at(0).at("text").get<std::string>();
    CHECK(text.find("Zażółć gęś") != std::string::npos);
    CHECK(text.find("PAPUGA") == std::string::npos);
    REQUIRE(notes.size() == 2);
    CHECK(notes[1]["kept_chars"] == 10);
    CHECK(notes[1]["original_chars"].get<int>() > 10);
    CHECK(notes[1]["message"].get<std::string>().find("wysłano 10 z") != std::string::npos);
    f.rt->config().set("chat_attachment_max_chars", 0);
    CHECK(unwrap(f.rt->chat().build_content_checked("", {path.string()}, nullptr)).dump().find("ąćęłńóśźż") != std::string::npos);
  }
#endif

  TEST_CASE("PDF: a missing reader refuses explicitly; the user's warning policy is respected without inventing content") {
    Fixture f;
    f.rt->config().set("ui_language", "pl");
    const Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "chat_context"},
        {"overrides", Json{{"attachments", Json{{"document_readers", Json::array({Json{{"extensions", Json::array({".pdf"})},
            {"adapter", "poppler_glib"}, {"library", "/missing/reader-for-test.so"}}})}}}}}};
    REQUIRE(fsutil::write_file(f.td.path() / "profiles/chat_context.pack", overlay.dump()));
    ChatOptions opts;
    opts.conv_id = unwrap(f.rt->db().create_conv("Reader missing")).id;
    opts.attachments = {(std::filesystem::path(LOOM_TEST_FIXTURES) / "documents/unicode-compressed.pdf").string()};
    const auto sent = f.rt->chat().send("Przeczytaj", opts);
    REQUIRE_FALSE(sent);
    CHECK(sent.error().code == Errc::Unavailable);
    CHECK(sent.error().message.find("nie jest dostępny czytnik PDF") != std::string::npos);
    CHECK(unwrap(f.rt->db().get_msgs(*opts.conv_id)).empty());
    CHECK(f.transport->requests().empty());
    f.rt->config().set("chat_attachment_on_unreadable", "send_with_warning");
    const auto body = f.send("Przeczytaj", opts);
    CHECK(body.at("messages").back().at("content").dump().find("Plik nie został dołączony") != std::string::npos);
    CHECK(body.dump().find("PAPUGA") == std::string::npos);
    const auto msgs = unwrap(f.rt->db().get_msgs(*opts.conv_id));
    CHECK(msgs.back().metadata.at("attachment_notes").at(0).at("status") == "unreadable");
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
