#include "active_task_acceptance.h"
#include "active_task_spec.h"
#include <limits>
#include <set>
#include "loom/provenance.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::chat {
namespace {
Error invalid(std::string_view reason) {
  return Error(Errc::InvalidArgument, "active_task acceptance: " + std::string(reason));
}
constexpr auto kAccepted = "chat.active_task.accepted.v1";
constexpr auto kLegacy = "chat.active_task.legacy_observed.v1";
constexpr auto kBaseline = "chat.active_task.baseline.v1";
constexpr auto kBoundary = "visible_top_level_metadata_all_statuses_at_upgrade";
}

std::string active_task_scope_key(const Json& scope) {
  return json::dump(Json::array({scope.at("conversation_id"), scope.at("branch_id"), scope.at("task_id")}));
}

bool valid_active_task_snapshot(const Json& retained) {
  // Validate the complete emitted representation, not only its current text
  // bindings: inherited evidence and coverage also control history replacement.
  if (!retained.is_object() || retained.size() != 12 ||
      retained.value("schema", Json()) != "loom.chat_active_task/1" ||
      !retained.contains("supplied_spec") || !retained.contains("compiled_spec") ||
      !retained.contains("bindings") || !retained["bindings"].is_object() ||
      !retained.contains("source_messages") || !retained["source_messages"].is_array() ||
      !retained.contains("history_coverage_message_ids") || !retained["history_coverage_message_ids"].is_array() ||
      !retained.contains("inherited_source_messages") || !retained["inherited_source_messages"].is_array() ||
      (retained.value("history_mode", Json()) != "replace_refinement" &&
       retained.value("history_mode", Json()) != "append") ||
      retained.value("acceptance", Json()) != "explicit_caller_supplied" ||
      retained.value("compiler", Json()) != Json{{"id", "loom.active_task_renderer"}, {"version", "1"}} ||
      retained.value("binding_verification", Json()) != "native_message_text_sha256_and_optional_quote" ||
      retained.value("source_selection", Json()) != "native:active") return false;
  auto compiled = chat::compile_active_task_spec(retained["supplied_spec"]);
  if (!compiled || *compiled != retained["compiled_spec"] ||
      (*compiled)["scope"]["branch_id"] != "native:active") return false;
  const auto& events = (*compiled)["history_event_ids"];
  const auto& bindings = retained["bindings"];
  if (bindings.size() != events.size() || retained["source_messages"].size() != events.size()) return false;
  auto valid_source = [](const Json& source) {
    if (!source.is_object() || source.size() != 10 ||
        !source.contains("event_id") || !source["event_id"].is_string() ||
        source["event_id"].get_ref<const std::string&>().empty() ||
        !source.contains("message_id") || !source["message_id"].is_string() ||
        source["message_id"].get_ref<const std::string&>().empty() ||
        !source.contains("text") || !source["text"].is_string() ||
        !source.contains("text_sha256") || !source["text_sha256"].is_string() ||
        !source.contains("role") || !source["role"].is_string() ||
        source.value("status", Json()) != "active" ||
        !source.contains("created") || !source["created"].is_string() ||
        !source.contains("attachments") ||
        !source.contains("version_group_id") ||
        (!source["version_group_id"].is_null() && !source["version_group_id"].is_string()) ||
        !source.contains("version_num") || !source["version_num"].is_number_integer() ||
        (source["version_num"].is_number_unsigned() &&
         source["version_num"].get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
      return false;
    // Attachments retain any supported native JSON value, including null.
    const auto& text = source["text"].get_ref<const std::string&>();
    return utf8::is_valid(text) && source["text_sha256"] == Sha256::hex(text);
  };
  std::set<std::string> event_ids;
  std::set<std::string> message_ids;
  std::map<std::string, std::string> source_texts;
  Json expected_coverage = Json::array();
  for (const auto& event : events) event_ids.insert(event.get<std::string>());
  for (const auto& source : retained["source_messages"]) {
    if (!valid_source(source)) return false;
    const auto eid = source["event_id"].get<std::string>();
    const auto mid = source["message_id"].get<std::string>();
    if (event_ids.erase(eid) != 1 || !message_ids.insert(mid).second) return false;
    source_texts.emplace(eid, source["text"].get<std::string>());
    auto binding = bindings.find(eid);
    if (binding == bindings.end() || !binding->is_object() || binding->size() != 2 ||
        !binding->contains("message_id") || (*binding)["message_id"] != mid ||
        !binding->contains("text_sha256") || (*binding)["text_sha256"] != source["text_sha256"]) return false;
    expected_coverage.push_back(mid);
  }
  for (const auto& source : (*compiled)["source_refs"]) {
    auto text = source_texts.find(source["event_id"].get<std::string>());
    if (text == source_texts.end() || (source.contains("quote") &&
        text->second.find(source["quote"].get<std::string>()) == std::string::npos)) return false;
  }
  for (const auto& inherited : retained["inherited_source_messages"]) {
    if (!inherited.is_object() || inherited.size() != 2 || !inherited.contains("product_ref") ||
        !inherited["product_ref"].is_object() || inherited["product_ref"].size() != 2 ||
        inherited["product_ref"].value("kind", Json()) != "product" ||
        !inherited["product_ref"].contains("id") || !inherited["product_ref"]["id"].is_string() ||
        inherited["product_ref"]["id"].get_ref<const std::string&>().empty() ||
        !inherited.contains("source_message") || !valid_source(inherited["source_message"])) return false;
    const auto mid = inherited["source_message"]["message_id"].get<std::string>();
    if (!message_ids.insert(mid).second) return false;
    expected_coverage.push_back(mid);
  }
  return event_ids.empty() && retained["history_coverage_message_ids"] == expected_coverage;
}

bool same_active_task_identity(const Json& a, const Json& b) {
  return a["supplied_spec"] == b["supplied_spec"] && a["compiled_spec"] == b["compiled_spec"] &&
      a["bindings"] == b["bindings"] && a["source_messages"] == b["source_messages"] &&
      a["inherited_source_messages"] == b["inherited_source_messages"] &&
      a["history_coverage_message_ids"] == b["history_coverage_message_ids"];
}

namespace {
// Validate every recovered scope, not just the scope of the next request.
Status validate_chains(const Json& snapshots) {
  std::map<std::string, const Json*> products;
  std::map<std::string, std::map<std::uint64_t, std::string>> scopes;
  for (const auto& snapshot : snapshots) {
    if (!valid_active_task_snapshot(snapshot)) return invalid("malformed retained snapshot");
    const auto& spec = snapshot["supplied_spec"];
    const auto id = spec["product_ref"]["id"].get<std::string>();
    auto [at, added] = products.emplace(id, &snapshot);
    if (!added && !same_active_task_identity(*at->second, snapshot))
      return invalid("conflicting retained product identity");
    auto& versions = scopes[active_task_scope_key(spec["scope"])];
    auto [version, first] = versions.emplace(spec["version"].get<std::uint64_t>(), id);
    if (!first && version->second != id) return invalid("competing retained version identities");
  }
  for (const auto& [scope, versions] : scopes) {
    (void)scope;
    std::uint64_t expected = 1;
    const Json* previous = nullptr;
    for (const auto& [version, id] : versions) {
      const auto& spec = (*products.at(id))["supplied_spec"];
      if (version != expected || (!previous && !spec["previous_product_ref"].is_null()) ||
          (previous && (spec["previous_product_ref"] != (*previous)["product_ref"] ||
                        spec["goal_id"] != (*previous)["goal_id"])))
        return invalid("incomplete or inconsistent retained ancestry");
      previous = &spec;
      if (expected != std::numeric_limits<std::uint64_t>::max()) ++expected;
    }
  }
  // Reconstruct inheritance from the validated graph. A self-consistent hash
  // and coverage array cannot certify invented or cross-scope source evidence.
  for (const auto& [id, snapshot] : products) {
    (void)id;
    Json expected = Json::array();
    std::set<std::string> covered;
    for (const auto& source : (*snapshot)["source_messages"])
      covered.insert(source["message_id"].get<std::string>());
    auto previous = (*snapshot)["supplied_spec"]["previous_product_ref"];
    while (!previous.is_null()) {
      const auto& ancestor = *products.at(previous["id"].get<std::string>());
      for (const auto& source : ancestor["source_messages"]) {
        if (covered.insert(source["message_id"].get<std::string>()).second)
          expected.push_back(Json{{"product_ref", ancestor["supplied_spec"]["product_ref"]},
                                  {"source_message", source}});
      }
      previous = ancestor["supplied_spec"]["previous_product_ref"];
    }
    if ((*snapshot)["inherited_source_messages"] != expected)
      return invalid("inherited evidence disagrees with accepted ancestry");
  }
  return ok_status();
}

// Legacy observations are an unordered recovery set. After their completed
// baseline, explicit acceptances must extend or replay the head at that event.
Status advance_head(std::map<std::string, Json>& heads, const Json& snapshot) {
  const auto& spec = snapshot["supplied_spec"];
  const auto key = active_task_scope_key(spec["scope"]);
  auto old = heads.find(key);
  if (old == heads.end()) {
    if (spec["version"] != 1 || !spec["previous_product_ref"].is_null())
      return invalid("acceptance precedes its version-one root");
  } else {
    const auto& prior = old->second["supplied_spec"];
    if (spec["product_ref"] == prior["product_ref"]) {
      if (!same_active_task_identity(snapshot, old->second))
        return invalid("replay changes accepted product evidence");
    } else {
      const auto version = prior["version"].get<std::uint64_t>();
      if (version == std::numeric_limits<std::uint64_t>::max() ||
          spec["version"].get<std::uint64_t>() != version + 1 ||
          spec["previous_product_ref"] != prior["product_ref"] || spec["goal_id"] != prior["goal_id"])
        return invalid("acceptance does not extend the head at its event sequence");
    }
  }
  heads[key] = snapshot;
  return ok_status();
}

Json envelope(std::string_view message_id, const Json& snapshot, bool legacy) {
  return Json{{"schema", "loom.chat_active_task_acceptance/1"},
    {"acceptance", legacy ? "legacy_retention_observed" : "explicit_caller_supplied"},
    {"originating_message_id", message_id}, {"accepted_snapshot", snapshot},
    {"snapshot_sha256", Sha256::hex(json::dump(snapshot))}};
}
}

Result<ActiveTaskHistory> read_active_task_history(Database& db, std::string_view conversation,
    const std::map<std::string, Json>& in_flight) {
  ActiveTaskHistory history;
  EventLog log(db);
  EventQuery query;
  query.type = "chat.active_task.*";
  query.subject_id = std::string(conversation);
  std::size_t legacy_records = 0;
  bool any_events = false;
  std::map<std::string, Json> heads;
  while (true) {
    auto page = log.query(query);
    if (!page) return page.error();
    if (page->empty()) break;
    for (const auto& event : *page) {
      any_events = true;
      if (event.seq <= query.after_seq) return invalid("event sequence did not advance");
      query.after_seq = event.seq;
      const auto& payload = event.payload;
      if (!payload.is_object()) return invalid("malformed event payload");
      if (event.type == "chat.active_task.acceptance_baseline.v1" ||
          (event.type == kAccepted && payload.size() == 10 && payload.contains("scope")))
        return invalid("incompatible external acceptance journal; an explicit migration is required");
      if (event.type == kBaseline) {
        if (history.baseline_complete || payload.size() != 3 ||
            !payload.contains("legacy_records") || !payload["legacy_records"].is_number_integer() ||
            payload.value("schema", Json()) != "loom.chat_active_task_baseline/1" ||
            payload.value("evidence_boundary", Json()) != kBoundary ||
            payload.value("legacy_records", Json()) != Json(legacy_records))
          return invalid("malformed or duplicate legacy baseline");
        LOOM_TRY(validate_chains(history.snapshots));
        for (const auto& snapshot : history.snapshots) {
          const auto& spec = snapshot["supplied_spec"];
          auto& head = heads[active_task_scope_key(spec["scope"])];
          if (head.is_null() || spec["version"] > head["supplied_spec"]["version"]) head = snapshot;
        }
        history.baseline_complete = true;
        continue;
      }
      const bool legacy = event.type == kLegacy;
      if ((!legacy && event.type != kAccepted) || (legacy == history.baseline_complete) || payload.size() != 5 ||
          payload.value("schema", Json()) != "loom.chat_active_task_acceptance/1" ||
          payload.value("acceptance", Json()) != (legacy ? "legacy_retention_observed" : "explicit_caller_supplied") ||
          !payload.contains("originating_message_id") || !payload["originating_message_id"].is_string() ||
          payload["originating_message_id"].get_ref<const std::string&>().empty() ||
          !payload.contains("accepted_snapshot") || !valid_active_task_snapshot(payload["accepted_snapshot"]))
        return invalid("malformed or out-of-order acceptance event");
      const auto& snapshot = payload["accepted_snapshot"];
      if (snapshot["supplied_spec"]["scope"]["conversation_id"] != conversation ||
          payload.value("snapshot_sha256", Json()) != Sha256::hex(json::dump(snapshot)))
        return invalid("acceptance event scope or digest mismatch");
      if (!legacy) LOOM_TRY(advance_head(heads, snapshot));
      const auto mid = payload["originating_message_id"].get<std::string>();
      auto [at, added] = history.projections.emplace(mid, snapshot);
      if (!added && at->second != snapshot) return invalid("originating message has conflicting accepted snapshots");
      history.snapshots.push_back(snapshot);
      if (legacy) ++legacy_records;
    }
  }
  if (any_events && !history.baseline_complete) return invalid("incomplete legacy baseline");
  auto messages = db.get_msgs(conversation, true);
  if (!messages) return messages.error();
  for (const auto& message : *messages) {
    if (in_flight.contains(message.id) || !message.metadata.is_object()) continue;
    auto retained = message.metadata.find("active_task");
    if (retained == message.metadata.end()) continue;
    if (history.baseline_complete) {
      // A present corrupted projection of a known acceptance is observable;
      // missing, moved, or newly injected projections never change authority.
      auto known = history.projections.find(message.id);
      if (known != history.projections.end() && known->second != *retained)
        return invalid("accepted message metadata diverges from its durable snapshot");
    } else {
      if (message.role != "user") return invalid("legacy retained acceptance is attached to a non-user row");
      if (!valid_active_task_snapshot(*retained) ||
          (*retained)["supplied_spec"]["scope"]["conversation_id"] != conversation)
        return invalid("malformed legacy retained snapshot");
      history.projections.emplace(message.id, *retained);
      history.snapshots.push_back(*retained);
    }
  }
  LOOM_TRY(validate_chains(history.snapshots));
  return history;
}

Status append_active_task_acceptance(Database& db, std::string_view conversation,
    std::string_view message_id, const Json& snapshot, const ActiveTaskHistory& history) {
  if (!db.conn().in_transaction()) return invalid("acceptance requires an enclosing write transaction");
  EventLog log(db);
  if (!history.baseline_complete) {
    for (const auto& [mid, legacy] : history.projections) {
      auto appended = log.append(kLegacy, conversation, envelope(mid, legacy, true));
      if (!appended) return appended.error();
    }
    auto baseline = log.append(kBaseline, conversation,
      Json{{"schema", "loom.chat_active_task_baseline/1"}, {"legacy_records", history.projections.size()},
        {"evidence_boundary", kBoundary}});
    if (!baseline) return baseline.error();
  }
  auto appended = log.append(kAccepted, conversation, envelope(message_id, snapshot, false));
  if (!appended) return appended.error();
  return ok_status();
}
}  // namespace loom::chat
