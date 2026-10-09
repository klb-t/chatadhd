#include <doctest/doctest.h>

#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
Json perspective_resolution(const Json& state, std::string_view key) {
  auto resolver = unwrap(DefaultLayers::create(state.at("pack"), state.at("layers")));
  return unwrap(resolver.resolve(key));
}
Json perspective_action(OnboardingStore& store, const Json& state, const Json& action) {
  auto command = action;
  command["target"] = "layers";
  command["id"] = "synthetic/perspective/" + std::to_string(state.at("revision").get<std::int64_t>());
  command["time"] = "2000-01-01T00:00:00Z";
  return unwrap(store.apply(state.at("user_id").get<std::string>(), state.at("revision").get<std::int64_t>(), command));
}
}

TEST_SUITE("graph perspective product persistence") {
  TEST_CASE("product perspective defaults share native layers without changing operational defaults") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "perspective.db");
    OnboardingStore store(*db);
    auto state = unwrap(store.open("synthetic/perspective-a"));
    const auto prior_privacy = state.at("profile").at("privacy");
    const auto prior_settings = state.at("profile").at("settings");
    CHECK(perspective_resolution(state, "graph.perspective.enabled").at("value") == false);
    CHECK(perspective_resolution(state, "graph.perspective.traversal").at("value").at("hops") == 1);
    CHECK(perspective_resolution(state, "graph.perspective.budget").at("value").at("render") == 60);
    state = perspective_action(store, state, {{"op", "override"}, {"key", "graph.perspective.enabled"}, {"value", true}});
    state = perspective_action(store, state, {{"op", "override"}, {"key", "graph.perspective.budget"},
        {"value", {{"query", 47}, {"render", 11}, {"page", 7}}}});
    const auto resolution = perspective_resolution(state, "graph.perspective.budget");
    CHECK(resolution.at("status") == "effective");
    CHECK(resolution.at("value").at("render") == 11);
    CHECK(resolution.contains("source"));
    CHECK(state.at("profile").at("privacy") == prior_privacy);
    CHECK(state.at("profile").at("settings") == prior_settings);
    const auto second = unwrap(store.open("synthetic/perspective-b"));
    CHECK(perspective_resolution(second, "graph.perspective.enabled").at("value") == false);
    CHECK(perspective_resolution(second, "graph.perspective.budget").at("value").at("render") == 60);
    db.reset();
    db = open_db(temp.path() / "perspective.db");
    OnboardingStore reopened(*db);
    state = unwrap(reopened.open("synthetic/perspective-a"));
    CHECK(perspective_resolution(state, "graph.perspective.enabled").at("value") == true);
    CHECK(perspective_resolution(state, "graph.perspective.budget").at("value").at("render") == 11);
    state = perspective_action(reopened, state, {{"op", "exclude"}, {"key", "graph.perspective.budget"}});
    auto pack = state.at("pack");
    pack["revision"] = pack.at("revision").get<std::int64_t>() + 1;
    for (auto& entry : pack["entries"]) if (entry.at("key") == "graph.perspective.budget") {
      entry["revision"] = entry.at("revision").get<std::int64_t>() + 1;
      entry["value"]["render"] = 90;
    }
    state = unwrap(reopened.update_pack("synthetic/perspective-a", state.at("revision").get<std::int64_t>(), pack, state.at("scenario_definition")));
    const auto excluded = perspective_resolution(state, "graph.perspective.budget");
    CHECK(excluded.at("status") == "excluded");
    CHECK_FALSE(excluded.contains("value"));
    CHECK(state.at("profile").at("privacy") == prior_privacy);
    db.reset();
    db = open_db(temp.path() / "perspective.db");
    OnboardingStore restored(*db);
    state = unwrap(restored.open("synthetic/perspective-a"));
    CHECK(perspective_resolution(state, "graph.perspective.budget").at("status") == "excluded");
  }

  TEST_CASE("stored pre-perspective pack remains unchanged until explicit native migration") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "legacy-perspective.db");
    OnboardingStore store(*db);
    auto seed = unwrap(store.open("synthetic/legacy-perspective"));
    // Build a valid public synthetic pre-v5 body. No private profile is modified.
    auto old_pack = seed.at("pack");
    old_pack.erase("entry_packs"); old_pack["revision"] = 4;
    auto entries = Json::array();
    for (const auto& entry : old_pack.at("entries"))
      if (!entry.at("key").get<std::string>().starts_with("graph.perspective.")) entries.push_back(entry);
    old_pack["entries"] = entries;
    old_pack["vendor_unknown"] = {{"retained", Json::array({2, 3, 5})},
      {"large_integer", std::uint64_t{9007199254740993ULL}}, {"integral_float", 1.0}};
    old_pack["entries"][0]["vendor_unknown"] = old_pack.at("vendor_unknown");
    seed["pack"] = old_pack;
    seed["layers"] = unwrap(DefaultLayers::create(old_pack)).snapshot();
    const auto original_profile = seed.at("profile");
    LOOM_REQUIRE_OK(db->conn().run("UPDATE loom_onboarding_profiles SET body=? WHERE user_id=?",
        json::dump(seed), "synthetic/legacy-perspective"));
    const auto old_body = unwrap(db->conn().query_text("SELECT body FROM loom_onboarding_profiles WHERE user_id=?", "synthetic/legacy-perspective"));
    auto state = unwrap(store.open("synthetic/legacy-perspective"));
    CHECK(state.at("pack").at("revision") == 4);
    CHECK(perspective_resolution(state, "graph.perspective.enabled").at("status") == "missing");
    CHECK(unwrap(db->conn().query_text("SELECT body FROM loom_onboarding_profiles WHERE user_id=?", "synthetic/legacy-perspective")) == old_body);
    const auto current_pack = unwrap(builtin_pack());
    const auto extension = current_pack.at("entry_packs").at(0);
    auto duplicate = extension;
    duplicate["entries"].push_back(duplicate.at("entries").at(0));
    CHECK_FALSE(store.install_entries("synthetic/legacy-perspective", state.at("revision").get<std::int64_t>(), duplicate));
    CHECK(unwrap(db->conn().query_text("SELECT body FROM loom_onboarding_profiles WHERE user_id=?", "synthetic/legacy-perspective")) == old_body);
    state = unwrap(store.install_entries("synthetic/legacy-perspective", state.at("revision").get<std::int64_t>(), extension));
    CHECK(state.at("pack").at("revision") == 5);
    CHECK(perspective_resolution(state, "graph.perspective.enabled").at("value") == false);
    CHECK(json::dump(state.at("pack").at("vendor_unknown")) == json::dump(old_pack.at("vendor_unknown")));
    CHECK(state.at("pack").at("vendor_unknown").at("large_integer").get<std::uint64_t>() == 9007199254740993ULL);
    CHECK(state.at("pack").at("vendor_unknown").at("integral_float").is_number_float());
    CHECK(json::dump(state.at("pack").at("entries").at(0)) == json::dump(old_pack.at("entries").at(0)));
    CHECK(state.at("profile").at("privacy") == original_profile.at("privacy"));
    CHECK(state.at("profile").at("settings") == original_profile.at("settings"));
    auto stale = store.install_entries("synthetic/legacy-perspective", seed.at("revision").get<std::int64_t>(), extension);
    REQUIRE_FALSE(stale);
    CHECK(stale.error().code == Errc::Conflict);
    auto collision = extension;
    collision["entries"][0]["id"] = "onboarding.settings/v1";
    CHECK_FALSE(store.install_entries("synthetic/legacy-perspective", state.at("revision").get<std::int64_t>(), collision));
    CHECK(unwrap(store.read("synthetic/legacy-perspective")).at("revision") == state.at("revision"));
    db.reset(); db = open_db(temp.path() / "legacy-perspective.db");
    OnboardingStore restored(*db);
    CHECK(perspective_resolution(unwrap(restored.open("synthetic/legacy-perspective")), "graph.perspective.enabled").at("value") == false);
  }
}
