#include <doctest/doctest.h>

#include "loom/adapter_registry.h"
#include "loom/db.h"
#include "loom/port_compose.h"
#include "loom/port_profile.h"
#include "../src/context/method_channels.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

TEST_SUITE("ports") {
  TEST_CASE("default profile = today's roles; scope order application < user < conversation") {
    fsutil::TempDir td;
    auto prof = unwrap(PortProfile::load(td.path()));
    Json settings{{"default_model", "m-chat"}, {"semantic_model", "m-fast"}};
    auto chat = unwrap(prof.resolve("chat.default", Json::object(), "", settings));
    CHECK(chat["scope"] == "installation");
    CHECK(chat["adapter"] == "openai_chat");
    CHECK(chat["params"]["model"] == "m-chat");
    CHECK(unwrap(prof.resolve("analysis.fast", Json::object(), "", settings))["params"]["model"] == "m-fast");
    CHECK(unwrap(prof.resolve("asr"))["compose"] == "fallback");
    CHECK(unwrap(prof.resolve("format.zip"))["adapter"] == "auto");  // wildcard
    CHECK_FALSE(unwrap(prof.resolve("nope"))["defined"].get<bool>());

    // user layer: a file in the data directory
    std::filesystem::create_directories(td.path() / "profiles");
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles" / "ports.pack",
        R"({"schema":"loom.runtime_profile_overlay/1","domain":"ports","overrides":{"roles":{"analysis.fast":{"adapter":"openai_chat","params":{"model":"user-model"}}}}})"));
    auto user = unwrap(PortProfile::load(td.path()));
    auto a = unwrap(user.resolve("analysis.fast", Json::object(), "", settings));
    CHECK(a["scope"] == "person");
    CHECK(a["params"]["model"] == "user-model");
    CHECK(a["file"].get<std::string>().find("profiles/ports.pack") != std::string::npos);
    CHECK(unwrap(user.resolve("chat.default", Json::object(), "", settings))["scope"] == "installation");  // untouched

    // conversation overrides the user
    Json conv{{"analysis.fast", Json{{"adapter", "anthropic_messages"}, {"params", Json{{"model", "conv-model"}}}}}};
    auto c = unwrap(user.resolve("analysis.fast", conv, "c_1", settings));
    CHECK(c["scope"] == "conversation");
    CHECK(c["adapter"] == "anthropic_messages");
    auto ex = unwrap(user.explain("analysis.fast", conv, "c_1", settings));
    REQUIRE(ex["layers"].size() == 3);
    CHECK(ex["layers"][0]["scope"] == "installation");
    CHECK(ex["layers"][0]["winner"] == false);
    CHECK(ex["layers"][1]["scope"] == "person");
    CHECK(ex["layers"][1]["defined"] == true);
    CHECK(ex["layers"][2]["winner"] == true);
    CHECK(ex["layers"][2]["file"] == "conversations/c_1");
    // another conversation does not see it
    CHECK(unwrap(user.resolve("analysis.fast", Json::object(), "c_2", settings))["scope"] == "person");
  }

  TEST_CASE("roles lists the preset and a custom role needs no code") {
    fsutil::TempDir td;
    std::filesystem::create_directories(td.path() / "profiles");
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles" / "ports.pack",
        R"({"schema":"loom.runtime_profile_overlay/1","domain":"ports","overrides":{"roles":{"my.role":{"compose":"ensemble","members":["x","y"]}}}})"));
    auto prof = unwrap(PortProfile::load(td.path()));
    const auto preset = unwrap(RuntimeProfile::builtin("ports"));
    CHECK(prof.roles().size() == preset.values().at("roles").size() + 1);  // the preset's roles and the user's one, counted from the pack
    auto r = unwrap(prof.resolve("my.role"));
    CHECK(r["compose"] == "ensemble");
    CHECK(r["scope"] == "person");
  }

  TEST_CASE("M1-D1: every role names its job in pl and en, and a role its readers do not resolve yet says so") {
    const auto preset = unwrap(RuntimeProfile::builtin("ports"));
    const Json& roles = preset.values().at("roles");
    for (const char* r : {"chat.default", "analysis.fast", "guardian", "intent.routing", "image.generate", "speech.synthesize",
                          "embed.default", "asr", "ocr"})
      CHECK_MESSAGE(roles.contains(r), r);
    for (auto it = roles.begin(); it != roles.end(); ++it) {
      INFO(it.key());
      for (const char* k : {"label", "why"}) {
        REQUIRE(it.value().contains(k));
        CHECK_FALSE(json::get_string(it.value()[k], "pl").empty());
        CHECK_FALSE(json::get_string(it.value()[k], "en").empty());
      }
      if (const Json* st = json::find(it.value(), "status")) {
        const std::string state = json::get_string(*st, "state");
        CHECK((state == "planned" || state == "partial"));
        for (const char* k : {"today", "reason"}) {
          CHECK_FALSE(json::get_string(st->value(k, Json::object()), "pl").empty());
          CHECK_FALSE(json::get_string(st->value(k, Json::object()), "en").empty());
        }
      }
    }
    CHECK(roles.at("chat.default").at("label").at("pl") == "Rozmowa");
    CHECK(roles.at("analysis.fast").at("label").at("pl") == "Analiza w tle");
    CHECK(roles.at("guardian").at("label").at("pl") == "Sekretarz");
    // the Secretary's role binds semantic_model, not guardian_model (guardian_model is step 1 of policies.guardian), and says so
    CHECK(roles.at("guardian").at("setting") == "semantic_model");
    CHECK(roles.at("guardian").at("why").at("pl").get<std::string>().find("guardian_model") != std::string::npos);
    // the three roles M1-D1 adds are honest: their readers (route.cpp, the params of depict/narrate) read the keys directly
    for (const char* r : {"intent.routing", "image.generate", "speech.synthesize"}) CHECK(roles.at(r).at("status").at("state") == "planned");
    // the role family the action runner really reads (runner.cpp chosen_role "action.<id>"): one documented entry action.*,
    // whose preset chooses no model, so an action keeps the step's model or default_model until the person or the conversation chooses
    REQUIRE(roles.contains("action.*"));
    CHECK(roles.at("action.*").at("label").at("pl") == "Model dla działania");
    CHECK(roles.at("action.*").at("status").at("state") == "partial");
    fsutil::TempDir td;
    auto prof = unwrap(PortProfile::load(td.path()));
    auto summarise = unwrap(prof.resolve("action.summarise", Json::object(), "", Json{{"default_model", "chat-model"}}));
    CHECK(summarise["scope"] == "installation");
    CHECK_FALSE(summarise["params"].contains("model"));
  }

  TEST_CASE("param_settings: a param the entry does not set comes from its setting, like the model") {
    fsutil::TempDir td;
    auto prof = unwrap(PortProfile::load(td.path()));
    auto img = unwrap(prof.resolve("image.generate", Json::object(), "", Json{{"image_model", "img-1"}, {"image_provider", "my-images"}}));
    CHECK(img["params"]["model"] == "img-1");
    CHECK(img["params"]["provider"] == "my-images");
    CHECK(img["adapter"] == "openai_images");
    // empty settings add nothing; a param the entry sets wins over the setting
    CHECK_FALSE(unwrap(prof.resolve("image.generate"))["params"].contains("provider"));
    std::filesystem::create_directories(td.path() / "profiles");
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles" / "ports.pack",
        R"({"schema":"loom.runtime_profile_overlay/1","domain":"ports","overrides":{"roles":{"speech.synthesize":{"params":{"provider":"fixed"},"setting":"speech_model","param_settings":{"provider":"speech_provider"}}}}})"));
    auto user = unwrap(PortProfile::load(td.path()));
    auto sp = unwrap(user.resolve("speech.synthesize", Json::object(), "", Json{{"speech_model", "tts-1"}, {"speech_provider", "other"}}));
    CHECK(sp["params"]["provider"] == "fixed");
    CHECK(sp["params"]["model"] == "tts-1");
  }

  TEST_CASE("compose: fallback") {
    std::vector<int> tried;
    auto r = compose::fallback<int>(std::vector<int>{1, 2, 3}, [&](int m) -> std::optional<Result<int>> {
      tried.push_back(m);
      if (m < 2) return Result<int>(Error(Errc::Unavailable, "x"));
      return Result<int>(m * 10);
    }, Error(Errc::Unavailable, "all failed"));
    CHECK(unwrap(std::move(r)) == 20);
    CHECK(tried == std::vector<int>{1, 2});
    auto none = compose::fallback<int>(std::vector<int>{1}, [](int) -> std::optional<Result<int>> {
      return Result<int>(Error(Errc::Unavailable, "x")); }, Error(Errc::Unavailable, "all failed"));
    CHECK(none.error().message == "x");  // default: the last member error
    auto skipped = compose::fallback<int>(std::vector<int>{1}, [](int) -> std::optional<Result<int>> { return std::nullopt; },
                                          Error(Errc::NotFound, "none"));
    CHECK(skipped.error().code == Errc::NotFound);
  }

  TEST_CASE("compose: mirror with quorum") {
    auto run = [](int quorum, std::vector<bool> ok) {
      int degraded = 0;
      auto st = compose::mirror(ok, [](bool b) -> std::optional<Status> {
        return b ? Status{} : Status(Error(Errc::Unavailable, "down")); },
        quorum, Error(Errc::Unavailable, "none"), [&](bool, const Error&) { ++degraded; });
      return std::make_pair(static_cast<bool>(st), degraded);
    };
    CHECK(run(1, {false, true}) == std::make_pair(true, 1));
    CHECK(run(2, {false, true}) == std::make_pair(false, 1));
    CHECK(run(2, {true, true, false}) == std::make_pair(true, 1));
    CHECK(run(0, {false}).first == false);
  }

  TEST_CASE("compose: chain and ensemble degrade, not fail") {
    auto c = compose::chain<int>(1, std::vector<int>{2, 3}, [](int m, int v) -> Result<int> { return v * m; });
    CHECK(unwrap(std::move(c)) == 6);
    auto bad = compose::chain<int>(1, std::vector<int>{2, 3}, [](int m, int v) -> Result<int> {
      if (m == 2) return Error(Errc::Unavailable, "stop");
      return v; });
    CHECK_FALSE(bad);
    auto e = unwrap(compose::ensemble<int>(std::vector<int>{1, 2, 3}, [](int m) -> std::optional<Result<int>> {
      if (m == 2) return Result<int>(Error(Errc::Unavailable, "member down"));
      return Result<int>(m); }, [](int m) { return "m" + std::to_string(m); }));
    CHECK(e.values == std::vector<int>{1, 3});
    CHECK(e.produced_by == std::vector<std::string>{"m1", "m3"});
    CHECK(e.degraded.size() == 1);
    CHECK(e.to_json()["produced_by"].size() == 2);
    auto q = compose::ensemble<int>(std::vector<int>{1, 2}, [](int m) -> std::optional<Result<int>> {
      if (m == 2) return Result<int>(Error(Errc::Unavailable, "down"));
      return Result<int>(m); }, [](int m) { return std::to_string(m); }, 2);
    CHECK_FALSE(q);
  }

  TEST_CASE("compose: spec parsing and members from an AdapterRegistry") {
    struct P { virtual ~P() = default; virtual std::string id() const = 0; };
    struct A : P { std::string id() const override { return "a"; } };
    struct B : P { std::string id() const override { return "b"; } };
    AdapterRegistry<P> reg;
    reg.add({"a", "implemented", [](const std::string&, const Json&, const NoAdapterEnv&) -> Result<std::unique_ptr<P>> { return std::unique_ptr<P>(new A); }, {}});
    reg.add({"b", "implemented", [](const std::string&, const Json&, const NoAdapterEnv&) -> Result<std::unique_ptr<P>> { return std::unique_ptr<P>(new B); }, {}});
    auto spec = unwrap(compose::Spec::parse(Json{{"compose", "ensemble"}, {"members", Json::array({"a", Json{{"adapter", "b"}}})}, {"quorum", 2}}));
    CHECK(spec.op == "ensemble");
    CHECK(spec.quorum == 2);
    auto members = unwrap(compose::build_members(reg, spec, "x", NoAdapterEnv{}));
    REQUIRE(members.size() == 2);
    CHECK(members[1].port->id() == "b");
    CHECK_FALSE(compose::Spec::parse(Json{{"compose", "bogus"}, {"members", Json::array({"a"})}}));
    CHECK_FALSE(compose::Spec::parse(Json{{"compose", "chain"}}));
    CHECK(unwrap(compose::Spec::parse(Json{{"adapter", "a"}})).op.empty());
    spec.members.push_back(Json{{"adapter", "zzz"}});
    CHECK_FALSE(compose::build_members(reg, spec, "x", NoAdapterEnv{}));
  }

  TEST_CASE("compose: ensemble fuses members' score sets with the search fusion (rrf)") {
    using Scores = std::map<std::string, double>;
    auto out = unwrap(compose::ensemble<Scores>(std::vector<int>{1, 2, 3}, [](int m) -> std::optional<Result<Scores>> {
      if (m == 3) return Result<Scores>(Error(Errc::Unavailable, "down"));
      return Result<Scores>(m == 1 ? Scores{{"a", 2}, {"b", 1}} : Scores{{"b", 2}, {"a", 1}}); },
      [](int m) { return "model" + std::to_string(m); }));
    std::vector<std::pair<double, Scores>> sets;
    for (auto& v : out.values) sets.emplace_back(1.0, v);
    auto fused = unwrap(context::fuse_score_sets(sets, Json{{"operation", "rrf"}, {"signal", "reciprocal_rank"}, {"rrf_constant", 60}}));
    CHECK(fused.at("a") == doctest::Approx(fused.at("b")));  // symmetric ranks
    CHECK(out.produced_by.size() == 2);
    CHECK_FALSE(context::fuse_score_sets(sets, Json{{"operation", "bogus"}, {"signal", "raw_score"}}));
  }
}
