// generalize: principle discovery and typing (§3.1, §6.5). Normative
// statements, generalizations and decision rationales of the owner are
// clustered by term overlap; clusters matching a visible seed raise its
// support, the others become discovered candidates, typed by level, form,
// scope, protects and derived_from.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

#include "internal.h"
#include "kb/stage_profile.h"

namespace loom::generalize {

using detail::FoldedText;
using detail::fold_text;
using detail::Index;
using detail::PreparedCues;
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
  std::vector<std::uint32_t> ids;      // `terms` as ids of the term table (ascending == lexicographic order)
  double weight = 0.0;                 // sum of the term weights, in term order
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

// The terms of the statements and seeds of one discovery pass as ids of a
// table sorted lexicographically, so that a merge over id vectors visits the
// terms in exactly the order a merge over the strings does (the sums below
// are therefore bit-identical to detail::wjaccard / detail::woverlap).
struct TermTable {
  std::vector<std::string> terms;
  std::unordered_map<std::string, std::uint32_t> id;
  std::vector<double> weight;

  template <class Docs>
  void build(const Docs& docs, const detail::TermWeights& w) {
    std::set<std::string> all;
    for (const auto* d : docs) all.insert(d->begin(), d->end());
    terms.assign(all.begin(), all.end());
    id.reserve(terms.size() * 2);
    weight.resize(terms.size());
    for (std::size_t i = 0; i < terms.size(); ++i) {
      id.emplace(terms[i], static_cast<std::uint32_t>(i));
      weight[i] = w.w(terms[i]);
    }
  }
  std::vector<std::uint32_t> ids_of(const std::vector<std::string>& v) const {
    std::vector<std::uint32_t> out;
    out.reserve(v.size());
    for (const auto& t : v) out.push_back(id.at(t));
    return out;
  }
  double sum(const std::vector<std::uint32_t>& v) const {
    double s = 0;
    for (auto i : v) s += weight[i];
    return s;
  }
};

// Weighted Jaccard and overlap coefficient of two id vectors in one merge.
struct Similarity {
  double jaccard = 0.0, overlap = 0.0;
};
Similarity similarity(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, double sa, double sb,
                      const std::vector<double>& weight) {
  double inter = 0, uni = 0;
  std::size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    if (j == b.size() || (i < a.size() && a[i] < b[j])) {
      uni += weight[a[i++]];
    } else if (i == a.size() || b[j] < a[i]) {
      uni += weight[b[j++]];
    } else {
      double x = weight[a[i]];
      inter += x;
      uni += x;
      ++i;
      ++j;
    }
  }
  double m = std::min(sa, sb);
  return Similarity{uni > 0 ? inter / uni : 0.0, m > 0 ? inter / m : 0.0};
}

}  // namespace

namespace {

// A principle the one being typed is compared with (visible seeds + the
// principles discovered so far), reduced to what the comparison reads.
struct Ref {
  std::string id;
  PrincipleLevel level = PrincipleLevel::Strategy;
  std::size_t evidence = 0;
  std::vector<std::string> protects;
  std::vector<std::string> terms;
};

Ref make_ref(const kb::Normalizer& norm, const model::Principle& q) {
  Ref r;
  r.id = q.id;
  r.level = q.level;
  r.evidence = q.evidence_for.size();
  r.protects = q.protects;
  r.terms = principle_terms(norm, q);
  return r;
}

// Everything typing reads that does not depend on the principle being typed.
// It is built once per pass (never per principle): the normalizer, the
// evidence index, the prepared cue classes and the lookup tables.
struct TypingContext {
  const kb::Normalizer& norm;
  const Index& ix;
  PreparedCues value, epistemic, conflict, meta, invariant, dflt;
  std::map<std::string, std::vector<std::string>, std::less<>> areas_of_obs;         // observation -> area ids
  std::map<std::string, std::set<std::string>, std::less<>> project_subjects_of_unit;  // unit -> project entity ids

  TypingContext(const kb::Pack& pack, const kb::Normalizer& n, const Index& index) : norm(n), ix(index) {
    auto prep = [&](std::string_view cls) { return PreparedCues(norm, detail::cue_class(pack, cls)); };
    value = prep("principle.level.value");
    epistemic = prep("principle.level.epistemic");
    conflict = prep("principle.form.conflict_resolution");
    meta = prep("principle.form.meta");
    invariant = prep("principle.form.invariant");
    dflt = prep("principle.form.default");
    for (const auto& a : ix.ev.areas) areas_of_obs[a.observation].push_back(a.id);
    for (const auto& [s, units] : ix.subject_units) {
      auto* ent = ix.find_entity(s);
      if (!ent || ent->kind != "project") continue;
      for (const auto& u : units) project_subjects_of_unit[u].insert(s);
    }
  }
};

// Document frequencies of the reference principles' terms.
struct RefFrequencies {
  std::map<std::string, int, std::less<>> df;
  std::size_t docs = 0;
  explicit RefFrequencies(const std::vector<Ref>& refs) : docs(refs.size()) {
    for (const auto& r : refs) {
      for (const auto& t : r.terms) ++df[t];
    }
  }
};

// The weights of one typing call: the references' frequencies plus the
// principle being typed as one more document (exactly what a TermWeights
// filled with `mine` and every reference would answer).
struct CallWeights {
  const RefFrequencies& base;
  const std::vector<std::string>& mine;  // sorted
  double w(const std::string& t) const {
    int c = 0;
    if (auto it = base.df.find(t); it != base.df.end()) c = it->second;
    if (std::binary_search(mine.begin(), mine.end(), t)) ++c;
    double df = c == 0 ? 1.0 : static_cast<double>(c);
    double n = static_cast<double>(std::max<std::size_t>(base.docs + 1, 1));
    return std::log(1.0 + n / df);
  }
};

void type_with(const TypingContext& cx, model::Principle& p, const std::vector<Ref>& refs, const RefFrequencies& freq) {
  const kb::Normalizer& norm = cx.norm;
  const Index& ix = cx.ix;
  std::string text;
  for (const auto& [l, s] : p.statement) text += s + " . ";
  for (const auto& s : p.phrasings) text += s + " . ";
  FoldedText ft = fold_text(norm, text);
  double n = std::max<double>(1.0, static_cast<double>(p.phrasings.size()));
  double sv = cx.value.score(ft) / n, se = cx.epistemic.score(ft) / n;
  if (sv >= 1.5 && sv >= se) {
    p.level = PrincipleLevel::Value;
  } else if (se >= 1.0) {
    p.level = PrincipleLevel::Epistemic;
  } else {
    p.level = PrincipleLevel::Strategy;
  }
  double fc = cx.conflict.score(ft) / n, fm = cx.meta.score(ft) / n, fi = cx.invariant.score(ft) / n, fd = cx.dflt.score(ft) / n;
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
  std::set<std::string> subjects, areas;
  for (const auto& e : p.evidence_for) {
    auto* o = ix.find_obs(e);
    if (!o) continue;
    if (auto it = cx.project_subjects_of_unit.find(o->unit); it != cx.project_subjects_of_unit.end()) {
      subjects.insert(it->second.begin(), it->second.end());
    }
    if (auto it = cx.areas_of_obs.find(o->id); it != cx.areas_of_obs.end()) areas.insert(it->second.begin(), it->second.end());
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
  CallWeights w{freq, mine};
  std::vector<double> overlap(refs.size(), 0.0);
  for (std::size_t i = 0; i < refs.size(); ++i) {
    if (refs[i].id != p.id) overlap[i] = detail::woverlap(mine, refs[i].terms, w);
  }
  p.protects.clear();
  p.derived_from.clear();
  if (p.level != PrincipleLevel::Value) {
    std::vector<std::pair<double, std::string>> vals;
    for (std::size_t i = 0; i < refs.size(); ++i) {
      const Ref& q = refs[i];
      if (q.id == p.id || q.level != PrincipleLevel::Value) continue;
      double s = overlap[i];
      if (s >= 0.25) vals.emplace_back(-s, q.id);
    }
    std::sort(vals.begin(), vals.end());
    for (std::size_t i = 0; i < vals.size() && i < 2; ++i) p.protects.push_back(vals[i].second);
  }
  std::vector<std::pair<double, std::string>> gen;
  for (std::size_t i = 0; i < refs.size(); ++i) {
    const Ref& q = refs[i];
    if (q.id == p.id) continue;
    double s = overlap[i];
    bool shared = false;
    for (const auto& v : q.protects) shared |= std::find(p.protects.begin(), p.protects.end(), v) != p.protects.end();
    shared |= std::find(p.protects.begin(), p.protects.end(), q.id) != p.protects.end();
    bool more_general = level_rank(q.level) < level_rank(p.level);
    bool same_broader = q.level == p.level && q.evidence > p.evidence_for.size() && s >= 0.45;
    if ((more_general && (shared || s >= 0.3)) || same_broader) gen.emplace_back(-(s + shared), q.id);
  }
  std::sort(gen.begin(), gen.end());
  for (std::size_t i = 0; i < gen.size() && i < 2; ++i) p.derived_from.push_back(gen[i].second);
  std::sort(p.protects.begin(), p.protects.end());
  std::sort(p.derived_from.begin(), p.derived_from.end());
}

}  // namespace

Status type_principle(const kb::Pack& pack, model::Principle& p, const Evidence& ev) {
  kb::Normalizer norm(pack);
  Index ix(ev);
  TypingContext cx(pack, norm, ix);
  std::vector<Ref> refs;
  refs.reserve(ev.principles.size());
  for (const auto& q : ev.principles) refs.push_back(make_ref(norm, q));
  RefFrequencies freq(refs);
  type_with(cx, p, refs, freq);
  return {};
}

namespace {
Result<double> required_parameter(const kb::Pack& pack, std::string_view section, std::string_view key) {
  const Json* values = json::find(pack.file("policy/thresholds.json"), section);
  const Json* value = values && values->is_object() ? json::find(*values, key) : nullptr;
  if (!value || !value->is_number())
    return Error(Errc::InvalidArgument, "required numeric policy parameter: " + std::string(section) + "/" + std::string(key));
  const double number = value->get<double>();
  if (!std::isfinite(number))
    return Error(Errc::InvalidArgument, "nonfinite policy parameter: " + std::string(section) + "/" + std::string(key));
  return number;
}
}  // namespace

Result<PrincipleReport> discover_principles(const kb::Pack& pack, const Evidence& ev, const model::PriorFilter& priors) {
  LOOM_TRY_ASSIGN(const double seed_jaccard_multiplier, required_parameter(pack, "principles", "seed_jaccard_multiplier"));
  LOOM_TRY_ASSIGN(const double repeat_confidence_residual_factor, required_parameter(pack, "principles", "repeat_confidence_residual_factor"));
  LOOM_TRY_ASSIGN(const double discovered_confidence_cap, required_parameter(pack, "principles", "discovered_confidence_cap"));
  LOOM_TRY_ASSIGN(const double discovered_confidence_base, required_parameter(pack, "principles", "discovered_confidence_base"));
  LOOM_TRY_ASSIGN(const double discovered_confidence_score_factor, required_parameter(pack, "principles", "discovered_confidence_score_factor"));
  kb::Normalizer norm(pack);
  Index ix(ev);
  const double tau_cluster = detail::threshold(pack, "principles", "cluster_jaccard", 0.5);
  const double tau_seed = detail::threshold(pack, "principles", "seed_match_overlap", 0.5);
  const int min_units = static_cast<int>(detail::threshold(pack, "principles", "min_units", 2));
  const int min_dates = static_cast<int>(detail::threshold(pack, "principles", "min_distinct_dates", 2));
  const double tau_statement = detail::threshold(pack, "principles", "min_cue_score", 1.5);
  const PreparedCues normative(norm, detail::cue_class(pack, "normative"));
  const PreparedCues general(norm, detail::cue_class(pack, "generalize.generalization"));

  // 1. Candidate statements.
  std::optional<prof::Scope> ph(std::in_place, "generalize.principles.statements");
  auto phase = [&](const char* name) { ph.reset(); ph.emplace(name); };
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
    const FoldedText ft = detail::fold_text(norm, o.text);
    double s = normative.score(ft) + general.score(ft);
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
  phase("generalize.principles.cluster");
  std::sort(st.begin(), st.end(), [](const Statement& a, const Statement& b) {
    if (a.date != b.date) return a.date < b.date;
    return a.obs < b.obs;
  });
  TermTable table;
  {
    std::vector<const std::vector<std::string>*> docs;
    for (const auto& x : st) docs.push_back(&x.terms);
    for (const auto& v : seed_texts) {
      for (const auto& t : v) docs.push_back(&t);
    }
    table.build(docs, w);
  }
  for (auto& x : st) {
    x.ids = table.ids_of(x.terms);
    x.weight = table.sum(x.ids);
  }
  std::vector<std::vector<std::vector<std::uint32_t>>> seed_ids;  // per seed: ids of statement + phrasings
  std::vector<std::vector<double>> seed_sum;
  for (const auto& v : seed_texts) {
    std::vector<std::vector<std::uint32_t>> ids;
    std::vector<double> sums;
    for (const auto& t : v) {
      ids.push_back(table.ids_of(t));
      sums.push_back(table.sum(ids.back()));
    }
    seed_ids.push_back(std::move(ids));
    seed_sum.push_back(std::move(sums));
  }
  std::vector<int> parent(st.size());
  for (std::size_t i = 0; i < st.size(); ++i) parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int i) { return parent[i] == i ? i : parent[i] = find(parent[i]); };
  {
    // Two statements can only join when they share a term (tau > 0): candidates
    // come from the postings of the terms of each statement, pairs already in
    // one cluster are skipped, and a pair whose total weights differ by more
    // than the threshold allows cannot reach it (an upper bound of the
    // Jaccard index, with a tolerance far above the rounding of the sums).
    const bool sparse = tau_cluster > 1e-9;
    std::vector<std::vector<std::uint32_t>> postings(table.terms.size());
    std::vector<std::size_t> stamp(st.size(), SIZE_MAX);
    std::vector<std::size_t> cand;
    long compared = 0;
    for (std::size_t i = 0; i < st.size(); ++i) {
      cand.clear();
      if (sparse) {
        for (auto t : st[i].ids) {
          for (auto j : postings[t]) {
            if (stamp[j] != i) {
              stamp[j] = i;
              cand.push_back(j);
            }
          }
        }
      } else {
        for (std::size_t j = 0; j < i; ++j) cand.push_back(j);
      }
      for (std::size_t j : cand) {
        int a = find(static_cast<int>(i)), b = find(static_cast<int>(j));
        if (a == b) continue;
        double lo = std::min(st[i].weight, st[j].weight), hi = std::max(st[i].weight, st[j].weight);
        if (sparse && lo < (tau_cluster - 1e-9) * hi) continue;
        ++compared;
        if (similarity(st[i].ids, st[j].ids, st[i].weight, st[j].weight, table.weight).jaccard >= tau_cluster) {
          parent[std::max(a, b)] = std::min(a, b);
        }
      }
      for (auto t : st[i].ids) postings[t].push_back(static_cast<std::uint32_t>(i));
    }
    prof::count("generalize.principles.statements", static_cast<long>(st.size()));
    prof::count("generalize.principles.cluster_pairs_compared", compared);
  }
  std::map<int, std::vector<const Statement*>> clusters;
  for (std::size_t i = 0; i < st.size(); ++i) clusters[find(static_cast<int>(i))].push_back(&st[i]);

  // 4. Seed matching, then discovered candidates.
  phase("generalize.principles.seeds");
  PrincipleReport rep;
  std::map<std::string, model::Principle> found_seeds;
  std::vector<std::vector<const Statement*>> unmatched;
  for (auto& [root, members] : clusters) {
    int best = -1;
    double best_s = 0;
    for (std::size_t k = 0; k < seeds.size(); ++k) {
      for (const auto* m : members) {
        for (std::size_t ti = 0; ti < seed_ids[k].size(); ++ti) {
          // Overlap must cover most of the statement (not only the seed): a
          // long seed statement does not swallow every short sentence.
          auto sim = similarity(m->ids, seed_ids[k][ti], m->weight, seed_sum[k][ti], table.weight);
          double s = std::min(sim.overlap, seed_jaccard_multiplier * sim.jaccard);
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
    p.confidence = detail::clamp01(1.0 - (1.0 - p.confidence) * std::pow(repeat_confidence_residual_factor, u));
    rep.seed_status[s.id] = "found";
    rep.principles.push_back(p);
  }

  // Discovered candidates. A single statement from a single unit stays a
  // principle only when it is strongly normative.
  phase("generalize.principles.discovered");
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
    p.confidence = detail::clamp01(1.0 - std::pow(1.0 - std::min(discovered_confidence_cap, discovered_confidence_base + discovered_confidence_score_factor * top), std::max(1, u)));
    p.owner = "user";
    p.origin = model::Origin::Archive;
    discovered.push_back(std::move(p));
  }
  // Type in two passes: level/form first, then protects / derived_from
  // against the value principles found in this report.
  phase("generalize.principles.typing");
  for (std::size_t i = 0; i < discovered.size(); ++i) discovered[i].id = "p_tmp" + std::to_string(i);
  // One typing context per pass (normalizer, evidence index, prepared cues
  // and the reference principles' terms are built once, not per principle).
  // Pass 1 compares with the visible seeds; pass 2 with the seeds and the
  // pass-1 result of every discovered principle.
  TypingContext typing(pack, norm, ix);
  std::vector<Ref> refs;
  refs.reserve(seeds.size() + discovered.size());
  for (const auto& q : seeds) refs.push_back(make_ref(norm, q));
  {
    RefFrequencies freq(refs);
    for (auto& p : discovered) type_with(typing, p, refs, freq);
  }
  for (const auto& p : discovered) refs.push_back(make_ref(norm, p));
  RefFrequencies freq2(refs);
  for (auto& p : discovered) {
    type_with(typing, p, refs, freq2);
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
