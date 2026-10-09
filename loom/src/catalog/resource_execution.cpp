#include "loom/catalog.h"

#include "catalog_internal.h"
#include "chat/graph_reply.h"
#include "context/method_registry.h"
#include "loom/db.h"
#include "loom/knowledge_store.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::catalog {
namespace {
Error invalid(std::string message) {
  return Error(Errc::InvalidArgument, "catalog.read_resource: " + std::move(message));
}
Json instrument_origin() {
  return Json{{"kind", "system"}, {"actor", "catalog.read_resource"}, {"model", nullptr},
      {"recipe_sha256", nullptr}, {"response_sha256", nullptr}};
}
Result<Json> accept_result(context::MethodRegistry& registry, const Json& packet, const std::string& target) {
  Json selected = Json::object(), expected = Json::object();
  for (const char* collection : {"entities", "claims", "sources"}) {
    selected[collection] = Json::array();
    expected[collection] = Json::object();
    for (const auto& row : packet[collection]) {
      const auto id = std::string_view(collection) == "sources" ? row["observation"]["id"] : row["id"];
      selected[collection].push_back(id);
      expected[collection][id.get<std::string>()] = nullptr;
    }
  }
  return registry.accept(Json{{"operation", "accept"}, {"packet", packet}, {"target", target},
      {"selection", selected}, {"expected_rows", expected}, {"explicitly_accepted", true}});
}
struct ResourceReadPlan {
  RuntimeProfile descriptor;
  RuntimeProfile read_profile;
  Json snapshot;
  Json leaf;
};
Result<ResourceReadPlan> resolve_resource_read(Runtime& runtime, context::MethodRegistry& registry,
                                              const Json& read_options) {
    LOOM_TRY_ASSIGN(auto descriptor, RuntimeProfile::load("resource_method", runtime.paths().root));
    const auto& method = descriptor.values();
    LOOM_TRY_ASSIGN(auto snapshot, registry.load(method.at("profile")));
    const Json host{{"execution", Json{{"catalog.read_resource", Json{{"available", true},
        {"authorization", "host_argument"}, {"transport", "local_source"}}}}}};
    LOOM_TRY_ASSIGN(auto plan, registry.resolve(snapshot, Json::object(), host));
    if (plan["leaves"].size() != 1) return invalid("one direct resource executor required");
    const auto& leaf = plan["leaves"][0];
    if (!leaf.value("available", false) || leaf["execution_capability"] != "catalog.read_resource")
      return Error(Errc::Unavailable, "catalog.read_resource: selected executor is unavailable");
    if (leaf["path"].size() != 1 || leaf["weight"] != 1 || !leaf["fusions"].empty() ||
        !leaf["recipe"].is_null() || !leaf["prompt"].is_null())
      return invalid("resource executor does not implement composition, weighting, prompt or recipe dispatch");
    const auto& parameters = leaf["effective_parameters"];
    if (!parameters.is_object() || parameters.size() != 1 || !parameters.contains("read_options") ||
        !parameters["read_options"].is_object()) return invalid("method parameters require only read_options");
    LOOM_TRY_ASSIGN(auto read_profile, RuntimeProfile::load("resource_read", runtime.paths().root, parameters["read_options"]));
    LOOM_TRY_ASSIGN(read_profile, read_profile.with_overrides(read_options));
    if (!method.contains("result_kind") || !method["result_kind"].is_string() || method["result_kind"].get_ref<const std::string&>().empty())
      return invalid("result_kind is missing from descriptor data");

    return ResourceReadPlan{std::move(descriptor), std::move(read_profile), std::move(snapshot), leaf};
}

}

Result<Json> Catalog::resource_read_configuration(const Json& read_options) {
  try {
    context::MethodRegistry registry(rt_.db());
    LOOM_TRY_ASSIGN(auto resolved, resolve_resource_read(rt_, registry, read_options));
    return resolved.read_profile.inspection();
  } catch (const std::exception& error) {
    return invalid(error.what());
  }
}

Result<Json> Catalog::execute_resource(std::string_view unit_id, const Json& read_options, bool read_authorized) {
  // This argument belongs to the authenticated/native host. Neither source
  // contents, selection, a discovered descriptor nor a graph receipt grants it.
  if (!read_authorized) return Error(Errc::Auth, "catalog.read_resource: source read was not authorized");
  if (unit_id.empty()) return invalid("nonempty unit_id required");
  try {
    context::MethodRegistry registry(rt_.db());
    LOOM_TRY_ASSIGN(auto resolved, resolve_resource_read(rt_, registry, read_options));
    const auto& descriptor = resolved.descriptor;
    const auto& method = descriptor.values();
    const auto& read_profile = resolved.read_profile;
    const auto& snapshot = resolved.snapshot;
    const auto& leaf = resolved.leaf;

    // Point-read catalog metadata only after permission and implementation
    // checks. No source bytes or catalog sketches enter the method packet.
    auto transaction_lock = rt_.db().lock();
    sql::Txn transaction(rt_.db().conn());
    LOOM_TRY(transaction.begin_status());
    LOOM_TRY(Catalog::ensure_schema(rt_.db()));
    CatalogUnit unit;
    {
      auto lock = rt_.db().lock();
      LOOM_TRY_ASSIGN(auto body, rt_.db().conn().query_text("SELECT body FROM loom_cat_units WHERE id = ?", std::string(unit_id)));
      if (!body) return Error(Errc::NotFound, "catalog.read_resource: unit not found");
      LOOM_TRY_ASSIGN(auto document, json::parse(*body));
      LOOM_TRY_ASSIGN(unit, CatalogUnit::from_json(document));
    }
    const Json input{{"unit_id", unit_id}, {"source", unit.unit.source},
        {"content_hash", unit.content_hash}, {"selector", unit.unit.locator.to_json()}};
    const Json effective{{"resource", input}, {"read_options", read_profile.values()},
        {"read_profile_sha256", read_profile.hash()}, {"method_profile_sha256", descriptor.hash()},
        {"implicit_context_eligible", method.at("implicit_context_eligible")}};
    const auto run_id = gen_id("resource_read_");
    const auto known = timeutil::utc_now_iso();
    const auto origin = instrument_origin();
    LOOM_TRY_ASSIGN(auto prepared, registry.prepare(snapshot, leaf,
        Json{{"run_id", run_id}, {"origin", origin}, {"known_at", known},
            {"input_sha256", Sha256::hex(json::canonical(input))}, {"effective_parameters", effective}},
        chat::graph_reply_packet_operation));

    auto read = read_resource(unit_id, read_profile.values());
    Json output;
    if (read) output = std::move(*read);
    else output = Json{{"status", "unavailable"}, {"current", false}, {"last_successful", nullptr},
        {"error", Json{{"code", errc_name(read.error().code)}, {"message", read.error().message}}}};
    const bool current = output.value("current", false);
    Json evidence{{"resource", input}, {"status", output.at("status")}, {"current", current},
        {"availability", current ? "ok" : "unavailable"},
        {"read_configuration", read_profile.inspection()}, {"read_scope", nullptr},
        {"mapping_version", nullptr}, {"mapping_status", "not_attempted"}, {"coverage", nullptr},
        {"source_index", nullptr}, {"mapping_index_scope", nullptr},
        {"authorization", Json{{"read", true}, {"source", "host_argument"}, {"grants_egress", false}}},
        {"validation", Json{{"unit_content_hash_verified", current}, {"domain_correctness", "not_established"}}}};
    if (output.contains("error") && output["error"].is_object())
      evidence["error_code"] = output["error"].value("code", Json(nullptr));
    // A retained snapshot may remain inspectable on failure, but it is never
    // evidence of this attempt's current parser, mapping, bytes or scope.
    if (current) {
      const auto& fresh = output.at("last_successful");
      for (const char* field : {"mapping_version", "mapping_status", "coverage", "read_scope", "read_configuration", "projection_profile", "source_index", "mapping_index_scope"})
        evidence[field] = fresh.at(field);
    }
    model::Entity result;
    result.kind = method["result_kind"].get<std::string>();
    result.canonical_key = run_id;
    result.id = model::Entity::make_id(result.kind, run_id);
    result.label = result.id;
    result.first_seen = result.last_seen = known;
    result.evidence = model::EvidenceClass::Derived;
    result.origin = model::Origin::System;
    result.confidence = 1;
    result.attrs = evidence;
    LOOM_TRY_ASSIGN(auto diff, chat::graph_reply_packet_operation(Json{{"operation", "empty_diff"},
        {"packet", prepared["packet"]}, {"proposal_id", run_id}, {"origin", origin}, {"known_at", known}}));
    diff["entities"]["add"].push_back(result.to_json());
    LOOM_TRY_ASSIGN(auto projected, chat::graph_reply_packet_operation(Json{{"operation", "preview"},
        {"packet", prepared["packet"]}, {"diff", diff}}));
    Json measurements = Json::object();
    if (current) measurements["unit_bytes"] = evidence["read_scope"]["unit_bytes"];
    Json instrumentation = evidence;
    instrumentation["result_metadata_sha256"] = Sha256::hex(json::canonical(result.attrs));
    instrumentation["result_entity_id"] = result.id;
    LOOM_TRY_ASSIGN(auto bound, registry.bind_results(projected["candidate_packet"], prepared["manifest"],
        Json{{"origin", origin}, {"known_at", known}, {"result_entity_ids", Json::array({result.id})},
            {"instrumentation", instrumentation}, {"measurements", measurements},
            {"availability", current ? "ok" : "unavailable"},
            {"input_sha256", prepared["manifest"]["trace"]["input_sha256"]}}, chat::graph_reply_packet_operation));
    LOOM_TRY_ASSIGN(auto accepted, accept_result(registry, bound["packet"], run_id));
    // Eligibility for an implicit context query is independent of source
    // retention and explicit receipt inspection. Publish it atomically with
    // the receipt, preserving the store's existing summary and identity.
    kb::KnowledgeStore knowledge(rt_.db());
    const auto stored_run_id = accepted["receipt"]["run_id"].get<std::string>();
    LOOM_TRY_ASSIGN(auto stored_run, knowledge.get_run(stored_run_id));
    if (!stored_run) return Error(Errc::Database, "catalog.read_resource: accepted run is missing");
    Json summary = stored_run->summary;
    summary["implicit_context_eligible"] = method.at("implicit_context_eligible");
    LOOM_TRY(knowledge.finish_run(stored_run_id, stored_run->status, summary));
    output["method_manifest"] = bound["manifest"];
    output["method_receipt"] = Json{{"receipt_id", accepted["receipt"]["id"]},
        {"run_id", accepted["receipt"]["run_id"]}, {"packet_id", bound["packet"]["packet_id"]}};
    const auto& bindings = bound["manifest"]["bindings"];
    output["produced_by"] = Json{{"result_entity_id", result.id}, {"run_id", bindings["run_id"]},
        {"method_version_id", bindings["method_version_id"]}, {"parameter_set_version_id", bindings["parameter_set_version_id"]}};
    LOOM_TRY(transaction.commit());
    return output;
  } catch (const std::exception& error) {
    return invalid(error.what());
  }
}
}  // namespace loom::catalog
