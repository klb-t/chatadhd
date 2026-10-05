#include <doctest/doctest.h>

#include "loom/knowledge_store.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::onboarding;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
constexpr const char* kSyntheticUser = "synthetic/onboarding-store-user";

DbOptions offline_options() {
  DbOptions options;
  options.enable_fts = false;
  return options;
}
Json profile_action(std::string op, std::string id, std::string field = "") {
  Json result{{"op", std::move(op)}, {"id", id}, {"time", "2000-01-01T00:00:00Z"},
              {"source_refs", Json::array({"synthetic/" + id})}};
  if (!field.empty()) result["field"] = std::move(field);
  return result;
}
Json apply(OnboardingStore& store, const Json& state, const Json& action) {
  return unwrap(store.apply(state.at("user_id").get<std::string>(), state.at("revision").get<std::int64_t>(), action));
}
Json known_preference(OnboardingStore& store, Json state, const char* field, const char* candidate,
                      const Json& value) {
  auto answer = profile_action("answer", candidate, field);
  answer["provenance"] = "form";
  answer["value"] = value;
  state = apply(store, state, answer);
  CHECK(state["profile"]["fields"][field]["status"] == "unknown");
  CHECK(state["profile"]["candidates"][candidate]["review"] == "pending");
  auto confirm = profile_action("review", std::string("confirm/") + candidate);
  confirm["candidate"] = candidate;  // distinct review event id, exact candidate id
  confirm["decision"] = "confirmed";
  state = apply(store, state, confirm);
  CHECK(state["profile"]["fields"][field]["value"] == value);
  return state;
}
Json layer_resolution(const Json& state, const char* key) {
  auto layers = unwrap(DefaultLayers::create(state.at("pack"), state.at("layers")));
  return unwrap(layers.resolve(key));
}
std::string persisted_body(Database& db, std::string_view user = kSyntheticUser) {
  auto body = unwrap(db.conn().query_text("SELECT body FROM loom_onboarding_profiles WHERE user_id=?", user));
  REQUIRE(body);
  return *body;
}
Json pack_entry(Json& pack, const char* key) {
  for (auto& entry : pack["entries"]) if (entry["key"] == key) return entry;
  FAIL("synthetic fixture missing expected pack entry");
  return Json::object();
}
Json saved_model_reply(const Json& request, const char* candidate, const char* field,
                      const Json& value, std::string summary = "Synthetic saved model summary; please confirm.") {
  return Json{{"provider", request.at("provider")}, {"request_token", request.at("request_token")},
              {"section", request.at("section")}, {"summary", std::move(summary)},
              {"questions", Json::array({Json{{"field", field}, {"text", "Synthetic follow-up question?"}}})},
              {"candidates", Json::array({Json{{"id", candidate}, {"field", field}, {"value", value},
                  {"time", "2000-01-01T00:00:00Z"}, {"source_refs", Json::array({"synthetic/saved-model-response"})}}})}};
}
Json saved_reply_action(const Json& reply) {
  return Json{{"target", "model_reply"}, {"reply", reply}, {"time", "2000-01-01T00:00:00Z"}};
}
void replace_pack_privacy(Json& pack, const Json& privacy) {
  pack["revision"] = pack["revision"].get<std::int64_t>() + 1;
  for (auto& entry : pack["entries"]) if (entry["key"] == "onboarding.privacy") {
    entry["value"] = privacy;
    entry["revision"] = entry["revision"].get<std::int64_t>() + 1;
  }
  pack["runtime_definition"]["defaults"]["privacy"] = privacy;
  pack["runtime_definition"]["revision"] = pack["runtime_definition"]["revision"].get<std::int64_t>() + 1;
}
}  // namespace

TEST_SUITE("onboarding native persistence") {
  TEST_CASE("fresh native profile seeds durable knowledge and every field starts explicitly unknown") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "fresh.db", offline_options());
    OnboardingStore store(*db);
    const auto original_core_version = unwrap(db->schema_version());
    auto state = unwrap(store.open(kSyntheticUser));
    CHECK(state["schema"] == "loom.onboarding_store/1");
    CHECK(state["revision"] == 0);
    CHECK(state["user_id"] == kSyntheticUser);
    CHECK(state["profile"]["fields"].size() == state["scenario_definition"]["fields"].size());
    for (const auto& [field, record] : state["profile"]["fields"].items()) {
      INFO(field);
      CHECK(record["status"] == "unknown");
      CHECK(record["value"].is_null());
    }
    REQUIRE(state["graph_run_id"].is_string());
    kb::KnowledgeStore knowledge(*db);
    const auto run_id = state["graph_run_id"].get<std::string>();
    const auto run = unwrap(knowledge.get_run(run_id));
    REQUIRE(run);
    CHECK(run->status == "done");
    CHECK(run->inputs["onboarding_user"] == kSyntheticUser);
    CHECK_FALSE(unwrap(knowledge.query_entities(run_id, {})).empty());
    CHECK_FALSE(unwrap(knowledge.query_claims(run_id, {})).empty());
    CHECK(unwrap(db->schema_version()) == original_core_version);
    auto request = unwrap(store.model_request(kSyntheticUser, "offline/synthetic-provider"));
    CHECK_FALSE(request["questions"].empty());
    CHECK(request["context"].empty());
    CHECK_FALSE(request["calls_authorized"].get<bool>());
    CHECK(request["graph_run_id"] == state["graph_run_id"]);
    CHECK(request["snapshot_revision"] == 0);
    CHECK_FALSE(request["request_token"].get<std::string>().empty());
    CHECK(unwrap(store.read(kSyntheticUser)) == state);
    db.reset();
    auto restored_db = open_db(temp.path() / "fresh.db", offline_options());
    OnboardingStore restored(*restored_db);
    CHECK(unwrap(restored.open(kSyntheticUser)) == state);
  }

  TEST_CASE("restart preserves confirmed user values core records and unrelated knowledge runs byte for byte") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "existing.db", offline_options());
    const auto conversation = unwrap(db->create_conv("Synthetic preexisting conversation"));
    NewMessage message;
    message.conv_id = conversation.id;
    message.role = "user";
    message.text = "Synthetic preexisting message; preserve exact bytes\n  second line";
    message.metadata = Json{{"unknown_extension", Json::array({1, nullptr, "keep"})}};
    const auto message_id = unwrap(db->create_msg(message));
    const auto old_conversation = unwrap(db->get_conv(conversation.id))->to_json();
    const auto old_message = unwrap(db->get_msg(message_id))->to_json();
    kb::KnowledgeStore knowledge(*db);
    const auto other_run = unwrap(knowledge.begin_run("synthetic-other-pack", Json{{"source", "synthetic/old.zip"}}));
    model::Entity entity;
    entity.kind = "project"; entity.canonical_key = "synthetic unrelated project"; entity.label = "Synthetic unrelated project";
    entity.id = model::Entity::make_id(entity.kind, entity.canonical_key);
    LOOM_REQUIRE_OK(knowledge.put_entities(other_run.id, {entity}));
    LOOM_REQUIRE_OK(knowledge.finish_run(other_run.id, "done", Json{{"untouched", true}}));
    const auto old_run = unwrap(knowledge.get_run(other_run.id))->to_json();
    const auto old_entity = unwrap(knowledge.query_entities(other_run.id, {})).front().to_json();
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser, Json{{"legacy_extension", {{"bytes", "  keep\n  "}}}}));
    const Json legacy_known{{"fields", {{"communication.style", {
        {"status", "known"}, {"value", "Synthetic imported legacy style"}, {"category", "communication"},
        {"section", "communication"}, {"detail", 1}, {"sensitivity", 1}, {"provenance", "form"},
        {"review", "confirmed"}, {"time", "2000-01-01T00:00:00Z"},
        {"source_refs", Json::array({"synthetic/legacy-import"})}}}}}};
    auto imported = unwrap(store.open("synthetic/imported-legacy-user", legacy_known));
    CHECK(layer_resolution(imported, "preference.style")["layer"] == "user");
    CHECK(layer_resolution(imported, "preference.style")["value"] == "Synthetic imported legacy style");
    CHECK(imported["profile"]["fields"]["communication.style"] == legacy_known["fields"]["communication.style"]);
    state = known_preference(store, state, "communication.style", "synthetic/style", "direct synthetic style");
    CHECK(layer_resolution(state, "preference.style")["value"] == "direct synthetic style");
    CHECK(state["profile"]["legacy_extension"]["bytes"] == "  keep\n  ");
    CHECK(unwrap(db->get_conv(conversation.id))->to_json() == old_conversation);
    CHECK(unwrap(db->get_msg(message_id))->to_json() == old_message);
    CHECK(unwrap(knowledge.get_run(other_run.id))->to_json() == old_run);
    CHECK(unwrap(knowledge.query_entities(other_run.id, {})).front().to_json() == old_entity);
    const auto raw_before = persisted_body(*db);
    db.reset();
    auto restarted_db = open_db(temp.path() / "existing.db", offline_options());
    OnboardingStore restarted(*restarted_db);
    CHECK(unwrap(restarted.open(kSyntheticUser, Json{{"legacy_extension", "must not replace existing user"}})) == state);
    CHECK(persisted_body(*restarted_db) == raw_before);
    CHECK(unwrap(restarted_db->get_conv(conversation.id))->to_json() == old_conversation);
    CHECK(unwrap(restarted_db->get_msg(message_id))->to_json() == old_message);
    kb::KnowledgeStore restarted_knowledge(*restarted_db);
    CHECK(unwrap(restarted_knowledge.get_run(other_run.id))->to_json() == old_run);
    CHECK(unwrap(restarted_knowledge.query_entities(other_run.id, {})).front().to_json() == old_entity);
    CHECK(unwrap(restarted.model_request(kSyntheticUser, "offline/synthetic-provider"))["context"].empty());
  }

  TEST_CASE("delete and purge remove confirmed personal preference from active layers persistent snapshot and knowledge") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "purge.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    const std::string private_fixture = "synthetic-user-value-to-purge-7d14";
    state = known_preference(store, state, "communication.style", "synthetic/private-style", private_fixture);
    const auto graph_run = state["graph_run_id"].get<std::string>();
    CHECK(layer_resolution(state, "preference.style")["layer"] == "user");
    state = apply(store, state, Json{{"target", "layers"}, {"op", "disable"}, {"key", "preference.style"}});
    CHECK(layer_resolution(state, "preference.style")["status"] == "disabled");
    auto erase = profile_action("delete", "synthetic/delete-style", "communication.style");
    erase["purge_history"] = true;
    state = apply(store, state, erase);
    CHECK(state["profile"]["fields"]["communication.style"]["status"] == "unknown");
    CHECK(state["profile"]["fields"]["communication.style"]["value"].is_null());
    CHECK_FALSE(state["layers"]["overrides"].contains("preference.style"));
    CHECK(layer_resolution(state, "preference.style")["status"] == "disabled");
    CHECK(state["graph_run_id"] == graph_run);
    CHECK(persisted_body(*db).find(private_fixture) == std::string::npos);
    for (const char* table : {"loom_kb_observations", "loom_kb_entities", "loom_kb_claims"}) {
      auto rows = unwrap(db->conn().query_text(std::string("SELECT group_concat(body,'') FROM ") + table + " WHERE run_id=?", graph_run));
      if (rows) CHECK(rows->find(private_fixture) == std::string::npos);
    }
    CHECK(unwrap(store.read(kSyntheticUser)) == state);
    state = apply(store, state, Json{{"target", "layers"}, {"op", "reenable"}, {"key", "preference.style"}});
    CHECK(layer_resolution(state, "preference.style")["layer"] == "builtin");
    CHECK(layer_resolution(state, "preference.style")["value"] != private_fixture);
    CHECK(persisted_body(*db).find(private_fixture) == std::string::npos);
  }

  TEST_CASE("prompt layers drive prepared model requests and excluded interview methods stop execution across restart") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "methods.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    auto original = unwrap(store.model_request(kSyntheticUser, "offline/synthetic-provider"));
    const std::string new_prompt = "Synthetic expert-edited onboarding prompt: request only permitted fields.";
    state = apply(store, state, Json{{"target", "layers"}, {"op", "override"}, {"key", "prompt.onboarding"}, {"value", new_prompt}});
    auto edited = unwrap(store.model_request(kSyntheticUser, "offline/synthetic-provider"));
    CHECK(edited["prompt"] == new_prompt);
    CHECK(edited["method_ref"]["recipe_hash"] != original["method_ref"]["recipe_hash"]);
    CHECK_FALSE(edited["calls_authorized"].get<bool>());
    state = apply(store, state, Json{{"target", "layers"}, {"op", "exclude"}, {"key", "method.onboarding"}});
    auto blocked = store.model_request(kSyntheticUser, "offline/synthetic-provider");
    REQUIRE_FALSE(blocked);
    CHECK(blocked.error().code == Errc::Unavailable);
    CHECK(layer_resolution(state, "method.onboarding")["status"] == "excluded");
    db.reset();
    auto restarted_db = open_db(temp.path() / "methods.db", offline_options());
    OnboardingStore restarted(*restarted_db);
    CHECK(unwrap(restarted.read(kSyntheticUser)) == state);
    CHECK_FALSE(restarted.model_request(kSyntheticUser, "offline/synthetic-provider"));
    state = apply(restarted, state, Json{{"target", "layers"}, {"op", "reenable"}, {"key", "method.onboarding"}});
    CHECK(unwrap(restarted.model_request(kSyntheticUser, "offline/synthetic-provider"))["prompt"] == new_prompt);
  }

  TEST_CASE("pack upgrades preserve excluded identities user values and propose new defaults in the same area") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "upgrades.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    state = known_preference(store, state, "communication.style", "synthetic/user-style", "unchanged explicit style");
    state = apply(store, state, Json{{"target", "layers"}, {"op", "exclude"}, {"key", "preference.length"}});
    const auto field_before = state["profile"]["fields"]["communication.style"];
    const auto graph_run_before = state["graph_run_id"];
    Json pack = state["pack"];
    pack["revision"] = pack["revision"].get<std::int64_t>() + 1;
    const auto excluded = pack_entry(pack, "preference.length");
    for (auto& entry : pack["entries"]) if (entry["key"] == "preference.length") {
      entry["revision"] = entry["revision"].get<std::int64_t>() + 1;
      entry["value"] = "updated synthetic default";
    }
    pack["entries"].push_back(Json{{"id", "synthetic.new-default/v1"}, {"key", "preference.synthetic_new"},
        {"area", excluded["area"]}, {"revision", 1}, {"value", "new synthetic preset"}});
    auto scenario = state["scenario_definition"];
    scenario["fields"].push_back(Json{{"id", "synthetic.new-field"}, {"category", "communication"},
        {"section", "communication"}, {"detail", 0}, {"sensitivity", 0}});
    state = unwrap(store.update_pack(kSyntheticUser, state["revision"].get<std::int64_t>(), pack, scenario));
    CHECK(layer_resolution(state, "preference.length")["status"] == "excluded");
    CHECK(layer_resolution(state, "preference.synthetic_new")["status"] == "proposal");
    CHECK(state["profile"]["fields"]["communication.style"] == field_before);
    CHECK(layer_resolution(state, "preference.style")["value"] == "unchanged explicit style");
    CHECK(state["profile"]["fields"]["synthetic.new-field"]["status"] == "unknown");
    CHECK(state["graph_run_id"] == graph_run_before);
    auto raw_before = persisted_body(*db);
    auto stale = store.update_pack(kSyntheticUser, state["revision"].get<std::int64_t>() - 1, pack, scenario);
    REQUIRE_FALSE(stale);
    CHECK(stale.error().code == Errc::Conflict);
    CHECK(persisted_body(*db) == raw_before);
    auto reverse = store.update_pack(kSyntheticUser, state["revision"].get<std::int64_t>(), unwrap(builtin_pack()), unwrap(builtin_scenario()));
    REQUIRE_FALSE(reverse);
    CHECK(reverse.error().code == Errc::Conflict);
    CHECK(persisted_body(*db) == raw_before);
  }

  TEST_CASE("stale CAS malformed actions and unsupported targets fail atomically without changing graph or profile") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "atomic.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    auto changed = apply(store, state, profile_action("pause", "synthetic/pause"));
    const auto before = persisted_body(*db);
    const auto graph_before = unwrap(db->conn().query_text("SELECT group_concat(body,'') FROM loom_kb_entities WHERE run_id=?",
                                                       changed["graph_run_id"].get<std::string>()));
    auto stale = store.apply(kSyntheticUser, 0, profile_action("resume", "synthetic/stale-resume"));
    REQUIRE_FALSE(stale);
    CHECK(stale.error().code == Errc::Conflict);
    for (const auto& bad : Json::array({Json::array(), Json{{"target", "unsupported"}},
                                       Json{{"op", "review"}, {"id", "synthetic/bad-review"}, {"time", "2000-01-01T00:00:00Z"},
                                            {"candidate", "does-not-exist"}, {"decision", "confirmed"}},
                                       Json{{"target", "layers"}, {"op", "override"}, {"key", "prompt.onboarding"}}})) {
      CHECK_FALSE(store.apply(kSyntheticUser, changed["revision"].get<std::int64_t>(), bad));
      CHECK(persisted_body(*db) == before);
      CHECK(unwrap(db->conn().query_text("SELECT group_concat(body,'') FROM loom_kb_entities WHERE run_id=?",
                                       changed["graph_run_id"].get<std::string>())) == graph_before);
    }
    CHECK(unwrap(store.read(kSyntheticUser)) == changed);
  }

  TEST_CASE("independent layer edits advance native CAS without exposing profile revision as its replacement") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "independent-revisions.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    const auto profile_revision = state["profile"]["revision"];
    state = apply(store, state, Json{{"target", "layers"}, {"op", "override"}, {"key", "prompt.onboarding"},
                                     {"value", "Synthetic prompt version one"}});
    CHECK(state["revision"] == 1);
    CHECK(state["profile"]["revision"] == profile_revision);
    auto raw_before = persisted_body(*db);
    auto stale = store.apply(kSyntheticUser, profile_revision.get<std::int64_t>(), profile_action("pause", "synthetic/stale-profile-revision"));
    REQUIRE_FALSE(stale);
    CHECK(stale.error().code == Errc::Conflict);
    CHECK(persisted_body(*db) == raw_before);
    state = apply(store, state, profile_action("pause", "synthetic/current-native-revision"));
    CHECK(state["revision"] == 2);
    CHECK(state["profile"]["revision"] == 1);
    CHECK(unwrap(store.read(kSyntheticUser))["revision"] == 2);
  }

  TEST_CASE("privacy and settings layer overlays govern provider transmission and automatic preference storage") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "layer-policy.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    state = known_preference(store, state, "identity.description", "synthetic/identity", "synthetic tester");
    CHECK(unwrap(store.model_request(kSyntheticUser, "offline/provider-a"))["context"].empty());
    auto privacy = state["profile"]["privacy"];
    privacy["rules"][0]["providers"] = Json::array({"offline/provider-a"});
    privacy["rules"][0]["infer"] = true;
    privacy["rules"][0]["explicit_only"] = false;
    auto allow = profile_action("override", "synthetic/allow-provider");
    allow["target"] = "layers"; allow["key"] = "onboarding.privacy"; allow["value"] = privacy;
    state = apply(store, state, allow);
    CHECK(state["profile"]["privacy"] == privacy);
    CHECK(unwrap(store.model_request(kSyntheticUser, "offline/provider-a"))["context"].contains("identity.description"));
    CHECK(unwrap(store.model_request(kSyntheticUser, "offline/provider-b"))["context"].empty());
    state = apply(store, state, Json{{"target", "layers"}, {"op", "override"}, {"key", "onboarding.settings"},
                                     {"value", {{"preference_mode", "automatic"}}}});
    CHECK(state["profile"]["settings"]["preference_mode"] == "automatic");
    auto proposal = profile_action("propose", "synthetic/automatic-length", "communication.length");
    proposal["value"] = "synthetic inferred length";
    state = apply(store, state, proposal);
    CHECK(state["profile"]["fields"]["communication.length"]["status"] == "known");
    CHECK(state["profile"]["fields"]["communication.length"]["review"] == "accepted_automatically");
    CHECK(state["profile"]["fields"]["communication.length"]["provenance"] == "model_inferred");
    CHECK(layer_resolution(state, "preference.length")["source"]["provenance"] == "model_inferred");
    kb::KnowledgeStore knowledge(*db);
    bool found_inferred_default = false;
    for (const auto& entity : unwrap(knowledge.query_entities(state["graph_run_id"].get<std::string>(), {}))) {
      if (entity.label != "preference.length") continue;
      found_inferred_default = true;
      CHECK(entity.origin == model::Origin::ModelKnowledge);
      CHECK(entity.evidence == model::EvidenceClass::Inferred);
      CHECK(entity.attrs["semantic_value_asserted"] == false);
    }
    CHECK(found_inferred_default);
    privacy["rules"][0]["providers"] = Json::array();
    auto deny = profile_action("override", "synthetic/deny-provider");
    deny["target"] = "layers"; deny["key"] = "onboarding.privacy"; deny["value"] = privacy;
    state = apply(store, state, deny);
    CHECK(state["profile"]["privacy"] == privacy);
    CHECK(unwrap(store.model_request(kSyntheticUser, "offline/provider-a"))["context"].empty());
    CHECK(layer_resolution(state, "onboarding.privacy")["value"] == privacy);
  }

  TEST_CASE("required runtime default can be disabled inspected and explicitly reenabled without silent resurrection") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "required-setting.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    state = apply(store, state, Json{{"target", "layers"}, {"op", "disable"}, {"key", "onboarding.settings"}});
    CHECK(layer_resolution(state, "onboarding.settings")["status"] == "disabled");
    CHECK(unwrap(store.read(kSyntheticUser)) == state);
    CHECK(state["runtime_profile"]["available"] == false);
    auto blocked = store.model_request(kSyntheticUser, "offline/provider-a");
    REQUIRE_FALSE(blocked);
    CHECK(blocked.error().code == Errc::Unavailable);
    auto raw_before = persisted_body(*db);
    CHECK_FALSE(store.apply(kSyntheticUser, state["revision"].get<std::int64_t>(), profile_action("pause", "synthetic/blocked-pause")));
    CHECK(persisted_body(*db) == raw_before);
    state = apply(store, state, Json{{"target", "layers"}, {"op", "reenable"}, {"key", "onboarding.settings"}});
    CHECK(layer_resolution(state, "onboarding.settings")["status"] == "effective");
    CHECK(unwrap(store.model_request(kSyntheticUser, "offline/provider-a"))["calls_authorized"] == false);
    state = apply(store, state, Json{{"target", "layers"}, {"op", "disable"}, {"key", "onboarding.privacy"}});
    const Json request{{"op", "store"}, {"field", "identity.description"}, {"category", "identity"},
                       {"provenance", "user_stated"}, {"detail", 1}, {"sensitivity", 1}};
    auto denied = unwrap(store.policy_decision(kSyntheticUser, request));
    CHECK(denied["allowed"] == false);
    CHECK(denied["resolution"]["status"] == "disabled");
    CHECK_FALSE(state["profile"]["privacy"]["rules"].empty());
    kb::KnowledgeStore knowledge(*db);
    kb::EntityQuery query;
    query.kind = state["pack"]["vocabulary"]["kinds"]["privacy_rule"].get<std::string>();
    const auto rules = unwrap(knowledge.query_entities(state["graph_run_id"].get<std::string>(), query));
    REQUIRE_FALSE(rules.empty());
    for (const auto& rule : rules) {
      CHECK(rule.attrs["applicable"] == false);
      CHECK(rule.attrs["layer_status"] == "disabled");
    }
    state = apply(store, state, Json{{"target", "layers"}, {"op", "reenable"}, {"key", "onboarding.privacy"}});
    CHECK(unwrap(store.policy_decision(kSyntheticUser, request))["allowed"] == true);
  }

  TEST_CASE("host-correlated saved model reply persists exact method-run provenance and rejects stale responses after restart") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "saved-model-run.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    auto expanded_pack = state["pack"];
    expanded_pack["revision"] = expanded_pack["revision"].get<std::int64_t>() + 1;
    auto extra_method = state["scenario_definition"]["graph_method"];
    extra_method["id"] = "synthetic.secondary-method";
    extra_method["prompt"]["text"] = "Synthetic unrelated method prompt";
    extra_method["execution_capability"] = "synthetic_secondary_capability";
    extra_method.erase("default_key"); extra_method.erase("prompt_default_key");
    expanded_pack["methods"].push_back(extra_method);
    auto alternate_version = state["scenario_definition"]["graph_method"];
    alternate_version["revision"] = alternate_version["revision"].get<std::int64_t>() + 1;
    alternate_version["parameters"]["synthetic_setting"] = 1;
    expanded_pack["methods"].push_back(alternate_version);
    state = unwrap(store.update_pack(kSyntheticUser, state["revision"].get<std::int64_t>(), expanded_pack,
                                    state["scenario_definition"]));
    auto privacy = state["profile"]["privacy"];
    privacy["rules"][0]["providers"] = Json::array({"offline/saved-provider"});
    privacy["rules"][0]["infer"] = true;
    privacy["rules"][0]["explicit_only"] = false;
    auto allow = profile_action("override", "synthetic/model-policy");
    allow["target"] = "layers"; allow["key"] = "onboarding.privacy"; allow["value"] = privacy;
    state = apply(store, state, allow);
    const auto request = unwrap(store.model_request(kSyntheticUser, "offline/saved-provider"));
    REQUIRE(request["method_profile"]["bindings"].size() == 3);
    Json binding;
    for (const auto& possible : request["method_profile"]["bindings"])
      if (possible["method_version_id"] == request["method_ref"]["version_id"]) binding = possible;
    REQUIRE(binding.is_object());
    CHECK(request["method_ref"]["version_id"] == binding["method_version_id"]);
    CHECK(request["method_ref"]["recipe_hash"] == binding["recipe_sha256"]);
    const auto reply = saved_model_reply(request, "synthetic/model-work", "work.projects", "Synthetic inferred work project");
    state = apply(store, state, saved_reply_action(reply));
    REQUIRE(state["profile"]["method_executions"].size() == 1);
    const auto execution = state["profile"]["method_executions"][0];
    CHECK(execution["request_token"] == request["request_token"]);
    CHECK(execution["provider"] == request["provider"]);
    CHECK(execution["response_sha256"] == Sha256::hex(json::canonical(reply)));
    CHECK(execution["measurement_status"] == "unavailable");
    CHECK(execution["method_version_id"] == binding["method_version_id"]);
    CHECK(state["profile"]["candidates"]["synthetic/model-work"]["provenance"] == "model_inferred");
    CHECK(state["profile"]["candidates"]["synthetic/model-work"]["method_run_id"] == execution["run_id"]);
    CHECK(state["profile"]["fields"]["work.projects"]["status"] == "unknown");
    kb::KnowledgeStore knowledge(*db);
    for (const char* predicate : {"produced_by_method_version", "produced_in_run"}) {
      kb::ClaimQuery query;
      query.predicate = state["pack"]["vocabulary"]["predicates"][predicate].get<std::string>();
      const auto claims = unwrap(knowledge.query_claims(state["graph_run_id"].get<std::string>(), query));
      REQUIRE(claims.size() == 1);
      CHECK(claims.front().object == (std::string_view(predicate) == "produced_in_run"
          ? execution["run_id"].get<std::string>() : binding["method_version_id"].get<std::string>()));
    }
    auto confirm = profile_action("review", "synthetic/confirm-model-work");
    confirm["candidate"] = "synthetic/model-work"; confirm["decision"] = "confirmed";
    state = apply(store, state, confirm);
    CHECK(state["profile"]["fields"]["work.projects"]["provenance"] == "model_inferred");
    CHECK(state["profile"]["fields"]["work.projects"]["review"] == "confirmed");
    for (const char* predicate : {"produced_by_method_version", "produced_in_run"}) {
      kb::ClaimQuery query;
      query.predicate = state["pack"]["vocabulary"]["predicates"][predicate].get<std::string>();
      const auto claims = unwrap(knowledge.query_claims(state["graph_run_id"].get<std::string>(), query));
      REQUIRE(claims.size() == 1);
      CHECK(claims.front().object == (std::string_view(predicate) == "produced_in_run"
          ? execution["run_id"].get<std::string>() : binding["method_version_id"].get<std::string>()));
    }
    const auto body_before_stale = persisted_body(*db);
    auto stale = store.apply(kSyntheticUser, state["revision"].get<std::int64_t>(), saved_reply_action(reply));
    REQUIRE_FALSE(stale);
    CHECK(stale.error().code == Errc::Conflict);
    CHECK(persisted_body(*db) == body_before_stale);
    db.reset();
    auto restarted_db = open_db(temp.path() / "saved-model-run.db", offline_options());
    OnboardingStore restarted(*restarted_db);
    CHECK(unwrap(restarted.open(kSyntheticUser)) == state);
    CHECK(persisted_body(*restarted_db) == body_before_stale);
  }

  TEST_CASE("optional imported extraction proposal retains exact executed method and never auto-confirms") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "imported-extraction.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    auto privacy = state["profile"]["privacy"];
    privacy["rules"][0]["infer"] = true;
    privacy["rules"][0]["explicit_only"] = false;
    auto allow = profile_action("override", "synthetic/import-policy");
    allow["target"] = "layers"; allow["key"] = "onboarding.privacy";
    allow["value"] = privacy;
    state = apply(store, state, allow);
    const auto graph = unwrap(project_graph(state["pack"], state["scenario_definition"], state["profile"], state["layers"], kSyntheticUser));
    const auto version = graph["method_profile"]["bindings"][0]["method_version_id"].get<std::string>();
    auto proposal = profile_action("propose", "synthetic/import-result", "work.projects");
    proposal["value"] = "Synthetic project extracted from saved archive";
    proposal["source_refs"] = Json::array({"synthetic/archive/record-1"});
    proposal["method_execution"] = Json{{"run_id", "e_synthetic_archive_run"}, {"method_profile", graph["method_profile"]},
        {"method_version_id", version}, {"request_token", "synthetic/archive-extraction-request"},
        {"response_sha256", Sha256::hex("synthetic saved extraction result")}, {"provider", "offline/archive-fixture"},
        {"known_at", "2000-01-01T00:00:00Z"}};
    state = apply(store, state, proposal);
    CHECK(state["profile"]["fields"]["work.projects"]["status"] == "unknown");
    CHECK(state["profile"]["candidates"]["synthetic/import-result"]["provenance"] == "model_inferred");
    CHECK(state["profile"]["candidates"]["synthetic/import-result"]["review"] == "pending");
    CHECK_FALSE(state["profile"]["candidates"]["synthetic/import-result"].contains("method_execution"));
    kb::KnowledgeStore knowledge(*db);
    kb::ClaimQuery query;
    query.predicate = state["pack"]["vocabulary"]["predicates"]["produced_by_method_version"].get<std::string>();
    const auto claims = unwrap(knowledge.query_claims(state["graph_run_id"].get<std::string>(), query));
    REQUIRE(claims.size() == 1);
    CHECK(claims[0].object == version);
    const auto body = persisted_body(*db);
    auto invalid_receipt = proposal;
    invalid_receipt["id"] = "synthetic/invalid-import";
    invalid_receipt["method_execution"]["response_sha256"] = "invalid";
    CHECK_FALSE(store.apply(kSyntheticUser, state["revision"].get<std::int64_t>(), invalid_receipt));
    CHECK(persisted_body(*db) == body);
  }

  TEST_CASE("exact privacy replacement omits old specific rules and applies metadata retention to every historical copy") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "exact-retention.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    auto old_privacy = state["profile"]["privacy"];
    old_privacy["rules"][0]["providers"] = Json::array({"offline/saved-provider"});
    old_privacy["rules"][0]["infer"] = true;
    old_privacy["rules"][0]["explicit_only"] = false;
    auto specific = old_privacy["rules"][0];
    specific["id"] = "synthetic/communication-specific"; specific["category"] = "communication";
    old_privacy["rules"].push_back(specific);
    auto initial_pack = state["pack"];
    replace_pack_privacy(initial_pack, old_privacy);
    state = unwrap(store.update_pack(kSyntheticUser, state["revision"].get<std::int64_t>(), initial_pack,
                                    state["scenario_definition"]));
    CHECK_FALSE(state["layers"]["overrides"].contains("onboarding.privacy"));
    const std::string raw_extra = "synthetic-old-historical-extra-2ba9";
    auto answer = profile_action("answer", "synthetic/old-style", "communication.style");
    answer["value"] = "Synthetic current explicit style"; answer["private_extra"] = raw_extra;
    state = apply(store, state, answer);
    auto confirm = profile_action("review", "synthetic/confirm-old-style");
    confirm["candidate"] = "synthetic/old-style"; confirm["decision"] = "confirmed";
    state = apply(store, state, confirm);
    auto old_layer = profile_action("override", "synthetic/old-layer-value");
    old_layer["target"] = "layers"; old_layer["key"] = "preference.length";
    old_layer["value"] = "Synthetic historical default override"; old_layer["private_extra"] = raw_extra;
    state = apply(store, state, old_layer);
    state = apply(store, state, Json{{"target", "layers"}, {"op", "clear_override"}, {"key", "preference.length"}});
    auto repeat = profile_action("repeat", "synthetic/communication-summary"); repeat["section"] = "communication";
    state = apply(store, state, repeat);
    auto request = unwrap(store.model_request(kSyntheticUser, "offline/saved-provider"));
    auto reply = saved_model_reply(request, "synthetic/old-language", "communication.language", "Synthetic current inferred language", raw_extra);
    reply["candidates"][0]["private_extra"] = raw_extra;
    state = apply(store, state, saved_reply_action(reply));
    confirm = profile_action("review", "synthetic/confirm-old-language");
    confirm["candidate"] = "synthetic/old-language"; confirm["decision"] = "confirmed";
    state = apply(store, state, confirm);
    CHECK(json::dump(state["profile"]["history"]).find(raw_extra) != std::string::npos);
    CHECK(json::dump(state["profile"]["candidates"]).find(raw_extra) != std::string::npos);
    CHECK(json::dump(state["profile"]["session"]).find(raw_extra) != std::string::npos);
    CHECK(json::dump(state["layers"]["history"]).find(raw_extra) != std::string::npos);
    auto new_privacy = old_privacy;
    new_privacy["rules"] = Json::array({old_privacy["rules"][0]});
    new_privacy["rules"][0]["retention"]["history"] = "metadata";
    new_privacy["rules"][0]["retention"]["metadata_keys"] = Json::array();

    SUBCASE("direct privacy layer override") {
      auto override = profile_action("override", "synthetic/replace-policy");
      override["target"] = "layers"; override["key"] = "onboarding.privacy"; override["value"] = new_privacy;
      state = apply(store, state, override);
      CHECK(layer_resolution(state, "onboarding.privacy")["layer"] == "user");
    }
    SUBCASE("installed pack upgrade with untouched builtin privacy layer") {
      CHECK_FALSE(state["layers"]["overrides"].contains("onboarding.privacy"));
      auto upgraded = state["pack"];
      replace_pack_privacy(upgraded, new_privacy);
      state = unwrap(store.update_pack(kSyntheticUser, state["revision"].get<std::int64_t>(), upgraded,
                                      state["scenario_definition"]));
      CHECK(layer_resolution(state, "onboarding.privacy")["layer"] == "builtin");
    }
    CHECK(state["profile"]["privacy"] == new_privacy);
    REQUIRE(state["profile"]["privacy"]["rules"].size() == 1);
    CHECK(state["profile"]["privacy"]["rules"][0]["category"] == "*");
    CHECK(state["profile"]["fields"]["communication.style"]["value"] == "Synthetic current explicit style");
    CHECK(state["profile"]["fields"]["communication.language"]["value"] == "Synthetic current inferred language");
    for (const char* copy : {"history", "candidates", "session"}) {
      INFO(copy);
      CHECK(json::dump(state["profile"][copy]).find(raw_extra) == std::string::npos);
    }
    CHECK(json::dump(state["layers"]["history"]).find(raw_extra) == std::string::npos);
    CHECK(persisted_body(*db).find(raw_extra) == std::string::npos);
    CHECK(unwrap(store.read(kSyntheticUser)) == state);
  }

  TEST_CASE("invalid settings layer mode is rejected atomically and prepared recipe identity matches its actual binding") {
    fsutil::TempDir temp;
    auto db = open_db(temp.path() / "invalid-settings.db", offline_options());
    OnboardingStore store(*db);
    const auto state = unwrap(store.open(kSyntheticUser));
    const auto body_before = persisted_body(*db);
    auto rejected = store.apply(kSyntheticUser, state["revision"].get<std::int64_t>(),
        Json{{"target", "layers"}, {"op", "override"}, {"key", "onboarding.settings"},
             {"value", {{"preference_mode", "invented-invalid-mode"}}}});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    CHECK(persisted_body(*db) == body_before);
    CHECK(unwrap(store.read(kSyntheticUser)) == state);
    CHECK(unwrap(db->conn().query_int("SELECT revision FROM loom_onboarding_profiles WHERE user_id=?", kSyntheticUser)) ==
          std::optional<std::int64_t>(state["revision"].get<std::int64_t>()));
    auto request = unwrap(store.model_request(kSyntheticUser, "offline/saved-provider"));
    REQUIRE(request["method_profile"]["bindings"].size() == 1);
    CHECK(request["method_ref"]["recipe_hash"] == request["method_profile"]["bindings"][0]["recipe_sha256"]);
    CHECK(request["method_ref"]["prompt_hash"] == request["method_profile"]["bindings"][0]["prompt_sha256"]);
    const auto changed = apply(store, state, Json{{"target", "layers"}, {"op", "override"}, {"key", "prompt.onboarding"},
                                                 {"value", "Synthetic updated exact prompt"}});
    auto edited = unwrap(store.model_request(kSyntheticUser, "offline/saved-provider"));
    CHECK(edited["method_ref"]["recipe_hash"] == edited["method_profile"]["bindings"][0]["recipe_sha256"]);
    CHECK(edited["method_ref"]["recipe_hash"] != request["method_ref"]["recipe_hash"]);
    CHECK(edited["snapshot_revision"] == changed["revision"]);
  }

  TEST_CASE("future native schemas are refused without downgrading or overwriting user data") {
    fsutil::TempDir temp;
    auto future_db = open_db(temp.path() / "future-meta.db", offline_options());
    LOOM_REQUIRE_OK(future_db->conn().exec("CREATE TABLE loom_onboarding_meta (key TEXT PRIMARY KEY,value TEXT NOT NULL);"
                                         "INSERT INTO loom_onboarding_meta VALUES ('schema_version','99')"));
    OnboardingStore future_store(*future_db);
    auto unopened = future_store.open(kSyntheticUser);
    REQUIRE_FALSE(unopened);
    CHECK(unopened.error().code == Errc::Unsupported);
    CHECK(unwrap(future_db->conn().query_text("SELECT value FROM loom_onboarding_meta WHERE key='schema_version'")) ==
          std::optional<std::string>("99"));
    CHECK_FALSE(future_db->conn().has_table("loom_onboarding_profiles"));

    auto changed_db = open_db(temp.path() / "future-meta-after-open.db", offline_options());
    OnboardingStore changed_store(*changed_db);
    auto established = unwrap(changed_store.open(kSyntheticUser));
    const auto established_body = persisted_body(*changed_db);
    LOOM_REQUIRE_OK(changed_db->conn().run("UPDATE loom_onboarding_meta SET value='99' WHERE key='schema_version'"));
    auto rejected_change = changed_store.apply(kSyntheticUser, established["revision"].get<std::int64_t>(),
                                              profile_action("pause", "synthetic/unsupported-schema"));
    REQUIRE_FALSE(rejected_change);
    CHECK(rejected_change.error().code == Errc::Unsupported);
    auto rejected_upgrade = changed_store.update_pack(kSyntheticUser, established["revision"].get<std::int64_t>(),
                                                    established["pack"], established["scenario_definition"]);
    REQUIRE_FALSE(rejected_upgrade);
    CHECK(rejected_upgrade.error().code == Errc::Unsupported);
    CHECK(persisted_body(*changed_db) == established_body);

    auto db = open_db(temp.path() / "future-profile.db", offline_options());
    OnboardingStore store(*db);
    auto state = unwrap(store.open(kSyntheticUser));
    auto future = unwrap(json::parse(persisted_body(*db)));
    future["schema"] = "loom.onboarding_store/99";
    future["future_extension"] = Json{{"preserve", "synthetic future user data"}};
    const auto original = json::dump(future);
    LOOM_REQUIRE_OK(db->conn().run("UPDATE loom_onboarding_profiles SET body=? WHERE user_id=?", original, kSyntheticUser));
    auto failed_read = store.read(kSyntheticUser);
    REQUIRE_FALSE(failed_read);
    CHECK(failed_read.error().code == Errc::Unsupported);
    CHECK_FALSE(store.open(kSyntheticUser));
    CHECK_FALSE(store.apply(kSyntheticUser, state["revision"].get<std::int64_t>(), profile_action("pause", "synthetic/future-pause")));
    CHECK(persisted_body(*db) == original);
  }
}
