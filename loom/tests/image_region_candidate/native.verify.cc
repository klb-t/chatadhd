// Sole integrator's scoped runner only; uses its existing doctest main.
// No OCR engine, network, private media or provider is involved.
#ifdef LOOM_IMAGE_REGION_TEST_MAIN
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#endif
#include <doctest/doctest.h>
#include <limits>
#include "context/image_region_candidate/native.h"
#include "context/unified_context.h"
#include "import/image_representation.h"
#include "loom/graph_memory.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "test_helpers.h"

namespace {
using namespace loom;
using loom::test::unwrap;
namespace region = loom::context::image_region;

Json document() {
  const auto path = std::filesystem::path(__FILE__).parent_path() / "representation.json";
  return unwrap(json::parse(unwrap(fsutil::read_file(path))));
}
struct RegionFixture {
  fsutil::TempDir dir;
  std::unique_ptr<Database> db = unwrap(Database::open(dir.path() / "db.sqlite"));
  BlobStore blobs{dir.path() / "blobs", *db};
  ProvenanceStore prov{*db};
  Config cfg{dir.path() / "config.json"};
  std::unique_ptr<SemanticAnalyzer> analyzer = unwrap(SemanticAnalyzer::create());
  GraphMemorySelector graph{*db, cfg, *analyzer};
  Json projection;
  std::vector<Node> nodes;
  explicit RegionFixture(Json doc = document()) {
    const auto bytes = unwrap(fsutil::read_file(std::filesystem::path(__FILE__).parent_path() / "original.txt"));
    const auto blob = unwrap(blobs.put(bytes, "application/octet-stream"));
    SourceRecord original;
    original.id = "src_original"; original.kind = "synthetic_image";
    original.blob_hash = blob.hash; original.size = blob.size;
    unwrap(prov.add_source(original));
    OcrResult ocr;
    ocr.text = doc.at("text").get<std::string>(); ocr.representation = doc;
    const Json preset{{"source_kinds", {{"representation", "synthetic_ocr"}}},
        {"node_kinds", {{"source", "synthetic_image"}, {"region", "synthetic_region"}, {"representation", "synthetic_rep"}}},
        {"relations", {{"representation", "derived_from"}, {"region", "part_of"}}}};
    projection = unwrap(store_image_representation(*db, blobs, prov, original.id, ocr, preset));
    for (const auto& id : projection.at("region_nodes")) {
      auto n = unwrap(db->get_node(id.get<std::string>())); REQUIRE(n); nodes.push_back(*n);
    }
  }
  region::Access access() {
    return region::Access{[&](std::string_view id) { return prov.get_source(id); },
        [&](std::string_view hash) { return blobs.read(hash); },
        [&](std::string_view hash) { return blobs.verify(hash); }};
  }
  Json build(Json options, int* calls = nullptr, bool enabled = true) {
    context::ContextRequest request;
    const std::function<Result<Json>(const Node&, std::size_t, std::size_t)> builder =
        [&](const Node& n, std::size_t detail, std::size_t hops) {
          if (calls) ++*calls;
          return region::candidate(*db, blobs, n, detail, hops);
        };
    return unwrap(context::compile_unified_context(*db, cfg, &graph, nullptr,
        request, nullptr, "", false, enabled, options, {}, builder));
  }
  Json options() {
    return Json{{"legacy_seed_ids", Json::array({nodes.at(0).id})},
        {"legacy_relation_hops", 0}, {"budget_tokens", 10000}};
  }
};
}

TEST_SUITE("image_region_candidate") {
TEST_CASE("existing producer selected region retains inferred provenance without siblings") {
  RegionFixture f;
  const auto row = unwrap(region::candidate(*f.db, f.blobs, f.nodes[0], 0, 2));
  CHECK(row["text"] == "Zażółć 🙂");
  CHECK(row["evidence"] == "inferred"); CHECK(row["confidence"].is_null());
  CHECK(row["provenance"]["observation_scope"] == "stored_ocr_output");
  CHECK(row["provenance"]["original_sha256"] == document()["source_sha256"]);
  CHECK(row["provenance"]["representation_blob"] == f.projection["representation_blob"]);
  CHECK(row["provenance"]["locator"]["json_pointer"] == "/regions/0/text");
  CHECK(row["provenance"]["frame"]["extent"] == Json::array({100,50}));
  CHECK(row["provenance"]["bounds"]["origin"] == Json::array({2,3}));
  CHECK(row["relation_hops"] == 2);
  CHECK(row.dump().find("UNSELECTED_SECRET_SENTINEL") == std::string::npos);
  CHECK(row.dump().find("engine_score") == std::string::npos);
  const auto short_row = unwrap(region::candidate(*f.db, f.blobs, f.nodes[0], 4, 0));
  CHECK(short_row["text"] == "Zażó");
  CHECK(short_row["provenance"]["byte_span"] == Json::array({0,6}));
  CHECK(short_row["provenance"]["codepoint_span"] == Json::array({0,4}));
}
TEST_CASE("denied and missing source access never falls back to node content") {
  RegionFixture f;
  for (const auto* denied : {"representation", "original"}) {
    auto access = f.access(); int reads=0, verifies=0;
    access.source = [&](std::string_view id) -> Result<std::optional<SourceRecord>> {
      if ((std::string(denied) == "original") == (id == "src_original")) return Error(Errc::Auth,"synthetic_denial");
      return f.prov.get_source(id);
    };
    access.read_blob = [&](std::string_view) -> Result<std::string> { ++reads; return std::string("forbidden"); };
    access.verify_blob = [&](std::string_view) -> Status { ++verifies; return {}; };
    const auto result = region::candidate(f.nodes[0], access, 0, 0);
    REQUIRE_FALSE(result); CHECK(result.error().code == Errc::Auth); CHECK(reads == 0); CHECK(verifies == 0);
  }
  auto access = f.access();
  access.source = [](std::string_view) -> Result<std::optional<SourceRecord>> { return std::optional<SourceRecord>{}; };
  const auto missing = region::candidate(f.nodes[0],access,0,0);
  REQUIRE_FALSE(missing); CHECK(missing.error().code == Errc::NotFound);
  CHECK_FALSE(region::candidate(f.nodes[0], {}, 0, 0));
  access = f.access();
  access.verify_blob = [](std::string_view) -> Status { return Error(Errc::Conflict,"synthetic_corrupt_original"); };
  const auto corrupt_original = region::candidate(f.nodes[0],access,0,0);
  REQUIRE_FALSE(corrupt_original); CHECK(corrupt_original.error().message == "synthetic_corrupt_original");
  access = f.access();
  access.read_blob = [&](std::string_view hash) -> Result<std::string> { return unwrap(f.blobs.read(hash)) + " "; };
  const auto corrupt_rep = region::candidate(f.nodes[0],access,0,0);
  REQUIRE_FALSE(corrupt_rep); CHECK(corrupt_rep.error().message == "ocr_region_representation_integrity");
}
TEST_CASE("tampered locator identity derivation evidence and copied geometry are refused") {
  RegionFixture f;
  const auto reject = [&](auto change) {
    Node node=f.nodes[0]; change(node); CHECK_FALSE(region::candidate(node,f.access(),0,0));
  };
  reject([](Node& n){ n.metadata["unit"]["locator"]["json_pointer"]="/regions/1"; });
  reject([](Node& n){ n.metadata["observation"]["locator"]["member"]="foreign"; });
  reject([](Node& n){ n.metadata["unit"]["attrs"]["original_sha256"]=std::string(64,'0'); });
  reject([](Node& n){ n.metadata["unit"]["attrs"]["spatial_region"]["origin"][0]=0; });
  reject([](Node& n){ n.metadata["unit"]["attrs"]["frames"][0]["extent"][0]=1; });
  reject([](Node& n){ n.metadata["observation"]["attrs"]["method"]["id"]="other"; });
  reject([](Node& n){ n.metadata["observation"]["attrs"]["evidence_class"]="observed"; });
  reject([](Node& n){ n.metadata["observation"]["attrs"]["confidence"]=0.97; });
  reject([](Node& n){ n.content="summary is not OCR output"; });
  reject([](Node& n){ n.metadata["observation"]["ordinal"]=0.5; });
}
TEST_CASE("resealed malformed frame and coordinate shapes are refused") {
  const auto reject = [](auto change) {
    auto doc=document(); change(doc); RegionFixture f(doc);
    CHECK_FALSE(region::candidate(f.nodes[0],f.access(),0,0));
  };
  reject([](Json& d){ d["regions"][0]["origin"][0]=99; });
  reject([](Json& d){ d["regions"][0]["origin"][0]=1.5; });
  reject([](Json& d){ d["regions"][0]["size"]=Json::array({1,2,3}); });
  reject([](Json& d){ d["regions"][0]["origin"][0]=std::numeric_limits<std::uint64_t>::max(); });
  reject([](Json& d){ d["regions"][0]["frame"]="missing"; });
  reject([](Json& d){ d["frames"].push_back(d["frames"][0]); });
  reject([](Json& d){ d["frames"][0]["units"][0]="mm"; });
}
TEST_CASE("unified adapter uses same final rendered budget and configurable traversal limits") {
  RegionFixture f; auto options=f.options(); int calls=0;
  const auto full=f.build(options,&calls);
  REQUIRE(full["selected"].size()==1); CHECK(calls==1);
  CHECK(full["selected"][0]["text"]=="Zażółć 🙂");
  const auto prompt=full["prompt"].get<std::string>();
  CHECK(prompt.find("inferred")!=std::string::npos);
  CHECK(prompt.find("RECORDED MESSAGE")==std::string::npos);
  CHECK(full.dump().find("UNSELECTED_SECRET_SENTINEL")==std::string::npos);
  const auto used=full["used_tokens"].get<std::size_t>(); REQUIRE(used>0);
  CHECK(used==(utf8::length(prompt)+3)/4);
  options["budget_tokens"]=used; CHECK(f.build(options)["prompt"]==full["prompt"]);
  options["budget_tokens"]=used-1; CHECK(f.build(options)["selected"].empty());
  options["budget_tokens"]=0; CHECK(f.build(options)["prompt"]=="");
  options=f.options(); options["legacy_seed_ids"].push_back(f.nodes[1].id);
  options["legacy_max_messages"]=1; CHECK(f.build(options)["selected"].size()==1);
  options["legacy_max_messages"]=0; CHECK(f.build(options)["selected"].size()==2);
  options["legacy_max_visited"]=1; CHECK(f.build(options)["selected"].size()==1);
  options=f.options(); options["legacy_detail_chars"]=4; CHECK(f.build(options)["selected"][0]["text"]=="Zażó");
  calls=0; CHECK(f.build(options,&calls,false)["selected"].empty()); CHECK(calls==0);
}
TEST_CASE("review exclusion and missing reader do not expose cached OCR") {
  RegionFixture f;
  context::ContextRequest req;
  const auto missing=unwrap(context::compile_unified_context(*f.db,f.cfg,&f.graph,nullptr,
      req,nullptr,"",false,true,f.options()));
  CHECK(missing["selected"].empty());
  bool found=false;
  for (const auto& drop:missing["dropped"]) if (drop.value("drop_reason","")=="ocr_region_source_access_unavailable") found=true;
  CHECK(found);
  const auto excluded=unwrap(f.graph.review_excluded_decisions()); REQUIRE_FALSE(excluded.empty());
  auto meta=f.nodes[0].metadata; meta["review"]=Json{{"decision",excluded[0]}};
  NodePatch patch; patch.metadata=meta; LOOM_REQUIRE_OK(f.db->update_node(f.nodes[0].id,patch));
  int calls=0;
  CHECK(f.build(f.options(),&calls)["selected"].empty()); CHECK(calls==0);
}
}
