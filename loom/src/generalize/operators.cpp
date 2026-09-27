// generalize: operator mining (§3.3, §6.5) and predictions (§5, §7.1).
//
// Every decision is read, in time order, as (situation at decision time ->
// chosen solution): the situation is what the owner faced (the alternatives
// on the table and the conversation right before the decision), the solution
// is what was chosen and how the owner said it. Decisions whose signatures
// recur are merged into one operator; an operator is linked to a visible
// seed operator (prior) when its text overlaps. Predictions apply the
// operators mined from the evidence <= T; they are scored against the
// decisions after T at solution-class level: a later decision is attributed
// to its nearest prediction (situation + solution signature) and the
// prediction holds only when the solution signature alone matches.
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>

#include "internal.h"

namespace loom::generalize {

using detail::Index;

namespace {

struct Sig {
  const model::Decision* d = nullptr;
  std::string date;
  std::string unit;
  std::string obs;
  std::string situation_text;
  std::string solution_text;
  std::vector<std::string> sit;
  std::vector<std::string> sol;
  bool kept_open = false;
  bool reversal = false;
};

bool owner(std::string_view s) { return s != "assistant" && s != "system" && s != "model" && s != "tool"; }

// Signature terms: normalizer keys cut to a 6-byte prefix, which folds the
// inflections the light stemmer leaves apart ("zamienilam" / "zamieniam",
// "restarter" / "restarting").
std::vector<std::string> sig_terms(const kb::Normalizer& norm, std::string_view text) {
  std::set<std::string> out;
  for (auto t : detail::terms(norm, text)) {
    if (t.size() > 6) {
      std::size_t n = 6;
      while (n > 0 && (static_cast<unsigned char>(t[n]) & 0xC0) == 0x80) --n;
      t.resize(n);
    }
    if (t.size() >= 3) out.insert(std::move(t));
  }
  return {out.begin(), out.end()};
}

std::string label_of(const model::DecisionAlternative& a) {
  if (!a.label.empty()) return a.label;
  return a.value.is_string() ? a.value.get<std::string>() : std::string();
}

Sig signature(const kb::Normalizer& norm, const Index& ix, const model::Decision& d) {
  Sig s;
  s.d = &d;
  s.date = detail::day(d.date);
  auto obs = ix.decision_obs(d);
  const model::DecisionAlternative* chosen = d.chosen();
  s.kept_open = chosen == nullptr;
  for (const auto& other : ix.ev.decisions) s.reversal |= other.superseded_by == d.id;
  std::string alts;
  std::vector<std::string> rejected_terms;
  for (const auto& a : d.alternatives) {
    alts += label_of(a) + " ; ";
    if (!a.chosen) rejected_terms = detail::set_union(rejected_terms, sig_terms(norm, label_of(a)));
  }
  std::string context;
  if (!obs.empty()) {
    const auto* o = obs.front();
    s.unit = o->unit;
    s.obs = o->id;
    auto it = ix.unit_obs.find(o->unit);
    if (it != ix.unit_obs.end()) {
      const auto& v = it->second;
      std::vector<const model::Observation*> before, after;
      for (auto* x : v) {
        if (x->ordinal < o->ordinal) before.push_back(x);
        if (x->ordinal > o->ordinal && owner(x->speaker)) after.push_back(x);
      }
      for (std::size_t i = before.size() > 2 ? before.size() - 2 : 0; i < before.size(); ++i) context += before[i]->text + " ";
      if (s.kept_open) {
        for (std::size_t i = 0; i < after.size() && i < 2; ++i) s.solution_text += after[i]->text + " ";
      }
    }
    for (auto* x : obs) s.solution_text = x->text + " " + s.solution_text;
  }
  if (chosen) s.solution_text += " " + label_of(*chosen);
  s.situation_text = context + " " + alts;
  if (s.kept_open) s.situation_text += " " + s.solution_text;
  s.sit = sig_terms(norm, s.situation_text);
  auto sol = sig_terms(norm, s.solution_text);
  // What the owner rejected describes the situation, not the solution.
  std::vector<std::string> chosen_terms = chosen ? sig_terms(norm, label_of(*chosen)) : std::vector<std::string>{};
  s.sol = detail::set_union(detail::set_minus(sol, rejected_terms), chosen_terms);
  return s;
}

// A decision deliberately kept open has no Decision record (a Decision has
// exactly one chosen alternative): it arrives as a `decides` claim whose
// value is {"kept_open": [alternatives]}. It is read as a decision whose
// solution is "keep the alternatives".
std::vector<Sig> signatures(const kb::Normalizer& norm, const Index& ix, std::deque<model::Decision>& kept_open) {
  std::vector<Sig> out;
  for (const auto& d : ix.ev.decisions) {
    if (d.status == model::DecisionStatus::Reverted) continue;
    auto s = signature(norm, ix, d);
    if (s.sol.empty() && s.sit.empty()) continue;
    out.push_back(std::move(s));
  }
  for (const auto& c : ix.ev.claims) {
    if (c.predicate != "decides" || ix.decision.count(c.id) || !detail::usable(c)) continue;
    const Json* alts = json::find(c.value, "kept_open");
    if (!alts || !alts->is_array()) continue;
    model::Decision d;
    d.id = c.id;
    d.subject = c.subject;
    d.date = ix.claim_date(c);
    for (const auto& a : *alts) {
      if (a.is_string()) d.alternatives.push_back(model::DecisionAlternative{a.get<std::string>(), "", Json(), {}, false});
    }
    kept_open.push_back(std::move(d));
    auto s = signature(norm, ix, kept_open.back());
    if (!s.sol.empty() || !s.sit.empty()) out.push_back(std::move(s));
  }
  std::sort(out.begin(), out.end(), [](const Sig& a, const Sig& b) {
    if (a.date != b.date) return a.date < b.date;
    return a.d->id < b.d->id;
  });
  return out;
}

struct Tau {
  double merge, apply, sol, sit;
};
Tau taus(const kb::Pack& pack) {
  return Tau{detail::threshold(pack, "operators", "merge_similarity", 0.14), detail::threshold(pack, "operators", "apply_similarity", 0.12),
             detail::threshold(pack, "operators", "solution_similarity", 0.1),
             detail::threshold(pack, "operators", "situation_similarity", 0.12)};
}

// Weighted overlap tempered by Jaccard: short signatures are not drowned by
// long ones, and one shared word does not make two long texts alike.
double sim(const std::vector<std::string>& a, const std::vector<std::string>& b, const detail::TermWeights& w) {
  return 0.5 * detail::woverlap(a, b, w) + 0.5 * detail::wjaccard(a, b, w);
}

double combined(const std::vector<std::string>& sit_a, const std::vector<std::string>& sol_a, const std::vector<std::string>& sit_b,
                const std::vector<std::string>& sol_b, const detail::TermWeights& w) {
  return 0.5 * sim(sit_a, sit_b, w) + 0.5 * sim(sol_a, sol_b, w);
}

std::vector<std::string> json_terms(const Json& basis, const char* key) {
  std::vector<std::string> out;
  if (const Json* v = json::find(basis, key); v && v->is_array()) {
    for (const auto& x : *v) {
      if (x.is_string()) out.push_back(x.get<std::string>());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

Json to_json_terms(const std::vector<std::string>& v) {
  Json a = Json::array();
  for (const auto& x : v) a.push_back(x);
  return a;
}

std::string lang_key(const kb::Normalizer& norm, const std::string& text) {
  return norm.guess_lang(text) == kb::Lang::Pl ? "pl" : "en";
}

std::string trim(std::string s) {
  auto b = s.find_first_not_of(" ;");
  auto e = s.find_last_not_of(" ;");
  if (b == std::string::npos) return "";
  return s.substr(b, e - b + 1);
}

}  // namespace

Result<std::vector<model::Operator>> mine_operators(const kb::Pack& pack, const Evidence& ev, const model::PriorFilter& priors) {
  kb::Normalizer norm(pack);
  Index ix(ev);
  Tau tau = taus(pack);
  std::deque<model::Decision> kept_open;
  auto sigs = signatures(norm, ix, kept_open);
  detail::TermWeights w;
  for (const auto& s : sigs) w.add(detail::set_union(s.sit, s.sol));

  // Seed operators (design operators, after the prior filter).
  std::vector<model::Operator> seeds;
  if (priors.enabled) {
    LOOM_TRY_ASSIGN(auto all, model::pack_operators(pack, priors));
    for (auto& o : all) {
      if (!o.is_rule()) seeds.push_back(std::move(o));
    }
  }
  std::vector<std::vector<std::string>> seed_terms, seed_sit, seed_sol;
  for (const auto& o : seeds) {
    std::vector<std::string> a, b;
    for (const auto& [l, x] : o.situation) a = detail::set_union(a, sig_terms(norm, x));
    for (const auto& [l, x] : o.solution) b = detail::set_union(b, sig_terms(norm, x));
    seed_sit.push_back(a);
    seed_sol.push_back(b);
    seed_terms.push_back(detail::set_union(a, b));
  }

  // Merge recurring decisions (single link, in time order).
  std::vector<int> group(sigs.size());
  for (std::size_t i = 0; i < sigs.size(); ++i) group[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int i) { return group[i] == i ? i : group[i] = find(group[i]); };
  if (std::getenv("LOOM_GEN_DEBUG")) {
    for (std::size_t i = 0; i < sigs.size(); ++i) {
      std::fprintf(stderr, "SIG %s %s sit=[", sigs[i].d->id.c_str(), sigs[i].date.c_str());
      for (auto& t : sigs[i].sit) std::fprintf(stderr, "%s ", t.c_str());
      std::fprintf(stderr, "] sol=[");
      for (auto& t : sigs[i].sol) std::fprintf(stderr, "%s ", t.c_str());
      std::fprintf(stderr, "]\n");
      for (std::size_t j = i + 1; j < sigs.size(); ++j)
        std::fprintf(stderr, "PAIR %s %s sit=%.3f sol=%.3f\n", sigs[i].d->id.c_str(), sigs[j].d->id.c_str(), sim(sigs[i].sit, sigs[j].sit, w), sim(sigs[i].sol, sigs[j].sol, w));
    }
  }
  for (std::size_t i = 0; i < sigs.size(); ++i) {
    for (std::size_t j = i + 1; j < sigs.size(); ++j) {
      // A reversal of an earlier decision is not a recurrence of it.
      bool reversal = sigs[i].d->superseded_by == sigs[j].d->id || sigs[j].d->superseded_by == sigs[i].d->id;
      bool same_shape = !reversal;
      if (same_shape && combined(sigs[i].sit, sigs[i].sol, sigs[j].sit, sigs[j].sol, w) >= tau.merge) {
        int a = find(static_cast<int>(i)), b = find(static_cast<int>(j));
        if (a != b) group[std::max(a, b)] = std::min(a, b);
      }
    }
  }
  std::map<int, std::vector<const Sig*>> groups;
  for (std::size_t i = 0; i < sigs.size(); ++i) groups[find(static_cast<int>(i))].push_back(&sigs[i]);

  // Principles justifying a decision: the principles of the evidence that
  // name it (or its rationale) as evidence.
  std::map<std::string, std::set<std::string>> principles_of;
  for (const auto& p : ev.principles) {
    for (const auto& e : p.evidence_for) principles_of[e].insert(p.id);
  }

  std::vector<model::Operator> out;
  for (const auto& [root, members] : groups) {
    const Sig& first = *members.front();
    std::vector<std::string> sit, sol;
    for (auto* m : members) {
      sit = detail::set_union(sit, m->sit);
      sol = detail::set_union(sol, m->sol);
    }
    model::Operator op;
    std::string sit_text = trim(first.situation_text), sol_text = trim(first.solution_text);
    if (sit_text.empty()) sit_text = sol_text;
    op.situation[lang_key(norm, sit_text)] = sit_text;
    op.solution[lang_key(norm, sol_text)] = sol_text;
    op.id = kb::stable_id("op_", sit_text + '\x1f' + sol_text);
    std::set<std::string> prins;
    for (auto* m : members) {
      model::Reference r;
      r.doc = m->unit;
      r.date = m->date;
      r.claim = m->d->id;
      r.observation = m->obs;
      r.note = m->d->chosen() ? label_of(*m->d->chosen()) : "kept open";
      op.examples.push_back(r);
      for (const auto& p : principles_of[m->d->id]) prins.insert(p);
      for (const auto& p : principles_of[m->obs]) prins.insert(p);
      for (const auto& p : m->d->principles) prins.insert(p);
    }
    // Prior link.
    int best = -1;
    double best_s = 0;
    for (std::size_t k = 0; k < seeds.size(); ++k) {
      double s = detail::woverlap(detail::set_union(sit, sol), seed_terms[k], w);
      if (s > best_s + 1e-12) {
        best_s = s;
        best = static_cast<int>(k);
      }
    }
    Json basis{{"situation_terms", to_json_terms(sit)}, {"solution_terms", to_json_terms(sol)},
               {"features", Json{{"kept_open", first.kept_open}, {"reversal", first.reversal}, {"alternatives", first.d->alternatives.size()}}}};
    Json decisions = Json::array();
    for (auto* m : members) decisions.push_back(m->d->id);
    basis["decisions"] = decisions;
    bool linked = best >= 0 && best_s >= 0.3;
    if (std::getenv("LOOM_GEN_DEBUG")) {
      std::fprintf(stderr, "LINK %s %s %.3f\n", first.d->id.c_str(), best >= 0 ? seeds[best].id.c_str() : "-", best_s);
    }
    if (linked) {
      const auto& seed = seeds[best];
      // The prior's own vocabulary generalises the solution class beyond
      // the words of this one decision.
      basis["situation_terms"] = to_json_terms(detail::set_union(sit, seed_sit[best]));
      basis["solution_terms"] = to_json_terms(detail::set_union(sol, seed_sol[best]));
      basis["prior"] = seed.id;
      basis["prior_overlap"] = best_s;
      op.sources.push_back(model::Reference{"loom/data/philosophy/operators.json", seed.id, model::earliest_source_date(seed.sources), "", "", "seed operator (prior)"});
      for (const auto& p : seed.principles) prins.insert(p);
    }
    op.principles.assign(prins.begin(), prins.end());
    // Success: the decisions that applied it; failure: other decisions in
    // the same situation that chose differently.
    op.success = static_cast<int>(members.size());
    for (const auto& s : sigs) {
      bool member = std::find(members.begin(), members.end(), &s) != members.end();
      if (member) continue;
      if (sim(sit, s.sit, w) >= std::max(tau.sit, 0.3) && sim(sol, s.sol, w) < tau.sol) ++op.failure;
    }
    double beta = (op.success + 1.0) / (op.success + op.failure + 2.0);
    op.confidence = detail::clamp01(beta * (linked ? 1.0 : 0.8));
    op.validation = (op.success >= 2 || (linked && op.success >= 1)) ? model::ValidationStatus::Supported : model::ValidationStatus::Candidate;
    op.origin = model::Origin::Archive;
    op.basis = basis;
    out.push_back(std::move(op));
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id == b.id; }), out.end());
  return out;
}

Result<std::vector<model::Prediction>> predict(const std::vector<model::Operator>& operators, const Evidence& ev, std::string_view cut) {
  auto pack = detail::builtin_pack();
  if (!pack) return Error(Errc::Internal, "predict: built-in pack unavailable");
  kb::Normalizer norm(*pack);
  Index ix(ev);
  Tau tau = taus(*pack);
  std::string c = cut.empty() ? ix.corpus_end : detail::day(cut);
  // Open situations at the cut: questions of a subject with no later
  // decision about that subject.
  std::vector<const model::Claim*> open;
  for (const auto& cl : ev.claims) {
    if (cl.predicate != "has_question" || !detail::usable(cl)) continue;
    std::string d = ix.claim_date(cl);
    if (!d.empty() && d > c) continue;
    bool resolved = false;
    for (const auto& dec : ev.decisions) resolved |= dec.subject == cl.subject && detail::day(dec.date) >= d && detail::day(dec.date) <= c;
    if (!resolved) open.push_back(&cl);
  }
  detail::TermWeights w;
  for (const auto& op : operators) w.add(detail::set_union(json_terms(op.basis, "situation_terms"), json_terms(op.basis, "solution_terms")));
  std::vector<model::Prediction> out;
  for (const auto& op : operators) {
    if (op.is_rule()) continue;
    auto sit = json_terms(op.basis, "situation_terms");
    auto sol = json_terms(op.basis, "solution_terms");
    auto text = [](const model::Text& t) { return t.count("en") ? t.at("en") : (t.empty() ? std::string() : t.begin()->second); };
    model::Prediction p;
    p.situation = text(op.situation);
    p.solution = text(op.solution);
    p.op = op.id;
    p.principles = op.principles;
    p.confidence = detail::clamp01(op.confidence);
    p.cut = c;
    p.features = Json{{"situation_terms", to_json_terms(sit)}, {"solution_terms", to_json_terms(sol)}, {"scope", "any subject"}};
    p.id = model::Prediction::make_id(op.id, p.situation, c);
    out.push_back(p);
    // Subject-specific: the operator applied to an open question at T.
    for (auto* q : open) {
      std::string qt = detail::value_text(ix, *q);
      if (qt.empty()) {
        for (const auto& s : q->assessment.support) qt += s.quote + " ";
      }
      auto qterms = sig_terms(norm, qt);
      double s = sim(sit, qterms, w);
      if (s < tau.sit) continue;
      model::Prediction ps = p;
      ps.situation = qt;
      ps.features = Json{{"situation_terms", to_json_terms(detail::set_union(sit, qterms))}, {"solution_terms", to_json_terms(sol)},
                         {"subject", q->subject}, {"question", q->id}, {"match", s}};
      ps.confidence = detail::clamp01(p.confidence * std::min(1.0, 0.5 + s));
      ps.id = model::Prediction::make_id(op.id, qt, c);
      out.push_back(std::move(ps));
    }
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id == b.id; }), out.end());
  return out;
}

Result<std::vector<model::Prediction>> evaluate_predictions(std::vector<model::Prediction> predictions, const Evidence& after_cut) {
  auto pack = detail::builtin_pack();
  if (!pack) return Error(Errc::Internal, "evaluate_predictions: built-in pack unavailable");
  kb::Normalizer norm(*pack);
  Index ix(after_cut);
  Tau tau = taus(*pack);
  std::deque<model::Decision> kept_open;
  auto sigs = signatures(norm, ix, kept_open);
  detail::TermWeights w;
  for (const auto& s : sigs) w.add(detail::set_union(s.sit, s.sol));
  for (const auto& p : predictions) {
    w.add(detail::set_union(json_terms(p.features, "situation_terms"), json_terms(p.features, "solution_terms")));
  }
  std::map<std::string, std::pair<bool, bool>> result;  // prediction -> (any holds, any violated)
  for (const auto& s : sigs) {
    if (!predictions.empty() && !predictions.front().cut.empty() && s.date <= predictions.front().cut) continue;
    int best = -1;
    double best_score = 0;
    for (std::size_t i = 0; i < predictions.size(); ++i) {
      const auto& p = predictions[i];
      // A subject-specific prediction only speaks about its subject.
      std::string subj = json::get_string(p.features, "subject");
      if (!subj.empty() && subj != s.d->subject) continue;
      double sc = combined(json_terms(p.features, "situation_terms"), json_terms(p.features, "solution_terms"), s.sit, s.sol, w);
      if (!subj.empty()) sc += 0.05;  // a targeted prediction beats the generic one of the same operator
      if (sc > best_score + 1e-12) {
        best_score = sc;
        best = static_cast<int>(i);
      }
    }
    if (best < 0 || best_score < tau.apply) continue;
    auto& p = predictions[best];
    double sol = sim(json_terms(p.features, "solution_terms"), s.sol, w);
    p.evaluated_against.push_back(s.d->id);
    auto& r = result[p.id];
    (sol >= tau.sol ? r.first : r.second) = true;
    p.features["evaluation"] = Json{{"decision", s.d->id}, {"score", best_score}, {"solution_similarity", sol}};
  }
  for (auto& p : predictions) {
    auto it = result.find(p.id);
    if (it == result.end()) continue;
    p.outcome = it->second.first ? model::CheckState::Holds : model::CheckState::Violated;
    std::sort(p.evaluated_against.begin(), p.evaluated_against.end());
  }
  return predictions;
}

}  // namespace loom::generalize
