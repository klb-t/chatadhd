// Explicit read boundary over existing Catalog access/mapping and native store.
// No parser, source executor, persistent message projection or egress is added.
#include "loom/runtime.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "loom/catalog.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/provenance.h"
#include "loom/util/sha256.h"
#include "util/json_value.h"

namespace loom {
namespace {

Json failure(const Error& error) {
  return Json{{"code", errc_name(error.code)}, {"message", error.message}};
}
Json row_capabilities(bool stored) {
  return Json{{"edit", stored}, {"set_status", stored}, {"restore", stored}, {"native_lookup", stored}};
}
Json native_row(const Message& message) {
  Json result = message.to_json();
  result["storage"] = "native";
  result["capabilities"] = row_capabilities(true);
  return result;
}
Json base_locator(Json locator) {
  if (locator.is_object()) locator.erase("catalog_link_binding");
  return locator;
}
bool same(const Json& a, const Json& b) { return json::equivalent(a, b); }

struct Journal {
  std::string unit_id, source_id, raw_blob;
};
struct Binding {
  std::string placeholder_id;
  std::string evidence;
};
Result<Binding> resolve_binding(Runtime& runtime, const std::string& conversation_id,
    const Journal& journal, const catalog::CatalogUnit& unit, const std::vector<Message>& messages,
    const std::vector<ProvenanceRecord>& conversation_provenance) {
  LOOM_TRY_ASSIGN(auto source, runtime.provenance().get_source(journal.source_id));
  const auto expected_uri = unit.unit.source + (unit.unit.locator.member.empty() ? "" : "!" + unit.unit.locator.member);
  if (!source || source->kind != "catalog_unit" || source->uri != expected_uri ||
      unit.unit.source != unit.unit.locator.source ||
      unit.unit.id != model::Unit::make_id(unit.unit.source, unit.unit.locator) ||
      json::get_string(source->metadata, "unit_id") != journal.unit_id)
    return Error(Errc::Conflict, "catalog link source/journal binding is unresolved");
  const auto locator = unit.unit.locator.to_json();
  std::vector<ProvenanceRecord> conversation_links;
  for (const auto& provenance : conversation_provenance)
    if (provenance.subject_kind == "conversation" && provenance.transform == "catalog.import.link@1")
      conversation_links.push_back(provenance);
  if (conversation_links.size() != 1 || conversation_links[0].source_id != journal.source_id ||
      !same(base_locator(conversation_links[0].locator), locator))
    return Error(Errc::Conflict, "catalog link conversation provenance is ambiguous or changed");
  LOOM_TRY_ASSIGN(auto source_provenance, runtime.provenance().for_source(journal.source_id, -1));
  std::vector<ProvenanceRecord> placeholders;
  for (const auto& provenance : source_provenance) {
    if (provenance.subject_kind != "message" || provenance.transform != "catalog.import.link@1") continue;
    // The source may have other projections; only rows belonging to this
    // conversation can establish this view's placeholder binding.
    auto row = std::find_if(messages.begin(), messages.end(), [&](const Message& message) {
      return message.id == provenance.subject_id;
    });
    if (row != messages.end()) placeholders.push_back(provenance);
  }
  if (placeholders.size() != 1 || !same(base_locator(placeholders[0].locator), locator))
    return Error(Errc::Conflict, "catalog link placeholder is missing or ambiguous");
  const auto& provenance = placeholders[0];
  const auto row = std::find_if(messages.begin(), messages.end(), [&](const Message& message) {
    return message.id == provenance.subject_id;
  });
  if (row->status != msg_status::kActive || row->version_num != 1)
    return Error(Errc::Conflict, "catalog link placeholder was excluded, deleted or versioned");
  if (!row->version_group_id) return Error(Errc::Conflict, "catalog link placeholder version identity is missing");
  LOOM_TRY_ASSIGN(auto versions, runtime.db().get_versions(*row->version_group_id));
  if (versions.size() != 1 || versions[0].id != row->id)
    return Error(Errc::Conflict, "catalog link placeholder has local version history");
  NewMessage actual;
  actual.conv_id = row->conv_id; actual.role = row->role; actual.text = row->text;
  actual.model = row->model; actual.parent_id = row->parent_id; actual.weight = row->weight;
  actual.attachments = row->attachments; actual.metadata = row->metadata;
  if (!actual.metadata.is_object()) return Error(Errc::Conflict, "catalog link placeholder metadata changed");
  const auto expected = catalog::make_link_placeholder(unit, conversation_id);
  const auto identity = catalog::link_placeholder_identity(actual);
  const auto expected_identity = catalog::link_placeholder_identity(expected);
  if (!same(identity.at("metadata"), expected_identity.at("metadata")))
    return Error(Errc::Conflict, "catalog link placeholder source metadata changed; stored row retained");
  const auto* message_proof = json::find(provenance.locator, "catalog_link_binding");
  const auto* conversation_proof = json::find(conversation_links[0].locator, "catalog_link_binding");
  if (message_proof || conversation_proof) {
    if (!message_proof || !conversation_proof || !same(*message_proof, *conversation_proof) ||
        json::get_string(*message_proof, "schema") != "loom.catalog_link_binding/1" ||
        json::get_string(*message_proof, "placeholder_id") != row->id ||
        json::get_string(*message_proof, "identity_sha256") != Sha256::hex(json::canonical(identity)))
      return Error(Errc::Conflict, "catalog link placeholder proof is inconsistent");
    return Binding{row->id, "journal_provenance_fingerprint"};
  }
  if (!same(identity, expected_identity))
    return Error(Errc::Conflict, "historical catalog link writer state is uncertain or locally edited; stored row retained");
  // Historical imports have no fingerprint. The same existing writer contract
  // plus both unique provenance records establishes an unchanged legacy row.
  // Semantic/local metadata additions are retained in resources[].placeholder.
  return Binding{row->id, "journal_provenance_legacy_writer"};
}

Result<Json> projected_messages(const Json& snapshot, const std::string& conversation_id,
                                const std::string& unit_id, std::string& version) {
  if (!snapshot.is_object() || !snapshot.contains("mapping") || !snapshot["mapping"].is_object())
    return Error(Errc::Unavailable, "complete export mapping is unavailable for this snapshot");
  const auto& mapping = snapshot["mapping"];
  if (json::get_string(mapping, "schema") != "loom.export_mapping/1" ||
      !mapping.contains("messages") || !mapping["messages"].is_array() ||
      !mapping.contains("groups") || !mapping["groups"].is_number_integer() ||
      mapping["groups"].get<std::int64_t>() < 0 ||
      json::get_string(mapping, "parser_version") != json::get_string(snapshot, "mapping_version"))
    return Error(Errc::Parse, "resource export mapping contract is incomplete");
  const auto groups = mapping["groups"].get<std::int64_t>();
  const auto& rows = mapping["messages"];
  const Json identity{{"unit_id", unit_id}, {"source", snapshot.at("source")},
    {"selector", snapshot.at("selector")}, {"content_hash", snapshot.at("content_hash")},
    {"mapping_schema", mapping.at("schema")}, {"mapping_version", mapping.at("parser_version")},
    {"source_index", snapshot.at("source_index")}, {"mapping_index_scope", snapshot.at("mapping_index_scope")}};
  version = Sha256::hex(json::canonical(identity));
  std::vector<std::string> ids;
  ids.reserve(rows.size());
  for (std::size_t index = 0; index < rows.size(); ++index)
    ids.push_back("refmsg:" + version + ":" + std::to_string(index));
  Json output = Json::array();
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto& row = rows[index];
    if (!row.is_object() || !row.contains("role") || !row["role"].is_string() ||
        !row.contains("text") || !row["text"].is_string() || !row.contains("status") || !row["status"].is_string() ||
        !row.contains("parent_index") || !row["parent_index"].is_number_integer() ||
        !row.contains("group") || !row["group"].is_number_integer() ||
        !row.contains("version_num") || !row["version_num"].is_number_integer() ||
        row["version_num"].get<std::int64_t>() < 1 || !row.contains("model") ||
        !(row["model"].is_null() || row["model"].is_string()) || !row.contains("weight") ||
        !row["weight"].is_number() || !std::isfinite(row["weight"].get<double>()) ||
        !row.contains("attachments") || !row.contains("export") || !row["export"].is_object())
      return Error(Errc::Parse, "resource mapped message contract is incomplete");
    const auto parent = row["parent_index"].get<std::int64_t>();
    const auto group = row["group"].get<std::int64_t>();
    if (parent < -1 || (parent >= 0 && static_cast<std::uint64_t>(parent) >= ids.size()) ||
        group < -1 || group >= groups)
      return Error(Errc::Parse, "resource mapped parent or version group is out of range");
    Json source_ref = identity;
    source_ref["message_index"] = index;
    source_ref["source_key"] = row.at("key");
    source_ref["version"] = version;
    output.push_back(Json{{"id", ids[index]}, {"conv_id", conversation_id},
      {"parent_id", parent < 0 ? Json(nullptr) : Json(ids[static_cast<std::size_t>(parent)])},
      {"role", row["role"]}, {"text", row["text"]}, {"model", row["model"]},
      {"status", row["status"]}, {"weight", row["weight"]},
      {"version_group_id", group < 0 ? Json(nullptr) : Json("refvg:" + version + ":" + std::to_string(group))},
      {"version_num", row["version_num"]}, {"attachments", row["attachments"]},
      {"metadata", {{"export", row["export"]}}}, {"created", nullptr},
      {"storage", "reference"}, {"source_ref", source_ref}, {"capabilities", row_capabilities(false)}});
  }
  return output;
}
}  // namespace

Result<Json> Runtime::read_conversation_view(std::string_view conversation_id, const Json& options,
                                             bool host_local_read_authorized) {
  if (conversation_id.empty() || !options.is_object())
    return Error(Errc::InvalidArgument, "conversation view requires an id and object options");
  for (const auto& [key, value] : options.items()) {
    if (key != "read_options" || !value.is_object())
      return Error(Errc::InvalidArgument, "conversation view supports only object read_options");
  }
  try {
    // Serializes authority and the native row snapshot with local edits. The
    // Catalog executor owns its source/hash validation and receipt transaction.
    auto lock = db().lock();
    LOOM_TRY_ASSIGN(auto conversation, db().get_conv(conversation_id));
    if (!conversation) return Error(Errc::NotFound, "conversation view does not exist");
    LOOM_TRY_ASSIGN(auto stored, db().get_msgs(conversation_id, true));
    Json messages = Json::array(), resources = Json::array(), omissions = Json::array();
    for (const auto& message : stored) messages.push_back(native_row(message));
    std::vector<Journal> journal;
    if (db().conn().has_table("loom_cat_imports")) {
      LOOM_TRY_ASSIGN(auto query, db().conn().prepare(
        "SELECT unit_id, source_id, raw_blob FROM loom_cat_imports WHERE conv_id=? ORDER BY unit_id"));
      query.bind(1, conversation_id);
      while (true) {
        LOOM_TRY_ASSIGN(auto row, query.step());
        if (!row) break;
        journal.push_back(Journal{query.get_text(0), query.get_text(1), query.get_text(2)});
      }
    }
    LOOM_TRY_ASSIGN(auto conversation_provenance, provenance().for_subject(conversation_id));
    const bool conversation_linked = std::any_of(conversation_provenance.begin(), conversation_provenance.end(), [](const auto& row) {
      return row.subject_kind == "conversation" && row.transform == "catalog.import.link@1";
    });
    LOOM_TRY_ASSIGN(auto message_links, db().conn().query_int(
      "SELECT COUNT(*) FROM loom_provenance p JOIN messages m ON m.id=p.subject_id "
      "WHERE m.conv_id=? AND p.subject_kind='message' AND p.transform='catalog.import.link@1'", conversation_id));
    const bool linked = conversation_linked || message_links.value_or(0) > 0;
    Json read_configuration = nullptr, configuration_error = nullptr;
    std::map<std::string, Json> replacements;
    std::set<std::string> placeholder_ids, anchor_ids;
    if (linked) {
      LOOM_TRY_ASSIGN(auto pack, knowledge().pack());
      catalog::Catalog catalog(*this, std::move(pack));
      auto configuration = catalog.resource_read_configuration(options.value("read_options", Json::object()));
      if (configuration) read_configuration = *configuration;
      else configuration_error = failure(configuration.error());
      if (journal.empty()) {
        resources.push_back(Json{{"status", "binding_unresolved"}, {"current", false},
          {"error", failure(Error(Errc::Conflict, "catalog link import journal is missing"))}});
        omissions.push_back(Json{{"scope", "reference_history"}, {"reason", "binding_unresolved"}});
      }
      for (const auto& imported : journal) {
        Json resource{{"unit_id", imported.unit_id}, {"source_id", imported.source_id},
          {"source_storage", imported.raw_blob.empty() ? "reference" : "retained_copy"},
          {"status", "not_attempted"}, {"current", false}, {"mapping_status", "not_attempted"},
          {"authorization", {{"local_read", host_local_read_authorized ? "granted" : "denied"},
                             {"source", "host_argument"}, {"grants_egress", false}}}};
        auto reject = [&](std::string_view status, const Error& error) {
          resource["status"] = status; resource["error"] = failure(error);
          omissions.push_back(Json{{"unit_id", imported.unit_id}, {"scope", "reference_history"}, {"reason", status}});
        };
        auto body = db().conn().query_text("SELECT body FROM loom_cat_units WHERE id=?", imported.unit_id);
        if (!body || !*body || journal.size() != 1) {
          reject("binding_unresolved", body ? Error(Errc::Conflict, "catalog link unit is missing or journal is ambiguous") : body.error());
          resources.push_back(resource); continue;
        }
        auto parsed = json::parse(**body);
        if (!parsed) { reject("binding_unresolved", parsed.error()); resources.push_back(resource); continue; }
        auto unit = catalog::CatalogUnit::from_json(*parsed);
        if (!unit || unit->unit.id != imported.unit_id) {
          reject("binding_unresolved", unit ? Error(Errc::Conflict, "catalog unit identity differs from journal") : unit.error());
          resources.push_back(resource); continue;
        }
        resource["source"] = unit->unit.source;
        resource["selector"] = unit->unit.locator.to_json();
        resource["content_hash"] = unit->content_hash;
        auto binding = resolve_binding(*this, std::string(conversation_id), imported, *unit, stored, conversation_provenance);
        if (!binding) { reject("binding_unresolved", binding.error()); resources.push_back(resource); continue; }
        placeholder_ids.insert(binding->placeholder_id);
        const bool anchor = std::any_of(stored.begin(), stored.end(), [&](const auto& row) {
          return row.parent_id && *row.parent_id == binding->placeholder_id;
        });
        if (anchor) anchor_ids.insert(binding->placeholder_id);
        resource["binding"] = Json{{"placeholder_id", binding->placeholder_id}, {"evidence", binding->evidence},
                                   {"placeholder_retained_as_anchor", anchor}};
        const auto placeholder = std::find_if(stored.begin(), stored.end(), [&](const auto& row) { return row.id == binding->placeholder_id; });
        resource["placeholder"] = native_row(*placeholder); // local metadata/evidence is never discarded
        if (!host_local_read_authorized) {
          reject("read_denied", Error(Errc::Auth, "source read was not authorized"));
          resources.push_back(resource); continue;
        }
        if (!configuration_error.is_null()) {
          resource["status"] = "unavailable"; resource["error"] = configuration_error;
          omissions.push_back(Json{{"unit_id", imported.unit_id}, {"scope", "reference_history"}, {"reason", "configuration_unavailable"}});
          resources.push_back(resource); continue;
        }
        auto read = catalog.execute_resource(imported.unit_id, options.value("read_options", Json::object()), true);
        if (!read) { reject("unavailable", read.error()); resources.push_back(resource); continue; }
        for (const char* key : {"status", "current", "error", "produced_by", "method_manifest", "method_receipt"})
          if (read->contains(key)) resource[key] = (*read)[key];
        if (!read->value("current", false)) {
          const auto& previous = read->at("last_successful");
          resource["historical_snapshot"] = previous.is_object()
            ? Json{{"available", true}, {"current", false}, {"source", previous.value("source", Json(nullptr))},
                   {"content_hash", previous.value("content_hash", Json(nullptr))}, {"mapping_available", previous.contains("mapping")}}
            : Json{{"available", false}};
          omissions.push_back(Json{{"unit_id", imported.unit_id}, {"scope", "reference_history"}, {"reason", resource["status"]}});
          resources.push_back(resource); continue;
        }
        const auto& snapshot = read->at("last_successful");
        read_configuration = snapshot.at("read_configuration");
        for (const char* key : {"mapping_status", "mapping_version", "source_index", "mapping_index_scope", "coverage", "read_configuration", "read_scope", "projection_profile"})
          resource[key] = snapshot.at(key);
        std::string version;
        auto projected = projected_messages(snapshot, std::string(conversation_id), imported.unit_id, version);
        if (snapshot.at("mapping_status") != "recognized" || snapshot.at("coverage") != "provider_conversation" || !projected) {
          resource["mapping_status"] = "uncertain";
          resource["error"] = failure(projected ? Error(Errc::Unavailable, "source domain is not a complete conversation") : projected.error());
          omissions.push_back(Json{{"unit_id", imported.unit_id}, {"scope", "reference_history"}, {"reason", "mapping_unavailable"}});
          resources.push_back(resource); continue;
        }
        resource["version"] = version;
        resource["mapping"] = Json{{"schema", snapshot["mapping"]["schema"]}, {"parser_version", snapshot["mapping"]["parser_version"]},
          {"source_index", snapshot["source_index"]}, {"mapping_index_scope", snapshot["mapping_index_scope"]}};
        resource["conversation"] = Json{{"title", snapshot["mapping"]["title"]}, {"metadata", {{"export", snapshot["mapping"]["export"]}}}};
        replacements.emplace(binding->placeholder_id, std::move(*projected));
        resources.push_back(resource);
      }
      messages = Json::array();
      for (const auto& message : stored) {
        const auto replaced = replacements.find(message.id);
        if (replaced == replacements.end()) messages.push_back(native_row(message));
        else {
          if (anchor_ids.contains(message.id)) {
            auto anchor = native_row(message); anchor["reference_anchor"] = true; messages.push_back(anchor);
          }
          for (const auto& row : replaced->second) messages.push_back(row);
        }
      }
    }
    const bool has_native_content = std::any_of(stored.begin(), stored.end(), [&](const auto& row) {
      return !placeholder_ids.contains(row.id);
    });
    const std::string status = omissions.empty() ? "complete" :
      (!replacements.empty() || has_native_content ? "partial" : "unavailable");
    Json versions = Json::array();
    for (const auto& resource : resources)
      versions.push_back(Json{{"unit_id", resource.value("unit_id", Json(nullptr))},
        {"status", resource.at("status")}, {"current", resource.at("current")},
        {"version", resource.value("version", Json(nullptr))}, {"mapping_status", resource.value("mapping_status", Json(nullptr))}});
    const auto view_id = "cv:" + Sha256::hex(json::canonical(Json{{"messages", messages}, {"resources", versions}}));
    Json result{{"schema", "loom.conversation_view/1"}, {"conversation_id", conversation_id}, {"view_id", view_id},
      {"status", status}, {"messages", messages}, {"resources", resources}, {"omissions", omissions},
      {"read_configuration", read_configuration},
      {"capabilities", {{"source_history_send", {{"available", false}, {"reason", linked ? "source_egress_not_bound" : "not_applicable"}}}}}};
    if (!configuration_error.is_null()) result["configuration_error"] = configuration_error;
    return result;
  } catch (const std::exception& error) {
    return Error(Errc::InvalidArgument, "conversation view: " + std::string(error.what()));
  }
}
}  // namespace loom
