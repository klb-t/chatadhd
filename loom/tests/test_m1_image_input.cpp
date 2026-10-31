// M1 native acceptance: authored under light admission; execute only after
// integrator applies shared wiring + regenerates runtime profiles + builds.
#include <doctest/doctest.h>
#include "chat/image_input.h"
#include "media/local_ocr.h"
#include "llm/image_contract.h"
#include "loom/importer.h"
#include "loom/chat_engine.h"
#include "loom/runtime.h"
#include "loom/providers.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"
using namespace loom;
using loom::test::unwrap;
namespace {
std::filesystem::path fixture(const char* file) {
  return std::filesystem::path(LOOM_TEST_FIXTURES) / "local_ocr" / file;
}
Json preset() { return unwrap(RuntimeProfile::builtin("image_input")).values(); }
}
TEST_SUITE("m1_image_input") {
  TEST_CASE("located output binds source hash and does not invent confidence") {
    const auto doc = unwrap(json::parse(unwrap(fsutil::read_file(fixture("recognized.json")))));
    const auto hash = Sha256::hex(unwrap(fsutil::read_file(fixture("synthetic-pl.png"))));
    const auto result = unwrap(decode_local_ocr(doc, hash));
    CHECK(result.to_json()["confidence"].is_null());
    CHECK(result.representation["regions"].size() == 13);
    CHECK_FALSE(decode_local_ocr(doc, std::string(64, '0')));
    Json bad = doc; bad["status"] = "no_text";
    CHECK_FALSE(decode_local_ocr(bad, hash));
  }
  TEST_CASE("data controls no prompt text image and explicit goal pipelines") {
    auto data = preset();
    CHECK(unwrap(chat::image_pipeline("", data))["steps"] == Json::array({"local_ocr", "vision"}));
    CHECK(unwrap(chat::image_pipeline("Odczytaj termin", data))["steps"] == Json::array({"local_ocr"}));
    CHECK(unwrap(chat::image_pipeline("Jaki kolor ma obraz?", data))["steps"] == Json::array({"local_ocr", "vision"}));
    CHECK(unwrap(chat::image_pipeline("Dowolne pytanie", data, "text"))["steps"] == Json::array({"local_ocr"}));
    data["routes"]["no_prompt"] = Json::array({"vision"});
    CHECK(unwrap(chat::image_pipeline("", data))["steps"] == Json::array({"vision"}));
    CHECK_FALSE(chat::image_pipeline("", data, "unknown"));
  }
  TEST_CASE("model data AND adapter contract admit the actual image body") {
    fsutil::TempDir td;
    Config cfg(td.path() / "config.json"); Secrets secrets(td.path() / "secrets.json");
    net::ScriptedTransport http;
    cfg.set("base_url", "http://127.0.0.1:9"); secrets.set("api_key", "synthetic");
    llm::Services services(cfg, secrets, http);
    llm::ModelSpec model{"name-does-not-say-vision", "", Json::object()};
    const auto image = Json{{"type", "image_url"}, {"image_url", {{"url", "data:image/png;base64,AA=="}}}};
    llm::ModelRequest request;
    request.models = {model};
    request.body = Json{{"model", model.model}, {"messages", Json::array({Json{{"role", "user"}, {"content", Json::array({image})}}})}};
    auto unknown = llm::prepare_call(services, request);
    REQUIRE_FALSE(unknown); CHECK(unknown.error().message == "vision_model_capability_unknown");
    cfg.set("model_capabilities", Json{{model.model, {{"accepts", Json::array({"text", "image"})}}}});
    const auto prepared = unwrap(llm::prepare_call(services, request));
    CHECK(unwrap(json::parse(prepared.request.body))["messages"][0]["content"][0] == image);
    // Actual byte body wins over a text-only description.
    request.body_bytes = request.body.dump();
    request.body = Json{{"model", model.model}, {"messages", Json::array()}};
    CHECK(unwrap(llm::prepare_call(services, request)).request.body == request.body_bytes);
    request.body = unwrap(json::parse(request.body_bytes));
    request.body_bytes.clear();
    request.body["model"] = "different/model";
    auto different = llm::prepare_call(services, request);
    REQUIRE_FALSE(different); CHECK(different.error().message == "vision_model_payload_mismatch");
    request.body["model"] = model.model;
    request.route.adapter = "openai_images";
    auto mismatch = llm::prepare_call(services, request);
    REQUIRE_FALSE(mismatch); CHECK(mismatch.error().message == "vision_adapter_image_contract_unavailable");
    request.route.adapter.clear();
    cfg.set("model_capabilities", Json{{model.model, {{"accepts", Json::array({"text"})}}}});
    CHECK_FALSE(llm::prepare_call(services, request));
    CHECK(http.requests().empty());
  }
  TEST_CASE("a remote provider name cannot satisfy the local OCR contract") {
    struct RemoteNamedLocal final : OcrProvider {
      bool called = false;
      std::string name() const override { return "local_tesseract"; }
      Result<OcrResult> recognize_bytes(std::string_view, std::string_view, const std::optional<std::string>&) override {
        called = true; return Error(Errc::Unavailable, "synthetic_remote_not_allowed");
      }
      Result<OcrResult> recognize(const std::filesystem::path&, const std::optional<std::string>&) override {
        called = true; return Error(Errc::Unavailable, "synthetic_remote_not_allowed");
      }
    };
    fsutil::TempDir td; Secrets secrets(td.path() / "secrets.json"); net::ScriptedTransport http;
    MediaProviders media(secrets, http);
    auto remote = std::make_shared<RemoteNamedLocal>(); media.add_ocr_provider(remote);
    CHECK_FALSE(media.ocr_local_bytes("synthetic", "png", "local_tesseract"));
    CHECK_FALSE(remote->called); CHECK(http.requests().empty());
    media.add_ocr_provider(std::make_shared<LocalOcrProvider>(preset().at("local")));
    const auto bytes = unwrap(fsutil::read_file(fixture("synthetic-pl.png")));
    const auto recognized = unwrap(media.ocr_local_bytes(bytes, "png", "local_tesseract"));
    CHECK(recognized.representation["status"] == "recognized");
    CHECK_FALSE(remote->called); CHECK(http.requests().empty());
  }
  TEST_CASE("catalogue image input survives refresh and reopen") {
    fsutil::TempDir td;
    Config cfg(td.path() / "config.json"); Secrets secrets(td.path() / "secrets.json");
    net::ScriptedTransport http; cfg.set("base_url", "http://127.0.0.1:9"); secrets.set("api_key", "synthetic");
    const Json entry{{"id", "synthetic/catalog"}, {"architecture", {{"input_modalities", Json::array({"text", "image"})}}}};
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"data", Json::array({entry})}}));
    ModelRegistry registry(td.path() / "models.json", cfg, secrets, http);
    LOOM_REQUIRE_OK(registry.update_from_api()); LOOM_REQUIRE_OK(registry.reload());
    const llm::Services services(cfg, secrets, http);
    const llm::ModelSpec model{"synthetic/catalog", "", Json::object()};
    const auto accepts = unwrap(llm::catalog_accepts_images(services, model));
    REQUIRE(accepts.has_value()); CHECK(*accepts);
  }
  TEST_CASE("local import regions source bytes reopen and repeat have stable identities") {
    fsutil::TempDir td;
    auto http = std::make_shared<net::ScriptedTransport>();
    RuntimeOptions opts; opts.data_dir = td.path().string(); opts.start_workers = false; opts.http = http;
    auto rt = unwrap(Runtime::open(opts));
    auto imported = unwrap(rt->importer().import_file(fixture("synthetic-pl.png")));
    REQUIRE(imported.conversations.size() == 1);
    const auto messages = unwrap(rt->db().get_msgs(imported.conversations[0].id));
    REQUIRE(messages.size() == 1); CHECK(messages[0].role == "user");
    CHECK(messages[0].metadata["speaker"].is_null());
    CHECK(messages[0].text.find("godzina 09:00.") != std::string::npos);
    const auto projection = messages[0].metadata["image_representation"];
    REQUIRE(projection["region_nodes"].size() == 13);
    CHECK(unwrap(rt->blobs().read(imported.blob_hash)) == unwrap(fsutil::read_file(fixture("synthetic-pl.png"))));
    const auto derived = unwrap(json::parse(unwrap(rt->blobs().read(projection["representation_blob"].get<std::string>()))));
    for (const auto& id : projection["region_nodes"]) {
      const auto node = unwrap(rt->db().get_node(id.get<std::string>())); REQUIRE(node);
      const auto& obs = node->metadata["observation"];
      CHECK(obs["speaker"] == "");
      CHECK(obs["locator"]["source"] == projection["representation_source"]);
      const auto pointer = Json::json_pointer(obs["locator"]["json_pointer"].get<std::string>());
      CHECK(derived.at(pointer) == obs["text"]);
      CHECK(node->metadata["unit"]["attrs"]["original_sha256"] == imported.blob_hash);
    }
    const auto nodes = unwrap(rt->db().list_nodes(std::nullopt, 10000)).size();
    const auto sources = unwrap(rt->provenance().list_sources(10000)).size();
    rt.reset(); rt = unwrap(Runtime::open(opts));
    const auto again = unwrap(rt->importer().import_file(fixture("synthetic-pl.png")));
    CHECK(again.already_imported); REQUIRE(again.conversations.size() == 1);
    CHECK(again.conversations[0].id == imported.conversations[0].id);
    CHECK(unwrap(rt->db().list_nodes(std::nullopt, 10000)).size() == nodes);
    CHECK(unwrap(rt->provenance().list_sources(10000)).size() == sources);
    CHECK(http->requests().empty());
  }
}
