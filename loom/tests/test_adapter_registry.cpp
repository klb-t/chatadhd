// AdapterRegistry<Port>: the contract of the one registry shape, and its users
// (storage backends, media ASR/OCR, the model adapters, batch backends, analysis
// modes, search tiers) - each takes a new adapter as one add(), no consumer edit.
#include <doctest/doctest.h>

#include <filesystem>

#include "loom/adapter_registry.h"
#include "loom/analysis_mode.h"
#include "loom/batch_api.h"
#include "loom/config.h"
#include "loom/media_providers.h"
#include "loom/net/http.h"
#include "loom/selector.h"
#include "loom/storage_backend.h"
#include "loom/use_model.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::storage;
using loom::test::unwrap;

namespace {

struct Greeter {
  virtual ~Greeter() = default;
  virtual std::string hello() const = 0;
};
struct Fixed final : Greeter {
  explicit Fixed(std::string s) : s_(std::move(s)) {}
  std::string hello() const override { return s_; }
  std::string s_;
};
struct Env {
  std::string suffix;
};
using GreeterRegistry = AdapterRegistry<Greeter, Env>;

GreeterRegistry::Descriptor greeter(std::string name, std::string text) {
  return {std::move(name), "implemented",
          [text](const std::string& id, const Json& cfg, const Env& env) -> Result<std::unique_ptr<Greeter>> {
            return std::unique_ptr<Greeter>(std::make_unique<Fixed>(text + json::get_string(cfg, "who", id) + env.suffix));
          }};
}

}  // namespace

TEST_SUITE("adapter registry") {
  TEST_CASE("contract: create by the selector key, registration order, replace keeps the place") {
    GreeterRegistry r("kind", "greeter");
    r.add(greeter("b", "hi "));
    r.add(greeter("a", "yo "));
    CHECK(r.names() == std::vector<std::string>{"b", "a"});
    r.add(greeter("b", "hey "));
    CHECK(r.names() == std::vector<std::string>{"b", "a"});
    auto g = unwrap(r.create("x", Json{{"kind", "b"}, {"who", "bob"}}, Env{"!"}));
    CHECK(g->hello() == "hey bob!");
    CHECK(unwrap(r.create("x", Json{{"kind", "a"}}, Env{"?"}))->hello() == "yo x?");
    CHECK(r.remove("a"));
    CHECK_FALSE(r.contains("a"));
  }

  TEST_CASE("contract: unknown -> Unsupported with the known names, planned -> NotImplemented, alias") {
    GreeterRegistry r("kind", "greeter");
    r.add(greeter("one", "1"));
    r.add({"two", "planned", {}});
    auto unknown = r.create("id1", Json{{"kind", "zzz"}}, Env{});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == Errc::Unsupported);
    CHECK(unknown.error().message.find("zzz") != std::string::npos);
    CHECK(unknown.error().message.find("known: one, two") != std::string::npos);
    CHECK(r.create("id2", Json{{"kind", "two"}}, Env{}).error().code == Errc::NotImplemented);
    r.alias("uno", "one");
    CHECK(r.canonical("uno") == "one");
    CHECK(unwrap(r.create("id3", Json{{"kind", "uno"}}, Env{}))->hello() == "1id3");
    CHECK(r.names() == std::vector<std::string>{"one", "two"});  // aliases are not entries
  }

  TEST_CASE("storage backends keep their vocabulary through the thin alias") {
    BackendRegistry r;
    r.add(KindDescriptor{"tape", "planned", {}});
    CHECK(r.kinds() == std::vector<std::string>{"tape"});
    CHECK(r.find("tape")->status == "planned");
    CHECK(r.create("t", Json{{"kind", "tape"}}, BackendEnv{}).error().code == Errc::NotImplemented);
    CHECK(BackendRegistry::builtin().kinds() == std::vector<std::string>{"local", "s3", "webdav", "gdrive"});
  }

  TEST_CASE("analysis modes and search tiers are registry entries; unknown names stay null") {
    auto modes = AnalysisModeRegistry::instance().names();
    CHECK(modes == std::vector<std::string>{"regex", "llm", "batch"});
    CHECK(AnalysisModeRegistry::instance().create("nope", AnalysisModeContext{}) == nullptr);
    auto tiers = SearchTierRegistry::instance().names();
    CHECK(tiers == std::vector<std::string>{"embedding", "tfidf", "keyword"});
    CHECK(SearchTierRegistry::instance().contains("tfidf"));
    CHECK_FALSE(SearchTierRegistry::instance().contains("nope"));
  }

  TEST_CASE("model adapters: the registry lists the media families; embedding resolves its shorthand from data") {
    const auto families = llm::adapter_families();
    for (const char* f : {"openai_chat", "anthropic_messages", "openai_embeddings", "openai_transcriptions",
                          "google_speech_v1", "ocr_space_form"})
      CHECK(std::count(families.begin(), families.end(), f) == 1);
    CHECK(llm::adapter_family_for("embedding", "") == "openai_embeddings");
    CHECK(llm::adapter_family_for("embedding", "openai") == "openai_embeddings");
    CHECK(llm::adapter_family_for("embedding", "custom_family") == "custom_family");
    CHECK(llm::adapter_family_for("hologram", "").empty());
  }

  TEST_CASE("batch backends: the types are registry entries, a new one is one add") {
    const auto names = SemanticBatchAPI::backends().names();
    CHECK(std::count(names.begin(), names.end(), "anthropic") == 1);
    CHECK(std::count(names.begin(), names.end(), "concurrent") == 1);
    CHECK(SemanticBatchAPI::backends().create_named("nope", "nope", Json::object(), BatchHostRef{}).error().code ==
          Errc::Unsupported);
  }

  TEST_CASE("media: a fake ASR is one registry entry plus a descriptor, and it goes through the model action") {
    struct FakeAsr final : AsrProvider {
      std::string name() const override { return "fake"; }
      Result<AsrResult> transcribe_bytes(std::string_view audio, std::string_view, const std::optional<std::string>&) override {
        AsrResult r;
        r.text = "fake:" + std::string(audio);
        return r;
      }
      Result<AsrResult> transcribe(const std::filesystem::path&, const std::optional<std::string>&) override {
        return AsrResult{};
      }
    };
    asr_adapters().add({"fake_asr", "implemented",
                        [](const std::string&, const Json&, const MediaAdapterEnv&) -> Result<std::unique_ptr<AsrProvider>> {
                          return std::unique_ptr<AsrProvider>(std::make_unique<FakeAsr>());
                        }});
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("fake_key", "k");
    auto defaults = unwrap(RuntimeProfile::builtin("media")).values();
    Json d = defaults["providers"]["groq"];
    d["name"] = "fake";
    d["adapter"] = "fake_asr";
    d["secret_key"] = "fake_key";
    Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "media"},
                 {"overrides", {{"providers", {{"fake", d}}}, {"asr_order", Json::array({"fake"})}, {"ocr_order", Json::array()}}}};
    LOOM_REQUIRE_OK(fsutil::ensure_dir(td.path() / "profiles"));
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles/media.pack", overlay.dump()));
    net::ScriptedTransport http;
    MediaProviders mp(secrets, http);
    CHECK(mp.get_asr_providers() == std::vector<std::string>{"fake"});
    CHECK(unwrap(mp.transcribe_bytes("abc", "wav")).text == "fake:abc");
    CHECK(http.requests().empty());
    asr_adapters().remove("fake_asr");
    mp.refresh();  // the descriptor now names an adapter nobody registered
    auto gone = mp.transcribe_bytes("abc", "wav");
    REQUIRE_FALSE(gone);
    CHECK(gone.error().code == Errc::Unsupported);
    CHECK(gone.error().message.find("known: groq_whisper, google_speech") != std::string::npos);
  }

  TEST_CASE("media: ASR and OCR are the model action - provenance recorded, no secret in it, action can be switched off") {
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("groq_api_key", "gk-secret");
    secrets.set("ocr_space_api_key", "ok-secret");
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"text", "hello"}, {"IsErroredOnProcessing", false},
                                                                    {"ParsedResults", Json::array()}}));
    MediaProviders mp(secrets, http);
    std::vector<Json> records;
    mp.set_provenance_sink([&](const Json& p) { records.push_back(p); });
    CHECK(unwrap(mp.transcribe_bytes("abc", "mp3")).text == "hello");
    LOOM_REQUIRE_OK(mp.ocr_bytes("img", "png"));
    REQUIRE(records.size() == 2);
    CHECK(records[0]["schema"] == "loom.model_use/1");
    CHECK(records[0]["action"] == "media.asr");
    CHECK(records[0]["adapter"] == "openai_transcriptions");
    CHECK(records[0]["provider"] == "groq");
    CHECK(records[0]["status"] == "ok");
    CHECK(records[1]["action"] == "media.ocr");
    CHECK(records[1]["adapter"] == "ocr_space_form");
    for (const auto& r : records) {
      const std::string text = r.dump();
      CHECK(text.find("gk-secret") == std::string::npos);
      CHECK(text.find("ok-secret") == std::string::npos);
      CHECK(text.find("YWJj") == std::string::npos);  // no audio bytes (base64 of "abc") in the record
    }
    // The usage ledger of the data directory admitted both calls.
    CHECK(std::filesystem::exists(td.path() / "usage-policy.sqlite"));

    // The action catalogue is data: switched off, nothing is sent.
    loom::test::declare_action_status(td.path(), "media.asr", "planned");
    const auto before = http.requests().size();
    auto off = mp.transcribe_bytes("abc", "mp3", std::string("groq"));
    REQUIRE_FALSE(off);
    CHECK(off.error().code == Errc::Unsupported);
    CHECK(http.requests().size() == before);
  }
}
