#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <utility>

#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
constexpr const char* kPresentationUser = "synthetic/onboarding-presentation-user";
constexpr const char* kOfflineProvider = "offline/synthetic-provider";
constexpr const char* kPresentationKey = "presentation.onboarding";
constexpr const char* kPresentationId = "onboarding.presentation/v1";

DbOptions presentation_db_options() {
  DbOptions options;
  options.enable_fts = false;
  return options;
}
Json presentation_apply(OnboardingStore& store, const Json& state, const Json& action) {
  return unwrap(store.apply(kPresentationUser, state.at("revision").get<std::int64_t>(), action));
}
Json presentation_resolution(const Json& state, const char* key) {
  auto layers = unwrap(DefaultLayers::create(state.at("pack"), state.at("layers")));
  return unwrap(layers.resolve(key));
}
Json model_facing_request(const Json& request) {
  Json result = Json::object();
  for (const char* key : {"provider", "prompt", "section", "questions", "context", "candidates", "policy", "reply_schema"})
    result[key] = request.at(key);
  CHECK_FALSE(request.at("calls_authorized").get<bool>());
  return result;
}
Json operational_decision(Json decision) {
  // Presentation and local CAS revision are metadata, not policy semantics.
  decision.erase("snapshot_revision");
  decision.at("resolution").erase("explanation");
  return decision;
}
Json privacy_request(const char* op) {
  return Json{{"op", op}, {"field", "communication.style"}, {"category", "communication"},
              {"provenance", "form"}, {"detail", 1}, {"sensitivity", 1}, {"provider", kOfflineProvider}};
}
std::string presentation_body(Database& db) {
  auto body = unwrap(db.conn().query_text("SELECT body FROM loom_onboarding_profiles WHERE user_id=?", kPresentationUser));
  REQUIRE(body);
  return *body;
}
Json native_projection_bytes(Database& db, const std::string& run) {
  Json result = Json::object();
  for (const char* table : {"loom_kb_entities", "loom_kb_claims", "loom_kb_observations"}) {
    auto bytes = unwrap(db.conn().query_text(std::string("SELECT group_concat(body, '\n') FROM (SELECT body FROM ") +
        table + " WHERE run_id=? ORDER BY id)", run));
    REQUIRE(bytes);
    result[table] = *bytes;
  }
  return result;
}
Json& presentation_entry(Json& pack) {
  for (auto& entry : pack.at("entries")) if (entry.at("key") == kPresentationKey) {
    REQUIRE(entry.at("id") == kPresentationId);
    return entry;
  }
  FAIL("canonical onboarding presentation entry must exist");
  return pack;
}
Json confirm_presentation_preference(OnboardingStore& store, Json state) {
  state = presentation_apply(store, state, Json{{"op", "answer"}, {"id", "synthetic/presentation-style"},
      {"field", "communication.style"}, {"value", "Synthetic confirmed concise style"}, {"provenance", "form"},
      {"time", "2000-01-01T00:00:00Z"}, {"source_refs", Json::array({"synthetic/presentation-fixture"})}});
  CHECK(state.at("profile").at("fields").at("communication.style").at("status") == "unknown");
  state = presentation_apply(store, state, Json{{"op", "review"}, {"id", "synthetic/presentation-confirm"},
      {"candidate", "synthetic/presentation-style"}, {"decision", "confirmed"}, {"time", "2000-01-01T00:00:00Z"},
      {"source_refs", Json::array({"synthetic/presentation-fixture"})}});
  REQUIRE(state.at("profile").at("fields").at("communication.style").at("review") == "confirmed");
  return state;
}
}  // namespace

TEST_SUITE("onboarding native presentation isolation") {
  TEST_CASE("answer and review remain operational with disabled or excluded presentation across restart") {
    for (const char* op : {"disable", "exclude"}) {
      INFO(op);
      fsutil::TempDir temp;
      const auto path = temp.path() / "presentation-answer-review.db";
      auto db = open_db(path, presentation_db_options());
      OnboardingStore store(*db);
      auto state = confirm_presentation_preference(store, unwrap(store.open(kPresentationUser)));
      state = presentation_apply(store, state, Json{{"target", "layers"}, {"op", op}, {"key", kPresentationKey}});
      const auto status = std::string(op) == "disable" ? "disabled" : "excluded";
      const auto original_field = state.at("profile").at("fields").at("communication.style");
      const auto settings = state.at("profile").at("settings");
      const auto privacy = state.at("profile").at("privacy");
      const auto candidate = std::string("synthetic/presentation-suppressed-style/") + op;
      const Json value = std::string("Synthetic new confirmed style while presentation is ") + status;
      state = presentation_apply(store, state, Json{{"op", "answer"}, {"id", candidate},
          {"field", "communication.style"}, {"value", value}, {"provenance", "form"},
          {"time", "2000-01-01T00:00:00Z"}, {"source_refs", Json::array({"synthetic/suppressed-form"})}});
      CHECK(state.at("profile").at("candidates").at(candidate).at("review") == "pending");
      CHECK(state.at("profile").at("fields").at("communication.style") == original_field);
      CHECK(presentation_resolution(state, "preference.style").at("value") == original_field.at("value"));
      state = presentation_apply(store, state, Json{{"op", "review"}, {"id", "confirm/" + candidate},
          {"candidate", candidate}, {"decision", "confirmed"}, {"time", "2000-01-01T00:00:00Z"},
          {"source_refs", Json::array({"synthetic/suppressed-form"})}});
      const auto confirmed = state.at("profile").at("fields").at("communication.style");
      CHECK(confirmed.at("status") == "known");
      CHECK(confirmed.at("review") == "confirmed");
      CHECK(confirmed.at("provenance") == "form");
      CHECK(confirmed.at("value") == value);
      CHECK(state.at("profile").at("candidates").at(candidate).at("review") == "confirmed");
      CHECK(presentation_resolution(state, "preference.style").at("layer") == "user");
      CHECK(presentation_resolution(state, "preference.style").at("value") == value);
      CHECK(state.at("presentation").at("available") == false);
      CHECK(state.at("presentation").at("status") == status);
      CHECK_FALSE(state.at("presentation").contains("value"));
      CHECK(state.at("profile").at("settings") == settings);
      CHECK(state.at("profile").at("privacy") == privacy);
      CHECK(unwrap(store.policy_decision(kPresentationUser, privacy_request("store"))).at("allowed") == true);
      const auto raw = presentation_body(*db);
      db.reset();
      auto restarted_db = open_db(path, presentation_db_options());
      OnboardingStore restarted(*restarted_db);
      const auto restored = unwrap(restarted.open(kPresentationUser));
      CHECK(restored == state);
      CHECK(presentation_body(*restarted_db) == raw);
      CHECK(restored.at("profile").at("fields").at("communication.style") == confirmed);
      CHECK(presentation_resolution(restored, "preference.style").at("value") == value);
      CHECK(restored.at("presentation").at("available") == false);
    }
  }

  TEST_CASE("disabled and excluded presentation preserve profile policy and prepared model inputs") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "presentation-isolation.db", presentation_db_options());
    OnboardingStore store(*db);
    auto state = confirm_presentation_preference(store, unwrap(store.open(kPresentationUser)));
    REQUIRE(state.at("pack").at("revision") == 6);
    REQUIRE(state.at("presentation").at("available") == true);
    const auto profile = state.at("profile");
    const auto settings = presentation_resolution(state, "onboarding.settings");
    const auto privacy = presentation_resolution(state, "onboarding.privacy");
    const auto request = model_facing_request(unwrap(store.model_request(kPresentationUser, kOfflineProvider)));
    Json decisions = Json::object();
    for (const char* op : {"store", "infer", "send"})
      decisions[op] = operational_decision(unwrap(store.policy_decision(kPresentationUser, privacy_request(op))));
    CHECK(decisions.at("store").at("allowed") == true);
    CHECK(decisions.at("infer").at("allowed") == false);
    CHECK(decisions.at("send").at("allowed") == false);

    for (const char* op : {"disable", "exclude"}) {
      INFO(op);
      state = presentation_apply(store, state, Json{{"target", "layers"}, {"op", op}, {"key", kPresentationKey}});
      const auto status = std::string(op) == "disable" ? "disabled" : "excluded";
      CHECK(state.at("presentation").at("available") == false);
      CHECK(state.at("presentation").at("status") == status);
      CHECK_FALSE(state.at("presentation").contains("value"));
      CHECK_FALSE(presentation_resolution(state, kPresentationKey).contains("value"));
      CHECK(state.at("profile") == profile);
      for (const auto& [key, original] : {std::pair{"onboarding.settings", settings}, std::pair{"onboarding.privacy", privacy}}) {
        auto effective = presentation_resolution(state, key);
        CHECK(effective.at("explanation").is_null());
        effective.erase("explanation");
        auto expected = original;
        expected.erase("explanation");
        CHECK(effective == expected);
      }
      for (const char* operation : {"store", "infer", "send"})
        CHECK(operational_decision(unwrap(store.policy_decision(kPresentationUser, privacy_request(operation)))) == decisions.at(operation));
      CHECK(model_facing_request(unwrap(store.model_request(kPresentationUser, kOfflineProvider))) == request);
      CHECK(unwrap(store.read(kPresentationUser)) == state);
    }
  }

  TEST_CASE("catalog pack upgrade and database restart retain exclusion and confirmed user preference") {
    fsutil::TempDir temp;
    const auto path = temp.path() / "presentation-upgrade.db";
    auto db = open_db(path, presentation_db_options());
    OnboardingStore store(*db);
    auto state = confirm_presentation_preference(store, unwrap(store.open(kPresentationUser)));
    state = presentation_apply(store, state, Json{{"target", "layers"}, {"op", "exclude"}, {"key", kPresentationKey}});
    const auto field = state.at("profile").at("fields").at("communication.style");
    const auto exclusion = state.at("layers").at("exclusions").at(kPresentationId);
    const auto settings = state.at("profile").at("settings");
    const auto privacy = state.at("profile").at("privacy");
    const auto request = model_facing_request(unwrap(store.model_request(kPresentationUser, kOfflineProvider)));
    auto pack = state.at("pack");
    REQUIRE(pack.at("revision") == 6);
    pack["revision"] = pack.at("revision").get<std::int64_t>() + 1;
    auto& entry = presentation_entry(pack);
    entry["revision"] = entry.at("revision").get<std::int64_t>() + 1;
    entry.at("value").at("locales").at("en")["layer.builtin"] = "Synthetic upgraded catalog label";
    state = unwrap(store.update_pack(kPresentationUser, state.at("revision").get<std::int64_t>(), pack, state.at("scenario_definition")));
    CHECK(state.at("pack") == pack);
    CHECK(state.at("presentation").at("available") == false);
    CHECK(state.at("presentation").at("status") == "excluded");
    CHECK(state.at("layers").at("exclusions").at(kPresentationId) == exclusion);
    CHECK_FALSE(presentation_resolution(state, kPresentationKey).contains("value"));
    CHECK(state.at("profile").at("fields").at("communication.style") == field);
    CHECK(state.at("profile").at("settings") == settings);
    CHECK(state.at("profile").at("privacy") == privacy);
    CHECK(presentation_resolution(state, "preference.style").at("layer") == "user");
    CHECK(presentation_resolution(state, "preference.style").at("value") == field.at("value"));
    CHECK(presentation_resolution(state, "onboarding.settings").at("value") == settings);
    CHECK(presentation_resolution(state, "onboarding.privacy").at("value") == privacy);
    CHECK(model_facing_request(unwrap(store.model_request(kPresentationUser, kOfflineProvider))) == request);
    const auto raw = presentation_body(*db);
    const auto graph = native_projection_bytes(*db, state.at("graph_run_id").get<std::string>());
    db.reset();
    auto restarted_db = open_db(path, presentation_db_options());
    OnboardingStore restarted(*restarted_db);
    CHECK(unwrap(restarted.open(kPresentationUser)) == state);
    CHECK(presentation_body(*restarted_db) == raw);
    CHECK(native_projection_bytes(*restarted_db, state.at("graph_run_id").get<std::string>()) == graph);
    CHECK(model_facing_request(unwrap(restarted.model_request(kPresentationUser, kOfflineProvider))) == request);
  }

  TEST_CASE("malformed active catalog update rolls back persisted profile and native graph atomically") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "presentation-rollback.db", presentation_db_options());
    OnboardingStore store(*db);
    const auto state = confirm_presentation_preference(store, unwrap(store.open(kPresentationUser)));
    REQUIRE(state.at("presentation").at("available") == true);
    const auto body = presentation_body(*db);
    const auto graph = native_projection_bytes(*db, state.at("graph_run_id").get<std::string>());
    const auto request = unwrap(store.model_request(kPresentationUser, kOfflineProvider));
    const auto decision = unwrap(store.policy_decision(kPresentationUser, privacy_request("store")));
    for (const char* malformed : {"schema", "default_locale"}) {
      INFO(malformed);
      auto pack = state.at("pack");
      pack["revision"] = pack.at("revision").get<std::int64_t>() + 1;
      auto& entry = presentation_entry(pack);
      entry["revision"] = entry.at("revision").get<std::int64_t>() + 1;
      entry.at("value")[malformed] = "synthetic/undeclared";
      auto result = store.update_pack(kPresentationUser, state.at("revision").get<std::int64_t>(), pack, state.at("scenario_definition"));
      REQUIRE_FALSE(result);
      CHECK(result.error().code == Errc::InvalidArgument);
      CHECK(presentation_body(*db) == body);
      CHECK(native_projection_bytes(*db, state.at("graph_run_id").get<std::string>()) == graph);
      CHECK(unwrap(store.read(kPresentationUser)) == state);
      CHECK(unwrap(store.model_request(kPresentationUser, kOfflineProvider)) == request);
      CHECK(unwrap(store.policy_decision(kPresentationUser, privacy_request("store"))) == decision);
    }
  }
}
