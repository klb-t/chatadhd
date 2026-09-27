// catalog.h: Bloom/MinHash/Sketch, the offset-tracking JSON array scanner,
// alias/context-gate matching, and an end-to-end scan -> profile -> score ->
// select -> import round trip on a small hand-built corpus (determinism,
// dedup/versioning, resumable checkpoints, targeted import + provenance).
// The synthetic_dev fixture corpus (recall/precision/noise-trap FP) is
// evaluated separately in test_catalog_eval.cpp.
#include <doctest/doctest.h>

#include <fstream>

#include "catalog/catalog_internal.h"
#include "loom/catalog.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::catalog;
using namespace loom::catalog::internal;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<Runtime> open_rt(const fs::path& dir) {
  RuntimeOptions o;
  o.data_dir = dir.string();
  o.start_workers = false;
  return unwrap(Runtime::open(o));
}

void write_file(const fs::path& p, std::string_view content) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f << content;
}

// A minimal ChatGPT-shaped export: one conversation mentioning "Zorblex"
// (a made-up alias), one that does not.
std::string chatgpt_fixture() {
  return R"([
{"id":"conv-a","title":"planning","create_time":1700000000,
 "current_node":"n2",
 "mapping":{
   "root":{"id":"root","parent":null,"children":["n1"],"message":null},
   "n1":{"id":"n1","parent":"root","children":["n2"],
         "message":{"id":"n1","author":{"role":"user"},
                    "content":{"content_type":"text","parts":["Zorblex needs a new storage layer."]},
                    "create_time":1700000001,"metadata":{}}},
   "n2":{"id":"n2","parent":"n1","children":[],
         "message":{"id":"n2","author":{"role":"assistant"},
                    "content":{"content_type":"text","parts":["Sure, SQLite works well for Zorblex."]},
                    "create_time":1700000002,"metadata":{}}}
 }},
{"id":"conv-b","title":"weekend","create_time":1700000100,
 "current_node":"m1",
 "mapping":{
   "root":{"id":"root","parent":null,"children":["m1"],"message":null},
   "m1":{"id":"m1","parent":"root","children":[],
         "message":{"id":"m1","author":{"role":"user"},
                    "content":{"content_type":"text","parts":["What should I cook this weekend?"]},
                    "create_time":1700000101,"metadata":{}}}
 }}
])";
}

}  // namespace

TEST_SUITE("catalog_sketch") {
  TEST_CASE("Bloom: never a false negative; sized from the fpr target") {
    Bloom b = Bloom::sized(100, 0.01);
    CHECK(b.bits > 0);
    for (int i = 0; i < 100; ++i) b.add("term" + std::to_string(i));
    for (int i = 0; i < 100; ++i) CHECK(b.maybe_contains("term" + std::to_string(i)));
    // base64 round trip preserves membership.
    Bloom b2 = Bloom::from_base64(b.to_base64(), b.bits, b.hashes);
    for (int i = 0; i < 100; ++i) CHECK(b2.maybe_contains("term" + std::to_string(i)));
  }

  TEST_CASE("MinHash: identical token streams -> jaccard 1; disjoint -> low") {
    std::vector<std::string> a = {"alpha", "beta", "gamma", "delta", "epsilon"};
    std::vector<std::string> b = {"zeta", "eta", "theta", "iota", "kappa"};
    MinHash ma = MinHash::build(a, 32);
    MinHash ma2 = MinHash::build(a, 32);
    MinHash mb = MinHash::build(b, 32);
    CHECK(ma.jaccard(ma2) == doctest::Approx(1.0));
    CHECK(ma.jaccard(mb) < 0.3);
    CHECK(ma.to_json() == ma2.to_json());
  }

  TEST_CASE("Sketch::build is deterministic and round-trips through JSON") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    kb::Normalizer norm(*unwrap(rt->knowledge().pack()));
    SketchParams p;
    std::string text = "Zorblex needs a new storage layer. Sure, SQLite works well for Zorblex.";
    Sketch s1 = Sketch::build(text, "", p, norm);
    Sketch s2 = Sketch::build(text, "", p, norm);
    CHECK(s1.to_json() == s2.to_json());
    CHECK(s1.n_chars == static_cast<std::int64_t>(text.size()));
    auto back = unwrap(Sketch::from_json(s1.to_json()));
    CHECK(back.to_json() == s1.to_json());
    CHECK(s1.topk_tf("zorblex") >= 2);  // stemmed/folded key, appears twice
  }
}

TEST_SUITE("catalog_alias") {
  TEST_CASE("ambiguous alias: context satisfied -> hit; negative context dominates -> trap; neither -> silent") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    kb::Normalizer norm(*unwrap(rt->knowledge().pack()));
    SelfProfile p;
    Json terms = Json::array();
    terms.push_back(Json{{"term", "Loom"},
                         {"key", norm.fold("Loom")},
                         {"class", "alias"},
                         {"weight", 3.0},
                         {"project", "loom"},
                         {"ambiguous", true},
                         {"requires_context", Json{{"any", Json::array({norm.fold("kernel"), norm.fold("sdk")})}, {"min", 1}}},
                         {"negative_context", Json::array({norm.fold("weaving"), norm.fold("krosno")})}});
    p.terms = terms;
    AliasIndex idx = AliasIndex::from_profile(p);

    auto satisfied = idx.find(norm.fold("I love the Loom kernel design."), 10);
    REQUIRE(satisfied.size() == 1);
    CHECK(!satisfied[0].trap);

    auto trap = idx.find(norm.fold("My grandmother's loom is great for weaving scarves."), 10);
    REQUIRE(trap.size() == 1);
    CHECK(trap[0].trap);

    auto neither = idx.find(norm.fold("I saw a loom at the museum yesterday."), 10);
    CHECK(neither.empty());
  }

  TEST_CASE("principle-class mentions are tagged key \"owner\" (R2: philosophy chats need no project alias)") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    kb::Normalizer norm(*unwrap(rt->knowledge().pack()));
    SelfProfile p;
    p.terms = Json::array({Json{{"term", "coding philosophy"},
                                {"key", norm.fold("coding philosophy")},
                                {"class", "principle"},
                                {"weight", 1.5},
                                {"project", ""},
                                {"ambiguous", false}}});
    AliasIndex idx = AliasIndex::from_profile(p);
    auto hits = idx.find(norm.fold("Let's talk about coding philosophy today."), 10);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].kind == "principle");
    CHECK(hits[0].key == "owner");
  }
}

TEST_SUITE("catalog_offset_scanner") {
  TEST_CASE("recovers exact byte ranges across arbitrary chunk boundaries") {
    std::string doc = R"([{"a":1,"s":"x,y{}"},  42,"bare string", {"nested":{"k":[1,2,3]}}])";
    for (std::size_t chunk_size : {1u, 3u, 7u, 64u, 4096u}) {
      OffsetArrayScanner sc;
      std::vector<std::string> elements;
      std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
      for (std::size_t i = 0; i < doc.size(); i += chunk_size) {
        std::string_view chunk(doc.data() + i, std::min(chunk_size, doc.size() - i));
        sc.feed(chunk, [&](std::string_view el, std::int64_t b, std::int64_t e) {
          elements.emplace_back(el);
          ranges.emplace_back(b, e);
          return true;
        });
      }
      REQUIRE(elements.size() == 4);
      CHECK(!sc.not_array());
      for (std::size_t i = 0; i < elements.size(); ++i) {
        auto [b, e] = ranges[i];
        REQUIRE(b >= 0);
        REQUIRE(e <= static_cast<std::int64_t>(doc.size()));
        std::string_view slice(doc.data() + b, static_cast<std::size_t>(e - b));
        CHECK(slice == elements[i]);
      }
    }
  }

  TEST_CASE("a top-level object is reported as not_array immediately") {
    OffsetArrayScanner sc;
    sc.feed(R"({"conversations_memory":"x"})", [](std::string_view, std::int64_t, std::int64_t) { return true; });
    CHECK(sc.not_array());
  }
}

TEST_SUITE("catalog_pipeline") {
  TEST_CASE("scan -> profile -> score -> select -> import: recall on the alias hit, dedup on rescan, provenance") {
    fsutil::TempDir data_dir;
    fsutil::TempDir src_dir;
    write_file(src_dir.path() / "conversations.json", chatgpt_fixture());

    auto rt = open_rt(data_dir.path());
    auto pack = unwrap(rt->knowledge().pack());
    Catalog cat(*rt, pack);

    ScanConfig scfg;
    scfg.sources = {src_dir.path().string()};
    auto s1 = unwrap(cat.scan(scfg));
    CHECK(json::get_int(s1, "units") == 2);
    CHECK(json::get_int(s1, "new") == 2);
    CHECK(json::get_int(s1, "unchanged") == 0);

    // Re-scanning the same unchanged source is idempotent (checkpoint skip).
    auto s2 = unwrap(cat.scan(scfg));
    CHECK(json::get_int(s2, "units") == 0);

    ProfileConfig pcfg;
    pcfg.extra_terms = {"Zorblex"};
    auto profile = unwrap(cat.build_profile(pcfg));
    bool has_zorblex = false;
    for (const auto& t : profile.terms) {
      if (json::get_string(t, "term") == "Zorblex") has_zorblex = true;
    }
    CHECK(has_zorblex);

    auto score_stats = unwrap(cat.score(ScoreConfig{}));
    std::string run_id = json::get_string(score_stats, "run_id");
    CHECK(json::get_int(score_stats, "relevant") + json::get_int(score_stats, "candidate") >= 1);

    auto decisions = unwrap(cat.select(run_id));
    REQUIRE(decisions.size() == 2);
    int selected = 0;
    std::string selected_unit;
    for (auto& d : decisions) {
      if (d.selected) {
        ++selected;
        selected_unit = d.unit_id;
      }
    }
    CHECK(selected == 1);  // only the Zorblex conversation clears the bar

    // preview()/read_unit() verify against content_hash.
    auto pv = unwrap(cat.preview(selected_unit));
    CHECK(pv["verified"] == true);

    ImportOptions iopts;
    iopts.run_id = run_id;
    auto imp = unwrap(cat.import_selected(iopts));
    CHECK(json::get_int(imp, "imported") == 1);
    REQUIRE(imp["conversations"].size() == 1);
    std::string conv_id = imp["conversations"][0].get<std::string>();
    auto msgs = unwrap(rt->db().get_msgs(conv_id, true));
    CHECK(msgs.size() == 2);
    auto prov = unwrap(rt->provenance().for_subject(conv_id));
    REQUIRE(!prov.empty());
    CHECK(prov[0].transform == "catalog.import@1");

    // Re-importing is idempotent (loom_cat_imports dedup).
    auto imp2 = unwrap(cat.import_selected(iopts));
    CHECK(json::get_int(imp2, "imported") == 0);
    CHECK(json::get_int(imp2, "skipped") == 1);
  }

  TEST_CASE("dry_run reports without writing conversations") {
    fsutil::TempDir data_dir;
    fsutil::TempDir src_dir;
    write_file(src_dir.path() / "conversations.json", chatgpt_fixture());
    auto rt = open_rt(data_dir.path());
    Catalog cat(*rt, unwrap(rt->knowledge().pack()));
    ScanConfig scfg;
    scfg.sources = {src_dir.path().string()};
    unwrap(cat.scan(scfg));
    ProfileConfig pcfg;
    pcfg.extra_terms = {"Zorblex"};
    unwrap(cat.build_profile(pcfg));
    auto sc = unwrap(cat.score(ScoreConfig{}));
    unwrap(cat.select(json::get_string(sc, "run_id")));
    ImportOptions iopts;
    iopts.dry_run = true;
    auto imp = unwrap(cat.import_selected(iopts));
    CHECK(json::get_int(imp, "imported") == 1);
    CHECK(unwrap(rt->db().list_convs(10)).empty());
  }
}
