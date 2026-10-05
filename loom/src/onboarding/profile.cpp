#include "loom/onboarding.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include "loom/util/sha256.h"

namespace loom::onboarding {
namespace {
Error invalid(std::string message) { return make_error(Errc::InvalidArgument, std::move(message)); }
bool text(const Json& j, std::string_view key) {
  const auto* v = json::find(j, key);
  return v && v->is_string() && !v->get_ref<const std::string&>().empty();
}
bool one_of(std::string_view v, std::initializer_list<std::string_view> choices) {
  return std::find(choices.begin(), choices.end(), v) != choices.end();
}
bool member(const Json& array, std::string_view value) {
  return array.is_array() && std::any_of(array.begin(), array.end(), [&](const Json& v) {
    return v.is_string() && (v == value || v == "*");
  });
}
const Json* field(const Json& state, std::string_view id) {
  const auto* fields = json::find(state, "fields");
  return fields ? json::find(*fields, id) : nullptr;
}
const Json* rule_for(const Json& state, std::string_view category) {
  const auto* privacy = json::find(state, "privacy");
  const auto* rules = privacy ? json::find(*privacy, "rules") : nullptr;
  if (!rules || !rules->is_array()) return nullptr;
  const Json* wildcard = nullptr;
  for (const auto& rule : *rules) {
    if (json::get_string(rule, "category") == category) return &rule;
    if (json::get_string(rule, "category") == "*") wildcard = &rule;
  }
  return wildcard;
}
Status validate_rule(const Json& rule) {
  if (!rule.is_object() || !text(rule, "id") || !text(rule, "category"))
    return invalid("privacy rule needs id and category");
  for (const char* key : {"store", "infer", "explicit_only"}) {
    const auto* value = json::find(rule, key);
    if (!value || !value->is_boolean()) return invalid(std::string("privacy rule needs boolean ") + key);
  }
  const auto* providers = json::find(rule, "providers");
  if (!providers || !providers->is_array()) return invalid("privacy rule providers must be an array");
  for (const auto& p : *providers) if (!p.is_string()) return invalid("privacy provider must be a string");
  for (const char* key : {"max_detail", "max_sensitivity"}) {
    const auto* value = json::find(rule, key);
    if (!value || (!value->is_null() && (!value->is_number() || value->get<double>() < 0)))
      return invalid(std::string("privacy rule needs nullable nonnegative ") + key);
  }
  const auto* retention = json::find(rule, "retention");
  if (!retention || !retention->is_object() ||
      !one_of(json::get_string(*retention, "history"), {"full", "metadata", "none"}))
    return invalid("privacy rule needs retention.history full, metadata or none");
  const auto* limit = json::find(*retention, "max_events");
  if (!limit || (!limit->is_null() && (!limit->is_number_integer() || limit->get<long long>() < 0)))
    return invalid("retention.max_events must be null or a nonnegative integer");
  if (retention->contains("metadata_keys")) {
    if (!(*retention)["metadata_keys"].is_array()) return invalid("retention.metadata_keys must be an array");
    for (const auto& key : (*retention)["metadata_keys"]) if (!key.is_string()) return invalid("retention metadata key must be a string");
  }
  return {};
}
Json checked_request(const Json& f, std::string_view id, std::string_view op,
                     std::string_view provenance = "") {
  Json request{{"op", op}, {"field", id}, {"category", f.at("category")},
               {"detail", f.at("detail")}, {"sensitivity", f.at("sensitivity")}};
  if (!provenance.empty()) request["provenance"] = provenance;
  return request;
}
Json metadata_only(const Json& payload, const Json& retention) {
  Json result = Json::object();
  // Identity and disposition are the structural record, not personal content.
  // Other metadata retained by choice is declared in the selected rule data.
  for (const char* key : {"id", "op", "field", "category", "time", "provenance", "review"})
    if (payload.contains(key)) result[key] = payload[key];
  const auto* keys = json::find(retention, "metadata_keys");
  if (keys && keys->is_array()) for (const auto& key : *keys) {
    const auto name = key.get<std::string>();
    if (payload.contains(name)) result[name] = payload[name];
  }
  return result;
}
void enforce_history(Json& state, std::string_view category) {
  const auto* rule = rule_for(state, category);
  if (!rule) return;
  const auto& retention = rule->at("retention");
  const auto mode = json::get_string(retention, "history");
  // Reviewed proposals are historical knowledge copies. Current confirmed
  // fields and not-yet-reviewed candidates have their separate live lifetimes.
  std::size_t reviewed = 0;
  for (auto& [id, candidate] : state["candidates"].items()) {
    (void)id;
    if (json::get_string(candidate, "category") != category || json::get_string(candidate, "review") == "pending") continue;
    ++reviewed;
    if (mode != "full") candidate = metadata_only(candidate, retention);
  }
  if (!retention["max_events"].is_null()) {
    const auto limit = retention["max_events"].get<std::size_t>();
    for (auto& [id, candidate] : state["candidates"].items()) {
      (void)id;
      if (reviewed <= limit) break;
      if (json::get_string(candidate, "category") == category && json::get_string(candidate, "review") != "pending") {
        candidate = metadata_only(candidate, retention); --reviewed;
      }
    }
  }
  auto& history = state["history"];
  if (mode == "none") {
    for (auto it = history.begin(); it != history.end();) {
      if (json::get_string(*it, "category") == category) it = history.erase(it);
      else ++it;
    }
    return;
  }
  std::size_t count = 0;
  for (auto& event : history) if (json::get_string(event, "category") == category) {
    ++count;
    if (mode == "metadata") event = metadata_only(event, retention);
  }
  if (retention["max_events"].is_null()) return;
  const auto limit = retention["max_events"].get<std::size_t>();
  for (auto it = history.begin(); count > limit && it != history.end();) {
    if (json::get_string(*it, "category") == category) { it = history.erase(it); --count; }
    else ++it;
  }
}
void record(Json& state, const Json& action, std::string_view category,
            const Json& before, const Json& after) {
  Json event = action;
  event["category"] = category;
  event["before"] = before;
  event["after"] = after;
  state["history"].push_back(std::move(event));
  enforce_history(state, category);
}
Status event_identity(const Json& action) {
  if (!text(action, "id") || !text(action, "time")) return invalid("action needs caller-supplied id and time");
  if (action.contains("source_refs") && !action["source_refs"].is_array()) return invalid("source_refs must be an array");
  return {};
}
bool cache_matches(const Json& state, const Json& cache, std::string_view category) {
  if (category.empty() || category == "*") return true;
  const auto* fields = json::find(cache, "summary_fields");
  if (!fields || !fields->is_array()) return true;  // legacy prose has no safe attribution
  for (const auto& id : *fields) {
    if (!id.is_string()) continue;
    const auto* f = field(state, id.get_ref<const std::string&>());
    if (f && json::get_string(*f, "category") == category) return true;
  }
  return false;
}
void redact_summaries(Json& state, std::string_view category = "") {
  auto& current = state["session"];
  if (cache_matches(state, current, category)) { current.erase("summary"); current.erase("latest_reply"); }
  for (auto& part : current["sections"].items()) {
    if (cache_matches(state, part.value(), category)) { part.value()["summary"] = nullptr; part.value().erase("questions"); }
  }
}
void bound_summary_history(Json& state) {
  std::set<std::string> categories;
  for (const auto& [id, f] : state["fields"].items()) { (void)id; categories.insert(json::get_string(f, "category")); }
  for (const auto& category : categories) {
    const auto* rule = rule_for(state, category);
    if (!rule || (*rule)["retention"]["max_events"].is_null()) continue;
    const auto limit = (*rule)["retention"]["max_events"].get<std::size_t>();
    std::vector<std::pair<std::uint64_t, std::string>> cached;
    for (const auto& [id, part] : state["session"]["sections"].items()) {
      if (part.contains("summary") && part["summary"].is_string() && cache_matches(state, part, category))
        cached.emplace_back(part.value("summary_revision", std::uint64_t{0}), id);
    }
    std::sort(cached.begin(), cached.end());
    for (std::size_t i = 0; cached.size() - i > limit; ++i) {
      auto& part = state["session"]["sections"][cached[i].second];
      part["summary"] = nullptr; part.erase("questions");
    }
    if (limit == 0 && cache_matches(state, state["session"], category)) {
      state["session"].erase("summary"); state["session"].erase("latest_reply");
    }
  }
}
Status bump_revision(Json& state) {
  const auto revision = state["revision"].get<std::uint64_t>();
  if (revision == std::numeric_limits<std::uint64_t>::max()) return make_error(Errc::Conflict, "profile revision exhausted");
  state["revision"] = revision + 1;
  return {};
}
std::string request_token(const Json& state, const Json& scenario, std::string_view provider) {
  return Sha256::hex(json::canonical(Json{{"state", state}, {"scenario", scenario}, {"provider", provider}}));
}
}  // namespace

Result<Json> privacy_decision(const Json& state, const Json& request) {
  try {
    if (!request.is_object() || !text(request, "category") ||
        !one_of(json::get_string(request, "op"), {"ask", "store", "send", "infer"}))
      return invalid("privacy request needs category and supported operation");
    const auto op = json::get_string(request, "op");
    const auto category = json::get_string(request, "category");
    const auto id = json::get_string(request, "field");
    Json result{{"allowed", true}, {"operation", op}, {"category", category}, {"reason", "rule allows operation"}};
    auto deny = [&](std::string_view reason) -> Result<Json> {
      result["allowed"] = false; result["reason"] = reason; return result;
    };
    if (!id.empty()) {
      const auto* f = field(state, id);
      if (!f) return invalid("privacy request names unknown profile field");
      if (json::get_string(*f, "category") != category) return invalid("privacy request category differs from field");
      const auto status = json::get_string(*f, "status");
      const auto disposition = json::get_string(*f, "question_disposition", status);
      if ((status == "never" || disposition == "never") && op != "store") return deny("field is never: do not ask, send or infer");
      if ((status == "declined" || disposition == "declined") && op == "ask") return deny("field is declined: do not ask again");
      if ((status == "never" || disposition == "never") && op == "store" && json::get_string(request, "provenance") == "model_inferred")
        return deny("field is never: do not infer");
    }
    const auto* rule = rule_for(state, category);
    if (!rule) return deny("no user-selected or preset rule covers category");
    LOOM_TRY(validate_rule(*rule));
    result["rule_id"] = (*rule)["id"];
    result["retention"] = (*rule)["retention"];
    if (op == "store" && !(*rule)["store"].get<bool>()) return deny("category storage is disabled");
    if (op == "infer" && (!(*rule)["infer"].get<bool>() || (*rule)["explicit_only"].get<bool>()))
      return deny("category requires explicit statements or inference is disabled");
    if (op == "store" && (*rule)["explicit_only"].get<bool>() &&
        json::get_string(request, "provenance") == "model_inferred") return deny("category only stores explicit statements");
    if (op == "send" && !member((*rule)["providers"], json::get_string(request, "provider")))
      return deny("provider is not allowed for category");
    for (const auto& [key, cap] : {std::pair{"detail", "max_detail"}, std::pair{"sensitivity", "max_sensitivity"}}) {
      const auto* value = json::find(request, key);
      if (value && (!value->is_number() || value->get<double>() < 0)) return invalid("detail and sensitivity must be nonnegative numbers");
      if (value && !(*rule)[cap].is_null() && value->get<double>() > (*rule)[cap].get<double>())
        return deny(std::string("requested ") + key + " exceeds category rule");
    }
    return result;
  } catch (const std::exception& e) { return invalid(std::string("malformed privacy state: ") + e.what()); }
}

Result<ProfileSession> ProfileSession::create(const Json& scenario, const Json& defaults, const Json& existing) {
  try {
    if (!scenario.is_object() || scenario.value("schema", "") != "loom.onboarding.scenario/1" ||
        !scenario.contains("fields") || !scenario["fields"].is_array() ||
        !scenario.contains("sections") || !scenario["sections"].is_array() || scenario["sections"].empty() ||
        !scenario.contains("method_ref") || !scenario["method_ref"].is_object() || !text(scenario, "prompt"))
      return invalid("invalid onboarding scenario");
    if (!defaults.is_object() || !existing.is_object()) return invalid("defaults and existing state must be objects");
    if (existing.contains("schema") && existing["schema"] != "loom.onboarding.state/1")
      return make_error(Errc::Unsupported, "onboarding migration cannot rewrite a different or newer schema");
    ProfileSession session;
    session.scenario_ = scenario;
    session.state_ = existing;
    auto& state = session.state_;
    if (!state.contains("schema")) state["schema"] = "loom.onboarding.state/1";
    if (!state.contains("revision")) state["revision"] = 0;
    if (!state["revision"].is_number_integer() || (state["revision"].is_number_integer() && !state["revision"].is_number_unsigned() && state["revision"].get<long long>() < 0))
      return invalid("profile revision must be a nonnegative integer");
    if (!state.contains("fields")) state["fields"] = Json::object();
    if (!state["fields"].is_object()) return invalid("existing fields must be an object");
    if (!state.contains("privacy")) state["privacy"] = defaults.at("privacy");
    if (!state["privacy"].is_object() || !state["privacy"].contains("rules") || !state["privacy"]["rules"].is_array())
      return invalid("privacy rules must be an array");
    for (const auto& rule : state["privacy"]["rules"]) LOOM_TRY(validate_rule(rule));
    if (!state.contains("settings")) state["settings"] = defaults.at("settings");
    if (!state["settings"].is_object() || !one_of(json::get_string(state["settings"], "preference_mode"), {"ask", "candidate", "automatic"}))
      return invalid("settings.preference_mode must be ask, candidate or automatic");
    std::set<std::string> section_ids;
    for (const auto& section : scenario["sections"]) {
      if (!section.is_object() || !text(section, "id") || !section.contains("questions") || !section["questions"].is_array())
        return invalid("invalid scenario section");
      if (!section_ids.insert(json::get_string(section, "id")).second) return invalid("duplicate scenario section id");
    }
    std::set<std::string> field_ids;
    for (const auto& descriptor : scenario["fields"]) {
      if (!descriptor.is_object() || !text(descriptor, "id") || !text(descriptor, "category") || !text(descriptor, "section") ||
          !descriptor.contains("detail") || !descriptor["detail"].is_number() || descriptor["detail"].get<double>() < 0 ||
          !descriptor.contains("sensitivity") || !descriptor["sensitivity"].is_number() || descriptor["sensitivity"].get<double>() < 0)
        return invalid("invalid scenario field descriptor");
      const auto id = json::get_string(descriptor, "id");
      if (!field_ids.insert(id).second) return invalid("duplicate scenario field id");
      if (!section_ids.contains(json::get_string(descriptor, "section"))) return invalid("scenario field names unknown section");
      if (!state["fields"].contains(id)) {
        Json f = descriptor; f["status"] = "unknown"; f["value"] = nullptr; f["provenance"] = nullptr; f["review"] = nullptr;
        state["fields"][id] = std::move(f);
      }
    }
    for (const auto& [id, f] : state["fields"].items()) {
      (void)id;
      if (!f.is_object() || !text(f, "category") || !one_of(json::get_string(f, "status"), {"unknown", "known", "declined", "never"}) ||
          !f.contains("detail") || !f["detail"].is_number() || !f.contains("sensitivity") || !f["sensitivity"].is_number())
        return invalid("existing profile field is malformed; refusing lossy migration");
    }
    if (!state.contains("candidates")) state["candidates"] = Json::object();
    if (!state.contains("history")) state["history"] = Json::array();
    if (!state["candidates"].is_object() || !state["history"].is_array()) return invalid("invalid candidate/history container");
    if (!state.contains("session")) {
      state["session"] = Json{{"status", "active"}, {"section", scenario["sections"][0].at("id")}, {"sections", Json::object()}};
    }
    if (!state["session"].is_object() || !state["session"].contains("sections") || !state["session"]["sections"].is_object())
      return invalid("invalid session state");
    for (const auto& section : scenario["sections"]) {
      if (!text(section, "id") || !section.contains("questions") || !section["questions"].is_array()) return invalid("invalid scenario section");
      const auto id = json::get_string(section, "id");
      for (const auto& question : section["questions"]) {
        if (!question.is_object() || !text(question, "field") || !text(question, "text") ||
            !state["fields"].contains(json::get_string(question, "field"))) return invalid("invalid scenario question or field reference");
      }
      if (!state["session"]["sections"].contains(id)) state["session"]["sections"][id] = Json{{"status", "pending"}, {"summary", nullptr}};
    }
    if (!state.contains("graph")) state["graph"] = Json{{"nodes", Json::object()}, {"edges", Json::array()}};
    if (!state["graph"].is_object() || !state["graph"].contains("nodes") || !state["graph"]["nodes"].is_object() ||
        !state["graph"].contains("edges") || !state["graph"]["edges"].is_array()) return invalid("invalid profile graph container");
    LOOM_TRY(session.sync_graph());
    return session;
  } catch (const std::exception& e) { return invalid(std::string("malformed onboarding state: ") + e.what()); }
}

Status ProfileSession::sync_graph() {
  auto& nodes = state_["graph"]["nodes"];
  // Only explicitly marked projection nodes belong to this view. Similar
  // legacy IDs alone never authorize deleting or overwriting user data.
  for (const auto& [id, f] : state_["fields"].items()) {
    (void)f;
    if (nodes.contains("profile/" + id) && !json::get_bool(nodes["profile/" + id], "onboarding_projection"))
      return make_error(Errc::Conflict, "profile graph identity collides with unowned legacy node");
  }
  for (const auto& rule : state_["privacy"]["rules"]) {
    const auto id = "privacy/" + json::get_string(rule, "id");
    if (nodes.contains(id) && !json::get_bool(nodes[id], "onboarding_projection"))
      return make_error(Errc::Conflict, "privacy graph identity collides with unowned legacy node");
  }
  for (auto it = nodes.begin(); it != nodes.end();) {
    if (json::get_bool(it.value(), "onboarding_projection")) it = nodes.erase(it);
    else ++it;
  }
  for (const auto& [id, f] : state_["fields"].items()) {
    nodes["profile/" + id] = Json{{"id", "profile/" + id}, {"onboarding_projection", true}, {"kind", "profile_claim"}, {"subject", "user"},
       {"predicate", id}, {"object", f.contains("value") ? f["value"] : Json(nullptr)}, {"qualifiers", f}};
  }
  for (const auto& rule : state_["privacy"]["rules"]) {
    const auto id = "privacy/" + json::get_string(rule, "id");
    nodes[id] = Json{{"id", id}, {"onboarding_projection", true}, {"kind", "privacy_rule"}, {"rule", rule}};
  }
  return {};
}

Result<Json> ProfileSession::dispatch(const Json& action) {
  // Transaction by copy: every malformed/privacy-denied action leaves state
  // unchanged, including history and graph projection.
  const Json before = state_;
  try {
    auto result = apply(action);
    if (!result) { state_ = before; return result.error(); }
    auto bumped = bump_revision(state_);
    if (!bumped) { state_ = before; return bumped.error(); }
    auto projected = sync_graph();
    if (!projected) { state_ = before; return projected.error(); }
    return state_;
  } catch (const std::exception& e) { state_ = before; return invalid(std::string("malformed onboarding action: ") + e.what()); }
}

Result<Json> ProfileSession::apply(const Json& action) {
  if (!action.is_object() || !text(action, "op")) return invalid("action needs op");
  LOOM_TRY(event_identity(action));
  const auto op = json::get_string(action, "op");
  auto& current = state_["session"];
  if (op == "pause" || op == "resume") { current["status"] = op == "pause" ? "paused" : "active"; return state_; }
  if (op == "confirm_section") {
    const auto section = json::get_string(action, "section", json::get_string(current, "section"));
    if (!current["sections"].contains(section)) return invalid("unknown onboarding section");
    const auto decision = json::get_string(action, "decision");
    if (!one_of(decision, {"confirmed", "corrected", "rejected"})) return invalid("invalid section confirmation");
    if (action.contains("summary") && !action["summary"].is_string()) return invalid("corrected summary must be a string");
    if (action.contains("summary")) current["sections"][section]["summary"] = action["summary"];
    current["sections"][section]["review"] = decision;
    if (decision != "confirmed") { current["sections"][section]["status"] = "pending"; return state_; }
    for (const auto& [id, candidate] : state_["candidates"].items()) {
      (void)id;
      const auto* f = field(state_, json::get_string(candidate, "field"));
      if (f && json::get_string(*f, "section") == section && candidate["review"] == "pending")
        return make_error(Errc::Conflict, "review pending candidates before confirming section");
    }
    current["sections"][section]["status"] = "complete";
    for (const auto& s : scenario_["sections"]) {
      const auto sid = json::get_string(s, "id");
      if (current["sections"][sid]["status"] == "pending") { current["section"] = sid; return state_; }
    }
    current["status"] = "complete";
    return state_;
  }
  if (op == "repeat" || op == "skip") {
    const auto section = json::get_string(action, "section", json::get_string(current, "section"));
    if (!current["sections"].contains(section)) return invalid("unknown onboarding section");
    current["sections"][section]["status"] = op == "skip" ? "skipped" : "pending";
    if (op == "repeat") {
      current["section"] = section; current["status"] = "active"; current["sections"][section]["summary"] = nullptr;
      current.erase("summary"); current.erase("latest_reply"); current["sections"][section].erase("questions");
    }
    if (op == "skip") {
      bool next = false;
      for (const auto& s : scenario_["sections"]) {
        const auto sid = json::get_string(s, "id");
        if (next && json::get_string(current["sections"][sid], "status") == "pending") { current["section"] = sid; return state_; }
        if (sid == section) next = true;
      }
      for (const auto& s : scenario_["sections"]) {
        const auto sid = json::get_string(s, "id");
        if (json::get_string(current["sections"][sid], "status") == "pending") { current["section"] = sid; return state_; }
      }
      current["status"] = "complete";
    }
    return state_;
  }
  if (op == "privacy") {
    if (!action.contains("rule")) return invalid("privacy action needs rule");
    const auto& rule = action["rule"];
    LOOM_TRY(validate_rule(rule));
    Json before = nullptr;
    auto& rules = state_["privacy"]["rules"];
    bool replaced = false;
    for (auto& old : rules) if (old["category"] == rule["category"]) { before = old; old = rule; replaced = true; break; }
    if (!replaced) rules.push_back(rule);
    if (!rule["store"].get<bool>() || rule["retention"]["history"] != "full" ||
        (rule["retention"]["max_events"].is_number_integer() && rule["retention"]["max_events"] == 0))
      redact_summaries(state_, json::get_string(rule, "category"));
    record(state_, action, json::get_string(rule, "category"), before, rule);
    // A wildcard update also reapplies its retention to each inherited category.
    for (const auto& [id, f] : state_["fields"].items()) { (void)id; enforce_history(state_, json::get_string(f, "category")); }
    bound_summary_history(state_);
    return state_;
  }
  if (op == "settings") {
    if (!action.contains("settings") || !action["settings"].is_object()) return invalid("settings action needs an object");
    for (const auto& [key, value] : action["settings"].items()) state_["settings"][key] = value;
    if (!one_of(json::get_string(state_["settings"], "preference_mode"), {"ask", "candidate", "automatic"})) return invalid("invalid preference mode");
    return state_;
  }
  if (op == "review") {
    const auto candidate_id = json::get_string(action, "candidate", json::get_string(action, "id"));
    if (!state_["candidates"].contains(candidate_id)) return invalid("unknown candidate");
    auto& candidate = state_["candidates"][candidate_id];
    if (candidate["review"] != "pending") return make_error(Errc::Conflict, "candidate already reviewed");
    const auto decision = json::get_string(action, "decision");
    if (!one_of(decision, {"confirmed", "rejected"})) return invalid("review decision must be confirmed or rejected");
    auto& f = state_["fields"].at(candidate.at("field").get<std::string>());
    const auto fid = json::get_string(candidate, "field");
    Json before = f;
    if (decision == "confirmed") {
      if (json::get_string(f, "status") == "never") return make_error(Errc::Conflict, "never field must be explicitly reopened first");
      const auto provenance = action.contains("value") ? "user_stated" : json::get_string(candidate, "provenance");
      Json classification = f;
      classification["detail"] = candidate["detail"]; classification["sensitivity"] = candidate["sensitivity"];
      LOOM_TRY_ASSIGN(auto allow, privacy_decision(state_, checked_request(classification, fid, "store", provenance)));
      if (!allow["allowed"].get<bool>()) return make_error(Errc::Auth, json::get_string(allow, "reason"));
      const auto prior_disposition = json::get_string(f, "question_disposition", json::get_string(f, "status"));
      f["question_disposition"] = provenance == "model_inferred" && prior_disposition == "declined" ? "declined" : "unknown";
      f["status"] = "known"; f["value"] = action.contains("value") ? action["value"] : candidate["value"];
      f["detail"] = candidate["detail"]; f["sensitivity"] = candidate["sensitivity"];
      f["provenance"] = provenance; f["review"] = json::get_bool(action, "automatic") ? "accepted_automatically" : "confirmed"; f["time"] = action["time"];
      f["source_refs"] = candidate["source_refs"]; f["candidate_id"] = candidate_id;
    }
    candidate["review"] = json::get_bool(action, "automatic") ? "accepted_automatically" : decision; candidate["review_time"] = action["time"];
    Json review_event = action; review_event["field"] = fid;
    record(state_, review_event, json::get_string(f, "category"), before, f);
    return state_;
  }
  const auto fid = json::get_string(action, "field");
  if (!state_["fields"].contains(fid)) return invalid("unknown profile field");
  auto& f = state_["fields"][fid];
  const auto category = json::get_string(f, "category");
  Json before = f;
  if (op == "status" || op == "delete") {
    const auto status = op == "delete" ? "unknown" : json::get_string(action, "status");
    if (!one_of(status, {"unknown", "declined", "never"})) return invalid("status must be unknown, declined or never");
    f["status"] = status; f["question_disposition"] = status; f["value"] = nullptr; f["provenance"] = nullptr; f["review"] = nullptr;
    f.erase("source_refs"); f.erase("candidate_id"); f.erase("time");
    for (auto it = state_["candidates"].begin(); it != state_["candidates"].end();) {
      if (json::get_string(it.value(), "field") == fid) it = state_["candidates"].erase(it); else ++it;
    }
    if (json::get_bool(action, "purge_history")) {
      auto& history = state_["history"];
      for (auto it = history.begin(); it != history.end();) {
        if (json::get_string(*it, "field") == fid) it = history.erase(it); else ++it;
      }
      before = nullptr;
    }
    // Summaries are unstructured text and can mention any previously supplied
    // field. A deletion/never must not leave another retained copy there.
    if (op == "delete" || status == "never") {
      redact_summaries(state_);
    }
    record(state_, action, category, before, f);
    return state_;
  }
  if (op != "answer" && op != "propose") return invalid("unsupported onboarding operation");
  if (!action.contains("value")) return invalid("answer/proposal needs value");
  const auto provenance = json::get_string(action, "provenance", op == "answer" ? "user_stated" : "model_inferred");
  if (!one_of(provenance, {"user_stated", "model_inferred", "form"})) return invalid("unknown information provenance");
  if (json::get_string(f, "status") == "never") return make_error(Errc::Auth, "never field must be explicitly reopened first");
  Json classification = f;
  for (const char* key : {"detail", "sensitivity"}) {
    if (action.contains(key)) classification[key] = action[key];
    if (!classification[key].is_number() || classification[key].get<double>() < 0)
      return invalid("answer detail and sensitivity must be nonnegative numbers");
  }
  if (provenance == "model_inferred") {
    LOOM_TRY_ASSIGN(auto allow, privacy_decision(state_, checked_request(classification, fid, "infer", provenance)));
    if (!allow["allowed"].get<bool>()) return make_error(Errc::Auth, json::get_string(allow, "reason"));
  }
  LOOM_TRY_ASSIGN(auto allow, privacy_decision(state_, checked_request(classification, fid, "store", provenance)));
  if (!allow["allowed"].get<bool>()) return make_error(Errc::Auth, json::get_string(allow, "reason"));
  const auto id = json::get_string(action, "id");
  if (state_["candidates"].contains(id)) return make_error(Errc::AlreadyExists, "candidate id already exists");
  Json candidate = action;
  candidate["provenance"] = provenance; candidate["review"] = "pending";
  candidate["detail"] = classification["detail"]; candidate["sensitivity"] = classification["sensitivity"];
  if (!candidate.contains("source_refs")) candidate["source_refs"] = Json::array();
  candidate["category"] = category;
  candidate["presentation"] = op == "answer" || json::get_bool(action, "wizard") ? "ask" : state_["settings"]["preference_mode"];
  state_["candidates"][id] = candidate;
  record(state_, action, category, nullptr, candidate);
  if (candidate["presentation"] == "automatic") {
    Json review{{"op", "review"}, {"id", action["id"]}, {"candidate", id}, {"decision", "confirmed"}, {"time", action["time"]}, {"automatic", true}};
    return apply(review);
  }
  return state_;
}

Result<Json> ProfileSession::model_request(std::string_view provider) const {
  try {
    if (state_["session"]["status"] != "active") return make_error(Errc::Paused, "interview is not active");
    const auto section_id = json::get_string(state_["session"], "section");
    const Json* section = nullptr;
    for (const auto& s : scenario_["sections"]) if (s["id"] == section_id) section = &s;
    if (!section) return invalid("session names unknown scenario section");
    Json result{{"method_ref", scenario_["method_ref"]}, {"prompt", scenario_["prompt"]}, {"provider", provider},
       {"request_token", request_token(state_, scenario_, provider)},
       {"section", section_id}, {"questions", Json::array()}, {"context", Json::object()}, {"candidates", Json::array()}, {"policy", Json::object()},
       {"reply_schema", scenario_.contains("reply_schema") ? scenario_["reply_schema"] : Json::object()}};
    for (const auto& question : (*section)["questions"]) {
      const auto fid = json::get_string(question, "field");
      const auto* f = field(state_, fid);
      if (!f) return invalid("scenario question names unknown field");
      LOOM_TRY_ASSIGN(auto allow, privacy_decision(state_, checked_request(*f, fid, "ask")));
      if (allow["allowed"].get<bool>()) result["questions"].push_back(question);
      LOOM_TRY_ASSIGN(auto inference, privacy_decision(state_, checked_request(*f, fid, "infer")));
      result["policy"][fid] = Json{{"may_ask", allow["allowed"]}, {"may_infer", inference["allowed"]}};
    }
    for (const auto& [fid, f] : state_["fields"].items()) {
      if (json::get_string(f, "status") != "known") continue;
      Json request = checked_request(f, fid, "send"); request["provider"] = provider;
      LOOM_TRY_ASSIGN(auto allow, privacy_decision(state_, request));
      if (allow["allowed"].get<bool>()) result["context"][fid] = Json{{"value", f["value"]}, {"provenance", f["provenance"]}, {"review", f["review"]}};
    }
    for (const auto& [id, candidate] : state_["candidates"].items()) {
      (void)id;
      if (candidate["review"] != "pending") continue;
      const auto fid = json::get_string(candidate, "field");
      const auto* f = field(state_, fid);
      if (!f) continue;
      Json classification = *f;
      if (candidate.contains("detail")) classification["detail"] = candidate["detail"];
      if (candidate.contains("sensitivity")) classification["sensitivity"] = candidate["sensitivity"];
      Json request = checked_request(classification, fid, "send"); request["provider"] = provider;
      LOOM_TRY_ASSIGN(auto allow, privacy_decision(state_, request));
      if (allow["allowed"].get<bool>()) result["candidates"].push_back(candidate);
    }
    return result;
  } catch (const std::exception& e) { return invalid(std::string("malformed model request state: ") + e.what()); }
}

Result<Json> ProfileSession::ingest_model_reply(const Json& reply) {
  const Json before = state_;
  try {
    if (state_["session"]["status"] != "active") return make_error(Errc::Paused, "interview is not active");
    if (!text(reply, "provider") || !text(reply, "request_token")) return invalid("model reply needs host-supplied provider and request_token");
    if (json::get_string(reply, "request_token") != request_token(state_, scenario_, json::get_string(reply, "provider")))
      return make_error(Errc::Conflict, "model reply is stale for profile, privacy, scenario or session");
    LOOM_TRY_ASSIGN(auto prepared, model_request(json::get_string(reply, "provider")));
    if (!reply.is_object() || !text(reply, "section") || !text(reply, "summary") ||
        !reply.contains("questions") || !reply["questions"].is_array() ||
        !reply.contains("candidates") || !reply["candidates"].is_array()) return invalid("model reply needs section, summary, questions and candidates");
    if (reply["section"] != state_["session"]["section"]) return invalid("model reply belongs to another section");
    for (const auto& q : reply["questions"]) {
      const auto fid = json::get_string(q, "field");
      const auto* f = field(state_, fid);
      if (!f || !text(q, "text")) return invalid("model question needs field and text");
      LOOM_TRY_ASSIGN(auto allow, privacy_decision(state_, checked_request(*f, fid, "ask")));
      if (!allow["allowed"].get<bool>()) return make_error(Errc::Auth, "model attempted to ask a declined or never field");
    }
    // Validate on a private copy so a malformed later candidate never commits
    // an earlier candidate or section summary.
    ProfileSession staged = *this;
    for (const auto& c : reply["candidates"]) {
      if (!c.is_object() || !c.contains("source_refs") || !c["source_refs"].is_array()) return invalid("model candidate needs source_refs");
      Json action = c; action["op"] = "propose"; action["provenance"] = "model_inferred"; action["wizard"] = true;
      auto applied = staged.dispatch(action);
      if (!applied) return applied.error();
    }
    Json section = staged.state_["session"]["sections"].at(reply["section"].get<std::string>());
    section["status"] = "awaiting_confirmation";
    bool cache_allowed = true;
    std::set<std::string> contributors;
    for (const auto& q : prepared["questions"]) contributors.insert(json::get_string(q, "field"));
    for (const auto& [id, f] : prepared["context"].items()) { (void)f; contributors.insert(id); }
    for (const auto& candidate : prepared["candidates"]) contributors.insert(json::get_string(candidate, "field"));
    for (const auto& q : reply["questions"]) contributors.insert(json::get_string(q, "field"));
    for (const auto& candidate : reply["candidates"]) contributors.insert(json::get_string(candidate, "field"));
    Json summary_fields = Json::array();
    // Scope retained prose to the actual allowed request/reply contributors,
    // never to unrelated categories that did not enter this interaction.
    for (const auto& id : contributors) {
      const auto* f = field(staged.state_, id);
      const auto* rule = f ? rule_for(staged.state_, json::get_string(*f, "category")) : nullptr;
      summary_fields.push_back(id);
      if (!rule || !(*rule)["store"].get<bool>() || (*rule)["retention"]["history"] != "full" ||
          ((*rule)["retention"]["max_events"].is_number_integer() && (*rule)["retention"]["max_events"] == 0)) cache_allowed = false;
    }
    if (cache_allowed) {
      section["summary"] = reply["summary"]; section["questions"] = reply["questions"];
      section["summary_fields"] = summary_fields; section["summary_revision"] = staged.state_["revision"];
      staged.state_["session"]["summary"] = reply["summary"];
      staged.state_["session"]["latest_reply"] = reply;
      staged.state_["session"]["summary_fields"] = summary_fields;
      section.erase("summary_redacted");
    } else {
      section["summary"] = nullptr; section.erase("questions"); section["summary_redacted"] = true;
      staged.state_["session"].erase("summary"); staged.state_["session"].erase("latest_reply");
    }
    staged.state_["session"]["sections"][reply["section"].get<std::string>()] = section;
    bound_summary_history(staged.state_);
    LOOM_TRY(bump_revision(staged.state_));
    state_ = std::move(staged.state_);
    return state_;
  } catch (const std::exception& e) { state_ = before; return invalid(std::string("malformed model reply: ") + e.what()); }
}
}  // namespace loom::onboarding
