// catalog.h: Bloom/MinHash/Sketch, the offset-tracking JSON array scanner,
// alias/context-gate matching, and an end-to-end scan -> profile -> score ->
// select -> import round trip on a small hand-built corpus (determinism,
// dedup/versioning, resumable checkpoints, targeted import + provenance).
// The synthetic_dev fixture corpus (recall/precision/noise-trap FP) is
// evaluated separately in test_catalog_eval.cpp.
#include <doctest/doctest.h>

#include <tuple>

#include <fstream>

#include "catalog/catalog_internal.h"
#include "loom/catalog.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/importer.h"
#include "loom/loom.h"
#include "loom/tasks.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_analyzer.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"
#include "../third_party/miniz/miniz.h"

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

  TEST_CASE("identity aliases have Unicode word boundaries; intentional stems stay configurable") {
    SelfProfile p;
    p.terms = Json::array({Json{{"term", "AR"}, {"key", "ar"}, {"class", "alias"}, {"project", "display"}}});
    auto idx = AliasIndex::from_profile(p);
    CHECK(idx.find("garden hardware carbon arbitrary", 20).empty());
    CHECK(idx.find("żar aré _ar ar_", 20).empty());
    auto hits = idx.find("(ar), ar-based and ar", 20);
    REQUIRE(hits.size() == 3);
    CHECK(hits.front().key == "display");
    CHECK(hits.front().offset == 1);

    p.terms = Json::array({Json{{"term", "C++"}, {"key", "c++"}, {"class", "alias"}},
                           Json{{"term", "IO"}, {"key", "io"}, {"class", "alias"}}});
    auto symbols = AliasIndex::from_profile(p);
    CHECK(symbols.find("c++/io; (c++) xio nodec++", 20).size() == 3);

    p.terms = Json::array({Json{{"term", "abstrakcj"}, {"key", "abstrakcj"}, {"class", "principle"}}});
    auto stem = AliasIndex::from_profile(p);
    CHECK(stem.find("abstrakcja abstrakcje", 20).size() == 2);
    CHECK(stem.find("nieabstrakcja", 20).empty());

    p.terms = Json::array({Json{{"term", "Widget"}, {"key", "widget"}, {"class", "alias"},
                               {"project", "widget"}, {"prefix", true}}});
    auto configured = AliasIndex::from_profile(p);
    CHECK(configured.find("widgetapp widget", 20).size() == 2);
    CHECK(configured.find("otherwidget", 20).empty());
  }

  TEST_CASE("rejected alias boundaries do not hide overlapping valid phrases") {
    for (const auto& [alias, text, offset] : std::vector<std::tuple<std::string, std::string, int>>{
             {"a a", "xa a a", 3}, {"abc-ab", "xabc-abc-ab", 5}}) {
      SelfProfile p;
      p.terms = Json::array({Json{{"term", alias}, {"key", alias}, {"class", "alias"}}});
      auto hits = AliasIndex::from_profile(p).find(text, 20);
      REQUIRE(hits.size() == 1);
      CHECK(hits[0].offset == offset);
    }
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
  TEST_CASE("external profile graph fields match the actual runtime, while uncertain mapping and source loss remain explicit") {
    fsutil::TempDir source, data;
    const Json pattern{{"entity_type", "concept"}, {"pattern", "SYNTHETIC_EXTERNAL_PROFILE"},
                       {"flags", Json::array()}, {"confidence", 1.0}};
    const Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "semantic_analyzer"},
                       {"overrides", {{"rules", {{"entity_patterns", Json::array({pattern})}}}}}};
    auto profile_path = source.path() / "semantic_analyzer.pack";
    write_file(profile_path, overlay.dump());
    fs::create_directories(data.path() / "profiles");
    fs::create_symlink(profile_path, data.path() / "profiles/semantic_analyzer.pack");
    auto transport = std::make_shared<net::ScriptedTransport>();
    RuntimeOptions runtime_options;
    runtime_options.data_dir = data.path().string(); runtime_options.start_workers = false;
    runtime_options.http = transport;
    auto runtime = unwrap(Runtime::open(runtime_options));
    Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
    ScanConfig scan;
    scan.sources = {profile_path.string()};
    unwrap(catalog.scan(scan));
    auto units = unwrap(catalog.query(UnitQuery{}));
    REQUIRE(units.size() == 1);
    const auto id = units[0].unit.id;
    auto resource = unwrap(catalog.read_resource(id));
    REQUIRE(resource["current"] == true);
    const auto snapshot = resource["last_successful"];
    CHECK(snapshot["mapping_status"] == "recognized");
    CHECK(snapshot["coverage"] == "runtime_profile");
    CHECK(snapshot["activation"] == "not_requested"); // reading never installs a profile
    REQUIRE(!snapshot["nodes"].empty());
    CHECK(snapshot["nodes"][0]["metadata"]["hash"] == runtime->analyzer().profile_hash());
    CHECK(snapshot["nodes"][0]["metadata"]["hash"] == unwrap(RuntimeProfile::load("semantic_analyzer", data.path())).hash());
    bool field_found = false;
    for (const auto& node : snapshot["nodes"]) {
      if (node["kind"] == "runtime:profile_field" && node["metadata"]["pointer"] == "/rules/entity_patterns/0/pattern") {
        field_found = true;
        CHECK(node["content"] == Json("SYNTHETIC_EXTERNAL_PROFILE").dump());
        REQUIRE(unwrap(runtime->db().get_node(node["id"].get<std::string>())).has_value());
      }
    }
    CHECK(field_found);
    const auto analysis = runtime->analyzer().analyse("SYNTHETIC_EXTERNAL_PROFILE");
    REQUIRE(analysis.entities.size() == 1);
    CHECK(analysis.entities[0].entity_type == "concept");
    CHECK(analysis.entities[0].text == "SYNTHETIC_EXTERNAL_PROFILE");

    const auto task_id = unwrap(runtime->tasks().submit("catalog.read_resource", Json{{"unit_id", id}}));
    LOOM_REQUIRE_OK(runtime->tasks().pause(task_id));
    CHECK(unwrap(runtime->tasks().get(task_id))->status == "paused");
    LOOM_REQUIRE_OK(runtime->tasks().resume(task_id));
    CHECK(unwrap(runtime->tasks().run_sync(task_id)).status == "done");

    auto unknown = overlay;
    unknown["future_mapping"] = Json{{"preserve", "synthetic unknown field"}};
    auto unknown_path = source.path() / "unknown.json";
    const auto unknown_bytes = unknown.dump();
    write_file(unknown_path, unknown_bytes);
    scan.sources = {unknown_path.string()};
    unwrap(catalog.scan(scan));
    units = unwrap(catalog.query(UnitQuery{}));
    REQUIRE(units.size() == 2);
    for (const auto& unit : units) {
      if (unit.unit.id == id) continue;
      auto uncertain = unwrap(catalog.read_resource(unit.unit.id));
      CHECK(uncertain["last_successful"]["mapping_status"] == "uncertain");
      CHECK(uncertain["last_successful"]["mapping_version"] == "loom.runtime_profile_overlay/1");
      CHECK(uncertain["last_successful"].contains("mapping_error"));
      CHECK(uncertain["last_successful"]["activation"] == "not_requested");
      CHECK(uncertain["last_successful"]["coverage"] == "syntax_only");
      bool unknown_field_found = false;
      for (const auto& node : uncertain["last_successful"]["nodes"]) {
        CHECK(node["kind"] != "runtime:profile");
        if (node["kind"] == "external:json_field" && node["metadata"]["pointer"] == "/future_mapping/preserve") {
          unknown_field_found = true;
          CHECK(node["content"] == ""); // unclassified values do not leak into graph exports
          CHECK(node["metadata"]["value_ref"]["unit_id"] == unit.unit.id);
          CHECK(unwrap(json::parse(unwrap(catalog.read_unit(unit.unit.id)))).at(Json::json_pointer("/future_mapping/preserve")) == "synthetic unknown field");
        }
      }
      CHECK(unknown_field_found);
      CHECK(unwrap(catalog.read_unit(unit.unit.id)) == unknown_bytes);
      CHECK(unwrap(fsutil::read_file(unknown_path)) == unknown_bytes);
    }
    CHECK(runtime->analyzer().profile_hash() == snapshot["nodes"][0]["metadata"]["hash"].get<std::string>());

    fs::rename(profile_path, source.path() / "unavailable.pack");
    auto unavailable = unwrap(catalog.read_resource(id));
    CHECK(unavailable["current"] == false);
    CHECK(unavailable["last_successful"] == snapshot);
    CHECK(unwrap(runtime->db().get_node("resource:" + id))->metadata["availability"]["current"] == false);
    RuntimeOptions options;
    options.data_dir = data.path().string(); options.start_workers = false;
    auto rejected = Runtime::open(options);
    CHECK_FALSE(rejected.has_value()); // dangling external reference is not absent preference/defaults
    fs::rename(source.path() / "unavailable.pack", profile_path);
    auto reopened = unwrap(Runtime::open(options));
    Catalog restored(*reopened, unwrap(reopened->knowledge().pack()));
    CHECK(unwrap(restored.read_resource(id))["last_successful"] == snapshot);
    CHECK(reopened->analyzer().profile_hash() == runtime->analyzer().profile_hash());
    CHECK(transport->requests().empty());
  }
  TEST_CASE("ZIP references expose lossless provider graph to headless and view consumers, retaining stale evidence") {
    fsutil::TempDir source, linked_data, copied_data;
    auto document = unwrap(json::parse(chatgpt_fixture()));
    document[0]["future_field"] = Json{{"keep", Json::array({nullptr, 42, "unknown"})}};
    auto branch = document[0]["mapping"]["n2"];
    branch["id"] = "alternate";
    branch["message"]["id"] = "alternate";
    branch["message"]["content"]["parts"] = Json::array({"An alternative answer."});
    document[0]["mapping"]["alternate"] = branch;
    document[0]["mapping"]["n1"]["children"].push_back("alternate");
    auto bytes = json::dump(document);
    auto zip_path = source.path() / "safe-conversations.zip";
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_file(&zip, zip_path.string().c_str(), 0));
    REQUIRE(mz_zip_writer_add_mem(&zip, "conversations.json", bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    REQUIRE(mz_zip_writer_end(&zip));

    auto linked = open_rt(linked_data.path());
    Catalog catalog(*linked, unwrap(linked->knowledge().pack()));
    ScanConfig scan;
    scan.sources = {zip_path.string()};
    unwrap(catalog.scan(scan));
    auto units = unwrap(catalog.query(UnitQuery{}));
    REQUIRE(units.size() == 2);
    auto first = std::find_if(units.begin(), units.end(), [](const auto& unit) { return unit.ext_id == "conv-a"; });
    REQUIRE(first != units.end());
    const auto id = first->unit.id;
    auto resolved = unwrap(catalog.read_resource(id));
    REQUIRE(resolved["current"] == true);
    const auto snapshot = resolved["last_successful"];
    CHECK(snapshot["raw"] == document[0]);
    CHECK(snapshot["mapping_status"] == "recognized");
    REQUIRE(snapshot["nodes"].size() == 4);
    CHECK(snapshot["edges"].size() == 5); // three contains + two parent edges
    CHECK(unwrap(linked->db().get_links(std::nullopt, "parent")).size() == 2);
    auto task_id = unwrap(linked->tasks().submit("catalog.read_resource", Json{{"unit_id", id}}));
    auto task = unwrap(linked->tasks().run_sync(task_id));
    REQUIRE(task.status == "done");
    REQUIRE(task.result.has_value());
    CHECK((*task.result)["last_successful"] == snapshot);

    // Real lossless importer, rather than a second expected-result parser.
    auto copied = open_rt(copied_data.path());
    loom::ImportOptions import_options;
    import_options.export_mode = ExportMode::On;
    auto imported = unwrap(copied->importer().import_file(zip_path, import_options));
    REQUIRE(imported.conversations.size() == 2);
    Catalog copied_catalog(*copied, unwrap(copied->knowledge().pack()));
    unwrap(copied_catalog.scan(scan));
    int compared = 0;
    for (const auto& conversation : imported.conversations) {
      auto stored = unwrap(copied->db().get_conv(conversation.id));
      REQUIRE(stored.has_value());
      auto unit = std::find_if(units.begin(), units.end(), [&](const auto& candidate) {
        return candidate.ext_id == stored->metadata["export"]["key"].get<std::string>();
      });
      REQUIRE(unit != units.end());
      const auto reference_graph = unwrap(catalog.read_resource(unit->unit.id))["last_successful"];
      CHECK(unwrap(copied_catalog.read_resource(unit->unit.id))["last_successful"] == reference_graph);
      ++compared;
      CHECK(reference_graph["nodes"][0]["metadata"]["graph"] == stored->metadata["export"]["graph"]);
      auto messages = unwrap(copied->db().get_msgs(conversation.id, true));
      REQUIRE(messages.size() == reference_graph["nodes"].size() - 1);
      for (const auto& message : messages) {
        bool found = false;
        for (const auto& node : reference_graph["nodes"]) {
          if (node["kind"] != "export:message" || node["metadata"]["export"]["key"] != message.metadata["export"]["key"]) continue;
          found = true;
          CHECK(node["content"] == message.text);
          CHECK(node["metadata"]["status"] == message.status);
          CHECK(node["metadata"]["export"]["raw"] == message.metadata["export"]["raw"]);
        }
        CHECK(found);
      }
    }
    CHECK(compared == 2);

    // Existing endpoint used by CatalogPane resolves through the same API.
    const auto opts = json::dump(Json{{"data_dir", linked_data.path().string()}, {"start_workers", false}});
    const char* error = nullptr;
    auto* context = loom_init_ex(opts.c_str(), &error);
    REQUIRE(context != nullptr);
    auto* response = loom_catalog_preview(context, id.c_str());
    auto preview = json::parse(response);
    loom_free_string(response);
    loom_shutdown(context);
    REQUIRE(preview.has_value());
    CHECK((*preview)["resource"]["last_successful"] == snapshot);

    // Projection vocabulary is data, while domain data and runtime are pinned.
    const auto analyzer_hash = linked->analyzer().profile_hash();
    const auto projection_path = linked_data.path() / "profiles/resource_projection.pack";
    const Json vocabulary_overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "resource_projection"},
      {"overrides", {{"kinds", {{"message", "synthetic:resource_message"}}},
                     {"predicates", {{"parent", "synthetic_resource_parent"}}}}}};
    write_file(projection_path, vocabulary_overlay.dump());
    auto alternative = unwrap(catalog.read_resource(id))["last_successful"];
    CHECK(alternative["projection_profile"]["hash"] != snapshot["projection_profile"]["hash"]);
    CHECK(alternative["nodes"][1]["kind"] == "synthetic:resource_message");
    CHECK(alternative["raw"] == snapshot["raw"]);
    CHECK(unwrap(linked->db().get_links(std::nullopt, "synthetic_resource_parent")).size() == 2);
    CHECK(linked->analyzer().profile_hash() == analyzer_hash);
    auto invalid_vocabulary = vocabulary_overlay;
    invalid_vocabulary["overrides"]["kinds"]["message"] = "";
    write_file(projection_path, invalid_vocabulary.dump());
    CHECK_FALSE(catalog.read_resource(id).has_value());
    invalid_vocabulary["overrides"] = Json::object();
    invalid_vocabulary["patch"] = Json::array({Json{{"op", "remove"}, {"path", "/kinds/message"}}});
    write_file(projection_path, invalid_vocabulary.dump());
    CHECK_FALSE(catalog.read_resource(id).has_value());
    CHECK(unwrap(linked->db().get_node("resource:" + id))->metadata["projection_profile"]["hash"] == alternative["projection_profile"]["hash"]);
    write_file(projection_path, Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "resource_projection"},
                                    {"overrides", Json::object()}}.dump());
    CHECK(unwrap(catalog.read_resource(id))["last_successful"] == snapshot);

    fs::rename(zip_path, source.path() / "temporarily-unavailable.zip");
    auto unavailable = unwrap(catalog.read_resource(id));
    CHECK(unavailable["current"] == false);
    CHECK(unavailable["status"] == "unavailable");
    CHECK(unavailable["last_successful"] == snapshot);
    linked.reset();
    linked = open_rt(linked_data.path());
    Catalog reopened(*linked, unwrap(linked->knowledge().pack()));
    CHECK(unwrap(reopened.read_resource(id))["last_successful"] == snapshot);
    fs::rename(source.path() / "temporarily-unavailable.zip", zip_path);
    CHECK(unwrap(reopened.read_resource(id))["current"] == true);
    // Replacing the container does not silently replace the pinned fragment.
    document[0]["mapping"]["n1"]["message"]["content"]["parts"][0] = "Sorblex needs a new storage layer.";
    bytes = json::dump(document);
    zip = {};
    REQUIRE(mz_zip_writer_init_file(&zip, zip_path.string().c_str(), 0));
    REQUIRE(mz_zip_writer_add_mem(&zip, "conversations.json", bytes.data(), bytes.size(), 0));
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    REQUIRE(mz_zip_writer_end(&zip));
    auto changed = unwrap(reopened.read_resource(id));
    CHECK(changed["current"] == false);
    CHECK(changed["status"] == "source_changed");
    CHECK(changed["last_successful"] == snapshot);
  }
  TEST_CASE("BM25 normalizes all profile terms and philosophy evidence permits bounded link support") {
    fsutil::TempDir data_dir;
    fsutil::TempDir src_dir;
    Json conversations = Json::array();
    for (const auto& [id, text] : std::vector<std::pair<std::string, std::string>>{
             {"stem", "An orchid flourishes."},
             {"separator", "A copper pipe feeds the valve."},
             {"short", "VR headset."},
             {"philosophy", "Prisms carved from granite."},
             {"seed", "Nebulite."},
             {"unrelated", "Cloudy breezes arrive."}}) {
      conversations.push_back(Json{{"id", id}, {"title", "note"}, {"create_time", 1700000000},
          {"current_node", "n"}, {"mapping", Json{{"n", Json{{"id", "n"}, {"parent", nullptr},
              {"children", Json::array()}, {"message", Json{{"id", "n"},
                  {"author", Json{{"role", "user"}}},
                  {"content", Json{{"content_type", "text"}, {"parts", Json::array({text})}}},
                  {"create_time", 1700000001}}}}}}}});
    }
    write_file(src_dir.path() / "conversations.json", json::dump(conversations));
    auto rt = open_rt(data_dir.path());
    Catalog cat(*rt, unwrap(rt->knowledge().pack()));
    ScanConfig scan;
    scan.sources = {src_dir.path().string()};
    unwrap(cat.scan(scan));

    // Isolate the profile from bundled owner vocabulary so this regression
    // exercises normalization and evidence channels, not pack-specific terms.
    auto profile = unwrap(cat.build_profile(ProfileConfig{}));
    profile.terms = Json::array();
    profile.projects = Json::array();
    for (const auto& surface : {"orchids", "copper_valve", "nebulite"}) {
      profile.terms.push_back(Json{{"term", surface}, {"key", surface}, {"class", "alias"}});
    }
    profile.terms.push_back(Json{{"term", "granite prism"}, {"key", "granite prism"}, {"class", "principle"}});
    profile.terms.push_back(Json{{"term", "VR"}, {"key", "vr"}, {"class", "identifier"}});
    {
      auto lk = rt->db().lock();
      unwrap(rt->db().conn().run("UPDATE loom_cat_profiles SET body = ? WHERE id = ?",
                                json::dump(profile.to_json()), profile.id));
    }
    ScoreConfig score;
    score.profile_id = profile.id;
    score.max_passes = 1;
    unwrap(cat.score(score));
    auto units = unwrap(cat.query(UnitQuery{}));
    REQUIRE(units.size() == 6);
    for (const auto& unit : units) {
      auto preview = unwrap(cat.preview(unit.unit.id));
      const auto& features = preview["score"]["features"];
      INFO("ext_id=" << unit.ext_id << " features=" << features.dump());
      if (unit.ext_id == "stem" || unit.ext_id == "separator" || unit.ext_id == "short") {
        CHECK(json::get_number(features, "id_hits") == 0.0);
        CHECK(json::get_number(features, "bm25_self") > 0.0);
      } else if (unit.ext_id == "philosophy") {
        CHECK(json::get_number(features, "id_hits") == 0.0);
        CHECK(json::get_number(features, "bm25_self") == 0.0);
        CHECK(json::get_number(features, "bm25_phil") > 0.05);
        CHECK(json::get_number(features, "link") > 0.0);
      } else if (unit.ext_id == "unrelated") {
        CHECK(json::get_number(features, "id_hits") == 0.0);
        CHECK(json::get_number(features, "bm25_self") == 0.0);
        CHECK(json::get_number(features, "bm25_phil") == 0.0);
        CHECK(json::get_number(features, "link") == 0.0);
      }
    }
  }

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

    catalog::ImportOptions iopts;
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
    catalog::ImportOptions iopts;
    iopts.dry_run = true;
    auto imp = unwrap(cat.import_selected(iopts));
    CHECK(json::get_int(imp, "imported") == 1);
    CHECK(unwrap(rt->db().list_convs(10)).empty());
  }

  TEST_CASE("import mode=full ignores selection; store_mode=link writes a placeholder, not the full text") {
    fsutil::TempDir data_dir;
    fsutil::TempDir src_dir;
    write_file(src_dir.path() / "conversations.json", chatgpt_fixture());
    auto rt = open_rt(data_dir.path());
    Catalog cat(*rt, unwrap(rt->knowledge().pack()));
    ScanConfig scfg;
    scfg.sources = {src_dir.path().string()};
    unwrap(cat.scan(scfg));
    // No profile/score/select at all: mode=full must still import everything.

    catalog::ImportOptions full_opts;
    full_opts.mode = "full";
    auto imp = unwrap(cat.import_selected(full_opts));
    CHECK(json::get_int(imp, "imported") == 2);  // both conversations, lossless
    REQUIRE(imp["conversations"].size() == 2);
    auto msgs_a = unwrap(rt->db().get_msgs(imp["conversations"][0].get<std::string>(), true));
    CHECK(!msgs_a.empty());

    // A fresh data dir for the link-mode variant.
    fsutil::TempDir data_dir2;
    auto rt2 = open_rt(data_dir2.path());
    Catalog cat2(*rt2, unwrap(rt2->knowledge().pack()));
    unwrap(cat2.scan(scfg));
    catalog::ImportOptions link_opts;
    link_opts.mode = "full";
    link_opts.store_mode = "link";
    auto imp2 = unwrap(cat2.import_selected(link_opts));
    CHECK(json::get_int(imp2, "imported") == 2);
    for (const auto& c : imp2["conversations"]) {
      auto msgs = unwrap(rt2->db().get_msgs(c.get<std::string>(), true));
      REQUIRE(msgs.size() == 1);  // one placeholder, not the real per-message content
      CHECK(json::get_string(msgs[0].metadata, "store_mode") == "link");
      CHECK(json::find(msgs[0].metadata, "content_hash") != nullptr);
    }
  }
}
