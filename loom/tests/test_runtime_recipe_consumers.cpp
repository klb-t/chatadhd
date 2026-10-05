#include <doctest/doctest.h>

#include "loom/config.h"
#include "loom/event_bus.h"
#include "loom/graph_engine.h"
#include "loom/graph_memory.h"
#include "loom/knowledge.h"
#include "loom/materialize.h"
#include "loom/memory_engine.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_analyzer.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
void overlay_at(const std::filesystem::path& root, std::string_view domain, const Json& changes) {
  Json document{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", changes}};
  LOOM_REQUIRE_OK(fsutil::write_file(root / "profiles" / (std::string(domain) + ".pack"), json::dump(document)));
}
std::string message(Database& db, std::string_view conv, std::string_view text) {
  NewMessage item;
  item.conv_id = std::string(conv); item.text = std::string(text); item.role = "user";
  return unwrap(db.create_msg(item));
}
}

TEST_SUITE("runtime_recipe_consumers") {
  TEST_CASE("consumer profiles revalidate executable values even with a permissive supplied descriptor") {
    Json loose{{"schema", "loom.runtime_profile/1"}, {"domain", "semantic_analyzer"}, {"revision", 1},
               {"defaults", Json::object()}, {"value_schema", Json{{"type", "object"}}}};
    auto malformed = unwrap(RuntimeProfile::from_definition(loose));
    CHECK_FALSE(AnalyzerRules::from_profile(malformed));
    CHECK_FALSE(SemanticAnalyzer::create_with_profile(malformed));
    loose["domain"] = "graph_memory";
    auto wrong_context = unwrap(RuntimeProfile::from_definition(loose));
    CHECK_FALSE(ContextRequest::from_json_with_profile(Json::object(), wrong_context));
  }

  TEST_CASE("semantic rules overlay replaces dictionaries and calibrates matched entities") {
    fsutil::TempDir td("loom_");
    overlay_at(td.path(), "semantic_analyzer",
               Json{{"rules", Json{{"entity_patterns", Json::array({Json{{"entity_type", "widget"}, {"pattern", "\\b(W[A-Z]+)\\b"}, {"flags", Json::array()}}})},
                                    {"topics", Json::array({Json{{"topic", "custom"}, {"keywords", Json::array({"widget"})}}})},
                                    {"relation_patterns", Json::array()}}},
                    {"parameters", Json{{"entity_confidence", 0.42}, {"topic_threshold", 1}, {"topic_confidence", 0.77}}}});
    auto analyzer = unwrap(SemanticAnalyzer::create_from_data_dir(td.path()));
    auto analysis = analyzer->analyse("WIDGET widget admin@example.com");
    REQUIRE(analysis.entities.size() == 1);
    CHECK(analysis.entities[0].entity_type == "widget");
    CHECK(analysis.entities[0].confidence == doctest::Approx(0.42));
    CHECK(analysis.topics == std::vector<std::string>{"custom"});
    CHECK(analyzer->to_unified_profile(analysis)["topics"][0]["confidence"] == Json(0.77));
    CHECK(analyzer->profile_hash() == unwrap(RuntimeProfile::load("semantic_analyzer", td.path())).hash());
    auto default_entities = unwrap(SemanticAnalyzer::create())->extract_entities("admin@example.com");
    bool retained_email = false;
    for (const auto& entity : default_entities)
      if (entity.text == "admin@example.com") retained_email = true;
    CHECK(retained_email);
  }

  TEST_CASE("graph ingestion overlay changes gates, kinds, relations and weights") {
    fsutil::TempDir td("loom_");
    auto db = loom::test::open_db(td.path() / "graph.db");
    EventBus bus;
    auto analyzer = unwrap(SemanticAnalyzer::create());
    GraphEngine graph(*db, bus, *analyzer);
    auto conv = unwrap(db->create_conv("custom graph"));
    auto mid = message(*db, conv.id, "x");
    overlay_at(td.path(), "graph_ingest",
               Json{{"entities", Json{{"min_label_codepoints", 1}, {"min_relevance", 0.0}, {"default_kind", "widget"}, {"predicate", "uses"}}},
                    {"topics", Json{{"kind", "category"}, {"lowercase", false}, {"min_confidence", 0.0}, {"predicate", "categorized"}}},
                    {"membership", Json{{"weight", 0.91}, {"predicate", "member_of"}}}});
    Json analysis{{"entities", Json::array({Json{{"name", "x"}, {"relevance", 0.12}}})},
                  {"topics", Json::array({Json{{"label", "Upper"}, {"confidence", 0.22}}})}};
    CHECK(unwrap(graph.ingest_analysis_checked(mid, conv.id, analysis)));
    auto node = unwrap(db->find_node("x", "widget")); REQUIRE(node.has_value());
    CHECK(unwrap(db->find_node("Upper", "category")).has_value());
    auto links = unwrap(db->get_links(mid));
    bool entity_edge = false, member_edge = false;
    for (const auto& link : links) {
      if (link.link_type == "uses" && link.dst == node->id) { entity_edge = true; CHECK(link.weight == doctest::Approx(0.12)); }
      if (link.link_type == "member_of" && link.dst == conv.id) { member_edge = true; CHECK(link.weight == doctest::Approx(0.91)); }
    }
    CHECK(entity_edge); CHECK(member_edge);
    CHECK(graph.profile_inspection()->at("is_builtin") == Json(false));
  }

  TEST_CASE("invalid existing graph and analyzer overlays fail checked APIs before mutation") {
    fsutil::TempDir td("loom_");
    auto db = loom::test::open_db(td.path() / "graph.db"); EventBus bus;
    auto analyzer = unwrap(SemanticAnalyzer::create()); GraphEngine graph(*db, bus, *analyzer);
    auto conv = unwrap(db->create_conv("bad policy")); auto mid = message(*db, conv.id, "content for analysis");
    overlay_at(td.path(), "graph_ingest", Json{{"entities", Json{{"min_relevance", "invalid"}}}});
    auto result = graph.ingest_analysis_checked(mid, conv.id, Json{{"entities", Json::array({Json{{"name", "ShouldNotExist"}}})}});
    CHECK_FALSE(result); CHECK_FALSE(unwrap(db->find_node("ShouldNotExist")).has_value());
    overlay_at(td.path(), "semantic_analyzer", Json{{"rules", Json{{"entity_patterns", Json::array({Json{{"entity_type", "bad"}, {"pattern", "("}, {"flags", Json::array()}}})}}}});
    CHECK_FALSE(SemanticAnalyzer::create_from_data_dir(td.path()));
  }

  TEST_CASE("graph memory template and truncation overlay reaches legacy selector") {
    fsutil::TempDir td("loom_");
    auto db = loom::test::open_db(td.path() / "graph.db"); Config cfg(td.path() / "config.json");
    auto analyzer = unwrap(SemanticAnalyzer::create()); GraphMemorySelector selector(*db, cfg, *analyzer);
    auto conv = unwrap(db->create_conv("A long retained conversation title"));
    auto mid = message(*db, conv.id, "widget original content with {{text}} literal");
    auto seed = unwrap(db->get_or_create_node("widget", "widget"));
    unwrap(db->create_link(mid, seed, "mentions", 1));
    overlay_at(td.path(), "graph_memory", Json{{"header", "CUSTOM\n"}, {"row", "{{title}}:{{text}}"}, {"title_codepoints", 0}, {"snippet_codepoints", 0}});
    GraphSelectOptions options; options.analysis = Json{{"entities", Json::array({Json{{"name", "widget"}}})}};
    auto context = unwrap(selector.select_context_checked("", options));
    CHECK(context == "CUSTOM\nA long retained conversation title:widget original content with {{text}} literal");
    overlay_at(td.path(), "graph_memory", Json{{"row", "{{absent}}"}});
    CHECK_FALSE(selector.select_context_checked("", options));
  }

#ifndef LOOM_PARITY_CORE_ONLY
  TEST_CASE("materialize supports new extensions, report text and invalid template errors") {
    fsutil::TempDir td("loom_");
    RuntimeOptions options; options.data_dir = td.path().string(); options.start_workers = false;
    auto rt = unwrap(Runtime::open(options));
    auto pack = unwrap(rt->knowledge().pack()); auto& store = rt->knowledge().store();
    auto run = unwrap(store.begin_run(pack->hash(), Json::object())).id;
    materialize::Materializer original(*rt, store, pack);
    auto code = td.path() / "source.custom";
    LOOM_REQUIRE_OK(fsutil::write_file(code, "model == \"opaque\"\n"));
    auto before = unwrap(original.check_preferences(model::Product{}, {code.string()}));
    auto baseline = unwrap(original.self_description(run));
    overlay_at(td.path(), "materialize", Json{{"languages", Json{{".custom", "cpp"}}},
                                              {"templates", Json{{"self_header", "# Custom report {{run}}\n"}, {"self_title", "Custom report"}}}});
    auto after = unwrap(original.check_preferences(model::Product{}, {code.string()}));
    bool detected = false;
    for (const auto& check : before) if (check.check == "chk.enum_dispatch") CHECK(check.passed);
    for (const auto& check : after) if (check.check == "chk.enum_dispatch") { detected = true; CHECK_FALSE(check.passed); }
    CHECK(detected);
    auto custom = unwrap(original.self_description(run));
    CHECK(custom.title == "Custom report");
    CHECK(custom.markdown.rfind("# Custom report ", 0) == 0);
    CHECK(custom.data.at("input_hash") != baseline.data.at("input_hash"));
    CHECK(custom.data.contains("runtime_profile_hash"));
    overlay_at(td.path(), "materialize", Json{{"templates", Json{{"self_header", "{{absent}}"}}}});
    CHECK_FALSE(original.self_description(run));
  }

  TEST_CASE("materialize recipe selects order and subset and rejects unknown capabilities before writes") {
    fsutil::TempDir td("loom_");
    RuntimeOptions options; options.data_dir = td.path().string(); options.start_workers = false;
    auto rt = unwrap(Runtime::open(options));
    auto pack = unwrap(rt->knowledge().pack()); auto& store = rt->knowledge().store();
    kb::Normalizer normalizer(*pack); knowledge::KnowledgeConfig config;
    auto prepare = [&](std::string_view recipe_case) {
      auto run = unwrap(store.begin_run(pack->hash(), Json{{"recipe_case", recipe_case}})).id;
      std::vector<model::Entity> entities; std::vector<model::Instance> instances;
      for (std::string key : {"first", "second"}) {
        model::Entity project; project.kind = "project"; project.canonical_key = key; project.label = key;
        project.id = model::Entity::make_id(project.kind, key); entities.push_back(project);
        model::Instance instance; instance.paradigm = "software"; instance.subject = project.id;
        instance.subject_label = key; instance.id = model::Instance::make_id(instance.paradigm, instance.subject);
        instances.push_back(instance);
      }
      LOOM_REQUIRE_OK(store.put_entities(run, entities)); LOOM_REQUIRE_OK(store.put_instances(run, instances));
      return run;
    };
    auto stage = [&](const std::string& run) {
      knowledge::StageContext context{*rt, store, pack, normalizer, config, run, "materialize", Json::object(),
        Json::object(), model::PriorFilter{}, {}, {}, {}, std::nullopt, std::nullopt};
      return materialize::run_stage(context);
    };
    auto kinds = [](const Json& result) {
      std::vector<std::string> out;
      for (const auto& artifact : result.at("artifacts")) out.push_back(artifact.at("kind").get<std::string>());
      return out;
    };
    const auto baseline_run = prepare("baseline"); auto baseline = unwrap(stage(baseline_run));
    CHECK(kinds(baseline) == std::vector<std::string>{"self_description", "dossier", "extrapolated_spec", "dossier", "extrapolated_spec", "backlog"});
    CHECK_FALSE(baseline.contains("complete"));
    CHECK_FALSE(baseline.contains("omitted_products"));
    auto recipe = [](std::string_view renderer, bool enabled) {
      return Json{{"renderer", renderer}, {"enabled", enabled}, {"on_error", "error"}};
    };
    overlay_at(td.path(), "materialize", Json{{"products", Json::array({recipe("backlog", true), recipe("self_description", false),
      recipe("extrapolated_spec", true), recipe("dossier", true)})}});
    const auto custom_run = prepare("custom_subset");
    REQUIRE(custom_run != baseline_run);
    auto custom = unwrap(stage(custom_run));
    CHECK(kinds(custom) == std::vector<std::string>{"backlog", "extrapolated_spec", "dossier", "extrapolated_spec", "dossier"});
    CHECK(custom.at("stats").at("products") == Json(5));
    CHECK(unwrap(store.list_products(custom_run)).size() == 5);
    overlay_at(td.path(), "materialize", Json{{"templates", Json{{"dossier_header", "{{missing}}"}}}});
    const auto incomplete_run = prepare("omitted_dossier");
    REQUIRE(incomplete_run != baseline_run);
    REQUIRE(incomplete_run != custom_run);
    auto incomplete = unwrap(stage(incomplete_run));
    CHECK(incomplete.at("complete") == Json(false));
    CHECK(kinds(incomplete) == std::vector<std::string>{"self_description", "extrapolated_spec", "extrapolated_spec", "backlog"});
    CHECK(incomplete.at("stats").at("dossiers") == Json(0));
    CHECK(unwrap(store.list_products(incomplete_run)).size() == 4);
    REQUIRE(incomplete.at("omitted_products").size() == 2);
    auto omitted_instances = unwrap(store.query_instances(incomplete_run, "", ""));
    for (std::size_t index = 0; index < omitted_instances.size(); ++index) {
      const auto& omitted = incomplete.at("omitted_products").at(index);
      CHECK(omitted.at("renderer") == Json("dossier"));
      CHECK(omitted.at("key") == Json(omitted_instances[index].id));
      CHECK(omitted.at("error").at("code") == Json("invalid_argument"));
      CHECK(omitted.at("error").at("message").get<std::string>().find("missing") != std::string::npos);
    }
    const auto before = unwrap(rt->provenance().list_artifacts()).size();
    overlay_at(td.path(), "materialize", Json{{"products", Json::array({recipe("backlog", true), recipe("unavailable", false)})}});
    config.out_dir = (td.path() / "unwritten").string(); const auto bad_run = prepare("unknown_renderer");
    REQUIRE(bad_run != baseline_run);
    REQUIRE(bad_run != custom_run);
    REQUIRE(bad_run != incomplete_run);
    auto failed = stage(bad_run); REQUIRE_FALSE(failed); CHECK(failed.error().code == Errc::Unavailable);
    CHECK(unwrap(rt->provenance().list_artifacts()).size() == before);
    CHECK(unwrap(store.list_products(bad_run)).empty());
    CHECK_FALSE(std::filesystem::exists(config.out_dir));
  }

#endif

  TEST_CASE("parsed context omissions use profile settings and explicit fields take precedence") {
    fsutil::TempDir td("loom_");
    auto db = loom::test::open_db(td.path() / "graph.db"); Config cfg(td.path() / "config.json");
    auto analyzer = unwrap(SemanticAnalyzer::create()); GraphMemorySelector graph(*db, cfg, *analyzer);
    MemoryEngine memory(td.path() / "memory.json", analyzer.get());
    unwrap(memory.add_node("A synthetic retained memory value longer than four code points"));
    ContextSelector selector(*db, cfg, graph, &memory);
    overlay_at(td.path(), "graph_memory", Json{{"context", Json{{"max_tokens", 1}, {"include_memory", true}, {"include_graph", false}, {"include_search", false}}}});
    auto omitted = unwrap(selector.select(unwrap(ContextRequest::from_json(Json::object()))));
    CHECK(omitted.items.empty()); CHECK(omitted.truncated);
    auto explicit_budget = unwrap(selector.select(unwrap(ContextRequest::from_json(Json{{"max_tokens", 100}}))));
    CHECK(explicit_budget.items.size() == 1);
    overlay_at(td.path(), "graph_memory", Json{{"context", Json{{"max_tokens", 100}, {"include_memory", false}, {"include_graph", false}, {"include_search", false}}}});
    CHECK(unwrap(selector.select(unwrap(ContextRequest::from_json(Json::object())))).items.empty());
    auto explicit_channel = unwrap(selector.select(unwrap(ContextRequest::from_json(Json{{"include_memory", true}}))));
    CHECK(explicit_channel.items.size() == 1);
  }

  TEST_CASE("context explicit zero and channel failure behavior are presets") {
    fsutil::TempDir td("loom_");
    auto db = loom::test::open_db(td.path() / "graph.db"); Config cfg(td.path() / "config.json");
    auto analyzer = unwrap(SemanticAnalyzer::create()); GraphMemorySelector graph(*db, cfg, *analyzer);
    MemoryEngine memory(td.path() / "memory.json", analyzer.get());
    unwrap(memory.add_node("Retained memory longer than the one-token preset"));
    ContextSelector selector(*db, cfg, graph, &memory);
    overlay_at(td.path(), "graph_memory", Json{{"context", Json{{"max_tokens", 1}, {"include_graph", false}, {"include_search", false}}}});
    auto zero = unwrap(ContextRequest::from_json(Json{{"max_tokens", 0}}));
    CHECK(unwrap(selector.select(zero)).truncated);
    overlay_at(td.path(), "graph_memory", Json{{"context", Json{{"max_tokens", 1}, {"zero_as_default", false}, {"include_graph", false}, {"include_search", false}}}});
    auto unlimited = unwrap(selector.select(zero));
    CHECK(unlimited.items.size() == 1); CHECK_FALSE(unlimited.truncated);
    CHECK_FALSE(selector.select(unwrap(ContextRequest::from_json(Json{{"max_tokens", -1}}))));

    overlay_at(td.path(), "memory", Json{{"context_max_chars", "invalid"}});
    CHECK_FALSE(memory.reload());  // Immutable memory recipes change on explicit reload.
    CHECK_FALSE(selector.select(zero));
    overlay_at(td.path(), "graph_memory", Json{{"context", Json{{"include_graph", false}, {"include_search", false},
      {"failure_policy", Json{{"memory", "omit"}}}}}});
    CHECK(unwrap(selector.select(zero)).items.empty());

    { auto lock = db->lock(); LOOM_REQUIRE_OK(db->conn().exec("DROP TABLE messages")); }
    overlay_at(td.path(), "graph_memory", Json{{"context", Json{{"include_memory", false}, {"include_graph", false}, {"include_search", true}}}});
    auto search = unwrap(ContextRequest::from_json(Json{{"text", "synthetic"}}));
    CHECK(selector.select(search));  // Legacy FTS failure omission.
    overlay_at(td.path(), "graph_memory", Json{{"context", Json{{"include_memory", false}, {"include_graph", false}, {"include_search", true},
      {"failure_policy", Json{{"search", "error"}}}}}});
    CHECK_FALSE(selector.select(search));
  }

  TEST_CASE("context explicit zero depth reaches graph preset and malformed Config returns checked error") {
    fsutil::TempDir td("loom_");
    auto db = loom::test::open_db(td.path() / "graph.db"); Config cfg(td.path() / "config.json");
    auto analyzer = unwrap(SemanticAnalyzer::create()); GraphMemorySelector graph(*db, cfg, *analyzer);
    ContextSelector selector(*db, cfg, graph, nullptr);
    auto conv = unwrap(db->create_conv("Depth fixture")); auto mid = message(*db, conv.id, "Connected retained content");
    auto seed = unwrap(db->get_or_create_node("Widget", "code_ref")); unwrap(db->create_link(mid, seed, "mentions", 1));
    overlay_at(td.path(), "graph_memory", Json{{"zero_as_default", false},
      {"context", Json{{"include_memory", false}, {"include_search", false}}}});
    auto omitted = unwrap(ContextRequest::from_json(Json{{"text", "class Widget"}}));
    CHECK(unwrap(selector.select(omitted)).items.size() == 1);
    auto profile = unwrap(RuntimeProfile::load("graph_memory", td.path()));
    auto parsed_with_profile = unwrap(ContextRequest::from_json_with_profile(Json{{"text", "class Widget"}}, profile));
    CHECK_FALSE(parsed_with_profile.provided_fields.is_null());
    CHECK_FALSE(parsed_with_profile.provided_fields.contains("depth"));
    CHECK(unwrap(selector.select(parsed_with_profile)).items.size() == 1);
    auto zero = unwrap(ContextRequest::from_json(Json{{"text", "class Widget"}, {"depth", 0}}));
    CHECK(zero.provided_fields.contains("depth")); CHECK(unwrap(selector.select(zero)).items.empty());
    auto zero_with_profile = unwrap(ContextRequest::from_json_with_profile(Json{{"text", "class Widget"}, {"depth", 0}}, profile));
    CHECK(zero_with_profile.provided_fields.contains("depth"));
    CHECK(unwrap(selector.select(zero_with_profile)).items.empty());
    cfg.set("graph_memory_max_nodes", "invalid");
    auto invalid = graph.select_context_checked("class Widget");
    REQUIRE_FALSE(invalid); CHECK(invalid.error().code == Errc::InvalidArgument);
    GraphSelectOptions explicit_options; explicit_options.max_nodes = 20; explicit_options.depth = 2;
    CHECK(graph.select_context_checked("class Widget", explicit_options));
  }
}
