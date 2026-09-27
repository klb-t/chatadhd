// knowledge.h: the knowledge pipeline as TaskEngine stages. With the area
// stubs a run stops at the first stage with not_implemented; with injected
// stage functions it caches by input hash (pack hash + config + upstream
// output), re-runs dependents when the config changes, and pauses/resumes.
#include <doctest/doctest.h>

#include <atomic>

#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::knowledge;
using loom::test::unwrap;

namespace {
std::unique_ptr<Runtime> open_rt(const std::filesystem::path& dir) {
  RuntimeOptions o;
  o.data_dir = dir.string();
  o.start_workers = false;
  return unwrap(Runtime::open(o));
}

// A fake stage: writes one entity per call and returns a content hash.
StageFn fake(const std::string& name, std::atomic<int>& calls) {
  return [name, &calls](StageContext& c) -> Result<Json> {
    ++calls;
    std::string in = json::get_string(c.input, "output");
    model::Entity e;
    e.kind = "concept";
    e.canonical_key = name;
    e.id = model::Entity::make_id(e.kind, e.canonical_key);
    e.label = name;
    LOOM_TRY(c.store.put_entities(c.run, {e}));
    return Json{{"output", Sha256::hex(name + "|" + in + "|" + c.pack->hash())}, {"stats", Json{{"stage", name}}}};
  };
}
}  // namespace

TEST_SUITE("knowledge") {
  TEST_CASE("config: unknown keys and stages are rejected; stages run in pipeline order") {
    CHECK(!KnowledgeConfig::from_json(Json{{"nope", 1}}));
    CHECK(!KnowledgeConfig::from_json(Json{{"stages", Json::array({"dream"})}}));
    auto c = unwrap(KnowledgeConfig::from_json(Json{{"stages", Json::array({"resolve", "extract"})}, {"prior_cut", "2026-02-08"}}));
    CHECK(c.stages == std::vector<std::string>{"extract", "resolve"});
    CHECK(c.prior_filter().as_of == "2026-02-08");
    CHECK(unwrap(KnowledgeConfig::from_json(c.to_json())).to_json() == c.to_json());
    CHECK(stage_input("extract") == "catalog");
    CHECK(stage_input("catalog").empty());
    auto none = unwrap(KnowledgeConfig::from_json(Json{{"priors", false}}));
    CHECK(!none.prior_filter().enabled);
  }

  TEST_CASE("every area is implemented: a run with no configured sources completes end to end") {
    // Every knowledge-wave area (catalog, extract+resolve, generalize,
    // context+materialize) is implemented (each has its own test_<area>*.cpp
    // for real per-stage coverage); with no sources at all every stage still
    // runs to completion over an empty corpus instead of stubbing out.
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    auto r = unwrap(rt->knowledge().run(KnowledgeConfig{}));
    CHECK(r.status == "done");
    CHECK(r.error.empty());
    REQUIRE(r.stages.size() == kStages.size());
    for (const auto& s : r.stages) CHECK(!s.cache_hit);
    CHECK(r.run.rfind("kr_", 0) == 0);
    auto st = unwrap(rt->knowledge().status());
    CHECK(st["run"]["task_id"] == r.task_id);
  }

  TEST_CASE("fake stages: cache hits on re-run, dependents recompute on a config change") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    std::map<std::string, std::atomic<int>> calls;
    for (auto s : kStages) rt->knowledge().set_stage(s, fake(std::string(s), calls[std::string(s)]));
    KnowledgeConfig cfg;
    auto r1 = unwrap(rt->knowledge().run(cfg));
    REQUIRE(r1.status == "done");
    REQUIRE(r1.stages.size() == kStages.size());
    for (const auto& s : r1.stages) CHECK(!s.cache_hit);
    CHECK(r1.summary["counts"]["entities"] == 6);
    auto r2 = unwrap(rt->knowledge().run(cfg));
    REQUIRE(r2.status == "done");
    for (const auto& s : r2.stages) CHECK(s.cache_hit);
    CHECK(r2.run == r1.run);
    CHECK(calls["catalog"] == 1);
    // A different holdout cut is a different run with different input hashes.
    cfg.prior_cut = "2026-02-08";
    auto r3 = unwrap(rt->knowledge().run(cfg));
    CHECK(r3.run != r1.run);
    CHECK(r3.stages[0].input_hash != r1.stages[0].input_hash);
    CHECK(calls["materialize"] == 2);
    // Only some stages.
    cfg.stages = {"catalog", "extract"};
    auto r4 = unwrap(rt->knowledge().run(cfg));
    CHECK(r4.stages.size() == 2);
    CHECK(r4.stages[0].cache_hit);
  }

  TEST_CASE("cancellation pauses a stage at its checkpoint; the next run resumes it") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    std::map<std::string, std::atomic<int>> calls;
    for (auto s : kStages) rt->knowledge().set_stage(s, fake(std::string(s), calls[std::string(s)]));
    CancelToken token;
    std::atomic<int> attempts{0};
    rt->knowledge().set_stage("extract", [&](StageContext& c) -> Result<Json> {
      ++attempts;
      if (!c.resume_from) {
        LOOM_TRY(c.checkpoint(Json{{"done_units", 3}}));
        token.cancel();
        if (c.should_stop()) return Error(Errc::Paused, "paused at unit 3");
      }
      CHECK(json::get_int(*c.resume_from, "done_units") == 3);
      return Json{{"output", "extract-hash"}, {"stats", Json::object()}};
    });
    auto r1 = unwrap(rt->knowledge().run(KnowledgeConfig{}, {}, &token));
    CHECK(r1.status == "paused");
    token.reset();
    auto r2 = unwrap(rt->knowledge().run(KnowledgeConfig{}, {}, &token));
    CHECK(r2.status == "done");
    CHECK(attempts == 2);
    bool resumed = false;
    for (const auto& s : r2.stages) resumed = resumed || (s.stage == "extract" && s.resumed);
    CHECK(resumed);
  }
}
