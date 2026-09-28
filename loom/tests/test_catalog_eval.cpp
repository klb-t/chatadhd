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
    if (const char* v = std::getenv("LOOM_CATALOG_EVAL_VERBOSE"); v && *v && std::string(v) != "0") {
      for (const char* category : {"relevant", "noise_traps", "noise_generic"}) {
        for (const auto& unit : units[category]) {
          std::string id = json::get_string(unit, "conv_id");
          bool selected = selected_by_ext.count(id) && selected_by_ext.at(id);
          if (selected != (std::string(category) == "relevant"))
            MESSAGE(std::string(category) << " " << id << " selected=" << selected << " " << diagnostics_by_ext[id].dump());
        }
      }
    }

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

