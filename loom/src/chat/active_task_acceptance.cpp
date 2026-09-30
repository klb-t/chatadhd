#include "active_task_acceptance.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

#include "active_task_spec.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::chat {
namespace {

constexpr int kEventPageSize = 500;

Error invalid_authority(std::string_view reason) {
  return Error(Errc::InvalidArgument, "active_task: malformed durable acceptance authority: " +
                                          std::string(reason));
}

Result<std::vector<EventRecord>> all_events(EventLog& log, std::string_view type,
                                            std::string_view conversation_id) {
  std::vector<EventRecord> out;
  std::int64_t after = 0;
  while (true) {
    EventQuery query;
    query.after_seq = after;
    query.type = std::string(type);
    query.subject_id = std::string(conversation_id);
    query.limit = kEventPageSize;
    LOOM_TRY_ASSIGN(auto page, log.query(query));
    if (page.empty()) break;
    for (auto& event : page) {
      if (event.seq <= after) return invalid_authority("event sequence did not advance");
      after = event.seq;
      out.push_back(std::move(event));
    }
    if (page.size() < static_cast<std::size_t>(kEventPageSize)) break;
  }
  return out;
}

Result<ActiveTaskAcceptance> parse_acceptance(const EventRecord& event,
                                              std::string_view conversation_id) {
  const auto& payload = event.payload;
  if (!payload.is_object() || payload.size() != 10 ||
      payload.value("schema", Json()) != "loom.chat_active_task_acceptance/1" ||
      !payload.contains("acceptance") || !payload["acceptance"].is_string() ||
      !payload.contains("originating_message_id") || !payload["originating_message_id"].is_string() ||
      payload["originating_message_id"].get_ref<const std::string&>().empty() ||
      !payload.contains("scope") || !payload["scope"].is_object() ||
      !payload.contains("goal_id") || !payload["goal_id"].is_string() ||
      !payload.contains("product_ref") || !payload["product_ref"].is_object() ||
      !payload.contains("version") || !payload["version"].is_number_unsigned() ||
      !payload.contains("previous_product_ref") ||
      !payload.contains("accepted_snapshot") || !payload["accepted_snapshot"].is_object() ||
      !payload.contains("snapshot_sha256") || !payload["snapshot_sha256"].is_string()) {
    return invalid_authority("accepted event payload shape");
  }
  const auto acceptance = payload["acceptance"].get<std::string>();
  if (acceptance != "explicit_caller_supplied" && acceptance != "legacy_retention_observed") {
    return invalid_authority("unknown acceptance provenance");
  }
  const auto& snapshot = payload["accepted_snapshot"];
  if (!valid_active_task_snapshot(snapshot)) return invalid_authority("invalid accepted snapshot");
  const auto& spec = snapshot["supplied_spec"];
  if (spec["scope"] != payload["scope"] || spec["goal_id"] != payload["goal_id"] ||
      spec["product_ref"] != payload["product_ref"] || spec["version"] != payload["version"] ||
      spec["previous_product_ref"] != payload["previous_product_ref"] ||
      spec["scope"]["conversation_id"] != conversation_id) {
    return invalid_authority("accepted event envelope disagrees with snapshot");
  }
  if (payload["snapshot_sha256"] != Sha256::hex(json::canonical(snapshot))) {
    return invalid_authority("accepted snapshot hash mismatch");
  }
  ActiveTaskAcceptance result;
  result.seq = event.seq;
  result.originating_message_id = payload["originating_message_id"].get<std::string>();
  result.acceptance = acceptance;
  result.snapshot = snapshot;
  return result;
}

Result<std::int64_t> append_impl(EventLog& log, std::string_view conversation_id,
                                 std::string_view originating_message_id, const Json& snapshot,
                                 std::string_view acceptance) {
  if (!valid_active_task_snapshot(snapshot)) {
    return Error(Errc::InvalidArgument, "active_task: refusing to persist an invalid accepted snapshot");
  }
  if (originating_message_id.empty()) {
    return Error(Errc::InvalidArgument, "active_task: originating message id is required");
  }
  if (acceptance != "explicit_caller_supplied" && acceptance != "legacy_retention_observed") {
    return Error(Errc::InvalidArgument, "active_task: invalid acceptance provenance");
  }
  const auto& spec = snapshot["supplied_spec"];
  if (spec["scope"]["conversation_id"] != conversation_id) {
    return Error(Errc::InvalidArgument, "active_task: accepted snapshot names another conversation");
  }
  Json payload{{"schema", "loom.chat_active_task_acceptance/1"},
               {"acceptance", acceptance},
               {"originating_message_id", originating_message_id},
               {"scope", spec["scope"]},
               {"goal_id", spec["goal_id"]},
               {"product_ref", spec["product_ref"]},
               {"version", spec["version"]},
               {"previous_product_ref", spec["previous_product_ref"]},
               {"accepted_snapshot", snapshot},
               {"snapshot_sha256", Sha256::hex(json::canonical(snapshot))}};
  return log.append(kActiveTaskAcceptedEvent, conversation_id, payload);
}

Status validate_acceptance_chains(const std::map<std::string, std::pair<std::string, Json>>& products) {
  std::map<std::string, std::map<std::string, const Json*>> scoped_products;
  for (const auto& [product_id, retained] : products) {
    const auto& spec = retained.second["supplied_spec"];
    scoped_products[json::canonical(spec["scope"])].emplace(product_id, &retained.second);
  }
  for (const auto& [scope, group] : scoped_products) {
    (void)scope;
    std::map<std::uint64_t, std::string> versions;
    for (const auto& [product_id, retained] : group) {
      const auto& spec = (*retained)["supplied_spec"];
      const auto version = spec["version"].get<std::uint64_t>();
      auto [position, inserted] = versions.emplace(version, product_id);
      if (!inserted && position->second != product_id) {
        return Error(Errc::InvalidArgument, "active_task: durable scope has competing version identities");
      }
    }
    for (const auto& [product_id, retained] : group) {
      (void)product_id;
      const auto& spec = (*retained)["supplied_spec"];
      const auto version = spec["version"].get<std::uint64_t>();
      const auto& previous = spec["previous_product_ref"];
      if (previous.is_null()) {
        if (version != 1) {
          return Error(Errc::InvalidArgument, "active_task: durable scope has no version-one root");
        }
        continue;
      }
      if (version <= 1 || !previous.is_object() || !previous.contains("id") || !previous["id"].is_string()) {
        return Error(Errc::InvalidArgument, "active_task: durable scope has an invalid predecessor");
      }
      auto ancestor = group.find(previous["id"].get<std::string>());
      if (ancestor == group.end()) {
        return Error(Errc::InvalidArgument, "active_task: durable scope has a missing predecessor");
      }
      const auto& ancestor_spec = (*ancestor->second)["supplied_spec"];
      if (ancestor_spec["version"].get<std::uint64_t>() != version - 1 ||
          ancestor_spec["goal_id"] != spec["goal_id"]) {
        return Error(Errc::InvalidArgument, "active_task: durable ancestry changes goal or version sequence");
      }
    }
  }
  return {};
}

Status validate_inherited_evidence(
    const Json& retained, const std::map<std::string, std::pair<std::string, Json>>& products) {
  Json expected = Json::array();
  std::set<std::string> covered;
  for (const auto& source : retained["source_messages"]) {
    covered.insert(source["message_id"].get<std::string>());
  }
  const auto& spec = retained["supplied_spec"];
  Json previous = spec["previous_product_ref"];
  while (!previous.is_null()) {
    auto ancestor = products.find(previous["id"].get<std::string>());
    if (ancestor == products.end()) {
      return Error(Errc::InvalidArgument, "active_task: durable inherited evidence has a missing predecessor");
    }
    const auto& ancestor_snapshot = ancestor->second.second;
    const auto& ancestor_spec = ancestor_snapshot["supplied_spec"];
    if (ancestor_spec["scope"] != spec["scope"]) {
      return Error(Errc::InvalidArgument, "active_task: durable inherited evidence crosses scope");
    }
    for (const auto& source : ancestor_snapshot["source_messages"]) {
      const auto mid = source["message_id"].get<std::string>();
      if (!covered.insert(mid).second) continue;
      expected.push_back(Json{{"product_ref", ancestor_spec["product_ref"]},
                              {"source_message", source}});
    }
    previous = ancestor_spec["previous_product_ref"];
  }
  if (retained["inherited_source_messages"] != expected) {
    return Error(Errc::InvalidArgument, "active_task: durable inherited evidence disagrees with ancestry");
  }
  return {};
}

bool compatible_product_identity(const Json& first, const Json& second) {
  return first["supplied_spec"] == second["supplied_spec"] && first["bindings"] == second["bindings"];
}

}  // namespace

bool valid_active_task_snapshot(const Json& retained) {
  if (!retained.is_object() || retained.size() != 12 ||
      retained.value("schema", Json()) != "loom.chat_active_task/1" ||
      !retained.contains("supplied_spec") || !retained.contains("compiled_spec") ||
      !retained.contains("bindings") || !retained["bindings"].is_object() ||
      !retained.contains("source_messages") || !retained["source_messages"].is_array() ||
      !retained.contains("history_coverage_message_ids") ||
      !retained["history_coverage_message_ids"].is_array() ||
      !retained.contains("inherited_source_messages") ||
      !retained["inherited_source_messages"].is_array() ||
      !retained.contains("history_mode") || !retained["history_mode"].is_string() ||
      (retained["history_mode"] != "replace_refinement" && retained["history_mode"] != "append") ||
      retained.value("acceptance", Json()) != "explicit_caller_supplied" ||
      !retained.contains("compiler") || !retained["compiler"].is_object() ||
      retained["compiler"].size() != 2 ||
      retained["compiler"].value("id", Json()) != "loom.active_task_renderer" ||
      retained["compiler"].value("version", Json()) != "1" ||
      retained.value("binding_verification", Json()) !=
          "native_message_text_sha256_and_optional_quote" ||
      retained.value("source_selection", Json()) != "native:active") return false;
  auto compiled = compile_active_task_spec(retained["supplied_spec"]);
  if (!compiled || *compiled != retained["compiled_spec"]) return false;
  if ((*compiled)["scope"]["branch_id"] != retained["source_selection"]) return false;
  const auto& events = (*compiled)["history_event_ids"];
  const auto& bindings = retained["bindings"];
  if (bindings.size() != events.size() || retained["source_messages"].size() != events.size()) return false;
  std::set<std::string> event_ids;
  std::set<std::string> message_ids;
  std::map<std::string, std::string> source_texts;
  auto valid_source = [](const Json& source) {
    if (!source.is_object() || source.size() != 10 || !source.contains("version_num")) return false;
    const auto& version = source["version_num"];
    const bool valid_version = version.type() == Json::value_t::number_integer ||
        (version.is_number_unsigned() &&
         version.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    return valid_version && source.contains("event_id") &&
        source["event_id"].is_string() &&
        !source["event_id"].get_ref<const std::string&>().empty() && source.contains("message_id") &&
        source["message_id"].is_string() && !source["message_id"].get_ref<const std::string&>().empty() &&
        source.contains("role") && source["role"].is_string() && source.contains("text") &&
        source["text"].is_string() && source.contains("text_sha256") && source["text_sha256"].is_string() &&
        source.contains("status") && source["status"] == "active" && source.contains("created") &&
        source["created"].is_string() && source.contains("attachments") &&
        source.contains("version_group_id") &&
        (source["version_group_id"].is_null() || source["version_group_id"].is_string());
  };
  for (const auto& event : events) event_ids.insert(event.get<std::string>());
  for (const auto& source : retained["source_messages"]) {
    if (!valid_source(source)) return false;
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
    auto text = source_texts.find(source["event_id"].get<std::string>());
    if (text == source_texts.end() ||
        (source.contains("quote") &&
         text->second.find(source["quote"].get<std::string>()) == std::string::npos)) return false;
  }
  if (!event_ids.empty()) return false;

  Json expected_coverage = Json::array();
  std::set<std::string> covered_ids;
  for (const auto& source : retained["source_messages"]) {
    const auto mid = source["message_id"].get<std::string>();
    covered_ids.insert(mid);
    expected_coverage.push_back(mid);
  }
  for (const auto& inherited : retained["inherited_source_messages"]) {
    if (!inherited.is_object() || inherited.size() != 2 || !inherited.contains("product_ref") ||
        !inherited["product_ref"].is_object() || !inherited.contains("source_message") ||
        !inherited["source_message"].is_object()) return false;
    const auto& product = inherited["product_ref"];
    if (product.size() != 2 || product.value("kind", Json()) != "product" || !product.contains("id") ||
        !product["id"].is_string() || product["id"].get_ref<const std::string&>().empty()) return false;
    const auto& source = inherited["source_message"];
    if (!valid_source(source) ||
        !utf8::is_valid(source["text"].get_ref<const std::string&>()) ||
        source["text_sha256"] != Sha256::hex(source["text"].get<std::string>())) return false;
    const auto mid = source["message_id"].get<std::string>();
    if (mid.empty() || !covered_ids.insert(mid).second) return false;
    expected_coverage.push_back(mid);
  }
  return retained["history_coverage_message_ids"] == expected_coverage;
}

Result<ActiveTaskAuthority> load_active_task_authority(Database& db, std::string_view conversation_id) {
  EventLog log(db);
  LOOM_TRY_ASSIGN(auto records, all_events(log, "chat.active_task.*", conversation_id));
  ActiveTaskAuthority result;
  std::size_t legacy_count = 0;
  std::int64_t last_legacy_seq = 0;
  std::set<std::string> legacy_products;
  std::set<std::string> legacy_event_origins;
  std::set<std::string> legacy_marker_origins;
  std::set<std::string> acceptance_origins;
  for (const auto& record : records) {
    if (record.type == kActiveTaskAcceptedEvent) {
      LOOM_TRY_ASSIGN(auto parsed, parse_acceptance(record, conversation_id));
      if (!acceptance_origins.insert(parsed.originating_message_id).second) {
        return invalid_authority("originating message id is reused by multiple acceptance events");
      }
      if ((!result.baseline_complete && parsed.acceptance != "legacy_retention_observed") ||
          (result.baseline_complete && parsed.acceptance != "explicit_caller_supplied")) {
        return invalid_authority("acceptance provenance appears on the wrong side of the baseline");
      }
      if (!result.baseline_complete) {
        const auto product_id = parsed.snapshot["supplied_spec"]["product_ref"]["id"].get<std::string>();
        if (!legacy_products.insert(product_id).second) {
          return invalid_authority("duplicate legacy product observation event");
        }
        legacy_event_origins.insert(parsed.originating_message_id);
        ++legacy_count;
        last_legacy_seq = parsed.seq;
      } else if (legacy_marker_origins.contains(parsed.originating_message_id)) {
        return invalid_authority("explicit acceptance reuses a legacy observed row");
      }
      result.acceptances.push_back(std::move(parsed));
      continue;
    }
    if (record.type != kActiveTaskBaselineEvent) {
      return invalid_authority("unknown event type in active-task authority namespace");
    }
    if (result.baseline_complete) return invalid_authority("multiple baseline markers");
    const auto& payload = record.payload;
    std::set<std::string> marker_origins;
    if (payload.is_object() && payload.contains("legacy_originating_message_ids") &&
        payload["legacy_originating_message_ids"].is_array()) {
      for (const auto& id : payload["legacy_originating_message_ids"]) {
        if (id.is_string()) marker_origins.insert(id.get<std::string>());
      }
    }
    if (!payload.is_object() || payload.size() != 7 ||
        payload.value("schema", Json()) != "loom.chat_active_task_acceptance_baseline/1" ||
        payload.value("completed", Json()) != true ||
        !payload.contains("legacy_rows_observed") || !payload["legacy_rows_observed"].is_number_unsigned() ||
        !payload.contains("legacy_acceptances_imported") ||
        !payload["legacy_acceptances_imported"].is_number_unsigned() ||
        payload["legacy_acceptances_imported"].get<std::size_t>() != legacy_count ||
        payload["legacy_rows_observed"].get<std::size_t>() < legacy_count ||
        !payload.contains("legacy_originating_message_ids") ||
        !payload["legacy_originating_message_ids"].is_array() ||
        payload["legacy_originating_message_ids"].size() !=
            payload["legacy_rows_observed"].get<std::size_t>() ||
        marker_origins.size() != payload["legacy_originating_message_ids"].size() ||
        !std::includes(marker_origins.begin(), marker_origins.end(),
                       legacy_event_origins.begin(), legacy_event_origins.end()) ||
        !std::all_of(payload["legacy_originating_message_ids"].begin(),
                     payload["legacy_originating_message_ids"].end(),
                     [](const Json& id) { return id.is_string() && !id.get_ref<const std::string&>().empty(); }) ||
        !payload.contains("last_legacy_acceptance_seq") ||
        ((legacy_count == 0 && !payload["last_legacy_acceptance_seq"].is_null()) ||
         (legacy_count != 0 &&
          (!payload["last_legacy_acceptance_seq"].is_number_integer() ||
           payload["last_legacy_acceptance_seq"].get<std::int64_t>() != last_legacy_seq))) ||
        payload.value("recovery_boundary", Json()) !=
            "upgrade-time observation of retained top-level metadata; absent or moved legacy rows are not recovered") {
      return invalid_authority("baseline marker payload");
    }
    legacy_marker_origins = std::move(marker_origins);
    result.baseline_complete = true;
  }
  if (!result.baseline_complete && !result.acceptances.empty()) {
    return invalid_authority("acceptance records exist without a completed baseline");
  }
  std::map<std::string, std::pair<std::string, Json>> products;
  for (const auto& accepted : result.acceptances) {
    const auto product_id = accepted.snapshot["supplied_spec"]["product_ref"]["id"].get<std::string>();
    auto [position, inserted] = products.emplace(
        product_id, std::make_pair(accepted.originating_message_id, accepted.snapshot));
    if (!inserted && !compatible_product_identity(position->second.second, accepted.snapshot)) {
      return invalid_authority("product identity has conflicting accepted snapshots");
    }
  }
  LOOM_TRY(validate_acceptance_chains(products));
  for (const auto& accepted : result.acceptances) {
    LOOM_TRY(validate_inherited_evidence(accepted.snapshot, products));
  }
  return result;
}

Result<ActiveTaskAuthority> ensure_active_task_baseline(Database& db, std::string_view conversation_id) {
  if (!db.conn().in_transaction()) {
    return Error(Errc::Internal, "active_task: legacy baseline requires an owning write transaction");
  }
  LOOM_TRY_ASSIGN(auto authority, load_active_task_authority(db, conversation_id));
  if (authority.baseline_complete) return authority;

  LOOM_TRY_ASSIGN(auto messages, db.get_msgs(conversation_id, true));
  std::map<std::string, std::pair<std::string, Json>> products;
  Json observed_origins = Json::array();
  std::size_t observed = 0;
  for (const auto& message : messages) {
    if (!message.metadata.is_object()) continue;
    auto retained = message.metadata.find("active_task");
    if (retained == message.metadata.end()) continue;
    if (message.role != "user") {
      return Error(Errc::InvalidArgument,
                   "active_task: retained legacy acceptance is attached to a non-user row");
    }
    ++observed;
    observed_origins.push_back(message.id);
    if (!valid_active_task_snapshot(*retained)) {
      return Error(Errc::InvalidArgument, "active_task: retained legacy acceptance metadata is malformed");
    }
    if ((*retained)["supplied_spec"]["scope"]["conversation_id"] != conversation_id) {
      return Error(Errc::InvalidArgument, "active_task: retained legacy acceptance names another conversation");
    }
    const auto product_id = (*retained)["supplied_spec"]["product_ref"]["id"].get<std::string>();
    auto [it, inserted] = products.emplace(product_id, std::make_pair(message.id, *retained));
    if (!inserted && !compatible_product_identity(it->second.second, *retained)) {
      return Error(Errc::InvalidArgument, "active_task: legacy product identity has conflicting retained snapshots");
    }
  }
  LOOM_TRY(validate_acceptance_chains(products));
  for (const auto& [product_id, retained] : products) {
    (void)product_id;
    LOOM_TRY(validate_inherited_evidence(retained.second, products));
  }

  EventLog log(db);
  std::int64_t last_legacy_seq = 0;
  for (const auto& [product_id, retained] : products) {
    (void)product_id;
    LOOM_TRY_ASSIGN(last_legacy_seq, append_impl(log, conversation_id, retained.first, retained.second,
                                                "legacy_retention_observed"));
  }
  Json marker{{"schema", "loom.chat_active_task_acceptance_baseline/1"},
              {"completed", true},
              {"legacy_rows_observed", observed},
              {"legacy_acceptances_imported", products.size()},
              {"legacy_originating_message_ids", std::move(observed_origins)},
              {"last_legacy_acceptance_seq", products.empty() ? Json(nullptr) : Json(last_legacy_seq)},
              {"recovery_boundary",
               "upgrade-time observation of retained top-level metadata; absent or moved legacy rows are not recovered"}};
  LOOM_TRY(log.append(kActiveTaskBaselineEvent, conversation_id, marker));
  return load_active_task_authority(db, conversation_id);
}

Result<std::int64_t> append_active_task_acceptance(Database& db, std::string_view conversation_id,
                                                   std::string_view originating_message_id,
                                                   const Json& snapshot) {
  if (!db.conn().in_transaction()) {
    return Error(Errc::Internal, "active_task: acceptance append requires an owning write transaction");
  }
  LOOM_TRY_ASSIGN(auto row, db.get_msg(originating_message_id));
  if (!row || row->conv_id != conversation_id || row->role != "user" ||
      !row->metadata.is_object() || !row->metadata.contains("active_task") ||
      row->metadata["active_task"] != snapshot) {
    return Error(Errc::InvalidArgument,
                 "active_task: originating user row does not retain the accepted snapshot");
  }
  EventLog log(db);
  return append_impl(log, conversation_id, originating_message_id, snapshot,
                     "explicit_caller_supplied");
}

}  // namespace loom::chat
