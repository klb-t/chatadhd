// generalize: Evidence::load and the JSON views of the area's result types.
#include <algorithm>
#include <set>

#include "internal.h"

namespace loom::generalize {

namespace {
template <class T>
void sort_by_id(std::vector<T>& v) {
  std::sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.id < b.id; });
  v.erase(std::unique(v.begin(), v.end(), [](const T& a, const T& b) { return a.id == b.id; }), v.end());
}
}  // namespace

Result<Evidence> Evidence::load(kb::KnowledgeStore& store, std::string_view run) {
  constexpr int kAll = 1 << 30;
  Evidence ev;
  kb::EntityQuery eq;
  eq.limit = kAll;
  LOOM_TRY_ASSIGN(ev.entities, store.query_entities(run, eq));
  kb::ClaimQuery cq;
  cq.limit = kAll;
  LOOM_TRY_ASSIGN(ev.claims, store.query_claims(run, cq));
  LOOM_TRY_ASSIGN(ev.decisions, store.list_decisions(run));
  LOOM_TRY_ASSIGN(ev.areas, store.list_areas(run));
  LOOM_TRY_ASSIGN(ev.principles, store.list_principles(run));

  // Observations: everything the claims, areas and principles point to, plus
  // the whole unit each of them lives in (the situation of a decision is the
  // conversation around it).
  std::set<std::string> ids;
  for (const auto& c : ev.claims) {
    for (const auto& s : c.assessment.support) ids.insert(s.observation);
  }
  for (const auto& a : ev.areas) {
    if (!a.observation.empty()) ids.insert(a.observation);
  }
  for (const auto& p : ev.principles) {
    for (const auto& e : p.evidence_for) {
      if (e.rfind("ob_", 0) == 0) ids.insert(e);
    }
  }
  std::set<std::string> units;
  for (const auto& id : ids) {
    LOOM_TRY_ASSIGN(auto o, store.get_observation(run, id));
    if (!o) continue;
    units.insert(o->unit);
    ev.observations.push_back(std::move(*o));
  }
  for (const auto& u : units) {
    LOOM_TRY_ASSIGN(auto v, store.observations_of_unit(run, u));
    for (auto& o : v) ev.observations.push_back(std::move(o));
  }
  sort_by_id(ev.entities);
  sort_by_id(ev.claims);
  sort_by_id(ev.observations);
  sort_by_id(ev.decisions);
  sort_by_id(ev.areas);
  sort_by_id(ev.principles);
  return ev;
}

Json Match::to_json() const {
  Json cs = Json::array();
  for (const auto& c : claims) cs.push_back(c.to_json());
  return Json{{"instance", instance.to_json()}, {"claims", cs}, {"score", score}, {"reasons", reasons}};
}

Json PrincipleReport::to_json() const {
  Json ps = Json::array();
  for (const auto& p : principles) ps.push_back(p.to_json());
  return Json{{"principles", ps}, {"seed_status", seed_status}};
}

}  // namespace loom::generalize
