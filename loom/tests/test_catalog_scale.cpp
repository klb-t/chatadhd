// catalog.h scan() memory bound on a ~1 GB corpus (R1: "multi-GB ChatGPT/
// Claude archives must be catalogued WITHOUT importing"). Generates the
// corpus with loom/tools/scale_archive.py (streaming, deterministic,
// gitignored output) into a temp dir, scans it, and asserts the process's
// peak RSS stays a small, bounded multiple of the sketch/read-chunk sizes
// rather than growing with the corpus (which would mean something read the
// whole file into memory).
//
// LONG TEST: generating the corpus is fast (~1s per 50MB, streaming), but
// scanning it is not: measured throughput is ~400-450 units/s (per-unit
// sketch build + alias matching dominates, not I/O -- indexes are used
// correctly for the dedup/version lookups, confirmed via EXPLAIN QUERY PLAN),
// so ~1GB (roughly half a million procedurally-generated conversations)
// takes on the order of 20-30 minutes. That per-unit cost, not I/O, is the
// realistic throughput bottleneck for a real multi-GB archive and is
// disclosed as a gap in the final report; this test's job is only to prove
// the *memory* bound, not throughput. Skipped by default; only runs with
// LOOM_RUN_SLOW_TESTS=1, e.g.:
//   LOOM_RUN_SLOW_TESTS=1 ./build/dev/loom_tests --test-suite=catalog_scale
#include <doctest/doctest.h>

#include <sys/resource.h>

#include <cstdlib>

#include "loom/catalog.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::catalog;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

fs::path repo_root() { return fs::path(LOOM_TEST_FIXTURES).parent_path().parent_path(); }

std::unique_ptr<Runtime> open_rt(const fs::path& dir) {
  RuntimeOptions o;
  o.data_dir = dir.string();
  o.start_workers = false;
  return unwrap(Runtime::open(o));
}

// Peak resident set size of this process so far, in bytes (Linux: ru_maxrss
// is in KiB; each ctest entry is its own fresh process, so this is the peak
// for exactly the test cases run in this invocation, not a residual from
// something else).
std::int64_t peak_rss_bytes() {
  struct rusage ru {};
  getrusage(RUSAGE_SELF, &ru);
  return static_cast<std::int64_t>(ru.ru_maxrss) * 1024;
}

bool slow_tests_enabled() { return std::getenv("LOOM_RUN_SLOW_TESTS") != nullptr; }

}  // namespace

TEST_SUITE("catalog_scale") {
  TEST_CASE("scan() over a ~1GB generated corpus keeps peak RSS bounded"
           * doctest::skip(!slow_tests_enabled()) * doctest::timeout(1800)) {
    fsutil::TempDir out_dir("loom_catalog_scale_");
    fs::path corpus = out_dir.path() / "conversations.json";
    fs::path script = repo_root() / "tools" / "scale_archive.py";
    REQUIRE(fs::exists(script));

    std::string cmd = "python3 \"" + script.string() + "\" --target-gb 1 --out \"" + corpus.string() + "\" >/dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    REQUIRE(rc == 0);
    std::error_code ec;
    auto size = fs::file_size(corpus, ec);
    REQUIRE(!ec);
    CAPTURE(size);
    REQUIRE(size > 900'000'000);  // close enough to the 1 GB target

    fsutil::TempDir data_dir;
    auto rt = open_rt(data_dir.path());
    Catalog cat(*rt, unwrap(rt->knowledge().pack()));

    ScanConfig cfg;
    cfg.sources = {corpus.string()};
    auto before = peak_rss_bytes();
    auto stats = unwrap(cat.scan(cfg));
    auto after = peak_rss_bytes();
    CAPTURE(json::dump(stats));
    CAPTURE(before);
    CAPTURE(after);

    CHECK(json::get_int(stats, "units") > 1000);   // the padded corpus is thousands of conversations
    CHECK(json::get_int(stats, "bytes") > 900'000'000);

    // Bounded: peak RSS stays under a few hundred MB regardless of the ~1GB
    // input (the per-element cap kElementByteCap is 32MB, the per-unit
    // sketch cap kSketchByteCap is 4MB, and only one element/chunk is ever
    // held at a time). 400 MB gives generous headroom over that bound for
    // allocator overhead, SQLite's page cache and the process baseline,
    // while still being far below the corpus size -- a whole-file read
    // would show up as RSS growing with the corpus (~1 GB+).
    constexpr std::int64_t kBoundBytes = 400LL * 1024 * 1024;
    CHECK(after < kBoundBytes);
  }
}
