// model.h: Layer C — principles, operators, morphisms, paradigm views
// (project kinds, facets, artifact types, anchoring), areas and instances.
#include <algorithm>
#include <set>

#include "model/model_json.h"

namespace loom::model {

using detail::join_key;
using detail::Rd;

// ── Scope / principles ──────────────────────────────────────────────
bool Scope::empty() const noexcept {
  return project_kinds.empty() && facets.empty() && artifact_types.empty() && areas.empty() && products.empty() &&
         conditions.empty();
}
Json Scope::to_json() const {
  return Json{{"project_kinds", detail::strs(project_kinds)}, {"facets", detail::strs(facets)},
              {"artifact_types", detail::strs(artifact_types)}, {"areas", detail::strs(areas)},
              {"products", detail::strs(products)},           {"conditions", detail::strs(conditions)}};
}
Result<Scope> Scope::from_json(const Json& j) {
  Rd r(j, "scope");
  Scope x;
  x.project_kinds = r.strs("project_kinds");
  x.facets = r.strs("facets");
  x.artifact_types = r.strs("artifact_types");
  x.areas = r.strs("areas");
  x.products = r.strs("products");
  x.conditions = r.strs("conditions");
  if (!r.ok()) return r.error();
  return x;
}

Json SituationSolution::to_json() const { return Json{{"situation", situation}, {"solution", solution}}; }
Result<SituationSolution> SituationSolution::from_json(const Json& j) {
  Rd r(j, "predicts");
  SituationSolution x;
  x.situation = r.str("situation", true);
  x.solution = r.str("solution", true);
  if (!r.ok()) return r.error();
  return x;
}

Json Principle::to_json() const {
  return Json{{"id", id},
              {"statement", text_to_json(statement)},
              {"phrasings", detail::strs(phrasings)},
              {"level", detail::en(level)},
              {"form", detail::en(form)},
              {"scope", scope.to_json()},
              {"protects", detail::strs(protects)},
              {"derived_from", detail::strs(derived_from)},
              {"evidence_for", detail::strs(evidence_for)},
              {"counterexamples", detail::strs(counterexamples)},
              {"exceptions", detail::strs(exceptions)},
              {"confidence", confidence},
              {"predicts", detail::list(predicts)},
              {"conflicts_with", detail::strs(conflicts_with)},
              {"supersedes", detail::strs(supersedes)},
              {"validation_status", detail::en(validation)},
              {"owner", owner},
              {"origin", detail::en(origin)},
              {"sources", detail::list(sources)},
              {"checks", detail::strs(checks)},
              {"value_constraints", value_constraints}};
}
Result<Principle> Principle::from_json(const Json& j) {
  Rd r(j, "principle");
  Principle x;
  x.id = r.str("id", true);
  x.statement = r.text("statement", true);
  x.phrasings = r.strs("phrasings");
  x.level = r.en<PrincipleLevel>("level", PrincipleLevel::Strategy, true);
  x.form = r.en<PrincipleForm>("form", PrincipleForm::Heuristic, true);
  x.scope = r.obj<Scope>("scope");
  x.protects = r.strs("protects");
  x.derived_from = r.strs("derived_from");
  x.evidence_for = r.strs("evidence_for");
  x.counterexamples = r.strs("counterexamples");
  x.exceptions = r.strs("exceptions");
  x.confidence = r.unit("confidence", 0.5);
  x.predicts = r.list<SituationSolution>("predicts");
  x.conflicts_with = r.strs("conflicts_with");
  x.supersedes = r.strs("supersedes");
  x.validation = r.en<ValidationStatus>("validation_status", ValidationStatus::Candidate);
  x.owner = r.str("owner");
  x.origin = r.en<Origin>("origin", Origin::Archive);
  x.sources = r.list<Reference>("sources");
  x.checks = r.strs("checks");
  x.value_constraints = r.object("value_constraints");
  if (!r.ok()) return r.error();
  return x;
}

// ── Operators ───────────────────────────────────────────────────────
Status Operator::validate() const {
  auto bad = [&](const std::string& m) { return Error(Errc::InvalidArgument, "operator " + id + ": " + m); };
  if (id.empty()) return bad("id is required");
  if (produces) {
    if (!is_producible(*produces)) return bad("a rule produces derived, inferred or extrapolated");
    int want = *produces == EvidenceClass::Derived ? 0 : *produces == EvidenceClass::Inferred ? 1 : 2;
    if (stratum != want) {
      return bad("stratum " + std::to_string(stratum) + " must produce '" + std::string(to_string(*produces)) +
                 "' at stratum " + std::to_string(want));
    }
    if (!when.is_object()) return bad("a rule has a 'when' condition");
    if (!value.is_object()) return bad("a rule has a 'value' expression");
    if (!target.is_object()) return bad("a rule has a 'target'");
    if (*produces == EvidenceClass::Inferred && !expected) return bad("an inferring rule carries an expected property");
  } else {
    if (situation.empty() || solution.empty()) return bad("a design operator has situation and solution text");
    if (stratum != -1) return bad("only rules have a stratum");
  }
  return {};
}
Json Operator::to_json() const {
  return Json{{"id", id},
              {"version", version},
              {"situation", text_to_json(situation)},
              {"solution", text_to_json(solution)},
              {"when", when},
              {"value", value},
              {"target", target},
              {"produces", produces ? detail::en(*produces) : Json(nullptr)},
              {"stratum", stratum},
              {"expected_property", expected ? expected->to_json() : Json(nullptr)},
              {"principles", detail::strs(principles)},
              {"examples", detail::list(examples)},
              {"success", success},
              {"failure", failure},
              {"confidence", confidence},
              {"validation_status", detail::en(validation)},
              {"origin", detail::en(origin)},
              {"sources", detail::list(sources)},
              {"basis", basis}};
}
Result<Operator> Operator::from_json(const Json& j) {
  Rd r(j, "operator");
  Operator x;
  x.id = r.str("id", true);
  x.version = r.integer("version", 1);
  x.situation = r.text("situation");
  x.solution = r.text("solution");
  x.when = r.expr("when");
  x.value = r.expr("value");
  x.target = r.raw("target");
  if (!x.target.is_null() && !x.target.is_object()) r.fail("target", "expected an object");
  x.produces = r.opt_en<EvidenceClass>("produces");
  x.stratum = r.integer("stratum", -1);
  x.expected = r.expected("expected_property");
  x.principles = r.strs("principles");
  x.examples = r.list<Reference>("examples");
  x.success = r.integer("success", 0);
  x.failure = r.integer("failure", 0);
  x.confidence = r.unit("confidence", 0.5);
  x.validation = r.en<ValidationStatus>("validation_status", ValidationStatus::Candidate);
  x.origin = r.en<Origin>("origin", Origin::Archive);
  x.sources = r.list<Reference>("sources");
  x.basis = r.object("basis");
  if (!r.ok()) return r.error();
  LOOM_TRY(x.validate());
  return x;
}

// ── Morphisms ───────────────────────────────────────────────────────
Json MorphismEnd::to_json() const {
  return Json{{"paradigm", paradigm}, {"kind", kind},     {"slot", slot},
              {"relation", relation}, {"target", target}, {"role", detail::opt_en(role)}};
}
Result<MorphismEnd> MorphismEnd::from_json(const Json& j) {
  Rd r(j, "end");
  MorphismEnd x;
  x.paradigm = r.str("paradigm");
  x.kind = r.str("kind");
  x.slot = r.str("slot");
  x.relation = r.str("relation");
  x.target = r.str("target");
  x.role = r.opt_en<Role>("role");
  if (r.ok()) {
    bool domain = !x.paradigm.empty() && !x.kind.empty();
    if (domain == x.role.has_value()) r.fail("", "an end is either {paradigm, kind[, slot|relation+target]} or {role}");
    if (!x.relation.empty() && x.target.empty()) r.fail("target", "a relation end names the target kind");
  }
  if (!r.ok()) return r.error();
  return x;
}

Json Morphism::to_json() const {
  Json conds = Json::array();
  for (const auto& c : conditions) conds.push_back(c);
  return Json{{"id", id},
              {"use", detail::en(use)},
              {"from", from.to_json()},
              {"to", to.to_json()},
              {"bidirectional", bidirectional},
              {"conditions", conds},
              {"mode", detail::en(mode)},
              {"confidence", confidence},
              {"expected_property", expected ? expected->to_json() : Json(nullptr)},
              {"rationale", rationale},
              {"origin", detail::en(origin)},
              {"validation_status", detail::en(validation)},
              {"sources", detail::list(sources)}};
}
Result<Morphism> Morphism::from_json(const Json& j) {
  Rd r(j, "morphism");
  Morphism x;
  x.id = r.str("id", true);
  x.use = r.en<MorphismUse>("use", MorphismUse::Transfer, true);
  x.from = r.obj<MorphismEnd>("from", true);
  x.to = r.obj<MorphismEnd>("to", true);
  x.bidirectional = r.boolean("bidirectional", x.use == MorphismUse::Transfer);
  for (const auto& c : r.array("conditions")) {
    if (!c.is_object()) r.fail("conditions", "conditions are expression objects");
    x.conditions.push_back(c);
  }
  x.mode = r.en<TransferMode>("mode", TransferMode::Presence);
  x.confidence = r.unit("confidence", 0.5);
  x.expected = r.expected("expected_property");
  x.rationale = r.str("rationale");
  x.origin = r.en<Origin>("origin", Origin::ModelKnowledge);
  x.validation = r.en<ValidationStatus>("validation_status", ValidationStatus::Candidate);
  x.sources = r.list<Reference>("sources");
  if (r.ok()) {
    if (x.use == MorphismUse::Anchoring) {
      if (x.from.role || !x.to.role) r.fail("to", "an anchoring morphism maps a domain kind onto a universal role");
      if (x.bidirectional) r.fail("bidirectional", "anchoring is one-way (domain -> meta-model)");
    } else {
      if (x.from.role || x.to.role) r.fail("to", "a transfer morphism maps domain kinds of two paradigms");
      if (x.from.paradigm == x.to.paradigm && x.from.kind == x.to.kind) r.fail("to", "a transfer maps two different kinds");
      if (!x.expected) r.fail("expected_property", "a transfer states what a transferred inference vouches for");
    }
  }
  if (!r.ok()) return r.error();
  return x;
}

// ── Paradigm views ──────────────────────────────────────────────────
Json SlotSpec::to_json() const {
  return Json{{"name", name},     {"type", type},     {"card", detail::en(card)},
              {"required", required}, {"weight", weight}, {"relation", relation},
              {"fields", fields}, {"bind", bind},     {"rules", detail::strs(rules)},
              {"description", description}};
}
Result<SlotSpec> SlotSpec::from_json(const Json& j) {
  Rd r(j, "slot");
  SlotSpec x;
  x.name = r.str("name", true);
  x.type = r.str("type", true);
  x.card = r.en<Cardinality>("card", Cardinality::One);
  x.required = r.boolean("required", false);
  x.weight = r.num("weight", 1.0);
  x.relation = r.str("relation");
  x.fields = r.object("fields");
  x.bind = r.array("bind");
  x.rules = r.strs("rules");
  x.description = r.str("description");
  if (r.ok() && x.weight <= 0) r.fail("weight", "must be positive");
  if (!r.ok()) return r.error();
  return x;
}

Json DomainRelation::to_json() const { return Json{{"rel", rel}, {"target", target}, {"card", detail::en(card)}}; }
Result<DomainRelation> DomainRelation::from_json(const Json& j) {
  Rd r(j, "relation");
  DomainRelation x;
  x.rel = r.str("rel", true);
  x.target = r.str("target", true);
  x.card = r.en<Cardinality>("card", Cardinality::Many);
  if (!r.ok()) return r.error();
  return x;
}

Json DomainKind::to_json() const {
  return Json{{"id", id},
              {"role", detail::en(role)},
              {"labels", text_to_json(labels)},
              {"description", description},
              {"entity_kind", entity_kind},
              {"value_type", value_type},
              {"card", detail::en(card)},
              {"required", required},
              {"weight", weight},
              {"relation", relation},
              {"relations", detail::list(relations)},
              {"slots", detail::list(slots)},
              {"anchors", anchors},
              {"bind", bind},
              {"rules", detail::strs(rules)}};
}
Result<DomainKind> DomainKind::from_json(const Json& j) {
  Rd r(j, "domain_kind");
  DomainKind x;
  x.id = r.str("id", true);
  x.role = r.en<Role>("role", Role::Part, true);
  x.labels = r.text("labels");
  x.description = r.str("description");
  x.entity_kind = r.str("entity_kind");
  x.value_type = r.str("value_type", false, x.entity_kind.empty() ? "text" : "entity:" + x.entity_kind);
  x.card = r.en<Cardinality>("card", Cardinality::Many);
  x.required = r.boolean("required", false);
  x.weight = r.num("weight", 1.0);
  x.relation = r.str("relation");
  x.relations = r.list<DomainRelation>("relations");
  x.slots = r.list<SlotSpec>("slots");
  x.anchors = r.object("anchors");
  x.bind = r.array("bind");
  x.rules = r.strs("rules");
  if (r.ok() && x.weight <= 0) r.fail("weight", "must be positive");
  if (r.ok()) {
    std::set<std::string> names;
    for (const auto& s : x.slots) {
      if (!names.insert(s.name).second) r.fail("slots", "duplicate slot '" + s.name + "'");
    }
  }
  if (!r.ok()) return r.error();
  return x;
}

Json ParadigmHeader::to_json() const {
  return Json{{"id", id},
              {"version", version},
              {"title", text_to_json(title)},
              {"description", description},
              {"origin", detail::en(origin)},
              {"validation_status", detail::en(validation)},
              {"derived_from", detail::strs(derived_from)},
              {"sources", detail::list(sources)}};
}
Result<ParadigmHeader> ParadigmHeader::from_json(const Json& j) {
  Rd r(j, "paradigm");
  ParadigmHeader x;
  x.id = r.str("id", true);
  x.version = r.integer("version", 1);
  x.title = r.text("title", true);
  x.description = r.str("description");
  x.origin = r.en<Origin>("origin", Origin::Archive);
  x.validation = r.en<ValidationStatus>("validation_status", ValidationStatus::Candidate);
  x.derived_from = r.strs("derived_from");
  x.sources = r.list<Reference>("sources");
  if (r.ok() && x.version < 1) r.fail("version", "must be >= 1");
  if (!r.ok()) return r.error();
  return x;
}

namespace {
// Header keys first, then the view's own keys.
Json with_header(const ParadigmHeader& h, std::initializer_list<std::pair<const char*, Json>> rest) {
  Json o = h.to_json();
  for (const auto& [k, v] : rest) o[k] = v;
  return o;
}
std::string unique_kinds(const std::vector<DomainKind>& kinds) {
  std::set<std::string> ids;
  for (const auto& k : kinds) {
    if (!ids.insert(k.id).second) return "duplicate domain kind '" + k.id + "'";
  }
  return {};
}
const DomainKind* find_kind(const std::vector<DomainKind>& kinds, std::string_view id) noexcept {
  for (const auto& k : kinds) {
    if (k.id == id) return &k;
  }
  return nullptr;
}
}  // namespace

const DomainKind* ProjectKind::domain_kind(std::string_view id) const noexcept { return find_kind(domain_kinds, id); }
Json ProjectKind::to_json() const {
  return with_header(header, {{"subject_kind", subject_kind},
                              {"anchors", anchors},
                              {"facets", detail::strs(facets)},
                              {"domain_kinds", detail::list(domain_kinds)},
                              {"constraints", constraints},
                              {"rules", detail::strs(rules)}});
}
Result<ProjectKind> ProjectKind::from_json(const Json& j) {
  LOOM_TRY_ASSIGN(ParadigmHeader h, ParadigmHeader::from_json(j));
  Rd r(j, "project_kind " + h.id);
  ProjectKind x;
  x.header = std::move(h);
  x.subject_kind = r.str("subject_kind", false, "project");
  x.anchors = r.object("anchors");
  x.facets = r.strs("facets");
  x.domain_kinds = r.list<DomainKind>("domain_kinds", true);
  x.constraints = r.array("constraints");
  x.rules = r.strs("rules");
  if (r.ok() && x.domain_kinds.empty()) r.fail("domain_kinds", "a project kind has domain kinds");
  if (r.ok()) {
    if (auto e = unique_kinds(x.domain_kinds); !e.empty()) r.fail("domain_kinds", e);
  }
  if (!r.ok()) return r.error();
  return x;
}

const DomainKind* Facet::domain_kind(std::string_view id) const noexcept { return find_kind(domain_kinds, id); }
Json Facet::to_json() const {
  return with_header(header, {{"applies_to", detail::strs(applies_to)},
                              {"anchors", anchors},
                              {"domain_kinds", detail::list(domain_kinds)},
                              {"constraints", constraints},
                              {"rules", detail::strs(rules)}});
}
Result<Facet> Facet::from_json(const Json& j) {
  LOOM_TRY_ASSIGN(ParadigmHeader h, ParadigmHeader::from_json(j));
  Rd r(j, "facet " + h.id);
  Facet x;
  x.header = std::move(h);
  x.applies_to = r.strs("applies_to", true);
  x.anchors = r.object("anchors");
  x.domain_kinds = r.list<DomainKind>("domain_kinds", true);
  x.constraints = r.array("constraints");
  x.rules = r.strs("rules");
  if (r.ok() && x.applies_to.empty()) r.fail("applies_to", "a facet names the project kinds it applies to");
  if (r.ok()) {
    if (auto e = unique_kinds(x.domain_kinds); !e.empty()) r.fail("domain_kinds", e);
  }
  if (!r.ok()) return r.error();
  return x;
}

Json ArtifactType::to_json() const {
  return with_header(header, {{"medium", detail::en(medium)},
                              {"detect", detect},
                              {"parse", parse},
                              {"extract", extract},
                              {"structure", detail::list(structure)}});
}
Result<ArtifactType> ArtifactType::from_json(const Json& j) {
  LOOM_TRY_ASSIGN(ParadigmHeader h, ParadigmHeader::from_json(j));
  Rd r(j, "artifact_type " + h.id);
  ArtifactType x;
  x.header = std::move(h);
  x.medium = r.en<Medium>("medium", Medium::Text, true);
  x.detect = r.object("detect");
  x.parse = r.object("parse");
  x.extract = r.array("extract");
  x.structure = r.list<SlotSpec>("structure");
  if (r.ok()) {
    std::set<std::string> names;
    for (const auto& s : x.structure) {
      if (!names.insert(s.name).second) r.fail("structure", "duplicate field '" + s.name + "'");
    }
  }
  if (!r.ok()) return r.error();
  return x;
}

Json RoleRelation::to_json() const {
  return Json{{"id", id}, {"from", detail::ens(from)}, {"to", detail::ens(to)}, {"labels", text_to_json(labels)}};
}
Result<RoleRelation> RoleRelation::from_json(const Json& j) {
  Rd r(j, "role_relation");
  RoleRelation x;
  x.id = r.str("id", true);
  x.from = r.ens<Role>("from");
  x.to = r.ens<Role>("to");
  x.labels = r.text("labels");
  if (r.ok() && (x.from.empty() || x.to.empty())) r.fail("", "a role relation names its from and to roles");
  if (!r.ok()) return r.error();
  return x;
}

const RoleRelation* AnchoringModel::role_relation(std::string_view id) const noexcept {
  for (const auto& rr : role_relations) {
    if (rr.id == id) return &rr;
  }
  return nullptr;
}
std::string AnchoringModel::check(std::string_view rel, Role from, Role to) const {
  auto it = relation_map.find(std::string(rel));
  if (it == relation_map.end()) return "relation '" + std::string(rel) + "' has no role relation in anchoring.relation_map";
  const RoleRelation* rr = role_relation(it->second);
  if (!rr) return "unknown role relation '" + it->second + "'";
  auto in = [](const std::vector<Role>& v, Role x) { return std::find(v.begin(), v.end(), x) != v.end(); };
  if (!in(rr->from, from) || !in(rr->to, to)) {
    return "'" + std::string(rel) + "' (" + rr->id + ") does not link " + std::string(to_string(from)) + " -> " +
           std::string(to_string(to));
  }
  return {};
}
Json AnchoringModel::to_json() const {
  Json m = Json::object();
  for (const auto& [k, v] : relation_map) m[k] = v;
  return Json{{"role_relations", detail::list(role_relations)}, {"relation_map", m}};
}
Result<AnchoringModel> AnchoringModel::from_json(const Json& j) {
  Rd r(j, "anchoring");
  AnchoringModel x;
  x.role_relations = r.list<RoleRelation>("role_relations", true);
  Json m = r.object("relation_map");
  for (auto it = m.begin(); it != m.end(); ++it) {
    if (!it.value().is_string()) {
      r.fail("relation_map/" + it.key(), "expected a role relation id");
      break;
    }
    x.relation_map[it.key()] = it.value().get<std::string>();
  }
  if (r.ok()) {
    std::set<std::string> ids;
    for (const auto& rr : x.role_relations) {
      if (!ids.insert(rr.id).second) r.fail("role_relations", "duplicate role relation '" + rr.id + "'");
    }
    for (const auto& [k, v] : x.relation_map) {
      if (!ids.count(v)) r.fail("relation_map/" + k, "unknown role relation '" + v + "'");
    }
  }
  if (!r.ok()) return r.error();
  return x;
}

// ── Areas / instances ───────────────────────────────────────────────
std::string Area::make_id(std::string_view subject, std::string_view observation, std::string_view statement) {
  return kb::stable_id("ar_", join_key({subject, observation, statement}));
}
Json Area::to_json() const {
  return Json{{"id", id},
              {"subject", subject},
              {"statement", statement},
              {"observation", observation},
              {"roles", detail::ens(roles)},
              {"kinds", detail::strs(kinds)},
              {"principle", principle},
              {"members", detail::strs(members)},
              {"inferred_members", detail::strs(inferred_members)},
              {"gap", gap}};
}
Result<Area> Area::from_json(const Json& j) {
  Rd r(j, "area");
  Area x;
  x.id = r.str("id", true);
  x.subject = r.str("subject", true);
  x.statement = r.str("statement", true);
  x.observation = r.str("observation");
  x.roles = r.ens<Role>("roles");
  x.kinds = r.strs("kinds");
  x.principle = r.str("principle");
  x.members = r.strs("members");
  x.inferred_members = r.strs("inferred_members");
  x.gap = r.boolean("gap", false);
  if (!r.ok()) return r.error();
  return x;
}

Json SlotValue::to_json() const {
  return Json{{"slot", slot}, {"ord", ord}, {"claim", claim}, {"role", detail::opt_en(role)}, {"conflict", conflict}};
}
Result<SlotValue> SlotValue::from_json(const Json& j) {
  Rd r(j, "slot_value");
  SlotValue x;
  x.slot = r.str("slot", true);
  x.ord = r.integer("ord", 0);
  x.claim = r.str("claim", true);
  x.role = r.opt_en<Role>("role");
  x.conflict = r.boolean("conflict", false);
  if (!r.ok()) return r.error();
  return x;
}

std::string Instance::make_id(std::string_view paradigm, std::string_view subject) {
  return kb::stable_id("in_", join_key({paradigm, subject}));
}
Json Instance::to_json() const {
  return Json{{"id", id},
              {"paradigm_kind", detail::en(paradigm_kind)},
              {"paradigm", paradigm},
              {"paradigm_version", paradigm_version},
              {"subject", subject},
              {"subject_label", subject_label},
              {"facets", detail::strs(facets)},
              {"score", score},
              {"coverage", coverage},
              {"model", model},
              {"slots", detail::list(slots)}};
}
Result<Instance> Instance::from_json(const Json& j) {
  Rd r(j, "instance");
  Instance x;
  x.id = r.str("id", true);
  x.paradigm_kind = r.en<ParadigmKind>("paradigm_kind", ParadigmKind::ProjectKind, true);
  x.paradigm = r.str("paradigm", true);
  x.paradigm_version = r.integer("paradigm_version", 1);
  x.subject = r.str("subject", true);
  x.subject_label = r.str("subject_label");
  x.facets = r.strs("facets");
  x.score = r.num("score", 0.0);
  x.coverage = r.object("coverage");
  x.model = r.str("model");
  x.slots = r.list<SlotValue>("slots");
  if (!r.ok()) return r.error();
  return x;
}

}  // namespace loom::model
