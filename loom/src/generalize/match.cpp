// generalize: paradigm matching (§3.5, §3.6) — anchored partial homomorphism
// of project kinds / facets / artifact types into the claim graph via the
// universal roles, with constraint propagation; conflicts are kept; absent
// slots are first-class claims; cross-subject similarity -> analogous_to.
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "internal.h"
#include "kb/stage_profile.h"

namespace loom::generalize {

using detail::Index;
using model::Claim;
using model::EvidenceClass;

namespace {

Json by_generalize() { return Json{{"by", "generalize"}}; }

struct AnchorResult {
  double score = 0.0;
  Json reasons = Json::array();
};

bool kind_hinted(const Index& ix, const model::Entity& e, std::string_view kind) {
  if (const Json* h = json::find(e.attrs, "kind_hint")) {
    if (h->is_string() && h->get<std::string>() == kind) return true;
    if (h->is_array()) {
      for (const auto& x : *h) {
        if (x.is_string() && x.get<std::string>() == kind) return true;
      }
    }
  }
  auto it = ix.by_subject.find(e.id);
  if (it == ix.by_subject.end()) return false;
  for (auto* c : it->second) {
    if (c->predicate != "instance_of" || !detail::usable(*c)) continue;
    if (c->value.is_string() && c->value.get<std::string>() == kind) return true;
    if (auto* o = ix.find_entity(c->object); o && o->canonical_key == kind) return true;
  }
  return false;
}

int count_claims(const Index& ix, std::string_view subject, std::string_view rel) {
  int n = 0;
  auto it = ix.by_subject.find(subject);
  if (it == ix.by_subject.end()) return 0;
  for (auto* c : it->second) n += c->predicate == rel && detail::usable(*c);
  return n;
}

// What anchor evaluation reads besides the evidence index, shared by every
// entity and paradigm of one matching call: the cue classes (prepared once),
// every observation folded and tokenized at most once, and how many
// observations of a unit hit a class (a unit is shared by many entities).
struct AnchorEnv {
  const kb::Pack& pack;
  const kb::Normalizer& norm;
  const Index& ix;
  std::map<std::string, std::optional<detail::PreparedCues>, std::less<>> classes;
  std::unordered_map<const model::Observation*, detail::FoldedText> folded;
  std::map<std::pair<std::string, std::string>, int> unit_hits;
  std::map<std::pair<std::string, std::string>, double> unit_weights;

  AnchorEnv(const kb::Pack& p, const kb::Normalizer& n, const Index& i) : pack(p), norm(n), ix(i) {}

  // nullptr: the pack has no such class.
  const detail::PreparedCues* cues(const std::string& cls) {
    auto it = classes.find(cls);
    if (it == classes.end()) {
      const Json& c = detail::cue_class(pack, cls);
      it = classes.emplace(cls, c.is_null() ? std::optional<detail::PreparedCues>() : std::optional<detail::PreparedCues>(detail::PreparedCues(norm, c)))
               .first;
    }
    return it->second ? &*it->second : nullptr;
  }

  // Observations of `unit` whose text hits the class (score > 0).
  int hits(const std::string& cls, const detail::PreparedCues& cues, const std::string& unit) {
    auto key = std::make_pair(cls, unit);
    if (auto it = unit_hits.find(key); it != unit_hits.end()) return it->second;
    int n = 0;
    if (auto uo = ix.unit_obs.find(unit); uo != ix.unit_obs.end()) {
      for (auto* o : uo->second) {
        auto f = folded.find(o);
        if (f == folded.end()) f = folded.emplace(o, detail::fold_text(norm, o->text)).first;
        n += cues.score(f->second) > 0;
      }
    }
    unit_hits.emplace(std::move(key), n);
    return n;
  }

  // Summed weight of the DISTINCT cue phrases found anywhere in `unit`: one
  // ambiguous word repeated a hundred times is not domain evidence, several
  // different domain words in one document are.
  double distinct_weight(const std::string& cls, const detail::PreparedCues& cues, const std::string& unit) {
    auto key = std::make_pair(cls, unit);
    if (auto it = unit_weights.find(key); it != unit_weights.end()) return it->second;
    std::set<std::size_t> seen;
    if (auto uo = ix.unit_obs.find(unit); uo != ix.unit_obs.end()) {
      for (auto* o : uo->second) {
        auto f = folded.find(o);
        if (f == folded.end()) f = folded.emplace(o, detail::fold_text(norm, o->text)).first;
        cues.hit_phrases(f->second, seen);
      }
    }
    double w = 0;
    for (std::size_t i : seen) w += cues.weight_of(i);
    unit_weights.emplace(std::move(key), w);
    return w;
  }
};

double anchor_op(AnchorEnv& env, const model::Entity& e, const Json& op, Json& reasons) {
  const Index& ix = env.ix;
  std::string name = json::get_string(op, "op");
  double s = 0.0;
  if (name == "claim_count") {
    double min = std::max(1.0, json::get_number(op, "min", 1));
    s = std::min(1.0, count_claims(ix, e.id, json::get_string(op, "rel")) / min);
  } else if (name == "kind_hint") {
    s = kind_hinted(ix, e, json::get_string(op, "value")) ? 1.0 : 0.0;
  } else if (name == "cue") {
    std::string cls = json::get_string(op, "class", json::get_string(op, "value"));
    const detail::PreparedCues* c = env.cues(cls);
    double min = std::max(1.0, json::get_number(op, "min", 1));
    const double min_weight = json::get_number(op, "min_weight", 0);
    int n = 0;
    double best = 0;
    if (c) {
      auto it = ix.subject_units.find(e.id);
      if (it != ix.subject_units.end()) {
        for (const auto& u : it->second) {
          if (min_weight > 0) {
            best = std::max(best, env.distinct_weight(cls, *c, u));
          } else {
            n += env.hits(cls, *c, u);
          }
        }
      }
    }
    // Fewer hits than the anchor asks for is weak evidence, not a match.
    // `min_weight`: the domain evidence is the summed weight of distinct cue
    // phrases in ONE unit the subject appears in (cross-domain words such as
    // "track" or "master" in software text stay below it).
    if (min_weight > 0) {
      s = best >= min_weight ? 1.0 : 0.5 * best / min_weight;
    } else {
      s = n >= min ? 1.0 : 0.5 * n / min;
    }
  } else if (name == "codebase") {
    auto it = ix.by_subject.find(e.id);
    if (it != ix.by_subject.end()) {
      for (auto* c : it->second) {
        if (c->assessment.origin == model::Origin::Repo || c->predicate == "has_file") s = 1.0;
      }
    }
  } else if (name == "has_items") {
    double min = std::max(1.0, json::get_number(op, "min", 1));
    s = std::min(1.0, count_claims(ix, e.id, "has_" + json::get_string(op, "type", "item")) / min);
  } else if (name == "principle_count") {
    double min = std::max(1.0, json::get_number(op, "min", 1));
    int n = 0;
    for (const auto& p : ix.ev.principles) n += p.scope.empty() || std::find(p.scope.areas.begin(), p.scope.areas.end(), e.id) != p.scope.areas.end();
    s = std::min(1.0, n / min);
  }
  if (s > 0) reasons.push_back(Json{{"anchor", name}, {"op", op}, {"score", s}});
  return s;
}

AnchorResult anchors(AnchorEnv& env, const model::Entity& e, const Json& a) {
  AnchorResult r;
  double any = -1, all = -1;
  if (const Json* v = json::find(a, "any"); v && v->is_array()) {
    any = 0;
    for (const auto& op : *v) any = std::max(any, anchor_op(env, e, op, r.reasons));
  }
  if (const Json* v = json::find(a, "all"); v && v->is_array()) {
    all = 1;
    for (const auto& op : *v) all = std::min(all, anchor_op(env, e, op, r.reasons));
  }
  if (any < 0 && all < 0) {
    r.score = 0;
  } else if (any < 0) {
    r.score = all;
  } else if (all < 0) {
    r.score = any;
  } else {
    r.score = std::min(any, all);
  }
  return r;
}

// One candidate value of a slot.
struct Cand {
  std::string key;
  std::vector<const Claim*> claims;  // representative first
  double conf = 0.0;
  std::string date;
};

int rank(const Claim& c) { return model::authority_rank(c.assessment.origin) * 2 + (c.assessment.status == model::ClaimStatus::Active); }

std::vector<Cand> aggregate(const kb::Normalizer& norm, const Index& ix, const std::vector<const Claim*>& claims) {
  std::map<std::string, Cand> by_key;
  for (auto* c : claims) {
    std::string k = detail::value_key(norm, ix, *c);
    auto& cd = by_key[k];
    cd.key = k;
    cd.claims.push_back(c);
  }
  std::vector<Cand> out;
  for (auto& [k, cd] : by_key) {
    // Unit-deduplicated noisy-OR: one conversation counts once.
    std::map<std::string, double> per_unit;
    for (auto* c : cd.claims) {
      auto units = ix.claim_units(*c);
      std::string u = units.empty() ? c->id : units.front();
      double conf = c->assessment.confidence;
      if (c->assessment.status == model::ClaimStatus::Superseded) conf *= 0.5;
      per_unit[u] = std::max(per_unit[u], conf);
    }
    double miss = 1.0;
    for (const auto& [u, c] : per_unit) miss *= 1.0 - detail::clamp01(c);
    cd.conf = 1.0 - miss;
    std::sort(cd.claims.begin(), cd.claims.end(), [](const Claim* a, const Claim* b) {
      if (rank(*a) != rank(*b)) return rank(*a) > rank(*b);
      if (a->assessment.confidence != b->assessment.confidence) return a->assessment.confidence > b->assessment.confidence;
      return a->id < b->id;
    });
    for (auto* c : cd.claims) {
      std::string d = ix.claim_date(*c);
      if (!d.empty() && (cd.date.empty() || d < cd.date)) cd.date = d;
    }
    out.push_back(std::move(cd));
  }
  std::sort(out.begin(), out.end(), [](const Cand& a, const Cand& b) {
    int ra = rank(*a.claims.front()), rb = rank(*b.claims.front());
    if (ra != rb) return ra > rb;
    if (a.conf != b.conf) return a.conf > b.conf;
    if (a.date != b.date) return a.date < b.date;
    return a.key < b.key;
  });
  return out;
}

struct Filler {
  const kb::Pack& pack;
  const kb::Normalizer& norm;
  const Index& ix;
  std::map<std::string, std::vector<const Claim*>, std::less<>> by_object;
  double tau_obs, conflict_ratio, req_mult;

  Filler(const kb::Pack& p, const kb::Normalizer& n, const Index& i)
      : pack(p), norm(n), ix(i),
        tau_obs(detail::threshold(p, "paradigm", "tau_observed", 0.5)),
        conflict_ratio(detail::threshold(p, "paradigm", "conflict_runner_up_ratio", 0.8)),
        req_mult(detail::threshold(p, "paradigm", "required_slot_weight_multiplier", 2.0)) {
    for (const auto& c : ix.ev.claims) {
      if (!c.object.empty()) by_object[c.object].push_back(&c);
    }
  }

  std::vector<const Claim*> candidates(const model::Entity& e, const model::DomainKind& dk,
                                       const std::map<std::string, std::string>& relation_owner) const {
    std::set<const Claim*> out;
    auto accept = [&](const Claim* c, bool literal_ok) {
      if (!detail::usable(*c)) return;
      if (!dk.entity_kind.empty() && !c->object.empty()) {
        auto* o = ix.find_entity(c->object);
        if (o && o->kind != dk.entity_kind) return;
      }
      if (c->object.empty() && !literal_ok) return;
      out.insert(c);
    };
    auto it = ix.by_subject.find(e.id);
    auto rel_owner = relation_owner.find(dk.relation);
    bool literal_ok = rel_owner == relation_owner.end() || rel_owner->second == dk.id;
    if (it != ix.by_subject.end() && !dk.relation.empty()) {
      for (auto* c : it->second) {
        if (c->predicate == dk.relation) accept(c, literal_ok);
      }
    }
    for (const auto& b : dk.bind) {
      std::string kind = json::get_string(b, "kind");
      if (kind == "claim") {
        std::string rel = json::get_string(b, "rel");
        if (json::get_string(b, "dir") == "in") {
          auto bo = by_object.find(e.id);
          if (bo == by_object.end()) continue;
          for (auto* c : bo->second) {
            if (c->predicate == rel) accept(c, true);
          }
        } else if (it != ix.by_subject.end() && rel != dk.relation) {
          for (auto* c : it->second) {
            if (c->predicate == rel) accept(c, literal_ok);
          }
        }
      } else if (kind == "items" && it != ix.by_subject.end()) {
        std::string type = json::get_string(b, "type");
        for (auto* c : it->second) {
          bool is_item = (type == "decision" && c->predicate == "decides") || c->predicate == "has_" + type ||
                         json::get_string(c->qualifiers.extra, "item_type") == type;
          if (is_item) accept(c, true);
        }
      }
      // lexicon / codebase / enumeration bindings produce OBSERVED claims:
      // that is the extract area's job (its claims arrive via the relation).
    }
    std::vector<const Claim*> v(out.begin(), out.end());
    std::sort(v.begin(), v.end(), [](const Claim* a, const Claim* b) { return a->id < b->id; });
    return v;
  }

  // Fills one slot; returns its best confidence (0 when absent).
  double fill(Match& m, const model::Entity& e, const model::DomainKind& dk, const std::string& slot,
              const std::map<std::string, std::string>& relation_owner) {
    auto cands = aggregate(norm, ix, candidates(e, dk, relation_owner));
    std::vector<const Cand*> chosen;
    bool conflict = false;
    if (!cands.empty()) {
      if (dk.card == model::Cardinality::One) {
        chosen.push_back(&cands[0]);
        if (cands.size() > 1 && cands[1].conf >= conflict_ratio * cands[0].conf && cands[0].conf >= tau_obs &&
            cands[1].conf >= tau_obs) {
          chosen.push_back(&cands[1]);
          conflict = true;
        }
      } else {
        for (const auto& c : cands) {
          if (c.conf >= tau_obs || &c == &cands[0]) chosen.push_back(&c);
        }
      }
    }
    int ord = 0;
    for (auto* c : chosen) {
      model::SlotValue sv;
      sv.slot = slot;
      sv.ord = ord++;
      sv.claim = c->claims.front()->id;
      sv.role = dk.role;
      sv.conflict = conflict;
      m.instance.slots.push_back(sv);
      m.claims.push_back(*c->claims.front());
    }
    if (!chosen.empty()) {
      Json r{{"slot", slot}, {"role", model::to_string(dk.role)}, {"values", static_cast<int>(chosen.size())},
             {"confidence", chosen.front()->conf}};
      if (conflict) r["conflict"] = true;
      m.reasons.push_back(r);
      return chosen.front()->conf;
    }
    m.claims.push_back(absent(m, e, dk, slot));
    model::SlotValue sv;
    sv.slot = slot;
    sv.claim = m.claims.back().id;
    sv.role = dk.role;
    m.instance.slots.push_back(sv);
    return 0.0;
  }

  Claim absent(const Match& m, const model::Entity& e, const model::DomainKind& dk, const std::string& slot) const {
    Claim a;
    a.subject = e.id;
    a.predicate = dk.relation.empty() ? "has_" + dk.id : dk.relation;
    a.qualifiers.scope = m.instance.id;
    a.qualifiers.extra = by_generalize();
    a.qualifiers.extra["slot"] = slot;
    a.assessment.evidence = EvidenceClass::Absent;
    a.assessment.origin = model::Origin::System;
    a.assessment.confidence = 0.0;
    a.assessment.open.slots.push_back(m.instance.id + "/" + slot);
    std::string label = dk.labels.count("en") ? dk.labels.at("en") : dk.id;
    a.assessment.open.questions.push_back("Which " + label + " does " + (e.label.empty() ? e.id : e.label) + " have?");
    Json terms = Json::array();
    if (const Json* t = json::find(dk.anchors, "terms"); t && t->is_object()) {
      for (const auto& [lang, v] : t->items()) {
        if (!v.is_array()) continue;
        for (const auto& x : v) terms.push_back(x);
      }
    }
    for (const auto& [lang, l] : dk.labels) terms.push_back(l);
    Json fq{{"subject", e.id}, {"relation", a.predicate}, {"role", model::to_string(dk.role)}, {"terms", terms}};
    if (!dk.entity_kind.empty()) fq["kinds"] = Json::array({dk.entity_kind});
    if (const Json* lx = json::find(dk.anchors, "lexicon")) fq["lexicon"] = *lx;
    a.assessment.open.fill_query = fq;
    return detail::finish(a);
  }
};

std::map<std::string, std::string> relation_owners(const std::vector<model::DomainKind>& dks, std::string_view prefix) {
  std::map<std::string, std::string> out;
  for (const auto& dk : dks) {
    if (!dk.relation.empty()) out.emplace(dk.relation, std::string(prefix) + dk.id);
  }
  return out;
}

void collect_slot_refs(const Json& j, std::set<std::string>& out) {
  if (j.is_string()) {
    const auto& s = j.get_ref<const std::string&>();
    if (s.rfind("$slot:", 0) == 0) out.insert(s.substr(6));
  } else if (j.is_array() || j.is_object()) {
    for (const auto& x : j) collect_slot_refs(x, out);
  }
}

void finish_instance(Match& m, double anchor, double wsum, double wfill, int required, int required_filled) {
  double fill = wsum > 0 ? wfill / wsum : 0.0;
  m.score = detail::clamp01(anchor * (0.5 + 0.5 * fill));
  m.instance.score = m.score;
  Json cov = Json::object();
  for (auto e : model::all<EvidenceClass>()) cov[std::string(model::to_string(e))] = 0;
  std::set<std::string> seen;
  for (const auto& c : m.claims) {
    if (!seen.insert(c.id).second) continue;
    auto k = std::string(model::to_string(c.assessment.evidence));
    cov[k] = cov[k].get<int>() + 1;
  }
  cov["required"] = required;
  cov["required_filled"] = required_filled;
  m.instance.coverage = cov;
}

}  // namespace

ParadigmMatcher::ParadigmMatcher(std::shared_ptr<const kb::Pack> pack) : pack_(std::move(pack)) {}

Result<std::vector<Match>> ParadigmMatcher::match_projects(const Evidence& ev) const {
  if (!pack_) return Error(Errc::InvalidArgument, "ParadigmMatcher: no pack");
  const kb::Pack& pack = *pack_;
  kb::Normalizer norm(pack);
  Index ix(ev);
  Filler filler(pack, norm, ix);
  AnchorEnv env(pack, norm, ix);
  // Facets are parsed from the pack once, not once per entity.
  std::map<std::string, std::optional<model::Facet>, std::less<>> facets;
  auto facet_of = [&](const std::string& id) -> const model::Facet* {
    auto it = facets.find(id);
    if (it == facets.end()) {
      auto f = model::facet(pack, id);
      it = facets.emplace(id, f ? std::optional<model::Facet>(std::move(*f)) : std::nullopt).first;
    }
    return it->second ? &*it->second : nullptr;
  };
  std::vector<Match> out;
  std::optional<prof::Scope> ps(std::in_place, "generalize.match_projects.anchor_fill");
  for (const auto& pk_id : pack.ids("project_kinds")) {
    LOOM_TRY_ASSIGN(auto pk, model::project_kind(pack, pk_id));
    double min_score = json::get_number(pk.anchors, "min_score", 0.4);
    const auto owners = relation_owners(pk.domain_kinds, "");
    for (const auto& e : ev.entities) {
      if (e.kind != pk.subject_kind || e.status == model::ClaimStatus::Rejected) continue;
      auto ar = anchors(env, e, pk.anchors);
      if (ar.score < min_score || ar.score <= 0) continue;
      Match m;
      m.instance.id = model::Instance::make_id(pk.header.id, e.id);
      m.instance.paradigm_kind = model::ParadigmKind::ProjectKind;
      m.instance.paradigm = pk.header.id;
      m.instance.paradigm_version = pk.header.version;
      m.instance.subject = e.id;
      m.instance.subject_label = e.label;
      m.reasons = ar.reasons;
      double wsum = 0, wfill = 0;
      int required = 0, required_filled = 0;
      auto account = [&](const model::DomainKind& dk, double c) {
        double w = dk.weight * (dk.required ? filler.req_mult : 1.0);
        wsum += w;
        wfill += w * c;
        required += dk.required;
        required_filled += dk.required && c > 0;
      };
      for (const auto& dk : pk.domain_kinds) account(dk, filler.fill(m, e, dk, dk.id, owners));
      // Facets: optional sub-templates whose own anchors fire on the subject.
      for (const auto& f_id : pk.facets) {
        const model::Facet* f = facet_of(f_id);
        if (!f) continue;
        auto fr = anchors(env, e, f->anchors);
        if (fr.score < json::get_number(f->anchors, "min_score", 0.4) || fr.score <= 0) continue;
        m.instance.facets.push_back(f_id);
        m.reasons.push_back(Json{{"facet", f_id}, {"anchors", fr.reasons}, {"score", fr.score}});
        auto fowners = relation_owners(f->domain_kinds, f_id + ".");
        for (const auto& dk : f->domain_kinds) account(dk, filler.fill(m, e, dk, f_id + "." + dk.id, fowners));
      }
      finish_instance(m, ar.score, wsum, wfill, required, required_filled);
      out.push_back(std::move(m));
    }
  }
  // Constraint propagation: a violated constraint never deletes a value; it
  // marks the slots it reads as conflicting (or rejects / extends the
  // instance, per its on_violation), with the reason kept.
  ps.reset();
  ps.emplace("generalize.match_projects.constraints");
  prof::count("generalize.match_projects.instances", static_cast<long>(out.size()));
  std::set<std::string> active;  // constraints may use principle_active
  std::vector<Match> kept;
  std::map<std::string, std::optional<model::ProjectKind>, std::less<>> kinds;
  for (auto& m : out) {
    auto kit = kinds.find(m.instance.paradigm);
    if (kit == kinds.end()) {
      auto loaded = model::project_kind(pack, m.instance.paradigm);
      kit = kinds.emplace(m.instance.paradigm, loaded ? std::optional<model::ProjectKind>(std::move(*loaded)) : std::nullopt).first;
    }
    if (!kit->second) continue;
    const model::ProjectKind* pk = &*kit->second;
    bool reject = false;
    std::vector<Json> constraints;
    for (const auto& c : pk->constraints) constraints.push_back(c);
    for (const auto& f_id : m.instance.facets) {
      if (const model::Facet* f = facet_of(f_id)) {
        for (const auto& c : f->constraints) constraints.push_back(c);
      }
    }
    for (const auto& c : constraints) {
      detail::ExprCtx ctx{ix, norm, &pack, &m, &out, m.instance.subject, Json(), "", &active, nullptr};
      auto st = detail::eval_pred(c.contains("expr") ? c["expr"] : Json(), ctx);
      if (st != model::CheckState::Violated) continue;
      std::string action = json::get_string(c, "on_violation", "conflict");
      m.reasons.push_back(Json{{"constraint", json::get_string(c, "id")}, {"violated", true}, {"on_violation", action},
                               {"why", json::get_string(c, "why")}});
      if (action == "reject_instance") reject = true;
      if (action == "conflict") {
        std::set<std::string> slots;
        collect_slot_refs(c, slots);
        for (auto& sv : m.instance.slots) {
          if (slots.count(sv.slot)) sv.conflict = true;
        }
      }
    }
    if (!reject) kept.push_back(std::move(m));
  }
  std::sort(kept.begin(), kept.end(), [](const Match& a, const Match& b) { return a.instance.id < b.instance.id; });
  return kept;
}

Result<std::vector<Match>> ParadigmMatcher::match_artifacts(const Evidence& ev) const {
  if (!pack_) return Error(Errc::InvalidArgument, "ParadigmMatcher: no pack");
  const kb::Pack& pack = *pack_;
  kb::Normalizer norm(pack);
  Index ix(ev);
  // Units recognised as an artifact type (the extract area records the type
  // that parsed each observation).
  std::map<std::string, std::set<std::string>> units_of_type;
  for (const auto& o : ev.observations) {
    if (!o.artifact_type.empty()) units_of_type[o.artifact_type].insert(o.unit);
  }
  // Claims supported from each unit.
  std::map<std::string, std::vector<const Claim*>> unit_claims;
  for (const auto& c : ev.claims) {
    for (const auto& u : ix.claim_units(c)) unit_claims[u].push_back(&c);
  }
  std::vector<Match> out;
  for (const auto& at_id : pack.ids("artifact_types")) {
    auto it = units_of_type.find(at_id);
    if (it == units_of_type.end()) continue;
    LOOM_TRY_ASSIGN(auto at, model::artifact_type(pack, at_id));
    for (const auto& unit : it->second) {
      Match m;
      m.instance.id = model::Instance::make_id(at_id, unit);
      m.instance.paradigm_kind = model::ParadigmKind::ArtifactType;
      m.instance.paradigm = at_id;
      m.instance.paradigm_version = at.header.version;
      m.instance.subject = unit;
      auto uo = ix.unit_obs.find(unit);
      if (uo != ix.unit_obs.end() && !uo->second.empty()) {
        for (auto* o : uo->second) {
          if (o->kind == model::ObservationKind::Heading) {
            m.instance.subject_label = o->text;
            break;
          }
        }
      }
      int required = 0, filled_req = 0, filled = 0;
      const auto& cs = unit_claims[unit];
      for (const auto& spec : at.structure) {
        std::vector<const Claim*> vals;
        std::string ekind = spec.type.rfind("entity:", 0) == 0 ? spec.type.substr(7) : std::string();
        for (auto* c : cs) {
          if (!detail::usable(*c)) continue;
          bool hit = (!spec.relation.empty() && c->predicate == spec.relation) ||
                     ((spec.name == "chosen" || spec.name == "decisions") && c->predicate == "decides") ||
                     (spec.name == "open_questions" && c->predicate == "has_question") ||
                     (spec.name == "areas" && c->predicate == "member_of");
          if (!hit && !ekind.empty() && !c->object.empty()) {
            auto* o = ix.find_entity(c->object);
            hit = o && o->kind == ekind;
          }
          if (hit) vals.push_back(c);
        }
        required += spec.required;
        if (!vals.empty()) {
          ++filled;
          filled_req += spec.required;
          int ord = 0;
          for (auto* c : vals) {
            m.instance.slots.push_back(model::SlotValue{spec.name, ord++, c->id, std::nullopt, false});
            m.claims.push_back(*c);
          }
        } else if (spec.required) {
          Claim a;
          a.subject = unit;
          a.predicate = spec.relation.empty() ? "has_" + spec.name : spec.relation;
          a.qualifiers.scope = m.instance.id;
          a.qualifiers.extra = by_generalize();
          a.qualifiers.extra["slot"] = spec.name;
          a.assessment.evidence = EvidenceClass::Absent;
          a.assessment.origin = model::Origin::System;
          a.assessment.open.slots.push_back(m.instance.id + "/" + spec.name);
          a.assessment.open.questions.push_back("No " + spec.name + " found in this " + at_id);
          a.assessment.open.fill_query = Json{{"unit", unit}, {"slot", spec.name}};
          a = detail::finish(a);
          m.instance.slots.push_back(model::SlotValue{spec.name, 0, a.id, std::nullopt, false});
          m.claims.push_back(std::move(a));
        }
      }
      double fill = at.structure.empty() ? 0.0 : static_cast<double>(filled) / static_cast<double>(at.structure.size());
      finish_instance(m, 1.0, 1.0, fill, required, filled_req);
      m.reasons.push_back(Json{{"detected", at_id}, {"unit", unit}, {"filled_slots", filled}});
      out.push_back(std::move(m));
    }
  }
  std::sort(out.begin(), out.end(), [](const Match& a, const Match& b) { return a.instance.id < b.instance.id; });
  return out;
}

Result<std::vector<Claim>> ParadigmMatcher::analogies(const std::vector<Match>& matches) const {
  if (!pack_) return Error(Errc::InvalidArgument, "ParadigmMatcher: no pack");
  double tau = detail::threshold(*pack_, "paradigm", "analogy_min_similarity", 0.4);
  kb::Normalizer norm(*pack_);
  // Best project instance per subject (competing kinds of one subject are
  // not analogies of each other).
  std::map<std::string, const Match*> best;
  for (const auto& m : matches) {
    if (m.instance.paradigm_kind != model::ParadigmKind::ProjectKind) continue;
    auto& b = best[m.instance.subject];
    if (!b || m.score > b->score || (m.score == b->score && m.instance.id < b->instance.id)) b = &m;
  }
  struct Sig {
    std::set<std::string> roles;
    std::set<std::string> values;  // slot|value key
    std::map<std::string, std::string> role_claim;  // role -> first observed claim id
  };
  auto signature = [&](const Match& m) {
    Sig s;
    std::map<std::string, const Claim*> claims;
    for (const auto& c : m.claims) claims.emplace(c.id, &c);
    for (const auto& sv : m.instance.slots) {
      auto it = claims.find(sv.claim);
      if (it == claims.end() || !detail::usable(*it->second) || !sv.role) continue;
      std::string role(model::to_string(*sv.role));
      s.roles.insert(role);
      const Claim& c = *it->second;
      std::string vk = !c.object.empty() ? c.object : (c.value.is_string() ? norm.phrase_key(c.value.get<std::string>()) : json::canonical(c.value));
      s.values.insert(sv.slot + "|" + vk);
      if (detail::observed_grade(c)) s.role_claim.emplace(role, c.id);
    }
    return s;
  };
  auto jac = [](const std::set<std::string>& a, const std::set<std::string>& b) {
    if (a.empty() && b.empty()) return 0.0;
    std::size_t inter = 0;
    for (const auto& x : a) inter += b.count(x);
    return static_cast<double>(inter) / static_cast<double>(a.size() + b.size() - inter);
  };
  std::vector<std::pair<const Match*, Sig>> sigs;
  for (const auto& [s, m] : best) sigs.emplace_back(m, signature(*m));
  std::vector<Claim> out;
  for (std::size_t i = 0; i < sigs.size(); ++i) {
    for (std::size_t j = i + 1; j < sigs.size(); ++j) {
      const auto& [a, sa] = sigs[i];
      const auto& [b, sb] = sigs[j];
      double role_sim = jac(sa.roles, sb.roles);
      double value_sim = a->instance.paradigm == b->instance.paradigm ? jac(sa.values, sb.values) : 0.0;
      // Same paradigm: shared values count; across paradigms only the role
      // structure can be compared (that is what morphisms transfer over).
      double sim = std::max(0.5 * role_sim + 0.5 * value_sim, 0.8 * role_sim);
      if (sim < tau) continue;
      Claim c;
      c.subject = a->instance.subject;
      c.predicate = "analogous_to";
      c.object = b->instance.subject;
      c.qualifiers.extra = by_generalize();
      Json shared = Json::array();
      for (const auto& r : sa.roles) {
        if (!sb.roles.count(r)) continue;
        shared.push_back(r);
        if (auto x = sa.role_claim.find(r); x != sa.role_claim.end()) c.assessment.premises.claims.push_back(x->second);
        if (auto y = sb.role_claim.find(r); y != sb.role_claim.end()) c.assessment.premises.claims.push_back(y->second);
      }
      c.qualifiers.extra["shared_roles"] = shared;
      std::sort(c.assessment.premises.claims.begin(), c.assessment.premises.claims.end());
      c.assessment.evidence = EvidenceClass::Inferred;
      c.assessment.origin = model::Origin::System;
      c.assessment.confidence = detail::calibrate(*pack_, EvidenceClass::Inferred, sim);
      c.assessment.derivation = model::Derivation{"g.analogy", 1, "", 0};
      kb::ExpectedProperty ep;
      ep.expr = Json{{"op", "not_contradicted_by"}, {"args", Json::array({"analogous_to"})}};
      ep.rationale = "both instances fill the same universal roles (" + std::to_string(shared.size()) +
                     " shared); role-level similarity " + detail::day(std::to_string(role_sim)) +
                     " — an analogy licenses depth-1 transfer only";
      ep.confirm_if = {"the owner relates the two projects", "a transferred slot is later observed on the target"};
      ep.refute_if = {"the owner rejects the analogy", "a transferred expectation is violated"};
      c.assessment.expected = ep;
      c.assessment.check = model::CheckState::Pending;
      c.assessment.alternatives.push_back(model::Alternative{"", Json{{"role_similarity", role_sim}, {"value_similarity", value_sim}}, sim});
      out.push_back(detail::finish(c));
    }
  }
  detail::dedupe(out);
  return out;
}

}  // namespace loom::generalize
