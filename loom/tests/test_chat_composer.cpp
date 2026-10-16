// P33: what the + menu next to the message field hands the kernel. A file the web composer uploaded travels as `blob:<hash>/<name>`
// and is read from the blob store; the model of a conversation is a setting of the one scope chain (P24), read when a request names none.
#include <doctest/doctest.h>

#include "loom/chat_engine.h"
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

  TEST_CASE("a blob reference that names no stored blob, or no valid hash, is left out and the text still goes") {
    Fixture f;
    ChatOptions opts;
    opts.conv_id = unwrap(f.rt->db().create_conv("Missing")).id;
    opts.attachments = {"blob:" + std::string(64, 'a') + "/gone.txt", "blob:../../config.json/x.txt", "blob:"};
    const Json body = f.send("Tekst bez plikow", opts);
    const Json& content = body.at("messages").back().at("content");
    const std::string dumped = content.dump();
    CHECK(dumped.find("Tekst bez plikow") != std::string::npos);
    CHECK(dumped.find("FILE:") == std::string::npos);
    CHECK(dumped.find("default_model") == std::string::npos);  // nothing outside the store is read
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
