// Code lineage on the repository's real history: the recovered snapshots
// history/chatadhd_v0.8.3 and history/chatadhd_v0.9.0 against the git
// history of the Python app. Expected (history/README.md,
// docs/history/ANALIZA_v0.8.3_v0.9.0.md): 0.8.3 <- 0.7.9 (46d0ba7) and
// 0.9.0 <- 0.7.10 (1e3fa2b, before the abspath fix ddcab9e). Skipped when
// git or the history is not available (shallow clones, Android).
#include <doctest/doctest.h>

#include <cstdlib>

#include "loom/resolve.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

const fs::path kRepo = fs::weakly_canonical(fs::path(LOOM_TEST_FIXTURES) / ".." / ".." / "..");

bool history_available() {
  if (!fs::is_directory(kRepo / "history" / "chatadhd_v0.8.3") || !fs::exists(kRepo / ".git")) return false;
  std::string cmd = "git -C '" + kRepo.string() + "' cat-file -e 46d0ba7^{commit} 2>/dev/null";
  return std::system(cmd.c_str()) == 0;
}

resolve::LineageResult lineage_of(const std::string& snapshot, std::vector<resolve::Revision>* hist_out = nullptr) {
  auto snap = unwrap(resolve::snapshot_revision(kRepo / "history" / snapshot));
  std::vector<std::string> paths;
  for (const auto& [p, c] : snap.files) paths.push_back(p);
  auto hist = unwrap(resolve::git_revisions(kRepo, paths));
  REQUIRE(hist.size() >= 20);
  auto r = unwrap(resolve::code_lineage(snap, hist));
  if (hist_out) *hist_out = std::move(hist);
  return r;
}

}  // namespace

TEST_SUITE("resolve_lineage") {
  TEST_CASE("history/chatadhd_v0.8.3 forked from 0.7.9 (46d0ba7)") {
    if (!history_available()) {
      MESSAGE("git history not available: skipped");
      return;
    }
    std::vector<resolve::Revision> hist;
    auto r = lineage_of("chatadhd_v0.8.3", &hist);
    MESSAGE("0.8.3 base " << r.base.substr(0, 7) << " confidence " << r.confidence << " votes " << r.votes[0].dump() << " / "
                          << r.votes[1].dump());
    CHECK(r.base.rfind("46d0ba7", 0) == 0);
    CHECK(r.confidence > 0.5);
    // engine/models.py is byte-identical to 0.7.9
    CHECK(r.per_file["engine/models.py"]["changed_lines"] == 0);
    auto claims = unwrap(resolve::lineage_claims(r, "e_project"));
    REQUIRE(claims.size() == 2);
    CHECK(claims[0].assessment.evidence == model::EvidenceClass::Derived);
    CHECK(claims[0].assessment.origin == model::Origin::Repo);
    CHECK(claims[0].value == r.base);
    auto fork = resolve::lineage_fork(r, hist, "e_project");
    CHECK(fork.kind == model::ForkKind::CodeLineage);
    CHECK(fork.base == r.base);
    REQUIRE(fork.sides.size() == 2);  // the snapshot and the main line after 0.7.9
  }

  TEST_CASE("history/chatadhd_v0.9.0 forked from 0.7.10 (1e3fa2b), before the abspath fix") {
    if (!history_available()) {
      MESSAGE("git history not available: skipped");
      return;
    }
    auto r = lineage_of("chatadhd_v0.9.0");
    MESSAGE("0.9.0 base " << r.base.substr(0, 7) << " confidence " << r.confidence << " votes " << r.votes[0].dump() << " / "
                          << r.votes[1].dump());
    CHECK(r.base.rfind("1e3fa2b", 0) == 0);
    // the fix commit (and its twin) must rank below the base
    for (const auto& v : r.votes) {
      std::string id = v["revision"];
      if (id.rfind("ddcab9e", 0) == 0 || id.rfind("287bdc1", 0) == 0) {
        CHECK(v["weight"].get<double>() < r.votes[0]["weight"].get<double>());
      }
    }
  }
}
