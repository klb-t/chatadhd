// Synthetic only; ScriptedTransport cannot contact a real provider.
#include <doctest/doctest.h>
#include <set>

#include "loom/action_runner.h"
#include "loom/chat_engine.h"
#include "loom/client_blobs.h"
#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/text_source_parts.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
struct Fixture {
  fsutil::TempDir tmp;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  Fixture() { open(); }
  void open() {
    RuntimeOptions options;
    options.data_dir = tmp.path().string();
    options.start_workers = false;
    options.http = transport;
    rt = unwrap(Runtime::open(options));
  }
  Json upload(const std::string& bytes) {
    const auto begun = unwrap(actions::client_blobs::begin(*rt, Json::object()));
    unwrap(actions::client_blobs::append(*rt, Json{{"upload_id", begun.at("upload_id")}, {"data_base64", base64::encode(bytes)}}));
    return unwrap(actions::client_blobs::commit(*rt, Json{{"upload_id", begun.at("upload_id")}, {"mime", "text/plain"}}));
  }
  Json request(const std::string& bytes, std::string key = "synthetic:notes", std::string format = "text") {
    return Json{{"hash", upload(bytes).at("hash")}, {"source_key", key}, {"name", "synthetic.txt"}, {"format", format}, {"language", "pl"}};
  }
  Json materialize(const Json& req) { return unwrap(rt->actions().invoke("client.blob_text", req)); }
  Json read(const Json& id) { return unwrap(rt->actions().invoke("client.blob_text_read", Json{{"node_id", id}})); }
  Json counts() {
    Json result = Json::array();
    for (const auto* table : {"nodes", "links", "loom_sources", "loom_provenance", "loom_blobs"})
      result.push_back(*unwrap(rt->db().conn().query_int(std::string("SELECT count(*) FROM ") + table)));
    return result;
  }
};
}

TEST_SUITE("text_source_parts") {
  TEST_CASE("UTF-8 tiles the original; repeated text has distinct occurrences and source identity") {
    Fixture f;
    const std::string bytes = "Zażółć gęślą jaźń.\nZażółć gęślą jaźń.\n";
    const auto req = f.request(bytes);
    const auto first = f.materialize(req);
    const auto& ids = first.at("occurrence_nodes");
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] != ids[1]);
    const auto a = f.read(ids[0]), b = f.read(ids[1]);
    CHECK(a.at("text") == b.at("text"));
    CHECK(a.at("provenance").at("fingerprint") == b.at("provenance").at("fingerprint"));
    CHECK(a.at("provenance").at("byte_range") != b.at("provenance").at("byte_range"));
    CHECK(a.at("source").at("id") == first.at("source_id"));
    CHECK(a.at("text").get<std::string>() + b.at("text").get<std::string>() == bytes);
    CHECK(unwrap(f.rt->blobs().read(req.at("hash").get<std::string>())) == bytes);
    const auto before = f.counts();
    CHECK(f.materialize(req) == first);
    CHECK(f.counts() == before);
    const auto saved_rules = unwrap(f.rt->blobs().read(first.at("method").at("text_pack_blob").get<std::string>()));
    CHECK(Sha256::hex(saved_rules) == first.at("method").at("text_pack_sha256").get<std::string>());
    CHECK(Json::parse(saved_rules).at("source_parts").at("formats").contains("markdown"));
    auto another = req;
    another["source_key"] = "synthetic:another-document";
    const auto other = f.materialize(another);
    CHECK(other.at("source_sha256") == first.at("source_sha256"));
    CHECK(other.at("source_id") != first.at("source_id"));
    CHECK(other.at("occurrence_nodes")[0] != ids[0]);
    CHECK(f.read(other.at("occurrence_nodes")[0]).at("source").at("id") == other.at("source_id"));
  }

  TEST_CASE("explicit revision retains both originals and survives reopen without multiplying rows") {
    Fixture f;
    const auto a_req = f.request("Stara żółta wersja.\n");
    const auto a = f.materialize(a_req);
    auto b_req = f.request("Nowa zielona wersja.\n");
    b_req["previous_source_id"] = a.at("source_id");
    const auto b = f.materialize(b_req);
    CHECK(a.at("source_id") != b.at("source_id"));
    CHECK(a.at("source_sha256") != b.at("source_sha256"));
    bool linked = false;
    for (const auto& link : unwrap(f.rt->db().get_links(b.at("source_node").get<std::string>())))
      linked |= link.dst == a.at("source_node").get<std::string>();
    CHECK(linked);
    const auto before = f.counts();
    f.rt.reset();
    f.open();
    CHECK(f.materialize(a_req) == a);
    CHECK(f.materialize(b_req) == b);
    CHECK(f.counts() == before);
    CHECK(f.read(a.at("occurrence_nodes")[0]).at("text") == "Stara żółta wersja.\n");
    CHECK(f.read(b.at("occurrence_nodes")[0]).at("text") == "Nowa zielona wersja.\n");
  }

  TEST_CASE("Markdown retains adapter observations and table bytes without claiming paragraphs") {
    Fixture f;
    const std::string bytes = "# Nagłówek\n- Żółw\n```python\nprint('Łódź')\n```\n| a | b |\n|---|---|\n| 1 | 2 |\n";
    const auto result = f.materialize(f.request(bytes, "synthetic:markdown", "markdown"));
    std::set<std::string> kinds;
    for (const auto& obs : result.at("observations")) {
      kinds.insert(obs.at("kind").get<std::string>());
      const auto& loc = obs.at("locator");
      CHECK(bytes.substr(loc.at("byte_start").get<std::size_t>(), loc.at("byte_len").get<std::size_t>()) == obs.at("text").get<std::string>());
    }
    CHECK(kinds.count("heading") == 1);
    CHECK(kinds.count("list_item") == 1);
    CHECK(kinds.count("code_block") == 1);
    CHECK(kinds.count("table_row") == 0);  // no table parser in this adapter
    CHECK(result.at("status") == "partial");
    std::string restored;
    for (const auto& id : result.at("occurrence_nodes")) restored += f.read(id).at("text").get<std::string>();
    CHECK(restored == bytes);
  }

  TEST_CASE("code remains exact code spans and makes no AST claim") {
    Fixture f;
    const std::string bytes = "# Łódź\ndef żółw():\n    return 'gęś'\n";
    auto req = f.request(bytes, "synthetic:code", "code");
    req["name"] = "synthetic.py";
    const auto result = f.materialize(req);
    CHECK(result.at("reason") == "code_text_only_no_ast");
    CHECK(result.at("observations").empty());
    std::string restored;
    for (const auto& id : result.at("occurrence_nodes")) {
      const auto part = f.read(id);
      CHECK(part.at("provenance").at("unit").at("kind") == "code_span");
      restored += part.at("text").get<std::string>();
    }
    CHECK(restored == bytes);
  }

  TEST_CASE("missing parser and invalid UTF-8 do not invent structure or lose the uploaded bytes") {
    Fixture f;
    auto req = f.request(std::string("\xff", 1));
    const auto before = f.counts();
    CHECK_FALSE(text::sources::materialize(*f.rt, req));
    CHECK(f.counts() == before);
    CHECK(unwrap(f.rt->blobs().read(req.at("hash").get<std::string>())) == std::string("\xff", 1));
    req = f.request("synthetic opaque");
    req["format"] = "unknown";
    const auto unsupported = text::sources::materialize(*f.rt, req);
    REQUIRE_FALSE(unsupported);
    CHECK(unsupported.error().code == Errc::Unsupported);
  }

  TEST_CASE("selected graph occurrence reaches the actual local fake request without an attachment") {
    Fixture f;
    const auto result = f.materialize(f.request("NIEWYBRANY PIERWSZY WIERSZ\nWybrany żółw 42.\n"));
    const auto selected = result.at("occurrence_nodes")[1];
    f.rt->config().set("llm_provider", "local");
    f.rt->config().set("local_model_base_url", "http://127.0.0.1:11434/v1");
    f.rt->config().set("default_model", "synthetic/model");
    f.rt->config().set("model_registry_autorefresh", "off");
    f.rt->config().set("semantic_analysis", false);
    f.rt->config().set("context_execution", Json{{"unified", {{"enabled", true}, {"include_knowledge", false},
        {"legacy_seed_ids", Json::array({selected})}, {"legacy_relation_hops", 0}}}});
    f.transport->expect("POST", "http://127.0.0.1:11434/v1/chat/completions",
        net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({Json{{"message", {{"role", "assistant"}, {"content", "Atrapa."}}}}})}}));
    ChatOptions options;
    options.stream = false;
    options.include_memory = false;
    unwrap(f.rt->chat().send("Przytocz wybrany fragment.", options));
    const auto calls = f.transport->requests();
    REQUIRE(calls.size() == 1);
    const auto wire = Json::parse(calls.front().body);
    const auto body = wire.at("messages").dump();
    CHECK(body.find("Wybrany żółw 42.") != std::string::npos);
    CHECK(body.find("NIEWYBRANY PIERWSZY WIERSZ") == std::string::npos);
  }

  TEST_CASE("changing the segmentation method keeps the original source and isolates projections") {
    Fixture f;
    auto req = f.request("Żółty żółw.\nDrugi wiersz.\n");
    const auto line = f.materialize(req);
    req["level"] = "word";
    req["language"] = "en";
    const auto word = f.materialize(req);
    CHECK(word.at("source_id") == line.at("source_id"));
    CHECK(word.at("method_sha256") != line.at("method_sha256"));
    CHECK(word.at("projection_node") != line.at("projection_node"));
    const auto before = f.counts();
    CHECK(f.materialize(req) == word);
    CHECK(f.counts() == before);
    CHECK(f.read(line.at("occurrence_nodes")[0]).at("text") == "Żółty żółw.\n");
  }

  TEST_CASE("corrupt occurrence content is refused instead of silently replacing source bytes") {
    Fixture f;
    const auto result = f.materialize(f.request("Właściwa treść.\n"));
    const auto id = result.at("occurrence_nodes")[0].get<std::string>();
    LOOM_REQUIRE_OK(f.rt->db().conn().run("UPDATE nodes SET content=? WHERE id=?", "Obca treść", id));
    const auto read = text::sources::read(*f.rt, Json{{"node_id", id}});
    REQUIRE_FALSE(read);
    CHECK(read.error().code == Errc::Conflict);
    CHECK(unwrap(f.rt->blobs().read(result.at("source_sha256").get<std::string>())) == "Właściwa treść.\n");
  }
  TEST_CASE("fallback language stores effective rules without changing legacy identities") {
    Fixture f;
    auto req = f.request("Żółw.\n");
    req["language"] = "not-a-language";
    const auto fallback = f.materialize(req);
    CHECK(fallback.at("method").at("language") == "*");
    CHECK(f.read(fallback.at("occurrence_nodes")[0]).at("provenance").at("unit").at("lang") == "*");
    const auto before = f.counts();
    req["language"] = "*";
    CHECK(f.materialize(req) == fallback);
    CHECK(f.counts() == before);
    req["language"] = "pl-PL";
    const auto region = f.materialize(req);
    CHECK(region.at("method").at("language") == "pl");
    CHECK(f.read(region.at("occurrence_nodes")[0]).at("provenance").at("unit").at("lang") == "pl");
    req["language"] = "pl";
    CHECK(f.materialize(req) == region);
  }

  TEST_CASE("cached receipt is not a subgraph repair or integrity verdict") {
    Fixture f;
    const auto req = f.request("Zachowany oryginał.\n");
    const auto result = f.materialize(req);
    const auto id = result.at("occurrence_nodes")[0].get<std::string>();
    LOOM_REQUIRE_OK(f.rt->db().conn().run("UPDATE nodes SET content=? WHERE id=?", "Uszkodzony cache", id));
    CHECK(f.materialize(req) == result);  // receipt only, never a repair claim
    CHECK_FALSE(text::sources::read(*f.rt, Json{{"node_id", id}}));
    const auto node = unwrap(f.rt->db().get_node(id));
    REQUIRE(node);
    CHECK(node->content == "Uszkodzony cache");
    CHECK_FALSE(text::sources::validate_occurrence(f.rt->db(), *node));
    CHECK(unwrap(f.rt->blobs().read(req.at("hash").get<std::string>())) == "Zachowany oryginał.\n");
  }

  TEST_CASE("occurrence byte coordinates reject fractional negative and foreign coordinates") {
    Fixture f;
    const auto result = f.materialize(f.request("Zażółć.\n"));
    const auto id = result.at("occurrence_nodes")[0].get<std::string>();
    const auto node = unwrap(f.rt->db().get_node(id));
    REQUIRE(node);
    for (const Json& bad : {Json(-1), Json(0.5), Json("0")}) {
      auto altered = *node;
      altered.metadata["unit"]["locator"]["byte_start"] = bad;
      CHECK_FALSE(text::sources::validate_occurrence(f.rt->db(), altered));
    }
    auto altered = *node;
    altered.metadata["coordinate"] = "pdf_page_characters";
    CHECK_FALSE(text::sources::validate_occurrence(f.rt->db(), altered));
  }

}
