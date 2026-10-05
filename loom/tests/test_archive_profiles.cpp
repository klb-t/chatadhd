// Archive defaults and scoped overrides: lexical membership, exact classifiers,
// user KB source tables, malformed-data errors and stage cache invalidation.
#include <set>
#include <limits>
#include "archive/archive_runtime.h"
#include "archive/profile.h"
#include "loom/archive.h"
#include "loom/runtime.h"
#include "loom/tasks.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::archive;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {
const fs::path kArchiveFixtures = fs::path(LOOM_TEST_FIXTURES) / "archive";
std::unique_ptr<Runtime> archive_runtime(const fs::path& root) {
  RuntimeOptions options;
  options.data_dir = root.string();
  options.start_workers = false;
  return unwrap(Runtime::open(options));
}
ArchiveConfig archive_fixture(const fs::path& output) {
  ArchiveConfig config;
  config.sources = {(kArchiveFixtures / "exports").string(), (kArchiveFixtures / "docs").string()};
  config.repo = (kArchiveFixtures / "repo").string();
  config.git = false;
  config.seed_terms = {"ChatADHD", "graph", "kivy", "importer"};
  config.project = "fixture";
  config.out_dir = output.string();
  return config;
}
void write_overlay(const fs::path& root, const Json& overrides) {
  unwrap(fsutil::ensure_dir(root / "profiles"));
  unwrap(fsutil::write_file(root / "profiles/archive.pack",
                          json::dump(Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "archive"},
                                          {"overrides", overrides}})));
}
}  // namespace

TEST_SUITE("archive.profiles") {
  TEST_CASE("default tables preserve exact legacy classifier and stemming") {
    auto policy = unwrap(ArchiveProfile::builtin());
    CHECK(policy.is_builtin());
    CHECK(policy.items()["cues"].size() == 206);
    CHECK(policy.items()["heading_hints"].size() == 26);
    CHECK(is_stopword("function", &policy));
    CHECK(is_stopword("się", &policy));
    CHECK_FALSE(is_stopword("sie", &policy));  // Expanded KB normalizer must not leak in.
    CHECK(stem("analysis", &policy) == "analysis");
    CHECK(stem("stories", &policy) == "story");
    CHECK(stem("classes", &policy) == "class");
    CHECK(stem("status", &policy) == "status");
    CHECK(gloss("pamięci", &policy) == "memory");
    auto classification = classify_sentence("We decided to use SQLite.", "", &policy);
    CHECK(classification.type == "decision");
    CHECK(classification.confidence == .88);
    CHECK(classification.cues == std::vector<std::string>{"we decided", "decided to"});
    CHECK(policy.inspection()["kb_pack_hash"].is_string());
    CHECK(policy.inspection()["runtime_profile_hash"].is_string());
  }

  TEST_CASE("explicit overrides change lexical behavior without changing another instance") {
    auto base = unwrap(ArchiveProfile::builtin());
    auto changed = unwrap(base.with_overrides(Json{{"text", Json{{"min_token_codepoints", 2},
                                                                  {"glossary", Json{{"wrench", "tool"}}}}},
                                                   {"items", Json{{"min_score", 100}}}}));
    CHECK_FALSE(changed.is_builtin());
    CHECK(changed.hash() != base.hash());
    CHECK(content_tokens("xy graphite", &changed) == std::vector<std::string>{"xy", "graphite"});
    CHECK(content_tokens("xy graphite", &base) == std::vector<std::string>{"graphite"});
    CHECK(gloss("wrench", &changed) == "tool");
    CHECK(gloss("wrench", &base) == "wrench");
    CHECK(classify_sentence("We decided to use SQLite.", "", &changed).type.empty());
    CHECK(classify_sentence("We decided to use SQLite.", "", &base).type == "decision");
    auto snapshot = unwrap(changed.provenance());
    auto replayed = unwrap(ArchiveProfile::from_provenance(snapshot));
    CHECK(replayed.hash() == changed.hash());
    CHECK(replayed.value("/text/glossary/wrench") == "tool");
    snapshot["hash"] = "tampered";
    CHECK_FALSE(ArchiveProfile::from_provenance(snapshot));
    auto restored = unwrap(changed.with_patch(Json::array({
        Json{{"op", "remove"}, {"path", "/text/glossary/wrench"}},
        Json{{"op", "replace"}, {"path", "/text/min_token_codepoints"},
             {"value", base.value("/text/min_token_codepoints")}},
        Json{{"op", "replace"}, {"path", "/items/min_score"}, {"value", base.value("/items/min_score")}}
    })));
    CHECK(restored.is_builtin());
    CHECK(restored.hash() == base.hash());
  }

  TEST_CASE("KB user tables reach the archive without normalizer expansion") {
    fsutil::TempDir temp;
    auto base = unwrap(kb::Pack::load_builtin());
    Json words = base->lexicon("stopwords_base");
    words["en"].push_back("frobnicate");
    Json items = base->lexicon("item_cues");
    items["cues"].push_back(Json{{"type", "idea"}, {"phrase", "frobnicate"}, {"weight", 3}});
    unwrap(fsutil::ensure_dir(temp.path() / "kb/lexicons"));
    unwrap(fsutil::write_file(temp.path() / "kb/lexicons/stopwords_base.json", json::dump(words)));
    unwrap(fsutil::write_file(temp.path() / "kb/lexicons/item_cues.json", json::dump(items)));
    auto custom = unwrap(ArchiveProfile::load(temp.path()));
    auto builtin = unwrap(ArchiveProfile::builtin());
    CHECK_FALSE(custom.is_builtin());
    CHECK(custom.hash() != builtin.hash());
    CHECK(is_stopword("frobnicate", &custom));
    CHECK_FALSE(is_stopword("frobnicate", &builtin));
    CHECK(classify_sentence("frobnicate archive behavior", "", &custom).type == "idea");
    CHECK(classify_sentence("frobnicate archive behavior", "", &builtin).type.empty());
    auto replayed = unwrap(ArchiveProfile::from_provenance(unwrap(custom.provenance())));
    CHECK(replayed.hash() == custom.hash());
    CHECK(is_stopword("frobnicate", &replayed));
  }

  TEST_CASE("language profiles and zero display settings are explicit") {
    auto base = unwrap(ArchiveProfile::builtin());
    auto changed = unwrap(base.with_overrides(Json{{"code", Json{{"extensions", Json{{".custom", "cpp"}}},
                                                                 {"max_digest_symbols", 0}}},
                                                    {"synthesis", Json{{"sections", Json::array()}}}}));
    CHECK(code_language("sample.custom", &base).empty());
    CHECK(code_language("sample.custom", &changed) == "cpp");
    auto digest = digest_code("sample.custom", "cpp", "class GraphStore {\n};\n", &changed);
    CHECK(digest.symbols == std::vector<std::string>{"GraphStore"});
    CHECK(digest.text.find("GraphStore") == std::string::npos);  // Presentation cap only.
    auto zero_tokens = unwrap(base.with_overrides(Json{{"vocabulary", Json{{"camel_min_length", 0},
                                                                            {"camel_min_uppercase", 0}}}}));
    CHECK(camel_identifiers("   ", &zero_tokens).empty());
  }

  TEST_CASE("malformed existing overlays and incompatible ranges fail explicitly") {
    fsutil::TempDir temp;
    unwrap(fsutil::ensure_dir(temp.path() / "profiles"));
    unwrap(fsutil::write_file(temp.path() / "profiles/archive.pack", "broken JSON"));
    CHECK_FALSE(ArchiveProfile::load(temp.path()));
    auto runtime = archive_runtime(temp.path());
    auto run = runtime->archive().run(archive_fixture(temp.path() / "out"));
    CHECK_FALSE(run);
    auto base = unwrap(ArchiveProfile::builtin());
    CHECK_FALSE(base.with_overrides(Json{{"pipeline", Json{{"auto_hits_min", 999}, {"auto_hits_max", 1}}}}));
    CHECK_FALSE(base.with_overrides(Json{{"items", Json{{"confidence_min", 1}, {"confidence_max", 0}}}}));
    CHECK_FALSE(base.with_overrides(Json{{"refinement", Json{{"batch_size", 0}}}}));
    CHECK_FALSE(base.with_overrides(Json{{"invented", true}}));
  }

  TEST_CASE("call bounds are presets and null maximum is unbounded") {
    auto base = unwrap(ArchiveProfile::builtin());
    CHECK(unwrap(ArchiveConfig::from_json(Json{{"max_passes", 99}})).max_passes == 20);
    auto raised = unwrap(base.with_overrides(Json{{"config_bounds", Json{{"max_passes", Json{{"maximum", 200}}}}}}));
    CHECK(unwrap(ArchiveConfig::from_json_with_profile(Json{{"max_passes", 99}}, raised)).max_passes == 99);
    auto unlimited = unwrap(base.with_overrides(Json{{"config_bounds", Json{{"max_passes", Json{{"maximum", nullptr}}},
                                                                          {"max_new_terms", Json{{"maximum", nullptr}}},
                                                                          {"max_hits_per_term", Json{{"maximum", nullptr}}},
                                                                          {"max_synthesis_rounds", Json{{"maximum", nullptr}}}}}}));
    auto config = unwrap(ArchiveConfig::from_json_with_profile(Json{{"max_passes", 99}, {"max_new_terms", 1000},
                                                                 {"max_hits_per_term", 10000}, {"max_synthesis_rounds", 10}},
                                                            unlimited));
    CHECK(config.max_passes == 99);
    CHECK(config.max_new_terms == 1000);
    CHECK(config.max_hits_per_term == 10000);
    CHECK(config.max_synthesis_rounds == 10);
    CHECK_FALSE(ArchiveConfig::from_json_with_profile(Json{{"max_passes", std::numeric_limits<std::uint64_t>::max()}}, unlimited));
    CHECK_FALSE(base.with_overrides(Json{{"config_bounds", Json{{"max_passes", Json{{"minimum", 30}, {"maximum", 20}}}}}}));
  }

  TEST_CASE("reopened runtime applies profile and invalidates old stage cache") {
    fsutil::TempDir temp;
    const auto output = temp.path() / "out";
    auto runtime = archive_runtime(temp.path());
    const auto config = archive_fixture(output);
    auto first = unwrap(runtime->archive().run(config));
    const auto before = unwrap(fsutil::read_file(output / "items.jsonl"));
    REQUIRE_FALSE(first.stages.empty());
    runtime.reset();
    write_overlay(temp.path(), Json{{"items", Json{{"min_score", 100}}}});
    runtime = archive_runtime(temp.path());
    auto second = unwrap(runtime->archive().run(config));
    const auto after = unwrap(fsutil::read_file(output / "items.jsonl"));
    CHECK(after != before);
    REQUIRE(second.stages.size() == first.stages.size());
    for (std::size_t i = 0; i < first.stages.size(); ++i) {
      CHECK(second.stages[i].input_hash != first.stages[i].input_hash);
      CHECK_FALSE(second.stages[i].cache_hit);
    }
    auto ingest_task = unwrap(runtime->tasks().get(second.stages.front().task_id));
    REQUIRE(ingest_task);
    REQUIRE(ingest_task->result);
    // A source parsed under old policy must not bind current corpus text to
    // old stored messages, whose content is what DB retrieval actually searches.
    CHECK((*ingest_task->result)["stats"]["units_reused"] == 0);
    CHECK((*ingest_task->result)["stats"]["conversations_created"].get<int>() > 0);
    auto status = unwrap(runtime->archive().status(second.run_id));
    CHECK(status["profile"]["is_builtin"] == false);
    CHECK(status["profile"]["values"]["items"]["min_score"] == 100);
    CHECK_FALSE(unwrap(runtime->archive().status(first.run_id)).contains("profile"));
    auto third = unwrap(runtime->archive().run(config));
    for (const auto& stage : third.stages) {
      if (stage.stage == "materialize") CHECK_FALSE(stage.cache_hit);
      else CHECK(stage.cache_hit);
    }
    runtime.reset();
    write_overlay(temp.path(), Json{{"items", Json{{"min_score", 3}}}});
    runtime = archive_runtime(temp.path());
    CHECK(unwrap(runtime->archive().profile())["values"]["items"]["min_score"] == 3);
    CHECK(unwrap(runtime->archive().status(second.run_id))["profile"]["values"]["items"]["min_score"] == 100);
  }
}
