// Source acquisition is the existing ConversationView/Catalog operation.
// Ranking, evidence, diversity and budgeting stay in ContextEngine.
#include "loom/runtime.h"

#include <limits>
#include <map>
#include <set>
#include <tuple>

#include "loom/catalog.h"
#include "loom/knowledge.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/runtime_profile.h"
#include "loom/util/sha256.h"

namespace loom {
namespace {

Json project(const Json& value, std::initializer_list<const char*> keys) {
  Json result = Json::object();
  for (const auto* key : keys) if (value.contains(key)) result[key] = value.at(key);
  return result;
}

// Explicit projections, never arbitrary source metadata, error bodies or the
// text-bearing method manifest. These identities refer only to the actual read.
Json read_ref(const Json& resource) {
  Json result = Json::object();
  if (resource.contains("produced_by")) result["produced_by"] = project(resource.at("produced_by"),
    {"result_entity_id", "run_id", "method_version_id", "parameter_set_version_id"});
  if (resource.contains("method_receipt")) result["method_receipt"] = project(resource.at("method_receipt"),
    {"receipt_id", "run_id", "packet_id"});
  return result;
}

Json source_ref(const Json& row) {
  return project(row.at("source_ref"), {"unit_id", "source", "selector", "content_hash",
    "mapping_schema", "mapping_version", "source_index", "mapping_index_scope", "message_index", "version"});
}

struct Policy {
  RuntimeProfile profile;
  Json explanation;
};

Result<Policy> policy_for(Runtime& runtime, const SourceContextRequest& request) {
  if (request.profile_user.empty()) return Error(Errc::InvalidArgument, "source context requires an explicit profile identity");
  LOOM_TRY_ASSIGN(auto descriptor, RuntimeProfile::builtin("context_source"));
  LOOM_TRY_ASSIGN(auto binding_descriptor, RuntimeProfile::builtin("context_source_binding"));
  const auto& binding = binding_descriptor.values();
  onboarding::OnboardingStore store(runtime.db());
  LOOM_TRY_ASSIGN(auto state, store.read(request.profile_user));
  LOOM_TRY_ASSIGN(auto layers, onboarding::DefaultLayers::create(state.at("pack"), state.at("layers")));
  LOOM_TRY_ASSIGN(auto effective, layers.resolve(binding.at("entry").at("key").get<std::string>()));
  if (effective.at("status") != "effective")
    return Error(Errc::Unavailable, "source context layer is " + effective.at("status").get<std::string>());
  LOOM_TRY_ASSIGN(auto profile, descriptor.with_values(effective.at("value")));
  LOOM_TRY_ASSIGN(profile, profile.with_overrides(request.source_profile_overrides));
  // Explanation is a known identity projection. A user override's source may
  // contain arbitrary private annotations; neither it nor translated prose is
  // copied into this receipt. Effective values are schema-validated policy data.
  Json explanation = project(effective, {"key", "id", "area", "revision", "status", "layer"});
  explanation["profile_identity_sha256"] = Sha256::hex(request.profile_user);
  explanation["profile_revision"] = state.at("revision");
  explanation["graph_run_id"] = state.at("graph_run_id");
  explanation["profile_state_sha256"] = Sha256::hex(json::canonical(state));
  explanation["profile_state_hash_scope"] = "canonical_onboarding_snapshot";
  explanation["binding_descriptor_sha256"] = binding_descriptor.hash();
  explanation["pack_id"] = state.at("pack").at("pack_id");
  explanation["pack_revision"] = state.at("pack").at("revision");
  explanation["resolved_values_sha256"] = Sha256::hex(json::canonical(effective.at("value")));
  explanation["one_call_overrides_sha256"] = Sha256::hex(json::canonical(request.source_profile_overrides));
  return Policy{std::move(profile), std::move(explanation)};
}

Status validate_selectors(const SourceContextRequest& request) {
  if (!request.read_options.is_object() || !request.source_profile_overrides.is_object())
    return Error(Errc::InvalidArgument, "source context options must be objects");
  std::map<std::string, std::pair<std::string, std::optional<std::string>>> units;
  std::set<std::tuple<std::string, std::string, std::size_t>> selected;
  for (const auto& fragment : request.fragments) {
    if (fragment.conversation_id.empty() || fragment.unit_id.empty() ||
        (fragment.expected_version && fragment.expected_version->empty()))
      return Error(Errc::InvalidArgument, "source context selector identity is empty");
    const auto [found, added] = units.emplace(fragment.unit_id, std::make_pair(fragment.conversation_id, fragment.expected_version));
    if (!added && found->second != std::make_pair(fragment.conversation_id, fragment.expected_version))
      return Error(Errc::Conflict, "source context selectors disagree about a unit binding or version");
    for (const auto index : fragment.message_indices) {
      if (index > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return Error(Errc::InvalidArgument, "source context ordinal exceeds the native Observation representation");
      if (!selected.emplace(fragment.conversation_id, fragment.unit_id, index).second)
        return Error(Errc::Conflict, "source context fragment requested more than once");
    }
  }
  return ok_status();
}

Json requested(const SourceFragmentRequest& fragment) {
  return Json{{"conversation_id", fragment.conversation_id}, {"unit_id", fragment.unit_id},
    {"message_indices", fragment.message_indices},
    {"expected_version", fragment.expected_version ? Json(*fragment.expected_version) : Json(nullptr)}};
}

} // namespace

Result<Json> Runtime::source_context_defaults() {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::builtin("context_source"));
  LOOM_TRY_ASSIGN(auto binding, RuntimeProfile::builtin("context_source_binding"));
  Json extension = binding.values();
  Json entry = extension.at("entry");
  extension.erase("entry");
  entry["value"] = profile.defaults(); // exactly one canonical source of defaults
  extension["entries"] = Json::array({entry});
  LOOM_TRY_ASSIGN(auto validated, onboarding::DefaultLayers::create(extension));
  return validated.pack();
}

Result<PreparedSourceContext> Runtime::prepare_source_context(
    const SourceContextRequest& request, bool host_local_read_authorized) {
  // Neither selected source metadata nor installed policy can create authority.
  if (!host_local_read_authorized) return Error(Errc::Auth, "source context local read was not authorized");
  try {
    LOOM_TRY(validate_selectors(request));
    // Keep native policy and link identities stable through acquisition. The
    // existing Database lock is recursive, as used by ConversationView itself.
    auto lock = db().lock();
    LOOM_TRY_ASSIGN(auto policy, policy_for(*this, request));
    LOOM_TRY_ASSIGN(auto pack, knowledge().pack());
    context::ContextEngine engine(*this, knowledge().store(), pack);
    LOOM_TRY_ASSIGN(auto preflight, engine.preflight_source_context(request.context, policy.profile));
    catalog::Catalog catalog(*this, pack);
    LOOM_TRY_ASSIGN(auto read_configuration, catalog.resource_read_configuration(request.read_options));
    if (read_configuration.at("values").at("projection_storage") != "transient")
      return Error(Errc::Unavailable, "source context preparation requires explicitly selected transient resource reads");

    context::SourceContextInput input;
    input.preflight_sha256 = preflight.binding_sha256;
    input.acquisition = Json{{"schema", "loom.source_context_acquisition/1"},
      {"requested", Json::array()}, {"sources", Json::array()}, {"omissions", Json::array()}};
    std::map<std::string, Json> views;
    std::set<std::string> failed_views;
    std::set<std::pair<std::string, std::string>> recorded_sources;
    for (const auto& fragment : request.fragments) {
      input.acquisition["requested"].push_back(requested(fragment));
      auto omit = [&](std::string_view reason, std::optional<std::size_t> index = {}) {
        Json omission{{"conversation_id", fragment.conversation_id}, {"unit_id", fragment.unit_id}, {"reason", reason}};
        if (index) omission["message_index"] = *index;
        input.acquisition["omissions"].push_back(std::move(omission));
      };
      // Empty selection is not a request for the complete source.
      if (fragment.message_indices.empty()) continue;
      // Bind the requested unit before any source I/O. A valid conversation id
      // with an unrelated unit id must not read that conversation's source.
      if (!db().conn().has_table("loom_cat_imports")) { omit("unit_not_bound"); continue; }
      LOOM_TRY_ASSIGN(auto bound, db().conn().prepare("SELECT unit_id FROM loom_cat_imports WHERE conv_id=?"));
      bound.bind(1, fragment.conversation_id);
      LOOM_TRY_ASSIGN(auto has_binding, bound.step());
      if (!has_binding || bound.get_text(0) != fragment.unit_id) { omit("unit_not_bound"); continue; }
      LOOM_TRY_ASSIGN(auto ambiguous, bound.step());
      if (ambiguous) { omit("unit_not_bound"); continue; }
      if (!views.contains(fragment.conversation_id) && !failed_views.contains(fragment.conversation_id)) {
        // Pin the resolved options; a later filesystem overlay change cannot
        // convert this operation to a persistent snapshot during its read.
        auto view = read_conversation_view(fragment.conversation_id,
          Json{{"read_options", read_configuration.at("values")}}, true);
        if (!view) {
          failed_views.insert(fragment.conversation_id);
          input.acquisition["sources"].push_back(Json{{"conversation_id", fragment.conversation_id},
            {"status", "source_read_unavailable"}, {"current", false}, {"error_code", errc_name(view.error().code)}});
        } else views.emplace(fragment.conversation_id, std::move(*view));
      }
      if (failed_views.contains(fragment.conversation_id)) { omit("source_read_unavailable"); continue; }
      const auto& view = views.at(fragment.conversation_id);
      const Json* resource = nullptr;
      for (const auto& candidate : view.at("resources"))
        if (candidate.value("unit_id", std::string()) == fragment.unit_id) { resource = &candidate; break; }
      if (!resource) { omit("unit_not_bound"); continue; }
      const auto evidence = read_ref(*resource);
      if (resource->contains("read_configuration") &&
          resource->at("read_configuration").at("values") != read_configuration.at("values"))
        return Error(Errc::Conflict, "source context resource configuration changed during acquisition");
      if (recorded_sources.emplace(fragment.conversation_id, fragment.unit_id).second) {
        auto identity = project(*resource, {"unit_id", "source_id", "source", "selector", "content_hash", "status", "current",
          "mapping_status", "mapping_version", "source_index", "mapping_index_scope", "coverage", "version", "read_scope"});
        identity["conversation_id"] = fragment.conversation_id;
        identity["read_receipt_ref"] = evidence;
        if (resource->contains("read_configuration"))
          identity["read_configuration"] = project(resource->at("read_configuration"), {"schema", "domain", "revision", "hash", "values"});
        input.acquisition["sources"].push_back(std::move(identity));
      }
      if (!resource->at("current").get<bool>()) { omit(resource->at("status").get<std::string>()); continue; }
      if (resource->at("mapping_status") != "recognized" || !resource->contains("version")) {
        omit("mapping_unavailable"); continue;
      }
      const auto version = resource->at("version").get<std::string>();
      if (fragment.expected_version && *fragment.expected_version != version) { omit("stale_selector"); continue; }

      // Projection identity is independent of which subset was requested. This
      // is a canonical derived document, not a fictional pointer into an export
      // with a different syntax. Its bytes exist only in this call's memory.
      Json rows = Json::array(), projection_rows = Json::array();
      bool complete_indices = true;
      for (const auto& row : view.at("messages")) {
        if (row.at("storage") != "reference" || row.at("source_ref").at("unit_id") != fragment.unit_id) continue;
        if (row.at("source_ref").at("message_index") != rows.size()) complete_indices = false;
        rows.push_back(row);
        projection_rows.push_back(project(row, {"id", "parent_id", "role", "status", "text"}));
      }
      if (!complete_indices) { omit("mapping_unavailable"); continue; }
      Json identity = project(*resource, {"unit_id", "source", "selector", "content_hash", "version", "mapping_version", "source_index", "mapping_index_scope"});
      identity["mapping_schema"] = resource->at("mapping").at("schema");
      const Json projection{{"schema", "loom.source_context_projection/1"}, {"source_identity", identity}, {"messages", projection_rows}};
      const auto projection_hash = Sha256::hex(json::canonical(projection));
      model::Locator root;
      root.source = "sha256:" + projection_hash;
      const auto mapping_unit = model::Unit::make_id(root.source, root);
      for (const auto index : fragment.message_indices) {
        if (index >= rows.size()) { omit("fragment_not_found", index); continue; }
        const auto& row = rows[index];
        context::SourceContextObservation item;
        auto& observation = item.observation;
        observation.unit = mapping_unit;
        observation.kind = model::ObservationKind::Utterance;
        observation.text = row.at("text").get<std::string>();
        observation.locator = root;
        observation.locator.json_pointer = "/messages/" + std::to_string(index) + "/text";
        observation.id = model::Observation::make_id(mapping_unit, observation.locator, observation.text);
        observation.ordinal = static_cast<int>(index);
        observation.speaker = row.at("role").get<std::string>();
        item.message_ref = row.at("id").get<std::string>();
        if (!row.at("parent_id").is_null()) item.parent_ref = row.at("parent_id").get<std::string>();
        item.source_version = version;
        item.message_status = row.at("status").get<std::string>();
        item.source_ref = source_ref(row);
        item.source_ref["mapping_projection"] = Json{{"schema", projection.at("schema")}, {"source", root.source},
          {"unit_id", mapping_unit}, {"hash_scope", "canonical_mapped_projection"},
          {"locator", observation.locator.to_json()}, {"storage", "transient"}};
        item.read_receipt_ref = evidence;
        input.observations.push_back(std::move(item));
      }
    }
    LOOM_TRY_ASSIGN(auto selected, engine.select(request.context, input, policy.profile));
    LOOM_TRY_ASSIGN(auto prompt, engine.render(selected));
    // This is a preparation receipt, not a fabricated registered selector run.
    // produced_by nested under acquisition describes Catalog reads only.
    const bool acquisition_failed = !input.acquisition.at("omissions").empty();
    const std::string status = acquisition_failed ? (input.observations.empty() ? "unavailable" : "partial") :
      (input.observations.empty() ? "empty" : "complete");
    Json configuration{{"domain", policy.profile.domain()}, {"hash", policy.profile.hash()},
      {"values", policy.profile.values()}, {"resolution", policy.explanation}, {"resource_read", read_configuration}};
    Json receipt{{"schema", "loom.source_context_preparation/1"}, {"status", status}, {"context_id", selected.id},
      {"request_sha256", Sha256::hex(json::canonical(request.context.to_json()))},
      {"request_hash_scope", "context_request_json"}, {"effective_configuration", configuration},
      {"acquisition", input.acquisition}, {"selection", selected.goal.params.at("source_context")},
      {"retention", {{"context", "memory"}, {"prompt", "memory"}, {"source_projection", "transient"}}},
      {"authorization", {{"local_read", true}, {"egress", false}}}};
    return PreparedSourceContext{std::move(selected), std::move(prompt), std::move(receipt)};
  } catch (const std::exception&) {
    // Exception text can contain source data; return only a developer code.
    return Error(Errc::InvalidArgument, "source context preparation contract is invalid");
  }
}

} // namespace loom
