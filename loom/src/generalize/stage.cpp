// generalize: competing models (§2.5) and the knowledge.generalize stage.
#include <algorithm>
#include <map>
#include <set>

#include "internal.h"
#include "loom/knowledge.h"

namespace loom::generalize {

using detail::Index;
using model::Claim;

Result<std::vector<model::Model>> build_models(const Evidence& ev, const std::vector<model::Principle>& principles) {
  Index ix(ev);
  // Which decisions / observations a principle explains.
  auto explained = [&](const std::vector<const model::Principle*>& ps, std::vector<std::string>& explains) {
    std::set<std::string> obs, decs;
    for (auto* p : ps) {
      for (const auto& e : p->evidence_for) {
        if (ix.find_obs(e)) obs.insert(e);
        if (ix.decision.count(e)) decs.insert(e);
      }
    }
    for (const auto& d : ev.decisions) {
      for (auto* o : ix.decision_obs(d)) {
        if (obs.count(o->id)) decs.insert(d.id);
      }
    }
    explains.assign(obs.begin(), obs.end());
    return ev.decisions.empty() ? 0.0 : static_cast<double>(decs.size()) / static_cast<double>(ev.decisions.size());
  };
  struct Reading {
    std::string name, description;
    std::vector<const model::Principle*> ps;
  };
  std::vector<Reading> readings;
  Reading seeds{"priors as found", "the owner's seed principles (priors) that the sources support", {}};
  Reading corpus{"corpus only", "principles discovered in the sources, ignoring the seed priors", {}};
  Reading all{"combined", "seed priors found in the sources plus discovered principles", {}};
  for (const auto& p : principles) {
    bool seed = p.id.rfind("p.", 0) == 0;
    (seed ? seeds : corpus).ps.push_back(&p);
    all.ps.push_back(&p);
  }
  readings.push_back(seeds);
  readings.push_back(corpus);
  readings.push_back(all);
  // One reading per side of every recorded principle conflict (I7: kept side
  // by side, never merged).
  std::set<std::pair<std::string, std::string>> seen;
  for (const auto& p : principles) {
    for (const auto& q : p.conflicts_with) {
      auto key = std::minmax(p.id, q);
      if (!seen.insert({key.first, key.second}).second) continue;
      for (const auto& drop : {key.first, key.second}) {
        Reading r{"without " + drop, "reading in which " + drop + " does not hold (conflict " + key.first + " vs " + key.second + ")", {}};
        for (const auto& x : principles) {
          if (x.id != drop) r.ps.push_back(&x);
        }
        readings.push_back(std::move(r));
      }
    }
  }
  std::vector<model::Model> out;
  for (const auto& r : readings) {
    if (r.ps.empty()) continue;
    model::Model m;
    m.name = r.name;
    m.id = model::Model::make_id(m.name);
    m.description = r.description;
    for (auto* p : r.ps) m.principles.push_back(p->id);
    std::sort(m.principles.begin(), m.principles.end());
    m.explanatory = explained(r.ps, m.explains);
    out.push_back(std::move(m));
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  return out;
}

namespace {

Json ids_json(const std::vector<Claim>& v) {
  Json a = Json::array();
  for (const auto& c : v) a.push_back(c.to_json());
  return a;
}

template <class T>
Json all_json(const std::vector<T>& v) {
  Json a = Json::array();
  for (const auto& x : v) a.push_back(x.to_json());
  return a;
}

void attach_all(std::vector<Match>& matches, const std::vector<Claim>& cs, const kb::Pack& pack) {
  for (const auto& c : cs) {
    std::string slot = json::get_string(c.qualifiers.extra, "slot");
    for (auto& m : matches) {
      if (m.instance.id != c.qualifiers.scope) continue;
      model::ProjectKind pk;
      model::Facet fc;
      const model::DomainKind* dk = detail::slot_kind(pack, m, slot, &pk, &fc);
      detail::attach(m, c, slot, dk ? std::optional<model::Role>(dk->role) : std::nullopt);
    }
  }
}

bool stop(knowledge::StageContext& ctx, std::string_view phase) {
  if (ctx.should_stop && ctx.should_stop()) {
    if (ctx.checkpoint) (void)ctx.checkpoint(Json{{"phase", phase}});
    return true;
  }
  return false;
}

}  // namespace

Result<Json> run_stage(knowledge::StageContext& ctx) {
  if (!ctx.pack) return Error(Errc::InvalidArgument, "knowledge.generalize: no pack");
  const kb::Pack& pack = *ctx.pack;
  auto progress = [&](int i, std::string_view msg) {
    if (ctx.progress) ctx.progress(i, 8, msg);
  };
  bool do_predict = json::get_bool(ctx.params, "predict", true);
  LOOM_TRY_ASSIGN(Evidence ev, Evidence::load(ctx.store, ctx.run));
  const std::string cut = ctx.config.prior_cut;
  Evidence base = cut.empty() ? ev : detail::slice(ev, cut, true);

  progress(0, "match");
  ParadigmMatcher matcher(ctx.pack);
  LOOM_TRY_ASSIGN(auto matches, matcher.match_projects(ev));
  LOOM_TRY_ASSIGN(auto artifacts, matcher.match_artifacts(ev));
  for (auto& a : artifacts) matches.push_back(std::move(a));
  LOOM_TRY_ASSIGN(auto analog, matcher.analogies(matches));
  if (stop(ctx, "match")) return Error(Errc::Paused, "knowledge.generalize paused");

  progress(1, "infer");
  LOOM_TRY_ASSIGN(auto inferred, infer(pack, ev, matches));
  progress(2, "transfer");
  LOOM_TRY_ASSIGN(auto transferred, transfer(pack, matches));
  attach_all(matches, transferred, pack);
  progress(3, "extrapolate");
  LOOM_TRY_ASSIGN(auto extrapolated, extrapolate(pack, ev, matches));
  attach_all(matches, extrapolated, pack);
  if (stop(ctx, "infer")) return Error(Errc::Paused, "knowledge.generalize paused");

  progress(4, "principles");
  LOOM_TRY_ASSIGN(auto report, discover_principles(pack, base, ctx.priors));
  Evidence with_principles = base;
  with_principles.principles = report.principles;
  progress(5, "operators");
  LOOM_TRY_ASSIGN(auto ops, mine_operators(pack, with_principles, ctx.priors));
  progress(6, "models");
  LOOM_TRY_ASSIGN(auto models, build_models(base, report.principles));
  progress(7, "predictions");
  std::vector<model::Prediction> preds;
  if (do_predict) {
    LOOM_TRY_ASSIGN(preds, predict(ops, base, cut));
    if (!cut.empty()) {
      LOOM_TRY_ASSIGN(preds, evaluate_predictions(std::move(preds), detail::slice(ev, cut, false)));
    }
  }
  // Predictive power of each reading: share of evaluated predictions that
  // held among those justified by (or not contradicting) its principles.
  for (auto& m : models) {
    int n = 0, held = 0;
    std::set<std::string> ps(m.principles.begin(), m.principles.end());
    for (const auto& p : preds) {
      if (p.outcome != model::CheckState::Holds && p.outcome != model::CheckState::Violated) continue;
      bool covered = p.principles.empty();
      for (const auto& x : p.principles) covered |= ps.count(x) > 0;
      if (!covered) continue;
      ++n;
      held += p.outcome == model::CheckState::Holds;
    }
    m.predictive = n ? static_cast<double>(held) / n : 0.0;
  }
  if (!models.empty()) {
    auto best = std::max_element(models.begin(), models.end(), [](const auto& a, const auto& b) {
      return a.explanatory + a.predictive < b.explanatory + b.predictive;
    });
    if (best->explanatory > 0) best->validation = model::ValidationStatus::Supported;
  }

  // Areas: inferred members.
  std::vector<model::Area> areas = ev.areas;
  for (auto& a : areas) {
    for (const auto& c : inferred) {
      if (c.predicate == "member_of" && json::get_string(c.value, "area") == a.id) a.inferred_members.push_back(c.id);
    }
    std::sort(a.inferred_members.begin(), a.inferred_members.end());
    a.inferred_members.erase(std::unique(a.inferred_members.begin(), a.inferred_members.end()), a.inferred_members.end());
  }

  // Everything this stage produced (existing evidence claims are not rewritten).
  std::vector<Claim> claims;
  std::vector<model::Instance> instances;
  std::set<std::string> known;
  for (const auto& c : ev.claims) known.insert(c.id);
  for (const auto& m : matches) {
    instances.push_back(m.instance);
    for (const auto& c : m.claims) {
      if (!known.count(c.id)) claims.push_back(c);
    }
  }
  for (auto* v : {&analog, &inferred, &transferred, &extrapolated}) {
    for (const auto& c : *v) claims.push_back(c);
  }
  detail::dedupe(claims);
  std::sort(instances.begin(), instances.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  std::vector<model::Morphism> morphs;
  if (auto t = model::morphisms(pack)) morphs = *t;
  if (auto a = model::anchoring_morphisms(pack)) {
    for (auto& m : *a) morphs.push_back(std::move(m));
  }
  std::sort(morphs.begin(), morphs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

  LOOM_TRY(ctx.store.put_claims(ctx.run, claims));
  LOOM_TRY(ctx.store.put_instances(ctx.run, instances));
  LOOM_TRY(ctx.store.put_principles(ctx.run, report.principles));
  LOOM_TRY(ctx.store.put_operators(ctx.run, ops));
  LOOM_TRY(ctx.store.put_morphisms(ctx.run, morphs));
  LOOM_TRY(ctx.store.put_models(ctx.run, models));
  LOOM_TRY(ctx.store.put_predictions(ctx.run, preds));
  LOOM_TRY(ctx.store.put_areas(ctx.run, areas));
  progress(8, "done");

  Json written{{"claims", ids_json(claims)},  {"instances", all_json(instances)}, {"principles", all_json(report.principles)},
               {"operators", all_json(ops)},  {"models", all_json(models)},       {"predictions", all_json(preds)},
               {"areas", all_json(areas)},    {"version", kGeneralizerVersion}};
  int holds = 0, violated = 0;
  for (const auto& p : preds) {
    holds += p.outcome == model::CheckState::Holds;
    violated += p.outcome == model::CheckState::Violated;
  }
  int absent = 0;
  for (const auto& c : claims) absent += c.is_absent();
  Json stats{{"instances", instances.size()},
             {"absent", absent},
             {"analogies", analog.size()},
             {"inferred", inferred.size()},
             {"extrapolated", extrapolated.size()},
             {"transferred", transferred.size()},
             {"principles", report.principles.size()},
             {"operators", ops.size()},
             {"models", models.size()},
             {"predictions", preds.size()},
             {"predictions_holds", holds},
             {"predictions_violated", violated}};
  return Json{{"output", detail::hash_json(written)}, {"stats", stats}, {"seed_status", report.seed_status}, {"cut", cut}};
}

}  // namespace loom::generalize
