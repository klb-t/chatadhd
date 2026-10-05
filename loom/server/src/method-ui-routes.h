#pragma once

// UI transport over W3/W4's actual operations and existing Runtime database.
// There is no server method catalogue, provider dispatcher or second graph.
#include "native-ui-common.h"
#include "../../src/chat/graph_reply.h"
#include "../../src/context/context_execution.h"
#include "../../src/context/method_channels.h"
#include "../../src/context/method_registry.h"
#include "../../src/packet/packet.h"
#include "loom/config.h"
#include "loom/graph_packet_store.h"
#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/util/sha256.h"

#include <limits>
#include <set>

namespace loom_server::method_ui {
using native_ui::Json;
using loom::Result;

inline loom::Error invalid(std::string message) {
  return {loom::Errc::InvalidArgument, "method UI: " + std::move(message)};
}
inline std::string hash(const Json& value) { return loom::Sha256::hex(loom::json::canonical(value)); }
// JSON strings are authoritative transport values. Browser numbers cannot
// preserve native integer/float representation (1.0 versus 1) or uint64 width.
// Existing hashes are never recalculated to repair a browser round trip.
inline Result<Json> payload(const Json& command, const char* field, const Json& fallback = nullptr) {
  const std::string raw_field = std::string(field) + "_json";
  if (command.contains(raw_field)) {
    if (!command[raw_field].is_string()) return invalid(raw_field + " must be exact JSON text");
    try { return loom::packet::parse_strict(command[raw_field].get<std::string>()); }
    catch (const std::exception&) { return loom::Error(loom::Errc::Parse, raw_field + " must contain strict JSON"); }
  }
  if (command.contains(field)) return command[field];
  return fallback;
}
inline Json merge(Json into, const Json& overlay) {
  if (!into.is_object() || !overlay.is_object()) return overlay;
  for (auto entry = overlay.begin(); entry != overlay.end(); ++entry) {
    if (into.contains(entry.key())) into[entry.key()] = merge(into[entry.key()], entry.value());
    else into[entry.key()] = entry.value();
  }
  return into;
}
inline loom::Status remove_path(Json& root, const Json& path) {
  if (!path.is_array() || path.empty()) return invalid("attrs removal path must be a nonempty key/index array");
  Json* current = &root;
  for (std::size_t position = 0; position < path.size(); ++position) {
    const auto& key = path[position];
    const bool last = position + 1 == path.size();
    if (key.is_string() && current->is_object()) {
      const auto name = key.get<std::string>();
      if (!current->contains(name)) return {};
      if (last) { current->erase(name); return {}; }
      current = &(*current)[name];
    } else if (key.is_number_integer() && current->is_array()) {
      if (!key.is_number_unsigned() && key.get<std::int64_t>() < 0) return invalid("attrs array removal index must be nonnegative");
      const auto index = key.get<std::uint64_t>();
      if (index >= current->size()) return {};
      if (last) { current->erase(current->begin() + static_cast<Json::difference_type>(index)); return {}; }
      current = &(*current)[static_cast<std::size_t>(index)];
    } else return invalid("attrs removal path does not address an object key or array index");
  }
  return {};
}
inline Json snapshot_view(const Json& snapshot) {
  Json view = snapshot;
  view["snapshot_json"] = snapshot.dump();
  view["profile_json"] = snapshot["profile"].dump();
  view["edit_attrs_json"] = Json::object();
  for (const auto& entity : snapshot["entities"]) view["edit_attrs_json"][entity["id"].get<std::string>()] = entity["attrs"].dump(2);
  return view;
}
inline Json preview_view(const Json& result) {
  Json view = result;
  if (result.contains("profile")) view["profile_json"] = result["profile"].dump();
  if (result.contains("packet")) view["packet_json"] = result["packet"].dump();
  if (result.contains("manifest")) view["manifest_json"] = result["manifest"].dump();
  if (result.contains("accept_request")) view["accept_request_json"] = result["accept_request"].dump();
  return view;
}
inline Result<std::string> required_text(const Json& value, const char* key) {
  if (!value.contains(key) || !value[key].is_string() || value[key].get_ref<const std::string&>().empty())
    return invalid(std::string("nonempty string required: ") + key);
  return value[key].get<std::string>();
}
inline Result<std::vector<std::string>> receipt_ids(const Json& value) {
  if (!value.is_array()) return invalid("receipt_ids must be an array");
  std::vector<std::string> result;
  for (const auto& id : value) {
    if (!id.is_string() || id.get_ref<const std::string&>().empty()) return invalid("receipt ID must be nonempty text");
    result.push_back(id.get<std::string>());
  }
  return result;
}
inline Json values(const Json& index) {
  Json result = Json::array();
  for (const auto& row : index) result.push_back(row);
  return result;
}
inline Json capabilities() {
  Json result{{"execution", loom::context::method_channel_capabilities()},
              {"fusion", loom::context::method_fusion_capabilities()}};
  // This describes the actual ChatGraphExecution instrument, not an installed
  // model or authorization. The methods endpoint itself never dispatches it.
  result["execution"]["llm.chat.completions"] = Json{{"available", true},
      {"source", "native_chat_graph_execution"}, {"protocol", "openai_chat_completions"},
      {"authorization", "explicit_context_execution_required"}, {"provider_call_performed", false}};
  result["execution"]["analysis.http"] = Json{{"available", true},
      {"source", "source_private_server_analysis_transport"}, {"authorization", "explicit_prepared_request_required"},
      {"mechanism", "native_exact_analysis_http_single_attempt"}, {"provider_call_performed", false},
      {"embedded_abi_available", false}};
  return result;
}
inline Json config_slots(const Json& execution) {
  Json result = Json::array();
  for (const char* section : {"method_registry", "graph_reply"}) {
    const auto settings = execution.value(section, Json::object());
    result.push_back(Json{{"id", std::string("context_execution.") + section + ".profile"},
        {"config_path", Json::array({"context_execution", section, "profile"})},
        {"receipt_path", Json::array({"context_execution", section, "receipt_ids"})},
        {"selection_path", Json::array({"context_execution", section, "selection"})},
        {"present", settings.is_object() && settings.contains("profile") && settings["profile"].is_object()}});
  }
  return result;
}
inline Json profiles(const Json& execution) {
  Json result = Json::array();
  for (const char* section : {"method_registry", "graph_reply"}) {
    const auto settings = execution.value(section, Json::object());
    if (!settings.is_object() || !settings.contains("profile") || !settings["profile"].is_object()) continue;
    result.push_back(Json{{"id", std::string("context_execution.") + section + ".profile"},
        {"config_path", Json::array({"context_execution", section, "profile"})},
        {"receipt_path", Json::array({"context_execution", section, "receipt_ids"})},
        {"selection_path", Json::array({"context_execution", section, "selection"})},
        {"selection_overlay", settings.value("selection", Json::object())},
        {"selection_overlay_json", settings.value("selection", Json::object()).dump(2)},
        {"profile", settings["profile"]}, {"profile_json", settings["profile"].dump(2)},
        {"receipt_ids", settings.value("receipt_ids", Json::array())}});
  }
  return result;
}
inline Json chat_settings(loom::Runtime& runtime) {
  const Json execution = runtime.config().get("context_execution", Json::object());
  return Json{{"scope", "global_native_config"}, {"context_execution", execution},
      {"context_execution_json", execution.dump()},
      {"context_execution_sha256", hash(execution)},
      {"graph_reply", execution.is_object() ? execution.value("graph_reply", Json::object()) : Json(nullptr)},
      {"graph_reply_json", (execution.is_object() ? execution.value("graph_reply", Json::object()) : Json(nullptr)).dump()},
      {"modes", Json::array({"off", "answer_as_graph", "text_plus_JSONgraph", "separate_model_afterwards"})},
      {"capabilities", capabilities()}, {"profiles", execution.is_object() ? profiles(execution) : Json::array()},
      {"config_slots", execution.is_object() ? config_slots(execution) : Json::array()},
      {"per_call_context_execution", false}, {"immutable_chat_resume", false}};
}
inline Result<Json> refreshed_snapshot(loom::context::MethodRegistry& registry, const Json& supplied) {
  if (!supplied.is_object() || !supplied.contains("profile") || !supplied.contains("snapshot_sha256"))
    return invalid("native registry snapshot required");
  Json ids = Json::array();
  for (const auto& receipt : supplied.at("receipts")) ids.push_back(receipt.at("receipt_id"));
  LOOM_TRY_ASSIGN(auto receipts, receipt_ids(ids));
  LOOM_TRY_ASSIGN(auto current, registry.load(supplied["profile"], receipts));
  if (current != supplied) return loom::Error(loom::Errc::Conflict, "method registry snapshot changed or its hash drifted");
  return current;
}
inline Result<Json> command_snapshot(const Json& command) {
  LOOM_TRY_ASSIGN(auto snapshot, payload(command, "snapshot"));
  // A load response includes an opaque original plus presentation metadata.
  // The original is explicitly authoritative when forwarded as a whole view.
  if (snapshot.is_object() && snapshot.contains("snapshot_json")) return payload(snapshot, "snapshot");
  return snapshot;
}
inline Result<Json> catalog(loom::Runtime& runtime, const Json& command) {
  const Json execution = runtime.config().get("context_execution", Json::object());
  if (!execution.is_object()) return invalid("configured context_execution must be an object");
  Json receipts = Json::array();
  auto& db = runtime.db();
  auto lock = db.lock();
  if (db.conn().has_table("loom_kb_graph_receipts")) {
    std::string query = "SELECT id FROM loom_kb_graph_receipts ORDER BY id";
    if (command.contains("limit")) {
      if (!command["limit"].is_number_integer() || command["limit"].get<std::int64_t>() < 0)
        return invalid("catalog limit must be a nonnegative supported integer");
      query += " LIMIT ?";
    }
    LOOM_TRY_ASSIGN(auto statement, db.conn().prepare(query));
    if (command.contains("limit")) statement.bind(1, command["limit"].get<std::int64_t>());
    std::vector<std::string> ids;
    while (true) { LOOM_TRY_ASSIGN(auto row, statement.step()); if (!row) break; ids.push_back(statement.get_text(0)); }
    loom::kb::GraphPacketStore store(db);
    for (const auto& id : ids) {
      LOOM_TRY_ASSIGN(auto read, store.execute(Json{{"operation", "read"}, {"receipt_id", id}}));
      const auto& receipt = read["receipt"];
      receipts.push_back(Json{{"id", id}, {"receipt_id", id}, {"target", receipt["target"]},
          {"run_id", receipt["run_id"]}, {"selection", receipt["selection"]},
          {"receipt_sha256", receipt["receipt_sha256"]}, {"row_drift", read["row_drift"]}});
    }
  }
  return Json{{"capabilities", capabilities()}, {"profiles", profiles(execution)},
      {"config_slots", config_slots(execution)}, {"receipts", receipts},
      {"context_execution_sha256", hash(execution)}, {"canonical_store_written", false}, {"provider_calls", 0}};
}
inline Result<Json> expected_rows(loom::Database& db, const Json& packet, const std::string& target) {
  auto lock = db.lock();
  loom::kb::KnowledgeStore store(db);
  const auto run = loom::kb::KnowledgeRun::make_id("loom.graph_packet_store/1", Json{{"graph_packet_target", target}});
  Json selection = Json::object(), expected = Json::object();
  for (const char* collection : {"entities", "claims", "sources"}) {
    selection[collection] = Json::array(); expected[collection] = Json::object();
    for (const auto& row : packet[collection]) {
      const auto& native = std::string_view(collection) == "sources" ? row["observation"] : row;
      const auto id = native.at("id").get<std::string>();
      Json current = nullptr;
      if (std::string_view(collection) == "entities") {
        LOOM_TRY_ASSIGN(auto found, store.get_entity(run, id));
        if (found) {
          current = found->to_json();
          // Fail the preview where native accept would reject an overwrite of
          // an executed immutable definition. CAS alone permits mutable edits.
          const auto& old_attrs = current["attrs"];
          const auto& next_attrs = native["attrs"];
          for (const char* key : {"definition", "definition_sha256", "text", "text_sha256"}) {
            const bool protected_field = std::string_view(key).starts_with("definition")
                ? old_attrs.contains("definition_sha256") : old_attrs.contains("text_sha256");
            if (protected_field && (!old_attrs.contains(key) || !next_attrs.contains(key) || old_attrs[key] != next_attrs[key]))
              return loom::Error(loom::Errc::Conflict, "immutable definition already exists under this target identity; create a new version");
          }
        }
      } else if (std::string_view(collection) == "claims") {
        LOOM_TRY_ASSIGN(auto found, store.get_claim(run, id)); if (found) current = found->to_json();
      } else {
        LOOM_TRY_ASSIGN(auto found, store.get_observation(run, id)); if (found) current = found->to_json();
      }
      selection[collection].push_back(id);
      expected[collection][id] = current.is_null() ? Json(nullptr) : Json(hash(current));
    }
  }
  return Json{{"selection", selection}, {"expected_rows", expected}};
}
inline Result<Json> profile_preview(loom::Runtime& runtime, loom::context::MethodRegistry& registry, const Json& command) {
  LOOM_TRY_ASSIGN(auto target, required_text(command, "target"));
  LOOM_TRY_ASSIGN(auto actor, required_text(command, "actor"));
  LOOM_TRY_ASSIGN(auto known, required_text(command, "known_at"));
  LOOM_TRY_ASSIGN(auto receipts, receipt_ids(command.value("receipt_ids", Json::array())));
  LOOM_TRY_ASSIGN(auto input_profile, payload(command, "profile"));
  LOOM_TRY_ASSIGN(auto snapshot, registry.load(input_profile, receipts));
  Json profile = snapshot["profile"];
  for (const char* collection : {"entities", "claims", "sources"}) profile[collection] = values(snapshot[collection]);
  for (const auto& entity : snapshot["entities"]) {
    std::string role;
    for (auto entry = snapshot["vocabulary"]["kinds"].begin(); entry != snapshot["vocabulary"]["kinds"].end(); ++entry)
      if (entry.value() == entity["kind"]) role = entry.key();
    if (!role.ends_with("_version")) continue;
    const auto& attrs = entity["attrs"];
    std::string bytes;
    if (role == "prompt_version") bytes = attrs.at("text").get<std::string>();
    else if (attrs.contains("definition")) bytes = loom::json::canonical(attrs["definition"]);
    else continue;
    const auto bytes_hash = loom::Sha256::hex(bytes);
    loom::model::Observation source;
    // A capture is an occurrence, distinct from its immutable byte source.
    // Shared definitions installed at different times must not reuse an
    // Observation ID with different known_at/actor metadata, nor collide with
    // MethodRegistry's own later runtime captures of the same exact bytes.
    source.unit = "un_" + hash(Json{{"source_sha256", bytes_hash}, {"version_id", entity["id"]},
        {"actor", actor}, {"known_at", known}});
    source.kind = loom::model::ObservationKind::Field;
    source.text = bytes; source.locator.source = "sha256:" + bytes_hash;
    source.locator.member = role; source.locator.byte_start = 0;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) return invalid("source byte length cannot be represented");
    source.locator.byte_len = static_cast<std::int64_t>(bytes.size());
    source.id = loom::model::Observation::make_id(source.unit, source.locator, source.text);
    source.speaker = actor; source.artifact_type = "loom.method_graph/1";
    source.attrs = Json{{"capture_role", role}, {"content_truth", "not_established"}};
    bool present = false;
    for (const auto& old : profile["sources"]) if (old["observation"]["id"] == source.id) present = true;
    if (!present) profile["sources"].push_back(Json{{"observation", source.to_json()}, {"known_at", known}, {"text_sha256", bytes_hash}});
  }
  LOOM_TRY_ASSIGN(auto captured, registry.load(profile));
  Json resolution = nullptr;
  if (profile.contains("selection")) { LOOM_TRY_ASSIGN(resolution, registry.resolve(captured, Json::object(), capabilities())); }
  const Json origin{{"kind", "user"}, {"actor", actor}, {"model", nullptr}, {"recipe_sha256", nullptr}, {"response_sha256", nullptr}};
  LOOM_TRY_ASSIGN(auto packet, loom::chat::graph_reply_packet_operation(Json{{"operation", "make"}, {"origin", origin},
      {"known_at", known}, {"entities", profile["entities"]}, {"claims", profile["claims"]}, {"sources", profile["sources"]}}));
  LOOM_TRY_ASSIGN(auto cas, expected_rows(runtime.db(), packet, target));
  return Json{{"profile", profile}, {"packet", packet}, {"resolution", resolution}, {"selection", cas["selection"]},
      {"expected_rows", cas["expected_rows"]},
      {"accept_request", Json{{"operation", "accept"}, {"target", target}, {"packet", packet},
          {"selection", cas["selection"]}, {"expected_rows", cas["expected_rows"]}, {"explicitly_accepted", true}}},
      {"canonical_store_written", false}, {"provider_calls", 0}};
}
inline Result<Json> version_edit(loom::Runtime& runtime, loom::context::MethodRegistry& registry, const Json& command) {
  auto database_lock = runtime.db().lock();
  LOOM_TRY_ASSIGN(auto supplied_snapshot, command_snapshot(command));
  LOOM_TRY_ASSIGN(auto snapshot, refreshed_snapshot(registry, supplied_snapshot));
  LOOM_TRY_ASSIGN(auto id, required_text(command, "entity_id"));
  LOOM_TRY_ASSIGN(auto target, required_text(command, "target"));
  LOOM_TRY_ASSIGN(auto actor, required_text(command, "actor"));
  LOOM_TRY_ASSIGN(auto known, required_text(command, "known_at"));
  if (!snapshot["entities"].contains(id)) return loom::Error(loom::Errc::NotFound, "method version entity not found");
  const auto& original = snapshot["entities"][id];
  std::string role;
  for (auto entry = snapshot["vocabulary"]["kinds"].begin(); entry != snapshot["vocabulary"]["kinds"].end(); ++entry)
    if (entry.value() == original["kind"]) role = entry.key();
  if (!role.ends_with("_version")) return invalid("only a declared immutable version may be forked");
  if ((command.contains("attrs_patch_json") || command.contains("attrs_patch")) &&
      (command.contains("attrs_json") || command.contains("attrs"))) return invalid("choose full attrs or a native attrs patch");
  LOOM_TRY_ASSIGN(auto supplied_attrs, payload(command, "attrs"));
  if (command.contains("attrs_patch_json") || command.contains("attrs_patch")) {
    LOOM_TRY_ASSIGN(auto patch, payload(command, "attrs_patch"));
    if (!patch.is_object()) return invalid("attrs patch must be an object");
    supplied_attrs = merge(original["attrs"], patch);
  }
  if (!supplied_attrs.is_object()) return invalid("complete version attrs must be an object");
  Json attrs = supplied_attrs;
  if (command.contains("attrs_remove_paths")) {
    if (!command["attrs_remove_paths"].is_array()) return invalid("attrs_remove_paths must be an array");
    for (const auto& path : command["attrs_remove_paths"]) LOOM_TRY(remove_path(attrs, path));
  }
  std::string bytes;
  if (role == "prompt_version") {
    if (!attrs.contains("text") || !attrs["text"].is_string()) return invalid("prompt version requires exact text");
    bytes = attrs["text"].get<std::string>();
    attrs["text_sha256"] = loom::Sha256::hex(bytes);
  } else {
    if (!attrs.contains("definition") || !attrs["definition"].is_object()) return invalid("version requires a definition object");
    bytes = loom::json::canonical(attrs["definition"]);
    attrs["definition_sha256"] = loom::Sha256::hex(bytes);
  }
  if (attrs == original["attrs"]) return invalid("new immutable version must change the captured content");
  LOOM_TRY_ASSIGN(auto entity, loom::model::Entity::from_json(original));
  entity.attrs = attrs;
  entity.canonical_key = hash(Json{{"previous_version", id}, {"attrs", attrs}});
  entity.id = loom::model::Entity::make_id(entity.kind, entity.canonical_key);
  if (command.contains("new_version_id")) { LOOM_TRY_ASSIGN(entity.id, required_text(command, "new_version_id")); }
  if (snapshot["entities"].contains(entity.id)) return loom::Error(loom::Errc::Conflict, "new version identity already exists");
  const auto target_run = loom::kb::KnowledgeRun::make_id("loom.graph_packet_store/1", Json{{"graph_packet_target", target}});
  LOOM_TRY_ASSIGN(auto stored_version, loom::kb::KnowledgeStore(runtime.db()).get_entity(target_run, entity.id));
  if (stored_version) return loom::Error(loom::Errc::AlreadyExists, "new version identity is already stored; read its receipt instead of replacing its capture metadata");
  entity.origin = loom::model::Origin::User;
  entity.evidence = loom::model::EvidenceClass::User;
  entity.first_seen = known; entity.last_seen = known;
  entity.aliases.clear();
  loom::model::Observation source;
  const auto bytes_hash = loom::Sha256::hex(bytes);
  source.unit = "un_" + hash(Json{{"source_sha256", bytes_hash}, {"version_id", entity.id},
      {"actor", actor}, {"known_at", known}});
  source.kind = loom::model::ObservationKind::Field;
  source.text = bytes;
  source.locator.source = "sha256:" + bytes_hash;
  source.locator.member = role;
  source.locator.byte_start = 0;
  if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) return invalid("source byte length cannot be represented");
  source.locator.byte_len = static_cast<std::int64_t>(bytes.size());
  source.id = loom::model::Observation::make_id(source.unit, source.locator, source.text);
  source.artifact_type = "loom.method_graph/1";
  source.speaker = actor;
  source.attrs = Json{{"capture_role", role}, {"content_truth", "not_established"}};
  const Json native_source{{"observation", source.to_json()}, {"known_at", known}, {"text_sha256", bytes_hash}};
  Json relation_descriptors = Json::array();
  const auto& predicates = snapshot["vocabulary"]["predicates"];
  const bool explicit_outgoing = command.contains("outgoing_claim_ids");
  std::set<std::string> selected_outgoing;
  if (explicit_outgoing) {
    if (!command["outgoing_claim_ids"].is_array()) return invalid("outgoing_claim_ids must be an array");
    for (const auto& claim_id : command["outgoing_claim_ids"]) {
      if (!claim_id.is_string() || claim_id.get_ref<const std::string&>().empty() ||
          !selected_outgoing.insert(claim_id.get<std::string>()).second) return invalid("outgoing Claim IDs must be unique nonempty strings");
      const auto selected_id = claim_id.get<std::string>();
      if (!snapshot["claims"].contains(selected_id) || snapshot["claims"][selected_id]["subject"] != id ||
          snapshot["claims"][selected_id]["assessment"]["status"] != "active")
        return invalid("outgoing selection must address an actual active original Claim");
    }
  }
  bool identity_retained = false;
  for (const auto& row : snapshot["claims"]) {
    if (row["subject"] != id || row["assessment"]["status"] != "active") continue;
    if (explicit_outgoing && !selected_outgoing.contains(row["id"].get<std::string>())) continue;
    // A definition edit cannot copy historical assertions that a new version
    // executed a run or produced a result. They retain their original subject.
    bool execution_relation = false;
    for (const char* name : {"produced_in_run", "produced_by_method_version", "requests_method_version", "evaluated_method_version", "projected_by_compiler"})
      if (predicates.contains(name) && row["predicate"] == predicates[name]) execution_relation = true;
    if (execution_relation) {
      if (explicit_outgoing) return invalid("a definition fork cannot copy an assertion of historical execution");
      continue;
    }
    if (role == "combination_version" && predicates.contains("includes_method") && row["predicate"] == predicates["includes_method"]) {
      if (explicit_outgoing) return invalid("combination membership edges are rebuilt from the reviewed definition members");
      continue;
    }
    if (predicates.contains("version_of") && row["predicate"] == predicates["version_of"]) identity_retained = true;
    relation_descriptors.push_back(Json{{"subject", entity.id}, {"predicate", row["predicate"]},
        {"object", row["object"]}, {"value", row["value"]}, {"qualifiers", row["qualifiers"]}});
  }
  if (role == "method_version" && !identity_retained) return invalid("method version fork must retain its actual version_of identity Claim");
  if (role == "combination_version") {
    if (!predicates.contains("includes_method")) return invalid("combination edit requires declared includes_method predicate");
    const auto& definition = attrs.at("definition");
    if (!definition.contains("members") || !definition["members"].is_array()) return invalid("combination members must be an array");
    for (const auto& member : definition["members"]) {
      if (!member.is_object() || member.contains("method_version_id") == member.contains("combination_version_id")) return invalid("combination member needs exactly one version role");
      const bool nested = member.contains("combination_version_id");
      LOOM_TRY_ASSIGN(auto child, required_text(member, nested ? "combination_version_id" : "method_version_id"));
      const char* child_role = nested ? "combination_version" : "method_version";
      if (!snapshot["entities"].contains(child) || snapshot["entities"][child]["status"] != "active" ||
          !snapshot["vocabulary"]["kinds"].contains(child_role) || snapshot["entities"][child]["kind"] != snapshot["vocabulary"]["kinds"][child_role])
        return invalid("combination member must reference an active native version of the declared kind");
      loom::model::Qualifiers qualifiers;
      qualifiers.scope = "loom.method_graph/1";
      relation_descriptors.push_back(Json{{"subject", entity.id}, {"predicate", predicates["includes_method"]},
          {"object", child}, {"value", nullptr}, {"qualifiers", qualifiers.to_json()}});
    }
  }
  // Separate exact bytes from the owner's declaration of structural links;
  // an empty prompt is a valid byte capture and need not be a nonempty quote.
  auto declaration = source;
  declaration.text = loom::json::canonical(Json{{"previous_version_id", id}, {"new_version_id", entity.id},
      {"attrs", attrs}, {"relations", relation_descriptors}, {"actor", actor}});
  const auto declaration_hash = loom::Sha256::hex(declaration.text);
  declaration.unit = "un_" + hash(Json{{"source_sha256", declaration_hash}, {"version_id", entity.id},
      {"actor", actor}, {"known_at", known}});
  declaration.locator.source = "sha256:" + declaration_hash;
  declaration.locator.member = "version-edit-declaration.json";
  declaration.locator.byte_len = static_cast<std::int64_t>(declaration.text.size());
  declaration.id = loom::model::Observation::make_id(declaration.unit, declaration.locator, declaration.text);
  declaration.attrs["capture_role"] = "owner_version_edit_declaration";
  const Json native_declaration{{"observation", declaration.to_json()}, {"known_at", known}, {"text_sha256", declaration_hash}};
  Json added_claims = Json::array();
  // Fork active outgoing graph relations using their actual native predicate
  // strings. Old versions, incoming result/run edges and assessments remain.
  std::set<std::string> added_claim_ids;
  for (const auto& row : relation_descriptors) {
    loom::model::Claim claim;
    claim.subject = entity.id; claim.predicate = row["predicate"].get<std::string>(); claim.object = row["object"].get<std::string>();
    claim.value = row["value"];
    LOOM_TRY_ASSIGN(claim.qualifiers, loom::model::Qualifiers::from_json(row["qualifiers"]));
    claim.assessment.origin = loom::model::Origin::User;
    claim.assessment.evidence = loom::model::EvidenceClass::User;
    claim.assessment.confidence = 1.0;
    claim.assessment.support.push_back(loom::model::Support{declaration.id, declaration.locator, declaration.text, "loom.server.method_version_edit/1", 1.0});
    claim.id = loom::model::Claim::make_id(claim.subject, claim.predicate, claim.object, claim.value, claim.qualifiers);
    LOOM_TRY(claim.validate());
    if (added_claim_ids.insert(claim.id).second) added_claims.push_back(claim.to_json());
  }
  const Json projection_origin{{"kind", "system"}, {"actor", "loom.server.method_snapshot_projection/1"},
      {"model", nullptr}, {"recipe_sha256", nullptr}, {"response_sha256", nullptr}};
  const Json edit_origin{{"kind", "user"}, {"actor", actor}, {"model", nullptr},
      {"recipe_sha256", nullptr}, {"response_sha256", nullptr}};
  LOOM_TRY_ASSIGN(auto base, loom::chat::graph_reply_packet_operation(Json{{"operation", "make"},
      {"origin", projection_origin}, {"known_at", known}, {"entities", values(snapshot["entities"])},
      {"claims", values(snapshot["claims"])}, {"sources", values(snapshot["sources"])}}));
  LOOM_TRY_ASSIGN(auto diff, loom::chat::graph_reply_packet_operation(Json{{"operation", "empty_diff"},
      {"packet", base}, {"proposal_id", entity.id}, {"origin", edit_origin}, {"known_at", known}}));
  diff["entities"]["add"].push_back(entity.to_json());
  diff["claims"]["add"] = added_claims;
  // Reusing unchanged exact bytes must reuse the original immutable source DTO.
  const bool source_present = snapshot["sources"].contains(source.id);
  if (!source_present) diff["sources"]["add"].push_back(native_source);
  if (!snapshot["sources"].contains(declaration.id)) diff["sources"]["add"].push_back(native_declaration);
  LOOM_TRY_ASSIGN(auto preview, loom::chat::graph_reply_packet_operation(Json{{"operation", "preview"}, {"packet", base}, {"diff", diff}}));
  const auto& packet = preview["candidate_packet"];
  Json profile = snapshot["profile"];
  for (const char* collection : {"entities", "claims", "sources"}) profile[collection] = packet[collection];
  // W3 validates the exact new attrs, source hashes, DTOs and native closure.
  LOOM_TRY_ASSIGN(auto edited_snapshot, registry.load(profile));
  Json resolution = nullptr;
  if ((role == "method_version" || role == "combination_version") && profile.contains("selection")) {
    const char* member_key = role == "method_version" ? "method_version_id" : "combination_version_id";
    LOOM_TRY_ASSIGN(resolution, registry.resolve(edited_snapshot, Json{{"members", Json::array({Json{{member_key, entity.id}}})}}, capabilities()));
  }
  LOOM_TRY_ASSIGN(auto cas, expected_rows(runtime.db(), packet, target));
  const Json accept{{"operation", "accept"}, {"target", target}, {"packet", packet},
      {"selection", cas["selection"]}, {"expected_rows", cas["expected_rows"]}, {"explicitly_accepted", true}};
  return Json{{"packet", packet}, {"diff", diff}, {"selection", cas["selection"]},
      {"expected_rows", cas["expected_rows"]}, {"accept_request", accept}, {"profile", profile},
      {"new_version_id", entity.id}, {"previous_version_id", id}, {"source_id", source.id},
      {"definition_sha256", attrs.value("definition_sha256", Json(nullptr))},
      {"text_sha256", attrs.value("text_sha256", Json(nullptr))},
      {"resolution", resolution},
      {"selection_redirected", false}, {"canonical_store_written", false}, {"provider_calls", 0}};
}
inline Result<Json> result_lineage(loom::context::MethodRegistry& registry, const Json& command) {
  LOOM_TRY_ASSIGN(auto supplied_snapshot, command_snapshot(command));
  LOOM_TRY_ASSIGN(auto snapshot, refreshed_snapshot(registry, supplied_snapshot));
  const auto requested = command.value("result_entity_id", std::string());
  if (!requested.empty() && !snapshot["entities"].contains(requested)) return loom::Error(loom::Errc::NotFound, "result entity not found");
  Json lineage = Json::object();
  const auto& predicates = snapshot["vocabulary"]["predicates"];
  for (const auto& claim : snapshot["claims"]) {
    const auto subject = claim["subject"].get<std::string>();
    if (!requested.empty() && requested != subject) continue;
    for (const char* role : {"produced_by_method_version", "produced_in_run", "projected_by_compiler"}) {
      if (predicates.contains(role) && claim["predicate"] == predicates[role]) {
        if (!lineage.contains(subject)) lineage[subject] = Json::object();
        if (!lineage[subject].contains(role)) lineage[subject][role] = Json::array();
        lineage[subject][role].push_back(claim);
      }
    }
  }
  return Json{{"snapshot", snapshot}, {"snapshot_json", snapshot.dump()}, {"lineage", lineage}, {"producer_execution_verified", false},
      {"canonical_store_written", false}, {"provider_calls", 0}};
}
inline Result<Json> execute(loom::Runtime& runtime, const Json& command) {
  const std::string action = command.value("action", command.value("operation", std::string()));
  loom::context::MethodRegistry registry(runtime.db());
  if (action == "capabilities") return capabilities();
  if (action == "catalog") return catalog(runtime, command);
  if (action == "chat_settings") return chat_settings(runtime);
  if (action == "save_profile_selection") {
    std::lock_guard lock(native_ui::config_mutex());
    const Json prior = runtime.config().get("context_execution", Json::object());
    if (!prior.is_object()) return invalid("context_execution must be an object");
    LOOM_TRY_ASSIGN(auto expected, required_text(command, "expected_context_execution_sha256"));
    if (expected != hash(prior)) return loom::Error(loom::Errc::Conflict, "global native context settings changed; reload before saving");
    LOOM_TRY_ASSIGN(auto slot_id, required_text(command, "slot_id"));
    Json slot = nullptr;
    for (const auto& candidate : config_slots(prior)) if (candidate["id"] == slot_id) slot = candidate;
    if (slot.is_null()) return invalid("native profile config slot is unavailable");
    LOOM_TRY_ASSIGN(auto profile, payload(command, "profile"));
    LOOM_TRY_ASSIGN(auto receipts, receipt_ids(command.value("receipt_ids", Json::array())));
    if (!command.contains("selection_json") && !command.contains("selection")) return invalid("explicit reviewed selection is required");
    LOOM_TRY_ASSIGN(auto selection, payload(command, "selection"));
    if (!profile.is_object() || !selection.is_object()) return invalid("profile and reviewed selection must be objects");
    // Selection is configuration metadata, separate from immutable graph rows.
    // Both native slots must describe the exact choice reviewed by the owner;
    // keeping an old profile default would misreport the saved profile on read.
    profile["selection"] = selection;
    LOOM_TRY_ASSIGN(auto snapshot, registry.load(profile, receipts));
    LOOM_TRY(registry.resolve(snapshot, selection, capabilities()));
    Json next = prior;
    const std::string owner = slot["config_path"][1].get<std::string>();
    if (next.contains(owner) && !next[owner].is_object()) return invalid("native profile owner must be an object");
    if (!next.contains(owner)) next[owner] = Json::object();
    next[owner][slot["config_path"][2].get<std::string>()] = profile;
    next[owner][slot["receipt_path"][2].get<std::string>()] = receipts;
    next[owner][slot["selection_path"][2].get<std::string>()] = selection;
    LOOM_TRY(loom::context::validate_context_execution_options(next));
    runtime.config().set("context_execution", next);
    const auto saved = runtime.config().save();
    if (!saved) { runtime.config().set("context_execution", prior); return saved.error(); }
    return chat_settings(runtime);
  }
  if (action == "set_chat_settings") {
    std::lock_guard lock(native_ui::config_mutex());
    const Json prior = runtime.config().get("context_execution", Json::object());
    LOOM_TRY_ASSIGN(auto expected, required_text(command, "expected_context_execution_sha256"));
    if (expected != hash(prior)) return loom::Error(loom::Errc::Conflict, "global native context settings changed; reload before saving");
    LOOM_TRY_ASSIGN(auto next, payload(command, "context_execution", prior));
    if (command.contains("graph_reply") || command.contains("graph_reply_json")) {
      if (command.contains("context_execution") || command.contains("context_execution_json")) return invalid("choose complete context_execution or graph_reply replacement");
      if (!next.is_object()) return invalid("context_execution must be an object");
      LOOM_TRY_ASSIGN(next["graph_reply"], payload(command, "graph_reply"));
    }
    LOOM_TRY(loom::context::validate_context_execution_options(next));
    for (const char* section : {"method_registry", "graph_reply"}) {
      const auto settings = next.value(section, Json::object());
      if (settings.contains("profile")) {
        LOOM_TRY_ASSIGN(auto ids, receipt_ids(settings.value("receipt_ids", Json::array())));
        LOOM_TRY_ASSIGN(auto snapshot, registry.load(settings["profile"], ids));
        LOOM_TRY(registry.resolve(snapshot, settings.value("selection", Json::object()), capabilities()));
      }
      if (std::string_view(section) == "graph_reply") {
        LOOM_TRY(loom::chat::prepare_graph_reply(settings, Json::object(), Json::object(), Json::object(), Json::object()));
      }
    }
    runtime.config().set("context_execution", next);
    const auto saved = runtime.config().save();
    if (!saved) { runtime.config().set("context_execution", prior); return saved.error(); }
    return chat_settings(runtime);
  }
  if (action == "read" || action == "replay") {
    LOOM_TRY_ASSIGN(auto id, required_text(command, "receipt_id"));
    LOOM_TRY_ASSIGN(auto read, loom::kb::GraphPacketStore(runtime.db()).execute(Json{{"operation", action}, {"receipt_id", id}}));
    read["receipt_json"] = read["receipt"].dump(); read["packet_json"] = read["receipt"]["packet"].dump();
    return read;
  }
  if (action == "load") {
    LOOM_TRY_ASSIGN(auto ids, receipt_ids(command.value("receipt_ids", Json::array())));
    LOOM_TRY_ASSIGN(auto profile, payload(command, "profile"));
    LOOM_TRY_ASSIGN(auto snapshot, registry.load(profile, ids));
    return snapshot_view(snapshot);
  }
  if (action == "resolve") {
    LOOM_TRY_ASSIGN(auto snapshot, command_snapshot(command));
    LOOM_TRY_ASSIGN(auto selection, payload(command, "selection", Json::object()));
    LOOM_TRY_ASSIGN(auto result, registry.resolve(snapshot, selection, capabilities()));
    Json view = result;
    view["resolution_json"] = result.dump(); view["selection_json"] = result["selection"].dump(2);
    view["resolved_leaf_json"] = Json::array();
    for (const auto& leaf : result["leaves"]) view["resolved_leaf_json"].push_back(leaf.dump());
    return view;
  }
  if (action == "prepare") {
    LOOM_TRY_ASSIGN(auto snapshot, command_snapshot(command));
    LOOM_TRY_ASSIGN(auto leaf, payload(command, "resolved_leaf"));
    LOOM_TRY_ASSIGN(auto run, payload(command, "run_context"));
    LOOM_TRY_ASSIGN(auto result, registry.prepare(snapshot, leaf, run, loom::chat::graph_reply_packet_operation));
    return preview_view(result);
  }
  if (action == "bind") {
    LOOM_TRY_ASSIGN(auto packet, payload(command, "packet"));
    LOOM_TRY_ASSIGN(auto manifest, payload(command, "manifest"));
    LOOM_TRY_ASSIGN(auto bindings, payload(command, "result_bindings"));
    LOOM_TRY_ASSIGN(auto result, registry.bind_results(packet, manifest, bindings, loom::chat::graph_reply_packet_operation));
    return preview_view(result);
  }
  if (action == "accept") {
    LOOM_TRY_ASSIGN(auto request, payload(command, "request"));
    LOOM_TRY_ASSIGN(auto result, registry.accept(request));
    result["receipt_json"] = result["receipt"].dump();
    return result;
  }
  if (action == "profile_preview") { LOOM_TRY_ASSIGN(auto result, profile_preview(runtime, registry, command)); return preview_view(result); }
  if (action == "version_edit" || action == "version-edit") { LOOM_TRY_ASSIGN(auto result, version_edit(runtime, registry, command)); return preview_view(result); }
  if (action == "result_lineage" || action == "result-lineage") return result_lineage(registry, command);
  return invalid("unknown action");
}
inline Result<Json> parse_command(const httplib::Request& request) {
  try {
    Json body = loom::packet::parse_strict(request.body);
    if (!body.is_object()) return invalid("body must be a JSON object");
    return body;
  } catch (const std::exception&) {
    return loom::Error(loom::Errc::Parse, "method UI body must be strict JSON without duplicate keys or invalid numeric values");
  }
}
inline Result<Json> fragment(loom::Runtime& runtime, const Json& command) {
  Json result;
  if (command.contains("message_id")) {
    LOOM_TRY_ASSIGN(auto message_id, required_text(command, "message_id"));
    LOOM_TRY_ASSIGN(auto expected, required_text(command, "expected_compilation_sha256"));
    LOOM_TRY_ASSIGN(auto message, runtime.db().get_msg(message_id));
    if (!message) return loom::Error(loom::Errc::NotFound, "graph reply message not found");
    const Json* saved = loom::json::find(message->metadata, "graph_reply");
    if (!saved || !saved->is_object() || !saved->contains("compilation")) {
      const auto* trace = loom::json::find(message->metadata, "context_trace");
      saved = trace ? loom::json::find(*trace, "graph_reply") : nullptr;
    }
    if (!saved || !saved->is_object() || !saved->contains("compilation"))
      return loom::Error(loom::Errc::Unavailable, "saved message graph compilation is unavailable");
    result = *saved;
    if (!result["compilation"].is_object() || result["compilation"].value("compilation_sha256", Json(nullptr)) != expected)
      return loom::Error(loom::Errc::Conflict, "saved message compilation changed; read and review it again");
  } else {
    LOOM_TRY_ASSIGN(result, payload(command, "reply_result"));
    if (command.contains("expected_compilation_sha256")) {
      LOOM_TRY_ASSIGN(auto expected, required_text(command, "expected_compilation_sha256"));
      if (!result.is_object() || !result.contains("compilation") || !result["compilation"].is_object() ||
          result["compilation"].value("compilation_sha256", Json(nullptr)) != expected)
        return loom::Error(loom::Errc::Conflict, "captured graph compilation identity changed");
    }
  }
  LOOM_TRY_ASSIGN(auto address, payload(command, "address"));
  return loom::chat::graph_reply_fragment(result, address);
}
}  // namespace loom_server::method_ui

namespace loom_server {
inline void register_method_ui_routes(httplib::Server& server, LoomContext* context) {
  server.Get("/api/methods/chat-settings", [context](const httplib::Request&, httplib::Response& response) {
    native_ui::protect(response, [&] {
      if (!loom::capi::live(context)) { native_ui::fail(response, {loom::Errc::Unavailable, "native runtime is unavailable"}); return; }
      native_ui::reply(response, method_ui::chat_settings(*context->rt));
    });
  });
  server.Post("/api/methods", [context](const httplib::Request& request, httplib::Response& response) {
    native_ui::protect(response, [&] {
      if (!loom::capi::live(context)) { native_ui::fail(response, {loom::Errc::Unavailable, "native runtime is unavailable"}); return; }
      auto body = method_ui::parse_command(request);
      if (!body) { native_ui::fail(response, body.error()); return; }
      const auto result = method_ui::execute(*context->rt, *body);
      if (!result) native_ui::fail(response, result.error()); else native_ui::reply(response, *result);
    });
  });
  server.Post("/api/graph-reply", [context](const httplib::Request& request, httplib::Response& response) {
    native_ui::protect(response, [&] {
      if (!loom::capi::live(context)) { native_ui::fail(response, {loom::Errc::Unavailable, "native runtime is unavailable"}); return; }
      auto body = method_ui::parse_command(request);
      if (!body) { native_ui::fail(response, body.error()); return; }
      const auto action = body->value("action", body->value("operation", std::string()));
      if (action != "fragment") { native_ui::fail(response, method_ui::invalid("graph reply action must be fragment")); return; }
      const auto result = method_ui::fragment(*context->rt, *body);
      if (!result) native_ui::fail(response, result.error()); else native_ui::reply(response, *result);
    });
  });
}
}  // namespace loom_server
