#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "chat/active_task_acceptance.h"
#include "chat/active_task_spec.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

constexpr std::uint64_t kRevisionCount = 1001;

Json make_spec(std::string_view conversation_id, std::uint64_t version) {
  const auto product_id = "scale-product-" + std::to_string(version);
  const Json previous = version == 1
      ? Json(nullptr)
      : Json{{"kind", "product"}, {"id", "scale-product-" + std::to_string(version - 1)}};
  const std::string instruction = "Preserve the alternating source evidence.";
  return Json{
      {"schema", "loom.active_task_spec/1"},
      {"product_ref", {{"kind", "product"}, {"id", product_id}}},
      {"goal_id", "acceptance-scale-goal"},
      {"knowledge_run", nullptr},
      {"scope", {{"conversation_id", conversation_id}, {"branch_id", "native:active"},
                 {"task_id", "acceptance-scale-task"}}},
      {"version", version},
      {"previous_product_ref", previous},
      {"known_at", "2026-10-01T00:00:00Z"},
      {"representation", "derived_product"},
      {"materializer", {{"id", "acceptance-scale-test"}, {"version", "1"}}},
      {"history_event_ids", Json::array({"event.0"})},
      {"source_refs", Json::array({Json{
          {"event_id", "event.0"},
          {"locator", {{"source", "synthetic.native.messages"}, {"json_pointer", "/messages/0"}}},
          {"known_at", "2026-09-30T23:59:00Z"},
          {"quote", "SCALE_SOURCE"}}})},
      {"statements", Json::array({Json{
          {"id", "goal"}, {"kind", "goal"}, {"status", "active"},
          {"text", instruction}, {"source_event_ids", Json::array({"event.0"})},
          {"claim_ids", Json::array()}, {"conditions", Json::array()},
          {"supersedes", Json::array()}}})},
      {"compiled_instruction", {
          {"text", instruction},
          {"source_map", Json::array({Json{
              {"span", {{"byte_start", 0}, {"byte_len", instruction.size()}}},
              {"statement_ids", Json::array({"goal"})}}})}}}};
}

Json source_message(bool source_a) {
  const std::string id = source_a ? "source-message-a" : "source-message-b";
  const std::string text = source_a ? "SCALE_SOURCE alpha" : "SCALE_SOURCE beta";
  return Json{{"event_id", "event.0"}, {"message_id", id}, {"role", "user"},
              {"text", text}, {"text_sha256", Sha256::hex(text)}, {"status", "active"},
              {"created", "2026-09-30T23:59:00Z"}, {"attachments", Json::array()},
              {"version_group_id", nullptr}, {"version_num", 1}};
}

Json make_snapshot(std::string_view conversation_id, std::uint64_t version) {
  Json spec = make_spec(conversation_id, version);
  Json compiled = unwrap(chat::compile_active_task_spec(spec));
  const bool current_is_a = version % 2 == 1;
  Json current = source_message(current_is_a);
  Json inherited = Json::array();
  Json coverage = Json::array({current["message_id"]});
  if (version > 1) {
    Json predecessor_source = source_message(!current_is_a);
    inherited.push_back(Json{
        {"product_ref", {{"kind", "product"},
                         {"id", "scale-product-" + std::to_string(version - 1)}}},
        {"source_message", predecessor_source}});
    coverage.push_back(predecessor_source["message_id"]);
  }
  const auto digest = current["text_sha256"];
  return Json{
      {"schema", "loom.chat_active_task/1"},
      {"supplied_spec", spec},
      {"compiled_spec", compiled},
      {"bindings", {{"event.0", {{"message_id", current["message_id"]},
                                    {"text_sha256", digest}}}}},
      {"source_messages", Json::array({current})},
      {"history_coverage_message_ids", coverage},
      {"inherited_source_messages", inherited},
      {"history_mode", "replace_refinement"},
      {"acceptance", "explicit_caller_supplied"},
      {"compiler", {{"id", "loom.active_task_renderer"}, {"version", "1"}}},
      {"binding_verification", "native_message_text_sha256_and_optional_quote"},
      {"source_selection", "native:active"}};
}

Json acceptance_payload(std::uint64_t version, const Json& snapshot,
                        std::string_view origin_suffix = {}) {
  const auto& spec = snapshot["supplied_spec"];
  return Json{{"schema", "loom.chat_active_task_acceptance/1"},
              {"acceptance", "explicit_caller_supplied"},
              {"originating_message_id", "accepting-message-" + std::to_string(version) +
                                             std::string(origin_suffix)},
              {"scope", spec["scope"]},
              {"goal_id", spec["goal_id"]},
              {"product_ref", spec["product_ref"]},
              {"version", spec["version"]},
              {"previous_product_ref", spec["previous_product_ref"]},
              {"accepted_snapshot", snapshot},
              {"snapshot_sha256", Sha256::hex(json::canonical(snapshot))}};
}

}  // namespace

TEST_SUITE("chat_active_task_acceptance_scale") {
  TEST_CASE("durable authority validates alternating evidence across three event pages") {
    fsutil::TempDir dir;
    auto db = unwrap(Database::open(dir.path() / "acceptance-scale.sqlite"));
    EventLog log(*db);
    const std::string conversation_id = "acceptance-scale-conversation";
    const Json baseline{
        {"schema", "loom.chat_active_task_acceptance_baseline/1"},
        {"completed", true},
        {"legacy_rows_observed", 0},
        {"legacy_acceptances_imported", 0},
        {"legacy_originating_message_ids", Json::array()},
        {"last_legacy_acceptance_seq", nullptr},
        {"recovery_boundary",
         "upgrade-time observation of retained top-level metadata; absent or moved legacy rows are not recovered"}};
    CHECK(unwrap(log.append(chat::kActiveTaskBaselineEvent, conversation_id, baseline)) == 1);

    for (std::uint64_t version = 1; version <= kRevisionCount; ++version) {
      Json snapshot = make_snapshot(conversation_id, version);
      REQUIRE(chat::valid_active_task_snapshot(snapshot));
      CHECK(unwrap(log.append(chat::kActiveTaskAcceptedEvent, conversation_id,
                              acceptance_payload(version, snapshot))) ==
            static_cast<std::int64_t>(version + 1));
    }

    auto authority = unwrap(chat::load_active_task_authority(*db, conversation_id));
    REQUIRE(authority.baseline_complete);
    REQUIRE(authority.acceptances.size() == kRevisionCount);
    CHECK(authority.acceptances.front().seq == 2);
    CHECK(authority.acceptances.back().seq == static_cast<std::int64_t>(kRevisionCount + 1));
    const auto& last = authority.acceptances.back().snapshot;
    REQUIRE(last["inherited_source_messages"].size() == 1);
    CHECK(last["inherited_source_messages"][0]["product_ref"]["id"] == "scale-product-1000");
    CHECK(last["history_coverage_message_ids"] ==
          Json::array({"source-message-a", "source-message-b"}));

    // A compatible product may be accepted by more than one user turn. Every
    // acceptance still needs its own evidence validation rather than relying
    // only on the first snapshot retained in the product index.
    Json malformed_duplicate = last;
    malformed_duplicate["inherited_source_messages"][0]["product_ref"]["id"] =
        "wrong-predecessor";
    REQUIRE(chat::valid_active_task_snapshot(malformed_duplicate));
    unwrap(log.append(chat::kActiveTaskAcceptedEvent, conversation_id,
                      acceptance_payload(kRevisionCount, malformed_duplicate, "-duplicate")));
    auto rejected = chat::load_active_task_authority(*db, conversation_id);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().message.find("durable inherited evidence disagrees") !=
          std::string::npos);
  }

  TEST_CASE("a later predecessor cannot retroactively validate an explicit successor") {
    fsutil::TempDir dir;
    auto db = unwrap(Database::open(dir.path() / "acceptance-causal-order.sqlite"));
    EventLog log(*db);
    const std::string conversation_id = "acceptance-causal-order-conversation";
    const Json baseline{
        {"schema", "loom.chat_active_task_acceptance_baseline/1"},
        {"completed", true},
        {"legacy_rows_observed", 0},
        {"legacy_acceptances_imported", 0},
        {"legacy_originating_message_ids", Json::array()},
        {"last_legacy_acceptance_seq", nullptr},
        {"recovery_boundary",
         "upgrade-time observation of retained top-level metadata; absent or moved legacy rows are not recovered"}};
    unwrap(log.append(chat::kActiveTaskBaselineEvent, conversation_id, baseline));

    Json successor = make_snapshot(conversation_id, 2);
    Json predecessor = make_snapshot(conversation_id, 1);
    unwrap(log.append(chat::kActiveTaskAcceptedEvent, conversation_id,
                      acceptance_payload(2, successor)));
    unwrap(log.append(chat::kActiveTaskAcceptedEvent, conversation_id,
                      acceptance_payload(1, predecessor)));

    auto authority = chat::load_active_task_authority(*db, conversation_id);
    REQUIRE_FALSE(authority);
    CHECK(authority.error().code == Errc::InvalidArgument);
    CHECK(authority.error().message.find("predates its predecessor") != std::string::npos);
  }
}
