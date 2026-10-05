// model.h: typed views over the data pack (kb::Pack keeps the validated
// JSON; these parse it into model types).
#include <algorithm>

#include "model/model_json.h"
#include "model/model_profile.h"

namespace loom::model {

namespace {

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
