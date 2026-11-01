// Integrator-only native/runtime gate; author runs stdlib and syntax checks.
#include <doctest/doctest.h>
#include <cstdlib>
#include <set>
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/text_source_parts.h"
#include "loom/util/sha256.h"
#include "text/pdf_structure/adapter.h"
#include "chat/docx_basic/adapter.h"
#include "chat/docx_basic/package.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace pdf = loom::text::sources::pdf;
namespace {
const auto root = std::filesystem::path(LOOM_TEST_FIXTURES).parent_path().parent_path();
const auto fixtures = root / "tests/pdf_structure";
std::string fixture(const char* name) { return unwrap(fsutil::read_file(fixtures / name)); }
Json extraction() { return unwrap(json::parse(fixture("projection-fixture.json"))); }
struct Fixture {
  fsutil::TempDir tmp;
  std::shared_ptr<net::ScriptedTransport> http = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  Json reader;
  Fixture() {
    const auto package = unwrap(chat::packaged_reader_directory("pdf_structure"));
    reader = unwrap(json::parse(unwrap(fsutil::read_file(package / "profile.json"))));
    const auto manifest = unwrap(json::parse(unwrap(fsutil::read_file(package / "runtime.json"))));
    reader["structure"]["python"] = manifest.at("python");
    reader["structure"]["script"] = (package / "worker.py").string();
    open();
  }
  void open() {
    RuntimeOptions options;
    options.data_dir = tmp.path().string();
    options.start_workers = false;
    options.http = http;
    rt = unwrap(Runtime::open(options));
  }
  Json request(const std::string& bytes) {
    const auto blob = unwrap(rt->blobs().put(bytes, "application/pdf"));
    return Json{{"hash", blob.hash}, {"source_key", "synthetic:pdf-parts"},
                {"name", "synthetic.pdf"}, {"format", "pdf"}};
  }
  Json counts() {
    Json result = Json::array();
    for (const auto* table : {"nodes", "links", "loom_sources", "loom_provenance", "loom_blobs"})
      result.push_back(*unwrap(rt->db().conn().query_int(std::string("SELECT count(*) FROM ") + table)));
    return result;
  }
};
}

TEST_SUITE("pdf_structure") {
TEST_CASE("saved projection validator binds original pages blocks and inferred chapter labels") {
  const auto e = extraction();
  const auto original = fixture("original.synthetic");
  const auto hash = Sha256::hex(original);
  LOOM_REQUIRE_OK(pdf::validate(e, hash, static_cast<std::int64_t>(original.size())));
  CHECK_FALSE(pdf::validate(e, Sha256::hex("other"), original.size()));
  CHECK_FALSE(pdf::validate(e, hash, original.size() + 1));
  for (const auto& pointer : {"/projection/sha256", "/blocks/0/text", "/blocks/0/locator/page",
                             "/pages/0/byte_range/0", "/chapters/0/evidence_class"}) {
    auto changed = e;
    changed[Json::json_pointer(pointer)] = "tampered";
    CHECK_FALSE(pdf::validate(changed, hash, original.size()));
  }
  for (const auto& invalid : {Json(-1), Json(true), Json(0.5), Json("0")}) {
    auto changed = e;
    changed["projection"]["ranges"][0]["byte_range"][0] = invalid;
    CHECK_FALSE(pdf::validate(changed, hash, original.size()));
  }
}

TEST_CASE("shared page and chapter Units have honest original and derived coordinate frames") {
  Fixture f;
  const auto e = extraction();
  const auto original = fixture("original.synthetic");
  const auto hash = unwrap(f.rt->blobs().put(original)).hash;
  const auto script = unwrap(fsutil::read_file(root / "src/text/pdf_structure/projection.py"));
  const Json transform{{"reader_script_blob", Sha256::hex(script)}};
  pdf::Prepared d{e, f.reader,
      Json{{"original_source_id", "src_pdf_" + hash}, {"original_sha256", hash}, {"original_size", original.size()},
           {"extraction_blob", Sha256::hex(json::canonical(e))}, {"transform_sha256", Sha256::hex(json::canonical(transform))}}, script};
  const auto original_id = unwrap(pdf::record_source(f.rt->db(), "fixture", d));
  CHECK(original_id == "src_pdf_" + hash);
  unwrap(f.rt->blobs().put(json::canonical(e), "application/json"));
  unwrap(f.rt->blobs().put(script, "text/x-python"));
  SourceRecord derived;
  derived.id = "src_pdf_fixture_derived";
  derived.kind = "file";
  derived.blob_hash = unwrap(f.rt->blobs().put(e.at("text").get<std::string>())).hash;
  derived.size = e.at("text").get<std::string>().size();
  derived.format = "pdf";
  unwrap(f.rt->provenance().add_source(derived));
  NodeOptions options;
  options.node_id = "n_pdf_fixture_source";
  unwrap(f.rt->db().create_node("fixture", "text_source", options));
  const Json result{{"source_id", "src_pdf_fixture_derived"}, {"method_sha256", Sha256::hex("fixture-method")},
                    {"source_node", *options.node_id}, {"occurrence_nodes", Json::array()}};
  const Json recipe{{"formats", {{"pdf", {{"structure_units", {{"page", "page"}, {"chapter", "chapter_candidate"}}}}}}},
                    {"edges", {{"part", "part_of"}}}};
  const auto nodes = unwrap(pdf::materialize_structure(*f.rt, result, d, recipe, false));
  REQUIRE(nodes.at("pages").size() == 3);
  REQUIRE(nodes.at("chapters").size() == 2);
  for (const auto& id : nodes.at("pages")) {
    const auto node = unwrap(f.rt->db().get_node(id.get<std::string>()));
    REQUIRE(node);
    const auto unit = unwrap(model::Unit::from_json(node->metadata.at("unit")));
    CHECK(unit.id == model::Unit::make_id(unit.source, unit.locator));
    CHECK(unit.source == original_id);
    CHECK_FALSE(unit.locator.byte_start);
    CHECK_FALSE(unit.locator.byte_len);
    CHECK(unit.attrs.at("coordinate") == "pdf_page_ordinal");
    REQUIRE(unwrap(f.rt->provenance().for_subject(node->id)).size() == 1);
  }
  for (const auto& id : nodes.at("chapters")) {
    const auto node = unwrap(f.rt->db().get_node(id.get<std::string>()));
    REQUIRE(node);
    const auto unit = unwrap(model::Unit::from_json(node->metadata.at("unit")));
    CHECK(unit.source == result.at("source_id").get<std::string>());
    CHECK(unit.attrs.at("evidence_class") == "inferred");
    REQUIRE(unit.locator.byte_start);
    REQUIRE(unit.locator.byte_len);
    const auto quoted = e.at("text").get<std::string>().substr(*unit.locator.byte_start, *unit.locator.byte_len);
    CHECK_FALSE(quoted.empty());
  }
  const auto before = f.counts();
  CHECK(unwrap(pdf::materialize_structure(*f.rt, result, d, recipe, false)) == nodes);
  CHECK(f.counts() == before);
}

TEST_CASE("existing Poppler path stores PDF original derived blob receipt genesis and per-page observations") {
  Fixture f;
  const auto original = unwrap(fsutil::read_file(std::filesystem::path(LOOM_TEST_FIXTURES) / "documents/mixed-text-blank.pdf"));
  const auto request = f.request(original);
  const auto result = unwrap(text::sources::materialize(*f.rt, request));
  CHECK(result.at("coordinate") == "derived_utf8_bytes");
  CHECK(result.at("derivation").at("schema") == "loom.pdf_source_derivation/1");
  REQUIRE(result.at("structure_nodes").at("pages").size() >= 2);
  // Verify the persisted/native derivation boundary separately from the text
  // replay, so a refusal identifies which contract failed.
  const auto source = unwrap(f.rt->provenance().get_source(result.at("source_id").get<std::string>()));
  const auto first = unwrap(f.rt->db().get_node(result.at("occurrence_nodes")[0].get<std::string>()));
  REQUIRE(source); REQUIRE(first);
  LOOM_REQUIRE_OK(pdf::validate_derivation(f.rt->db(), *source, first->metadata));
  const auto fresh = unwrap(pdf::prepare(*f.rt, request.at("hash").get<std::string>()));
  CHECK(json::canonical(fresh.derivation) == json::canonical(result.at("derivation")));
  const auto saved = unwrap(json::parse(unwrap(f.rt->blobs().read(result.at("derivation").at("extraction_blob").get<std::string>()))));
  CHECK(json::canonical(fresh.extraction) == json::canonical(saved));
  LOOM_REQUIRE_OK(pdf::replay(*f.rt, saved, result.at("derivation")));
  std::string restored;
  for (const auto& id : result.at("occurrence_nodes")) {
    const auto part = unwrap(text::sources::read(*f.rt, Json{{"node_id", id}}));
    restored += part.at("text").get<std::string>();
    CHECK(part.at("provenance").at("structure_evidence") == "inferred");
  }
  CHECK(Sha256::hex(restored) == result.at("source_sha256").get<std::string>());
  CHECK(unwrap(f.rt->blobs().read(request.at("hash").get<std::string>())) == original);
  for (const auto& item : result.at("observations")) {
    const auto observation = unwrap(model::Observation::from_json(item));
    CHECK(observation.id == model::Observation::make_id(observation.unit, observation.locator, observation.text));
    CHECK(observation.attrs.at("evidence_class") == "derived");
    CHECK(observation.attrs.at("structure_evidence") == "inferred");
    CHECK(observation.attrs.at("original_locator").contains("page"));
    CHECK(restored.substr(*observation.locator.byte_start, *observation.locator.byte_len) == observation.text);
  }
  const auto before = f.counts();
  CHECK(unwrap(text::sources::materialize(*f.rt, request)) == result);
  CHECK(f.counts() == before);
  f.rt.reset();
  f.open();
  CHECK(unwrap(text::sources::materialize(*f.rt, request)) == result);
  REQUIRE_FALSE(result.at("occurrence_nodes").empty());
  CHECK(text::sources::read(*f.rt, Json{{"node_id", result.at("occurrence_nodes")[0]}}));
  CHECK(f.http->requests().empty());
}

TEST_CASE("invalid PDF and helper output limit leave graph unchanged") {
  Fixture f;
  auto request = f.request("synthetic invalid PDF");
  auto before = f.counts();
  CHECK_FALSE(text::sources::materialize(*f.rt, request));
  CHECK(f.counts() == before);
  request = f.request(unwrap(fsutil::read_file(std::filesystem::path(LOOM_TEST_FIXTURES) / "documents/mixed-text-blank.pdf")));
  // Transport changed in this batch: verify a real output bound through the
  // shared anonymous-input path, without making request config trusted.
  auto command = f.reader.at("structure");
  command["max_output_bytes"] = 1;
  before = f.counts();
  CHECK_FALSE(chat::read_local_json_bytes(fixture("reader-output.json"), command, command.at("limits")));
  CHECK(f.counts() == before);
  auto forged = request; forged["receipt"] = extraction();
  CHECK_FALSE(text::sources::materialize(*f.rt, forged));
  CHECK(f.counts() == before);
  CHECK(f.http->requests().empty());
}

TEST_CASE("changed saved extraction blob is refused on read") {
  Fixture f;
  const auto request = f.request(unwrap(fsutil::read_file(std::filesystem::path(LOOM_TEST_FIXTURES) / "documents/mixed-text-blank.pdf")));
  const auto result = unwrap(text::sources::materialize(*f.rt, request));
  const auto hash = result.at("derivation").at("extraction_blob").get<std::string>();
  const auto path = f.rt->blobs().local_path(hash);
  std::filesystem::permissions(path, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
  LOOM_REQUIRE_OK(fsutil::write_file(path, "tampered synthetic receipt"));
  CHECK_FALSE(text::sources::read(*f.rt, Json{{"node_id", result.at("occurrence_nodes")[0]}}));
  CHECK(f.http->requests().empty());
}
}
