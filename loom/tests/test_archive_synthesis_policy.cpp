// Pure synthesis policy fixtures; the same source is a frozen before/after
// probe with ARCHIVE_SYNTHESIS_POLICY_PROBE. No Runtime or transport is used.
#include <iostream>
#include <set>
#include <stdexcept>

#include "archive/archive_runtime.h"
#include "archive/profile.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

#ifndef ARCHIVE_SYNTHESIS_POLICY_PROBE
#include "test_helpers.h"
#endif

using namespace loom;
using namespace loom::archive;

namespace {
struct Fixture {
  Corpus corpus;
  Fixture() {
    Doc spec;
    spec.key = "spec";
    spec.unit = "spec-unit";
    spec.kind = "doc";
    spec.title = "Synthetic Spec";
    spec.label = "API";
    spec.uri = "docs/spec.md";
    spec.date = "2026-01-01T12:00:00Z";
    spec.text = "IWidget ProtoWidget PresentThing UsedThing CommentOnly ABIs lowerCamelName.\n"
                "- Alpha Feature — detailed explanation\n- Cache Module: implementation details\n"
                "- Other Module: extra source words\nReferenced src/missing.cpp.\n";
    corpus.docs.push_back(spec);
    for (int i = 0; i < 4; ++i) {
      Doc code;
      code.key = "code-" + std::to_string(i);
      code.unit = code.key;
      code.kind = "code";
      code.title = "synthetic.cpp";
      code.uri = "src/synthetic-" + std::to_string(i) + ".cpp";
      code.date = "2026-01-02T12:00:00Z";
      code.text = "class Widget {}; class PresentThing {}; // CommentOnly\n";
      code.extra = Json{{"symbols", Json::array({"Widget", "PresentThing"})},
                        {"uses", Json::array({"UsedThing"})}, {"language", "cpp"},
                        {"todos", Json::array({Json{{"line", 1}, {"text", "TODO " + std::string(180, 'x')}}})}};
      corpus.docs.push_back(code);
    }
    corpus.sources = Json::array({Json{{"uri", "synthetic"}, {"adapter", "fixture"}}});
    corpus.forks = Json::array({Json{{"origin", "synthetic"}, {"title", std::string(80, 'f')}, {"after", "spec"},
                                    {"alternatives", Json::array({Json{{"first", "code-0"}, {"messages", 2}, {"current", true}},
                                                                 Json{{"first", "code-1"}, {"messages", 1}, {"current", false}}})}}});
    corpus.reindex();
  }
  SynthesisInput input(const ArchiveProfile* profile = nullptr) const {
    SynthesisInput in;
    in.profile = profile;
    in.corpus = &corpus;
    in.project = "Synthetic";
    in.round = 2;
    in.passes = Json::array({Json{{"pass", 1}, {"terms", 2}, {"hits", 5}, {"new_hits", 5}, {"added", Json::array({"Widget"})}}});
    TermRecord seed;
    seed.term = "synthetic";
    seed.origin = "seed";
    in.vocab.push_back(seed);
    TermRecord expanded;
    expanded.term = "Widget";
    expanded.origin = "expansion";
    expanded.pass = 1;
    expanded.reasons = {"synthetic reason"};
    expanded.evidence = {"spec"};
    in.vocab.push_back(expanded);
    Json keys = Json::array();
    Json events = Json::array();
    for (const auto& doc : corpus.docs) {
      keys.push_back(doc.key);
      in.hits.push_back(Json{{"key", doc.key}, {"score", 1.23456}, {"terms", Json::array({"alpha", "beta"})}});
      in.doc_theme[doc.key] = "theme-1";
      events.push_back(Json{{"key", doc.key}, {"date", "2026-01-02"}});
    }
    in.themes = Json::array({Json{{"id", "theme-1"}, {"label", "Synthetic theme"}, {"size", 5},
                                 {"docs", keys}, {"terms", Json::array({"alpha", "beta"})}}});
    in.timeline = Json::array({Json{{"id", "theme-1"}, {"label", "Synthetic theme"}, {"first", "2026-01-01"},
                                   {"last", "2026-01-02"}, {"events", events}, {"forks", corpus.forks}}});
    in.global_terms = Json::array({"synthetic"});
    for (int i = 0; i < 83; ++i) {
      Item item;
      item.id = "requirement-" + std::to_string(i);
      item.type = "requirement";
      item.text = "requirement " + std::to_string(i) + " " + std::string(280, 'x');
      item.doc = "spec";
      item.unit = "spec-unit";
      item.theme = "theme-1";
      item.date = "2026-01-01";
      item.confidence = .8;
      in.items.push_back(item);
    }
    for (const auto& suffix : {"first", "second"}) {
      Item item;
      item.id = "duplicate-prefix-" + std::string(suffix);
      item.type = "requirement";
      item.text = std::string(150, 'y') + " " + suffix;
      item.doc = "spec";
      item.confidence = .9;
      in.items.push_back(item);
    }
    Item old;
    old.id = "decision-old";
    old.type = "decision";
    old.text = "Old decision " + std::string(180, 'o');
    old.doc = "spec";
    old.status = "superseded";
    Item current = old;
    current.id = "decision-new";
    current.text = "New decision " + std::string(180, 'n');
    current.status = "active";
    in.items.push_back(old);
    in.items.push_back(current);
    in.edges.push_back(ItemEdge{current.id, old.id, "supersedes", "synthetic replacement"});
    return in;
  }
};

[[maybe_unused]] const Json* component(const SynthesisOutput& out, std::string_view name) {
  for (const auto& row : out.gap["components"]) if (json::get_string(row, "name") == name) return &row;
  return nullptr;
}
}

#ifdef ARCHIVE_SYNTHESIS_POLICY_PROBE
int main() {
  Fixture fixture;
  const auto out = synthesize(fixture.input());
  std::cout << json::canonical(Json{{"files", out.files}, {"gap", out.gap}, {"discovered_terms", out.discovered_terms}}) << '\n';
}
#else
using loom::test::unwrap;
TEST_SUITE("archive.synthesis_policy") {
  TEST_CASE("builtin synthesis preserves filenames CSV and full manifest graph contracts") {
    Fixture fixture;
    const auto out = synthesize(fixture.input());
    std::set<std::string> names;
    for (const auto& [name, body] : out.files) {
      CHECK_FALSE(body.empty());
      names.insert(name);
    }
    CHECK(names == std::set<std::string>{"MASTER.md", "gap_report.md", "source_map.csv", "items.jsonl",
                                         "graph.json", "timeline.json", "project_manifest.json"});
    CHECK(out.files.at("source_map.csv").rfind("ref,key,kind,title,location,date,uri,theme,score,terms\n", 0) == 0);
    CHECK(out.files.at("MASTER.md").find("Full details: `gap_report.md`.") != std::string::npos);
    CHECK(unwrap(json::parse(out.files.at("project_manifest.json")))["requirements"].size() == 80);
    const auto* iface = component(out, "IWidget");
    REQUIRE(iface);
    CHECK((*iface)["status"] == "partial");
    CHECK((*iface)["evidence"].size() == 3);
    CHECK_FALSE(component(out, "ABIs"));
    CHECK_FALSE(component(out, "lowerCamelName"));
    const auto graph = unwrap(json::parse(out.files.at("graph.json")));
    CHECK(graph.contains("project"));
    CHECK(graph.contains("nodes"));
    CHECK(graph.contains("edges"));
  }

  TEST_CASE("display dedup and projection caps are independently configurable beyond old presets") {
    Fixture fixture;
    auto base = unwrap(ArchiveProfile::builtin());
    auto wider = unwrap(base.with_overrides(Json{{"synthesis_rendering", Json{
        {"selection", Json{{"manifest_items", 100}}},
        {"display", Json{{"item_codepoints", 400}, {"graph_item_codepoints", 400}, {"manifest_item_codepoints", 400}}}}}}));
    const auto full = synthesize(fixture.input(&wider));
    CHECK(unwrap(json::parse(full.files.at("project_manifest.json")))["requirements"].size() == 84);
    auto distinct = unwrap(wider.with_overrides(Json{{"synthesis_rendering", Json{{"display", Json{{"dedup_codepoints", 300},
        {"graph_item_codepoints", 5}, {"manifest_item_codepoints", 11}}}}}}));
    const auto clipped = synthesize(fixture.input(&distinct));
    const auto manifest = unwrap(json::parse(clipped.files.at("project_manifest.json")));
    CHECK(manifest["requirements"].size() == 85);
    for (const auto& item : manifest["requirements"]) CHECK(utf8::length(json::get_string(item, "text")) <= 11);
    const auto graph = unwrap(json::parse(clipped.files.at("graph.json")));
    for (const auto& node : graph["nodes"]) {
      if (json::get_string(node, "kind") == "requirement") CHECK(utf8::length(json::get_string(node, "label")) <= 5);
    }
    CHECK(clipped.files.at("items.jsonl") == full.files.at("items.jsonl"));
  }

  TEST_CASE("name and interface conventions use configured prefixes and acronym rules") {
    Fixture fixture;
    auto base = unwrap(ArchiveProfile::builtin());
    auto changed = unwrap(base.with_overrides(Json{{"synthesis_rendering", Json{
        {"identifiers", Json{{"require_uppercase_initial", false}, {"exclude_plural_acronyms", false}}},
        {"interfaces", Json{{"prefixes", Json::array({"Proto"})}}}}}}));
    const auto out = synthesize(fixture.input(&changed));
    REQUIRE(component(out, "ProtoWidget"));
    CHECK((*component(out, "ProtoWidget"))["status"] == "partial");
    CHECK(json::get_string(*component(out, "ProtoWidget"), "note").find("concrete `Widget`") != std::string::npos);
    REQUIRE(component(out, "IWidget"));
    CHECK((*component(out, "IWidget"))["status"] == "missing");
    CHECK(component(out, "ABIs"));
    CHECK(component(out, "lowerCamelName"));
  }

  TEST_CASE("evidence projections can increase or omit files while recorded status remains factual") {
    Fixture fixture;
    auto base = unwrap(ArchiveProfile::builtin());
    auto changed = unwrap(base.with_overrides(Json{{"synthesis_rendering", Json{{"component_evidence", Json{
        {"declaration_files", 4}, {"used_files", 4}, {"comment_files", 0}}}}}}));
    const auto out = synthesize(fixture.input(&changed));
    REQUIRE(component(out, "PresentThing"));
    CHECK((*component(out, "PresentThing"))["status"] == "implemented");
    CHECK((*component(out, "PresentThing"))["evidence"].size() == 4);
    REQUIRE(component(out, "UsedThing"));
    CHECK((*component(out, "UsedThing"))["status"] == "used");
    CHECK((*component(out, "UsedThing"))["evidence"].size() == 4);
    REQUIRE(component(out, "CommentOnly"));
    CHECK((*component(out, "CommentOnly"))["status"] == "missing");
    CHECK((*component(out, "CommentOnly"))["evidence"].empty());
  }

  TEST_CASE("feature head token bounds and separators change parsing without changing wire keys") {
    Fixture fixture;
    auto base = unwrap(ArchiveProfile::builtin());
    auto changed = unwrap(base.with_overrides(Json{{"synthesis_rendering", Json{{"feature_terms", Json{{"maximum_tokens", 1}}}}}}));
    const auto builtin = synthesize(fixture.input(&base));
    const auto custom = synthesize(fixture.input(&changed));
    REQUIRE_FALSE(builtin.gap["sections"].empty());
    REQUIRE_FALSE(custom.gap["sections"].empty());
    CHECK(builtin.gap["sections"][0]["features"][0]["feature"] == "Alpha Feature");
    CHECK(custom.gap["sections"][0]["features"][0]["feature"] == "Alpha Feature — detailed explanation");
  }

  TEST_CASE("artifact layout CSV projections and report templates are data and insertion stays inert") {
    Fixture fixture;
    auto base = unwrap(ArchiveProfile::builtin());
    auto changed = unwrap(base.with_overrides(Json{{"synthesis_rendering", Json{
        {"files", Json{{"master", "OVERVIEW.md"}, {"gap", "GAPS.md"}, {"source_map", "SOURCES.csv"}}},
        {"csv", Json{{"score_precision", 3}, {"columns", Json::array({Json{{"name", "URI"}, {"path", "/uri"}, {"quote", true}},
                                                                      Json{{"name", "score"}, {"path", "/score"}, {"quote", false}}})}}},
        {"strings", Json{{"master_generated", " — CUSTOM {{project}}\n\n"}}}}}}));
    auto in = fixture.input(&changed);
    in.project = "{{not_a_variable}}";
    const auto out = synthesize(in);
    CHECK_FALSE(out.files.contains("MASTER.md"));
    CHECK(out.files.at("SOURCES.csv").rfind("URI,score\n\"docs/spec.md\",1.235\n", 0) == 0);
    CHECK(out.files.at("OVERVIEW.md").find("CUSTOM {{not_a_variable}}") != std::string::npos);
    CHECK(out.files.at("OVERVIEW.md").find("Full details: `GAPS.md`.") != std::string::npos);
    const auto disabled = unwrap(changed.with_overrides(Json{{"synthesis_rendering", Json{{"files", Json{{"graph", ""}}}}}}));
    CHECK_FALSE(synthesize(fixture.input(&disabled)).files.contains("graph.json"));
    const auto collision = unwrap(base.with_overrides(Json{{"synthesis_rendering", Json{{"files", Json{{"gap", "MASTER.md"}}}}}}));
    CHECK_THROWS_AS(synthesize(fixture.input(&collision)), std::invalid_argument);
    const auto missing = unwrap(base.with_overrides(Json{{"synthesis_rendering", Json{{"strings", Json{{"master_generated", "{{missing}}"}}}}}}));
    CHECK_THROWS_AS(synthesize(fixture.input(&missing)), std::invalid_argument);
  }
}
#endif
