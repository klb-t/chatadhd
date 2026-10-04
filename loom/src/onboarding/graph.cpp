#include "loom/onboarding_store.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "loom/kb.h"
#include "loom/model.h"
#include "loom/onboarding_layers.h"
#include "loom/util/sha256.h"

namespace loom::onboarding {
namespace {
using model::Claim;
using model::Entity;
using model::Observation;
using model::Origin;
using model::EvidenceClass;

Error invalid(std::string message) {
  return Error(Errc::InvalidArgument, "onboarding graph: " + std::move(message));
}
Error conflict(std::string message) {
  return Error(Errc::Conflict, "onboarding graph: " + std::move(message));
}
std::string hash(const Json& value) { return Sha256::hex(json::canonical(value)); }
std::string key(std::initializer_list<std::string_view> parts) {
  std::string result;
  for (auto part : parts) { if (!result.empty()) result += '\x1f'; result += part; }
  return result;
}

class Projection {
 public:
  explicit Projection(const Json& vocabulary, std::string_view user)
      : vocabulary_(vocabulary), user_(user) {}

  Status validate_vocabulary() const {
    if (!vocabulary_.is_object()) return invalid("vocabulary must be an object");
    for (const char* group : {"kinds", "predicates"}) {
      const auto* entries = json::find(vocabulary_, group);
      if (!entries || !entries->is_object()) return invalid(std::string("missing vocabulary.") + group);
      std::set<std::string> seen;
      for (const auto& entry : *entries) {
        if (!entry.is_string() || entry.get_ref<const std::string&>().empty()) return invalid("vocabulary values must be nonempty strings");
        if (!seen.insert(entry.get<std::string>()).second) return invalid("ambiguous vocabulary role values");
      }
    }
    return {};
  }
  Result<std::string> role(std::string_view group, std::string_view name) const {
    const auto* entries = json::find(vocabulary_, group);
    const auto* entry = entries ? json::find(*entries, name) : nullptr;
    if (!entry || !entry->is_string() || entry->get_ref<const std::string&>().empty())
      return Error(Errc::Unavailable, "onboarding graph: missing vocabulary " + std::string(group) + "." + std::string(name));
    return entry->get<std::string>();
  }

  Observation capture(std::string_view member, std::string_view bytes, std::string_view date = "",
                      std::string_view provenance = "", bool method = false) {
    Observation observation;
    observation.text = bytes;
    observation.kind = model::ObservationKind::Field;
    observation.locator.source = "sha256:" + Sha256::hex(bytes);
    observation.locator.member = member;
    observation.locator.byte_start = 0;
    observation.locator.byte_len = static_cast<std::int64_t>(bytes.size());
    observation.unit = model::Unit::make_id(observation.locator.source, observation.locator);
    observation.id = Observation::make_id(observation.unit, observation.locator, observation.text);
    observation.date = date;
    observation.attrs = Json{{"capture_scope", "record_bytes_only"}, {"provenance", provenance}};
    observations_[observation.id] = observation.to_json();
    if (method) method_sources_[observation.id] = Json{{"observation", observation.to_json()},
        {"known_at", date.empty() ? Json(nullptr) : Json(date)}, {"text_sha256", Sha256::hex(bytes)}};
    return observation;
  }
  Observation capture_json(std::string_view member, const Json& data, std::string_view date = "",
                           std::string_view provenance = "", bool method = false) {
    return capture(member, json::canonical(data), date, provenance, method);
  }

  Result<std::string> entity(std::string_view role_name, std::string_view identity,
                             std::string_view label, Json attrs,
                             Origin origin = Origin::System,
                             EvidenceClass evidence = EvidenceClass::Derived,
                             bool shared = false, bool method = false) {
    LOOM_TRY_ASSIGN(auto kind, role("kinds", role_name));
    Entity entity;
    entity.kind = kind;
    entity.canonical_key = shared ? key({"onboarding", role_name, identity}) : key({"onboarding", user_, role_name, identity});
    entity.id = Entity::make_id(entity.kind, entity.canonical_key);
    entity.label = label;
    entity.origin = origin;
    entity.evidence = evidence;
    entity.attrs = std::move(attrs);
    const Json body = entity.to_json();
    if (entities_.contains(entity.id) && entities_[entity.id] != body) return conflict("different definitions reuse native entity identity");
    entities_[entity.id] = body;
    if (method) method_entities_[entity.id] = body;
    return entity.id;
  }

  Status relation(std::string_view subject, std::string_view predicate_role,
                  std::string_view object, const Observation& source, Json extra = Json::object(), bool method = false) {
    LOOM_TRY_ASSIGN(auto predicate, role("predicates", predicate_role));
    Claim claim;
    claim.subject = subject;
    claim.predicate = predicate;
    claim.object = object;
    claim.assessment.evidence = EvidenceClass::Derived;
    claim.assessment.origin = Origin::System;
    claim.assessment.confidence = 1;
    claim.assessment.derivation = model::Derivation{"onboarding.project_graph", 1, "", 0};
    claim.assessment.support.push_back(model::Support{source.id, source.locator, source.text, "onboarding.project_graph@1", 1});
    extra["confidence_scope"] = "structure_only";
    claim.qualifiers.extra = std::move(extra);
    return put_claim(claim, method);
  }

  Status literal(std::string_view subject, std::string_view predicate_role, const Json& value,
                 const Observation& source, Origin origin, EvidenceClass evidence,
                 Json extra = Json::object()) {
    // Native Claim uses null as 'no value'. An explicit null remains exactly
    // recorded in Entity attrs/source bytes, without inventing a DTO literal.
    if (value.is_null()) return {};
    LOOM_TRY_ASSIGN(auto predicate, role("predicates", predicate_role));
    Claim claim;
    claim.subject = subject; claim.predicate = predicate; claim.value = value;
    claim.assessment.evidence = evidence;
    claim.assessment.origin = origin;
    claim.assessment.confidence = 1;
    claim.assessment.support.push_back(model::Support{source.id, source.locator, source.text, "onboarding.project_graph@1", 1});
    if (evidence == EvidenceClass::Derived) {
      claim.assessment.derivation = model::Derivation{"onboarding.project_graph", 1, "", 0};
      extra["confidence_scope"] = "structure_only";
    }
    claim.qualifiers.extra = std::move(extra);
    return put_claim(claim);
  }
  Status absent(std::string_view subject, std::string_view field_id, bool askable) {
    LOOM_TRY_ASSIGN(auto predicate, role("predicates", "has_value"));
    Claim claim;
    claim.subject = subject; claim.predicate = predicate;
    claim.assessment.evidence = EvidenceClass::Absent;
    claim.assessment.origin = Origin::System;
    claim.assessment.open.slots.push_back(std::string(field_id));
    if (askable) claim.assessment.open.questions.push_back(std::string(field_id));
    return put_claim(claim);
  }
  Status put_claim(Claim& claim, bool method = false) {
    claim.id = Claim::make_id(claim.subject, claim.predicate, claim.object, claim.value, claim.qualifiers);
    LOOM_TRY(claim.validate());
    const Json body = claim.to_json();
    if (claims_.contains(claim.id) && claims_[claim.id] != body) return conflict("different assessments reuse claim identity");
    claims_[claim.id] = body;
    if (method) method_claims_[claim.id] = body;
    return {};
  }
  Status import_method_profile(const Json& profile) {
    if (!profile.is_object() || !profile.contains("entities") || !profile["entities"].is_array() ||
        !profile.contains("claims") || !profile["claims"].is_array() ||
        !profile.contains("sources") || !profile["sources"].is_array() ||
        !profile.contains("vocabulary") || !profile["vocabulary"].is_object())
      return invalid("executed method profile needs vocabulary and complete native definitions");
    // A forward pack revision may add roles. Historical captures retain their
    // original vocabulary; a new mapping never rewrites those source bytes.
    for (const char* group : {"kinds", "predicates"}) {
      if (!profile["vocabulary"].contains(group) || !profile["vocabulary"][group].is_object())
        return invalid("executed method vocabulary is malformed");
      for (const auto& [name, value] : profile["vocabulary"][group].items()) {
        if (!value.is_string() || value.get_ref<const std::string&>().empty()) return invalid("executed method vocabulary role is malformed");
        if (vocabulary_[group].contains(name) && vocabulary_[group][name] != value)
          return conflict("executed method vocabulary role was remapped");
      }
    }
    std::set<std::string> definition_bytes;
    for (const auto& source : profile["sources"]) {
      LOOM_TRY_ASSIGN(auto observation, Observation::from_json(source.at("observation")));
      if (source.value("text_sha256", Json(nullptr)) != Sha256::hex(observation.text) ||
          observation.locator.source != "sha256:" + Sha256::hex(observation.text))
        return conflict("executed definition capture hash mismatch");
      definition_bytes.insert(observation.text);
      const auto body = observation.to_json();
      if (observations_.contains(observation.id) && observations_[observation.id] != body)
        return conflict("executed definition capture identity reused for different bytes");
      observations_[observation.id] = body;
    }
    for (const auto& row : profile["entities"]) {
      LOOM_TRY_ASSIGN(auto entity, Entity::from_json(row));
      const auto& attrs = entity.attrs;
      if (attrs.contains("definition_sha256") &&
          (attrs["definition_sha256"] != hash(attrs.at("definition")) ||
           !definition_bytes.contains(json::canonical(attrs["definition"]))))
        return conflict("executed method definition differs from its immutable hash/source");
      if (attrs.contains("text_sha256") &&
          (attrs["text_sha256"] != Sha256::hex(attrs.at("text").get<std::string>()) ||
           !definition_bytes.contains(attrs["text"].get<std::string>())))
        return conflict("executed prompt differs from its immutable hash/source");
      const auto body = entity.to_json();
      if (entities_.contains(entity.id) && entities_[entity.id] != body)
        return conflict("executed method reuses a different immutable native definition");
      entities_[entity.id] = body;
    }
    for (const auto& row : profile["claims"]) {
      LOOM_TRY_ASSIGN(auto claim, Claim::from_json(row));
      if (!entities_.contains(claim.subject) || (!claim.object.empty() && !entities_.contains(claim.object)))
        return invalid("executed method claim has a dangling native reference");
      for (const auto& support : claim.assessment.support) {
        if (!observations_.contains(support.observation)) return invalid("executed method claim lacks native source");
        const auto& observation = observations_[support.observation];
        if (support.quote != observation["text"].get<std::string>() || support.locator.to_json() != observation["locator"])
          return conflict("executed method support differs from exact native capture");
      }
      LOOM_TRY(put_claim(claim));
    }
    return {};
  }
  Status actual_run(const Json& execution, std::string_view method_version,
                    const Observation& source) {
    LOOM_TRY_ASSIGN(auto kind, role("kinds", "run"));
    const auto id = json::get_string(execution, "run_id");
    const auto response_hash = json::get_string(execution, "response_sha256");
    if (id.empty() || json::get_string(execution, "request_token").empty() || response_hash.size() != 64 ||
        response_hash.find_first_not_of("0123456789abcdef") != std::string::npos || !entities_.contains(std::string(method_version)))
      return invalid("actual execution needs run id, request token, response SHA256 and retained method version");
    Entity run;
    run.id = id; run.kind = kind; run.canonical_key = id; run.label = id;
    run.evidence = EvidenceClass::Observed; run.origin = Origin::System;
    run.attrs = execution; run.attrs.erase("method_profile");
    run.attrs["capture_scope"] = "host_execution_receipt";
    run.attrs["content_truth"] = "not_established";
    const auto body = run.to_json();
    if (entities_.contains(run.id) && entities_[run.id] != body) return conflict("actual execution run identity reused");
    entities_[run.id] = body;
    return relation(run.id, "requests_method_version", method_version, source);
  }
  Json result(const Json& method_selection, const Json& method_bindings) const {
    auto rows = [](const auto& map) { Json result = Json::array(); for (const auto& [id, row] : map) { (void)id; result.push_back(row); } return result; };
    return Json{{"entities", rows(entities_)}, {"claims", rows(claims_)}, {"observations", rows(observations_)},
      {"method_profile", Json{{"vocabulary", vocabulary_}, {"entities", rows(method_entities_)},
       {"claims", rows(method_claims_)}, {"sources", rows(method_sources_)},
       {"selection", method_selection}, {"bindings", method_bindings}}}};
  }

 private:
  Json vocabulary_;
  std::string user_;
  std::map<std::string, Json> entities_, claims_, observations_, method_entities_, method_claims_, method_sources_;
};

Result<Json> project_method(Projection& graph, const Json& method) {
  if (!method.is_object() || json::get_string(method, "id").empty() ||
      !method.contains("revision") || !method["revision"].is_number_integer() ||
      !method.contains("prompt") || !method["prompt"].is_object() ||
      !method["prompt"].contains("text") || !method["prompt"]["text"].is_string() ||
      !method.contains("recipe") || !method["recipe"].is_object() ||
      !method.contains("parameters") || !method["parameters"].is_object() ||
      json::get_string(method, "execution_capability").empty())
    return invalid("graph method needs id, revision, prompt text, recipe, parameters and execution_capability");
  const std::string id = method["id"];
  const auto revision = method["revision"];
  const std::string prompt = method["prompt"]["text"];
  const auto prompt_hash = Sha256::hex(prompt);
  const auto prompt_source = graph.capture("method/prompt", prompt, "", "effective_definition", true);
  LOOM_TRY_ASSIGN(auto prompt_id, graph.entity("prompt_version", key({id, prompt_hash}), id + " prompt",
      Json{{"text", prompt}, {"text_sha256", prompt_hash}}, Origin::System, EvidenceClass::Derived, true, true));

  const Json parameters{{"effective_parameters", method["parameters"]},
      {"user_overrides", method.value("user_overrides", Json::object())}};
  const auto parameter_hash = hash(parameters);
  const auto parameter_source = graph.capture_json("method/parameters", parameters, "", "effective_definition", true);
  LOOM_TRY_ASSIGN(auto parameter_id, graph.entity("parameter_set_version", key({id, parameter_hash}), id + " parameters",
      Json{{"definition", parameters}, {"definition_sha256", parameter_hash}}, Origin::System, EvidenceClass::Derived, true, true));

  Json recipe = method["recipe"];
  if (recipe.contains("prompt_sha256") && recipe["prompt_sha256"] != prompt_hash) return conflict("recipe prompt hash differs from exact prompt");
  if (recipe.contains("parameters") && recipe["parameters"] != method["parameters"]) return conflict("recipe parameters differ from exact parameter set");
  recipe["prompt_sha256"] = prompt_hash;
  recipe["parameters"] = method["parameters"];
  const auto recipe_hash = hash(recipe);
  const auto recipe_source = graph.capture_json("method/recipe", recipe, "", "effective_definition", true);
  LOOM_TRY_ASSIGN(auto recipe_id, graph.entity("recipe_version", key({id, recipe_hash}), id + " recipe",
      Json{{"definition", recipe}, {"definition_sha256", recipe_hash}}, Origin::System, EvidenceClass::Derived, true, true));

  Json definition = method.value("definition", Json::object());
  if (!definition.is_object()) return invalid("method definition must be an object");
  const Json bindings{{"execution_capability", method["execution_capability"]}, {"parameters", method["parameters"]},
      {"user_overrides", parameters["user_overrides"]}, {"prompt_sha256", prompt_hash},
      {"recipe_sha256", recipe_hash}, {"parameter_set_sha256", parameter_hash}, {"revision", revision}};
  for (const auto& [name, value] : bindings.items()) {
    if (definition.contains(name) && definition[name] != value) return conflict("method definition differs from effective " + name);
    definition[name] = value;
  }
  std::string preset_id;
  if (method.contains("preset")) {
    const Json preset = method["preset"];
    if (!preset.is_object()) return invalid("method preset must be an object");
    const auto preset_hash = hash(preset);
    const auto preset_source = graph.capture_json("method/preset", preset, "", "effective_definition", true);
    (void)preset_source;
    LOOM_TRY_ASSIGN(preset_id, graph.entity("preset_version", key({id, preset_hash}), id + " preset",
        Json{{"definition", preset}, {"definition_sha256", preset_hash}}, Origin::System, EvidenceClass::Derived, true, true));
    if (definition.contains("preset_sha256") && definition["preset_sha256"] != preset_hash) return conflict("method preset hash differs from exact preset");
    definition["preset_sha256"] = preset_hash;
  }
  const auto definition_hash = hash(definition);
  const auto definition_source = graph.capture_json("method/definition", definition, "", "effective_definition", true);
  const auto identity_source = graph.capture_json("method/identity", Json{{"id", id}}, "", "effective_definition", true);
  LOOM_TRY_ASSIGN(auto method_id, graph.entity("method", id, id, Json{{"definition_id", id}}, Origin::System, EvidenceClass::Derived, true, true));
  LOOM_TRY_ASSIGN(auto version_id, graph.entity("method_version", key({id, definition_hash}), id + " version",
      Json{{"definition", definition}, {"definition_sha256", definition_hash}, {"revision", revision}}, Origin::System, EvidenceClass::Derived, true, true));
  LOOM_TRY(graph.relation(version_id, "version_of", method_id, identity_source, Json::object(), true));
  LOOM_TRY(graph.relation(version_id, "uses_recipe", recipe_id, definition_source, Json::object(), true));
  LOOM_TRY(graph.relation(version_id, "uses_prompt", prompt_id, prompt_source, Json::object(), true));
  LOOM_TRY(graph.relation(recipe_id, "uses_prompt", prompt_id, recipe_source, Json::object(), true));
  LOOM_TRY(graph.relation(version_id, "uses_parameter_set", parameter_id, parameter_source, Json::object(), true));
  if (!preset_id.empty()) LOOM_TRY(graph.relation(version_id, "uses_preset", preset_id, definition_source, Json::object(), true));
  return Json{{"definition_id", id}, {"revision", revision}, {"execution_capability", method.at("execution_capability")},
      {"parameter_set_sha256", parameter_hash}, {"method_id", method_id}, {"method_version_id", version_id}, {"prompt_version_id", prompt_id},
      {"recipe_version_id", recipe_id}, {"parameter_set_version_id", parameter_id},
      {"definition_sha256", definition_hash}, {"recipe_sha256", recipe_hash}, {"prompt_sha256", prompt_hash}};
}
}  // namespace

Result<Json> project_graph(const Json& pack, const Json& scenario, const Json& profile,
                           const Json& layers, std::string_view user) {
  try {
    if (user.empty() || !pack.is_object() || !scenario.is_object() || !profile.is_object() ||
        !profile.contains("fields") || !profile["fields"].is_object() ||
        !profile.contains("privacy") || !profile["privacy"].is_object() ||
        !profile["privacy"].contains("rules") || !profile["privacy"]["rules"].is_array() ||
        !pack.contains("vocabulary")) return invalid("checked pack/scenario/profile and nonempty user are required");
    LOOM_TRY_ASSIGN(auto defaults, DefaultLayers::create(pack, layers));
    Projection graph(pack["vocabulary"], user);
    LOOM_TRY(graph.validate_vocabulary());
    const auto profile_source = graph.capture_json("profile/identity", Json{{"user", user}}, "", "system_initialisation");
    LOOM_TRY_ASSIGN(auto profile_id, graph.entity("profile", "user", user, Json{{"role", "user_profile"}, {"user", user},
        {"schema", profile.value("schema", Json(nullptr))}}));

    std::map<std::string, std::vector<std::pair<std::string, Observation>>> result_candidates;
    for (const auto& [fid, field] : profile["fields"].items()) {
      if (!field.is_object()) return invalid("profile field must be an object");
      const std::string status = json::get_string(field, "status");
      const std::string provenance = json::get_string(field, "provenance");
      if (status != "known" && status != "unknown" && status != "declined" && status != "never") return invalid("unknown profile field state");
      const bool inferred = provenance == "model_inferred";
      const bool explicit_value = provenance == "user_stated" || provenance == "form";
      const auto origin = inferred ? Origin::ModelKnowledge : explicit_value ? Origin::User : Origin::System;
      const auto evidence = inferred ? EvidenceClass::Inferred : explicit_value ? EvidenceClass::User : EvidenceClass::Derived;
      Json attrs = field;
      attrs["field_id"] = fid;
      attrs["semantic_value_asserted"] = status == "known" && explicit_value;
      if (inferred) attrs["content_verification"] = "unverified";
      const auto source = graph.capture_json("profile/" + fid, attrs, json::get_string(field, "time"), provenance);
      LOOM_TRY_ASSIGN(auto field_id, graph.entity("field", fid, fid, attrs, origin, evidence));
      LOOM_TRY(graph.relation(profile_id, "has_field", field_id, source));
      const auto candidate_id = json::get_string(field, "candidate_id");
      if (!candidate_id.empty() && inferred) result_candidates[candidate_id].emplace_back(field_id, source);
      LOOM_TRY(graph.literal(field_id, "has_state", status, source, Origin::System, EvidenceClass::Derived));
      if (status == "known" && explicit_value) {
        LOOM_TRY(graph.literal(field_id, "has_value", field.value("value", Json(nullptr)), source, origin, evidence,
            Json{{"provenance", provenance}, {"review", field.value("review", Json(nullptr))}}));
      } else if (status != "known") LOOM_TRY(graph.absent(field_id, fid, status == "unknown"));
    }

    if (profile.contains("candidates")) {
      if (!profile["candidates"].is_object()) return invalid("candidates must be an object");
      for (const auto& [id, candidate] : profile["candidates"].items()) {
        // Reviewed old candidate bytes belong only to explicitly retained
        // history. They must not reappear in the current graph projection.
        if (json::get_string(candidate, "review") != "pending") continue;
        const std::string provenance = json::get_string(candidate, "provenance");
        const bool inferred = provenance == "model_inferred";
        Json attrs = candidate; attrs["role"] = "pending_candidate";
        attrs["semantic_value_asserted"] = false;
        if (inferred) attrs["content_verification"] = "unverified";
        const auto source = graph.capture_json("candidate/" + id, attrs, json::get_string(candidate, "time"), provenance);
        LOOM_TRY_ASSIGN(auto candidate_id, graph.entity("field", "candidate/" + id, id, attrs,
            inferred ? Origin::ModelKnowledge : Origin::User, inferred ? EvidenceClass::Inferred : EvidenceClass::User));
        LOOM_TRY(graph.relation(profile_id, "has_field", candidate_id, source, Json{{"role", "pending_candidate"}}));
        if (inferred) result_candidates[id].emplace_back(candidate_id, source);
      }
    }

    LOOM_TRY_ASSIGN(auto privacy_layer, defaults.resolve("onboarding.privacy"));
    for (const auto& rule : profile["privacy"]["rules"]) {
      const auto id = json::get_string(rule, "id");
      if (id.empty()) return invalid("privacy rule needs id");
      const bool preset = json::get_string(rule, "provenance") == "builtin_preset";
      const auto source = graph.capture_json("privacy/" + id, rule, json::get_string(rule, "time"), preset ? "builtin_preset" : "user_stated");
      Json attrs = rule;
      attrs["applicable"] = privacy_layer.at("status") == "effective";
      attrs["layer_status"] = privacy_layer.at("status");
      LOOM_TRY_ASSIGN(auto rule_id, graph.entity("privacy_rule", id, id, attrs,
          preset ? Origin::Repo : Origin::User, preset ? EvidenceClass::Observed : EvidenceClass::User));
      LOOM_TRY(graph.relation(profile_id, "has_rule", rule_id, source));
    }

    LOOM_TRY_ASSIGN(auto builtin_layer, graph.entity("layer", "builtin", "Built-in defaults", Json{{"layer", "builtin"},
        {"pack_id", pack["pack_id"]}, {"pack_revision", pack["revision"]}}));
    LOOM_TRY_ASSIGN(auto user_layer, graph.entity("layer", "user", "User overrides", Json{{"layer", "user"}}));
    std::set<std::string> keys;
    for (const auto& entry : pack["entries"]) keys.insert(entry["key"].get<std::string>());
    for (const auto& [entry_key, value] : defaults.snapshot()["overrides"].items()) { (void)value; keys.insert(entry_key); }
    for (const auto& [id, record] : defaults.snapshot()["catalog"].items()) { (void)id; keys.insert(record["entry"]["key"].get<std::string>()); }
    for (const auto& default_key : keys) {
      LOOM_TRY_ASSIGN(auto resolution, defaults.resolve(default_key));
      Json attrs = resolution;
      const auto layer = json::get_string(resolution, "layer");
      const auto* source_record = json::find(resolution, "source");
      const auto provenance = source_record ? json::get_string(*source_record, "provenance") : std::string();
      const bool inferred = provenance == "model_inferred";
      if (inferred) { attrs["content_verification"] = "unverified"; attrs["semantic_value_asserted"] = false; }
      const auto source = graph.capture_json("defaults/" + default_key, attrs, "", provenance.empty() ? "effective_layer_resolution" : provenance);
      const std::string identity = json::get_string(resolution, "id", default_key);
      LOOM_TRY_ASSIGN(auto default_id, graph.entity("default", identity, default_key, attrs,
          inferred ? Origin::ModelKnowledge : layer == "user" ? Origin::User : Origin::System,
          inferred ? EvidenceClass::Inferred : layer == "user" ? EvidenceClass::User : EvidenceClass::Derived));
      LOOM_TRY(graph.relation(profile_id, "has_default", default_id, source));
      if (inferred && source_record) {
        const auto profile_field = json::get_string(*source_record, "profile_field");
        if (profile["fields"].contains(profile_field)) {
          const auto candidate_id = json::get_string(profile["fields"][profile_field], "candidate_id");
          if (!candidate_id.empty()) result_candidates[candidate_id].emplace_back(default_id, source);
        }
      }
      LOOM_TRY(graph.literal(default_id, "has_state", resolution["status"], source, Origin::System, EvidenceClass::Derived));
      LOOM_TRY(graph.relation(default_id, "from_layer", layer == "builtin" || layer == "pack_proposal" ? builtin_layer : user_layer, source));
      if (resolution["status"] == "effective" && !inferred) {
        LOOM_TRY(graph.literal(default_id, "has_value", resolution["value"], source,
            layer == "user" ? Origin::User : Origin::Repo, layer == "user" ? EvidenceClass::User : EvidenceClass::Observed));
      } else if (resolution["status"] == "excluded") {
        LOOM_TRY_ASSIGN(auto marker, graph.entity("exclusion", identity, default_key,
            Json{{"default_id", identity}, {"key", default_key}, {"rule", resolution["source"]}}, Origin::User, EvidenceClass::User));
        LOOM_TRY(graph.relation(marker, "excluded", default_id, source));
      }
    }

    LOOM_TRY_ASSIGN(auto kbpack, kb::Pack::load_builtin());
    const auto& types = kbpack->types();
    const auto types_source = graph.capture_json("schema/types.json", types, "", "builtin_definition");
    LOOM_TRY_ASSIGN(auto type_document, graph.entity("pack_document", "schema/types.json/" + hash(types), "Type definitions",
        Json{{"path", "schema/types.json"}, {"definition", types}, {"definition_sha256", hash(types)}}, Origin::Repo, EvidenceClass::Observed, true));
    for (const auto& descriptor : types["entity_kinds"]) {
      const auto kind = json::get_string(descriptor, "kind");
      if (kind.empty()) return invalid("builtin entity kind lacks name");
      LOOM_TRY_ASSIGN(auto type_id, graph.entity("type", kind, kind, Json{{"definition", descriptor}, {"definition_sha256", hash(descriptor)}},
          Origin::Repo, EvidenceClass::Observed, true));
      LOOM_TRY(graph.relation(type_document, "declares_type", type_id, types_source));
    }
    for (const auto& entry : pack["entries"]) {
      if (!entry["value"].is_object() || !entry["value"].contains("pack_path")) continue;
      LOOM_TRY_ASSIGN(auto resolution, defaults.resolve(entry["key"].get<std::string>()));
      if (resolution["status"] != "effective") continue;
      if (!resolution["value"].is_object() || !resolution["value"].contains("pack_path")) continue;
      const auto path = json::get_string(resolution["value"], "pack_path");
      const auto& document = kbpack->file(path);
      if (document.is_null()) return Error(Errc::Unavailable, "onboarding graph: pack document absent: " + path);
      const auto source = graph.capture_json(path, document, "", "builtin_definition");
      LOOM_TRY_ASSIGN(auto document_id, graph.entity("pack_document", path + "/" + hash(document), path,
          Json{{"path", path}, {"definition", document}, {"definition_sha256", hash(document)},
               {"role", resolution["value"].value("role", Json(nullptr))}, {"user_personal_fact", false}}, Origin::Repo, EvidenceClass::Observed, true));
      LOOM_TRY(graph.relation(profile_id, "has_profile", document_id, source, Json{{"role", "application_probe"}}));
    }

    if (!pack.contains("method_selection") || !pack["method_selection"].is_object() ||
        !pack["method_selection"].contains("parameter_layers") || !pack["method_selection"]["parameter_layers"].is_array())
      return Error(Errc::Unavailable, "onboarding graph: method selection/parameter precedence data is absent");
    Json method_selection = pack["method_selection"];
    method_selection["members"] = Json::array();
    Json method_bindings = Json::array();
    Json methods = pack.value("methods", Json::array());
    if (!methods.is_array()) return invalid("pack.methods must be an array");
    if (scenario.contains("graph_method")) methods.push_back(scenario["graph_method"]);
    for (const auto& source_descriptor : methods) {
      Json descriptor = source_descriptor;
      const auto default_key = json::get_string(descriptor, "default_key");
      Json method_resolution = Json::object(), prompt_resolution = Json::object();
      if (!default_key.empty()) {
        LOOM_TRY_ASSIGN(auto resolution, defaults.resolve(default_key));
        if (resolution["status"] != "effective") continue;
        method_resolution = resolution;
        const auto& value = resolution["value"];
        if (value.is_object() && value.contains("id") && value.contains("recipe") && value.contains("parameters"))
          descriptor = value;
      }
      const auto prompt_key = json::get_string(source_descriptor, "prompt_default_key");
      if (!prompt_key.empty()) {
        LOOM_TRY_ASSIGN(auto resolution, defaults.resolve(prompt_key));
        if (resolution["status"] != "effective") continue;
        prompt_resolution = resolution;
        if (resolution["value"].is_string()) descriptor["prompt"]["text"] = resolution["value"];
      }
      LOOM_TRY_ASSIGN(auto binding, project_method(graph, descriptor));
      method_selection["members"].push_back(Json{{"method_version_id", binding["method_version_id"]}});
      method_bindings.push_back(binding);
      const auto source = graph.capture_json("method/binding", binding, "", "definition_binding");
      const auto identity_source = graph.capture_json("method/identity", Json{{"id", descriptor["id"]}}, "", "effective_definition", true);
      LOOM_TRY(graph.relation(profile_id, "has_method", binding["method_id"].get<std::string>(), identity_source));
      LOOM_TRY(graph.relation(profile_id, "uses_method_version", binding["method_version_id"].get<std::string>(), source));
      for (const auto& [target, resolution] : std::vector<std::pair<std::string, Json>>{
             {binding["method_version_id"].get<std::string>(), method_resolution},
             {binding["prompt_version_id"].get<std::string>(), prompt_resolution}}) {
        if (resolution.empty()) continue;
        const auto lineage = graph.capture_json("method/default-origin", resolution, "", "effective_layer_resolution");
        LOOM_TRY(graph.relation(target, "from_layer", resolution["layer"] == "user" ? user_layer : builtin_layer, lineage));
      }
    }
    if (profile.contains("method_executions")) {
      if (!profile["method_executions"].is_array()) return invalid("method_executions must be an array");
      for (const auto& execution : profile["method_executions"]) {
        if (!execution.is_object() || !execution.contains("method_profile") ||
            !execution.contains("result_candidate_ids") || !execution["result_candidate_ids"].is_array())
          return invalid("execution needs exact method profile and complete result candidate identities");
        // A retained execution that no longer has a surviving result is not
        // copied into this current graph; deleted bytes cannot return through
        // a stale execution's embedded source profile.
        bool retained = false;
        for (const auto& result : execution["result_candidate_ids"])
          if (result.is_string() && result_candidates.contains(result.get<std::string>())) retained = true;
        if (!retained) continue;
        LOOM_TRY(graph.import_method_profile(execution["method_profile"]));
        std::string method_version = json::get_string(execution, "method_version_id");
        if (method_version.empty()) {
          const auto& bindings = execution["method_profile"].at("bindings");
          if (!bindings.is_array() || bindings.size() != 1) return invalid("execution needs one unambiguous method version binding");
          method_version = bindings[0].at("method_version_id").get<std::string>();
        }
        bool bound = false;
        for (const auto& binding : execution["method_profile"].at("bindings"))
          if (binding.value("method_version_id", Json(nullptr)) == method_version) bound = true;
        if (!bound) return conflict("execution method version differs from captured binding");
        Json receipt = execution; receipt.erase("method_profile");
        const auto source = graph.capture_json("execution/" + json::get_string(execution, "run_id"), receipt,
            json::get_string(execution, "known_at"), "host_execution_receipt");
        LOOM_TRY(graph.actual_run(execution, method_version, source));
        for (const auto& candidate : execution["result_candidate_ids"]) {
          if (!candidate.is_string()) return invalid("result candidate identity must be a string");
          const auto id = candidate.get<std::string>();
          if (!result_candidates.contains(id)) continue;
          for (const auto& [result_id, result_source] : result_candidates[id]) {
            (void)result_source;
            LOOM_TRY(graph.relation(result_id, "produced_in_run", execution["run_id"].get<std::string>(), source));
            LOOM_TRY(graph.relation(result_id, "produced_by_method_version", method_version, source));
          }
        }
      }
    }
    (void)profile_source;
    return graph.result(method_selection, method_bindings);
  } catch (const std::exception& e) { return invalid(e.what()); }
}
}  // namespace loom::onboarding
