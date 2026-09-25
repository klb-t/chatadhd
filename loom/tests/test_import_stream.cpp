// Bounded-memory streaming: a ~200 MB generated ChatGPT-style export must
// import without loading the file into memory (JsonArrayStreamer /
// _iter_json_elements parity). Peak RSS is read from /proc/self/status
// (VmHWM), matching what the task asks for directly rather than estimating.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

// AddressSanitizer instruments every allocation with redzones and keeps its
// own shadow memory, so process RSS under `asan`/`tsan` presets is many
// times larger than under a plain build and is not a meaningful proxy for
// "did we load the whole file". The streaming behaviour itself (this test's
// element-count and content assertions) is still fully exercised under the
// sanitizers; only the numeric RSS bound is dev/release-preset-specific.
#if defined(__SANITIZE_ADDRESS__)
constexpr bool kSanitizerBuild = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
constexpr bool kSanitizerBuild = true;
#else
constexpr bool kSanitizerBuild = false;
#endif
#else
constexpr bool kSanitizerBuild = false;
#endif

// Peak resident set size ever reached by this process, in kB (Linux only;
// returns nullopt elsewhere so the test degrades to "ran without OOMing").
std::optional<long> vm_hwm_kb() {
#if defined(__linux__)
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("VmHWM:", 0) == 0) {
      long kb = 0;
      if (std::sscanf(line.c_str(), "VmHWM: %ld kB", &kb) == 1) return kb;
    }
  }
#endif
  return std::nullopt;
}

std::string repeat_to(std::size_t target_len, std::string_view seed) {
  std::string out;
  out.reserve(target_len + seed.size());
  while (out.size() < target_len) out += seed;
  out.resize(target_len);
  return out;
}

// Writes a ~`target_bytes` ChatGPT-conversations.json-shaped array to `path`
// without ever holding the whole document in memory, and returns how many
// conversation elements it wrote.
std::size_t generate_large_chatgpt_export(const std::filesystem::path& path, std::uint64_t target_bytes) {
  std::string u1 = repeat_to(4000, "the quick brown fox jumps over the lazy dog ");
  std::string a1 = repeat_to(4000, "SHA-256 content addressing keeps raw sources immutable ");
  std::string u2 = repeat_to(4000, "does the kernel stay bounded in memory while streaming ");
  std::string a2 = repeat_to(4000, "yes, one element at a time via JsonArrayStreamer ");

  std::ofstream out(path, std::ios::binary);
  REQUIRE(out.good());
  out << "[";
  std::size_t n = 0;
  bool first = true;
  while (static_cast<std::uint64_t>(out.tellp()) < target_bytes) {
    if (!first) out << ",";
    first = false;
    out << R"({"title":"Generated )" << n << R"(","mapping":{)"
        << R"("root":{"id":"root","parent":null,"children":["u1"],"message":null},)"
        << R"("u1":{"id":"u1","parent":"root","children":["a1"],)"
        << R"("message":{"author":{"role":"user"},"content":{"parts":[")" << u1 << n << R"("]}}},)"
        << R"("a1":{"id":"a1","parent":"u1","children":["u2"],)"
        << R"("message":{"author":{"role":"assistant"},"content":{"parts":[")" << a1 << n << R"("]}}},)"
        << R"("u2":{"id":"u2","parent":"a1","children":["a2"],)"
        << R"("message":{"author":{"role":"user"},"content":{"parts":[")" << u2 << n << R"("]}}},)"
        << R"("a2":{"id":"a2","parent":"u2","children":[],)"
        << R"("message":{"author":{"role":"assistant"},"content":{"parts":[")" << a2 << n << R"("]}}})"
        << "}}";
    ++n;
  }
  out << "]";
  out.close();
  return n;
}

}  // namespace

TEST_SUITE("import.stream") {
  TEST_CASE("importing a ~200MB generated JSON export keeps peak RSS bounded"
           * doctest::timeout(300)) {
    fsutil::TempDir td("loom_import_mem_");
    std::filesystem::path json_path = td.path() / "huge_conversations.json";

    constexpr std::uint64_t kTargetBytes = 200ull * 1000 * 1000;
    std::size_t written = generate_large_chatgpt_export(json_path, kTargetBytes);
    REQUIRE(written > 100);
    auto file_size = std::filesystem::file_size(json_path);
    REQUIRE(file_size >= kTargetBytes);
    INFO("generated " << written << " conversations, " << file_size << " bytes");

    DbOptions dbopts;
    dbopts.enable_fts = false;
    auto db = open_db(td.path() / "d.db", dbopts);
    EventBus bus;
    ConversationImporter imp(*db, bus);  // no BlobStore: isolates the JSON-streaming memory bound

    auto before = vm_hwm_kb();

    ImportOptions opts;
    opts.stream_threshold_bytes = 5'000'000;  // Python's 5 MB threshold; file is ~40x that
    auto r = unwrap(imp.import_file(json_path, opts));
    CHECK(r.format == "json");
    CHECK(r.conversations.size() == written);

    auto after = vm_hwm_kb();
    if (before && after) {
      long delta_kb = *after - *before;
      INFO("VmHWM before=" << *before << "kB after=" << *after << "kB delta=" << delta_kb << "kB");
      if (kSanitizerBuild) {
        // Sanitizer redzones/shadow memory dominate RSS; only assert we
        // stayed well under "the whole 200MB file plus a full parsed DOM of
        // it, all inflated by instrumentation" rather than a tight bound.
        WARN("sanitizer build: using a loose RSS bound (redzones inflate real usage)");
        CHECK(delta_kb < 1'500'000);
      } else {
        // The file is ~200MB (200,000 kB); a correct streaming import
        // touches one element (~16KB) at a time plus fixed-size I/O
        // buffers, so the RSS growth caused by this call must stay a small
        // fraction of the file size. A non-streaming (json::parse the whole
        // file) importer would grow RSS by several times the file size.
        CHECK(delta_kb < 60'000);   // < 60MB growth for a 200MB input (observed: ~7MB)
        CHECK(*after < 150'000);    // absolute cap: well under "load it all + parse it all"
      }
    } else {
      WARN("VmHWM not available on this platform; skipped the RSS bound check");
    }

    // Spot-check a couple of conversations to make sure streaming didn't
    // corrupt content at chunk boundaries.
    auto msgs_first = unwrap(db->get_msgs(r.conversations.front().id, true));
    REQUIRE(msgs_first.size() == 4);
    CHECK(msgs_first[0].role == "user");
    CHECK(msgs_first[0].text.find("the quick brown fox") == 0);
    CHECK(msgs_first[0].text.substr(msgs_first[0].text.size() - 1) == "0");

    auto msgs_last = unwrap(db->get_msgs(r.conversations.back().id, true));
    REQUIRE(msgs_last.size() == 4);
    CHECK(msgs_last[3].role == "assistant");
  }
}
