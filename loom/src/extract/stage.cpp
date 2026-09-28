// knowledge.extract stage: units -> Extraction -> KnowledgeStore.
//
// Units come from the catalog (selected units, read by locator) when it can
// hand them over; otherwise the stage reads config.sources itself
// (read_units). Two passes: the first collects names that units introduce
// (appositives, project enumerations, Claude project names, branch names),
// the second extracts every unit with those names as lexicon entries, so a
// project named once is recognised everywhere (still observed: every mention
// is a located observation). Deterministic; ids content-derived.
#include <algorithm>
#include <map>
#include <set>

#include "extract/extract_internal.h"
#include "loom/catalog.h"
#include "loom/extract.h"
#include "loom/knowledge.h"
#include "loom/knowledge_semantic.h"
#include "loom/util/sha256.h"

namespace loom::extract {

namespace {

void merge_entity(model::Entity& into, const model::Entity& e) {
  for (const auto& a : e.aliases) {
    bool found = false;
    for (auto& x : into.aliases) {
      if (x.key == a.key) {
        x.count += a.count;
        x.confidence = std::max(x.confidence, a.confidence);
        found = true;
      }
    }
    if (!found) into.aliases.push_back(a);
  }
  std::sort(into.aliases.begin(), into.aliases.end(), [](const model::Alias& a, const model::Alias& b) { return a.key < b.key; });
  if (!e.first_seen.empty() && (into.first_seen.empty() || e.first_seen < into.first_seen)) into.first_seen = e.first_seen;
  if (e.last_seen > into.last_seen) into.last_seen = e.last_seen;
  into.confidence = std::max(into.confidence, e.confidence);
  for (const char* key : {"units", "observations"}) {
    std::set<std::string> s;
    for (const Json* src : {json::find(into.attrs, key), json::find(e.attrs, key)}) {
      if (src && src->is_array()) {
        for (const auto& x : *src) {
          if (x.is_string()) s.insert(x.get<std::string>());
        }
      }
    }
    Json a = Json::array();
    std::size_t cap = std::string(key) == "observations" ? 256 : 100000;
    for (const auto& x : s) {
      if (a.size() >= cap) break;
      a.push_back(x);
    }
    into.attrs[key] = a;
  }
}

void merge_claim(model::Claim& into, const model::Claim& c) {
  for (const auto& s : c.assessment.support) {
    bool dup = false;
    for (const auto& x : into.assessment.support) dup = dup || (x.observation == s.observation && x.extractor == s.extractor);
    if (!dup) into.assessment.support.push_back(s);
  }
  std::sort(into.assessment.support.begin(), into.assessment.support.end(), [](const model::Support& a, const model::Support& b) {
    return std::tie(a.observation, a.extractor) < std::tie(b.observation, b.extractor);
  });
  into.assessment.confidence = std::max(into.assessment.confidence, c.assessment.confidence);
}

}  // namespace

// Accumulates extractions of many units into one deterministic set.
struct Accumulator {
  std::map<std::string, model::Observation> observations;
  std::map<std::string, model::Entity> entities;
  std::map<std::string, model::Claim> claims;
  std::map<std::string, model::Area> areas;
  std::map<std::string, model::Principle> principles;
  std::map<std::string, model::Decision> decisions;
  std::map<std::string, model::Fork> forks;
  std::map<std::string, model::StatusRecord> statuses;
  std::vector<ClassifiedItem> items;
  Json units = Json::array();

  void add(Extraction&& ex) {
    for (auto& o : ex.observations) observations.emplace(o.id, std::move(o));
    for (auto& e : ex.entities) {
      auto [it, fresh] = entities.try_emplace(e.id, e);
      if (!fresh) merge_entity(it->second, e);
    }
    for (auto& c : ex.claims) {
      auto [it, fresh] = claims.try_emplace(c.id, c);
      if (!fresh) merge_claim(it->second, c);
    }
    for (auto& a : ex.areas) areas.emplace(a.id, std::move(a));
    for (auto& p : ex.principles) {
      auto [it, fresh] = principles.try_emplace(p.id, p);
      if (!fresh) {
        for (const auto& ev : p.evidence_for) {
          if (std::find(it->second.evidence_for.begin(), it->second.evidence_for.end(), ev) == it->second.evidence_for.end()) {
            it->second.evidence_for.push_back(ev);
          }
        }
        for (const auto& s : p.sources) it->second.sources.push_back(s);
        std::sort(it->second.evidence_for.begin(), it->second.evidence_for.end());
      }
    }
    for (auto& d : ex.decisions) decisions.emplace(d.id, std::move(d));
    for (auto& f : ex.forks) forks.emplace(f.id, std::move(f));
    for (auto& s : ex.statuses) statuses.emplace(s.id, std::move(s));
    for (auto& i : ex.items) items.push_back(std::move(i));
    units.push_back(ex.stats);
  }
};

namespace {

template <class M>
auto values(const M& m) {
  std::vector<typename M::mapped_type> v;
  v.reserve(m.size());
  for (const auto& [k, x] : m) v.push_back(x);
  return v;
}

// Names from pass 1: merged by (kind, phrase key), aliases unioned.
Json merge_names(const kb::Normalizer& norm, const std::vector<Json>& stats) {
  std::map<std::pair<std::string, std::string>, std::pair<std::string, std::set<std::string>>> m;
  for (const auto& s : stats) {
    const Json* names = json::find(s, "names");
    if (!names || !names->is_array()) continue;
    for (const auto& n : *names) {
      std::string kind = json::get_string(n, "kind");
      if (kind != "project" && kind != "component" && kind != "branch") continue;
      std::string label = json::get_string(n, "label");
      std::string key = norm.phrase_key(label);
      if (key.empty()) continue;
      auto& slot = m[{kind, key}];
      if (slot.first.empty() || label < slot.first) slot.first = label;
      if (const Json* a = json::find(n, "aliases"); a && a->is_array()) {
        for (const auto& x : *a) {
          if (x.is_string()) slot.second.insert(x.get<std::string>());
        }
      }
    }
  }
  Json out = Json::array();
  for (const auto& [k, v] : m) {
    Json al = Json::array();
    for (const auto& a : v.second) al.push_back(a);
    out.push_back(Json{{"kind", k.first}, {"label", v.first}, {"aliases", al}});
  }
  return out;
}

Result<std::vector<UnitContent>> units_from_catalog(knowledge::StageContext& ctx) {
  const Json* ids = json::find(ctx.input, "units");
  if (!ids || !ids->is_array()) return Error(Errc::InvalidArgument, "catalog input requires a units array");
  catalog::Catalog cat(ctx.rt, ctx.pack);
  catalog::UnitQuery q;
  // The upstream scope already includes full import and optional related
  // units. Reapplying selective filtering here discards valid input IDs.
  q.limit = 1000000;
  LOOM_TRY_ASSIGN(auto cus, cat.query(q));
  std::map<std::string, catalog::CatalogUnit> by_id;
  for (auto& cu : cus) by_id.emplace(cu.unit.id, std::move(cu));
  std::vector<UnitContent> out;
  for (const auto& idj : *ids) {
    if (!idj.is_string()) return Error(Errc::InvalidArgument, "catalog unit id must be a string");
    auto it = by_id.find(idj.get<std::string>());
    if (it == by_id.end()) return Error(Errc::NotFound, "catalog unit missing: " + idj.get<std::string>());
    LOOM_TRY_ASSIGN(std::string bytes, cat.read_unit(it->first));
    UnitContent u;
    u.unit = it->second.unit;
    auto j = json::parse(bytes);
    if (j && j->is_object()) u.structured = std::move(*j);
    else u.text = std::move(bytes);
    out.push_back(std::move(u));
  }
  return out;
}

}  // namespace

Extraction extract_units(std::shared_ptr<const kb::Pack> pack, const std::vector<UnitContent>& units,
                         const std::function<bool()>& should_stop) {
  kb::Normalizer norm(*pack);
  std::vector<Json> stats1;
  {
    Extractor ex(pack);
    for (const auto& u : units) {
      if (should_stop && should_stop()) return Extraction{};
      auto r = ex.process(u);
      if (r) stats1.push_back(r->stats);
    }
  }
  Json names = merge_names(norm, stats1);
  Extractor ex(pack, names);
  Accumulator acc;
  int failed = 0;
  Json per_unit = Json::array();
  for (const auto& u : units) {
    if (should_stop && should_stop()) return Extraction{};
    auto r = ex.process(u);
    if (!r) {
      ++failed;
      continue;
    }
    per_unit.push_back(r->stats);
    acc.add(std::move(*r));
  }
  Extraction out;
  out.observations = values(acc.observations);
  out.entities = values(acc.entities);
  out.claims = values(acc.claims);
  out.areas = values(acc.areas);
  out.principles = values(acc.principles);
  out.decisions = values(acc.decisions);
  out.forks = values(acc.forks);
  out.statuses = model::order_status_history(values(acc.statuses));
  out.items = std::move(acc.items);
  out.stats = Json{{"units", units.size()}, {"failed", failed}, {"names", names}, {"per_unit", per_unit}};
  return out;
}

Result<Json> run_stage(knowledge::StageContext& ctx) {
  std::vector<UnitContent> units;
  std::string from = "catalog";
  if (json::find(ctx.input, "units")) {
    // An empty selection is deliberate. A read/verification failure is an
    // error. Neither authorizes silently analyzing the entire raw corpus.
    LOOM_TRY_ASSIGN(units, units_from_catalog(ctx));
  } else {
    from = "sources";
    for (const auto& s : ctx.config.sources) {
      LOOM_TRY_ASSIGN(auto source_units, read_units(s));
      for (auto& u : source_units) units.push_back(std::move(u));
    }
  }
  std::set<std::string> only;
  if (const Json* t = json::find(ctx.params, "types"); t && t->is_array()) {
    for (const auto& x : *t) {
      if (x.is_string()) only.insert(x.get<std::string>());
    }
  }
  std::int64_t max_units = json::get_int(ctx.params, "max_units", 0);
  if (max_units > 0 && static_cast<std::int64_t>(units.size()) > max_units) units.resize(static_cast<std::size_t>(max_units));
  // de-duplicate units (same id from two sources)
  std::sort(units.begin(), units.end(), [](const UnitContent& a, const UnitContent& b) { return a.unit.id < b.unit.id; });
  units.erase(std::unique(units.begin(), units.end(), [](const UnitContent& a, const UnitContent& b) { return a.unit.id == b.unit.id; }),
              units.end());

  if (!only.empty()) {
    Extractor det(ctx.pack);
    std::vector<UnitContent> kept;
    for (auto& u : units) {
      auto d = det.detect(u);
      if (d && !d->empty() && only.count(d->front().artifact_type)) kept.push_back(std::move(u));
    }
    units = std::move(kept);
  }
  bool paused = false;
  Extraction all = extract_units(ctx.pack, units, [&] {
    paused = paused || (ctx.should_stop && ctx.should_stop());
    return paused;
  });
  if (paused) return Error(Errc::Paused, "extract paused");
  Json names = all.stats["names"];
  int failed = static_cast<int>(json::get_int(all.stats, "failed"));
  Accumulator acc;
  acc.add(std::move(all));
  auto recs = values(acc.statuses);
  std::sort(recs.begin(), recs.end(), [](const model::StatusRecord& x, const model::StatusRecord& y) { return x.id < y.id; });

  auto& st = ctx.store;
  LOOM_TRY(st.clear_run(ctx.run));
  LOOM_TRY(st.put_observations(ctx.run, values(acc.observations)));
  LOOM_TRY(st.put_entities(ctx.run, values(acc.entities)));
  LOOM_TRY(st.put_claims(ctx.run, values(acc.claims)));
  LOOM_TRY(st.put_principles(ctx.run, values(acc.principles)));
  LOOM_TRY(st.put_areas(ctx.run, values(acc.areas)));
  LOOM_TRY(st.put_decisions(ctx.run, values(acc.decisions)));
  LOOM_TRY(st.put_forks(ctx.run, values(acc.forks)));
  LOOM_TRY(st.put_status_records(ctx.run, recs));

  // Optional cheap-model proposals use the already persisted, located
  // observations. They remain candidates; this never upgrades a model's
  // interpretation into an observed Claim or overwrites owner judgements.
  LOOM_TRY_ASSIGN(auto semantic, propose_semantics(ctx, values(acc.observations),
                                                 values(acc.entities), values(acc.claims)));

  Sha256 h;
  auto feed = [&](const auto& m) {
    for (const auto& [id, x] : m) {
      h.update(id);
      h.update(json::canonical(x.to_json()));
    }
  };
  feed(acc.observations);
  feed(acc.entities);
  feed(acc.claims);
  feed(acc.principles);
  feed(acc.areas);
  feed(acc.decisions);
  feed(acc.forks);
  for (const auto& r : recs) h.update(json::canonical(r.to_json()));
  h.update(json::get_string(semantic, "output"));
  Json stats{{"from", from},
             {"units", units.size()},
             {"failed", failed},
             {"names", names.size()},
             {"observations", acc.observations.size()},
             {"entities", acc.entities.size()},
             {"claims", acc.claims.size()},
             {"areas", acc.areas.size()},
             {"principles", acc.principles.size()},
             {"decisions", acc.decisions.size()},
             {"forks", acc.forks.size()},
             {"statuses", recs.size()},
             {"semantic", semantic}};
  return Json{{"output", h.finish_hex()}, {"stats", stats}, {"names", names}};
}

}  // namespace loom::extract
