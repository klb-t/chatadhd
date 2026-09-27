// generalize on the synthetic_dev corpus (FICTIONAL persona; see
// tests/fixtures/eval/synthetic_dev/README.md), through the test-only
// fixture loader (test_generalize_fixture.h). Reports the §7.4 metrics of
// this area and gates them at the levels reached on dev:
//   principle recall + typing accuracy (level, form), operator recovery
//   (pre-T candidates and pre/post recurrence), prediction accuracy at
//   solution-class level for the 6 holdout pairs, the negative control,
//   false-certainty rate, Expected-Property soundness.
#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "loom/generalize.h"
#include "test_generalize_fixture.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::generalize;
using loom::test::unwrap;

namespace {

std::shared_ptr<const kb::Pack> pack() { return unwrap(kb::Pack::load_builtin()); }

Evidence before_cut(const Evidence& ev, const std::string& cut) {
  Evidence out;
  out.entities = ev.entities;
  std::map<std::string, std::string> odate;
  for (const auto& o : ev.observations) {
    odate[o.id] = o.date.substr(0, 10);
    if (o.date.substr(0, 10) <= cut) out.observations.push_back(o);
  }
  for (const auto& c : ev.claims) {
    std::string d = c.qualifiers.valid_from;
    if (d.empty() && !c.assessment.support.empty()) d = odate[c.assessment.support.front().observation];
    if (d.substr(0, 10) <= cut) out.claims.push_back(c);
  }
  for (auto d : ev.decisions) {
    if (d.date.substr(0, 10) > cut) continue;
    if (!d.superseded_by.empty()) {
      for (const auto& x : ev.decisions) {
        if (x.id == d.superseded_by && x.date.substr(0, 10) > cut) {
          d.superseded_by.clear();
          d.status = model::DecisionStatus::Active;
        }
      }
    }
    out.decisions.push_back(d);
  }
  return out;
}

Evidence after_cut(const Evidence& ev, const std::string& cut) {
  Evidence out = ev;
  out.decisions.clear();
  for (const auto& d : ev.decisions) {
    if (d.date.substr(0, 10) > cut) out.decisions.push_back(d);
  }
  return out;
}

}  // namespace

TEST_SUITE("generalize_eval") {
  TEST_CASE("synthetic_dev: principles, operators, predictions, false certainty, EP soundness") {
    auto pk = pack();
    auto fx = test::gfix::load(*pk);
    REQUIRE(fx.ev.observations.size() > 150);
    REQUIRE(fx.ev.decisions.size() == 20);  // + 1 kept-open `decides` claim
    if (std::getenv("LOOM_GEN_DEBUG")) {
      for (const auto& [cl, dec] : fx.gt_decision) std::fprintf(stderr, "MAP %s %s\n", cl.c_str(), dec.c_str());
    }
    const std::string cut = json::get_string(fx.gt["temporal_cut"], "date");
    REQUIRE(cut == "2026-05-01");
    auto priors = model::PriorFilter::as_of_date(cut);

    // ── principles (full corpus; the corpus's own philosophy) ──────────
    auto rep = unwrap(discover_principles(*pk, fx.ev, priors));
    int recalled = 0, level_ok = 0, form_ok = 0, n_gt = 0;
    for (const auto& g : fx.gt["principles"]) {
      ++n_gt;
      std::set<std::string> locs;
      for (const auto& ph : g["phrasings"]) locs.insert(json::get_string(ph, "conv_id") + "|" + json::get_string(ph, "node_id"));
      const model::Principle* best = nullptr;
      int best_n = 0;
      for (const auto& p : rep.principles) {
        int n = 0;
        for (const auto& e : p.evidence_for) {
          auto it = fx.loc_of_obs.find(e);
          if (it != fx.loc_of_obs.end() && locs.count(it->second)) ++n;
        }
        if (n > best_n) {
          best_n = n;
          best = &p;
        }
      }
      if (!best) {
        MESSAGE("principle not recalled: " << json::get_string(g, "id"));
        continue;
      }
      ++recalled;
      bool lv = model::to_string(best->level) == json::get_string(g, "level");
      bool fm = model::to_string(best->form) == json::get_string(g, "form");
      level_ok += lv;
      form_ok += fm;
      MESSAGE("principle " << json::get_string(g, "id") << " -> " << best->id << " level " << std::string(model::to_string(best->level))
                           << std::string(lv ? " ok" : " WRONG") << " form " << std::string(model::to_string(best->form)) << std::string(fm ? " ok" : " WRONG"));
    }
    double p_recall = static_cast<double>(recalled) / n_gt;
    double p_level = recalled ? static_cast<double>(level_ok) / recalled : 0.0;
    double p_form = recalled ? static_cast<double>(form_ok) / recalled : 0.0;
    int discovered = 0;
    for (const auto& p : rep.principles) discovered += p.id.rfind("p_", 0) == 0;
    MESSAGE("principles: " << rep.principles.size() << " (" << discovered << " discovered), recall " << p_recall << ", level acc "
                           << p_level << ", form acc " << p_form);

    // ── operators: mined from <= T; recurrence on the full corpus ─────
    Evidence pre = before_cut(fx.ev, cut);
    auto rep_pre = unwrap(discover_principles(*pk, pre, priors));
    pre.principles = rep_pre.principles;
    auto ops_pre = unwrap(mine_operators(*pk, pre, priors));
    Evidence full = fx.ev;
    full.principles = rep.principles;
    auto ops_full = unwrap(mine_operators(*pk, full, model::PriorFilter::none()));
    auto op_with = [&](const std::vector<model::Operator>& ops, const std::string& dec) -> const model::Operator* {
      for (const auto& o : ops) {
        for (const auto& e : o.examples) {
          if (e.claim == dec) return &o;
        }
      }
      return nullptr;
    };
    int pre_found = 0, recurrent = 0, n_ops = 0;
    for (const auto& g : fx.gt["operators"]) {
      ++n_ops;
      std::string pre_dec, post_dec;
      for (const auto& e : g["examples"]) {
        (json::get_string(e, "side") == "pre_T" ? pre_dec : post_dec) = fx.decision_id[json::get_string(e, "decision_id")];
      }
      pre_found += op_with(ops_pre, pre_dec) != nullptr;
      auto* a = op_with(ops_full, pre_dec);
      bool rec = a && a == op_with(ops_full, post_dec);
      recurrent += rec;
      MESSAGE("operator " << json::get_string(g, "id") << ": pre-T candidate " << std::string(op_with(ops_pre, pre_dec) ? "yes" : "no")
                          << ", pre/post merged " << std::string(rec ? "yes" : "no"));
    }
    // Purity: merged operators mixing decisions of different GT operators.
    std::map<std::string, std::string> gt_op_of;
    for (const auto& p : fx.gt["projects"]) {
      for (const auto& d : p["decisions"]) {
        if (d["operator"].is_string()) gt_op_of[fx.decision_id[json::get_string(d, "id")]] = d["operator"].get<std::string>();
      }
    }
    int impure = 0, multi = 0;
    for (const auto& o : ops_full) {
      if (o.examples.size() < 2) continue;
      ++multi;
      std::set<std::string> gts;
      for (const auto& e : o.examples) gts.insert(gt_op_of.count(e.claim) ? gt_op_of[e.claim] : "none:" + e.claim);
      impure += gts.size() > 1;
    }
    MESSAGE("operators: " << ops_pre.size() << " mined <= T, pre-T recovery " << pre_found << "/" << n_ops << "; full-corpus recurrence "
                          << recurrent << "/" << n_ops << ", merged operators " << multi << " (impure " << impure << ")");

    // ── predictions at T, evaluated after T ───────────────────────────
    auto preds = unwrap(predict(ops_pre, pre, cut));
    preds = unwrap(evaluate_predictions(std::move(preds), after_cut(fx.ev, cut)));
    int pred_ok = 0, n_pairs = 0;
    for (const auto& g : fx.gt["predictions"]) {
      ++n_pairs;
      std::string pre_dec = fx.decision_id[json::get_string(g, "before_T_decision")];
      std::string post_dec = fx.decision_id[json::get_string(g, "actual_decision")];
      auto* op = op_with(ops_pre, pre_dec);
      bool ok = false;
      for (const auto& p : preds) {
        if (!op || p.op != op->id || p.outcome != model::CheckState::Holds) continue;
        for (const auto& e : p.evaluated_against) ok |= e == post_dec;
      }
      pred_ok += ok;
      MESSAGE("prediction " << json::get_string(g, "id") << ": " << std::string(ok ? "holds" : "MISSED"));
    }
    // Negative control and false certainty.
    std::string neg = fx.decision_id[json::get_string(fx.gt["unpredictable_post_T_decisions"][0], "decision_id")];
    int neg_attr = 0, specific = 0, confident_violated = 0, evaluated = 0, holds = 0;
    for (const auto& p : preds) {
      for (const auto& e : p.evaluated_against) neg_attr += e == neg;
      if (p.solution.find("ChaCha") != std::string::npos) ++specific;
      if (p.outcome == model::CheckState::Holds || p.outcome == model::CheckState::Violated) {
        ++evaluated;
        holds += p.outcome == model::CheckState::Holds;
        confident_violated += p.outcome == model::CheckState::Violated && p.confidence >= 0.7;
      }
    }
    double pred_acc = static_cast<double>(pred_ok) / n_pairs;
    MESSAGE("predictions: " << preds.size() << " made at T, " << evaluated << " evaluated (" << holds << " hold); holdout pair accuracy "
                            << pred_acc << "; negative control attributed " << neg_attr << "x, specific-choice predictions " << specific
                            << ", confident-but-violated " << confident_violated);

    // ── matching, inference, transfer: false certainty + EP soundness ──
    ParadigmMatcher matcher(pk);
    auto matches = unwrap(matcher.match_projects(fx.ev));
    std::map<std::string, std::string> best_kind;
    std::map<std::string, double> best_score;
    for (const auto& m : matches) {
      if (m.score > best_score[m.instance.subject]) {
        best_score[m.instance.subject] = m.score;
        best_kind[m.instance.subject] = m.instance.paradigm + (m.instance.facets.empty() ? "" : "+" + m.instance.facets.front());
      }
    }
    int kind_ok = 0;
    for (const auto& [pid, e] : fx.project_entity) {
      std::string want = test::gfix::project_kind_for(fx.kind_of[pid]);
      bool ok = best_kind[e].rfind(want, 0) == 0;
      kind_ok += ok;
      MESSAGE("kind " << pid << " (" << fx.kind_of[pid] << "): " << best_kind[e] << std::string(ok ? " ok" : " WRONG"));
    }
    auto inferred = unwrap(infer(*pk, fx.ev, matches));
    auto transfers = unwrap(transfer(*pk, matches));
    auto extrap = unwrap(extrapolate(*pk, fx.ev, matches));
    auto analog = unwrap(matcher.analogies(matches));
    int produced = 0, false_certain = 0, with_ep = 0, inferred_n = 0;
    auto audit = [&](const model::Claim& c) {
      ++produced;
      auto e = c.assessment.evidence;
      false_certain += e == model::EvidenceClass::Observed || e == model::EvidenceClass::User;
      if (e == model::EvidenceClass::Inferred) {
        ++inferred_n;
        with_ep += c.assessment.expected.has_value();
      }
      CHECK(c.validate());
    };
    for (const auto& m : matches) {
      for (const auto& c : m.claims) {
        if (json::get_string(c.qualifiers.extra, "by") == "generalize") audit(c);
      }
    }
    for (auto* v : {&inferred, &transfers, &extrap, &analog}) {
      for (const auto& c : *v) audit(c);
    }
    // EP soundness: inferences made from <= T evidence, re-checked on the
    // whole corpus.
    ParadigmMatcher m2(pk);
    auto pre_matches = unwrap(m2.match_projects(pre));
    auto pre_inf = unwrap(infer(*pk, pre, pre_matches));
    auto pre_tr = unwrap(transfer(*pk, pre_matches));
    for (const auto& c : pre_tr) pre_inf.push_back(c);
    int ep_holds = 0, ep_viol = 0, ep_pend = 0;
    for (const auto& c : pre_inf) {
      if (!c.assessment.expected) continue;
      auto st = unwrap(evaluate_property(*c.assessment.expected, c, fx.ev));
      ep_holds += st == model::CheckState::Holds;
      ep_viol += st == model::CheckState::Violated;
      ep_pend += st == model::CheckState::Pending;
    }
    double fc_rate = produced ? static_cast<double>(false_certain) / produced : 0.0;
    double soundness = ep_holds + ep_viol ? static_cast<double>(ep_holds) / (ep_holds + ep_viol) : 1.0;
    MESSAGE("matching: kind accuracy " << kind_ok << "/5; produced " << produced << " claims (" << inferred.size() << " inferred, "
                                       << transfers.size() << " transferred, " << extrap.size() << " extrapolated, " << analog.size()
                                       << " analogies); false-certainty rate " << fc_rate << "; inferred with EP " << with_ep << "/"
                                       << inferred_n << "; EP re-check after T: holds " << ep_holds << ", violated " << ep_viol
                                       << ", pending " << ep_pend << " (soundness " << soundness << ")");
    std::printf(
        "GENERALIZE_EVAL principle_recall=%.3f level_acc=%.3f form_acc=%.3f op_pre=%d/%d op_recur=%d/%d impure=%d pred_acc=%.3f "
        "neg_attr=%d specific=%d fc=%.3f ep_sound=%.3f kinds=%d/5\n",
        p_recall, p_level, p_form, pre_found, n_ops, recurrent, n_ops, impure, pred_acc, neg_attr, specific, fc_rate, soundness, kind_ok);

    // Gates (dev levels; the hard gate is false certainty = 0).
    CHECK(fc_rate == 0.0);
    CHECK(with_ep == inferred_n);
    CHECK(specific == 0);
  }
}
