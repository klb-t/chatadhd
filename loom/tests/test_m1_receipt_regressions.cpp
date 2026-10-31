// Receipt corrections R1-R3. Native tests AUTHORED, NOT EXECUTED under light admission.
#include <doctest/doctest.h>
#include "chat/image_input.h"
#include "loom/chat_engine.h"
#include "loom/importer.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/use_model.h"
#include "test_helpers.h"
#include "loom/util/sha256.h"
using namespace loom;
using loom::test::unwrap;
namespace {
std::filesystem::path image_file() {
  return std::filesystem::path(LOOM_TEST_FIXTURES) / "local_ocr/synthetic-pl.png";
}
Json synthetic_patterns() {
  // A public synthetic field is designated a secret by this test recipe.
  return Json::array({Json{{"id", "synthetic_ocr_field"}, {"regex", "godzina 09:00"},
      {"flags", ""}, {"label", {{"pl", "pole syntetyczne"}, {"en", "synthetic field"}}}}});
}
void secret_recipe(const std::filesystem::path& root) {
  LOOM_REQUIRE_OK(fsutil::ensure_dir(root / "profiles"));
  LOOM_REQUIRE_OK(fsutil::write_file(root / "profiles/chat_secrets.pack",
      Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "chat_secrets"},
           {"overrides", {{"patterns", synthetic_patterns()}}}}.dump()));
}
Json image_state(Runtime& rt) {
  Json out;
  auto lock = rt.db().lock();
  for (const auto* table : {"loom_sources", "loom_blobs", "nodes", "links"}) {
    auto query = unwrap(rt.db().conn().prepare(std::string("SELECT count(*) FROM ") + table));
    REQUIRE(unwrap(query.step())); out[table] = query.get_int(0);
  }
  Json files = Json::object();
  if (std::filesystem::exists(rt.blobs().root()))
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rt.blobs().root()))
      if (entry.is_regular_file()) files[entry.path().string()] = Sha256::hex(unwrap(fsutil::read_file(entry.path())));
  out["blob_files"] = std::move(files);
  return out;
}
struct Fixture {
  fsutil::TempDir tmp;
  std::shared_ptr<net::ScriptedTransport> http = std::make_shared<net::ScriptedTransport>();
  RuntimeOptions options;
  std::unique_ptr<Runtime> rt;
  Fixture() {
    secret_recipe(tmp.path());
    options.data_dir = tmp.path().string(); options.start_workers = false; options.http = http;
    rt = unwrap(Runtime::open(options));
    rt->config().set("base_url", "http://127.0.0.1:9");
    rt->config().set("model_registry_autorefresh", "off");
    rt->config().set("default_model", "synthetic/text");
    rt->config().set("auto_title", false); rt->config().set("semantic_analysis", false);
    rt->secrets().set("api_key", "synthetic-loopback-only");
  }
  ChatOptions chat() {
    ChatOptions o; o.include_memory = false; o.include_graph_memory = false;
    o.selector_profile = "compat"; o.stream = false; return o;
  }
};
}
TEST_SUITE("m1_receipt_regressions") {
  TEST_CASE("R1 pure preparation previews refusal and holds preserve all image stores and original upload") {
    Fixture f;
    const auto bytes = unwrap(fsutil::read_file(image_file()));
    const auto upload = unwrap(f.rt->blobs().put(bytes, "image/png"));
    const auto before = image_state(*f.rt);
    auto opts = f.chat(); opts.attachments = {"blob:" + upload.hash + "/synthetic-pl.png"};
    const auto image = unwrap(chat::prepare_image_attachment(f.rt->media(), image_file(), image_file(),
        "Odczytaj tekst", unwrap(RuntimeProfile::builtin("image_input")).values()));
    CHECK(image.ocr.text.find("godzina 09:00") != std::string::npos);
    CHECK(image_state(*f.rt) == before);
    CHECK(f.rt->chat().build_content_checked("Odczytaj tekst", opts.attachments, nullptr));
    CHECK(f.rt->chat().preview_usage("Odczytaj tekst", opts));
    CHECK(f.rt->chat().provider_request_preview("Odczytaj tekst", opts));
    CHECK(image_state(*f.rt) == before);
    for (const auto* mode : {"ask", "never"}) {
      f.rt->config().set("secrets_in_chat", mode);
      const auto result = unwrap(f.rt->chat().send("Odczytaj tekst", opts));
      REQUIRE(result.secret_pending);
      CHECK((*result.secret_pending)["status"] == (std::string(mode) == "ask" ? "needs_confirmation" : "refused"));
      CHECK(image_state(*f.rt) == before);
      CHECK(f.http->requests().empty());
      CHECK(unwrap(f.rt->blobs().read(upload.hash)) == bytes);
    }
  }
  TEST_CASE("R2 imported reopened historical OCR is gated without a new attachment and excluded rows are absent") {
    Fixture f;
    const auto imported = unwrap(f.rt->importer().import_file(image_file()));
    const auto cid = imported.conversations.at(0).id;
    const auto mid = unwrap(f.rt->db().get_msgs(cid)).at(0).id;
    f.rt.reset(); f.rt = unwrap(Runtime::open(f.options));
    const auto before = image_state(*f.rt);
    auto opts = f.chat(); opts.conv_id = cid;
    const auto held = unwrap(f.rt->chat().send("Odczytaj tekst", opts));
    REQUIRE(held.secret_pending); CHECK(f.http->requests().empty());
    CHECK(image_state(*f.rt) == before);
    opts.secret_confirmation = Json{{"approved", true}}; // old consent is insufficient
    REQUIRE(unwrap(f.rt->chat().secret_gate("Odczytaj tekst", opts)));
    opts.history_skip = {mid};
    CHECK_FALSE(unwrap(f.rt->chat().secret_gate("Odczytaj tekst", opts)));
    auto selected = unwrap(f.rt->chat().build_messages(cid, "Odczytaj tekst", {}, opts));
    CHECK(selected.dump().find("godzina 09:00") == std::string::npos);
    opts.history_skip.clear(); opts.include_history = false;
    CHECK_FALSE(unwrap(f.rt->chat().secret_gate("Odczytaj tekst", opts)));
    CHECK(unwrap(f.rt->chat().build_messages(cid, "Odczytaj tekst", {}, opts)).dump().find("godzina 09:00") == std::string::npos);
    opts.include_history = true;
    LOOM_REQUIRE_OK(f.rt->db().set_msg_status(mid, "excluded"));
    CHECK_FALSE(unwrap(f.rt->chat().secret_gate("Odczytaj tekst", opts)));
    CHECK(image_state(*f.rt) == before); CHECK(f.http->requests().empty());
  }
  TEST_CASE("R2 history window and budget remove OCR from both admission and payload") {
    Fixture f;
    const auto imported = unwrap(f.rt->importer().import_file(image_file()));
    const auto cid = imported.conversations.at(0).id;
    NewMessage later; later.conv_id = cid; later.role = "user"; later.text = "synthetic harmless later turn";
    LOOM_REQUIRE_OK(f.rt->db().create_msg(later));
    auto data = unwrap(RuntimeProfile::builtin("chat_context")).values();
    Json profiles = data["selector_profiles"];
    Json selected;
    for (const auto& row : profiles) if (row["id"] == "compat") selected = row;
    REQUIRE(selected.is_object());
    Json window = selected; window["id"] = "m1_window"; window["breadth"]["history_window"] = 1;
    Json budget = selected; budget["id"] = "m1_budget"; budget["budget_tokens"] = 1;
    profiles.push_back(window); profiles.push_back(budget);
    LOOM_REQUIRE_OK(fsutil::write_file(f.tmp.path() / "profiles/chat_context.pack",
        Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "chat_context"},
             {"overrides", {{"selector_profiles", profiles}}}}.dump()));
    const auto before = image_state(*f.rt);
    for (const auto* profile : {"m1_window", "m1_budget"}) {
      auto opts = f.chat(); opts.conv_id = cid; opts.selector_profile = profile;
      CHECK_FALSE(unwrap(f.rt->chat().secret_gate("Odczytaj tekst", opts)));
      chat::PreparedImages images;
      const auto messages = unwrap(f.rt->chat().build_messages(cid, "Odczytaj tekst", {}, opts, "", nullptr, &images));
      CHECK(images.empty()); CHECK(messages.dump().find("godzina 09:00") == std::string::npos);
      CHECK(image_state(*f.rt) == before); CHECK(f.http->requests().empty());
    }
  }
  TEST_CASE("R2 approval binds selected OCR original parts and question instead of prior turn boolean") {
    Fixture f;
    auto image = unwrap(chat::prepare_image_attachment(f.rt->media(), image_file(), image_file(),
        "Odczytaj tekst", unwrap(RuntimeProfile::builtin("image_input")).values()));
    auto recipe = unwrap(RuntimeProfile::builtin("chat_secrets")).values(); recipe["patterns"] = synthetic_patterns();
    const auto gate = [&](const Json& confirmation, const chat::PreparedImage& item, const char* question) {
      return unwrap(chat::image_secret_gate(recipe, "ask", Json::object(), confirmation, Json(question), {item}));
    };
    const auto held = gate(Json::object(), image, "Odczytaj tekst"); REQUIRE(held.is_object());
    const Json confirmed{{"approved", true}, {"image_payload_sha256", held["image_payload_sha256"]}};
    CHECK(gate(confirmed, image, "Odczytaj tekst").is_null());
    CHECK_FALSE(gate(Json{{"approved", true}}, image, "Odczytaj tekst").is_null());
    CHECK_FALSE(gate(confirmed, image, "Inne pytanie").is_null());
    auto changed = image; changed.ocr.representation["method"]["version"] = "synthetic-new-version";
    CHECK_FALSE(gate(confirmed, changed, "Odczytaj tekst").is_null());
    changed = image; changed.parts.push_back(Json{{"type", "text"}, {"text", "synthetic new payload"}});
    CHECK_FALSE(gate(confirmed, changed, "Odczytaj tekst").is_null());
    CHECK(f.http->requests().empty());
  }
  TEST_CASE("R3 actual raw image payload is rejected for unsupported adapter before any transport") {
    Fixture f;
    const llm::Services services(f.rt->config(), f.rt->secrets(), *f.http);
    llm::ModelRequest req; req.models = {llm::ModelSpec{"synthetic/text", "", Json::object()}};
    req.route.adapter = "anthropic_messages";
    const Json text_body{{"model", "synthetic/text"}, {"max_tokens", 20},
        {"messages", Json::array({Json{{"role", "user"}, {"content", "synthetic text"}}})}};
    auto image_body = text_body;
    image_body["messages"][0]["content"] = Json::array({Json{{"type", "image"},
        {"source", {{"type", "base64"}, {"media_type", "image/png"}, {"data", "AA=="}}}}});
    for (const bool mismatched_description : {false, true}) {
      req.body = mismatched_description ? text_body : image_body;
      req.body_bytes = image_body.dump();
      const auto prepared = llm::prepare_call(services, req);
      REQUIRE_FALSE(prepared); CHECK(prepared.error().message == "vision_adapter_image_contract_unavailable");
      const auto outcome = unwrap(llm::use_model(services, req));
      CHECK_FALSE(outcome.ok()); CHECK_FALSE(outcome.last().sent); CHECK(f.http->requests().empty());
    }
    req.body = image_body; req.body_bytes.clear();
    REQUIRE_FALSE(llm::prepare_call(services, req)); CHECK(f.http->requests().empty());
    // Raw text wins over a stale image description, preserving Anthropic text.
    req.body = image_body; req.body_bytes = text_body.dump();
    CHECK(unwrap(llm::prepare_call(services, req)).request.body == text_body.dump());
    req.body = text_body; req.body_bytes = "{invalid";
    CHECK_FALSE(llm::prepare_call(services, req)); CHECK(f.http->requests().empty());
    req.body_bytes.clear();
    CHECK(llm::prepare_call(services, req)); CHECK(f.http->requests().empty());
    // Serialized parameters must not introduce image parts after CallSpec checking.
    req.route.adapter = "openai_chat"; req.body = nullptr;
    req.messages = text_body["messages"]; req.parameters["messages"] = image_body["messages"];
    CHECK_FALSE(llm::prepare_call(services, req)); CHECK(f.http->requests().empty());
    // A non-message list operation still follows its existing adapter contract.
    req.operation = llm::Operation::ListModels; req.parameters = Json::object(); req.messages = Json::array();
    CHECK(llm::prepare_call(services, req)); CHECK(f.http->requests().empty());
  }
}
