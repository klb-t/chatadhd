// M1-K1c2: the graph perspective 'sent_to_model' (source "receipt"): the record of a request (loom.context_receipt/1, kept with the message the person
// sent) drawn as a graph - the message, one node per item of the record, links sent_to_model with the tokens as weight, what was left out as links
// left_out_of_model. A projection: nothing is written to the nodes or links tables. Synthetic data only.
#include <doctest/doctest.h>

#include <map>
#include <set>

#include "graph/projection.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/effective_view.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/graph_memory.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/perspectives.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json openai_reply(std::string content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 42}}}};
}

// A chat with memory, graph and four earlier turns (like the fixture of test_context_receipt.cpp), a mock model, and the perspective over it.
struct Fixture {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  std::unique_ptr<Database> db = loom::test::open_db(td.path() / "test.db");
  EventBus bus;
  std::unique_ptr<SemanticAnalyzer> analyzer = unwrap(SemanticAnalyzer::create());
  net::ScriptedTransport transport;
  MemoryEngine mem{td.path() / "memory.json"};
  GraphMemorySelector gm{*db, cfg, *analyzer};
  std::string conv_id;
  std::vector<std::string> history_ids;

  Fixture() {
    secrets.set("api_key", "sk-test-key");
    cfg.set("base_url", "https://api.test");
    cfg.set("default_model", "test/model");
    cfg.set("system_prompt", "SYS-S0");
    cfg.set("auto_title", false);
    unwrap(mem.add_node("MEM-S1 core fact"));
    auto old_conv = unwrap(db->create_conv("Old conversation"));
    NewMessage nm; nm.conv_id = old_conv.id; nm.text = "we discussed the database and the API design"; nm.role = "assistant";
    auto old_mid = unwrap(db->create_msg(nm));
    auto topic = unwrap(db->get_or_create_node("tech", "topic"));
    unwrap(db->create_link(old_mid, topic, "tagged_with", 0.5));
    auto cur = unwrap(db->create_conv("Current"));
    conv_id = cur.id;
    for (int i = 1; i <= 4; ++i) {
      NewMessage h; h.conv_id = cur.id; h.text = "HIST-S3 turn " + std::to_string(i); h.role = i % 2 ? "user" : "assistant";
      history_ids.push_back(unwrap(db->create_msg(h)));
    }
  }
  void overlay_chat_context(const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    Json o{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "chat_context"}, {"overrides", overrides}};
    REQUIRE(fsutil::write_file(td.path() / "profiles" / "chat_context.pack", o.dump()));
  }
  void add_window2() {
    const Json base = unwrap(RuntimeProfile::load("chat_context", td.path())).values().at("selector_profiles");
    Json all = base;
    all.push_back(Json{{"id", "window2"}, {"label", Json{{"pl", "w2"}, {"en", "w2"}}}, {"why", Json{{"pl", "test"}, {"en", "test"}}}, {"order", "stability"},
                       {"sources", Json{{"profile", true}, {"rules", true}, {"memory", true}, {"project", true}, {"history", true}, {"graph", true}}},
                       {"breadth", Json{{"nodes", nullptr}, {"search_limit", 0}, {"history_window", 2}}}, {"depth", Json{{"hops", nullptr}}},
                       {"representation", nullptr}, {"flow", nullptr}, {"budget_tokens", nullptr}, {"status", "preset"}, {"note", Json{{"pl", "test"}, {"en", "test"}}}});
    overlay_chat_context(Json{{"selector_profiles", all}});
  }
  // Sends one message; the id of the user message that carries the record (or the plain message).
  std::string send(const ChatOptions& base, const std::string& text = "tell me about the database and API again") {
    transport.expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, openai_reply("ok")));
    ChatEngine e(cfg, secrets, *db, bus, transport, *analyzer, &mem, &gm);
    ChatOptions opts = base;
    opts.conv_id = conv_id;
    opts.stream = false;
    unwrap(e.send(text, opts));
    std::string id;
    for (const auto& m : unwrap(db->get_msgs(conv_id)))
      if (m.role == "user" && m.text == text) id = m.id;
    REQUIRE_FALSE(id.empty());
    return id;
  }
  std::string answer_of(const std::string& request_id) {
    bool seen = false;
    for (const auto& m : unwrap(db->get_msgs(conv_id))) {
      if (m.id == request_id) { seen = true; continue; }
      if (seen && m.role == "assistant") return m.id;
    }
    return "";
  }
  Json receipt_of(const std::string& request_id) {
    const auto m = unwrap(db->get_msg(request_id));
    REQUIRE(m);
    return m->metadata.value("context_trace", Json::object()).value("receipt", Json(nullptr));
  }
  Json render(const Json& request) {
    auto p = unwrap(views::Perspectives::open(td.path()));
    views::RenderContext rc;
    rc.db = db.get();
    rc.cfg = &cfg;
    rc.root = td.path();
    rc.memory = &mem;
    return unwrap(p.render(rc, "sent_to_model", request));
  }
  std::size_t node_rows() { return unwrap(db->list_nodes(std::nullopt, 1000000)).size(); }
  std::size_t link_rows() { return unwrap(db->get_links()).size(); }
};

const Json* node_by_id(const Json& nodes, const std::string& id) {
  for (const auto& n : nodes)
    if (n.value("id", "") == id) return &n;
  return nullptr;
}

}  // namespace

TEST_SUITE("graph_sent_to_model") {

TEST_CASE("the receipt of a request is drawn: the message, one node per item, links sent_to_model with the tokens as weight, derived flags on aggregates") {
  Fixture f;
  ChatOptions o;
  o.guardian_rules = "RULES-R0 never use emoji";
  const std::string req = f.send(o);
  const Json receipt = f.receipt_of(req);
  REQUIRE(receipt.is_object());
  std::size_t items = 0;
  double tokens = 0;
  for (const auto& s : receipt["sections"]) {
    items += s["items"].size();
    for (const auto& i : s["items"]) tokens += i["tokens_est"].get<double>();
  }
  REQUIRE(items >= 5);

  const std::size_t nodes_before = f.node_rows(), links_before = f.link_rows();
  const Json r = f.render(Json{{"msg_id", req}, {"lang", "en"}});
  CHECK(f.node_rows() == nodes_before);  // a projection: nothing is written
  CHECK(f.link_rows() == links_before);

  CHECK(r["schema"] == "loom.perspective_render/1");
  CHECK(r["source"] == "receipt");
  CHECK(r["perspective"] == "sent_to_model");
  CHECK(r["request_msg_id"] == req);
  CHECK(r["receipt"]["present"] == true);
  const Json nodes = r["nodes"], edges = r["edges"];
  // the request is the first node, a real message
  CHECK(nodes[0]["id"] == req);
  CHECK(nodes[0]["kind"] == "message");
  CHECK(nodes[0]["derived"] == false);
  CHECK(nodes[0]["address"] == "msg:" + req.substr(2));

  // one link per item, from the request, weight = the tokens of the item
  std::size_t sent = 0;
  double weight = 0;
  for (const auto& e : edges) {
    if (e["type"] != "sent_to_model") continue;
    ++sent;
    weight += e["weight"].get<double>();
    CHECK(e["src"] == req);
    CHECK(e["metadata"]["class"].is_string());
    CHECK(e["metadata"]["why"].is_string());
    CHECK(e["metadata"]["source_kind"].is_string());
    CHECK(node_by_id(nodes, e["dst"].get<std::string>()) != nullptr);
  }
  CHECK(sent == items);
  CHECK(weight == doctest::Approx(tokens));
  CHECK(r["counts"]["sent"] == items);

  // an aggregate is a derived node with a stable id; its words are data (en here, pl below)
  const Json* sys = node_by_id(nodes, "sent:" + req + ":system_prompt");
  REQUIRE(sys);
  CHECK((*sys)["derived"] == true);
  CHECK((*sys)["metadata"]["derived"] == true);
  CHECK((*sys)["kind"] == "instruction");
  CHECK((*sys)["label"] == "Instruction (system prompt)");
  const Json* question = node_by_id(nodes, "sent:" + req + ":question");
  REQUIRE(question);
  CHECK((*question)["kind"] == "question");
  const Json pl = f.render(Json{{"msg_id", req}, {"lang", "pl-PL"}});
  const Json* sys_pl = node_by_id(pl["nodes"], "sent:" + req + ":system_prompt");
  REQUIRE(sys_pl);
  CHECK((*sys_pl)["label"] == "Instrukcja (prompt systemowy)");
  CHECK(pl["note"].get<std::string>().find("nie jest dowodem") != std::string::npos);

  // an item whose ref is a message of the database is THAT message (the person can go on from it)
  std::size_t real_history = 0;
  for (const auto& s : receipt["sections"])
    if (s["class"] == "history")
      for (const auto& i : s["items"]) {
        const Json* n = node_by_id(nodes, i["ref"].get<std::string>());
        REQUIRE(n);
        CHECK((*n)["derived"] == false);
        CHECK((*n)["kind"] == "message");
        CHECK((*n)["address"] == "msg:" + i["ref"].get<std::string>().substr(2));
        ++real_history;
      }
  CHECK(real_history >= 1);
  CHECK(r["counts"]["resolved_nodes"].get<std::size_t>() >= 1 + real_history);

  // the answer is resolved to its request, exactly as the action context.receipt does
  const std::string ans = f.answer_of(req);
  REQUIRE_FALSE(ans.empty());
  const Json from_answer = f.render(Json{{"msg_id", ans}, {"lang", "en"}});
  CHECK(from_answer["request_msg_id"] == req);
  CHECK(from_answer["msg_id"] == ans);
  CHECK(from_answer["nodes"] == r["nodes"]);
  CHECK(from_answer["edges"] == r["edges"]);
  // the same through the seeds the view engine passes, and through the address form
  const Json by_seed = f.render(Json{{"seeds", Json::array({req})}, {"lang", "en"}});
  CHECK(by_seed["nodes"] == r["nodes"]);
  const Json by_address = f.render(Json{{"msg_id", "msg:" + req.substr(2)}, {"lang", "en"}});
  CHECK(by_address["request_msg_id"] == req);
  // a cap asked for by the request is honoured and said
  const Json capped = f.render(Json{{"msg_id", req}, {"limit", 3}});
  CHECK(capped["nodes"].size() == 3);
  CHECK(capped["truncated"] == true);
}

TEST_CASE("what was left out is visible: links left_out_of_model with the reason, for elements with a ref and for a group without one") {
  Fixture f;
  f.add_window2();
  ChatOptions o;
  o.selector_profile = "window2";  // a window of two earlier messages: two are left out, with their ids
  o.history_skip = {f.history_ids[0]};
  o.history_skip_reason[f.history_ids[0]] = "thread_filter";
  const std::string req = f.send(o);
  const Json receipt = f.receipt_of(req);
  REQUIRE(receipt["dropped"].is_array());
  REQUIRE_FALSE(receipt["dropped"].empty());

  const Json r = f.render(Json{{"msg_id", req}, {"lang", "en"}});
  // the part of the record that says what was NOT sent is in the render, whole
  CHECK(r["dropped"].size() == receipt["dropped"].size());
  std::size_t left_links = 0;
  for (const auto& e : r["edges"])
    if (e["type"] == "left_out_of_model") {
      ++left_links;
      CHECK(e["src"] == req);
      CHECK(e["metadata"]["reason"].is_string());
      CHECK(e["metadata"]["reason_human"].is_string());
    }
  std::size_t expected = 0;
  for (const auto& d : receipt["dropped"]) expected += d["refs"].empty() ? 1 : d["refs"].size();
  CHECK(left_links == expected);
  CHECK(r["counts"]["left_out"] == expected);
  // a message left out by the thread filter is that message, with the reason in words
  const Json* thread = nullptr;
  for (const auto& e : r["edges"])
    if (e["type"] == "left_out_of_model" && e["metadata"]["reason"] == "thread_filter") thread = &e;
  REQUIRE(thread);
  CHECK((*thread)["dst"] == f.history_ids[0]);
  CHECK((*thread)["metadata"]["reason_human"] == "another thread");
  const Json* gone = node_by_id(r["nodes"], f.history_ids[0]);
  REQUIRE(gone);
  CHECK((*gone)["derived"] == false);
  // a group without refs is one derived node, named and reasoned
  ChatOptions compat;
  compat.selector_profile = "compat";  // does not read most sources
  const std::string req2 = f.send(compat, "a second question");
  const Json r2 = f.render(Json{{"msg_id", req2}, {"lang", "en"}});
  bool group_node = false;
  for (const auto& n : r2["nodes"])
    if (n["kind"] == "left_out") {
      group_node = true;
      CHECK(n["derived"] == true);
      CHECK(n["label"].get<std::string>().find("Left out: ") == 0);
    }
  CHECK(group_node);
  // sent and left out are different relations of one perspective
  auto persp = unwrap(views::Perspectives::open(f.td.path()));
  std::set<std::string> kinds;
  for (const auto& e : persp.list())
    if (e["id"] == "sent_to_model")
      for (const auto& k : e["edge_kinds"]) kinds.insert(k.get<std::string>());
  CHECK(kinds == std::set<std::string>{"left_out_of_model", "sent_to_model"});
}

TEST_CASE("a shortened record keeps the sections as nodes and says so; a message with no record answers with the words of context.receipt and an empty graph") {
  {
    Fixture f;
    f.cfg.set("context_receipt_max_bytes", 300);  // the record does not fit: the sections keep their size, not their list
    const std::string req = f.send(ChatOptions{});
    const Json receipt = f.receipt_of(req);
    REQUIRE(receipt["truncated"] == true);
    const Json r = f.render(Json{{"msg_id", req}, {"lang", "en"}});
    CHECK(r["receipt"]["truncated"] == true);
    CHECK(r["receipt"]["truncated_reason"] == "context_receipt_max_bytes");
    std::size_t section_nodes = 0;
    for (const auto& n : r["nodes"])
      if (n["kind"] == "section") {
        ++section_nodes;
        CHECK(n["derived"] == true);
        CHECK(n["metadata"]["items_omitted"].get<int>() >= 1);
      }
    CHECK(section_nodes >= 1);
    for (const auto& e : r["edges"])
      if (e["type"] == "sent_to_model") CHECK(e["metadata"]["truncated"] == true);
  }
  {
    Fixture f;
    f.cfg.set("context_receipt_store", "never");
    const std::string req = f.send(ChatOptions{});
    const std::size_t nodes_before = f.node_rows(), links_before = f.link_rows();
    const Json r = f.render(Json{{"msg_id", req}, {"lang", "en"}});
    CHECK(f.node_rows() == nodes_before);
    CHECK(f.link_rows() == links_before);
    CHECK(r["nodes"].empty());
    CHECK(r["edges"].empty());
    CHECK(r["empty"] == true);
    CHECK(r["missing_reason"] == "before_m1");
    CHECK(r["receipt"]["present"] == false);
    CHECK(r["request_msg_id"] == req);
    // the very words of the action context.receipt (chat_ops.pack), in both languages
    const Json texts = unwrap(RuntimeProfile::load("chat_ops", f.td.path())).values().at("texts");
    CHECK(r["text"] == texts["en"]["context_receipt_missing"]);
    const Json r_pl = f.render(Json{{"msg_id", req}, {"lang", "pl"}});
    CHECK(r_pl["text"] == texts["pl"]["context_receipt_missing"]);
    // a message written before the records existed (no metadata at all) is the same
    NewMessage old; old.conv_id = f.conv_id; old.text = "from long ago"; old.role = "user";
    const std::string old_id = unwrap(f.db->create_msg(old));
    const Json r_old = f.render(Json{{"msg_id", old_id}});
    CHECK(r_old["missing_reason"] == "before_m1");
  }
}

TEST_CASE("the perspective asks for a message that exists") {
  Fixture f;
  auto p = unwrap(views::Perspectives::open(f.td.path()));
  views::RenderContext rc;
  rc.db = f.db.get();
  rc.cfg = &f.cfg;
  rc.root = f.td.path();
  auto none = p.render(rc, "sent_to_model", Json::object());
  REQUIRE_FALSE(none);
  CHECK(none.error().code == Errc::InvalidArgument);
  auto missing = p.render(rc, "sent_to_model", Json{{"msg_id", "m_000000000000"}});
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == Errc::NotFound);
  // the status of the perspective says what it does today
  const auto persp = unwrap(views::effective(f.td.path(), "perspectives"));
  CHECK(persp.find("sent_to_model")->value.at("status").at("state") == "implemented");
}

}  // TEST_SUITE
