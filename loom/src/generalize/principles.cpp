// generalize: principle discovery and typing (§3.1, §6.5). Normative
// statements, generalizations and decision rationales of the owner are
// clustered by term overlap; clusters matching a visible seed raise its
// support, the others become discovered candidates, typed by level, form,
// scope, protects and derived_from.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

#include "internal.h"

namespace loom::generalize {

using detail::Index;
using model::PrincipleForm;
using model::PrincipleLevel;

namespace {

bool owner_speaker(std::string_view s) {
  return s != "assistant" && s != "system" && s != "model" && s != "tool" && s != "bot";
}

struct Statement {
  std::string obs;        // observation id ("" for a text-only extract candidate)
  std::string text;
  std::string date;
  std::string unit;
  std::string lang;
  double score = 0.0;
  std::vector<std::string> terms;
  std::vector<std::string> decisions;  // decision ids it is the rationale of
  std::string candidate;               // extract candidate principle id
};

std::string lang_of(const kb::Normalizer& norm, const std::string& lang, const std::string& text) {
  if (lang == "pl" || lang == "en") return lang;
  auto g = norm.guess_lang(text);
  return g == kb::Lang::Pl ? "pl" : "en";
}

std::vector<std::string> principle_terms(const kb::Normalizer& norm, const model::Principle& p) {
  std::vector<std::string> t;
  for (const auto& [l, s] : p.statement) t = detail::set_union(t, detail::terms(norm, s));
  for (const auto& s : p.phrasings) t = detail::set_union(t, detail::terms(norm, s));
  return t;
}

double level_rank(PrincipleLevel l) { return l == PrincipleLevel::Value ? 0 : l == PrincipleLevel::Epistemic ? 1 : 2; }

}  // namespace

Status type_principle(const kb::Pack& pack, model::Principle& p, const Evidence& ev) {
  kb::Normalizer norm(pack);
  std::string text;
  for (const auto& [l, s] : p.statement) text += s + " . ";
  for (const auto& s : p.phrasings) text += s + " . ";
  auto score = [&](std::string_view cls) { return detail::cue_score(norm, text, detail::cue_class_or_default(pack, cls)); };
  double n = std::max<double>(1.0, static_cast<double>(p.phrasings.size()));
  double sv = score("principle.level.value") / n, se = score("principle.level.epistemic") / n;
  if (sv >= 1.5 && sv >= se) {
    p.level = PrincipleLevel::Value;
  } else if (se >= 1.0) {
    p.level = PrincipleLevel::Epistemic;
  } else {
    p.level = PrincipleLevel::Strategy;
  }
  double fc = score("principle.form.conflict_resolution") / n, fm = score("principle.form.meta") / n,
         fi = score("principle.form.invariant") / n, fd = score("principle.form.default") / n;
  if (fc >= 2.0 && fc >= fm) {
    p.form = PrincipleForm::ConflictResolution;
  } else if (fm >= 2.0) {
    p.form = PrincipleForm::Meta;
  } else if (fd >= 1.5 && fd > fi) {
    p.form = PrincipleForm::Default;
  } else if (fi >= 1.5 || p.level == PrincipleLevel::Value) {
    p.form = PrincipleForm::Invariant;
  } else {
    p.form = PrincipleForm::Heuristic;
  }
  // How conflicts are settled is a strategy, whatever values it names.
  if (p.form == PrincipleForm::ConflictResolution && p.level == PrincipleLevel::Value) p.level = PrincipleLevel::Strategy;

  // Scope: the subjects and areas its evidence comes from.
  Index ix(ev);
  std::set<std::string> subjects, areas;
  for (const auto& e : p.evidence_for) {
    auto* o = ix.find_obs(e);
    if (!o) continue;
    for (const auto& [s, units] : ix.subject_units) {
      if (units.count(o->unit)) {
        if (auto* ent = ix.find_entity(s); ent && ent->kind == "project") subjects.insert(s);
      }
    }
    for (const auto& a : ev.areas) {
      if (a.observation == o->id) areas.insert(a.id);
    }
  }
  p.scope.areas.assign(areas.begin(), areas.end());
  p.scope.conditions.clear();
  if (subjects.size() == 1) {
    auto* ent = ix.find_entity(*subjects.begin());
    p.scope.conditions.push_back("observed only in " + (ent && !ent->label.empty() ? ent->label : *subjects.begin()));
  }

  // protects / derived_from against the principles of the evidence (visible
  // seeds + principles discovered so far).
  auto mine = principle_terms(norm, p);
  detail::TermWeights w;
  w.add(mine);
  for (const auto& q : ev.principles) w.add(principle_terms(norm, q));
  p.protects.clear();
  p.derived_from.clear();
  if (p.level != PrincipleLevel::Value) {
    std::vector<std::pair<double, std::string>> vals;
    for (const auto& q : ev.principles) {
      if (q.id == p.id || q.level != PrincipleLevel::Value) continue;
      double s = detail::woverlap(mine, principle_terms(norm, q), w);
      if (s >= 0.25) vals.emplace_back(-s, q.id);
    }
    std::sort(vals.begin(), vals.end());
    for (std::size_t i = 0; i < vals.size() && i < 2; ++i) p.protects.push_back(vals[i].second);
  }
  std::vector<std::pair<double, std::string>> gen;
  for (const auto& q : ev.principles) {
    if (q.id == p.id) continue;
    double s = detail::woverlap(mine, principle_terms(norm, q), w);
    bool shared = false;
    for (const auto& v : q.protects) shared |= std::find(p.protects.begin(), p.protects.end(), v) != p.protects.end();
    shared |= std::find(p.protects.begin(), p.protects.end(), q.id) != p.protects.end();
    bool more_general = level_rank(q.level) < level_rank(p.level);
    bool same_broader = q.level == p.level && q.evidence_for.size() > p.evidence_for.size() && s >= 0.45;
    if ((more_general && (shared || s >= 0.3)) || same_broader) gen.emplace_back(-(s + shared), q.id);
  }
  std::sort(gen.begin(), gen.end());
  for (std::size_t i = 0; i < gen.size() && i < 2; ++i) p.derived_from.push_back(gen[i].second);
  std::sort(p.protects.begin(), p.protects.end());
  std::sort(p.derived_from.begin(), p.derived_from.end());
  return {};
}

Result<PrincipleReport> discover_principles(const kb::Pack& pack, const Evidence& ev, const model::PriorFilter& priors) {
  kb::Normalizer norm(pack);
  Index ix(ev);
  const double tau_cluster = detail::threshold(pack, "principles", "cluster_jaccard", 0.5);
  const double tau_seed = detail::threshold(pack, "principles", "seed_match_overlap", 0.5);
  const int min_units = static_cast<int>(detail::threshold(pack, "principles", "min_units", 2));
  const int min_dates = static_cast<int>(detail::threshold(pack, "principles", "min_distinct_dates", 2));
  const double tau_statement = detail::threshold(pack, "principles", "min_cue_score", 1.5);
  Json normative = detail::cue_class_or_default(pack, "normative");
  Json general = detail::cue_class_or_default(pack, "generalize.generalization");

  // 1. Candidate statements.
  std::map<std::string, std::vector<std::string>> rationale_of;  // obs -> decision ids
  for (const auto& d : ev.decisions) {
    for (auto* o : ix.decision_obs(d)) rationale_of[o->id].push_back(d.id);
  }
  std::map<std::string, std::string> candidate_of;  // obs -> extract candidate id
  std::vector<const model::Principle*> text_only;
  std::set<std::string> pack_ids;
  if (auto all = model::principles(pack)) {
    for (const auto& p : *all) pack_ids.insert(p.id);
  }
  for (const auto& p : ev.principles) {
    if (pack_ids.count(p.id)) continue;
    bool any = false;
    for (const auto& e : p.evidence_for) {
      if (ix.find_obs(e)) {
        candidate_of.emplace(e, p.id);
        any = true;
      }
    }
    if (!any) text_only.push_back(&p);
  }
  std::vector<Statement> st;
  for (const auto& o : ev.observations) {
    if (!owner_speaker(o.speaker) || o.kind == model::ObservationKind::CodeBlock) continue;
    double s = detail::cue_score(norm, o.text, normative) + detail::cue_score(norm, o.text, general);
    bool rationale = rationale_of.count(o.id) > 0, cand = candidate_of.count(o.id) > 0;
    if (s < tau_statement && !cand && !(rationale && s >= 1.0)) continue;
    Statement x;
    x.obs = o.id;
    x.text = o.text;
    x.date = detail::day(o.date);
    x.unit = o.unit;
    x.lang = lang_of(norm, o.lang, o.text);
    x.score = s;
    x.terms = detail::terms(norm, o.text);
    if (rationale) x.decisions = rationale_of[o.id];
    if (cand) x.candidate = candidate_of[o.id];
    if (!x.terms.empty()) st.push_back(std::move(x));
  }
  for (auto* p : text_only) {
    Statement x;
    x.text = p->statement.count("en") ? p->statement.at("en") : (p->statement.empty() ? "" : p->statement.begin()->second);
    x.lang = p->statement.count("en") ? "en" : "pl";
    x.score = tau_statement;
    x.terms = detail::terms(norm, x.text);
    x.candidate = p->id;
    if (!x.terms.empty()) st.push_back(std::move(x));
  }

  // 2. Seeds visible under the prior filter.
  std::vector<model::Principle> seeds;
  if (priors.enabled) {
    LOOM_TRY_ASSIGN(seeds, model::principles(pack, priors));
  }
  std::vector<std::vector<std::vector<std::string>>> seed_texts;  // per seed: statement + phrasings
  detail::TermWeights w;
  for (const auto& x : st) w.add(x.terms);
  for (const auto& s : seeds) {
    std::vector<std::vector<std::string>> v;
    for (const auto& [l, t] : s.statement) v.push_back(detail::terms(norm, t));
    for (const auto& t : s.phrasings) v.push_back(detail::terms(norm, t));
    for (const auto& t : v) w.add(t);
    seed_texts.push_back(std::move(v));
  }

  // 3. Clustering (single link on weighted Jaccard, deterministic order).
  std::sort(st.begin(), st.end(), [](const Statement& a, const Statement& b) {
    if (a.date != b.date) return a.date < b.date;
    return a.obs < b.obs;
  });
  std::vector<int> parent(st.size());
  for (std::size_t i = 0; i < st.size(); ++i) parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int i) { return parent[i] == i ? i : parent[i] = find(parent[i]); };
  for (std::size_t i = 0; i < st.size(); ++i) {
    for (std::size_t j = i + 1; j < st.size(); ++j) {
      if (detail::wjaccard(st[i].terms, st[j].terms, w) >= tau_cluster) {
        int a = find(static_cast<int>(i)), b = find(static_cast<int>(j));
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
      }
    }
  }
  std::map<int, std::vector<const Statement*>> clusters;
  for (std::size_t i = 0; i < st.size(); ++i) clusters[find(static_cast<int>(i))].push_back(&st[i]);

  // 4. Seed matching, then discovered candidates.
  PrincipleReport rep;
  std::map<std::string, model::Principle> found_seeds;
  std::vector<std::vector<const Statement*>> unmatched;
  for (auto& [root, members] : clusters) {
    int best = -1;
    double best_s = 0;
    for (std::size_t k = 0; k < seeds.size(); ++k) {
      for (const auto* m : members) {
        for (const auto& t : seed_texts[k]) {
          // Overlap must cover most of the statement (not only the seed): a
          // long seed statement does not swallow every short sentence.
          double s = std::min(detail::woverlap(m->terms, t, w), 2.0 * detail::wjaccard(m->terms, t, w));
          if (s > best_s + 1e-12) {
            best_s = s;
            best = static_cast<int>(k);
          }
        }
      }
    }
    if (best >= 0 && best_s >= tau_seed) {
      auto& p = found_seeds.try_emplace(seeds[best].id, seeds[best]).first->second;
      for (const auto* m : members) {
        if (!m->obs.empty()) p.evidence_for.push_back(m->obs);
        for (const auto& d : m->decisions) p.evidence_for.push_back(d);
        p.phrasings.push_back(m->text);
      }
      continue;
    }
    unmatched.push_back(members);
  }
  auto support = [&](const std::vector<std::string>& evidence, int& units, int& dates) {
    std::set<std::string> us, ds;
    for (const auto& e : evidence) {
      if (auto* o = ix.find_obs(e)) {
        us.insert(o->unit);
        if (!o->date.empty()) ds.insert(detail::day(o->date));
      }
    }
    units = static_cast<int>(us.size());
    dates = static_cast<int>(ds.size());
  };
  for (const auto& s : seeds) {
    auto it = found_seeds.find(s.id);
    if (it == found_seeds.end()) {
      rep.seed_status[s.id] = "seed_only";
      continue;
    }
    auto& p = it->second;
    std::sort(p.evidence_for.begin(), p.evidence_for.end());
    p.evidence_for.erase(std::unique(p.evidence_for.begin(), p.evidence_for.end()), p.evidence_for.end());
    int u = 0, d = 0;
    support(p.evidence_for, u, d);
    if (u >= min_units && d >= min_dates && p.validation == model::ValidationStatus::Candidate) p.validation = model::ValidationStatus::Supported;
    p.confidence = detail::clamp01(1.0 - (1.0 - p.confidence) * std::pow(0.7, u));
    rep.seed_status[s.id] = "found";
    rep.principles.push_back(p);
  }

  // Discovered candidates. A single statement from a single unit stays a
  // principle only when it is strongly normative.
  Evidence typing_ev = ev;
  typing_ev.principles = seeds;
  std::vector<model::Principle> discovered;
  for (const auto& members : unmatched) {
    std::set<std::string> units;
    double top = 0;
    for (const auto* m : members) {
      units.insert(m->unit.empty() ? m->candidate : m->unit);
      top = std::max(top, m->score);
    }
    if (units.size() < 2 && top < 2.0 && members.front()->candidate.empty()) continue;
    const Statement* rep_st = members.front();
    for (const auto* m : members) {
      if (m->score > rep_st->score + 1e-12) rep_st = m;
    }
    model::Principle p;
    p.statement[rep_st->lang] = rep_st->text;
    for (const auto* m : members) {
      p.phrasings.push_back(m->text);
      if (!m->obs.empty()) p.evidence_for.push_back(m->obs);
      for (const auto& d : m->decisions) p.evidence_for.push_back(d);
      if (!m->candidate.empty()) p.supersedes.push_back(m->candidate);
    }
    std::sort(p.evidence_for.begin(), p.evidence_for.end());
    p.evidence_for.erase(std::unique(p.evidence_for.begin(), p.evidence_for.end()), p.evidence_for.end());
    std::sort(p.supersedes.begin(), p.supersedes.end());
    p.supersedes.erase(std::unique(p.supersedes.begin(), p.supersedes.end()), p.supersedes.end());
    int u = 0, d = 0;
    support(p.evidence_for, u, d);
    p.validation = (u >= min_units && d >= min_dates) ? model::ValidationStatus::Supported : model::ValidationStatus::Candidate;
    p.confidence = detail::clamp01(1.0 - std::pow(1.0 - std::min(0.8, 0.25 + 0.1 * top), std::max(1, u)));
    p.owner = "user";
    p.origin = model::Origin::Archive;
    discovered.push_back(std::move(p));
  }
  // Type in two passes: level/form first, then protects / derived_from
  // against the value principles found in this report.
  for (std::size_t i = 0; i < discovered.size(); ++i) discovered[i].id = "p_tmp" + std::to_string(i);
  for (auto& p : discovered) LOOM_TRY(type_principle(pack, p, typing_ev));
  for (const auto& p : discovered) typing_ev.principles.push_back(p);
  for (auto& p : discovered) {
    LOOM_TRY(type_principle(pack, p, typing_ev));
    auto it = p.statement.begin();
    p.id = kb::stable_id("p_", std::string(model::to_string(p.level)) + '\x1f' + std::string(model::to_string(p.form)) + '\x1f' +
                                   norm.phrase_key(it->second));
  }
  // Ids are known only now: re-point derived_from / protects of discovered
  // principles (typing referenced them before their ids were final).
  std::map<std::string, std::string> id_map;
  for (std::size_t i = 0; i < discovered.size(); ++i) id_map["p_tmp" + std::to_string(i)] = discovered[i].id;
  for (auto& p : discovered) {
    for (auto& x : p.derived_from) {
      if (auto it = id_map.find(x); it != id_map.end()) x = it->second;
    }
    for (auto& x : p.protects) {
      if (auto it = id_map.find(x); it != id_map.end()) x = it->second;
    }
    p.derived_from.erase(std::remove(p.derived_from.begin(), p.derived_from.end(), p.id), p.derived_from.end());
    rep.principles.push_back(p);
  }
  std::sort(rep.principles.begin(), rep.principles.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  rep.principles.erase(std::unique(rep.principles.begin(), rep.principles.end(), [](const auto& a, const auto& b) { return a.id == b.id; }),
                       rep.principles.end());
  return rep;
}

}  // namespace loom::generalize
