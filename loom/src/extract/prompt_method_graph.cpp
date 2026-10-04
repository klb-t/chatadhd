#include "extract/prompt_method_graph.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

#include "loom/model.h"
#include "loom/util/sha256.h"

namespace loom::extract::prompts::method_graph {
namespace {
#include "extract/prompt_method_data.inc"

Error invalid(std::string message) { return Error(Errc::InvalidArgument, std::move(message)); }
Error conflict(std::string message) { return Error(Errc::Conflict, std::move(message)); }
std::string hash(const Json& value) { return Sha256::hex(json::canonical(value)); }
bool same(const Json& a, const Json& b) { return json::canonical(a) == json::canonical(b); }
bool digest(const Json& value) {
  if (!value.is_string()) return false;
  const auto& s = value.get_ref<const std::string&>();
  if (s.size() != 64) return false;
  for (const char c : s) if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
  return true;
}
Status finite(const Json& value) {
  if (value.is_number_float() && !std::isfinite(value.get<double>())) return invalid("method data contains a nonfinite number");
  if (value.is_structured()) for (const auto& child : value) LOOM_TRY(finite(child));
  return {};
}
Result<std::string> required_text(const Json& value, const char* key) {
  const auto* p = json::find(value, key);
  if (!p || !p->is_string() || p->get_ref<const std::string&>().empty())
    return invalid(std::string("method graph requires nonempty ") + key);
  return p->get<std::string>();
}
Status known_at(const Json& value) {
  if (!value.is_null() && !value.is_string()) return invalid("known_at must be a string or null");
  return {};
}
Result<std::string> role(const Json& vocabulary, const char* group, const char* key) {
  const auto* rows = json::find(vocabulary, group);
  if (!rows || !rows->is_object()) return invalid(std::string("method vocabulary requires ") + group);
  return required_text(*rows, key);
}
Status validate_data(const Json& data) {
  LOOM_TRY(finite(data));
  if (!data.is_object() || data.value("schema", Json()) != "loom.analysis_methods/1" ||
      !data.contains("vocabulary") || !data.contains("methods") || !data["methods"].is_object())
    return invalid("analysis method data schema/fields");
  if (!data.contains("default_prompt_ids") || !data["default_prompt_ids"].is_object())
    return invalid("default_prompt_ids must be caller data");
  for (auto it = data["default_prompt_ids"].begin(); it != data["default_prompt_ids"].end(); ++it)
    if (it.key().empty() || !it.value().is_string() || it.value().get_ref<const std::string&>().empty())
      return invalid("default_prompt_ids must contain nonempty names and IDs");
  for (const char* key : {"method", "method_version", "parameter_set_version", "prompt_version", "recipe_version", "preset_version", "run", "result", "model_identity"}) {
    LOOM_TRY(role(data["vocabulary"], "kinds", key));
  }
  for (const char* key : {"version_of", "uses_recipe", "uses_prompt", "uses_preset", "uses_parameter_set", "requests_method_version", "produced_in_run", "produced_by_method_version"}) {
    LOOM_TRY(role(data["vocabulary"], "predicates", key));
  }
  for (const auto& group : {std::pair<const char*, const char*>{"structural_assessment", "confidence"},
                          {"structural_assessment", "support_quality"}, {"result_assessment", "confidence"}}) {
    if (!data.contains(group.first) || !data[group.first].is_object() || !data[group.first].contains(group.second) ||
        !data[group.first][group.second].is_number()) return invalid("method assessment data missing");
    const auto number = data[group.first][group.second].get<double>();
    if (!std::isfinite(number) || number < 0 || number > 1) return invalid("native assessment confidence/quality must be in [0,1]");
  }
  for (auto it = data["methods"].begin(); it != data["methods"].end(); ++it) {
    if (it.key().empty() || !it.value().is_object()) return invalid("method metadata must be a named object");
    LOOM_TRY(required_text(it.value(), "method_key"));
    const auto* capability = json::find(it.value(), "execution_capability");
    if (!capability || !(capability->is_null() || capability->is_string())) return invalid("method execution capability must be a string or null");
  }
  if (!data.contains("selection") || !data["selection"].is_object() ||
      !data["selection"].contains("parameter_layers") || !data["selection"]["parameter_layers"].is_array())
    return invalid("method selection parameter_layers must be caller data");
  for (const auto& layer : data["selection"]["parameter_layers"])
    if (!layer.is_string() || layer.get_ref<const std::string&>().empty()) return invalid("method parameter layer must be a nonempty string");
  if (data.contains("recipe_records") && !data["recipe_records"].is_object()) return invalid("recipe_records must be an object");
  return {};
}
Result<Json> effective_data(const Json& options) {
  if (!options.is_object()) return invalid("method options must be an object");
  Json data = options.value("method_data_snapshot", builtin_method_data());
  if (options.contains("method_data")) {
    if (!options["method_data"].is_object()) return invalid("method_data overlay must be an object");
    data = overlay(data, options["method_data"]);
  }
  LOOM_TRY(validate_data(data));
  return data;
}
Result<Json> source(std::string bytes, const Json& date, const char* capture_role, const std::string& run_id = {}) {
  LOOM_TRY(known_at(date));
  if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
    return invalid("native source byte length representation overflow");
  const auto sha = Sha256::hex(bytes);
  model::Observation o;
  o.unit = "un_" + sha;
  o.kind = model::ObservationKind::Field;
  o.text = std::move(bytes);
  o.locator.source = "sha256:" + sha;
  o.locator.member = run_id.empty() ? std::string(capture_role) : std::string(capture_role) + ":" + run_id;
  o.locator.byte_start = 0;
  o.locator.byte_len = static_cast<std::int64_t>(o.text.size());
  o.id = model::Observation::make_id(o.unit, o.locator, o.text);
  o.date = date.is_string() ? date.get<std::string>() : "";
  o.artifact_type = "loom.method_graph/1";
  o.attrs = Json{{"capture_role", capture_role}, {"content_truth", "not_established"}};
  return Json{{"observation", o.to_json()}, {"known_at", date}, {"text_sha256", sha}};
}
Result<Json> entity(const Json& data, const char* semantic_role, const Json& attrs, const Json& date,
                    const std::string& key = {}, const std::string& id = {}) {
  LOOM_TRY_ASSIGN(auto kind, role(data["vocabulary"], "kinds", semantic_role));
  model::Entity e;
  e.kind = kind;
  e.canonical_key = key.empty() ? hash(attrs) : key;
  e.id = id.empty() ? model::Entity::make_id(kind, e.canonical_key) : id;
  e.label = e.id;
  e.first_seen = date.is_string() ? date.get<std::string>() : "";
  e.last_seen = e.first_seen;
  e.evidence = model::EvidenceClass::Derived;
  e.origin = model::Origin::System;
  e.confidence = data["structural_assessment"]["confidence"].get<double>();
  e.attrs = attrs;
  return e.to_json();
}
std::string row_id(const Json& row, const char* collection) {
  return std::string_view(collection) == "sources" ? row["observation"]["id"].get<std::string>() : row["id"].get<std::string>();
}
Status append(Json& profile, const char* collection, const Json& row) {
  const auto id = row_id(row, collection);
  for (const auto& existing : profile[collection]) if (row_id(existing, collection) == id) {
    if (!same(existing, row)) return conflict("method graph immutable row identity reused with changed content: " + id);
    return {};
  }
  profile[collection].push_back(row);
  return {};
}
Result<Json> relation(const Json& data, const std::string& subject, const char* semantic_role,
                      const std::string& object, const Json& capture) {
  LOOM_TRY_ASSIGN(auto predicate, role(data["vocabulary"], "predicates", semantic_role));
  LOOM_TRY_ASSIGN(auto o, model::Observation::from_json(capture["observation"]));
  model::Claim c;
  c.subject = subject;
  c.predicate = predicate;
  c.object = object;
  c.qualifiers.scope = "loom.method_graph/1";
  c.qualifiers.extra = Json{{"confidence_scope", "structure_only"}, {"content_verification", "unverified"},
                           {"acceptance_establishes_content_truth", false}};
  c.assessment.evidence = model::EvidenceClass::Derived;
  c.assessment.origin = model::Origin::System;
  c.assessment.confidence = data["structural_assessment"]["confidence"].get<double>();
  c.assessment.derivation = model::Derivation{"loom.analysis_method_projection/1", 1, "", 0};
  c.assessment.support.push_back(model::Support{o.id, o.locator, o.text, "loom.analysis_method_projection/1",
                                              data["structural_assessment"]["support_quality"].get<double>()});
  c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  LOOM_TRY(c.validate());
  return c.to_json();
}
Status add_relation(Json& profile, const Json& data, const std::string& subject, const char* name,
                     const std::string& object, const Json& capture) {
  LOOM_TRY_ASSIGN(auto claim, relation(data, subject, name, object, capture));
  return append(profile, "claims", claim);
}
Result<Json> version(Json& profile, const Json& data, const char* role_name, const Json& definition,
                     const Json& date) {
  LOOM_TRY_ASSIGN(auto e, entity(data, role_name, Json{{"definition", definition}, {"definition_sha256", hash(definition)}}, date));
  LOOM_TRY(append(profile, "entities", e));
  // Definition captures are timeless content records. Attempt dates belong
  // to separate run-specific captures, never to a reused content identity.
  LOOM_TRY_ASSIGN(auto capture, source(json::canonical(definition), Json(nullptr), role_name));
  LOOM_TRY(append(profile, "sources", capture));
  return e;
}
Json prompt_preset(const Contract& original) {
  return Json{{"analysis_prompt", original.definition}, {"analysis_prompt_sha256", original.hash},
      {"parameters", Json{{"request_parameters", original.definition["request_parameters"]},
          {"analysis_parameters", original.definition["analysis_parameters"]}, {"transport", original.definition["transport"]},
          {"validation_mode", original.definition["validation_mode"]}, {"output_schema_hash", hash(original.definition["output_schema"])}}}};
}
Status measurements(const Json& value) {
  if (value.is_null()) return {};
  if (!value.is_object()) return invalid("measurements must be an object or null");
  for (const auto& item : value) if (!item.is_null() && !item.is_number()) return invalid("measurements must contain numbers or null");
  return finite(value);
}
Json records(const Json& profile, const Json& bindings) {
  Json out = Json::object();
  for (auto it = bindings.begin(); it != bindings.end(); ++it) {
    if (it.key() == "run_id" || it.value().is_null()) continue;
    for (const auto& e : profile["entities"]) if (e["id"] == it.value()) out[it.key()] = e["attrs"];
  }
  return out;
}
Status profile_rows(const Json& profile) {
  for (const char* name : {"entities", "claims", "sources"})
    if (!profile.contains(name) || !profile[name].is_array()) return invalid("native profile arrays missing");
  std::set<std::string> global_ids;
  for (const char* collection : {"entities", "claims", "sources"}) for (const auto& row : profile[collection]) {
    if (!global_ids.insert(row_id(row, collection)).second) return invalid("native profile record identity collides");
  }
  for (const auto& row : profile["entities"]) {
    LOOM_TRY_ASSIGN(auto e, model::Entity::from_json(row));
    if (!same(e.to_json(), row)) return invalid("native Entity DTO is not lossless");
  }
  for (const auto& row : profile["claims"]) {
    LOOM_TRY_ASSIGN(auto c, model::Claim::from_json(row));
    if (!same(c.to_json(), row)) return invalid("native Claim DTO is not lossless");
  }
  for (const auto& row : profile["sources"]) {
    if (!row.is_object() || !row.contains("observation") || !row.contains("known_at") || !row.contains("text_sha256"))
      return invalid("native source DTO missing fields");
    LOOM_TRY_ASSIGN(auto o, model::Observation::from_json(row["observation"]));
    if (!same(o.to_json(), row["observation"])) return invalid("native Observation DTO is not lossless");
    LOOM_TRY(known_at(row["known_at"]));
    if (!digest(row["text_sha256"]) || row["text_sha256"] != Sha256::hex(o.text)) return conflict("native source hash drift");
  }
  return {};
}
}  // namespace

Json builtin_method_data() {
  return json::parse(kBuiltinMethodData).value();
}

Result<Json> resolve_method_data(const std::filesystem::path& directory, const Json& patch) {
  try {
    if (!patch.is_object()) return invalid("method data patch must be an object");
    Json data = builtin_method_data();
    if (!directory.empty()) {
      const auto path = directory / "analysis_methods.pack";
      std::error_code ec;
      const bool exists = std::filesystem::exists(path, ec);
      if (ec) return Error(Errc::Io, "cannot inspect analysis method overlay: " + ec.message());
      if (exists) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return Error(Errc::Io, "cannot read analysis method overlay");
        std::ostringstream bytes; bytes << file.rdbuf();
        if (file.bad()) return Error(Errc::Io, "cannot read analysis method overlay");
        LOOM_TRY_ASSIGN(auto document, json::parse(bytes.str()));
        if (!document.is_object()) return invalid("analysis method overlay must be an object");
        data = overlay(data, document);
      }
    }
    data = overlay(data, patch);
    LOOM_TRY(validate_data(data));
    return data;
  } catch (const std::exception& e) { return invalid(e.what()); }
}

Result<Json> method_profile(const Contract& contract, const Json& prepared, const Json& parameters,
                            const Json& overrides, const Json& options) {
  try {
    LOOM_TRY_ASSIGN(auto checked, from_snapshot(contract.definition));
    if (checked.hash != contract.hash) return conflict("prompt contract definition/hash drift");
    LOOM_TRY(finite(parameters)); LOOM_TRY(finite(overrides));
    LOOM_TRY_ASSIGN(auto data, effective_data(options));
    const auto id = contract.definition["id"].get<std::string>();
    if (!data["methods"].contains(id)) return Error(Errc::Unavailable, "method metadata is not registered for prompt " + id);
    const auto metadata = data["methods"][id];
    if (!prepared.is_object() || !prepared.contains("body_json") || !prepared["body_json"].is_object() ||
        !prepared.contains("body_bytes") || !prepared["body_bytes"].is_string() ||
        prepared.value("contract_hash", Json()) != contract.hash || !digest(prepared.value("request_hash", Json())))
      return invalid("method profile needs an exact prepared prompt request");
    LOOM_TRY_ASSIGN(auto body, json::parse(prepared["body_bytes"].get<std::string>()));
    if (!same(body, prepared["body_json"])) return conflict("prepared body bytes differ from body JSON");
    if (prepared.value("output_schema_hash", Json()) != hash(contract.definition["output_schema"]))
      return conflict("prepared output schema hash differs from effective contract");
    const auto request_hash = hash(Json{{"method", prepared.at("method")}, {"url", prepared.at("url")},
        {"headers", prepared.at("headers")}, {"body_bytes", prepared["body_bytes"]}, {"transport", prepared.at("transport")},
        {"validation_mode", contract.definition["validation_mode"]}, {"output_schema_hash", prepared["output_schema_hash"]}});
    if (prepared["request_hash"] != request_hash) return conflict("prepared request envelope hash drift");
    const auto date = options.value("known_at", Json(nullptr)); LOOM_TRY(known_at(date));
    Json profile{{"vocabulary", data["vocabulary"]}, {"entities", Json::array()}, {"claims", Json::array()},
                 {"sources", Json::array()}, {"selection", data["selection"]}, {"method_data_snapshot", data},
                 {"prepared_request", prepared}};
    LOOM_TRY_ASSIGN(auto identity, entity(data, "method", Json{{"method_key", metadata["method_key"]}}, date,
                                        metadata["method_key"].get<std::string>()));
    identity["label"] = metadata.value("label", identity["label"]);
    LOOM_TRY(append(profile, "entities", identity));
    const auto prompt_messages = options.value("prompt_messages_snapshot", contract.definition["messages"]);
    const auto prompt_text = json::canonical(prompt_messages);
    LOOM_TRY_ASSIGN(auto prompt, entity(data, "prompt_version", Json{{"text", prompt_text}, {"text_sha256", Sha256::hex(prompt_text)},
        {"encoding", "utf-8"}, {"format", "canonical_effective_messages_template_json"}, {"content_scope", "roles_literal_parts_and_binding_positions"}}, date));
    LOOM_TRY(append(profile, "entities", prompt));
    LOOM_TRY_ASSIGN(auto prompt_source, source(prompt_text, Json(nullptr), "prompt_version"));
    LOOM_TRY(append(profile, "sources", prompt_source));
    const Json parameter_definition{{"effective_parameters", parameters}, {"user_overrides", overrides}};
    LOOM_TRY_ASSIGN(auto parameter_set, version(profile, data, "parameter_set_version", parameter_definition, date));
    Json recipe_definition{{"analysis_prompt", contract.definition}, {"analysis_prompt_sha256", contract.hash},
        {"prompt_sha256", prompt["attrs"]["text_sha256"]}, {"parameters", parameters}, {"user_overrides", overrides},
        {"model", prepared["body_json"].value("model", Json(nullptr))}, {"transport", prepared["transport"]},
        {"output_schema", contract.definition["output_schema"]}, {"output_schema_sha256", prepared["output_schema_hash"]},
        {"validation_mode", contract.definition["validation_mode"]},
        {"request_template", Json{{"messages", prompt_messages}, {"request_parameters", contract.definition["request_parameters"]}}}};
    LOOM_TRY_ASSIGN(auto recipe, version(profile, data, "recipe_version", recipe_definition, date));
    Json preset = nullptr;
    if (options.contains("preset_snapshot")) {
      LOOM_TRY_ASSIGN(auto original, from_snapshot(options["preset_snapshot"]));
      LOOM_TRY_ASSIGN(preset, version(profile, data, "preset_version", prompt_preset(original), date));
    } else {
      auto original = resolve(id);
      if (original) { LOOM_TRY_ASSIGN(preset, version(profile, data, "preset_version", prompt_preset(*original), date)); }
      else if (original.error().code != Errc::NotFound) return original.error();
    }
    Json definition{{"method_key", metadata["method_key"]}, {"execution_capability", metadata["execution_capability"]},
        {"metadata", metadata}, {"parameters", parameters}, {"user_overrides", overrides},
        {"recipe_sha256", recipe["attrs"]["definition_sha256"]},
        {"parameter_set_sha256", parameter_set["attrs"]["definition_sha256"]}};
    if (!preset.is_null()) definition["preset_sha256"] = preset["attrs"]["definition_sha256"];
    LOOM_TRY_ASSIGN(auto method, version(profile, data, "method_version", definition, date));
    Json bindings{{"method_identity_id", identity["id"]}, {"method_version_id", method["id"]},
        {"prompt_version_id", prompt["id"]}, {"recipe_version_id", recipe["id"]},
        {"parameter_set_version_id", parameter_set["id"]}, {"compiler_transform_id", nullptr}};
    Json hashes{{"method_version", method["attrs"]["definition_sha256"]}, {"prompt_bytes", prompt["attrs"]["text_sha256"]},
        {"recipe", recipe["attrs"]["definition_sha256"]}, {"parameter_set", parameter_set["attrs"]["definition_sha256"]}};
    if (!preset.is_null()) { bindings["preset_version_id"] = preset["id"]; hashes["preset"] = preset["attrs"]["definition_sha256"]; }
    const auto actual_model = prepared["body_json"].value("model", Json(nullptr));
    if (actual_model.is_string()) {
      LOOM_TRY_ASSIGN(auto descriptor, entity(data, "model_identity", Json{{"model", actual_model}, {"identity_basis", "prepared_request_model"}}, date));
      LOOM_TRY(append(profile, "entities", descriptor)); bindings["model_identity_id"] = descriptor["id"];
    }
    profile["bindings"] = bindings; profile["definition_hashes"] = hashes;
    profile["definition_records"] = records(profile, bindings);
    LOOM_TRY_ASSIGN(auto captured, source(json::canonical(profile["definition_records"]), Json(nullptr), "method-definition-records.json"));
    LOOM_TRY(append(profile, "sources", captured));
    LOOM_TRY(add_relation(profile, data, method["id"], "version_of", identity["id"], captured));
    LOOM_TRY(add_relation(profile, data, method["id"], "uses_recipe", recipe["id"], captured));
    LOOM_TRY(add_relation(profile, data, recipe["id"], "uses_prompt", prompt["id"], captured));
    LOOM_TRY(add_relation(profile, data, method["id"], "uses_parameter_set", parameter_set["id"], captured));
    if (!preset.is_null()) LOOM_TRY(add_relation(profile, data, method["id"], "uses_preset", preset["id"], captured));
    profile["selection"]["members"] = Json::array({Json{{"method_version_id", method["id"]}}});
    profile["selection"]["user_overrides"] = overrides;
    LOOM_TRY(profile_rows(profile));
    return profile;
  } catch (const std::exception& e) { return invalid(e.what()); }
}

Result<Json> recipe_profile(std::string_view selected_id, const Json& patch, const Json& options) {
  try {
    if (!patch.is_object()) return invalid("recipe patch must be an object");
    LOOM_TRY_ASSIGN(auto data, effective_data(options));
    const std::string id(selected_id);
    if (!data["methods"].contains(id) || !data.contains("recipe_records") || !data["recipe_records"].contains(id))
      return Error(Errc::Unavailable, "recipe method data is not registered: " + id);
    const auto& metadata = data["methods"][id];
    const auto& record = data["recipe_records"][id];
    if (!record.is_object() || !record.contains("definition") || !record["definition"].is_object())
      return invalid("recipe record requires an exact definition object");
    const auto original = record["definition"];
    const auto effective = overlay(original, patch);
    if (effective.value("id", Json()) != id) return invalid("recipe override changed its selected identity");
    LOOM_TRY(finite(effective));
    const auto date = options.value("known_at", Json(nullptr)); LOOM_TRY(known_at(date));
    const auto parameters = options.value("declared_parameters", Json(nullptr)); LOOM_TRY(finite(parameters));
    Json profile{{"vocabulary", data["vocabulary"]}, {"entities", Json::array()}, {"claims", Json::array()},
        {"sources", Json::array()}, {"selection", data["selection"]}, {"method_data_snapshot", data},
        {"execution_available", false}, {"execution_status", "recipe_registration_only"},
        {"canonical_store_written", false}, {"acceptance_establishes_content_truth", false}};
    LOOM_TRY_ASSIGN(auto identity, entity(data, "method", Json{{"method_key", metadata["method_key"]}}, date,
                                         metadata["method_key"].get<std::string>()));
    identity["label"] = metadata.value("label", identity["label"]);
    LOOM_TRY(append(profile, "entities", identity));
    Json parameter_definition{{"effective_parameters", parameters}, {"user_overrides", patch}, {"parameter_status", "declared_unexecuted"}};
    LOOM_TRY_ASSIGN(auto parameter_set, version(profile, data, "parameter_set_version", parameter_definition, date));
    Json recipe_definition{{"recipe_data", effective}, {"recipe_data_sha256", hash(effective)},
        {"parameters", parameters}, {"user_overrides", patch}, {"execution_status", "recipe_registration_only"}};
    LOOM_TRY_ASSIGN(auto recipe, version(profile, data, "recipe_version", recipe_definition, date));
    LOOM_TRY_ASSIGN(auto preset, version(profile, data, "preset_version", Json{{"recipe_data", original},
        {"recipe_data_sha256", hash(original)}, {"parameters", parameters}, {"parameter_status", "declared_unexecuted"}}, date));
    Json definition{{"method_key", metadata["method_key"]}, {"execution_capability", metadata["execution_capability"]},
        {"metadata", metadata}, {"parameters", parameters}, {"user_overrides", patch},
        {"recipe_sha256", recipe["attrs"]["definition_sha256"]}, {"preset_sha256", preset["attrs"]["definition_sha256"]},
        {"parameter_set_sha256", parameter_set["attrs"]["definition_sha256"]}, {"execution_status", "recipe_registration_only"}};
    LOOM_TRY_ASSIGN(auto method, version(profile, data, "method_version", definition, date));
    Json bindings{{"method_identity_id", identity["id"]}, {"method_version_id", method["id"]},
        {"recipe_version_id", recipe["id"]}, {"parameter_set_version_id", parameter_set["id"]},
        {"preset_version_id", preset["id"]}, {"prompt_version_id", nullptr}, {"model_identity_id", nullptr}, {"compiler_transform_id", nullptr}};
    profile["bindings"] = bindings;
    profile["definition_hashes"] = Json{{"method_version", method["attrs"]["definition_sha256"]},
        {"recipe", recipe["attrs"]["definition_sha256"]}, {"parameter_set", parameter_set["attrs"]["definition_sha256"]},
        {"preset", preset["attrs"]["definition_sha256"]}};
    profile["definition_records"] = records(profile, bindings);
    LOOM_TRY_ASSIGN(auto captured, source(json::canonical(profile["definition_records"]), Json(nullptr), "method-definition-records.json"));
    LOOM_TRY(append(profile, "sources", captured));
    LOOM_TRY(add_relation(profile, data, method["id"], "version_of", identity["id"], captured));
    LOOM_TRY(add_relation(profile, data, method["id"], "uses_recipe", recipe["id"], captured));
    LOOM_TRY(add_relation(profile, data, method["id"], "uses_parameter_set", parameter_set["id"], captured));
    LOOM_TRY(add_relation(profile, data, method["id"], "uses_preset", preset["id"], captured));
    profile["selection"]["members"] = Json::array({Json{{"method_version_id", method["id"]}}});
    profile["selection"]["user_overrides"] = patch;
    if (record.contains("source_hash")) profile["recipe_source_hash"] = record["source_hash"];
    LOOM_TRY(profile_rows(profile));
    return profile;
  } catch (const std::exception& e) { return invalid(e.what()); }
}

Result<Json> prepare_graph(const Json& original, const Json& context) {
  try {
    LOOM_TRY(profile_rows(original));
    if (!context.is_object() || !context.contains("known_at") || !context.contains("input_sha256"))
      return invalid("method run requires known_at and explicit input identity");
    LOOM_TRY_ASSIGN(auto run_id, required_text(context, "run_id"));
    LOOM_TRY(known_at(context["known_at"]));
    if (!context["input_sha256"].is_null() && !digest(context["input_sha256"])) return invalid("method input identity must be SHA256 or null");
    const auto data = original["method_data_snapshot"]; LOOM_TRY(validate_data(data));
    Json profile = original;
    Json bindings = profile["bindings"]; bindings["run_id"] = run_id;
    if (bindings["method_identity_id"] == run_id || bindings["method_version_id"] == run_id) return invalid("method, version and run identities must differ");
    const auto& parameter_attrs = profile["definition_records"]["parameter_set_version_id"];
    const auto& recipe = profile["definition_records"]["recipe_version_id"]["definition"];
    const auto& prepared = profile["prepared_request"];
    Json trace{{"schema", "loom.method_run_trace/1"}, {"run_id", run_id},
        {"method_identity_id", bindings["method_identity_id"]}, {"method_version_id", bindings["method_version_id"]},
        {"effective_parameters", parameter_attrs["definition"]["effective_parameters"]},
        {"user_overrides", parameter_attrs["definition"]["user_overrides"]}, {"input_sha256", context["input_sha256"]},
        {"parameter_set_version_id", bindings["parameter_set_version_id"]}, {"parameter_set_sha256", profile["definition_hashes"]["parameter_set"]},
        {"recipe_sha256", profile["definition_hashes"]["recipe"]}, {"prompt_sha256", profile["definition_hashes"]["prompt_bytes"]},
        {"model", recipe["model"]}, {"requested_model", recipe["model"]}, {"compiler_transform_id", nullptr},
        {"prepared_at", context["known_at"]}, {"projection_status", "prepared"},
        {"measurements", context.value("measurements", Json::object())},
        {"analysis_prompt_sha256", prepared["contract_hash"]}, {"analysis_request_envelope_sha256", prepared["request_hash"]},
        {"request_sha256", hash(prepared["body_json"])}, {"request_bytes_sha256", Sha256::hex(prepared["body_bytes"].get<std::string>())},
        {"request_hash_scope", "actual_prepared_payload_before_dispatch"}};
    LOOM_TRY(measurements(trace["measurements"]));
    for (const char* name : {"model_identity_id", "preset_version_id"}) if (bindings.contains(name)) trace[name] = bindings[name];
    if (profile["definition_hashes"].contains("preset")) trace["preset_sha256"] = profile["definition_hashes"]["preset"];
    for (const char* name : {"measurement_scope", "knowledge_run_id", "execution_kind"}) if (context.contains(name)) trace[name] = context[name];
    if (!trace.contains("execution_kind")) trace["execution_kind"] = profile["definition_records"]["method_version_id"]["definition"]["execution_capability"];
    LOOM_TRY_ASSIGN(auto request_capture, source(prepared["body_bytes"].get<std::string>(), context["known_at"], "method-request.json", run_id));
    LOOM_TRY(append(profile, "sources", request_capture)); trace["request_source_ref"] = request_capture["observation"]["id"];
    Json manifest{{"schema", "loom.method_graph/1"}, {"vocabulary", profile["vocabulary"]}, {"bindings", bindings},
        {"definition_hashes", profile["definition_hashes"]}, {"definition_records", profile["definition_records"]}, {"trace", trace}};
    LOOM_TRY_ASSIGN(auto definition_capture, source(json::canonical(manifest), context["known_at"], "method-manifest.json", run_id));
    LOOM_TRY(append(profile, "sources", definition_capture));
    LOOM_TRY_ASSIGN(auto trace_capture, source(json::canonical(trace), context["known_at"], "prepared-method-run-trace.json", run_id));
    LOOM_TRY(append(profile, "sources", trace_capture));
    LOOM_TRY_ASSIGN(auto run, entity(data, "run", trace, context["known_at"], run_id, run_id));
    LOOM_TRY(append(profile, "entities", run));
    LOOM_TRY(add_relation(profile, data, run_id, "requests_method_version", bindings["method_version_id"], definition_capture));
    LOOM_TRY(add_relation(profile, data, run_id, "uses_parameter_set", bindings["parameter_set_version_id"], definition_capture));
    Json graph{{"schema", "loom.analysis_method_graph_proposal/1"}, {"profile", profile}, {"manifest", manifest}, {"trace", trace},
        {"result_entity_ids", Json::array()}, {"definition_capture_source_id", definition_capture["observation"]["id"]},
        {"trace_capture_source_id", trace_capture["observation"]["id"]}, {"canonical_store_written", false},
        {"acceptance_establishes_content_truth", false}};
    LOOM_TRY(validate_graph(graph));
    return graph;
  } catch (const std::exception& e) { return invalid(e.what()); }
}

Status validate_graph(const Json& graph) {
  try {
    if (!graph.is_object() || graph.value("schema", Json()) != "loom.analysis_method_graph_proposal/1" ||
        graph.value("canonical_store_written", Json()) != false || graph.value("acceptance_establishes_content_truth", Json()) != false)
      return invalid("method graph proposal schema/persistence scope");
    const auto& profile = graph.at("profile");
    LOOM_TRY(profile_rows(profile)); LOOM_TRY(finite(graph));
    const auto& data = profile.at("method_data_snapshot"); LOOM_TRY(validate_data(data));
    if (!same(data["vocabulary"], profile.at("vocabulary"))) return conflict("method vocabulary snapshot drift");
    const auto& manifest = graph.at("manifest"); const auto& trace = graph.at("trace");
    if (manifest.value("schema", Json()) != "loom.method_graph/1" || trace.value("schema", Json()) != "loom.method_run_trace/1" ||
        !same(manifest.at("vocabulary"), profile["vocabulary"])) return invalid("method manifest/trace schema");
    const auto& bindings = manifest.at("bindings"); const auto& hashes = manifest.at("definition_hashes");
    Json entities = Json::object(), captures = Json::object(), claims = Json::object();
    for (const auto& row : profile["entities"]) {
      const auto id = row["id"].get<std::string>();
      if (entities.contains(id)) return invalid("duplicate native Entity ID");
      entities[id] = row;
      const auto& attrs = row["attrs"];
      if (attrs.contains("definition_sha256") && (!attrs.contains("definition") || !digest(attrs["definition_sha256"]) ||
          attrs["definition_sha256"] != hash(attrs["definition"]))) return conflict("immutable definition hash drift");
      if (attrs.contains("text_sha256") && (!attrs.contains("text") || !attrs["text"].is_string() ||
          attrs["text_sha256"] != Sha256::hex(attrs["text"].get<std::string>()))) return conflict("immutable prompt byte hash drift");
    }
    for (const auto& row : profile["sources"]) {
      const auto id = row["observation"]["id"].get<std::string>();
      if (captures.contains(id)) return invalid("duplicate native Observation ID");
      captures[id] = row;
    }
    for (const auto& row : profile["claims"]) {
      const auto id = row["id"].get<std::string>();
      if (claims.contains(id)) return invalid("duplicate native Claim ID");
      claims[id] = row;
    }
    for (const auto& c : profile["claims"]) {
      if (!entities.contains(c["subject"].get<std::string>()) ||
          (!c["object"].get_ref<const std::string&>().empty() && !entities.contains(c["object"].get<std::string>())))
        return conflict("native Claim endpoint missing");
      for (const auto& premise : c["assessment"]["premises"]["claims"])
        if (!claims.contains(premise.get<std::string>())) return conflict("native Claim premise missing");
      for (const auto& support : c["assessment"]["basis"]["support"]) {
        const auto id = support["observation"].get<std::string>();
        if (!captures.contains(id)) return conflict("native supporting Observation missing");
        const auto& o = captures[id]["observation"];
        if (!same(support["locator"], o["locator"]) || !support["quote"].is_string() ||
            o["text"].get_ref<const std::string&>().find(support["quote"].get<std::string>()) == std::string::npos)
          return conflict("native support quote/locator drift");
      }
    }
    for (const char* id : {"method_identity_id", "method_version_id", "run_id"}) {
      LOOM_TRY(required_text(bindings, id));
      if (!entities.contains(bindings[id].get<std::string>()) || trace.value(id, Json()) != bindings[id]) return conflict("method/run binding drift");
    }
    if (bindings["method_identity_id"] == bindings["method_version_id"] || bindings["method_identity_id"] == bindings["run_id"] ||
        bindings["method_version_id"] == bindings["run_id"]) return invalid("method/version/run identities are not distinct");
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
      if (!it.value().is_null() && (!it.value().is_string() || !entities.contains(it.value().get<std::string>())))
        return conflict("unknown native definition binding");
    }
    if (!same(records(profile, bindings), manifest.at("definition_records"))) return conflict("captured definition records drift");
    for (const auto& group : {std::pair<const char*, const char*>{"method_version", "method_version_id"},
         {"parameter_set", "parameter_set_version_id"}, {"recipe", "recipe_version_id"}, {"preset", "preset_version_id"}}) {
      if (!bindings.contains(group.second) || bindings[group.second].is_null()) continue;
      if (hashes.value(group.first, Json()) != entities[bindings[group.second].get<std::string>()]["attrs"]["definition_sha256"])
        return conflict("manifest definition hash drift");
    }
    auto edge = [&](const Json& a, const char* relation_role, const Json& b) -> Status {
      LOOM_TRY_ASSIGN(auto predicate, role(profile["vocabulary"], "predicates", relation_role));
      for (const auto& c : profile["claims"]) if (c["subject"] == a && c["predicate"] == predicate && c["object"] == b && c["value"].is_null()) {
        if (c["assessment"]["origin"] != "system" || c["assessment"]["evidence_class"] != "derived" ||
            c["qualifiers"]["extra"].value("confidence_scope", Json()) != "structure_only" ||
            c["qualifiers"]["extra"].value("acceptance_establishes_content_truth", Json()) != false)
          return conflict("structural provenance promoted content authority");
        return {};
      }
      return conflict(std::string("native provenance edge missing: ") + relation_role);
    };
    LOOM_TRY(edge(bindings["method_version_id"], "version_of", bindings["method_identity_id"]));
    LOOM_TRY(edge(bindings["run_id"], "requests_method_version", bindings["method_version_id"]));
    LOOM_TRY(edge(bindings["method_version_id"], "uses_parameter_set", bindings.at("parameter_set_version_id")));
    LOOM_TRY(edge(bindings["run_id"], "uses_parameter_set", bindings["parameter_set_version_id"]));
    const auto& parameter = entities[bindings["parameter_set_version_id"].get<std::string>()]["attrs"]["definition"];
    const auto& method = entities[bindings["method_version_id"].get<std::string>()]["attrs"]["definition"];
    if (!same(parameter["effective_parameters"], trace.at("effective_parameters")) ||
        !same(parameter["user_overrides"], trace.at("user_overrides")) || method["parameter_set_sha256"] != hashes["parameter_set"] ||
        trace.at("parameter_set_sha256") != hashes["parameter_set"] || trace.at("parameter_set_version_id") != bindings["parameter_set_version_id"])
      return conflict("effective parameter snapshot drift");
    if (!trace.contains("input_sha256") || (!trace["input_sha256"].is_null() && !digest(trace["input_sha256"]))) return invalid("run input identity missing/invalid");
    LOOM_TRY(measurements(trace.at("measurements")));
    if (bindings.contains("recipe_version_id") && !bindings["recipe_version_id"].is_null()) {
      const auto& recipe = entities[bindings["recipe_version_id"].get<std::string>()]["attrs"]["definition"];
      if (!same(recipe["parameters"], trace["effective_parameters"]) || method["recipe_sha256"] != hashes["recipe"] ||
          trace.at("recipe_sha256") != hashes["recipe"]) return conflict("effective recipe parameters/hash drift");
      LOOM_TRY(edge(bindings["method_version_id"], "uses_recipe", bindings["recipe_version_id"]));
      const auto& prompt = entities[bindings.at("prompt_version_id").get<std::string>()]["attrs"];
      if (prompt["text_sha256"] != hashes.at("prompt_bytes") || prompt["text_sha256"] != trace.at("prompt_sha256") ||
          prompt["text_sha256"] != recipe["prompt_sha256"]) return conflict("prompt/recipe binding drift");
      LOOM_TRY(edge(bindings["recipe_version_id"], "uses_prompt", bindings["prompt_version_id"]));
      LOOM_TRY_ASSIGN(auto contract, from_snapshot(recipe.at("analysis_prompt")));
      const auto& prepared = profile.at("prepared_request");
      if (recipe.at("analysis_prompt_sha256") != contract.hash || prepared.at("contract_hash") != contract.hash ||
          prepared.at("output_schema_hash") != hash(contract.definition["output_schema"]) ||
          !same(recipe["output_schema"], contract.definition["output_schema"]) ||
          recipe["output_schema_sha256"] != prepared["output_schema_hash"] || recipe["validation_mode"] != contract.definition["validation_mode"])
        return conflict("retained prompt contract/schema identity drift");
      if (!prepared.at("body_bytes").is_string()) return invalid("retained request bytes must be a string");
      LOOM_TRY_ASSIGN(auto body, json::parse(prepared["body_bytes"].get<std::string>()));
      const auto envelope_hash = hash(Json{{"method", prepared.at("method")}, {"url", prepared.at("url")},
          {"headers", prepared.at("headers")}, {"body_bytes", prepared["body_bytes"]}, {"transport", prepared.at("transport")},
          {"validation_mode", contract.definition["validation_mode"]}, {"output_schema_hash", prepared["output_schema_hash"]}});
      if (!same(body, prepared.at("body_json")) || prepared.at("request_hash") != envelope_hash ||
          trace.at("request_sha256") != hash(body) || trace.at("request_bytes_sha256") != Sha256::hex(prepared["body_bytes"].get<std::string>()) ||
          trace.at("analysis_request_envelope_sha256") != envelope_hash || trace.at("analysis_prompt_sha256") != contract.hash)
        return conflict("retained request envelope/body identity drift");
      LOOM_TRY_ASSIGN(auto request_source, required_text(trace, "request_source_ref"));
      if (!captures.contains(request_source) || captures[request_source]["observation"]["text"] != prepared["body_bytes"])
        return conflict("retained exact request capture drift");
      if (prompt["text"] != json::canonical(recipe["request_template"]["messages"]) ||
          !same(recipe["transport"], prepared["transport"])) return conflict("effective prompt template/transport drift");
    }
    if (bindings.contains("preset_version_id") && !bindings["preset_version_id"].is_null()) {
      if (trace.at("preset_sha256") != hashes["preset"] || method["preset_sha256"] != hashes["preset"]) return conflict("preset binding drift");
      LOOM_TRY(edge(bindings["method_version_id"], "uses_preset", bindings["preset_version_id"]));
    }
    const auto& prepared_trace = manifest.at("trace");
    const std::set<std::string> phase_fields{"measurements", "measurement_scope", "projection_status", "projected_at", "response_provenance"};
    for (auto it = prepared_trace.begin(); it != prepared_trace.end(); ++it)
      if (!phase_fields.contains(it.key()) && (!trace.contains(it.key()) || !same(trace[it.key()], it.value()))) return conflict("prepared run identity/settings changed");
    if (!same(entities[bindings["run_id"].get<std::string>()]["attrs"], trace)) return conflict("run attrs differ from final trace");
    for (const auto& capture : {std::pair<const char*, Json>{"definition_capture_source_id", manifest}, {"trace_capture_source_id", trace}}) {
      LOOM_TRY_ASSIGN(auto id, required_text(graph, capture.first));
      if (!captures.contains(id)) return conflict("method graph capture missing");
      LOOM_TRY_ASSIGN(auto decoded, json::parse(captures[id]["observation"]["text"].get<std::string>()));
      if (!same(decoded, capture.second)) return conflict("method graph immutable capture drift");
    }
    if (!graph.at("result_entity_ids").is_array()) return invalid("result_entity_ids must be an array");
    std::set<std::string> ids, result_bindings;
    for (const auto& id : graph["result_entity_ids"]) {
      if (!id.is_string() || !entities.contains(id.get<std::string>()) || !ids.insert(id.get<std::string>()).second) return invalid("native result identities missing/duplicated");
    }
    if (!ids.empty() && !trace.contains("result_bindings")) return invalid("native result bindings missing");
    if (trace.contains("result_bindings")) for (const auto& result : trace["result_bindings"]) {
      LOOM_TRY_ASSIGN(auto id, required_text(result, "result_entity_id"));
      if (!entities.contains(id) || !result_bindings.insert(id).second || result["run_id"] != bindings["run_id"] ||
          result["method_version_id"] != bindings["method_version_id"] || !result.at("compiler_transform_id").is_null()) return conflict("native result binding drift");
      const auto& attrs = entities[id]["attrs"];
      if (attrs.value("content_verification", Json()) != "unverified" || attrs.value("acceptance_establishes_content_truth", Json()) != false ||
          entities[id]["origin"] != "model_knowledge" || !same(attrs.at("model_origin"), result.at("model_origin")))
        return conflict("model proposal provenance promoted/changed");
      const auto& origin = result["model_origin"];
      if (origin.at("kind") != "model" || origin.at("recipe_sha256") != trace["recipe_sha256"] ||
          origin.at("response_sha256") != trace.at("response_sha256") || origin.at("model") != trace["model"])
        return conflict("model proposal response/recipe identity drift");
      LOOM_TRY(edge(id, "produced_in_run", bindings["run_id"]));
      LOOM_TRY(edge(id, "produced_by_method_version", bindings["method_version_id"]));
    }
    if (ids != result_bindings) return conflict("declared result set differs from actual bindings");
    if (trace.contains("raw_model_content_source_ref")) {
      const auto id = trace["raw_model_content_source_ref"].get<std::string>();
      if (!captures.contains(id) || captures[id]["text_sha256"] != trace.at("response_sha256")) return conflict("model content capture/hash drift");
    }
    return {};
  } catch (const std::exception& e) { return invalid(e.what()); }
}

Result<Json> bind_candidates(const Json& original, const Json& candidates, const Json& provenance) {
  try {
    if (!original.is_object() || original.value("schema", Json()) != "loom.analysis_method_graph_proposal/1" ||
        !candidates.is_array() || !provenance.is_object() || !provenance.contains("raw_model_content") ||
        !provenance["raw_model_content"].is_string() || !provenance.contains("known_at")) return invalid("candidate graph/provenance shape");
    LOOM_TRY(known_at(provenance["known_at"])); LOOM_TRY(validate_graph(original));
    Json graph = original;
    auto& profile = graph["profile"]; LOOM_TRY(profile_rows(profile));
    const auto data = profile["method_data_snapshot"]; LOOM_TRY(validate_data(data));
    auto& trace = graph["trace"];
    const auto& bindings = graph["manifest"]["bindings"];
    const auto bytes = provenance["raw_model_content"].get<std::string>();
    const auto response_hash = Sha256::hex(bytes);
    LOOM_TRY_ASSIGN(auto response_capture, source(bytes, provenance["known_at"], "model-content.txt", graph["manifest"]["bindings"]["run_id"].get<std::string>()));
    response_capture["observation"]["attrs"]["content_verification"] = "unverified";
    LOOM_TRY(append(profile, "sources", response_capture));
    const Json model_origin{{"kind", "model"}, {"actor", provenance.value("actor", Json("loom.extract.semantic"))},
        {"model", trace.value("model", Json(nullptr))}, {"recipe_sha256", trace["recipe_sha256"]}, {"response_sha256", response_hash},
        {"model_identity_scope", "requested_model"}};
    trace["response_sha256"] = response_hash;
    trace["response_hash_scope"] = "native_analysis_decoder_input";
    trace["raw_model_content_source_ref"] = response_capture["observation"]["id"];
    trace["response_provenance"] = provenance.value("response_provenance", Json(nullptr));
    trace["from_cache"] = provenance.value("from_cache", Json(false));
    if (!trace["from_cache"].is_boolean()) return invalid("from_cache must be a boolean");
    trace["invocation_kind"] = trace["from_cache"].get<bool>() ? "cache_replay" : "model_response";
    trace["original_producer_run_id"] = provenance.value("original_producer_run_id", Json(nullptr));
    if (provenance.contains("model")) trace["observed_model"] = provenance["model"];
    trace["projected_at"] = provenance["known_at"];
    trace["projection_status"] = "candidate_projection";
    trace["result_bindings"] = Json::array();
    if (provenance.contains("measurements")) trace["measurements"] = provenance["measurements"];
    LOOM_TRY(measurements(trace["measurements"]));
    for (const char* name : {"measurement_scope", "instrumentation"}) if (provenance.contains(name)) trace[name] = provenance[name];
    Json result_ids = Json::array();
    std::set<std::string> queue_ids;
    for (const auto& candidate : candidates) {
      LOOM_TRY_ASSIGN(auto queue_id, required_text(candidate, "id"));
      if (!candidate.contains("payload") || !queue_ids.insert(queue_id).second) return invalid("candidate payload missing or duplicate queue ID");
      Json attrs{{"candidate_id", queue_id}, {"proposal", candidate["payload"]}, {"model_origin", model_origin},
          {"content_verification", "unverified"}, {"acceptance_establishes_content_truth", false},
          {"projection_operator", "loom.analysis_method_projection/1"}};
      LOOM_TRY_ASSIGN(auto result, entity(data, "result", attrs, provenance["known_at"], queue_id));
      result["origin"] = "model_knowledge";
      result["confidence"] = data["result_assessment"]["confidence"];
      LOOM_TRY(append(profile, "entities", result));
      result_ids.push_back(result["id"]);
      trace["result_bindings"].push_back(Json{{"result_entity_id", result["id"]}, {"run_id", bindings["run_id"]},
          {"method_version_id", bindings["method_version_id"]}, {"compiler_transform_id", nullptr}, {"model_origin", model_origin}});
    }
    LOOM_TRY_ASSIGN(auto trace_capture, source(json::canonical(trace), provenance["known_at"], "method-run-trace.json", bindings["run_id"].get<std::string>()));
    LOOM_TRY(append(profile, "sources", trace_capture));
    for (const auto& id : result_ids) {
      LOOM_TRY(add_relation(profile, data, id, "produced_in_run", bindings["run_id"], trace_capture));
      LOOM_TRY(add_relation(profile, data, id, "produced_by_method_version", bindings["method_version_id"], trace_capture));
    }
    bool found_run = false;
    for (auto& e : profile["entities"]) if (e["id"] == bindings["run_id"]) { e["attrs"] = trace; found_run = true; }
    if (!found_run) return invalid("method run entity missing");
    graph["result_entity_ids"] = result_ids;
    graph["trace_capture_source_id"] = trace_capture["observation"]["id"];
    LOOM_TRY(validate_graph(graph));
    return graph;
  } catch (const std::exception& e) { return invalid(e.what()); }
}

}  // namespace loom::extract::prompts::method_graph
