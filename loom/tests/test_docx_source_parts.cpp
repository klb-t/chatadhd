// New DOCX graph bridge only. Native acceptance is reserved for the integrator.
#include <doctest/doctest.h>
#include <set>
#include <cstdlib>

#include "loom/action_runner.h"
#include "loom/chat_engine.h"
#include "loom/client_blobs.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/text_source_parts.h"
#include "loom/util/base64.h"
#include "loom/util/sha256.h"
#include "text/docx_projection.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace {
const auto fixtures = std::filesystem::path(LOOM_TEST_FIXTURES) / "docx_parts";
const auto source_root = std::filesystem::path(LOOM_TEST_FIXTURES).parent_path().parent_path();
std::string fixture(const std::string& name) { return unwrap(fsutil::read_file(fixtures / name)); }
Json extraction() { return unwrap(json::parse(fixture("base.json"))); }

struct Fixture {
  fsutil::TempDir tmp;
  std::shared_ptr<net::ScriptedTransport> http = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  Json reader;
  Fixture() {
    auto profile = unwrap(RuntimeProfile::builtin("chat_context"));
    for (const auto& item : profile.values().at("attachments").at("document_readers"))
      if (json::get_string(item, "adapter") == "docx_basic") reader = item;
    REQUIRE(reader.is_object());
    const char* python = std::getenv("LOOM_DOCX_TEST_PYTHON");
    REQUIRE_MESSAGE(python, "Set LOOM_DOCX_TEST_PYTHON to an absolute local Python interpreter; no install/fallback");
    reader["python"] = python;
    const char* staged = std::getenv("LOOM_DOCX_TEST_READER");
    reader["script"] = staged ? std::string(staged) : (source_root / "tools/docx_basic/reader.py").string();
    save_reader();
    open();
  }
  void save_reader() {
    std::filesystem::create_directories(tmp.path() / "profiles");
    LOOM_REQUIRE_OK(fsutil::write_file(tmp.path() / "profiles/chat_context.pack",
        Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "chat_context"},
             {"overrides", {{"attachments", {{"document_readers", Json::array({reader})}}}}}}.dump()));
  }
  void open() {
    RuntimeOptions opts;
    opts.data_dir = tmp.path().string();
    opts.start_workers = false;
    opts.http = http;
    rt = unwrap(Runtime::open(opts));
  }
  Json upload(std::string bytes, std::string key = "synthetic:docx-graph") {
    auto started = unwrap(actions::client_blobs::begin(*rt, Json::object()));
    unwrap(actions::client_blobs::append(*rt, {{"upload_id", started.at("upload_id")}, {"data_base64", base64::encode(bytes)}}));
    auto blob = unwrap(actions::client_blobs::commit(*rt, {{"upload_id", started.at("upload_id")},
        {"source_key", key}, {"name", "synthetic.docx"}, {"format", "docx"}, {"mime", reader.at("mime")}}));
    return {{"hash", blob.at("hash")}, {"source_key", key}, {"name", "synthetic.docx"}, {"format", "docx"}, {"language", "pl"}};
  }
  Json materialize(const Json& req) { return unwrap(rt->actions().invoke("client.blob_text", req)); }
  Json read(const Json& id) { return unwrap(rt->actions().invoke("client.blob_text_read", {{"node_id", id}})); }
  Json counts() {
    Json out = Json::array();
    for (const auto* table : {"nodes", "links", "loom_sources", "loom_provenance", "loom_blobs"})
      out.push_back(*unwrap(rt->db().conn().query_int(std::string("SELECT count(*) FROM ") + table)));
    return out;
  }
};
}

TEST_SUITE("docx_source_parts") {
TEST_CASE("projection validator refuses stale source version and malformed derived coordinates") {
  const auto e = extraction();
  const auto raw = fixture("base.docx");
  const auto hash = Sha256::hex(raw);
  LOOM_REQUIRE_OK(text::sources::docx::validate(e, hash, raw.size()));
  CHECK_FALSE(text::sources::docx::validate(e, Sha256::hex(fixture("zip-change.docx")), raw.size()));
  CHECK_FALSE(text::sources::docx::validate(e, hash, raw.size() + 1));
  for (const Json& invalid : {Json(-1), Json(0.5), Json("0"), Json(true)}) {
    auto altered = e;
    altered["projection"]["ranges"][0]["byte_range"][0] = invalid;
    CHECK_FALSE(text::sources::docx::validate(altered, hash, raw.size()));
  }
  for (const auto& ptr : {"/projection/schema", "/projection/coordinate", "/projection/sha256", "/blocks/0/locator", "/blocks/0/text"}) {
    auto altered = e;
    altered[Json::json_pointer(ptr)] = "wrong";
    CHECK_FALSE(text::sources::docx::validate(altered, hash, raw.size()));
  }
  auto altered = e;
  altered["blocks"][1]["locator"] = altered["blocks"][0]["locator"];
  CHECK_FALSE(text::sources::docx::validate(altered, hash, raw.size()));
  altered = e;
  altered.erase("projection");  // old helper, no fabricated paragraph coordinates
  CHECK_FALSE(text::sources::docx::validate(altered, hash, raw.size()));
}

TEST_CASE("explicit upload bridge preserves raw blob derived observations duplicates and provenance") {
  Fixture f;
  const auto req = f.upload(fixture("base.docx"));
  // Upload registration alone must not generate graph parts.
  CHECK(*unwrap(f.rt->db().conn().query_int("SELECT count(*) FROM nodes WHERE kind='paragraph'")) == 0);
  const auto result = f.materialize(req);
  const auto ids = result.at("occurrence_nodes");
  REQUIRE(ids.size() == 11);
  CHECK(result.at("coordinate") == "derived_utf8_bytes");
  CHECK(result.at("source_sha256") != req.at("hash"));
  CHECK(result.at("derivation").at("original_sha256") == req.at("hash"));
  CHECK(result.at("warnings") == extraction().at("warnings"));
  CHECK(result.at("observations").size() == 11);
  std::string restored;
  std::set<std::string> distinct;
  for (const auto& id : ids) {
    const auto part = f.read(id);
    restored += part.at("text").get<std::string>();
    distinct.insert(id.get<std::string>());
    CHECK(part.at("source").at("id") == result.at("source_id"));
    CHECK(part.at("provenance").at("coordinate") == "derived_utf8_bytes");
    REQUIRE(unwrap(f.rt->provenance().for_subject(id.get<std::string>())).size() == 1);
  }
  CHECK(distinct.size() == 11);
  CHECK(restored == extraction().at("text").get<std::string>());
  const auto first = f.read(ids[0]), second = f.read(ids[1]);
  CHECK(first.at("text") == second.at("text"));
  CHECK(first.at("provenance").at("fingerprint") == second.at("provenance").at("fingerprint"));
  CHECK(first.at("provenance").at("byte_range") != second.at("provenance").at("byte_range"));
  CHECK(f.read(ids[2]).at("text") == "Wewnątrz\nakapitu.\n");
  CHECK(f.read(ids[10]).at("text") == "");
  CHECK(unwrap(f.rt->blobs().read(req.at("hash").get<std::string>())) == fixture("base.docx"));
  for (const auto& observation : result.at("observations")) {
    const auto& loc = observation.at("locator");
    CHECK(loc.at("source") == result.at("source_id"));
    CHECK(restored.substr(loc.at("byte_start").get<std::size_t>(), loc.at("byte_len").get<std::size_t>()) == observation.at("text").get<std::string>());
    CHECK(observation.at("attrs").at("evidence_class") == "derived");
  }
  bool original_link = false;
  for (const auto& link : unwrap(f.rt->db().get_links(result.at("source_node").get<std::string>())))
    original_link |= link.dst == result.at("original_source_node").get<std::string>();
  CHECK(original_link);
  const auto before = f.counts();
  CHECK(f.materialize(req) == result);
  CHECK(f.counts() == before);
  CHECK(f.http->requests().empty());
}

TEST_CASE("ZIP revisions with identical text and changed text retain separate sources through reopen") {
  Fixture f;
  auto ar = f.upload(fixture("base.docx"));
  const auto a = f.materialize(ar);
  auto br = f.upload(fixture("zip-change.docx"));
  br["previous_source_id"] = a.at("source_id");
  const auto b = f.materialize(br);
  CHECK(a.at("source_sha256") == b.at("source_sha256"));
  CHECK(a.at("source_id") != b.at("source_id"));
  CHECK(a.at("occurrence_nodes")[0] != b.at("occurrence_nodes")[0]);
  auto cr = f.upload(fixture("text-change.docx"));
  cr["previous_source_id"] = b.at("source_id");
  const auto c = f.materialize(cr);
  CHECK(c.at("source_sha256") != a.at("source_sha256"));
  const auto before = f.counts();
  f.rt.reset();
  f.open();
  CHECK(f.materialize(ar) == a);
  CHECK(f.materialize(br) == b);
  CHECK(f.materialize(cr) == c);
  CHECK(f.counts() == before);
  CHECK(f.read(b.at("occurrence_nodes")[9]).at("text") == "Koniec.\n");
  CHECK(f.read(c.at("occurrence_nodes")[9]).at("text") == "Zmiana.\n");
  CHECK(f.http->requests().empty());
}

TEST_CASE("method changes create another derivation without overwriting its old receipt") {
  Fixture f;
  auto req = f.upload(fixture("base.docx"));
  const auto a = f.materialize(req);
  // Same bytes and algorithm, different exact resource configuration.
  f.reader["limits"]["max_text_chars"] = 0;
  f.save_reader();
  const auto b = f.materialize(req);
  CHECK(a.at("source_sha256") == b.at("source_sha256"));
  CHECK(a.at("source_id") != b.at("source_id"));
  CHECK(a.at("method_sha256") != b.at("method_sha256"));
  CHECK(f.read(a.at("occurrence_nodes")[0]).at("text") == f.read(b.at("occurrence_nodes")[0]).at("text"));
  CHECK(f.materialize(req) == b);
  req["pack"] = Json{{"genesis", {{"record_initial_additions", true}}}};
  const auto with_events = f.materialize(req);
  const auto event_count = *unwrap(f.rt->db().conn().query_int("SELECT count(*) FROM nodes WHERE kind='text_event'"));
  CHECK(event_count == 11);
  const auto before_repeat = f.counts();
  CHECK(f.materialize(req) == with_events);
  CHECK(f.counts() == before_repeat);
  req.erase("pack");
  req["level"] = "word";
  const auto finer = f.materialize(req);
  CHECK(finer.at("source_id") == b.at("source_id"));
  CHECK(finer.at("method_sha256") != b.at("method_sha256"));
  CHECK(finer.at("occurrence_nodes").size() > b.at("occurrence_nodes").size());
}

TEST_CASE("wrong predecessor and failed reader leave graph unchanged and originals readable") {
  Fixture f;
  const auto other = f.materialize(f.upload(fixture("base.docx"), "synthetic:other"));
  auto req = f.upload(fixture("text-change.docx"));
  req["previous_source_id"] = other.at("source_id");
  auto before = f.counts();
  CHECK_FALSE(text::sources::materialize(*f.rt, req));
  CHECK(f.counts() == before);
  req = f.upload("PKbroken");
  before = f.counts();
  CHECK_FALSE(text::sources::materialize(*f.rt, req));
  CHECK(f.counts() == before);
  CHECK(unwrap(f.rt->blobs().read(req.at("hash").get<std::string>())) == "PKbroken");
  req = f.upload(fixture("base.docx"));
  f.reader["limits"]["max_text_chars"] = 1;
  f.save_reader();
  before = f.counts();
  CHECK_FALSE(text::sources::materialize(*f.rt, req));
  CHECK(f.counts() == before);
}

TEST_CASE("altered original descriptor is refused by cached materialization read and selector validation") {
  Fixture f;
  auto req = f.upload(fixture("base.docx"));
  const auto a = f.materialize(req);
  const auto id = a.at("occurrence_nodes")[0].get<std::string>();
  const auto node = unwrap(f.rt->db().get_node(id));
  REQUIRE(node);
  LOOM_REQUIRE_OK(f.rt->db().conn().run("UPDATE loom_sources SET blob_hash=? WHERE id=?",
      Sha256::hex("foreign"), a.at("derivation").at("original_source_id").get<std::string>()));
  CHECK_FALSE(text::sources::materialize(*f.rt, req));
  CHECK_FALSE(text::sources::read(*f.rt, {{"node_id", id}}));
  CHECK_FALSE(text::sources::validate_occurrence(f.rt->db(), *node));
}

TEST_CASE("missing original blob is refused even when the projection receipt still exists") {
  Fixture f;
  auto req = f.upload(fixture("base.docx"));
  const auto a = f.materialize(req);
  REQUIRE(std::filesystem::remove(f.rt->blobs().path_for(req.at("hash").get<std::string>())));
  CHECK_FALSE(text::sources::materialize(*f.rt, req));
  CHECK_FALSE(text::sources::read(*f.rt, {{"node_id", a.at("occurrence_nodes")[0]}}));
}

TEST_CASE("missing derived receipt and foreign occurrence coordinates are refused") {
  Fixture f;
  auto req = f.upload(fixture("base.docx"));
  const auto a = f.materialize(req);
  const auto id = a.at("occurrence_nodes")[0].get<std::string>();
  const auto node = unwrap(f.rt->db().get_node(id));
  REQUIRE(node);
  auto altered = *node;
  altered.metadata["coordinate"] = "source_utf8_bytes";
  CHECK_FALSE(text::sources::validate_occurrence(f.rt->db(), altered));
  altered = *node;
  altered.metadata["unit"]["locator"]["source"] = a.at("derivation").at("original_source_id");
  CHECK_FALSE(text::sources::validate_occurrence(f.rt->db(), altered));
  REQUIRE(std::filesystem::remove(f.rt->blobs().path_for(a.at("derivation").at("extraction_blob").get<std::string>())));
  CHECK_FALSE(text::sources::materialize(*f.rt, req));
  CHECK_FALSE(text::sources::read(*f.rt, {{"node_id", id}}));
}

TEST_CASE("selected recognized paragraph reaches only the synthetic provider request") {
  Fixture f;
  const auto a = f.materialize(f.upload(fixture("base.docx")));
  const auto selected = a.at("occurrence_nodes")[2];
  f.rt->config().set("llm_provider", "local");
  f.rt->config().set("local_model_base_url", "http://127.0.0.1:11434/v1");
  f.rt->config().set("default_model", "synthetic/docx");
  f.rt->config().set("model_registry_autorefresh", "off");
  f.rt->config().set("semantic_analysis", false);
  f.rt->config().set("context_execution", Json{{"unified", {{"enabled", true}, {"include_knowledge", false},
      {"legacy_seed_ids", Json::array({selected})}, {"legacy_relation_hops", 0}}}});
  f.http->expect("POST", "http://127.0.0.1:11434/v1/chat/completions",
      net::ScriptedTransport::Reply::json(200, {{"choices", Json::array({Json{{"message", {{"role", "assistant"}, {"content", "Atrapa."}}}}})}}));
  ChatOptions opts;
  opts.stream = false;
  opts.include_memory = false;
  unwrap(f.rt->chat().send("Przytocz wybrany akapit.", opts));
  REQUIRE(f.http->requests().size() == 1);
  const auto wire = Json::parse(f.http->requests().front().body).at("messages").dump();
  CHECK(wire.find("Wewnątrz\\nakapitu.") != std::string::npos);
  CHECK(wire.find("Żółw powtórzony.") == std::string::npos);
  CHECK(wire.find("Komórka A") == std::string::npos);
}
}
