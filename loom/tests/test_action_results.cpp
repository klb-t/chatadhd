// P10 A: the result of an action run lands in the graph (GRAF_HUB_KOD_SILNIK §5-§6): a form node of the class of its
// modality, `expresses` a content node shared per source element, `derived_from` the source, `produced_by` a run node;
// large text goes to the blob store; where results go is a setting with presets, scoped like the rules; provenance
// survives a restart; the chat result message links to the produced node.
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

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id;
  std::string message_id;

  Fixture() {
    open();
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    message_id = add("user", "We should ship on Friday.");
    add("assistant", "Agreed, Friday works if the tests pass.");
  }
  void open() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
  }
  std::string add(const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv_id;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  void script(int n, const std::string& content = "A short summary.") {
    for (int i = 0; i < n; ++i)
      transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply(content)));
  }
  void overlay(const std::string& domain, const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(td.path() / "profiles" / (domain + ".pack"))
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
  }
  Json summarise(const std::string& element, const Json& context = Json::object()) {
    return unwrap(rt->actions().run(Json{{"action", "summarise"}, {"element", element}, {"context", context}, {"ref", "test"}}));
  }
  std::vector<Link> links(const std::string& node, const std::string& type) { return unwrap(rt->db().get_links(node, type)); }
  std::size_t nodes_of(const std::string& kind) { return unwrap(rt->db().list_nodes(kind, 1000)).size(); }
  std::size_t all_nodes() { return unwrap(rt->db().list_nodes(std::nullopt, 1000)).size(); }
};

const Json& produced(const Json& run) { return run.at("result").at("produced"); }

}  // namespace

TEST_SUITE("action_results") {
  TEST_CASE("a result is a form node that expresses the content of its source, derived from it and produced by a run node") {
    Fixture f;
    f.script(1);
    const Json run = f.summarise(f.message_id, Json{{"conv_id", f.conv_id}});
    REQUIRE(run.at("status") == "completed");
    const Json& p = produced(run);
    INFO(p.dump());
    CHECK(p.at("target") == "graph");
    CHECK(p.at("target_scope").at("scope") == "installation");
    REQUIRE(p.at("items").size() == 1);
    const std::string form = p.at("items").at(0).at("form");
    const std::string content = p.at("content");
    const std::string run_node = p.at("run_node");
    CHECK(p.at("source") == f.message_id);
    CHECK(p.at("content_reused") == false);
    CHECK(p.at("issues").empty());

    const Node form_node = *unwrap(f.rt->db().get_node(form));
    CHECK(form_node.kind == "text_form");  // the class of its modality
    CHECK(form_node.content == "A short summary.");
    CHECK(form_node.metadata.at("run_id") == run.at("id"));
    CHECK(form_node.metadata.at("action") == "summarise");
    CHECK(form_node.metadata.at("step") == "a1.s1");
    CHECK(form_node.metadata.at("source") == f.message_id);
    CHECK(form_node.metadata.at("sha256") == Sha256::hex("A short summary."));

    const Node content_node = *unwrap(f.rt->db().get_node(content));
    CHECK(content_node.kind == "content");
    CHECK(content_node.label == "content:" + f.message_id);

    const Node run_n = *unwrap(f.rt->db().get_node(run_node));
    CHECK(run_n.kind == "action_run");
    CHECK(run_n.metadata.at("run_id") == run.at("id"));
    CHECK(run_n.metadata.at("action") == "summarise");
    CHECK(run_n.metadata.at("step") == "a1.s1");
    CHECK(run_n.metadata.at("model") == "test/model");
    CHECK(run_n.metadata.at("adapter") == "openai_chat");
    // nothing was held, but every admitted call has the number of its usage receipt (operation_id), and the run names the last one
    const Json call = run_n.metadata.at("steps").at(0).at("calls").at(0);
    REQUIRE(call.at("operation_id").is_string());
    CHECK(call.at("operation_id").get<std::string>().rfind("model_usage_", 0) == 0);
    CHECK(run_n.metadata.at("usage_receipt_id") == call.at("operation_id"));
    CHECK(run_n.metadata.at("usage_operation_ids") == Json::array({call.at("operation_id")}));
    CHECK(run_n.metadata.at("usage_receipt_ids").empty());  // the user confirmed nothing
    CHECK(run_n.metadata.at("steps").at(0).at("calls").at(0).at("request_sha256").is_string());
    // References and hashes only: never a secret, never the bytes of a request.
    const std::string everything = run_n.metadata.dump() + form_node.metadata.dump();
    CHECK(everything.find("sk-test-key") == std::string::npos);
    CHECK(everything.find("We should ship") == std::string::npos);

    const auto expresses = f.links(form, "expresses");
    REQUIRE(expresses.size() == 1);
    CHECK(expresses[0].src == form);
    CHECK(expresses[0].dst == content);
    CHECK(expresses[0].weight == doctest::Approx(0.6));  // summarise condenses: the fidelity says so
    CHECK(expresses[0].metadata.at("lost") == Json::array({"detail"}));
    CHECK(expresses[0].metadata.at("content_op") == "condense");
    const auto derived = f.links(form, "derived_from");
    REQUIRE(derived.size() == 1);
    CHECK(derived[0].dst == f.message_id);
    const auto by = f.links(form, "produced_by");
    REQUIRE(by.size() == 1);
    CHECK(by[0].dst == run_node);
    CHECK(by[0].metadata.at("step") == "a1.s1");
    CHECK(by[0].metadata.at("model") == "test/model");
    // The source is a form of the same content (fidelity 1): a search for the content finds both.
    const auto source_link = f.links(f.message_id, "expresses");
    REQUIRE(source_link.size() == 1);
    CHECK(source_link[0].dst == content);
    CHECK(source_link[0].weight == 1.0);
  }

  TEST_CASE("a second run on the same source reuses its content node and rewrites nothing") {
    Fixture f;
    f.script(2);
    const Json first = f.summarise(f.message_id);
    const std::string content = produced(first).at("content");
    const Node before = *unwrap(f.rt->db().get_node(content));
    const auto source_before = f.links(f.message_id, "expresses");
    REQUIRE(source_before.size() == 1);

    const Json second = f.summarise(f.message_id);
    CHECK(produced(second).at("content") == content);
    CHECK(produced(second).at("content_reused") == true);
    CHECK(f.nodes_of("content") == 1);
    CHECK(f.nodes_of("text_form") == 2);  // each run is its own form
    CHECK(f.nodes_of("action_run") == 2);
    const Node after = *unwrap(f.rt->db().get_node(content));
    CHECK(after.to_json() == before.to_json());
    const auto source_after = f.links(f.message_id, "expresses");
    REQUIRE(source_after.size() == 1);
    CHECK(source_after[0].id == source_before[0].id);
    CHECK(source_after[0].created == source_before[0].created);
    CHECK(f.links(content, "expresses").size() == 3);  // the source and the two results
    // Another source has its own content node.
    f.script(1);
    const Json other = f.summarise(f.conv_id);
    CHECK(produced(other).at("content") != content);
    CHECK(f.nodes_of("content") == 2);
  }

  TEST_CASE("a large result goes to the blob store and the run document keeps a reference; the original is still hashed") {
    Fixture f;
    const std::string big(3000, 'a');
    const std::string source = f.add("user", std::string(3000, 's'));  // as big as the answer: the usage guard sees no growth
    f.script(1, big);
    const Json run = f.summarise(source);
    REQUIRE(run.at("status") == "completed");
    const Json& part = run.at("result").at("parts").at(0);
    CHECK(part.at("stored") == "blob");
    CHECK(part.at("text") == "");
    CHECK(part.at("size") == 3000);
    CHECK(part.at("preview").get<std::string>().size() == 160);
    const std::string hash = part.at("blob_hash");
    CHECK(unwrap(f.rt->blobs().read(hash)) == big);
    CHECK(unwrap(actions::part_text(&f.rt->blobs(), part)) == big);
    // The same reference stands in every copy of the output in the document.
    CHECK(run.at("steps").at(0).at("outputs").at(0) == part);
    CHECK(run.at("steps").at(0).at("variants").at(0).at("parts").at(0) == part);
    // The step's hash is the hash of what the model returned, not of the reference.
    CHECK(run.at("steps").at(0).at("outputs_sha256") ==
          Sha256::hex(json::canonical(Json::array({Json{{"kind", "text"}, {"text", big}}}))));
    // The graph carries the reference too: the form node has no inline text, its blob node holds the bytes.
    const Json& item = produced(run).at("items").at(0);
    const Node form = *unwrap(f.rt->db().get_node(item.at("form").get<std::string>()));
    CHECK(form.content.empty());
    CHECK(form.metadata.at("blob_hash") == hash);
    CHECK(form.metadata.at("size") == 3000);
    CHECK(form.metadata.at("source") == source);
    const auto stored = f.links(form.id, "stored_as");
    REQUIRE(stored.size() == 1);
    const Node blob_node = *unwrap(f.rt->db().get_node(stored[0].dst));
    CHECK(blob_node.kind == "blob");
    CHECK(blob_node.label == "blob:" + hash);
    // A later action reads the stored text back through the node.
    f.script(1);
    const Json again = unwrap(f.rt->actions().run(Json{{"action", "summarise"}, {"element", form.id}}));
    REQUIRE(again.at("status") == "completed");
    CHECK(f.transport->requests().at(1).body.find(big) != std::string::npos);

    // The threshold is data: raised, nothing moves; null, never.
    Fixture g;
    g.overlay("action_results", Json{{"inline_max_bytes", nullptr}});
    g.script(1, big);
    const Json inlined = g.summarise(g.message_id);
    CHECK(inlined.at("result").at("parts").at(0).at("text") == big);
    CHECK_FALSE(inlined.at("result").at("parts").at(0).contains("blob_hash"));
    Fixture h;
    h.overlay("action_results", Json{{"inline_max_bytes", 10}});
    h.script(1, "twelve chars");
    CHECK(h.summarise(h.message_id).at("result").at("parts").at(0).at("stored") == "blob");
  }

  TEST_CASE("target run_only: the result stays in the run document, the graph is untouched; large text still goes to a blob") {
    Fixture f;
    f.rt->config().set("action_result_target", "run_only");
    const std::string source = f.add("user", std::string(3000, 's'));
    f.script(1);
    const Json run = f.summarise(source);
    CHECK(produced(run).at("target") == "run_only");
    CHECK(produced(run).at("graph") == false);
    CHECK(produced(run).at("items").empty());
    CHECK(produced(run).at("target_scope").at("scope") == "person");
    CHECK(produced(run).at("target_scope").at("source") == "setting:action_result_target");
    CHECK(run.at("result").at("parts").at(0).at("text") == "A short summary.");
    CHECK(f.all_nodes() == 0);
    CHECK(f.links(source, "expresses").empty());
    f.script(1, std::string(3000, 'b'));
    const Json big = f.summarise(source);
    CHECK(big.at("result").at("parts").at(0).at("stored") == "blob");
    CHECK(f.all_nodes() == 0);
  }

  TEST_CASE("target conversation_message: the text is posted as a message and that message is the form node") {
    Fixture f;
    f.rt->config().set("action_result_target", "conversation_message");
    f.script(2);
    const std::size_t before = unwrap(f.rt->db().get_msgs(f.conv_id)).size();
    const Json run = f.summarise(f.message_id, Json{{"conv_id", f.conv_id}});
    const Json& item = produced(run).at("items").at(0);
    const std::string posted = item.at("message_id");
    CHECK(item.at("form") == posted);
    const auto msgs = unwrap(f.rt->db().get_msgs(f.conv_id));
    REQUIRE(msgs.size() == before + 1);
    CHECK(msgs.back().id == posted);
    CHECK(msgs.back().role == "assistant");
    CHECK(msgs.back().text == "A short summary.");
    CHECK(msgs.back().model.value_or("") == "loom:actions");
    CHECK(msgs.back().metadata.at("action_result").at("run_id") == run.at("id"));
    CHECK_FALSE(msgs.back().metadata.contains("action_run"));  // a result is content, not command traffic
    CHECK(f.nodes_of("text_form") == 0);  // the message is the form: no second node
    CHECK(f.links(posted, "expresses").size() == 1);
    CHECK(f.links(posted, "derived_from").at(0).dst == f.message_id);
    CHECK(f.links(posted, "produced_by").at(0).dst == produced(run).at("run_node").get<std::string>());
    // A run that belongs to no conversation cannot post: it says so and keeps a node.
    NodeOptions idea;
    idea.content = "A loose idea, in no conversation.";
    const std::string node = unwrap(f.rt->db().create_node("Idea", "text_form", idea));
    f.script(1);
    const Json homeless = f.summarise(node, Json{{"origin", "cli"}});
    CHECK(homeless.at("status") == "completed");
    CHECK(produced(homeless).at("issues").at(0).at("code") == "no_conversation");
    CHECK(f.nodes_of("text_form") == 2);  // the idea itself and the result
  }

  TEST_CASE("the target resolves through the scope chain: installation preset, the user's setting, a rule on one conversation") {
    Fixture f;
    f.rt->config().set("action_result_target", "run_only");
    const std::string other = unwrap(f.rt->db().create_conv("Other")).id;
    f.overlay("action_rules",
              Json{{"rules", Json{{"c_graph", Json{{"id", "c_graph"}, {"scope", "conversation"}, {"scope_id", f.conv_id},
                                                    {"target", Json{{"kind", "setting"}, {"key", "action_results.target"}}},
                                                    {"effect", "set_value"}, {"value", "graph"}, {"initiator", "the user, in this chat"}}}}}});
    f.script(2);
    const Json here = f.summarise(f.message_id, Json{{"conv_id", f.conv_id}});
    CHECK(produced(here).at("target") == "graph");
    CHECK(produced(here).at("target_scope").at("scope") == "conversation");
    CHECK(produced(here).at("target_scope").at("scope_id") == f.conv_id);
    CHECK(produced(here).at("target_scope").at("rule") == "c_graph");
    CHECK(produced(here).at("target_scope").at("initiator") == "the user, in this chat");
    const Json there = f.summarise(f.message_id, Json{{"conv_id", other}});
    CHECK(produced(there).at("target") == "run_only");
    CHECK(produced(there).at("target_scope").at("scope") == "person");
    CHECK(f.nodes_of("text_form") == 1);
    // A preset nobody defined is said, not guessed: the result stays in the run.
    f.rt->config().set("action_result_target", "everywhere");
    f.script(1);
    const Json odd = f.summarise(f.message_id, Json{{"conv_id", other}});
    CHECK(odd.at("status") == "completed");
    CHECK(produced(odd).at("issues").at(0).at("code") == "target_unknown");
    CHECK(f.nodes_of("text_form") == 1);
  }

  TEST_CASE("a user preset in the overlay is one more entry, not code") {
    Fixture f;
    f.overlay("action_results", Json{{"target", Json{{"presets", Json{{"graph_and_message", Json{{"graph", true}, {"message", true}}}}}, {"active", "graph_and_message"}}}});
    f.script(1);
    const Json run = f.summarise(f.message_id, Json{{"conv_id", f.conv_id}});
    CHECK(produced(run).at("target") == "graph_and_message");
    CHECK_FALSE(produced(run).at("items").at(0).at("message_id").is_null());
  }

  TEST_CASE("only the steps that produced a storable part are stored; json results and credentials never reach the graph") {
    Fixture f;
    const Json get = unwrap(f.rt->actions().run(Json{{"action", "settings.get"}, {"args", Json{{"key", "default_model"}}}}));
    CHECK(get.at("status") == "completed");
    CHECK(produced(get).at("items").empty());
    CHECK(produced(get).at("run_node").is_null());
    const Json secret = unwrap(f.rt->actions().run(Json{{"action", "settings.get"}, {"args", Json{{"key", "api_key"}}}}));
    CHECK(secret.at("status") == "completed");
    CHECK_FALSE(secret.at("result").contains("produced"));  // a run on a credential stores nothing anywhere
    CHECK(f.all_nodes() == 0);
    // A chain: with steps = result one result is stored, with steps = all every step's.
    f.script(2);
    const Json chain = unwrap(f.rt->actions().run(Json{{"chain", Json::array({"summarise", "summarise"})}, {"element", f.message_id}}));
    REQUIRE(chain.at("status") == "completed");
    CHECK(produced(chain).at("items").size() == 1);
    Fixture g;
    g.overlay("action_results", Json{{"steps", "all"}});
    g.script(2);
    const Json all = unwrap(g.rt->actions().run(Json{{"chain", Json::array({"summarise", "summarise"})}, {"element", g.message_id}}));
    REQUIRE(all.at("status") == "completed");
    REQUIRE(produced(all).at("items").size() == 2);
    CHECK(produced(all).at("items").at(0).at("step") != produced(all).at("items").at(1).at("step"));
    CHECK(g.nodes_of("action_run") == 1);  // one run node, both results hang on it
    CHECK(g.nodes_of("text_form") == 2);
    CHECK(g.links(produced(all).at("run_node"), "produced_by").size() == 2);
  }

  TEST_CASE("provenance survives a restart: the document, the nodes, the links and the blob are all still there") {
    Fixture f;
    const std::string big(3000, 'z');
    f.script(1, big);
    const Json run = f.summarise(f.message_id, Json{{"conv_id", f.conv_id}});
    const std::string run_id = run.at("id");
    const std::string form = produced(run).at("items").at(0).at("form");
    const std::string content = produced(run).at("content");
    const std::string run_node = produced(run).at("run_node");
    LOOM_REQUIRE_OK(f.rt->config().save());
    LOOM_REQUIRE_OK(f.rt->secrets().save());
    f.rt.reset();
    f.open();
    const Json again = unwrap(f.rt->actions().status(Json{{"run_id", run_id}}));
    CHECK(again.at("status") == "completed");
    CHECK(produced(again) == produced(run));
    const Node n = *unwrap(f.rt->db().get_node(form));
    CHECK(n.kind == "text_form");
    CHECK(n.metadata.at("run_id") == run_id);
    CHECK(unwrap(f.rt->blobs().read(n.metadata.at("blob_hash").get<std::string>())) == big);
    CHECK(f.links(form, "expresses").at(0).dst == content);
    CHECK(f.links(form, "derived_from").at(0).dst == f.message_id);
    CHECK(f.links(form, "produced_by").at(0).dst == run_node);
    CHECK(unwrap(f.rt->db().get_node(run_node))->metadata.at("run_id") == run_id);
    CHECK(unwrap(actions::part_text(&f.rt->blobs(), again.at("result").at("parts").at(0))) == big);
    // And the content node is still shared by the next run after the restart.
    f.script(1);
    CHECK(produced(f.summarise(f.message_id)).at("content") == content);
  }

  TEST_CASE("the chat's result message links to the produced node; with conversation_message the posted message is the answer") {
    Fixture f;
    f.script(1);
    ChatOptions opts;
    opts.conv_id = f.conv_id;
    opts.stream = false;
    auto input = [&](const std::string& text) {
      Json req{{"message", text}};
      return unwrap(chat_route::handle(*f.rt, text, opts, req, nullptr));
    };
    const auto proposed = input("Summarise this conversation");
    REQUIRE(proposed.handled);
    const auto done = input("yes");
    REQUIRE(done.handled);
    const Message result = *unwrap(f.rt->db().get_msg(done.assistant_message_id));
    const Json& run_meta = result.metadata.at("action_run");
    REQUIRE(run_meta.at("produced").at("forms").size() == 1);
    const std::string form = run_meta.at("produced").at("forms").at(0);
    CHECK(run_meta.at("produced").at("target") == "graph");
    CHECK(result.text.find("A short summary.") != std::string::npos);
    const auto presented = f.links(result.id, "references");
    REQUIRE(presented.size() == 1);
    CHECK(presented[0].dst == form);
    CHECK(unwrap(f.rt->db().get_node(form))->kind == "text_form");

    Fixture g;
    g.rt->config().set("action_result_target", "conversation_message");
    g.script(1);
    ChatOptions gopts;
    gopts.conv_id = g.conv_id;
    gopts.stream = false;
    auto ginput = [&](const std::string& text) {
      Json req{{"message", text}};
      return unwrap(chat_route::handle(*g.rt, text, gopts, req, nullptr));
    };
    ginput("Summarise this conversation");
    const std::size_t before = unwrap(g.rt->db().get_msgs(g.conv_id)).size();
    const auto answered = ginput("yes");
    const auto msgs = unwrap(g.rt->db().get_msgs(g.conv_id));
    REQUIRE(msgs.size() == before + 2);  // the user's yes and ONE answer: the posted result
    CHECK(msgs.back().id == answered.assistant_message_id);
    CHECK(msgs.back().text == "A short summary.");
    CHECK(msgs.back().metadata.contains("action_result"));
    CHECK(g.nodes_of("text_form") == 0);
    CHECK(g.links(msgs.back().id, "expresses").size() == 1);
  }

  TEST_CASE("a long result reaches the chat in full: the router reads the text back from the blob store") {
    Fixture f;
    const std::string big = std::string(1500, 'p') + " " + std::string(1500, 'q');
    f.script(1, big);
    ChatOptions opts;
    opts.conv_id = f.conv_id;
    opts.stream = false;
    auto input = [&](const std::string& text) {
      Json req{{"message", text}};
      return unwrap(chat_route::handle(*f.rt, text, opts, req, nullptr));
    };
    input("Summarise this conversation");
    const auto done = input("yes");
    const Message result = *unwrap(f.rt->db().get_msg(done.assistant_message_id));
    CHECK(result.text.find(big) != std::string::npos);
  }
}
