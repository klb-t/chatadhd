#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/event_bus.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

// Independent synthetic callback counterexamples: these do not use a model,
// exported conversations, or evaluation fixtures.
struct RetentionFixture {
  fsutil::TempDir dir;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;
  Json spec;
  Json bindings;

  RetentionFixture() {
    RuntimeOptions setup;
    setup.data_dir = dir.path().string();
    setup.start_workers = false;
    setup.http = transport;
    rt = unwrap(Runtime::open(setup));
    rt->config().set("base_url", "https://retention.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("semantic_analysis", false);
    rt->secrets().set("api_key", "retention-test-key");
    conv = unwrap(rt->db().create_conv("Synthetic retention")).id;
    NewMessage source;
    source.conv_id = conv;
    source.role = "user";
    source.text = "Prepare a concise report.";
    auto source_id = unwrap(rt->db().create_msg(source));
    bindings = Json{{"source.1", {{"message_id", source_id}, {"text_sha256", Sha256::hex(source.text)}}}};
    spec = Json{
      {"schema", "loom.active_task_spec/1"},
      {"product_ref", {{"kind", "product"}, {"id", "retention.product.1"}}},
      {"goal_id", "retention.goal"}, {"knowledge_run", nullptr},
      {"scope", {{"conversation_id", conv}, {"branch_id", "native:active"}, {"task_id", "retention.task"}}},
      {"version", 1}, {"previous_product_ref", nullptr}, {"known_at", "2026-09-30T20:00:00Z"},
      {"representation", "derived_product"},
      {"materializer", {{"id", "synthetic.retention"}, {"version", "1"}}},
      {"history_event_ids", Json::array({"source.1"})},
      {"source_refs", Json::array({Json{{"event_id", "source.1"},
        {"locator", {{"source", "synthetic.native"}}},
        {"known_at", "2026-09-30T19:00:00Z"}, {"quote", source.text}}})},
      {"statements", Json::array({Json{{"id", "goal"}, {"kind", "goal"}, {"status", "active"},
        {"text", source.text}, {"source_event_ids", Json::array({"source.1"})},
        {"claim_ids", Json::array()}, {"conditions", Json::array()}, {"supersedes", Json::array()}}})},
      {"compiled_instruction", {{"text", "placeholder"}, {"source_map", Json::array({Json{
        {"span", {{"byte_start", 0}, {"byte_len", 11}}}, {"statement_ids", Json::array({"goal"})}}})}}}
    };
  }

  ChatOptions options(bool trace) const {
    return unwrap(ChatOptions::from_json(Json{{"conv_id", conv}, {"stream", false},
      {"include_memory", false}, {"include_graph_memory", false}, {"trace_context", trace},
      {"active_task_spec", spec}, {"active_task_bindings", bindings}}));
  }

  void reply(int status = 200) {
    transport->expect("POST", "https://retention.test/chat/completions",
      net::ScriptedTransport::Reply::json(status, Json{{"choices", Json::array({
        Json{{"message", {{"content", "Synthetic response."}}}}})}}));
  }

  Json metadata(std::string_view id) {
    auto message = unwrap(rt->db().get_msg(id));
    REQUIRE(message);
    return message->metadata;
  }
};

}  // namespace

TEST_SUITE("chat_active_task_retention") {
  TEST_CASE("callback replacement cannot erase accepted task even without trace or after provider failure") {
    for (bool trace : {false, true}) {
      for (int status : {200, 503}) {
        RetentionFixture f;
        std::string user_id;
        ChatCallbacks cb;
        cb.on_start = [&](std::string_view, std::string_view id) {
          user_id = id;
          MsgPatch patch;
          patch.metadata = Json{{"callback_note", "Keep this unrelated enrichment."}};
          LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
        };
        f.reply(status);
        auto result = f.rt->chat().send("Continue.", f.options(trace), cb);
        CHECK(static_cast<bool>(result) == (status == 200));
        REQUIRE(f.transport->requests().size() == 1);
        const auto metadata = f.metadata(user_id);
        CHECK(metadata["active_task"]["supplied_spec"] == f.spec);
        CHECK(metadata["active_task"]["bindings"] == f.bindings);
        CHECK(metadata["callback_note"] == "Keep this unrelated enrichment.");
        CHECK(metadata.contains("context_trace") == trace);
        CHECK(metadata["loom_active_task_metadata_recovery"]["previous_active_task_present"] == false);
        if (trace) CHECK(metadata["context_trace"]["active_task"] == metadata["active_task"]);
      }
    }
  }

  TEST_CASE("message-created and on-start enrichment preserve task and their own fields") {
    RetentionFixture f;
    ScopedSubscription sub(f.rt->bus(), f.rt->bus().on(events::kMsgCreated,
      [&](std::string_view, const Json& event) {
        if (event["role"] != "user") return;
        auto id = event["id"].get<std::string>();
        auto metadata = f.metadata(id);
        metadata["event_note"] = "event enrichment";
        MsgPatch patch;
        patch.metadata = metadata;
        LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
      }));
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view id) {
      auto metadata = f.metadata(id);
      metadata["start_note"] = "start enrichment";
      MsgPatch patch;
      patch.metadata = metadata;
      LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
    };
    f.reply();
    auto result = unwrap(f.rt->chat().send("Continue.", f.options(false), cb));
    const auto metadata = f.metadata(result.user_message_id);
    CHECK(metadata["active_task"]["supplied_spec"] == f.spec);
    CHECK(metadata["event_note"] == "event enrichment");
    CHECK(metadata["start_note"] == "start enrichment");
    CHECK_FALSE(metadata.contains("loom_active_task_metadata_recovery"));
    CHECK_FALSE(metadata.contains("context_trace"));
  }

  TEST_CASE("conflicting callback task and preexisting recovery value are preserved as evidence") {
    for (bool trace : {false, true}) {
      RetentionFixture f;
      const Json conflicting{{"schema", "callback.task"}, {"value", "not the accepted task"}};
      const Json prior_recovery = Json::array({"a preexisting value", 17});
      ChatCallbacks cb;
      cb.on_start = [&](std::string_view, std::string_view id) {
        MsgPatch patch;
        patch.metadata = Json{{"active_task", conflicting},
          {"loom_active_task_metadata_recovery", prior_recovery}, {"unrelated", "keep"}};
        LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
      };
      f.reply();
      auto result = unwrap(f.rt->chat().send("Continue.", f.options(trace), cb));
      const auto metadata = f.metadata(result.user_message_id);
      CHECK(metadata["active_task"]["supplied_spec"] == f.spec);
      CHECK(metadata["unrelated"] == "keep");
      const auto recovery = metadata["loom_active_task_metadata_recovery"];
      CHECK(recovery["previous_active_task_present"] == true);
      CHECK(recovery["previous_active_task"] == conflicting);
      CHECK(recovery["previous_recovery"] == prior_recovery);
      if (trace) CHECK(result.context_trace["active_task"] == metadata["active_task"]);
    }
  }

  TEST_CASE("nonobject callback metadata survives alongside the accepted task") {
    for (const Json& replacement : {Json("callback raw metadata"), Json::array({"callback", 9})}) {
      RetentionFixture f;
      ChatCallbacks cb;
      cb.on_start = [&](std::string_view, std::string_view id) {
        MsgPatch patch;
        patch.metadata = replacement;
        LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
      };
      f.reply();
      auto result = unwrap(f.rt->chat().send("Continue.", f.options(false), cb));
      const auto metadata = f.metadata(result.user_message_id);
      CHECK(metadata["loom_preserved_metadata"] == replacement);
      CHECK(metadata["active_task"]["supplied_spec"] == f.spec);
    }
  }

  TEST_CASE("current turn identity changes fail before transport even with trace disabled") {
    for (bool trace : {false, true}) {
      for (const std::string field : {"text", "role", "conversation", "attachments"}) {
        RetentionFixture f;
        std::string user_id;
        const auto other = unwrap(f.rt->db().create_conv("Other synthetic conversation")).id;
        ChatCallbacks cb;
        cb.on_start = [&](std::string_view, std::string_view id) {
          user_id = id;
          MsgPatch patch;
          if (field == "text") patch.text = "Changed current text.";
          if (field == "role") patch.role = "assistant";
          if (field == "conversation") patch.conv_id = other;
          if (field == "attachments") patch.attachments = Json::array({"changed-attachment.txt"});
          patch.metadata = Json{{"callback_note", "preserve despite changed current turn"}};
          LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
        };
        auto result = f.rt->chat().send("Continue.", f.options(trace), cb);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == Errc::InvalidArgument);
        CHECK(f.transport->requests().empty());
        const auto message = unwrap(f.rt->db().get_msg(user_id));
        REQUIRE(message);
        if (field == "text") CHECK(message->text == "Changed current text.");
        if (field == "role") CHECK(message->role == "assistant");
        if (field == "conversation") CHECK(message->conv_id == other);
        if (field == "attachments") CHECK(message->attachments == Json::array({"changed-attachment.txt"}));
        CHECK(message->metadata["active_task"]["supplied_spec"] == f.spec);
        CHECK(message->metadata["callback_note"] == "preserve despite changed current turn");
      }
    }
  }

  TEST_CASE("callback metadata replacement does not permit a second version-one product") {
    RetentionFixture f;
    ChatCallbacks cb;
    cb.on_start = [&](std::string_view, std::string_view id) {
      MsgPatch patch;
      patch.metadata = Json::object();
      LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
    };
    f.reply();
    unwrap(f.rt->chat().send("Accept first.", f.options(false), cb));
    f.spec["product_ref"]["id"] = "unexpected.restarted.product";
    auto result = f.rt->chat().send("Do not restart this task.", f.options(false));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::InvalidArgument);
    CHECK(f.transport->requests().size() == 1);
  }

  TEST_CASE("temporarily absent callback metadata cannot admit a reentrant competing version one") {
    for (bool trace : {false, true}) {
      RetentionFixture f;
      const auto original = f.options(trace);
      ChatCallbacks cb;
      bool nested_rejected = false;
      cb.on_start = [&](std::string_view, std::string_view id) {
        MsgPatch patch;
        patch.metadata = Json::object();
        LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
        auto nested = original;
        (*nested.active_task_spec)["product_ref"]["id"] = "competing.in.flight.v1";
        const auto before = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
        auto attempt = f.rt->chat().send("Competing version one.", nested);
        REQUIRE_FALSE(attempt);
        nested_rejected = attempt.error().code == Errc::InvalidArgument;
        CHECK(f.transport->requests().empty());
        CHECK(unwrap(f.rt->db().get_msgs(f.conv, true)).size() == before);
      };
      f.reply();
      auto outer = unwrap(f.rt->chat().send("Original accepted version.", original, cb));
      CHECK(nested_rejected);
      CHECK(f.metadata(outer.user_message_id)["active_task"]["supplied_spec"] == f.spec);
      CHECK(f.transport->requests().size() == 1);
    }
  }

  TEST_CASE("a legitimate reentrant successor extends the canonical in-flight acceptance") {
    for (bool trace : {false, true}) {
      RetentionFixture f;
      const auto original = f.options(trace);
      auto successor = original;
      (*successor.active_task_spec)["version"] = 2;
      (*successor.active_task_spec)["previous_product_ref"] = f.spec["product_ref"];
      (*successor.active_task_spec)["product_ref"]["id"] = "legitimate.in.flight.v2";
      (*successor.active_task_spec)["statements"][0]["text"] = "Prepare the concise report with references.";
      std::string nested_id;
      ChatCallbacks cb;
      cb.on_start = [&](std::string_view, std::string_view id) {
        MsgPatch patch;
        patch.metadata = Json{{"callback_note", "temporary projection rewrite"}};
        LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
        auto nested = unwrap(f.rt->chat().send("Accept the legitimate successor.", successor));
        nested_id = nested.user_message_id;
        CHECK(f.metadata(nested_id)["active_task"]["supplied_spec"] == *successor.active_task_spec);
      };
      f.reply();
      f.reply();
      auto outer = unwrap(f.rt->chat().send("Original accepted version.", original, cb));
      CHECK_FALSE(nested_id.empty());
      CHECK(f.metadata(outer.user_message_id)["active_task"]["supplied_spec"] == f.spec);
      CHECK(f.metadata(nested_id)["active_task"]["supplied_spec"] == *successor.active_task_spec);
      CHECK(f.transport->requests().size() == 2);
      auto stale = f.rt->chat().send("Do not roll back.", original);
      REQUIRE_FALSE(stale);
      CHECK(stale.error().code == Errc::InvalidArgument);
      CHECK(f.transport->requests().size() == 2);
    }
  }

  TEST_CASE("in-flight authority ends on success provider failure and callback exception") {
    for (const std::string outcome : {"success", "provider_failure", "callback_throw"}) {
      RetentionFixture f;
      std::string user_id;
      ChatCallbacks cb;
      cb.on_start = [&](std::string_view, std::string_view id) {
        user_id = id;
        if (outcome == "callback_throw") {
          MsgPatch patch;
          patch.metadata = Json{{"callback_note", "preserve even though the callback throws"}};
          LOOM_REQUIRE_OK(f.rt->db().update_msg(id, patch));
          throw std::runtime_error("synthetic callback exception");
        }
      };
      if (outcome == "callback_throw") {
        bool threw = false;
        try {
          (void)f.rt->chat().send("Accepted before callback.", f.options(false), cb);
        } catch (const std::runtime_error& error) {
          threw = true;
          CHECK(std::string(error.what()) == "synthetic callback exception");
        }
        CHECK(threw);
      } else {
        f.reply(outcome == "success" ? 200 : 503);
        auto result = f.rt->chat().send("Accepted before callback.", f.options(false), cb);
        CHECK(static_cast<bool>(result) == (outcome == "success"));
      }
      REQUIRE_FALSE(user_id.empty());
      CHECK(f.metadata(user_id)["active_task"]["supplied_spec"] == f.spec);
      if (outcome == "callback_throw") {
        CHECK(f.metadata(user_id)["callback_note"] == "preserve even though the callback throws");
        CHECK(f.transport->requests().empty());
      }
      // Direct writes after a completed send are intentionally not covered by
      // the transient guard. A leaked map would wrongly mask this corruption.
      auto metadata = f.metadata(user_id);
      metadata["active_task"] = nullptr;
      MsgPatch patch;
      patch.metadata = metadata;
      LOOM_REQUIRE_OK(f.rt->db().update_msg(user_id, patch));
      const auto before = f.transport->requests().size();
      auto attempt = f.rt->chat().send("Reject corrupt retained lineage.", f.options(false));
      REQUIRE_FALSE(attempt);
      CHECK(attempt.error().code == Errc::InvalidArgument);
      CHECK(f.transport->requests().size() == before);
    }
  }

  TEST_CASE("reentrant knowledge builder acceptance prevents persistence of the outer stale version") {
    RetentionFixture f;
    const auto original = f.options(false);
    auto outer_options = original;
    outer_options.knowledge_context = context::ContextRequest{};
    auto nested_options = original;
    (*nested_options.active_task_spec)["product_ref"]["id"] = "accepted.inside.knowledge.builder";
    std::string nested_id;
    int builder_calls = 0;
    f.rt->chat().set_knowledge_context_builder([&](const context::ContextRequest&) -> Result<Json> {
      ++builder_calls;
      auto nested = unwrap(f.rt->chat().send("Nested accepted version.", nested_options));
      nested_id = nested.user_message_id;
      return Json{{"prompt", "Synthetic independent context."}, {"context_set", Json::object()}};
    });
    f.reply();
    const auto before = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
    auto outer = f.rt->chat().send("This outer version has become stale.", outer_options);
    REQUIRE_FALSE(outer);
    CHECK(outer.error().code == Errc::InvalidArgument);
    CHECK(builder_calls == 1);
    CHECK_FALSE(nested_id.empty());
    CHECK(f.metadata(nested_id)["active_task"]["supplied_spec"] == *nested_options.active_task_spec);
    CHECK(f.transport->requests().size() == 1);
    auto rows = unwrap(f.rt->db().get_msgs(f.conv, true));
    CHECK(rows.size() == before + 2);  // Only the nested user/assistant pair.
    for (const auto& row : rows) CHECK(row.text != "This outer version has become stale.");
  }

  TEST_CASE("knowledge builder source mutations invalidate the prepared acceptance before persistence") {
    for (const std::string field : {"text", "role"}) {
      RetentionFixture f;
      const auto source_id = f.bindings["source.1"]["message_id"].get<std::string>();
      auto options = f.options(false);
      options.knowledge_context = context::ContextRequest{};
      f.rt->chat().set_knowledge_context_builder([&](const context::ContextRequest&) -> Result<Json> {
        MsgPatch patch;
        if (field == "text") patch.text = "Changed during context preparation.";
        if (field == "role") patch.role = "assistant";
        LOOM_REQUIRE_OK(f.rt->db().update_msg(source_id, patch));
        return Json{{"prompt", "Synthetic independent context."}, {"context_set", Json::object()}};
      });
      const auto before = unwrap(f.rt->db().get_msgs(f.conv, true)).size();
      auto rejected = f.rt->chat().send("Do not accept stale source snapshots.", options);
      REQUIRE_FALSE(rejected);
      CHECK(rejected.error().code == Errc::InvalidArgument);
      CHECK(f.transport->requests().empty());
      CHECK(unwrap(f.rt->db().get_msgs(f.conv, true)).size() == before);
    }
  }
}
