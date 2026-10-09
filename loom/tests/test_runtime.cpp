#include <doctest/doctest.h>

#include <algorithm>

#include "loom/db.h"
#include "loom/config.h"
#include "loom/event_bus.h"
#include "loom/graph_engine.h"
#include "loom/net/http.h"
#include "loom/relations.h"
#include "loom/runtime.h"
#include "loom/selector.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/semantic_worker.h"
#include "loom/runtime_profile.h"
#include "loom/tasks.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("runtime") {
  TEST_CASE("direct graph and worker retain constructor analyzer and explicit overlay strategies") {
    fsutil::TempDir td;
    auto db = loom::test::open_db(td.path() / "direct.db");
    Config config(td.path() / "config.json");
    Secrets secrets(td.path() / "secrets.json");
    EventBus bus;
    net::ScriptedTransport transport;
    auto rules = AnalyzerRules::builtin();
    rules.entity_patterns = {EntityPattern{"concept", "SYNTHETIC_CONSTRUCTOR_MARKER", 0, 1.0}};
    auto analyzer = unwrap(SemanticAnalyzer::create(rules));
    SemanticLLM llm(config, secrets, transport, *analyzer);
    GraphEngine graph(*db, bus, *analyzer);
    SemanticWorker worker(*db, llm, graph, config, secrets, bus, transport, *analyzer);
    graph.start();
    const auto conversation = unwrap(db->create_conv("direct analyzer strategies"));
    const std::string text = "Synthetic source direct@example.invalid SYNTHETIC_CONSTRUCTOR_MARKER SYNTHETIC_OVERLAY_MARKER";
    for (const char* expected : {"SYNTHETIC_CONSTRUCTOR_MARKER", "SYNTHETIC_OVERLAY_MARKER"}) {
      if (std::string_view(expected) == "SYNTHETIC_OVERLAY_MARKER") {
        LOOM_REQUIRE_OK(fsutil::ensure_dir(td.path() / "profiles"));
        const Json pattern{{"entity_type", "concept"}, {"pattern", expected}, {"flags", Json::array()}, {"confidence", 1.0}};
        LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles/semantic_analyzer.pack",
          Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "semantic_analyzer"},
               {"overrides", {{"rules", {{"entity_patterns", Json::array({pattern})}}}}}}.dump()));
      }
      NewMessage message; message.conv_id = conversation.id; message.role = "user"; message.text = text;
      const auto live = unwrap(db->create_msg(message));
      LOOM_REQUIRE_OK(graph.on_message_checked(Json{{"id", live}, {"text", text}, {"conv_id", conversation.id}}));
      const auto queued = unwrap(db->create_msg(message));
      CHECK(unwrap(worker.drain_once()) == 1);
      const auto expected_node = unwrap(db->find_node(expected, "concept"));
      REQUIRE(expected_node.has_value());
      for (const auto& id : {live, queued}) {
        const auto row = *unwrap(db->get_msg(id));
        CHECK(row.semantic_status == "done");
        CHECK(row.metadata["entity_count"] == 1);
        const auto links = unwrap(db->get_links(id));
        CHECK(std::any_of(links.begin(), links.end(), [&](const Link& link) { return link.dst == expected_node->id; }));
      }
    }
    CHECK_FALSE(unwrap(db->find_node("direct@example.invalid", "email")).has_value());
    CHECK(transport.requests().empty());
  }

  TEST_CASE("A2-BSEM-001 reset and removal reach current builtin in live worker and LLM fallback") {
    for (bool remove_overlay : {false, true}) for (bool use_llm : {false, true}) {
      CAPTURE(remove_overlay);
      CAPTURE(use_llm);
      fsutil::TempDir td;
      const auto root = td.path() / "data";
      const auto overlay_path = root / "profiles/semantic_analyzer.pack";
      LOOM_REQUIRE_OK(fsutil::ensure_dir(overlay_path.parent_path()));
      const std::string marker = "SYNTHETIC_REMOVED_OVERLAY_MARKER";
      const Json pattern{{"entity_type", "concept"}, {"pattern", marker},
                         {"flags", Json::array()}, {"confidence", 1.0}};
      const Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "semantic_analyzer"},
          {"overrides", {{"rules", {{"entity_patterns", Json::array({pattern})}}}}}};
      LOOM_REQUIRE_OK(fsutil::write_file(overlay_path, overlay.dump()));
      const auto initial_hash = unwrap(RuntimeProfile::load("semantic_analyzer", root)).hash();
      auto transport = std::make_shared<net::ScriptedTransport>();
      transport->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Unavailable, "synthetic offline failure"));
      RuntimeOptions options;
      options.data_dir = root.string(); options.start_workers = false; options.http = transport;
      auto runtime = unwrap(Runtime::open(options));
      if (use_llm) {
        runtime->config().set("semantic_model", "synthetic/offline-model");
        runtime->secrets().set("api_key", "synthetic-not-a-credential");
      }
      const auto conversation = unwrap(runtime->db().create_conv("synthetic builtin reset"));
      const std::string text = "Synthetic source reset@example.invalid " + marker;
      auto pending = [&] {
        NewMessage message; message.conv_id = conversation.id; message.role = "user"; message.text = text;
        return unwrap(runtime->db().create_msg(message));
      };
      auto live = [&](const std::string& id) {
        return runtime->graph().on_message_checked(Json{{"id", id}, {"text", text}, {"conv_id", conversation.id}});
      };
      const auto prior = pending();
      LOOM_REQUIRE_OK(live(prior));
      const auto historical = unwrap(runtime->db().get_msg(prior))->to_json();
      CHECK(historical["metadata"]["analyzer_profile_hash"] == initial_hash);
      CHECK(historical["metadata"]["entity_count"] == 1);
      if (remove_overlay) {
        CHECK(std::filesystem::remove(overlay_path));
      } else {
        LOOM_REQUIRE_OK(fsutil::write_file(overlay_path,
          Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "semantic_analyzer"},
               {"overrides", Json::object()}}.dump()));
      }
      CHECK(unwrap(RuntimeProfile::load("semantic_analyzer", root)).is_builtin());
      const auto current_live = pending();
      LOOM_REQUIRE_OK(live(current_live));
      const auto current_worker = pending();
      CHECK(unwrap(runtime->worker().drain_once()) == 1);
      const auto email = unwrap(runtime->db().find_node("reset@example.invalid", "email"));
      const auto old_concept = unwrap(runtime->db().find_node(marker, "concept"));
      REQUIRE(email.has_value()); REQUIRE(old_concept.has_value());
      for (const auto& id : {current_live, current_worker}) {
        const auto message = *unwrap(runtime->db().get_msg(id));
        CHECK(message.semantic_status == "done");
        CHECK(message.metadata["semantic_source"] == "regex");
        CHECK(message.metadata["entity_count"] == 2); // builtin email and mention
        CHECK_FALSE(message.metadata.contains("analyzer_profile_hash"));
        const auto links = unwrap(runtime->db().get_links(id));
        CHECK(std::any_of(links.begin(), links.end(), [&](const Link& link) { return link.dst == email->id; }));
        CHECK(std::none_of(links.begin(), links.end(), [&](const Link& link) { return link.dst == old_concept->id; }));
      }
      // The explicit constructor-bound public API retains its original recipe.
      const auto explicit_analysis = runtime->semantic_llm().analyse(text);
      REQUIRE(explicit_analysis["entities"].size() == 1);
      CHECK(explicit_analysis["entities"][0]["name"] == marker);
      CHECK(runtime->analyzer().profile_hash() == initial_hash);
      CHECK(transport->requests().size() == (use_llm ? 4 : 0));
      CHECK(unwrap(runtime->db().get_msg(prior))->to_json() == historical);
      runtime->shutdown();
      auto reopened = unwrap(Runtime::open(options));
      CHECK(unwrap(reopened->db().get_msg(prior))->to_json() == historical);
      CHECK(unwrap(reopened->db().get_msg(current_worker))->metadata["entity_count"] == 2);
      CHECK(unwrap(reopened->db().count_pending_semantic()) == 0);
    }
  }

  TEST_CASE("effective analyzer data reaches live graph worker and LLM fallback without hidden defaults") {
    fsutil::TempDir td;
    const auto root = td.path() / "data";
    LOOM_REQUIRE_OK(fsutil::ensure_dir(root / "profiles"));
    auto transport = std::make_shared<net::ScriptedTransport>();
    transport->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Unavailable, "synthetic offline provider failure"));
    auto write_overlay = [&](const char* marker) {
      const Json pattern{{"entity_type", "concept"}, {"pattern", marker},
                         {"flags", Json::array()}, {"confidence", 1.0}};
      const Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "semantic_analyzer"},
          {"overrides", {{"rules", {{"entity_patterns", Json::array({pattern})}}}}}};
      LOOM_REQUIRE_OK(fsutil::write_file(root / "profiles/semantic_analyzer.pack", overlay.dump()));
      return unwrap(RuntimeProfile::load("semantic_analyzer", root)).hash();
    };
    const auto initial_hash = write_overlay("SYNTHETIC_INITIAL_MARKER");
    RuntimeOptions options;
    options.data_dir = root.string(); options.start_workers = false; options.http = transport;
    auto runtime = unwrap(Runtime::open(options));
    CHECK(runtime->analyzer().profile_hash() == initial_hash);
    runtime->config().set("semantic_model", "synthetic/offline-model");
    runtime->secrets().set("api_key", "synthetic-not-a-credential");
    const auto conversation = unwrap(runtime->db().create_conv("synthetic analyzer wiring"));
    auto pending = [&](const std::string& text) {
      NewMessage message; message.conv_id = conversation.id; message.role = "user"; message.text = text;
      return unwrap(runtime->db().create_msg(message));
    };
    for (const char* marker : {"SYNTHETIC_INITIAL_MARKER", "SYNTHETIC_UPDATED_MARKER"}) {
      const auto hash = write_overlay(marker);
      const std::string text = std::string("Synthetic analysis input with ") + marker;
      const auto live = pending(text);
      LOOM_REQUIRE_OK(runtime->graph().on_message_checked(Json{{"id", live}, {"text", text}, {"conv_id", conversation.id}}));
      const auto live_row = *unwrap(runtime->db().get_msg(live));
      CHECK(live_row.metadata["semantic"]["analyzer_profile_hash"] == hash);
      CHECK(live_row.metadata["analyzer_profile_hash"] == hash);
      const auto background = pending(text);
      CHECK(unwrap(runtime->worker().drain_once()) == 1);
      const auto worker_row = *unwrap(runtime->db().get_msg(background));
      CHECK(worker_row.semantic_status == "done");
      CHECK(worker_row.metadata["analyzer_profile_hash"] == hash);
      CHECK(worker_row.metadata["entity_count"] == live_row.metadata["entity_count"]);
      CHECK(unwrap(runtime->db().find_node(marker, "concept")).has_value());
    }
    CHECK(transport->requests().size() == 4); // All failures fell back to the pinned data, no network.
    runtime->config().set("semantic_analysis", false);
    const auto batch_hash = write_overlay("SYNTHETIC_BATCH_MARKER");
    const auto first = pending("Synthetic batch input SYNTHETIC_BATCH_MARKER");
    const auto second = pending("Another batch input SYNTHETIC_BATCH_MARKER");
    // A source update during ingestion applies to the NEXT drain, never midway
    // through the current batch. The listener runs on the actual graph bus.
    ScopedSubscription change(runtime->bus(), runtime->bus().on(events::kGraphChanged,
        [&](std::string_view, const Json&) { write_overlay("SYNTHETIC_NEXT_MARKER"); }));
    CHECK(unwrap(runtime->worker().drain_once()) == 2);
    for (const auto& id : {first, second}) {
      const auto row = *unwrap(runtime->db().get_msg(id));
      CHECK(row.metadata["analyzer_profile_hash"] == batch_hash);
      CHECK(row.metadata["entity_count"] == 1);
    }
    change.reset();
    const auto saved = pending("Synthetic pending input SYNTHETIC_UPDATED_MARKER");
    const auto before = transport->requests().size();
    LOOM_REQUIRE_OK(fsutil::write_file(root / "profiles/semantic_analyzer.pack", "invalid source retained"));
    CHECK_FALSE(runtime->worker().drain_once());
    CHECK_FALSE(runtime->graph().on_message_checked(Json{{"id", saved},
        {"text", "Synthetic pending input SYNTHETIC_UPDATED_MARKER"}, {"conv_id", conversation.id}}));
    CHECK(unwrap(runtime->db().get_msg(saved))->semantic_status == "pending");
    CHECK(transport->requests().size() == before);
    runtime->shutdown();
    CHECK_FALSE(Runtime::open(options));
    CHECK(unwrap(fsutil::read_file(root / "profiles/semantic_analyzer.pack")) == "invalid source retained");
    const auto resumed_hash = write_overlay("SYNTHETIC_UPDATED_MARKER");
    auto resumed = unwrap(Runtime::open(options));
    CHECK(unwrap(resumed->worker().drain_once()) == 1);
    CHECK(unwrap(resumed->db().get_msg(saved))->metadata["analyzer_profile_hash"] == resumed_hash);
  }

  TEST_CASE("open wires every subsystem; shutdown is idempotent") {
    fsutil::TempDir td;
    RuntimeOptions o;
    o.data_dir = (td.path() / "data").string();
    o.start_workers = true;
    auto rt = unwrap(Runtime::open(o));
    CHECK(rt->paths().root == td.path() / "data");
    CHECK(std::filesystem::exists(td.path() / "data" / ".chatadhd_data"));
    CHECK(unwrap(rt->db().schema_version()) == 4);
    CHECK(rt->bus().handler_count(events::kMsgCreated) == 1);  // GraphEngine
    CHECK(rt->bus().handler_count(events::kImportDone) == 1);  // SemanticWorker wake
    CHECK(unwrap(rt->relations().get("supersedes")).has_value());
    CHECK(rt->tasks().running());
    CHECK(rt->config().get("theme") == "dark");
    CHECK(!rt->analyzer().rules().entity_patterns.empty());
    CHECK(rt->analyzer().rules().entity_patterns.size() == 11);
    CHECK(rt->analyzer().rules().topics.size() == 7);

    // Tasks run on the runtime's worker pool.
    rt->tasks().register_handler("echo", [](TaskContext& ctx) -> Status {
      ctx.set_result(ctx.params());
      return {};
    });
    std::string id = unwrap(rt->tasks().submit("echo", Json{{"x", 1}}));
    for (int i = 0; i < 300 && unwrap(rt->tasks().get(id))->status != "done"; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(unwrap(rt->tasks().get(id))->status == "done");

    Json info = rt->info();
    CHECK(info["data_dir"] == (td.path() / "data").string());
    CHECK(info["paths"]["db"] == (td.path() / "data" / "chatadhd.db").string());

    rt->shutdown();
    rt->shutdown();
    CHECK(!rt->tasks().running());
  }

  TEST_CASE("HTTP transport can be swapped at runtime") {
    fsutil::TempDir td;
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    auto scripted = std::make_shared<net::ScriptedTransport>();
    scripted->expect("GET", "https://example.test/", net::ScriptedTransport::Reply::json(200, Json{{"ok", true}}));
    o.http = scripted;
    auto rt = unwrap(Runtime::open(o));
    net::HttpRequest req;
    req.url = "https://example.test/ping";
    auto resp = unwrap(rt->http().send(req));
    CHECK(resp.status == 200);
    CHECK(unwrap(resp.json())["ok"] == true);
    CHECK(rt->http().name() == "scripted");
    rt->set_http_transport(nullptr);
    CHECK(rt->http().name() != "scripted");
    CHECK(!rt->tasks().running());
  }

  TEST_CASE("options from JSON") {
    auto o = unwrap(RuntimeOptions::from_json(Json{{"data_dir", "/x"}, {"start_workers", false}, {"log_level", "warning"}}));
    CHECK(*o.data_dir == "/x");
    CHECK(!o.start_workers);
    CHECK(*o.log_level == log::Level::Warning);
    CHECK(!RuntimeOptions::from_json(Json{{"nope", 1}}));
    CHECK(!RuntimeOptions::from_json(Json{{"log_level", "loud"}}));
    CHECK(RuntimeOptions::from_json(Json(nullptr)));
    log::set_level(log::Level::Info);
  }

  TEST_CASE("ScriptedTransport streaming, fallback and helpers") {
    net::ScriptedTransport t;
    t.expect("POST", "https://api.test/chat", net::ScriptedTransport::Reply::sse({"{\"a\":1}", "[DONE]"}));
    std::string streamed;
    int status = 0;
    net::StreamSink sink;
    sink.on_headers = [&](int s, const net::Headers&) {
      status = s;
      return true;
    };
    sink.on_data = [&](std::string_view c) {
      streamed += c;
      return true;
    };
    net::HttpRequest req;
    req.method = "POST";
    req.url = "https://api.test/chat/completions";
    auto r = unwrap(t.send(req, &sink));
    CHECK(status == 200);
    CHECK(r.body.empty());
    CHECK(streamed == "data: {\"a\":1}\n\ndata: [DONE]\n\n");
    CHECK(!t.send(req));  // no expectation left, no fallback
    t.set_fallback(net::ScriptedTransport::Reply::fail(Errc::Timeout, "slow"));
    auto f = t.send(req);
    REQUIRE(!f);
    CHECK(f.error().code == Errc::Timeout);
    CHECK(t.requests().size() == 3);

    CancelToken tok;
    tok.cancel();
    t.expect("", "https://", net::ScriptedTransport::Reply::text(200, "x"));
    CHECK(t.send(req, nullptr, &tok).error().code == Errc::Cancelled);

    auto u = unwrap(net::parse_url("https://user@api.example.com:8443/v1/models?x=1#frag"));
    CHECK(u.scheme == "https");
    CHECK(u.host == "api.example.com");
    CHECK(u.port == 8443);
    CHECK(u.target == "/v1/models?x=1");
    CHECK(unwrap(net::parse_url("http://h")).target == "/");
    CHECK(unwrap(net::parse_url("http://h")).port == 80);
    CHECK(!net::parse_url("nohost"));
    CHECK(net::url_encode("a b/ż") == "a%20b%2F%C5%BC");
    CHECK(net::form_urlencode({{"k", "a b"}, {"x", "1&2"}}) == "k=a+b&x=1%262");
    CHECK(net::with_query("https://x/y", {{"key", "v"}}) == "https://x/y?key=v");
    CHECK(net::with_query("https://x/y?a=1", {{"key", "v"}}) == "https://x/y?a=1&key=v");
    auto mp = net::build_multipart({{"model", "", "", "whisper"}, {"file", "audio.wav", "audio/wav", "RIFF"}});
    CHECK(mp.content_type.rfind("multipart/form-data; boundary=", 0) == 0);
    CHECK(mp.body.find("name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\nRIFF\r\n") !=
          std::string::npos);
    auto hr = unwrap(net::HttpRequest::from_json(req.to_json()));
    CHECK(hr.url == req.url);
    CHECK(hr.method == "POST");
    CHECK(net::header_value({{"Content-Type", "x"}}, "content-type") == "x");
  }
}
