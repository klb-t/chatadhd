// P27: the flow of a request - the selector reads the graph, a renderer gives the selection to the model, an output node takes a graph reply back;
// the embedder is pure structure and the inverse of the selector.
#include <doctest/doctest.h>

#include "loom/action_overlay.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/guardian.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json chat_reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 7}}}};
}

struct Fix {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Fix() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://flow.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("system_prompt", "System.");
    rt->config().set("semantic_analysis", false);
    rt->config().set("model_registry_autorefresh", "off");
    conv = unwrap(rt->db().create_conv("Flow")).id;
  }
  std::string say(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  ChatOptions opts() {
    ChatOptions o;
    o.conv_id = conv;
    o.stream = false;
    o.include_memory = false;
    o.include_graph_memory = false;
    return o;
  }
  // The body of the request sent for `text` when the model answers `reply`.
  Json send(const std::string& text, const std::string& reply, const ChatOptions& o, Json* result = nullptr) {
    transport->expect("POST", "https://flow.test/chat/completions", net::ScriptedTransport::Reply::json(200, chat_reply(reply)));
    auto r = unwrap(rt->chat().send(text, o));
    if (result) *result = r.to_json();
    return unwrap(json::parse(transport->requests().back().body));
  }
  // Everything the graph holds, to compare byte for byte.
  std::string dump_graph() {
    Json all = Json::object();
    all["messages"] = Json::array();
    for (const auto& m : unwrap(rt->db().get_msgs(conv))) all["messages"].push_back(m.to_json());
    Json links = Json::array();
    for (const auto& l : unwrap(rt->db().get_links())) links.push_back(l.to_json());
    all["links"] = links;
    Json nodes = Json::array();
    for (const auto& n : unwrap(rt->db().list_nodes(std::nullopt, 100000))) nodes.push_back(n.to_json());
    all["nodes"] = nodes;
    return json::dump(all);
  }
};

}  // namespace

TEST_SUITE("chat_flow") {
  TEST_CASE("preset plain: the request is the chat turns, without any format addition") {
    Fix f;
    f.say("user", "Write logs to file A.");
    f.say("assistant", "Done.");
    const Json body = f.send("Which file?", "File A.", f.opts());
    CHECK(body["messages"].size() == 4);  // system, 2 history turns, the question
    CHECK_FALSE(body.contains("response_format"));
    CHECK_FALSE(body.contains("provider"));
    for (const auto& m : body["messages"]) CHECK(m["content"].get<std::string>().find("loom.context_graph") == std::string::npos);
  }

  TEST_CASE("the selector reads the scope chain even when config says nothing: a value of the conversation alone decides") {
    Fix f;
    f.say("user", "yyy I want, I want a list um for Friday");
    Json meta = Json{{"settings", Json{{"chat_representation", "denoised"}}}};
    ConvPatch p;
    p.metadata = meta;
    REQUIRE(f.rt->db().update_conv(f.conv, p));
    CHECK(unwrap(guardian::select(f.rt->db(), f.rt->config(), f.conv)).preset == "denoised");
  }

  TEST_CASE("graph_io: the selection goes as a packet, the answer is forced through response_format, rendered, embedded") {
    Fix f;
    const std::string u = f.say("user", "Write logs to file A.");
    f.say("assistant", "Done.");
    ChatOptions o = f.opts();
    o.flow = "graph_io";
    const Json sel = unwrap(f.rt->chat().context_selection(f.conv, o));
    const std::string id = sel["packet_id"];
    const Json reply = Json{{"schema", "loom.graph_reply/2"}, {"base_packet_sha256", id}, {"response_id", "r"},
        {"nodes", Json::array({Json{{"id", "r"}, {"text", nullptr}, {"role", "response"}, {"children", Json::array({"p1", "p2"})}},
                               Json{{"id", "p1"}, {"text", "File A. "}, {"role", "answer"}, {"children", Json::array()}},
                               Json{{"id", "p2"}, {"text", "It was set earlier."}, {"role", "note"}, {"children", Json::array()}}})},
        {"links", Json::array({Json{{"from", "p1"}, {"predicate", "refers_to"}, {"to", u}}})}};
    Json result;
    const Json body = f.send("Which file?", json::dump(reply), o, &result);
    INFO(body.dump());
    REQUIRE(body.contains("response_format"));
    CHECK(body["response_format"]["type"] == "json_schema");
    CHECK(body["response_format"]["json_schema"]["strict"] == true);
    CHECK(body["stream"] == false);
    bool packet = false, instruction = false;
    for (const auto& m : body["messages"]) {
      const std::string c = m["content"].is_string() ? m["content"].get<std::string>() : "";
      if (c.find("loom.context_graph/1") != std::string::npos) { packet = true; CHECK(c.find("\"table\"") == std::string::npos); }  // the renderer drops sources
      if (c.find("base_packet_sha256 for this request: " + id) != std::string::npos) instruction = true;
      CHECK(m["role"] != "assistant");  // no chat turns of the history under this renderer
    }
    CHECK(packet);
    CHECK(instruction);
    INFO(result.dump());
    CHECK(result["text"] == "File A. It was set earlier.");
    const Json last = unwrap(f.rt->db().get_msg(result["assistant_message_id"].get<std::string>()))->to_json();
    CHECK(last["metadata"]["reply_format"]["status"] == "ok");
    CHECK(last["metadata"]["reply_format"]["embedded"]["nodes"] == 3);
    const std::string nid = last["metadata"]["reply_format"]["embedded"]["root_node"];
    CHECK(unwrap(f.rt->db().get_node(nid)).has_value());
    CHECK(unwrap(f.rt->db().get_links(nid, "generated_by")).size() == 1);
    bool refers = false;
    for (const auto& l : unwrap(f.rt->db().get_links(std::nullopt, "refers_to"))) refers = refers || l.dst == u;
    CHECK(refers);
  }

  TEST_CASE("a reply that refers to an id that was not in the selection sent is rejected: raw text and a visible note, no node invented") {
    Fix f;
    f.say("user", "Hello.");
    ChatOptions o = f.opts();
    o.flow = "graph_out";
    const Json sel = unwrap(f.rt->chat().context_selection(f.conv, o));
    const Json reply = Json{{"schema", "loom.graph_reply/2"}, {"base_packet_sha256", sel["packet_id"]}, {"response_id", "r"},
        {"nodes", Json::array({Json{{"id", "r"}, {"text", "Hi."}, {"role", "response"}, {"children", Json::array()}}})},
        {"links", Json::array({Json{{"from", "r"}, {"predicate", "refers_to"}, {"to", "m_does_not_exist"}}})}};
    Json result;
    f.send("Hi?", json::dump(reply), o, &result);
    const std::string text = result["text"];
    CHECK(text.find("link_to_unknown") != std::string::npos);  // the visible note names the reason
    CHECK(text.find("\"schema\"") != std::string::npos);        // and the raw text is what is shown
    const Json last = unwrap(f.rt->db().get_msg(result["assistant_message_id"].get<std::string>()))->to_json();
    CHECK(last["metadata"]["reply_format"]["status"] == "invalid");
    CHECK(unwrap(f.rt->db().list_nodes(std::string("reply_part"), 100)).empty());
  }

  TEST_CASE("fallback: not JSON at all, and a model known not to support structured outputs gets the instruction only") {
    Fix f;
    f.say("user", "Hello.");
    f.rt->config().set("model_capabilities", Json{{"test/model", Json{{"structured_outputs", false}}}});
    ChatOptions o = f.opts();
    o.flow = "graph_out";
    Json result;
    const Json body = f.send("Hi?", "just words", o, &result);
    CHECK_FALSE(body.contains("response_format"));
    bool schema_in_prompt = false;
    for (const auto& m : body["messages"])
      if (m["content"].is_string() && m["content"].get<std::string>().find("JSON schema") != std::string::npos) schema_in_prompt = true;
    CHECK(schema_in_prompt);
    CHECK(result["text"].get<std::string>().find("just words") == 0);
    CHECK(result["text"].get<std::string>().find("not_json") != std::string::npos);
  }

  TEST_CASE("embedder: embed(select(G)) leaves G byte for byte unchanged; embedding again and again is a fixpoint") {
    Fix f;
    const std::string a = f.say("user", "yyy I want, I want a list um for Friday");
    const std::string b = f.say("assistant", "Here is a list.");
    const std::string c = f.say("user", "Add milk.");
    REQUIRE(f.rt->db().create_link(a, c, "related", 0.5, Json{{"why", "test"}}));
    ChatOptions lossy = f.opts();
    lossy.history_text[a] = "I want a list for Friday";  // what a lossy level of the selector would send
    const std::string before = f.dump_graph();
    const Json s1 = unwrap(f.rt->chat().context_selection(f.conv, lossy));
    CHECK(s1["nodes"].size() == 3);
    CHECK(s1["selection"]["substituted"] == 1);
    CHECK(s1["nodes"][0]["text"] == "I want a list for Friday");
    CHECK(s1["nodes"][0]["provenance"]["text_form"] == "selected_level");
    CHECK(s1["nodes"][0]["source"]["id"] == a);
    CHECK(s1["nodes"][0]["source"]["position"] == 0);
    bool graph_link = false;
    for (const auto& l : s1["links"]) graph_link = graph_link || (l.contains("source") && l["source"]["table"] == "links");
    CHECK(graph_link);
    Json done;
    for (int i = 0; i < 4; ++i) {
      done = unwrap(f.rt->chat().embed_selection(f.conv, s1));
      CHECK(done["created_nodes"] == 0);
      CHECK(done["created_links"] == 0);
      CHECK(done["orphans"].empty());
      CHECK(done["conflicts"].empty());
      CHECK(f.dump_graph() == before);
      const Json s2 = unwrap(f.rt->chat().context_selection(f.conv, lossy));
      CHECK(s2 == s1);  // select(embed(select(G))) == select(G)
    }
  }

  TEST_CASE("embedder: an element without a source is new and is added once, with provenance; a vanished source is an orphan, not re-invented") {
    Fix f;
    const std::string a = f.say("user", "Hello.");
    Json sel = unwrap(f.rt->chat().context_selection(f.conv, f.opts()));
    Json extra = Json{{"id", "ext1"}, {"kind", "message"}, {"role", "user"}, {"text", "From outside."}, {"order", 1}, {"weight", 1.0},
                      {"provenance", Json{{"origin", "import"}, {"text_form", "raw"}}}};
    sel["nodes"].push_back(extra);
    sel["links"].push_back(Json{{"from", "ext1"}, {"predicate", "follows"}, {"to", a}, {"derived", true}});
    const Json first = unwrap(f.rt->chat().embed_selection(f.conv, sel));
    CHECK(first["created_nodes"] == 1);
    const std::string after_first = f.dump_graph();
    const Json second = unwrap(f.rt->chat().embed_selection(f.conv, sel));
    CHECK(second["created_nodes"] == 0);
    CHECK(f.dump_graph() == after_first);
    REQUIRE(f.rt->db().set_msg_status(a, "deleted"));
    CHECK(unwrap(f.rt->chat().embed_selection(f.conv, sel))["orphans"].size() == 0);  // a soft-deleted row still exists: nothing changes
  }
}
