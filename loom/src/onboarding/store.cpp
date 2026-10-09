#include "loom/onboarding_store.h"

#include <limits>

#include "loom/knowledge_store.h"
#include "loom/onboarding.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_presentation.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "runtime_adapter.h"

namespace loom::onboarding {
namespace {
#include "builtin.inc"

Error invalid(std::string message) { return {Errc::InvalidArgument, std::move(message)}; }

Result<Json> interview_binding(const Json& registry, const Json& scenario) {
  Json selected = nullptr;
  const auto& descriptor = scenario.at("graph_method");
  Json recipe = descriptor.at("recipe");
  recipe["prompt_sha256"] = Sha256::hex(scenario.at("prompt").get<std::string>());
  recipe["parameters"] = descriptor.at("parameters");
  const auto recipe_hash = Sha256::hex(json::canonical(recipe));
  const auto parameter_hash = Sha256::hex(json::canonical(Json{{"effective_parameters", descriptor.at("parameters")},
      {"user_overrides", descriptor.value("user_overrides", Json::object())}}));
  for (const auto& binding : registry.at("bindings")) {
    if (binding.at("definition_id") != scenario.at("graph_method").at("id")) continue;
    if (binding.at("prompt_sha256") != Sha256::hex(scenario.at("prompt").get<std::string>())) continue;
    if (binding.at("recipe_sha256") != recipe_hash || binding.at("parameter_set_sha256") != parameter_hash ||
        binding.at("revision") != descriptor.at("revision") || binding.at("execution_capability") != descriptor.at("execution_capability")) continue;
    if (!selected.is_null() && selected.at("method_version_id") != binding.at("method_version_id"))
      return Error(Errc::Conflict, "scenario resolves to conflicting method versions");
    selected = binding;
  }
  if (selected.is_null()) return Error(Errc::Unavailable, "interview method version is unavailable");
  return selected;
}

Result<Json> effective_scenario(const Json& pack, const Json& original, const DefaultLayers& layers,
                                bool require_method) {
  Json scenario = original;
  auto effective_value = [&](const Json& value) -> Result<Json> {
    if (value.is_object() && value.value("schema", "") == "loom.default_reference/1") {
      if (value.value("source", "") != "scenario") return invalid("unknown default reference source");
      return original.at(Json::json_pointer(value.at("pointer").get<std::string>()));
    }
    return value;
  };
  const auto& method = original.at("graph_method");
  if (method.contains("default_key")) {
    LOOM_TRY_ASSIGN(auto resolution, layers.resolve(method.at("default_key").get<std::string>()));
    if (resolution.at("status") == "effective") {
      LOOM_TRY_ASSIGN(scenario["graph_method"], effective_value(resolution.at("value")));
      scenario["graph_method"]["default_key"] = method.at("default_key");
    } else if (require_method) return Error(Errc::Unavailable, "onboarding interview method is disabled or excluded");
  }
  for (const auto& binding : pack.at("scenario_bindings").items()) {
    LOOM_TRY_ASSIGN(auto resolution, layers.resolve(binding.value().get<std::string>()));
    if (resolution.at("status") != "effective") {
      if (require_method) return Error(Errc::Unavailable, "onboarding prompt is disabled, excluded or awaiting acceptance");
      continue;
    }
    LOOM_TRY_ASSIGN(auto value, effective_value(resolution.at("value")));
    scenario[Json::json_pointer(binding.key())] = std::move(value);
  }
  for (const auto& binding : pack.at("scenario_copy_bindings").items())
    scenario[Json::json_pointer(binding.key())] = scenario.at(Json::json_pointer(binding.value().get<std::string>()));
  // Method identities/hashes are derived from the effective graph descriptor,
  // never the original display alias's stale hash after a prompt override.
  scenario["method_ref"]["recipe_hash"] = Sha256::hex(json::canonical(scenario.at("graph_method")));
  return scenario;
}

Result<Json> defaults_for(const Json& pack, const DefaultLayers& layers) {
  Json defaults = Json::object();
  for (const auto& binding : pack.at("runtime_bindings").items()) {
    LOOM_TRY_ASSIGN(auto effective, layers.resolve(binding.value().get<std::string>()));
    if (effective.at("status") != "effective")
      return Error(Errc::Unavailable, "required onboarding operation setting is disabled or excluded: " + binding.key());
    defaults[Json::json_pointer(binding.key())] = effective.at("value");
  }
  // Use W11's validator whenever its foundation is installed. There is no
  // second schema interpreter; without it only ProfileSession's operational
  // contract is available, and inspection reports that dependency honestly.
  auto validated = runtime_profile_values(pack.at("runtime_definition"), layers, pack.at("runtime_bindings"));
  if (validated) return validated->at("values");
  if (validated.error().code != Errc::Unavailable) return validated.error();
  return defaults;
}

Result<Json> decorate(Json state) {
  LOOM_TRY_ASSIGN(auto layers, DefaultLayers::create(state.at("pack"), state.at("layers")));
  state["effectiveDefaults"] = Json::array();
  for (const auto& entry : state.at("pack").at("entries")) {
    LOOM_TRY_ASSIGN(auto resolved, layers.resolve(entry.at("key").get<std::string>()));
    state["effectiveDefaults"].push_back(std::move(resolved));
  }
  auto validated = runtime_profile_values(state.at("pack").at("runtime_definition"), layers,
                                         state.at("pack").at("runtime_bindings"));
  if (validated) state["runtime_profile"] = validated.value();
  else if (validated.error().code == Errc::Unavailable)
    state["runtime_profile"] = {{"available", false}, {"reason", validated.error().message}};
  else state["runtime_profile"] = {{"available", false}, {"error", {{"code", errc_name(validated.error().code)}, {"message", validated.error().message}}}};
  state["scenario"] = state.at("scenario_definition");
  state["scenario"]["source"] = json::dump(state.at("scenario_definition"), 2);
  // UI bridge receives the same native state, with no separate frontend store.
  for (const char* key : {"fields", "privacy", "settings", "session", "candidates", "history", "graph"})
    if (state.at("profile").contains(key)) state[key] = state.at("profile").at(key);
  LOOM_TRY_ASSIGN(auto privacy_layer, layers.resolve("onboarding.privacy"));
  state["effective_privacy"] = {{"available", privacy_layer.at("status") == "effective"}, {"resolution", privacy_layer}};
  // Presentation is a separate graph default, never inferred from a personal
  // language preference. Suppression does not restore a hidden UI preset.
  if (state.at("pack").contains("presentation_key")) {
    LOOM_TRY_ASSIGN(auto presentation, layers.resolve(state.at("pack").at("presentation_key").get<std::string>()));
    const bool available = presentation.at("status") == "effective";
    state["presentation"] = Json{{"available", available}, {"status", presentation.at("status")},
        {"resolution", presentation}};
    if (available) state["presentation"]["value"] = presentation.at("value");
  }
  return state;
}

Result<Json> version_checked(Json state, std::int64_t expected) {
  if (expected < 0 || state.at("revision") != expected)
    return Error(Errc::Conflict, "onboarding snapshot revision changed; reload before editing");
  if (expected == std::numeric_limits<std::int64_t>::max())
    return Error(Errc::Conflict, "onboarding revision cannot be represented");
  state["revision"] = expected + 1;
  return state;
}

Result<Json> apply_operational_defaults(const Json& scenario, const Json& profile, const Json& defaults,
                                        std::string_view event, std::string_view when) {
  Json copy = profile;
  // Exact effective settings, validated before they become operative. Existing
  // explicit legacy choices have already been recorded in the layer resolver.
  copy["settings"] = defaults.at("settings");
  copy["privacy"] = defaults.at("privacy");
  LOOM_TRY_ASSIGN(auto session, ProfileSession::create(scenario, defaults, copy));
  for (const auto& rule : defaults.at("privacy").at("rules")) {
    LOOM_TRY(session.dispatch(Json{{"op", "privacy"}, {"rule", rule},
        {"id", std::string(event) + "/" + rule.at("id").get<std::string>()}, {"time", when}}));
  }
  copy = session.snapshot(); copy["privacy"] = defaults.at("privacy");
  LOOM_TRY_ASSIGN(auto checked, ProfileSession::create(scenario, defaults, copy));
  return checked.snapshot();
}

// Preference fields have data-defined keys. Committed explicit values use
// the same resolver as methods, prompts, profile presets and privacy rules.
Result<Json> sync_preferences(const Json& scenario, const Json& before, const Json& after,
                              const Json& pack, Json layers_state, const Json& action) {
  if (!scenario.contains("fields")) return layers_state;
  const auto& descriptors = scenario.at("fields");
  auto visit = [&](const Json& descriptor) -> Status {
    if (!descriptor.contains("default_key")) return ok_status();
    const auto field = descriptor.at("id").get<std::string>();
    if (!after.at("fields").contains(field)) return ok_status();
    const auto& next = after.at("fields").at(field);
    if (before.at("fields").contains(field) && before.at("fields").at(field) == next) return ok_status();
    const auto key = descriptor.at("default_key").get<std::string>();
    if (!layers_state.contains("profile_sources")) layers_state["profile_sources"] = Json::object();
    if (next.at("status") != "known") {
      if (!layers_state["profile_sources"].contains(key) || layers_state["profile_sources"].at(key) != field) return ok_status();
      LOOM_TRY_ASSIGN(auto layers, DefaultLayers::create(pack, layers_state));
      LOOM_TRY_ASSIGN(auto updated, layers.dispatch(Json{{"op", "clear_override"}, {"key", key}, {"profile_field", field}}));
      layers_state = std::move(updated); layers_state["profile_sources"].erase(key);
      if (next.at("status") == "never" || action.value("purge_history", false)) {
        for (auto& event : layers_state["history"]) if (event.value("profile_field", "") == field) event.erase("value");
      }
      return ok_status();
    }
    LOOM_TRY_ASSIGN(auto layers, DefaultLayers::create(pack, layers_state));
    LOOM_TRY_ASSIGN(auto updated, layers.dispatch(Json{{"op", "override"},
        {"key", descriptor.at("default_key")}, {"value", next.at("value")}, {"profile_field", field},
        {"provenance", next.at("provenance")}, {"review", next.at("review")},
        {"time", next.value("time", Json(nullptr))}, {"source_refs", next.value("source_refs", Json::array())}}));
    layers_state = std::move(updated);
    layers_state["profile_sources"][key] = field;
    // Profile history is authoritative for these mirrored writes; don't keep
    // a second raw copy that would evade category retention.
    layers_state["history"].back().erase("value");
    return ok_status();
  };
  if (descriptors.is_array()) { for (const auto& descriptor : descriptors) LOOM_TRY(visit(descriptor)); }
  else if (descriptors.is_object()) {
    for (const auto& item : descriptors.items()) {
      Json descriptor = item.value(); descriptor["id"] = item.key(); LOOM_TRY(visit(descriptor));
    }
  }
  return layers_state;
}

Status enforce_layer_retention(Json& state) {
  auto& history = state["layers"]["history"];
  for (const auto& descriptor : state["scenario_definition"]["fields"]) {
    if (!descriptor.contains("default_key")) continue;
    const auto field = descriptor.at("id").get<std::string>();
    const auto key = descriptor.at("default_key").get<std::string>();
    const auto category = descriptor.at("category").get<std::string>();
    LOOM_TRY_ASSIGN(auto decision, privacy_decision(state.at("profile"), Json{{"op", "store"},
        {"category", category}, {"field", field}, {"provenance", "user_stated"}}));
    if (!decision.contains("retention")) continue;
    const auto& retention = decision.at("retention");
    const auto mode = retention.at("history").get<std::string>();
    for (auto it = history.begin(); it != history.end();) {
      if (it->value("key", "") != key) { ++it; continue; }
      if (mode == "none") { it = history.erase(it); continue; }
      if (mode == "metadata") {
        Json metadata = Json::object();
        for (const char* structural : {"op", "key", "profile_field", "category", "time", "provenance", "review"})
          if (it->contains(structural)) metadata[structural] = it->at(structural);
        if (retention.contains("metadata_keys"))
          for (const auto& key_name : retention.at("metadata_keys"))
            if (key_name.is_string() && it->contains(key_name.get<std::string>())) metadata[key_name.get<std::string>()] = it->at(key_name.get<std::string>());
        *it = std::move(metadata);
      }
      ++it;
    }
    if (!retention.at("max_events").is_null()) {
      const auto limit = retention.at("max_events").get<std::uint64_t>();
      std::uint64_t used = 0;
      for (const auto& event : state.at("profile").at("history")) if (event.value("category", "") == category) ++used;
      std::uint64_t own = 0;
      for (const auto& event : history) if (event.value("key", "") == key) ++own;
      const auto available = limit > used ? limit - used : 0;
      for (auto it = history.begin(); own > available && it != history.end();) {
        if (it->value("key", "") == key) { it = history.erase(it); --own; } else ++it;
      }
    }
  }
  return ok_status();
}
}  // namespace

Result<Json> builtin_pack() { return json::parse(kOnboardingPack); }
Result<Json> builtin_scenario() { return json::parse(kOnboardingScenario); }
Result<Json> builtin_presentation() {
  LOOM_TRY_ASSIGN(auto catalog, json::parse(kOnboardingPresentation));
  LOOM_TRY(validate_presentation(catalog));
  return catalog;
}

Status OnboardingStore::ensure_schema() {
  auto lock = db_.lock();
  sql::Txn txn(db_.conn()); LOOM_TRY(txn.begin_status());
  LOOM_TRY(db_.conn().exec("CREATE TABLE IF NOT EXISTS loom_onboarding_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"));
  LOOM_TRY_ASSIGN(auto version, db_.conn().query_text("SELECT value FROM loom_onboarding_meta WHERE key='schema_version'"));
  if (version && *version != "1") return Error(Errc::Unsupported, "newer onboarding schema is not downgraded");
  LOOM_TRY(db_.conn().exec("CREATE TABLE IF NOT EXISTS loom_onboarding_profiles (user_id TEXT PRIMARY KEY, revision INTEGER NOT NULL, body TEXT NOT NULL)"));
  LOOM_TRY(db_.conn().run("INSERT OR IGNORE INTO loom_onboarding_meta (key,value) VALUES ('schema_version','1')"));
  return txn.commit();
}

Result<Json> OnboardingStore::read_locked(std::string_view user) {
  if (user.empty()) return invalid("onboarding user identity is required");
  if (!db_.conn().has_table("loom_onboarding_profiles")) return Error(Errc::NotFound, "onboarding profile not initialized");
  LOOM_TRY_ASSIGN(auto version, db_.conn().query_text("SELECT value FROM loom_onboarding_meta WHERE key='schema_version'"));
  if (!version || *version != "1") return Error(Errc::Unsupported, "onboarding schema version is unsupported; no downgrade performed");
  LOOM_TRY_ASSIGN(auto raw, db_.conn().query_text("SELECT body FROM loom_onboarding_profiles WHERE user_id=?", user));
  if (!raw) return Error(Errc::NotFound, "onboarding profile not initialized");
  LOOM_TRY_ASSIGN(auto state, json::parse(*raw));
  if (state.value("schema", "") != "loom.onboarding_store/1") return Error(Errc::Unsupported, "newer onboarding profile is not downgraded");
  return state;
}

Result<Json> OnboardingStore::read(std::string_view user) {
  auto lock = db_.lock();
  LOOM_TRY_ASSIGN(auto state, read_locked(user));
  return decorate(std::move(state));
}

Status OnboardingStore::save_locked(const Json& state) {
  LOOM_TRY_ASSIGN(auto layers, DefaultLayers::create(state.at("pack"), state.at("layers")));
  LOOM_TRY_ASSIGN(auto scenario, effective_scenario(state.at("pack"), state.at("scenario_definition"), layers, false));
  LOOM_TRY_ASSIGN(auto graph, project_graph(state.at("pack"), scenario,
      state.at("profile"), state.at("layers"), state.at("user_id").get<std::string>()));
  kb::KnowledgeStore knowledge(db_);
  // Stable namespace, independent of pack revision: otherwise upgrades would
  // retain stale private snapshots under old runs, bypassing retention/deletion.
  LOOM_TRY_ASSIGN(auto run, knowledge.begin_run(Sha256::hex("loom.onboarding/1"),
      Json{{"onboarding_user", state.at("user_id")}}));
  LOOM_TRY(knowledge.clear_run(run.id));
  std::vector<model::Observation> observations;
  std::vector<model::Entity> entities;
  std::vector<model::Claim> claims;
  for (const auto& value : graph.at("observations")) { LOOM_TRY_ASSIGN(auto row, model::Observation::from_json(value)); observations.push_back(std::move(row)); }
  for (const auto& value : graph.at("entities")) { LOOM_TRY_ASSIGN(auto row, model::Entity::from_json(value)); entities.push_back(std::move(row)); }
  for (const auto& value : graph.at("claims")) { LOOM_TRY_ASSIGN(auto row, model::Claim::from_json(value)); claims.push_back(std::move(row)); }
  LOOM_TRY(knowledge.put_observations(run.id, observations));
  LOOM_TRY(knowledge.put_entities(run.id, entities));
  LOOM_TRY(knowledge.put_claims(run.id, claims));
  LOOM_TRY(knowledge.finish_run(run.id, "done", {{"onboarding", true}, {"revision", state.at("revision")}}));
  Json body = state; body["graph_run_id"] = run.id;
  LOOM_TRY(db_.conn().run("INSERT INTO loom_onboarding_profiles (user_id,revision,body) VALUES (?,?,?) ON CONFLICT(user_id) DO UPDATE SET revision=excluded.revision,body=excluded.body",
      state.at("user_id").get<std::string>(), state.at("revision").get<std::int64_t>(), json::dump(body)));
  return ok_status();
}

Result<Json> OnboardingStore::open(std::string_view user, const Json& legacy) {
  try {
    if (user.empty()) return invalid("onboarding user identity is required");
    auto lock = db_.lock(); LOOM_TRY(ensure_schema());
    sql::Txn txn(db_.conn()); LOOM_TRY(txn.begin_status());
    auto existing = read_locked(user);
    if (existing) return decorate(std::move(existing).value());
    if (existing.error().code != Errc::NotFound) return existing.error();
    LOOM_TRY_ASSIGN(auto pack, builtin_pack());
    LOOM_TRY_ASSIGN(auto scenario, builtin_scenario());
    LOOM_TRY_ASSIGN(auto layers, DefaultLayers::create(pack));
    // Legacy user choices are explicit overlays, not inherited presets. They
    // survive future pack updates without preserving stale builtin defaults.
    for (const char* property : {"settings", "privacy"}) {
      if (!legacy.contains(property)) continue;
      LOOM_TRY_ASSIGN(auto updated, layers.dispatch(Json{{"op", "override"},
          {"key", std::string("onboarding.") + property}, {"value", legacy.at(property)}}));
      LOOM_TRY_ASSIGN(layers, DefaultLayers::create(pack, updated));
    }
    LOOM_TRY_ASSIGN(auto defaults, defaults_for(pack, layers));
    LOOM_TRY_ASSIGN(auto profile, ProfileSession::create(scenario, defaults, legacy));
    LOOM_TRY_ASSIGN(auto layer_state, sync_preferences(scenario, Json{{"fields", Json::object()}}, profile.snapshot(), pack, layers.snapshot(), Json::object()));
    Json state{{"schema", "loom.onboarding_store/1"}, {"user_id", user}, {"revision", 0},
               {"pack", pack}, {"scenario_definition", scenario}, {"layers", layer_state}, {"profile", profile.snapshot()}};
    LOOM_TRY(enforce_layer_retention(state));
    LOOM_TRY(save_locked(state)); LOOM_TRY(txn.commit());
    LOOM_TRY_ASSIGN(auto saved, read_locked(user)); return decorate(std::move(saved));
  } catch (const std::exception& e) { return invalid(std::string("onboarding open: ") + e.what()); }
}

Result<Json> OnboardingStore::apply(std::string_view user, std::int64_t expected_revision, const Json& action) {
  try {
    auto lock = db_.lock(); sql::Txn txn(db_.conn()); LOOM_TRY(txn.begin_status());
    LOOM_TRY_ASSIGN(auto prior, read_locked(user));
    LOOM_TRY_ASSIGN(auto state, version_checked(prior, expected_revision));
    const auto target = action.value("target", "profile");
    LOOM_TRY_ASSIGN(auto layers, DefaultLayers::create(state.at("pack"), state.at("layers")));
    if (target == "layers") {
      LOOM_TRY_ASSIGN(state["layers"], layers.dispatch(action));
      const auto op = action.value("op", "");
      if ((op == "override" || op == "clear_override" || op == "exclude") && action.contains("key") && state["layers"].contains("profile_sources"))
        state["layers"]["profile_sources"].erase(action.at("key").get<std::string>());
      LOOM_TRY_ASSIGN(auto changed, DefaultLayers::create(state.at("pack"), state.at("layers")));
      auto defaults = defaults_for(state.at("pack"), changed);
      if (defaults && (state["profile"]["settings"] != defaults->at("settings") || state["profile"]["privacy"] != defaults->at("privacy"))) {
        LOOM_TRY_ASSIGN(state["profile"], apply_operational_defaults(state.at("scenario_definition"), state.at("profile"), defaults.value(),
            "layer-policy/" + std::to_string(expected_revision + 1), action.value("time", timeutil::utc_now_iso())));
      } else if (!defaults && defaults.error().code != Errc::Unavailable) {
        return defaults.error();
      }
    } else {
      LOOM_TRY_ASSIGN(auto defaults, defaults_for(state.at("pack"), layers));
      LOOM_TRY_ASSIGN(auto scenario, effective_scenario(state.at("pack"), state.at("scenario_definition"), layers, target == "model_reply"));
      LOOM_TRY_ASSIGN(auto profile, ProfileSession::create(scenario, defaults, state.at("profile")));
      if (target == "model_reply") {
        LOOM_TRY_ASSIGN(auto graph, project_graph(state.at("pack"), scenario, state.at("profile"), state.at("layers"), user));
        const auto& method_profile = graph.at("method_profile");
        LOOM_TRY_ASSIGN(auto binding, interview_binding(method_profile, scenario));
        Json reply = action.at("reply");
        const auto response_hash = Sha256::hex(json::canonical(reply));
        const auto run_id = model::Entity::make_id(state.at("pack").at("vocabulary").at("kinds").at("run").get<std::string>(),
            std::string(user) + "/" + reply.at("request_token").get<std::string>() + "/" + response_hash);
        Json results = Json::array();
        for (auto& candidate : reply.at("candidates")) {
          candidate["method_version_id"] = binding.at("method_version_id");
          candidate["method_run_id"] = run_id;
          results.push_back(candidate.at("id"));
        }
        LOOM_TRY_ASSIGN(state["profile"], profile.ingest_model_reply(reply));
        if (!state["profile"].contains("method_executions")) state["profile"]["method_executions"] = Json::array();
        state["profile"]["method_executions"].push_back(Json{{"run_id", run_id},
            {"method_profile", method_profile}, {"method_version_id", binding.at("method_version_id")},
            {"request_token", reply.at("request_token")}, {"response_sha256", response_hash},
            {"response_hash_scope", "canonical_received_reply"}, {"raw_provider_response_sha256", nullptr},
            {"provider", reply.at("provider")}, {"known_at", action.value("time", "")},
            {"measurement_status", "unavailable"}, {"result_candidate_ids", results}});
      }
      else if (target == "profile") {
        Json profile_action = action; profile_action.erase("method_execution");
        LOOM_TRY_ASSIGN(state["profile"], profile.dispatch(profile_action));
        if (action.contains("method_execution")) {
          if (action.value("op", "") != "propose") return invalid("method execution belongs to a proposed extracted result");
          Json execution = action.at("method_execution");
          execution["result_candidate_ids"] = Json::array({action.at("id")});
          if (!state["profile"].contains("method_executions")) state["profile"]["method_executions"] = Json::array();
          state["profile"]["method_executions"].push_back(std::move(execution));
        }
      }
      else return invalid("unknown onboarding action target");
      LOOM_TRY_ASSIGN(state["layers"], sync_preferences(state.at("scenario_definition"), prior.at("profile"), state.at("profile"), state.at("pack"), state.at("layers"), action));
      if (action.value("op", "") == "settings" || action.value("op", "") == "privacy") {
        LOOM_TRY_ASSIGN(auto resolver, DefaultLayers::create(state.at("pack"), state.at("layers")));
        const auto property = action.at("op") == "settings" ? "settings" : "privacy";
        LOOM_TRY_ASSIGN(state["layers"], resolver.dispatch(Json{{"op", "override"}, {"key", std::string("onboarding.") + property}, {"value", state.at("profile").at(property)}}));
      }
    }
    LOOM_TRY(enforce_layer_retention(state));
    LOOM_TRY_ASSIGN(auto preview, decorate(state));
    (void)preview;
    LOOM_TRY(save_locked(state)); LOOM_TRY(txn.commit());
    LOOM_TRY_ASSIGN(auto saved, read_locked(user)); return decorate(std::move(saved));
  } catch (const std::exception& e) { return invalid(std::string("onboarding action: ") + e.what()); }
}

Result<Json> OnboardingStore::update_pack(std::string_view user, std::int64_t expected_revision,
                                         const Json& pack, const Json& scenario) {
  try {
    auto lock = db_.lock(); sql::Txn txn(db_.conn()); LOOM_TRY(txn.begin_status());
    LOOM_TRY_ASSIGN(auto prior, read_locked(user));
    LOOM_TRY_ASSIGN(auto state, version_checked(prior, expected_revision));
    LOOM_TRY_ASSIGN(auto layers, DefaultLayers::create(prior.at("pack"), prior.at("layers")));
    LOOM_TRY_ASSIGN(auto next_layers, layers.update_pack(pack));
    LOOM_TRY_ASSIGN(auto next_resolver, DefaultLayers::create(pack, next_layers));
    LOOM_TRY_ASSIGN(auto defaults, defaults_for(pack, next_resolver));
    LOOM_TRY_ASSIGN(auto migrated, apply_operational_defaults(scenario, prior.at("profile"), defaults,
        "pack-update/" + std::to_string(expected_revision + 1), timeutil::utc_now_iso()));
    state["pack"] = pack; state["scenario_definition"] = scenario;
    state["layers"] = next_layers; state["profile"] = migrated;
    LOOM_TRY(enforce_layer_retention(state));
    LOOM_TRY_ASSIGN(auto preview, decorate(state));
    (void)preview;
    LOOM_TRY(save_locked(state)); LOOM_TRY(txn.commit());
    LOOM_TRY_ASSIGN(auto saved, read_locked(user)); return decorate(std::move(saved));
  } catch (const std::exception& e) { return invalid(std::string("onboarding pack update: ") + e.what()); }
}

Result<Json> OnboardingStore::install_entries(std::string_view user, std::int64_t expected_revision,
                                             const Json& extension) {
  try {
    // Reuse the existing pack validator, including stable identities and duplicates.
    LOOM_TRY_ASSIGN(auto checked, DefaultLayers::create(extension));
    (void)checked;
    LOOM_TRY_ASSIGN(auto prior, read(user));
    if (expected_revision < 0 || prior.at("revision") != expected_revision)
      return Error(Errc::Conflict, "onboarding snapshot revision changed; reload before editing");
    Json pack = prior.at("pack");
    if (pack.contains("entry_packs") && !pack.at("entry_packs").is_array())
      return invalid("native entry pack sources have an unsupported representation");
    for (const auto& entry : extension.at("entries")) {
      bool exists = false;
      for (const auto& current : pack.at("entries")) {
        if (current.at("id") != entry.at("id") && current.at("key") != entry.at("key")) continue;
        if (current.at("id") != entry.at("id") || current.at("key") != entry.at("key") || current.at("area") != entry.at("area"))
          return Error(Errc::Conflict, "default entry identity collides with the native pack");
        exists = true; break;
      }
      // Existing definitions, including unknown fields/numeric types, stay exact.
      if (!exists) pack["entries"].push_back(entry);
    }
    if (pack.at("revision").is_number_unsigned()) {
      const auto revision = pack.at("revision").get<std::uint64_t>();
      if (revision == std::numeric_limits<std::uint64_t>::max()) return invalid("pack revision cannot be incremented");
      pack["revision"] = revision + 1;
    } else {
      const auto revision = pack.at("revision").get<std::int64_t>();
      if (revision == std::numeric_limits<std::int64_t>::max()) return invalid("pack revision cannot be incremented");
      pack["revision"] = revision + 1;
    }
    if (!pack.contains("entry_packs")) pack["entry_packs"] = Json::array();
    pack["entry_packs"].push_back(extension);
    // A concurrent edit after read is rejected by the existing transactional CAS.
    return update_pack(user, expected_revision, pack, prior.at("scenario_definition"));
  } catch (const std::exception& e) { return invalid(std::string("onboarding entry installation: ") + e.what()); }
}

Result<Json> OnboardingStore::model_request(std::string_view user, std::string_view provider) {
  try {
    auto lock = db_.lock();
    LOOM_TRY_ASSIGN(auto state, read_locked(user));
    LOOM_TRY_ASSIGN(auto layers, DefaultLayers::create(state.at("pack"), state.at("layers")));
    LOOM_TRY_ASSIGN(auto defaults, defaults_for(state.at("pack"), layers));
    LOOM_TRY_ASSIGN(auto scenario, effective_scenario(state.at("pack"), state.at("scenario_definition"), layers, true));
    LOOM_TRY_ASSIGN(auto profile, ProfileSession::create(scenario, defaults, state.at("profile")));
    LOOM_TRY_ASSIGN(auto request, profile.model_request(provider));
    LOOM_TRY_ASSIGN(auto graph, project_graph(state.at("pack"), scenario, state.at("profile"), state.at("layers"), user));
    request["method_profile"] = graph.at("method_profile");
    LOOM_TRY_ASSIGN(auto binding, interview_binding(graph.at("method_profile"), scenario));
    request["method_ref"] = {{"method_id", binding.at("method_id")}, {"version_id", binding.at("method_version_id")},
        {"recipe_hash", binding.at("recipe_sha256")}, {"prompt_hash", binding.at("prompt_sha256")}};
    request["snapshot_revision"] = state.at("revision");
    request["graph_run_id"] = state.at("graph_run_id");
    request["calls_authorized"] = false;  // preparation never authorizes a paid call
    return request;
  } catch (const std::exception& e) { return invalid(std::string("onboarding model request: ") + e.what()); }
}

Result<Json> OnboardingStore::policy_decision(std::string_view user, const Json& request) {
  try {
    auto lock = db_.lock();
    LOOM_TRY_ASSIGN(auto state, read_locked(user));
    LOOM_TRY_ASSIGN(auto layers, DefaultLayers::create(state.at("pack"), state.at("layers")));
    LOOM_TRY_ASSIGN(auto policy, layers.resolve("onboarding.privacy"));
    if (policy.at("status") != "effective") return Json{{"allowed", false},
        {"reason", "privacy policy is disabled, excluded or awaiting acceptance"}, {"resolution", policy}};
    LOOM_TRY_ASSIGN(auto result, privacy_decision(state.at("profile"), request));
    result["resolution"] = policy;
    result["snapshot_revision"] = state.at("revision");
    return result;
  } catch (const std::exception& e) { return invalid(std::string("onboarding policy decision: ") + e.what()); }
}
}  // namespace loom::onboarding
