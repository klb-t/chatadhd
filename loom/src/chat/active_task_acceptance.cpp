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

bool valid_active_task_snapshot(const Json& retained) {
  if (!retained.is_object() || retained.value("schema", Json()) != "loom.chat_active_task/1" ||
      !retained.contains("supplied_spec") || !retained.contains("compiled_spec") ||
      !retained.contains("bindings") || !retained["bindings"].is_object() ||
      !retained.contains("source_messages") || !retained["source_messages"].is_array()) return false;
  auto compiled = chat::compile_active_task_spec(retained["supplied_spec"]);
  if (!compiled || *compiled != retained["compiled_spec"]) return false;
  const auto& events = (*compiled)["history_event_ids"];
  const auto& bindings = retained["bindings"];
  if (bindings.size() != events.size() || retained["source_messages"].size() != events.size()) return false;
  std::set<std::string> event_ids;
  std::set<std::string> message_ids;
  std::map<std::string, std::string> source_texts;
  for (const auto& event : events) event_ids.insert(event.get<std::string>());
  for (const auto& source : retained["source_messages"]) {
    if (!source.is_object() || !source.contains("event_id") || !source["event_id"].is_string() ||
        !source.contains("message_id") || !source["message_id"].is_string() ||
        !source.contains("text") || !source["text"].is_string() ||
        !source.contains("text_sha256") || !source["text_sha256"].is_string()) return false;
    const auto eid = source["event_id"].get<std::string>();
    const auto mid = source["message_id"].get<std::string>();
    if (!utf8::is_valid(source["text"].get_ref<const std::string&>())) return false;
    if (event_ids.erase(eid) != 1 || mid.empty() || !message_ids.insert(mid).second) return false;
    source_texts.emplace(eid, source["text"].get<std::string>());
    auto binding = bindings.find(eid);
    if (binding == bindings.end() || !binding->is_object() || binding->size() != 2 ||
        !binding->contains("message_id") || (*binding)["message_id"] != mid ||
        !binding->contains("text_sha256") || (*binding)["text_sha256"] != source["text_sha256"] ||
        source["text_sha256"] != Sha256::hex(source["text"].get<std::string>())) return false;
  }
  for (const auto& source : (*compiled)["source_refs"]) {
    if (source.contains("quote") &&
        source_texts.at(source["event_id"].get<std::string>()).find(source["quote"].get<std::string>()) == std::string::npos) {
      return false;
    }
  }
  return event_ids.empty();
}

bool same_active_task_identity(const Json& a, const Json& b) {
  return a["supplied_spec"] == b["supplied_spec"] && a["compiled_spec"] == b["compiled_spec"] &&
      a["bindings"] == b["bindings"] && a["source_messages"] == b["source_messages"];
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
    auto& versions = scopes[json::dump(spec["scope"])];
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
  while (true) {
    auto page = log.query(query);
    if (!page) return page.error();
    if (page->empty()) break;
    for (const auto& event : *page) {
      any_events = true;
      query.after_seq = event.seq;
      const auto& payload = event.payload;
      if (!payload.is_object()) return invalid("malformed event payload");
      if (event.type == kBaseline) {
        if (history.baseline_complete || payload.value("schema", Json()) != "loom.chat_active_task_baseline/1" ||
            payload.value("evidence_boundary", Json()) != kBoundary ||
            payload.value("legacy_records", Json()) != Json(legacy_records))
          return invalid("malformed or duplicate legacy baseline");
        LOOM_TRY(validate_chains(history.snapshots));
        history.baseline_complete = true;
        continue;
      }
      const bool legacy = event.type == kLegacy;
      if ((!legacy && event.type != kAccepted) || (legacy == history.baseline_complete) ||
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
