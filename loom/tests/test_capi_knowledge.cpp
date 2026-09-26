// loom.h knowledge-layer entry points: pack manifest and policies, store
// queries, judgements, pipeline runs; the still-stubbed areas (catalog,
// extract+resolve, generalize) answer not_implemented. context+materialize
// is implemented (see tests/test_context*.cpp, tests/test_materialize*.cpp
// for its own coverage); here it just needs a run to work from, so a bare
// "{}" request answers not_found / invalid_argument instead.
#include <doctest/doctest.h>

#include "loom/knowledge.h"
#include "loom/loom.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;

namespace {
Json take(const char* s) {
  REQUIRE(s);
  Json j = json::parse_or(s, Json(nullptr));
  loom_free_string(s);
  return j;
}
std::string code_of(const Json& j) { return json::get_string(json::find(j, "error") ? j["error"] : Json(), "code"); }
}  // namespace

TEST_SUITE("capi_knowledge") {
  TEST_CASE("pack, policies, queries, judgements and runs through the C ABI") {
    fsutil::TempDir td;
    std::string opts = json::dump(Json{{"data_dir", (td.path() / "d").string()}, {"start_workers", false}});
    LoomContext* ctx = loom_init_ex(opts.c_str(), nullptr);
    REQUIRE(ctx);
    Json pack = take(loom_kb_pack(ctx));
    CHECK(json::get_string(pack, "hash").size() == 64);
    CHECK(pack["files"].size() > 30);
    Json enc = take(loom_kb_policy(ctx, "evidence_encoding"));
    CHECK(enc["origin"].contains("model_knowledge"));
    CHECK(take(loom_kb_policy(ctx, "goal_types"))["goal_types"].size() >= 7);
    CHECK(take(loom_kb_policy(ctx, "project_kinds/film.json"))["id"] == "film");
    CHECK(code_of(take(loom_kb_policy(ctx, "nope"))) == "not_found");
    CHECK(code_of(take(loom_kb_query(ctx, R"({"what":"claims"})"))) == "not_found");  // no run yet
    CHECK(take(loom_kb_runs(ctx, 10)).empty());

    Json j = take(loom_kb_judge(ctx, R"({"target_kind":"entity","target":"e_x","verdict":"merge","payload":{"into":"e_y"}})"));
    CHECK(j["seq"] == 1);
    CHECK(code_of(take(loom_kb_judge(ctx, R"({"target_kind":"observation","target":"ob_1","verdict":"edit","payload":{"text":"x"}})"))) ==
          "invalid_argument");

    Json r = take(loom_knowledge_run(ctx, R"({"stages":["catalog"]})", nullptr, nullptr));
    CHECK(r["status"] == "failed");
    CHECK(json::get_string(r, "error").find("not implemented") != std::string::npos);
    CHECK(take(loom_kb_runs(ctx, 10)).size() == 1);
    CHECK(take(loom_kb_query(ctx, R"({"what":"stats"})"))["items"]["claims"] == 0);
    CHECK(code_of(take(loom_kb_query(ctx, R"({"what":"claims","origin":"rumour"})"))) == "invalid_argument");
    CHECK(take(loom_knowledge_status(ctx, nullptr))["run"]["task_id"] == r["task_id"]);
    CHECK(loom_knowledge_cancel(ctx) == LOOM_E_NOT_FOUND);

    CHECK(code_of(take(loom_catalog_query(ctx, "{}"))) == "not_implemented");
    CHECK(code_of(take(loom_catalog_scan(ctx, "{}", nullptr, nullptr))) == "not_implemented");
    CHECK(code_of(take(loom_extract_preview(ctx, "x", nullptr))) == "not_implemented");
    CHECK(code_of(take(loom_resolve_lineage(ctx, "{}"))) == "not_implemented");
    CHECK(code_of(take(loom_generalize_predict(ctx, "{}"))) == "not_implemented");
    CHECK(code_of(take(loom_context_build(ctx, "{}"))) == "not_found");   // no finished knowledge run yet
    CHECK(code_of(take(loom_materialize(ctx, "{}"))) == "invalid_argument");  // "kind" missing/unknown
    CHECK(code_of(take(loom_kb_pack(nullptr))) == "invalid_argument");
    loom_shutdown(ctx);
  }
}
