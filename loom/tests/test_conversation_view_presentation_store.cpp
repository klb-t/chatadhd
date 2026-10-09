#include <doctest/doctest.h>
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/util/time.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
constexpr const char* user = "synthetic/conversation-view-presentation";
constexpr const char* feature_key = "presentation.conversation_view";
Json resolve_view_labels(const Json& state) {
  return unwrap(unwrap(DefaultLayers::create(state.at("pack"), state.at("layers"))).resolve(feature_key));
}
}

TEST_CASE("conversation view optional catalog preserves historical packs until explicit installation and excludes durably") {
  fsutil::TempDir temp;
  const auto path = temp.path() / "source-view-presentation.db";
  auto db = open_db(path);
  OnboardingStore store(*db);
  auto seed = unwrap(store.open(user));
  REQUIRE(seed.at("pack").at("revision") == 6);
  seed = unwrap(store.apply(user, seed.at("revision").get<std::int64_t>(),
      Json{{"op", "answer"}, {"field", "identity.description"}, {"value", "Public migration evidence"},
           {"provenance", "form"}, {"id", "synthetic/historical-answer"},
           {"time", "2000-01-01T00:00:00Z"}, {"source_refs", Json::array({"synthetic/public-fixture"})}}));
  auto legacy = seed.at("pack");
  legacy["revision"] = 5;
  legacy.erase("entry_packs");
  Json old_entries = Json::array();
  for (const auto& entry : legacy.at("entries")) if (entry.at("key") != feature_key) old_entries.push_back(entry);
  legacy["entries"] = old_entries;
  legacy["vendor_unknown"] = {{"integer", std::uint64_t{9007199254740993ULL}}, {"float", 1.0}};
  seed["pack"] = legacy;
  seed["layers"] = unwrap(DefaultLayers::create(legacy)).snapshot();
  const auto old_profile = seed.at("profile");
  LOOM_REQUIRE_OK(db->conn().run("UPDATE loom_onboarding_profiles SET body=? WHERE user_id=?", json::dump(seed), user));
  const auto body = unwrap(db->conn().query_text("SELECT body FROM loom_onboarding_profiles WHERE user_id=?", user));
  auto state = unwrap(store.open(user));
  CHECK(state.at("pack") == legacy);
  CHECK(resolve_view_labels(state).at("status") == "missing");
  CHECK(unwrap(db->conn().query_text("SELECT body FROM loom_onboarding_profiles WHERE user_id=?", user)) == body);
  Json extension;
  const auto current_pack = unwrap(builtin_pack());
  for (const auto& pack : current_pack.at("entry_packs"))
    if (pack.at("pack_id") == "loom.product.conversation_view") extension = pack;
  REQUIRE(extension.is_object());
  state = unwrap(store.install_entries(user, state.at("revision").get<std::int64_t>(), extension));
  CHECK(resolve_view_labels(state).at("status") == "effective");
  // install_entries deliberately reuses update_pack's existing operational
  // defaults audit. Its unchanged privacy rule is recorded, not new consent.
  const auto& history = state.at("profile").at("history");
  REQUIRE(old_profile.at("privacy").at("rules").size() == 1);
  REQUIRE(history.size() == old_profile.at("history").size() + 1);
  for (std::size_t i = 0; i < old_profile.at("history").size(); ++i)
    CHECK(history.at(i) == old_profile.at("history").at(i));
  const auto& audit = history.back();
  REQUIRE(audit.at("time").is_string());
  CHECK(timeutil::parse_iso_utc(audit.at("time").get<std::string>()).has_value());
  const auto& prior_rule = old_profile.at("privacy").at("rules").at(0);
  const Json expected_audit{{"op", "privacy"}, {"rule", prior_rule},
      {"id", "pack-update/" + std::to_string(seed.at("revision").get<std::int64_t>() + 1) + "/" + prior_rule.at("id").get<std::string>()},
      {"time", audit.at("time")}, {"category", prior_rule.at("category")}, {"before", prior_rule}, {"after", prior_rule}};
  CHECK(audit == expected_audit);
  auto expected_profile = old_profile;
  expected_profile["history"].push_back(expected_audit);
  expected_profile["revision"] = old_profile.at("revision").get<std::int64_t>() + 1;
  // This exact comparison also protects fields, candidates, settings, privacy,
  // session, graph and every unknown profile field against unrelated changes.
  CHECK(state.at("profile") == expected_profile);
  CHECK(state.at("pack").at("revision") == 6);
  CHECK(state.at("pack").at("vendor_unknown").at("integer").get<std::uint64_t>() == 9007199254740993ULL);
  CHECK(state.at("pack").at("vendor_unknown").at("float").is_number_float());
  for (std::size_t i = 0; i < old_entries.size(); ++i) CHECK(json::dump(state.at("pack").at("entries").at(i)) == json::dump(old_entries.at(i)));
  state = unwrap(store.apply(user, state.at("revision").get<std::int64_t>(),
      Json{{"target", "layers"}, {"op", "exclude"}, {"key", feature_key}}));
  auto update = state.at("pack"); update["revision"] = 7;
  for (auto& entry : update["entries"]) if (entry.at("key") == feature_key) {
    entry["revision"] = 2; entry["value"]["locales"]["en"]["local_read"] = "Changed public label";
  }
  state = unwrap(store.update_pack(user, state.at("revision").get<std::int64_t>(), update, state.at("scenario_definition")));
  CHECK(resolve_view_labels(state).at("status") == "excluded");
  CHECK_FALSE(resolve_view_labels(state).contains("value"));
  CHECK(state.at("profile").at("privacy") == old_profile.at("privacy"));
  CHECK(state.at("profile").at("settings") == old_profile.at("settings"));
  db.reset(); db = open_db(path);
  OnboardingStore reopened(*db);
  const auto restored = unwrap(reopened.open(user));
  CHECK(resolve_view_labels(restored).at("status") == "excluded");
  CHECK(restored.at("pack").at("vendor_unknown").at("integer").get<std::uint64_t>() == 9007199254740993ULL);
}
