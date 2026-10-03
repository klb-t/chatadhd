// knowledge.h: the knowledge pipeline as TaskEngine stages. With the area
// stubs a run stops at the first stage with not_implemented; with injected
// stage functions it caches by input hash (pack hash + config + upstream
// output), re-runs dependents when the config changes, and pauses/resumes.
#include <doctest/doctest.h>

#include <atomic>

#include "loom/catalog.h"
#include "loom/config.h"
#include "loom/event_bus.h"
#include "loom/extract.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/tasks.h"
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
    CHECK(!KnowledgeConfig::from_json(Json{{"stage_params", Json{{"catalog", 7}}}}));
    auto c = unwrap(KnowledgeConfig::from_json(Json{{"stages", Json::array({"resolve", "extract"})}, {"prior_cut", "2026-02-08"}}));
    CHECK(c.stages == std::vector<std::string>{"extract", "resolve"});
    CHECK(c.prior_filter().as_of == "2026-02-08");
    CHECK(unwrap(KnowledgeConfig::from_json(c.to_json())).to_json() == c.to_json());
    CHECK(stage_input("extract") == "catalog");
    CHECK(stage_input("catalog").empty());
    auto none = unwrap(KnowledgeConfig::from_json(Json{{"priors", false}}));
    CHECK(!none.prior_filter().enabled);
  }

  TEST_CASE("semantic model identity invalidates auto runs without persisting API keys") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    std::atomic<int> calls{0};
    rt->knowledge().set_stage("extract", fake("extract", calls));
    KnowledgeConfig cfg;
    cfg.stages = {"extract"};
    cfg.llm = "auto";
    rt->config().set("semantic_model", "economical/a");
    rt->config().set("base_url", "https://provider.test/v1");
    auto missing = unwrap(rt->knowledge().run(cfg));
    rt->secrets().set("api_key", "only-in-memory-test-secret");
    auto available = unwrap(rt->knowledge().run(cfg));
    CHECK(missing.run != available.run);
    CHECK(calls == 2);
    auto repeated = unwrap(rt->knowledge().run(cfg));
    CHECK(repeated.run == available.run);
    CHECK(repeated.stages[0].cache_hit);
    CHECK(calls == 2);
    rt->config().set("semantic_model", "economical/b");
    auto changed = unwrap(rt->knowledge().run(cfg));
    CHECK(changed.run != available.run);
    CHECK(calls == 3);
    {
      auto lock = rt->db().lock();
      const auto persisted = unwrap(rt->db().conn().query_text(
          "SELECT inputs FROM loom_kb_runs WHERE run_id=?", changed.run));
      REQUIRE(persisted.has_value());
      CHECK(persisted->find("only-in-memory-test-secret") == std::string::npos);
      CHECK(persisted->find("economical/b") != std::string::npos);
    }
    // When the user asks for no model, model settings are not pipeline inputs.
    cfg.llm = "off";
    auto off = unwrap(rt->knowledge().run(cfg));
    rt->config().set("semantic_model", "economical/c");
    auto off_again = unwrap(rt->knowledge().run(cfg));
    CHECK(off.run == off_again.run);
    CHECK(off_again.stages[0].cache_hit);
  }

  TEST_CASE("an explicit new run retries incomplete semantic output once and then caches success") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    int calls = 0;
    rt->knowledge().set_stage("extract", [&](StageContext&) -> Result<Json> {
      ++calls;
      return Json{{"output", std::to_string(calls)},
                  {"stats", Json{{"semantic", Json{{"failed", calls < 3 ? 1 : 0}, {"rejected", 0}}}}}};
    });
    KnowledgeConfig cfg;
    cfg.llm = "auto";
    cfg.stages = {"extract"};
    CHECK(unwrap(rt->knowledge().run(cfg)).status == "done");
    CHECK(calls == 1); // no automatic retry inside the first invocation
    auto retry = unwrap(rt->knowledge().run(cfg));
    CHECK(calls == 2);
    CHECK(!retry.stages[0].cache_hit);
    auto success = unwrap(rt->knowledge().run(cfg));
    CHECK(calls == 3);
    CHECK(!success.stages[0].cache_hit);
    auto cached = unwrap(rt->knowledge().run(cfg));
    CHECK(calls == 3);
    CHECK(cached.stages[0].cache_hit);
  }

  TEST_CASE("a new stage finishing before the first poll cannot trigger a paid retry") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    int calls = 0;
    rt->knowledge().set_stage("extract", [&](StageContext&) -> Result<Json> {
      ++calls;
      return Json{{"output", "partial"}, {"stats", Json{{"semantic", Json{{"failed", 1}}}}}};
    });
    // Deterministically reproduce the worker race: complete the new stage
    // from its submission notification, before submit() returns to the caller.
    const auto subscription = rt->bus().on(events::kTaskChanged, [&](std::string_view, const Json& data) {
      if (json::get_string(data, "kind") == "knowledge.extract" && json::get_string(data, "status") == "pending")
        unwrap(rt->tasks().run_sync(json::get_string(data, "id")));
    });
    ScopedSubscription guard(rt->bus(), subscription);
    KnowledgeConfig cfg;
    cfg.llm = "auto";
    cfg.stages = {"extract"};
    auto result = unwrap(rt->knowledge().run(cfg));
    CHECK(calls == 1);
    CHECK(!result.stages[0].cache_hit);
    auto retry = unwrap(rt->knowledge().run(cfg));
    CHECK(calls == 2);
    CHECK(!retry.stages[0].cache_hit);
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
    CHECK(unwrap(rt->knowledge().status(r2.task_id))["stages"].size() == kStages.size());
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

  TEST_CASE("source bytes, additions and deletions invalidate the pipeline without force") {
    fsutil::TempDir td, sources;
    auto rt = open_rt(td.path());
    std::map<std::string, std::atomic<int>> calls;
    for (auto s : kStages) rt->knowledge().set_stage(s, fake(std::string(s), calls[std::string(s)]));
    KnowledgeConfig cfg;
    cfg.sources = {sources.path().string()};
    unwrap(fsutil::write_file(sources.path() / "note.md", "first"));
    auto first = unwrap(rt->knowledge().run(cfg));
    auto unchanged = unwrap(rt->knowledge().run(cfg));
    CHECK(unchanged.run == first.run);
    CHECK(calls["catalog"] == 1);
    unwrap(fsutil::write_file(sources.path() / "note.md", "other"));  // same size
    auto changed = unwrap(rt->knowledge().run(cfg));
    CHECK(changed.run != first.run);
    CHECK(calls["materialize"] == 2);
    unwrap(fsutil::write_file(sources.path() / "second.md", "addition"));
    auto added = unwrap(rt->knowledge().run(cfg));
    CHECK(added.run != changed.run);
    std::filesystem::remove(sources.path() / "note.md");
    auto removed = unwrap(rt->knowledge().run(cfg));
    CHECK(removed.run != added.run);
    CHECK(calls["materialize"] == 4);
  }

  TEST_CASE("cached products are exported to a new destination and restored if missing") {
    fsutil::TempDir td, source;
    auto rt = open_rt(td.path());
    KnowledgeConfig cfg;
    cfg.sources = {source.path().string()};
    cfg.out_dir = (source.path() / "products").string();
    auto first = unwrap(rt->knowledge().run(cfg));
    REQUIRE(first.status == "done");
    auto text = unwrap(fsutil::read_file(std::filesystem::path(cfg.out_dir) / "SELF.md"));
    cfg.out_dir = (source.path() / "other_products").string();
    auto second = unwrap(rt->knowledge().run(cfg));
    REQUIRE(second.status == "done");
    CHECK(second.run == first.run);  // outputs never feed back into inputs
    for (const auto& s : second.stages) CHECK(s.cache_hit);
    CHECK(unwrap(fsutil::read_file(std::filesystem::path(cfg.out_dir) / "SELF.md")) == text);
    std::filesystem::remove(std::filesystem::path(cfg.out_dir) / "SELF.md");
    auto third = unwrap(rt->knowledge().run(cfg));
    CHECK(third.status == "done");
    CHECK(std::filesystem::exists(std::filesystem::path(cfg.out_dir) / "SELF.md"));
  }

  TEST_CASE("repository revision changes invalidate lineage even with unchanged working files") {
    fsutil::TempDir td, source;
    auto rt = open_rt(td.path());
    std::map<std::string, std::atomic<int>> calls;
    for (auto s : kStages) rt->knowledge().set_stage(s, fake(std::string(s), calls[std::string(s)]));
    unwrap(fsutil::ensure_dir(source.path() / ".git/refs/heads"));
    unwrap(fsutil::write_file(source.path() / ".git/HEAD", "ref: refs/heads/main\n"));
    unwrap(fsutil::write_file(source.path() / ".git/refs/heads/main", std::string(40, 'a') + "\n"));
    KnowledgeConfig cfg;
    cfg.repo = source.path().string();
    auto first = unwrap(rt->knowledge().run(cfg));
    unwrap(fsutil::write_file(source.path() / ".git/refs/heads/main", std::string(40, 'b') + "\n"));
    auto second = unwrap(rt->knowledge().run(cfg));
    CHECK(first.run != second.run);
    CHECK(calls["resolve"] == 2);
  }

  TEST_CASE("active data nested under a source never feeds back into catalog or fingerprint") {
    fsutil::TempDir source;
    auto rt = open_rt(source.path() / "runtime-data");
    unwrap(fsutil::write_file(source.path() / "notes.md", "ChatADHD keeps observations with provenance."));
    KnowledgeConfig cfg;
    cfg.sources = {source.path().string()};
    cfg.stages = {"catalog"};
    auto first = unwrap(rt->knowledge().run(cfg));
    REQUIRE(first.status == "done");
    auto second = unwrap(rt->knowledge().run(cfg));
    CHECK(second.run == first.run);
    CHECK(second.stages.front().cache_hit);
    catalog::Catalog cat(*rt, unwrap(rt->knowledge().pack()));
    catalog::UnitQuery query;
    query.limit = 1000;
    auto units = unwrap(cat.query(query));
    CHECK(units.size() == 1);
    cfg.sources = {rt->paths().root.string()};
    CHECK(!rt->knowledge().run(cfg));
  }

  TEST_CASE("failed stage marks the parent task failed and preserves its stage result") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    rt->knowledge().set_stage("catalog", [](StageContext&) -> Result<Json> {
      return Error(Errc::InvalidArgument, "invalid source contract");
    });
    auto run = unwrap(rt->knowledge().run(KnowledgeConfig{}));
    CHECK(run.status == "failed");
    REQUIRE(run.stages.size() == 1);
    CHECK(run.error.find("invalid source contract") != std::string::npos);
    auto task = unwrap(rt->tasks().get(run.task_id));
    REQUIRE(task);
    CHECK(task->status == task_status::kFailed);
    KnowledgeConfig invalid;
    invalid.llm = "unknown";
    CHECK(!rt->knowledge().run(invalid));
  }

  TEST_CASE("catalog profile options cannot loosen the pipeline prior filter") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    KnowledgeConfig cfg;
    cfg.stages = {"catalog"};
    cfg.stage_params = Json{{"catalog", Json{{"profile", Json{{"extra_terms", Json::array({"OnlyThisTerm"})}}}}}};
    SUBCASE("no priors") { cfg.priors = false; }
    SUBCASE("dated priors") { cfg.prior_cut = "2026-01-01"; }
    auto run = unwrap(rt->knowledge().run(cfg));
    REQUIRE(run.status == "done");
    auto task = unwrap(rt->tasks().get(run.stages.front().task_id));
    REQUIRE(task);
    REQUIRE(task->result);
    catalog::ProfileConfig expected;
    expected.extra_terms = {"OnlyThisTerm"};
    expected.priors = cfg.prior_filter();
    catalog::Catalog cat(*rt, unwrap(rt->knowledge().pack()));
    auto profile = unwrap(cat.build_profile(expected));
    CHECK((*task->result)["profile_id"] == profile.id);
  }

  TEST_CASE("full import scope feeds extraction and catalogue hashes exclude random import ids") {
    fsutil::TempDir source, data_a, data_b;
    const auto path = source.path() / "conversations.json";
    unwrap(fsutil::write_file(path,
        R"([{"uuid":"quiet-garden","name":"A quiet garden","chat_messages":[{"uuid":"m1","sender":"human","text":"Sunflowers follow the afternoon light.","created_at":"2026-01-01T12:00:00Z"}]}])"));
    KnowledgeConfig cfg;
    cfg.sources = {path.string()};
    cfg.stages = {"catalog", "extract"};
    cfg.stage_params = Json{{"catalog", Json{{"import", Json{{"mode", "full"}}}}}};
    auto a = open_rt(data_a.path());
    auto first = unwrap(a->knowledge().run(cfg));
    REQUIRE(first.status == "done");
    REQUIRE(first.stages.size() == 2);
    CHECK(first.stages[1].stats["from"] == "catalog");
    CHECK(first.stages[1].stats["units"] == 1);
    auto task = unwrap(a->tasks().get(first.stages.front().task_id));
    REQUIRE(task);
    REQUIRE(task->result);
    CHECK((*task->result)["stats"]["selected"] == 0);
    CHECK((*task->result)["units"].size() == 1);
    auto counts = unwrap(a->knowledge().store().stats(first.run));
    CHECK(json::get_int(counts, "observations") > 0);
    auto b = open_rt(data_b.path());
    auto second = unwrap(b->knowledge().run(cfg));
    REQUIRE(second.status == "done");
    CHECK(first.stages.front().output_hash == second.stages.front().output_hash);

    fsutil::TempDir data_selective;
    auto selective = open_rt(data_selective.path());
    cfg.stage_params = Json::object();
    auto empty = unwrap(selective->knowledge().run(cfg));
    REQUIRE(empty.status == "done");
    CHECK(empty.stages[1].stats["from"] == "catalog");
    CHECK(empty.stages[1].stats["units"] == 0);
    CHECK(json::get_int(unwrap(selective->knowledge().store().stats(empty.run)), "observations") == 0);
  }

  TEST_CASE("invalid upstream catalogue IDs fail extraction without raw-source fallback") {
    fsutil::TempDir source, data;
    unwrap(fsutil::write_file(source.path() / "must_not_read.md", "ChatADHD always preserves alternatives."));
    auto rt = open_rt(data.path());
    rt->knowledge().set_stage("catalog", [](StageContext& ctx) -> Result<Json> {
      LOOM_TRY(catalog::Catalog::ensure_schema(ctx.rt.db()));
      return Json{{"output", "bad-input"}, {"units", Json::array({"missing-catalog-unit"})}};
    });
    KnowledgeConfig cfg;
    cfg.sources = {source.path().string()};
    cfg.stages = {"catalog", "extract"};
    auto run = unwrap(rt->knowledge().run(cfg));
    CHECK(run.status == "failed");
    CHECK(run.error.find("catalog unit missing") != std::string::npos);
    CHECK(json::get_int(unwrap(rt->knowledge().store().stats(run.run)), "observations") == 0);
  }

  TEST_CASE("standalone JSON locator survives multiple leading whitespace chunks") {
    fsutil::TempDir source, data;
    auto rt = open_rt(data.path());
    catalog::Catalog cat(*rt, unwrap(rt->knowledge().pack()));
    const std::string object = R"({"uuid":"padded","name":"Garden","unknown":{"keep":true},"chat_messages":[{"uuid":"m1","sender":"human","text":"Sunflowers follow daylight."}]})";
    const std::string bytes = std::string(600 * 1024, ' ') + object + "\n";
    unwrap(fsutil::write_file(source.path() / "padded.json", bytes));
    catalog::ScanConfig scan;
    scan.sources = {source.path().string()};
    unwrap(cat.scan(scan));
    const auto units = unwrap(cat.query(catalog::UnitQuery{}));
    REQUIRE(units.size() == 1);
    const auto& locator = units.front().unit.locator;
    REQUIRE(locator.byte_start.has_value());
    REQUIRE(locator.byte_len.has_value());
    const auto raw = unwrap(cat.read_unit(units.front().unit.id));
    CHECK(raw == bytes.substr(static_cast<std::size_t>(*locator.byte_start),
                              static_cast<std::size_t>(*locator.byte_len)));
    CHECK(unwrap(json::parse(raw)) == unwrap(json::parse(object)));
  }

  TEST_CASE("standalone and object-wrapped JSON units verify their actual source locators") {
    fsutil::TempDir source, data;
    auto rt = open_rt(data.path());
    catalog::Catalog cat(*rt, unwrap(rt->knowledge().pack()));
    const std::string single = R"({ "uuid": "standalone", "name": "Garden", "unknown": {"keep": true},
      "chat_messages": [{"uuid":"m0","sender":"human","text":"Sunflowers follow daylight."}] })";
    const std::string wrapped = R"({"conversations":[
      {"uuid":"wrapped-a","name":"Blue garden","chat_messages":[{"uuid":"m1","sender":"human","text":"Blue petals."}]},
      {"uuid":"wrapped-b","name":"Red garden","chat_messages":[{"uuid":"m2","sender":"human","text":"Red petals."}]}],"unknown_export_metadata":42})";
    unwrap(fsutil::write_file(source.path() / "single.json", single));
    unwrap(fsutil::write_file(source.path() / "wrapped.json", wrapped));
    catalog::ScanConfig scan;
    scan.sources = {source.path().string()};
    unwrap(cat.scan(scan));
    auto units = unwrap(cat.query(catalog::UnitQuery{}));
    REQUIRE(units.size() == 3);
    for (auto unit : units) {
      const auto raw = unwrap(cat.read_unit(unit.unit.id));
      const auto parsed = unwrap(json::parse(raw));
      if (parsed["uuid"] == "standalone") {
        CHECK(raw == single);
        // Existing v1 catalogues used a normalized /0 for root objects.
        unit.unit.locator.byte_start.reset();
        unit.unit.locator.byte_len.reset();
        unit.unit.locator.json_pointer = "/0";
        unit.content_hash = Sha256::hex(json::dump(parsed));
      } else {
        CHECK(unit.unit.locator.json_pointer.rfind("/conversations/", 0) == 0);
        // Existing v1 wrapper pointers omitted the wrapper key.
        unit.unit.locator.json_pointer = parsed["uuid"] == "wrapped-a" ? "/0" : "/1";
      }
      {
        auto lock = rt->db().lock();
        unwrap(rt->db().conn().run("UPDATE loom_cat_units SET body = ? WHERE id = ?", json::dump(unit.to_json()), unit.unit.id));
      }
      CHECK(unwrap(json::parse(unwrap(cat.read_unit(unit.unit.id)))) == parsed);
    }
    KnowledgeConfig cfg;
    cfg.sources = scan.sources;
    cfg.stages = {"catalog", "extract"};
    cfg.stage_params = Json{{"catalog", Json{{"import", Json{{"mode", "full"}}}}}};
    auto run = unwrap(rt->knowledge().run(cfg));
    REQUIRE(run.status == "done");
    CHECK(run.stages[1].stats["from"] == "catalog");
    CHECK(run.stages[1].stats["units"] == 3);
  }

  TEST_CASE("catalog filesystem paths preserve code artifact detection") {
    fsutil::TempDir source, data;
    auto rt = open_rt(data.path());
    auto pack = unwrap(rt->knowledge().pack());
    catalog::Catalog cat(*rt, pack);
    unwrap(fsutil::ensure_dir(source.path() / "src"));
    unwrap(fsutil::write_file(source.path() / "src/widget.cpp", "class GardenWidget {};\n"));
    catalog::ScanConfig scan;
    scan.sources = {source.path().string()};
    unwrap(cat.scan(scan));
    auto units = unwrap(cat.query(catalog::UnitQuery{}));
    REQUIRE(units.size() == 1);
    CHECK(units.front().unit.title == "src/widget.cpp");
    extract::UnitContent content;
    content.unit = units.front().unit;
    content.text = unwrap(cat.read_unit(content.unit.id));
    extract::Extractor extractor(pack);
    auto detections = unwrap(extractor.detect(content));
    REQUIRE(!detections.empty());
    CHECK(detections.front().artifact_type == "codebase");
  }
}
