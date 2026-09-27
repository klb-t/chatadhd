// knowledge.resolve and knowledge.assess stages.
//
// resolve: the run's mention entities -> canonical entities (merged ones keep
//   their row: status rejected, attrs.merged_into), claims re-keyed through
//   the remap (old claims kept, superseded), same_as claims, decisions /
//   status records / areas / forks re-pointed; then code lineage for
//   params.snapshots against the git history of config.repo (forked_from /
//   based_on claims + a code_lineage Fork).
// assess: calibrate every claim, detect conflicts (contested), apply explicit
//   decision reversals (the reversed decision becomes superseded), then
//   replay the owner's judgements last (I4).
#include <algorithm>
#include <map>
#include <set>

#include "loom/knowledge.h"
#include "loom/resolve.h"
#include "loom/util/sha256.h"

namespace loom::resolve {

namespace {

constexpr int kAll = 100000000;

std::string hash_of(const std::vector<Json>& parts) {
  Sha256 h;
  for (const auto& p : parts) h.update(json::canonical(p));
  return h.finish_hex();
}

template <class T>
Json ids_json(const std::vector<T>& v) {
  Json a = Json::array();
  for (const auto& x : v) a.push_back(x.id);
  return a;
}

}  // namespace

Result<Json> run_resolve_stage(knowledge::StageContext& ctx) {
  auto& st = ctx.store;
  const std::string& run = ctx.run;
  kb::EntityQuery eq;
  eq.limit = kAll;
  LOOM_TRY_ASSIGN(auto ents, st.query_entities(run, eq));
  kb::ClaimQuery cq;
  cq.limit = kAll;
  LOOM_TRY_ASSIGN(auto claims, st.query_claims(run, cq));
  std::set<std::string> units;
  for (const auto& e : ents) {
    if (const Json* u = json::find(e.attrs, "units"); u && u->is_array()) {
      for (const auto& x : *u) {
        if (x.is_string()) units.insert(x.get<std::string>());
      }
    }
  }
  std::vector<model::Observation> obs;
  for (const auto& u : units) {
    if (ctx.should_stop && ctx.should_stop()) return Error(Errc::Paused, "resolve paused");
    LOOM_TRY_ASSIGN(auto v, st.observations_of_unit(run, u));
    for (auto& o : v) obs.push_back(std::move(o));
  }
  // active entities only (a re-run starts from the mentions again)
  std::vector<model::Entity> mentions;
  for (const auto& e : ents) {
    if (!e.attrs.contains("merged_into")) mentions.push_back(e);
  }
  Resolver resolver(ctx.pack);
  LOOM_TRY_ASSIGN(auto res, resolver.resolve(mentions, obs));
  auto remapped = resolver.apply_remap(claims, res);
  std::map<std::string, std::string> claim_map;  // old claim id -> new claim id
  for (const auto& c : remapped) {
    if (c.assessment.status == model::ClaimStatus::Superseded && !c.assessment.consequences.claims.empty()) {
      claim_map[c.id] = c.assessment.consequences.claims.front();
    }
  }
  auto ent_of = [&](const std::string& id) {
    auto it = res.remap.find(id);
    return it == res.remap.end() ? id : it->second;
  };
  auto claim_of = [&](const std::string& id) {
    auto it = claim_map.find(id);
    return it == claim_map.end() ? id : it->second;
  };
  // entities: canonical + merged rows kept
  std::vector<model::Entity> out_ents = res.entities;
  std::set<std::string> canon;
  for (const auto& e : res.entities) canon.insert(e.id);
  for (const auto& e : mentions) {
    if (canon.count(e.id)) continue;
    model::Entity m = e;
    m.status = model::ClaimStatus::Rejected;
    m.attrs["merged_into"] = ent_of(e.id);
    out_ents.push_back(std::move(m));
  }
  std::vector<model::Claim> out_claims = remapped;
  for (const auto& c : res.same_as) out_claims.push_back(c);
  // decisions, statuses, areas, forks
  LOOM_TRY_ASSIGN(auto decisions, st.list_decisions(run));
  std::vector<model::Decision> out_dec;
  for (auto d : decisions) {
    std::string nid = claim_of(d.id);
    if (nid != d.id) {
      model::Decision old = d;
      old.status = model::DecisionStatus::Superseded;
      old.superseded_by = nid;
      out_dec.push_back(std::move(old));
      d.id = nid;
    }
    d.subject = ent_of(d.subject);
    out_dec.push_back(std::move(d));
  }
  std::set<std::string> status_entities;
  for (const auto& e : ents) status_entities.insert(e.id);
  std::vector<model::StatusRecord> out_status;
  for (const auto& eid : status_entities) {
    LOOM_TRY_ASSIGN(auto recs, st.status_history(run, eid));
    for (auto r : recs) {
      r.entity = ent_of(r.entity);
      r.claim = claim_of(r.claim);
      r.id = model::StatusRecord::make_id(r.entity, r.branch, r.version, r.status, r.date);
      out_status.push_back(std::move(r));
    }
  }
  out_status = model::order_status_history(std::move(out_status));
  LOOM_TRY_ASSIGN(auto areas, st.list_areas(run));
  for (auto& a : areas) {
    a.subject = ent_of(a.subject);
    for (auto& m : a.members) m = claim_of(m);
    std::sort(a.members.begin(), a.members.end());
    a.members.erase(std::unique(a.members.begin(), a.members.end()), a.members.end());
  }
  LOOM_TRY_ASSIGN(auto forks, st.list_forks(run));
  std::vector<model::Fork> out_forks;
  for (auto f : forks) {
    f.subject = ent_of(f.subject);
    for (auto& s : f.sides) s.ref = ent_of(s.ref);
    f.id = model::Fork::make_id(f.kind, f.subject, f.base, f.sides);
    out_forks.push_back(std::move(f));
  }

  // code lineage
  Json lineage = Json::array();
  std::vector<model::Entity> lin_ents;
  const Json* snaps = json::find(ctx.params, "snapshots");
  if (snaps && snaps->is_array() && ctx.config.repo && !ctx.config.repo->empty()) {
    std::string project;
    if (!ctx.config.project.empty()) {
      model::Entity p;
      p.kind = "project";
      p.label = ctx.config.project;
      p.canonical_key = ctx.normalizer.phrase_key(p.label);
      p.id = model::Entity::make_id(p.kind, p.canonical_key);
      p.evidence = model::EvidenceClass::Derived;
      p.origin = model::Origin::Repo;
      p.confidence = 1.0;
      project = ent_of(p.id);
      if (project == p.id && !canon.count(p.id)) lin_ents.push_back(p);
    }
    for (const auto& s : *snaps) {
      std::string dir = s.is_string() ? s.get<std::string>() : json::get_string(s, "dir");
      std::string label = s.is_object() ? json::get_string(s, "label") : "";
      LOOM_TRY_ASSIGN(auto snap, snapshot_revision(dir, label));
      std::vector<std::string> paths;
      for (const auto& [p, c] : snap.files) paths.push_back(p);
      LOOM_TRY_ASSIGN(auto hist, git_revisions(*ctx.config.repo, paths));
      if (hist.empty()) continue;
      LOOM_TRY_ASSIGN(auto lr, code_lineage(snap, hist));
      LOOM_TRY_ASSIGN(auto lc, lineage_claims(lr, project));
      for (auto& c : lc) out_claims.push_back(std::move(c));
      out_forks.push_back(lineage_fork(lr, hist, project));
      model::Entity v;
      v.kind = "version";
      v.label = snap.label;
      std::string nv = kb::normalize_version(snap.label);
      v.canonical_key = nv.empty() ? snap.label : nv;
      v.id = revision_entity(snap.label);
      v.evidence = model::EvidenceClass::Derived;
      v.origin = model::Origin::Repo;
      v.confidence = lr.confidence;
      v.attrs = Json{{"snapshot", dir}, {"base", lr.base}};
      lin_ents.push_back(std::move(v));
      Json lj = lr.to_json();
      lj["votes"] = Json(std::vector<Json>(lj["votes"].begin(), lj["votes"].begin() + std::min<std::ptrdiff_t>(5, static_cast<std::ptrdiff_t>(lj["votes"].size()))));
      lineage.push_back(lj);
    }
  }
  for (auto& e : lin_ents) out_ents.push_back(std::move(e));

  LOOM_TRY(st.put_entities(run, out_ents));
  LOOM_TRY(st.put_claims(run, out_claims));
  LOOM_TRY(st.put_decisions(run, out_dec));
  LOOM_TRY(st.put_status_records(run, out_status));
  LOOM_TRY(st.put_areas(run, areas));
  LOOM_TRY(st.put_forks(run, out_forks));

  int merges = 0, blocked = 0;
  for (const auto& d : res.decisions) {
    merges += d.merged ? 1 : 0;
    blocked += !d.merged && !d.blocked_by.empty() ? 1 : 0;
  }
  std::vector<Json> parts{ids_json(out_ents), ids_json(out_claims), ids_json(out_dec), ids_json(out_status),
                          ids_json(areas), ids_json(out_forks), lineage};
  Json stats{{"entities", res.entities.size()}, {"mentions", mentions.size()}, {"merges", merges},
             {"blocked", blocked},              {"claims", out_claims.size()},  {"lineage", lineage}};
  return Json{{"output", hash_of(parts)}, {"stats", stats}};
}

namespace {

std::set<std::string> keys_of(const kb::Normalizer& norm, std::string_view text) {
  std::set<std::string> out;
  for (const auto& t : norm.tokens(text)) {
    if (norm.is_stopword(t) || t.size() < 3) continue;
    out.insert(norm.match_key(t));
  }
  return out;
}

}  // namespace

Result<Json> run_assess_stage(knowledge::StageContext& ctx) {
  auto& st = ctx.store;
  const std::string& run = ctx.run;
  kb::ClaimQuery cq;
  cq.limit = kAll;
  LOOM_TRY_ASSIGN(auto claims, st.query_claims(run, cq));
  LOOM_TRY(calibrate(*ctx.pack, claims));
  LOOM_TRY_ASSIGN(auto conflicts, detect_conflicts(claims));
  // explicit reversals: a decision flagged as reversing supersedes the
  // closest earlier decision about the same thing (shared content keys)
  LOOM_TRY_ASSIGN(auto decisions, st.list_decisions(run));
  std::map<std::string, model::Claim*> by_id;
  for (auto& c : claims) by_id[c.id] = &c;
  std::sort(decisions.begin(), decisions.end(), [](const model::Decision& a, const model::Decision& b) {
    return std::tie(a.date, a.id) < std::tie(b.date, b.id);
  });
  int reversed = 0;
  for (std::size_t i = 0; i < decisions.size(); ++i) {
    auto it = by_id.find(decisions[i].id);
    if (it == by_id.end() || !json::get_bool(it->second->qualifiers.extra, "reversal")) continue;
    if (decisions[i].status != model::DecisionStatus::Active) continue;
    std::set<std::string> k;
    for (const auto& s : it->second->assessment.support) {
      auto q = keys_of(ctx.normalizer, s.quote);
      k.insert(q.begin(), q.end());
    }
    std::size_t best = SIZE_MAX;
    int best_ov = 0;
    for (std::size_t j = 0; j < i; ++j) {
      if (decisions[j].status != model::DecisionStatus::Active || decisions[j].date >= decisions[i].date) continue;
      std::set<std::string> kj;
      for (const auto& a : decisions[j].alternatives) {
        if (!a.chosen) continue;
        auto q = keys_of(ctx.normalizer, a.label);
        kj.insert(q.begin(), q.end());
      }
      int ov = 0;
      for (const auto& x : kj) ov += k.count(x) ? 1 : 0;
      if (ov > best_ov || (ov == best_ov && ov > 0)) {
        best_ov = ov;
        best = j;
      }
    }
    if (best == SIZE_MAX) continue;
    decisions[best].status = model::DecisionStatus::Superseded;
    decisions[best].superseded_by = decisions[i].id;
    if (auto c = by_id.find(decisions[best].id); c != by_id.end() && c->second->assessment.status == model::ClaimStatus::Active) {
      c->second->assessment.status = model::ClaimStatus::Superseded;
      c->second->assessment.consequences.claims.push_back(decisions[i].id);
    }
    ++reversed;
  }
  LOOM_TRY(st.put_claims(run, claims));
  LOOM_TRY(st.put_decisions(run, decisions));
  LOOM_TRY_ASSIGN(auto rep, st.replay_judgements(run));
  int contested = 0;
  for (const auto& c : claims) contested += c.assessment.status == model::ClaimStatus::Contested ? 1 : 0;
  Json cj = Json::array();
  for (const auto& c : conflicts) cj.push_back(c.to_json());
  Json stats{{"claims", claims.size()}, {"contested", contested}, {"conflicts", conflicts.size()},
             {"reversals", reversed},   {"replayed", rep.applied}};
  std::vector<Json> parts{ids_json(claims), cj, ids_json(decisions)};
  for (const auto& c : claims) parts.push_back(Json{c.assessment.confidence, std::string(model::to_string(c.assessment.status))});
  return Json{{"output", hash_of(parts)}, {"stats", stats}, {"conflicts", cj}};
}

}  // namespace loom::resolve
