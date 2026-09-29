// catalog.h evaluated against tests/fixtures/eval/synthetic_dev/ (a fully
// synthetic, structurally-isomorphic corpus with exact ground truth:
// 45 relevant conversations across 5 fictional projects, 5 noise traps that
// deliberately collide lexically with the REAL owner's own ambiguous
// aliases — "loom" (weaving), "watchdog" (a hardware timer), "agent" (real
// estate), "ADHD" (a medical diagnosis) — and 15 generic-noise
// conversations; see the fixture's own README.md).
//
// The self-profile a real user builds for THEIR OWN archive would know
// their own projects' aliases (here: the 5 fictional projects', taken
// straight from ground_truth.json's projects[].aliases, exactly as a real
// owner would type them into ProfileConfig.extra_terms or a data-dir
// overlay) while noise-trap rejection is exercised through the REAL,
// built-in pack's own ambiguous-alias context gates (profiles/self.json's
// "loom"/"watchdog"/"agent"/"ADHD" entries) -- which is exactly what the
// fixture's noise traps are built to test, per its own README.
//
// This development-set gate measures the complete configured selector,
// including corpus retrieval, expansion and linked-unit completion. Evaluate
// precision on the same labeled conversation population as recall; provider
// project and memory documents are reported separately, not mislabeled noise.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <set>

#include "loom/catalog.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::catalog;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

fs::path fixture_dir() { return fs::path(LOOM_TEST_FIXTURES) / "eval" / "synthetic_dev"; }

std::unique_ptr<Runtime> open_rt(const fs::path& dir) {
  RuntimeOptions o;
  o.data_dir = dir.string();
  o.start_workers = false;
  return unwrap(Runtime::open(o));
}

// The fictional persona's own project aliases (ground_truth.json
// projects[].aliases, copied verbatim): what a real owner running this
// pipeline over their own archive would configure for their own projects.
// Deliberately excludes the bare generic words the noise traps collide on
// ("watchdog", "loom", "pipeline", "agent", "ADHD"): those stay governed by
// the real pack's own ambiguous/negative-context entries, which is exactly
// what this fixture is testing.
const std::vector<std::string>& persona_aliases() {
  static const std::vector<std::string> kAliases = {
      "NoteFlow", "appka od notatek", "note-app", "ta apka z notatkami i czatem", "notatnik",
      "Reeltime", "generator reelsow", "ten pipeline do wideo", "reels-gen", "generator klipow",
      "Stroz", "Watchdog-skrypt", "ten pilnujacy skrypt", "CodeWatchdog", "stroz.py",
      "sprawa z Kwiatowej", "sprawa najmu", "sprawa z Zenonem", "sprawa o kaucje", "Lokatorka-sprawa",
      "Analog Ghosts", "plyta", "EP", "projekt muzyczny", "te nagrania",
  };
  return kAliases;
}

struct Metrics {
  int relevant_total = 0, relevant_selected = 0;
  int trap_total = 0, trap_selected = 0;
  int generic_total = 0, generic_selected = 0;
  int selected_total = 0;
  double recall() const { return relevant_total ? static_cast<double>(relevant_selected) / relevant_total : 0.0; }
  double precision() const { return selected_total ? static_cast<double>(relevant_selected) / selected_total : 0.0; }
  double trap_fpr() const { return trap_total ? static_cast<double>(trap_selected) / trap_total : 0.0; }
};

// Ranking quality is reported separately from selection (thresholds/rules):
// AUC over the labeled conversations (relevant vs trap + generic noise, ties
// count 1/2), recall@k and precision@k of the score-ordered list.
struct Ranked {
  std::string id;
  bool relevant = false;
  double score = 0.0;
};

double auc_of(const std::vector<Ranked>& v) {
  double pos = 0, wins = 0;
  for (const auto& p : v) {
    if (!p.relevant) continue;
    ++pos;
    for (const auto& n : v) {
      if (n.relevant) continue;
      wins += p.score > n.score ? 1.0 : (p.score == n.score ? 0.5 : 0.0);
    }
  }
  double neg = 0;
  for (const auto& n : v) neg += n.relevant ? 0 : 1;
  return pos > 0 && neg > 0 ? wins / (pos * neg) : 0.0;
}

// Number of relevant units among the k best (ties broken by id, so the
// figure is deterministic).
int hits_at(std::vector<Ranked> v, std::size_t k) {
  std::sort(v.begin(), v.end(), [](const Ranked& a, const Ranked& b) {
    return a.score != b.score ? a.score > b.score : a.id < b.id;
  });
  int n = 0;
  for (std::size_t i = 0; i < std::min(k, v.size()); ++i) n += v[i].relevant;
  return n;
}

}  // namespace

TEST_SUITE("catalog_eval") {
  TEST_CASE("synthetic_dev: recall/precision/noise-trap FP") {
    fsutil::TempDir data_dir;
    auto rt = open_rt(data_dir.path());
    Catalog cat(*rt, unwrap(rt->knowledge().pack()));

    ScanConfig scfg;
    scfg.sources = {(fixture_dir() / "chatgpt_export.zip").string(), (fixture_dir() / "claude_export.zip").string()};
    auto scan_stats = unwrap(cat.scan(scfg));
    CAPTURE(json::dump(scan_stats));
    REQUIRE(json::get_int(scan_stats, "units") == 68);  // 65 conversations + 2 projects + 1 memories doc

    ProfileConfig pcfg;
    pcfg.extra_terms = persona_aliases();
    unwrap(cat.build_profile(pcfg));

    auto score_stats = unwrap(cat.score(ScoreConfig{}));
    std::string run_id = json::get_string(score_stats, "run_id");
    unwrap(cat.select(run_id));

    Json truth = unwrap(json::parse(unwrap(fsutil::read_file(fixture_dir() / "ground_truth.json"))));
    const Json& units = truth["units"];

    std::map<std::string, bool> selected_by_ext;
    std::map<std::string, Json> diagnostics_by_ext;
    {
      auto lk = rt->db().lock();
      sql::Stmt st = unwrap(rt->db().conn().prepare(
          "SELECT u.ext_id, d.selected, s.score, s.label, s.features, d.decided_by FROM loom_cat_units u "
          "JOIN loom_cat_decisions d ON d.unit_id = u.id AND d.run_id = ? "
          "JOIN loom_cat_scores s ON s.unit_id = u.id AND s.run_id = d.run_id"));
      st.bind(1, run_id);
      while (true) {
        auto has = st.step();
        REQUIRE(has.has_value());
        if (!*has) break;
        selected_by_ext[st.get_text(0)] = st.get_int(1) != 0;
        diagnostics_by_ext[st.get_text(0)] = Json{{"score", st.get_double(2)}, {"label", st.get_text(3)},
                                                {"features", json::parse_or(st.get_text(4), Json::object())},
                                                {"decided_by", st.get_text(5)}};
      }
    }

    auto count_selected = [&](const Json& arr) {
      int n = 0;
      for (const auto& r : arr) {
        auto it = selected_by_ext.find(json::get_string(r, "conv_id"));
        if (it != selected_by_ext.end() && it->second) ++n;
      }
      return n;
    };

    Metrics m;
    m.relevant_total = static_cast<int>(units["relevant"].size());
    m.relevant_selected = count_selected(units["relevant"]);
    m.trap_total = static_cast<int>(units["noise_traps"].size());
    m.trap_selected = count_selected(units["noise_traps"]);
    m.generic_total = static_cast<int>(units["noise_generic"].size());
    m.generic_selected = count_selected(units["noise_generic"]);
    m.selected_total = m.relevant_selected + m.trap_selected + m.generic_selected;
    std::set<std::string> labeled;
    for (const char* category : {"relevant", "noise_traps", "noise_generic"}) {
      for (const auto& unit : units[category]) labeled.insert(json::get_string(unit, "conv_id"));
    }
    int auxiliary_selected = 0;
    for (const auto& [ext_id, selected] : selected_by_ext) {
      if (selected && !labeled.count(ext_id)) ++auxiliary_selected;
    }
    // LOOM_CATALOG_EVAL_VERBOSE=1 prints the misclassified conversations, =2
    // every labeled conversation (score distribution per class).
    if (const char* v = std::getenv("LOOM_CATALOG_EVAL_VERBOSE"); v && *v && std::string(v) != "0") {
      const bool all = std::string(v) == "2";
      for (const char* category : {"relevant", "noise_traps", "noise_generic"}) {
        for (const auto& unit : units[category]) {
          std::string id = json::get_string(unit, "conv_id");
          bool selected = selected_by_ext.count(id) && selected_by_ext.at(id);
          if (all || selected != (std::string(category) == "relevant"))
            MESSAGE(std::string(category) << " " << id << " selected=" << selected << " " << diagnostics_by_ext[id].dump());
        }
      }
    }

    // ── Ranking (independent of thresholds) and the lexical/semantic shadow
    // audit (R25/R27): C_lexical and C_semantic are the elements each
    // independent channel puts in the relevant band on its own; their
    // difference is a diagnostic, verified against the ground truth as
    // lexical rescue (truly relevant), lexical noise (trap/generic) or
    // undecided (no label). Neither channel gates the other in the final
    // score. ────────────────────────────────────────────────────────────
    std::vector<Ranked> rank_final, rank_lex, rank_sem;
    std::set<std::string> c_lexical, c_semantic;
    const double tau = 0.7;
    for (const char* category : {"relevant", "noise_traps", "noise_generic"}) {
      for (const auto& unit : units[category]) {
        std::string id = json::get_string(unit, "conv_id");
        if (!diagnostics_by_ext.count(id)) continue;
        const Json& d = diagnostics_by_ext[id];
        bool rel = std::string(category) == "relevant";
        double lex = json::get_number(d["features"], "lexical_score", 0.0);
        double sem = json::get_number(d["features"], "semantic_score", 0.0);
        rank_final.push_back({id, rel, json::get_number(d, "score")});
        rank_lex.push_back({id, rel, lex});
        rank_sem.push_back({id, rel, sem});
        if (lex >= tau) c_lexical.insert(id);
        if (sem >= tau) c_semantic.insert(id);
      }
    }
    for (const auto& [ext_id, selected] : selected_by_ext) {
      if (labeled.count(ext_id)) continue;
      const Json& d = diagnostics_by_ext[ext_id];
      if (json::get_number(d["features"], "lexical_score", 0.0) >= tau) c_lexical.insert(ext_id);
      if (json::get_number(d["features"], "semantic_score", 0.0) >= tau) c_semantic.insert(ext_id);
    }
    std::set<std::string> relevant_ids, noise_ids;
    for (const auto& r : units["relevant"]) relevant_ids.insert(json::get_string(r, "conv_id"));
    for (const char* category : {"noise_traps", "noise_generic"}) {
      for (const auto& r : units[category]) noise_ids.insert(json::get_string(r, "conv_id"));
    }
    int gap_rescue = 0, gap_noise = 0, gap_undecided = 0, sem_only_relevant = 0, sem_only_noise = 0;
    std::string rescue_ids;
    for (const auto& id : c_lexical) {
      if (c_semantic.count(id)) continue;  // diagnostic_gap = C_lexical - C_semantic
      if (relevant_ids.count(id)) { ++gap_rescue; rescue_ids += id + " "; }
      else if (noise_ids.count(id)) ++gap_noise;
      else ++gap_undecided;
    }
    for (const auto& id : c_semantic) {
      if (c_lexical.count(id)) continue;
      if (relevant_ids.count(id)) ++sem_only_relevant;
      else if (noise_ids.count(id)) ++sem_only_noise;
    }
    double auc_final = auc_of(rank_final), auc_lex = auc_of(rank_lex), auc_sem = auc_of(rank_sem);
    MESSAGE("ranking AUC final=" << auc_final << " lexical=" << auc_lex << " semantic=" << auc_sem);
    MESSAGE("ranking hits@20=" << hits_at(rank_final, 20) << "/20 hits@45=" << hits_at(rank_final, 45) << "/45"
                               << " (lexical hits@45=" << hits_at(rank_lex, 45) << ", semantic hits@45="
                               << hits_at(rank_sem, 45) << ")");
    MESSAGE("shadow: |C_lexical|=" << c_lexical.size() << " |C_semantic|=" << c_semantic.size()
                                    << " gap(lex-sem): rescue=" << gap_rescue << " noise=" << gap_noise
                                    << " undecided=" << gap_undecided << " [" << rescue_ids << "]"
                                    << " semantic-only: relevant=" << sem_only_relevant << " noise=" << sem_only_noise);

    MESSAGE("relevant: " << m.relevant_selected << "/" << m.relevant_total << " recall=" << m.recall());
    MESSAGE("traps: " << m.trap_selected << "/" << m.trap_total << " trap_fpr=" << m.trap_fpr());
    MESSAGE("noise_generic selected: " << m.generic_selected << "/" << m.generic_total);
    MESSAGE("selected conversations: " << m.selected_total << " precision=" << m.precision());

    MESSAGE("selected auxiliary documents (outside conversation labels): " << auxiliary_selected);
    MESSAGE("selector stats: " << score_stats.dump());

    // Trap rejection and precision are the safety-critical properties (R1:
    // never auto-import irrelevant content from a multi-GB archive) and are
    // evaluated alongside recall so a permissive selector cannot hide noise.
    CHECK(m.trap_fpr() <= 0.05);
    CHECK(m.precision() >= 0.75);
    // Keep the original recall gate until current measurements are recorded.
    CHECK(m.recall() >= 0.55);
    CHECK(m.generic_selected <= m.generic_total / 3);
  }
}

