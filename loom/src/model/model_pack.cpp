// model.h: typed views over the data pack (kb::Pack keeps the validated
// JSON; these parse it into model types).
#include <algorithm>
#include <array>

#include "loom/model_profile.h"
#include "model/model_json.h"
#include "model/model_profile.h"

namespace loom::model {

namespace {

template <class T>
Status validate_creation(const Json& record) {
  LOOM_TRY(T::from_json(record));
  return {};
}

struct CreationChild {
  std::string_view field;
  std::string_view kind;
};
struct CreationCapability {
  std::string_view kind;
  Status (*validate)(const Json&);
  bool explicit_origin;
  std::vector<CreationChild> children;
};

// This is a registry of existing wire decoders and structural relationships,
// not a source of mutable creation priors or domain policy.
const std::array<CreationCapability, 7>& creation_registry() {
  static const std::array<CreationCapability, 7> capabilities{{
      {"principle", validate_creation<Principle>, true, {}},
      {"operator", validate_creation<Operator>, true, {}},
      {"morphism", validate_creation<Morphism>, true, {}},
      {"slot", validate_creation<SlotSpec>, false, {}},
      {"domain_relation", validate_creation<DomainRelation>, false, {}},
      {"domain_kind", validate_creation<DomainKind>, false, {{"slots", "slot"}, {"relations", "domain_relation"}}},
      {"project_kind", validate_creation<ProjectKind>, true, {{"domain_kinds", "domain_kind"}}},
  }};
  return capabilities;
}

std::string creation_pointer(std::string_view field) {
  std::string out;
  for (const char c : field) {
    if (c == '~') out += "~0";
    else if (c == '/') out += "~1";
    else out += c;
  }
  return out;
}

Result<Json> normalize_creation_record(std::string_view kind, const Json& record,
                                       const RuntimeProfile& profile, Json& applied,
                                       const std::string& prefix) {
  const auto& registry = creation_registry();
  const auto found = std::find_if(registry.begin(), registry.end(),
                                  [&](const auto& entry) { return entry.kind == kind; });
  if (found == registry.end()) return Error(Errc::InvalidArgument, "unsupported model creation kind '" + std::string(kind) + "'");
  if (!record.is_object()) return Error(Errc::InvalidArgument, "model creation record must be an object");
  if (found->explicit_origin && (!record.contains("origin") || !record.at("origin").is_string() ||
                                 record.at("origin").get_ref<const std::string&>().empty())) {
    return Error(Errc::InvalidArgument, "model creation " + std::string(kind) + " requires explicit producer origin");
  }

  Json normalized = record;
  const auto& defaults = profile.values().at("creation_defaults").at(std::string(kind));
  // Validate explicit policy fields against the same checked schema as priors;
  // null must not silently re-enter a historic decoder's missing-field branch.
  Json effective = profile.values();
  for (auto it = defaults.begin(); it != defaults.end(); ++it) {
    if (!normalized.contains(it.key())) {
      normalized[it.key()] = it.value();
      applied[prefix + "/" + creation_pointer(it.key())] = it.value();
    }
    effective["creation_defaults"][std::string(kind)][it.key()] = normalized.at(it.key());
  }
  LOOM_TRY(profile.with_values(effective));

  for (const auto& child : found->children) {
    const std::string field(child.field);
    if (!normalized.contains(field) || normalized.at(field).is_null()) continue;
    if (!normalized.at(field).is_array()) return Error(Errc::InvalidArgument, "model creation " + field + " must be an array");
    auto& values = normalized[field];
    for (std::size_t i = 0; i < values.size(); ++i) {
      LOOM_TRY_ASSIGN(values[i], normalize_creation_record(child.kind, values[i], profile, applied,
                      prefix + "/" + creation_pointer(field) + "/" + std::to_string(i)));
    }
  }
  LOOM_TRY(found->validate(normalized));
  return normalized;
}

Result<const Json*> file_of(const kb::Pack& pack, const std::string& path, std::string_view what, std::string_view id) {
  const Json& d = pack.file(path);
  if (!d.is_object()) return Error(Errc::NotFound, "no " + std::string(what) + " '" + std::string(id) + "' in the pack");
  return &d;
}

// Elements of `list_key` of every pack file with the given schema id, in
// file path order.
std::vector<const Json*> elements(const kb::Pack& pack, std::string_view schema, std::string_view list_key) {
  std::vector<const Json*> out;
  for (const auto& f : pack.files()) {
    const Json& d = pack.file(f);
    if (json::get_string(d, "schema") != schema) continue;
    const Json* a = json::find(d, list_key);
    if (!a || !a->is_array()) continue;
    for (const auto& x : *a) out.push_back(&x);
  }
  return out;
}

template <class T>
Result<T> find_element(const kb::Pack& pack, std::string_view schema, std::string_view list_key, std::string_view id,
                       std::string_view what) {
  for (const Json* x : elements(pack, schema, list_key)) {
    if (json::get_string(*x, "id") == id) return T::from_json(*x);
  }
  return Error(Errc::NotFound, "no " + std::string(what) + " '" + std::string(id) + "' in the pack");
}

template <class T>
Result<std::vector<T>> all_elements(const kb::Pack& pack, std::string_view schema, std::string_view list_key) {
  std::vector<T> out;
  for (const Json* x : elements(pack, schema, list_key)) {
    LOOM_TRY_ASSIGN(T v, T::from_json(*x));
    out.push_back(std::move(v));
  }
  std::sort(out.begin(), out.end(), [](const T& a, const T& b) { return a.id < b.id; });
  return out;
}

}  // namespace

Json creation_capabilities() {
  Json entries = Json::array();
  for (const auto& capability : creation_registry()) {
    Json children = Json::array();
    for (const auto& child : capability.children) children.push_back(Json{{"field", child.field}, {"kind", child.kind}});
    entries.push_back(Json{{"kind", capability.kind}, {"explicit_producer_origin", capability.explicit_origin},
                          {"children", std::move(children)}});
  }
  return Json{{"schema", "loom.model_creation_capabilities/1"}, {"records", std::move(entries)}};
}

Result<CreationRecord> normalize_creation(std::string_view kind, const Json& record) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::builtin("model"));
  return normalize_creation(kind, record, profile);
}

Result<CreationRecord> normalize_creation(std::string_view kind, const Json& record, const RuntimeProfile& profile) {
  LOOM_TRY_ASSIGN(auto checked, detail::checked_model_profile(profile));
  CreationRecord result;
  result.profile_hash = checked.hash();
  LOOM_TRY_ASSIGN(result.record, normalize_creation_record(kind, record, checked, result.applied_defaults, ""));
  return result;
}

Result<ProjectKind> project_kind(const kb::Pack& pack, std::string_view id) {
  LOOM_TRY_ASSIGN(const Json* d, file_of(pack, "project_kinds/" + std::string(id) + ".json", "project kind", id));
  return ProjectKind::from_json(*d);
}
Result<Facet> facet(const kb::Pack& pack, std::string_view id) {
  LOOM_TRY_ASSIGN(const Json* d, file_of(pack, "facets/" + std::string(id) + ".json", "facet", id));
  return Facet::from_json(*d);
}
Result<ArtifactType> artifact_type(const kb::Pack& pack, std::string_view id) {
  LOOM_TRY_ASSIGN(const Json* d, file_of(pack, "artifact_types/" + std::string(id) + ".json", "artifact type", id));
  return ArtifactType::from_json(*d);
}
Result<Principle> principle(const kb::Pack& pack, std::string_view id) {
  return find_element<Principle>(pack, "loom.kb.principles/2", "principles", id, "principle");
}
Result<Operator> pack_operator(const kb::Pack& pack, std::string_view id) {
  auto r = find_element<Operator>(pack, "loom.kb.operators/1", "operators", id, "operator");
  if (r || r.error().code != Errc::NotFound) return r;
  return find_element<Operator>(pack, "loom.kb.rules/2", "rules", id, "operator");
}
Result<Morphism> morphism(const kb::Pack& pack, std::string_view id) {
  auto r = find_element<Morphism>(pack, "loom.kb.morphisms/1", "morphisms", id, "morphism");
  if (r || r.error().code != Errc::NotFound) return r;
  LOOM_TRY_ASSIGN(auto anchors, anchoring_morphisms(pack));
  for (auto& m : anchors) {
    if (m.id == id) return m;
  }
  return r;
}
Result<GoalType> goal_type(const kb::Pack& pack, std::string_view id) {
  return find_element<GoalType>(pack, "loom.kb.goal_types/1", "goal_types", id, "goal type");
}
Result<AnchoringModel> anchoring(const kb::Pack& pack) {
  LOOM_TRY_ASSIGN(const Json* d, file_of(pack, "morphisms/anchoring.json", "anchoring model", "anchoring"));
  return AnchoringModel::from_json(*d);
}
Result<std::vector<Principle>> principles(const kb::Pack& pack) {
  return all_elements<Principle>(pack, "loom.kb.principles/2", "principles");
}
Result<std::vector<Operator>> pack_operators(const kb::Pack& pack) {
  LOOM_TRY_ASSIGN(auto ops, all_elements<Operator>(pack, "loom.kb.operators/1", "operators"));
  LOOM_TRY_ASSIGN(auto rules, all_elements<Operator>(pack, "loom.kb.rules/2", "rules"));
  for (auto& r : rules) ops.push_back(std::move(r));
  std::sort(ops.begin(), ops.end(), [](const Operator& a, const Operator& b) { return a.id < b.id; });
  return ops;
}
std::string earliest_source_date(const std::vector<Reference>& sources) {
  std::string best;
  for (const auto& r : sources) {
    if (r.date.empty()) continue;
    std::string d = r.date.substr(0, 10);
    if (best.empty() || d < best) best = d;
  }
  return best;
}

bool prior_visible(const std::vector<Reference>& sources, const PriorFilter& filter) {
  if (!filter.enabled) return false;
  if (filter.as_of.empty()) return true;
  std::string d = earliest_source_date(sources);
  return !d.empty() && d <= filter.as_of.substr(0, 10);
}

Result<std::vector<Principle>> principles(const kb::Pack& pack, const PriorFilter& filter) {
  LOOM_TRY_ASSIGN(auto all, principles(pack));
  std::vector<Principle> out;
  for (auto& p : all) {
    if (prior_visible(p.sources, filter)) out.push_back(std::move(p));
  }
  return out;
}

Result<std::vector<Operator>> pack_operators(const kb::Pack& pack, const PriorFilter& filter) {
  LOOM_TRY_ASSIGN(auto ops, all_elements<Operator>(pack, "loom.kb.operators/1", "operators"));
  LOOM_TRY_ASSIGN(auto rules, all_elements<Operator>(pack, "loom.kb.rules/2", "rules"));
  std::vector<Operator> out;
  for (auto& o : ops) {
    if (prior_visible(o.sources, filter)) out.push_back(std::move(o));
  }
  for (auto& r : rules) out.push_back(std::move(r));
  std::sort(out.begin(), out.end(), [](const Operator& a, const Operator& b) { return a.id < b.id; });
  return out;
}

Result<std::vector<Morphism>> morphisms(const kb::Pack& pack) {
  return all_elements<Morphism>(pack, "loom.kb.morphisms/1", "morphisms");
}

Result<std::vector<Morphism>> anchoring_morphisms(const kb::Pack& pack) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::builtin("model"));
  return anchoring_morphisms(pack, profile);
}

Result<std::vector<Morphism>> anchoring_morphisms(const kb::Pack& pack, const RuntimeProfile& profile) {
  LOOM_TRY_ASSIGN(auto checked, detail::checked_model_profile(profile));
  std::vector<Morphism> out;
  auto add = [&](const ParadigmHeader& h, const std::vector<DomainKind>& kinds) -> Status {
    for (const auto& k : kinds) {
      Morphism m;
      m.id = "m.anchor." + h.id + "." + k.id;
      m.use = MorphismUse::Anchoring;
      m.from.paradigm = h.id;
      m.from.kind = k.id;
      m.to.role = k.role;
      m.bidirectional = false;
      m.confidence = 1.0;
      LOOM_TRY_ASSIGN(m.rationale, render_profile_template(checked.values().at("anchoring_rationale").get<std::string>(),
                         Json{{"paradigm", h.id}, {"kind", k.id}, {"role", std::string(to_string(k.role))}}));
      m.origin = h.origin;
      m.validation = h.validation;
      out.push_back(std::move(m));
    }
    return {};
  };
  for (const auto& f : pack.files()) {
    const Json& d = pack.file(f);
    std::string schema = json::get_string(d, "schema");
    if (schema == "loom.kb.project_kind/1") {
      LOOM_TRY_ASSIGN(auto pk, ProjectKind::from_json(d));
      LOOM_TRY(add(pk.header, pk.domain_kinds));
    } else if (schema == "loom.kb.facet/1") {
      LOOM_TRY_ASSIGN(auto fc, Facet::from_json(d));
      LOOM_TRY(add(fc.header, fc.domain_kinds));
    }
  }
  std::sort(out.begin(), out.end(), [](const Morphism& a, const Morphism& b) { return a.id < b.id; });
  return out;
}

}  // namespace loom::model
