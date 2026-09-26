// generalize: stratified rule execution (§2.4, §3.3) — stratum 0 derived,
// 1 inferred (with Expected Property), 2 extrapolated (capped, never
// premises); area generalizations (§3.7); cross-domain transfer through
// morphisms (§3.6, depth 1); re-checking Expected Properties.
#include <algorithm>
#include <map>
#include <set>

#include "internal.h"

namespace loom::generalize {

using detail::ExprCtx;
using detail::Index;
using model::CheckState;
using model::Claim;
using model::EvidenceClass;

namespace {

int premise_depth(const Index& ix, const std::vector<Match>& matches, const std::string& id) {
  const Claim* c = ix.find_claim(id);
  if (!c) {
    for (const auto& m : matches) {
      for (const auto& x : m.claims) {
        if (x.id == id) c = &x;
      }
    }
  }
  if (!c || c->assessment.evidence != EvidenceClass::Inferred || !c->assessment.derivation) return -1;
  return c->assessment.derivation->depth;
}

const Claim* find_any(const Index& ix, const std::vector<Match>& matches, const std::string& id) {
  if (auto* c = ix.find_claim(id)) return c;
  for (const auto& m : matches) {
    for (const auto& x : m.claims) {
      if (x.id == id) return &x;
    }
  }
  return nullptr;
}

}  // namespace

// Puts a produced claim into its instance slot, replacing an absent value.
void detail::attach(Match& m, const Claim& c, const std::string& slot, std::optional<model::Role> role) {
  bool extrap = c.assessment.evidence == EvidenceClass::Extrapolated;
  std::set<std::string> absent_ids;
  for (const auto& x : m.claims) {
    if (x.is_absent()) absent_ids.insert(x.id);
  }
  int ord = 0;
  auto& sl = m.instance.slots;
  if (!extrap) {
    sl.erase(std::remove_if(sl.begin(), sl.end(), [&](const model::SlotValue& sv) { return sv.slot == slot && absent_ids.count(sv.claim); }),
             sl.end());
    m.claims.erase(std::remove_if(m.claims.begin(), m.claims.end(),
                                  [&](const Claim& x) { return x.is_absent() && json::get_string(x.qualifiers.extra, "slot") == slot; }),
                   m.claims.end());
  }
  for (const auto& sv : sl) {
    if (sv.slot == slot) ord = std::max(ord, sv.ord + 1);
  }
  for (const auto& sv : sl) {
    if (sv.claim == c.id) return;
  }
  sl.push_back(model::SlotValue{slot, ord, c.id, role, false});
  m.claims.push_back(c);
}

namespace {

struct RuleRun {
  const kb::Pack& pack;
  const kb::Normalizer& norm;
  const Index& ix;
  std::vector<Match>& matches;
  std::set<std::string> active;
  Json skipped = Json::array();
  int rejected_by_property = 0;
  double cap;

  RuleRun(const kb::Pack& p, const kb::Normalizer& n, const Index& i, std::vector<Match>& m, const Evidence& ev)
      : pack(p), norm(n), ix(i), matches(m), cap(detail::threshold(p, "paradigm", "extrapolation_cap", 0.5)) {
    active = detail::active_principles(p, ev, model::PriorFilter{});
  }

  struct Target {
    Match* m;
    std::string slot;
    std::string predicate;
    std::optional<model::Role> role;
    std::string entity_kind;
  };

  std::vector<Target> targets(const model::Operator& r) {
    std::vector<Target> out;
    const Json& t = r.target;
    if (json::find(t, "field")) return out;  // record fields: owned by the areas that build version records
    std::string temporal = json::get_string(t, "temporal");
    if (!temporal.empty()) {
      if (temporal != "current_version" && temporal != "status") return out;
      std::map<std::string, Match*> best;
      for (auto& m : matches) {
        if (m.instance.paradigm_kind != model::ParadigmKind::ProjectKind) continue;
        auto& b = best[m.instance.subject];
        if (!b || m.score > b->score) b = &m;
      }
      for (auto& [s, m] : best) {
        out.push_back(Target{m, temporal, temporal == "status" ? "has_status" : "has_version", std::nullopt, ""});
      }
      return out;
    }
    std::string par = json::get_string(t, "paradigm");
    std::string slot = json::get_string(t, "slot");
    if (par.empty() || slot.empty()) return out;
    for (auto& m : matches) {
      std::string sname;
      if (m.instance.paradigm == par) {
        sname = slot;
      } else if (std::find(m.instance.facets.begin(), m.instance.facets.end(), par) != m.instance.facets.end()) {
        sname = par + "." + slot;
      } else {
        continue;
      }
      model::ProjectKind pk;
      model::Facet fc;
      const model::DomainKind* dk = detail::slot_kind(pack, m, sname, &pk, &fc);
      if (dk) {
        out.push_back(Target{&m, sname, dk->relation.empty() ? "has_" + dk->id : dk->relation, dk->role, dk->entity_kind});
      } else {
        out.push_back(Target{&m, sname, "has_" + slot, std::nullopt, ""});
      }
    }
    return out;
  }

  // Runs one rule on every target; returns the claims produced.
  std::vector<Claim> run(const model::Operator& r) {
    std::vector<Claim> out;
    for (const auto& p : r.principles) {
      if (!active.count(p)) {
        skipped.push_back(Json{{"rule", r.id}, {"reason", "basis principle " + p + " not active (prior filter)"}});
        return out;
      }
    }
    for (auto& tg : targets(r)) {
      std::vector<std::string> used;
      ExprCtx ctx{ix, norm, &pack, tg.m, &matches, tg.m->instance.subject, Json(), "", &active, &used};
      if (!detail::eval_cond(r.when, ctx)) continue;
      auto vr = detail::eval_value(r.value, ctx);
      if (!vr.unsupported.empty()) {
        skipped.push_back(Json{{"rule", r.id}, {"instance", tg.m->instance.id}, {"reason", vr.unsupported}});
        continue;
      }
      if (vr.values.empty()) continue;
      EvidenceClass produces = *r.produces;
      Json values = vr.values;
      if (tg.slot == "current_version" || tg.slot == "status") values = Json::array({values[0]});
      for (const auto& v : values) {
        Claim c;
        c.subject = tg.m->instance.subject;
        c.predicate = tg.predicate;
        c.value = v;
        c.qualifiers.scope = tg.m->instance.id;
        c.qualifiers.extra = Json{{"by", "generalize"}, {"slot", tg.slot}};
        auto& a = c.assessment;
        a.evidence = produces;
        a.origin = model::Origin::System;
        double conf = r.confidence;
        if (produces == EvidenceClass::Extrapolated) conf = std::min(conf, cap);
        a.confidence = detail::calibrate(pack, produces, conf);
        if (produces == EvidenceClass::Extrapolated) a.confidence = std::min(a.confidence, cap);
        int depth = 0;
        std::set<std::string> prem;
        for (const auto& id : used) {
          const Claim* p = find_any(ix, matches, id);
          if (!p || !model::may_be_premise(p->assessment.evidence) || p->assessment.status == model::ClaimStatus::Rejected) continue;
          prem.insert(id);
          depth = std::max(depth, premise_depth(ix, matches, id) + 1);
        }
        int max_depth = static_cast<int>(detail::threshold(pack, "paradigm", "analogy_max_depth", 1));
        if (produces == EvidenceClass::Inferred && depth > max_depth) {
          skipped.push_back(Json{{"rule", r.id}, {"instance", tg.m->instance.id}, {"reason", "inference depth limit"}});
          continue;
        }
        a.premises.claims.assign(prem.begin(), prem.end());
        a.premises.principles = r.principles;
        a.derivation = model::Derivation{r.id, r.version, "", depth};
        a.alternatives = vr.alternatives;
        if (r.expected) {
          kb::ExpectedProperty ep = *r.expected;
          ExprCtx bctx{ix, norm, &pack, tg.m, &matches, tg.m->instance.subject, v, "", &active, nullptr};
          ep.expr = detail::bind(ep.expr, bctx);
          a.expected = ep;
          auto st = detail::eval_pred(ep.expr, bctx);
          if (st == CheckState::Violated && produces != EvidenceClass::Derived) {
            ++rejected_by_property;
            skipped.push_back(Json{{"rule", r.id}, {"instance", tg.m->instance.id}, {"value", v},
                                   {"reason", "expected property violated at inference time"}});
            continue;
          }
          a.check = st == CheckState::NotApplicable ? CheckState::Pending : st;
        }
        c = detail::finish(c);
        detail::attach(*tg.m, c, tg.slot, tg.role);
        out.push_back(std::move(c));
      }
    }
    return out;
  }

  std::vector<Claim> stratum(int s) {
    std::vector<Claim> out;
    auto rules = model::pack_operators(pack);
    if (!rules) return out;
    std::vector<model::Operator> rs;
    for (auto& r : *rules) {
      if (r.is_rule() && r.stratum == s) rs.push_back(r);
    }
    std::sort(rs.begin(), rs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    // Fixpoint: rules are monotone (they fill empty slots), so a second pass
    // only adds what the first enabled; bounded by the number of rules.
    std::set<std::string> seen;
    for (std::size_t pass = 0; pass <= rs.size(); ++pass) {
      std::size_t before = seen.size();
      for (const auto& r : rs) {
        for (auto& c : run(r)) {
          if (seen.insert(c.id).second) out.push_back(std::move(c));
        }
      }
      if (seen.size() == before) break;
    }
    return out;
  }
};

// §3.7: an area's generating statement infers membership of unlisted items
// of its kinds/roles under its subject.
std::vector<Claim> area_members(const kb::Pack& pack, const Index& ix, std::vector<Match>& matches) {
  std::vector<Claim> out;
  for (const auto& area : ix.ev.areas) {
    if (area.principle.empty() || (area.kinds.empty() && area.roles.empty())) continue;
    std::set<std::string> members(area.members.begin(), area.members.end());
    auto* ao = ix.find_obs(area.observation);
    std::string since = ao ? detail::day(ao->date) : std::string();
    for (auto& m : matches) {
      if (m.instance.subject != area.subject || m.instance.paradigm_kind != model::ParadigmKind::ProjectKind) continue;
      for (const auto& sv : m.instance.slots) {
        bool in_kind = std::find(area.kinds.begin(), area.kinds.end(), sv.slot) != area.kinds.end();
        bool in_role = sv.role && std::find(area.roles.begin(), area.roles.end(), *sv.role) != area.roles.end();
        if (!in_kind && !in_role) continue;
        if (members.count(sv.claim)) continue;
        const Claim* c = find_any(ix, matches, sv.claim);
        if (!c || !detail::observed_grade(*c)) continue;
        if (!since.empty() && ix.claim_date(*c) < since) continue;
        Claim x;
        x.subject = area.subject;
        x.predicate = "member_of";
        x.value = Json{{"area", area.id}, {"claim", c->id}};
        x.qualifiers.scope = area.id;
        x.qualifiers.extra = Json{{"by", "generalize"}};
        auto& a = x.assessment;
        a.evidence = EvidenceClass::Inferred;
        a.origin = model::Origin::System;
        a.confidence = detail::calibrate(pack, EvidenceClass::Inferred, 0.6);
        a.premises.claims = {c->id};
        a.premises.principles = {area.principle};
        a.derivation = model::Derivation{"g.area_generalization", 1, "", 0};
        kb::ExpectedProperty ep;
        ep.expr = Json{{"op", "consistent_with"}, {"args", Json::array({area.principle})}};
        ep.rationale = "the area's generating statement ('" + area.statement + "') covers every member of its kinds, listed or not";
        ep.confirm_if = {"the owner lists the item under the area"};
        ep.refute_if = {"the owner excludes the item from the area", "the item violates the area principle"};
        a.expected = ep;
        a.check = CheckState::Pending;
        out.push_back(detail::finish(x));
      }
    }
  }
  return out;
}

}  // namespace

Result<std::vector<Claim>> infer(const kb::Pack& pack, const Evidence& ev, std::vector<Match>& matches) {
  kb::Normalizer norm(pack);
  Index ix(ev);
  RuleRun rr(pack, norm, ix, matches, ev);
  std::vector<Claim> out = rr.stratum(0);
  for (auto& c : rr.stratum(1)) out.push_back(std::move(c));
  for (auto& c : area_members(pack, ix, matches)) out.push_back(std::move(c));
  detail::dedupe(out);
  return out;
}

Result<std::vector<Claim>> extrapolate(const kb::Pack& pack, const Evidence& ev, const std::vector<Match>& matches) {
  kb::Normalizer norm(pack);
  Index ix(ev);
  std::vector<Match> work = matches;  // extrapolations never feed back (never premises)
  RuleRun rr(pack, norm, ix, work, ev);
  std::vector<Claim> out = rr.stratum(2);
  detail::dedupe(out);
  return out;
}

Result<std::vector<Claim>> transfer(const kb::Pack& pack, const std::vector<Match>& matches) {
  auto ms = model::morphisms(pack);
  if (!ms) return ms.error();
  kb::Normalizer norm(pack);
  Evidence no_evidence;
  Index ix(no_evidence);
  std::set<std::string> active;
  // Best project instance per subject.
  std::map<std::string, const Match*> best;
  for (const auto& m : matches) {
    if (m.instance.paradigm_kind != model::ParadigmKind::ProjectKind) continue;
    auto& b = best[m.instance.subject];
    if (!b || m.score > b->score) b = &m;
  }
  auto filled_roles = [](const Match& m) {
    std::set<model::Role> r;
    std::map<std::string, const Claim*> cs;
    for (const auto& c : m.claims) cs.emplace(c.id, &c);
    for (const auto& sv : m.instance.slots) {
      auto it = cs.find(sv.claim);
      if (sv.role && it != cs.end() && detail::observed_grade(*it->second)) r.insert(*sv.role);
    }
    return r;
  };
  auto slot_of = [](const Match& m, const model::MorphismEnd& e) -> std::string {
    if (m.instance.paradigm == e.paradigm) return e.kind;
    if (std::find(m.instance.facets.begin(), m.instance.facets.end(), e.paradigm) != m.instance.facets.end()) return e.paradigm + "." + e.kind;
    return "";
  };
  auto claims_in = [](const Match& m, const std::string& slot) {
    std::vector<const Claim*> out;
    std::map<std::string, const Claim*> cs;
    for (const auto& c : m.claims) cs.emplace(c.id, &c);
    for (const auto& sv : m.instance.slots) {
      auto it = cs.find(sv.claim);
      if (sv.slot == slot && it != cs.end()) out.push_back(it->second);
    }
    return out;
  };
  std::vector<Claim> out;
  for (const auto& mo : *ms) {
    if (mo.use != model::MorphismUse::Transfer || mo.validation == model::ValidationStatus::Rejected) continue;
    std::vector<std::pair<const model::MorphismEnd*, const model::MorphismEnd*>> dirs{{&mo.from, &mo.to}};
    if (mo.bidirectional) dirs.emplace_back(&mo.to, &mo.from);
    for (const auto& [src, dst] : dirs) {
      for (const auto& [sa, a] : best) {
        std::string sslot = slot_of(*a, *src);
        if (sslot.empty()) continue;
        std::vector<const Claim*> source;
        for (auto* c : claims_in(*a, sslot)) {
          if (detail::observed_grade(*c) && !detail::transferred(*c)) source.push_back(c);
        }
        if (source.empty()) continue;
        auto ra = filled_roles(*a);
        for (const auto& [sb, b] : best) {
          if (sb == sa) continue;
          std::string tslot = slot_of(*b, *dst);
          if (tslot.empty()) continue;
          bool empty = true;
          for (auto* c : claims_in(*b, tslot)) empty &= !detail::usable(*c);
          if (!empty) continue;
          // The two projects must be analogous at role level (≥ 2 shared
          // filled roles besides the transferred one).
          auto rb = filled_roles(*b);
          int shared = 0;
          for (auto r : ra) shared += rb.count(r) > 0;
          if (shared < 2) continue;
          ExprCtx ctx{ix, norm, &pack, b, &matches, b->instance.subject, Json(), "", &active, nullptr};
          bool ok = true;
          for (const auto& cond : mo.conditions) ok &= detail::eval_cond(cond, ctx);
          if (!ok) continue;
          model::ProjectKind pk;
          model::Facet fc;
          const model::DomainKind* dk = detail::slot_kind(pack, *b, tslot, &pk, &fc);
          if (!dk) continue;
          Claim c;
          c.subject = b->instance.subject;
          c.predicate = dk->relation.empty() ? "has_" + dk->id : dk->relation;
          std::string label = dk->labels.count("en") ? dk->labels.at("en") : dk->id;
          if (mo.mode == model::TransferMode::Value) {
            c.value = detail::value_text(ix, *source.front());
          } else {
            c.value = Json{{"presence", dk->id}, {"label", label}, {"analog", a->instance.subject_label.empty() ? a->instance.subject : a->instance.subject_label}};
          }
          c.qualifiers.scope = b->instance.id;
          c.qualifiers.extra = Json{{"by", "generalize"}, {"slot", tslot}, {"from", a->instance.id}};
          auto& as = c.assessment;
          as.evidence = EvidenceClass::Inferred;
          as.origin = mo.origin;
          double src_conf = 0;
          for (auto* s : source) {
            src_conf = std::max(src_conf, s->assessment.confidence);
            as.premises.claims.push_back(s->id);
          }
          std::sort(as.premises.claims.begin(), as.premises.claims.end());
          double conf = std::min(mo.confidence, src_conf);
          // model_knowledge never outranks the owner's sources: a transfer
          // stays below the inference threshold unless the morphism was
          // confirmed by the owner.
          if (mo.origin == model::Origin::ModelKnowledge && mo.validation != model::ValidationStatus::Confirmed) {
            conf = std::min(conf, detail::threshold(pack, "paradigm", "tau_inferred", 0.55) - 0.05);
          }
          as.confidence = detail::calibrate(pack, EvidenceClass::Inferred, conf);
          as.derivation = model::Derivation{mo.id, 1, mo.id, 1};
          kb::ExpectedProperty ep;
          if (mo.expected) {
            ep = *mo.expected;
          } else {
            ep.expr = Json{{"op", "nonempty"}, {"args", Json::array({"$slot:" + dk->id})}};
            ep.rationale = mo.rationale;
          }
          ExprCtx bctx{ix, norm, &pack, b, &matches, b->instance.subject, c.value, "", &active, nullptr};
          ep.expr = detail::bind(ep.expr, bctx);
          as.expected = ep;
          as.check = CheckState::Pending;
          out.push_back(detail::finish(c));
        }
      }
    }
  }
  detail::dedupe(out);
  return out;
}

Result<CheckState> evaluate_property(const kb::ExpectedProperty& ep, const Claim& claim, const Evidence& ev) {
  auto pack = detail::builtin_pack();
  if (!pack) return Error(Errc::Internal, "evaluate_property: built-in pack unavailable");
  kb::Normalizer norm(*pack);
  Index ix(ev);
  Json value;
  if (!claim.object.empty()) {
    value = detail::value_text(ix, claim);
  } else {
    value = claim.value;
  }
  ExprCtx ctx{ix, norm, nullptr, nullptr, nullptr, claim.subject, value, claim.id, nullptr, nullptr};
  return detail::eval_pred(ep.expr, ctx);
}

}  // namespace loom::generalize
