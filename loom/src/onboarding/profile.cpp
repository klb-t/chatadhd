#include "loom/onboarding.h"

#include <algorithm>
#include <exception>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "loom/kb.h"
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

// ---- texts for the person: a string (legacy single-language scenario) or {en, pl, ...}. English is the language of record; a
// requested language that is missing falls back to it and says so (fell_back), never silently.
bool valid_text(const Json& v) {
  if (v.is_string()) return !v.get_ref<const std::string&>().empty();
  if (!v.is_object() || !text(v, "en")) return false;
  for (const auto& [key, item] : v.items()) { (void)key; if (!item.is_string()) return false; }
  return true;
}
std::string pick_text(const Json& v, std::string_view locale, bool& fell_back) {
  if (v.is_string()) return v.get<std::string>();
  if (!v.is_object()) return "";
  if (const auto* t = json::find(v, locale); t && t->is_string() && !t->get_ref<const std::string&>().empty()) return t->get<std::string>();
  fell_back = true;
  return json::get_string(v, "en");
}
void replace_all(std::string& s, std::string_view from, const std::string& to) {
  for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}
double cap_of(const Json& level, const char* key) {
  const auto* v = json::find(level, key);
  return !v || v->is_null() ? std::numeric_limits<double>::infinity() : v->get<double>();
}
Status validate_extensions(const Json& scenario) {
  for (const auto& section : scenario["sections"]) {
    if (section.contains("title") && !valid_text(section["title"])) return invalid("scenario section title needs a text or {en, ...}");
    for (const auto& question : section["questions"])
      if (!question.contains("text") || !valid_text(question["text"])) return invalid("scenario question text needs a text or {en, ...}");
  }
  if (const auto* levels = json::find(scenario, "detail_levels")) {
    if (!levels->is_array() || levels->empty()) return invalid("detail_levels must be a non-empty array");
    std::set<std::string> ids;
    for (const auto& level : *levels) {
      if (!level.is_object() || !text(level, "id") || !ids.insert(json::get_string(level, "id")).second) return invalid("detail level needs a unique id");
      if (!level.contains("label") || !valid_text(level["label"])) return invalid("detail level needs a label");
      for (const char* key : {"max_detail", "max_sensitivity"}) {
        const auto* v = json::find(level, key);
        if (!v || (!v->is_null() && (!v->is_number() || v->get<double>() < 0))) return invalid(std::string("detail level needs null or nonnegative ") + key);
      }
      const auto* preview = json::find(level, "preview");
      for (const char* part : {"asks", "stores", "model_sees"})
        if (!preview || !preview->is_object() || !preview->contains(part) || !valid_text((*preview)[part])) return invalid("detail level preview needs asks, stores and model_sees");
    }
  }
  if (const auto* rules = json::find(scenario, "remark_rules")) {
    if (!rules->is_array()) return invalid("remark_rules must be an array");
    std::set<std::string> ids;
    for (const auto& rule : *rules) {
      if (!rule.is_object() || !text(rule, "id") || !ids.insert(json::get_string(rule, "id")).second) return invalid("remark rule needs a unique id");
      const auto* pattern = json::find(rule, "pattern");
      const auto* effect = json::find(rule, "effect");
      if (!pattern || !pattern->is_object() || !effect || !effect->is_object()) return invalid("remark rule needs pattern and effect");
      for (const auto& [lang, list] : pattern->items()) {
        (void)lang;
        if (!list.is_array()) return invalid("remark patterns must be arrays");
        for (const auto& p : list) if (!p.is_string() || p.get_ref<const std::string&>().empty()) return invalid("remark pattern must be a non-empty string");
      }
      const bool delta = effect->contains("detail_level_delta");
      if (delta ? !((*effect)["detail_level_delta"].is_number_integer() && std::abs((*effect)["detail_level_delta"].get<int>()) == 1)
                : !one_of(json::get_string(*effect, "status"), {"never", "unknown"}))
        return invalid("remark effect needs status never|unknown or detail_level_delta of 1 or -1");
      if (!rule.contains("understood") || !valid_text(rule["understood"])) return invalid("remark rule needs an understood text");
    }
  }
  return {};
}

// ---- the plan of the interview at a detail level
const Json* level_def(const Json& scenario, std::string_view id) {
  const auto* levels = json::find(scenario, "detail_levels");
  if (!levels || !levels->is_array()) return nullptr;
  for (const auto& level : *levels) if (json::get_string(level, "id") == id) return &level;
  return nullptr;
}
bool known_status(const Json& f, std::string_view status) {
  return json::get_string(f, "status") == status || json::get_string(f, "question_disposition", json::get_string(f, "status")) == status;
}
// level: the global level id; by_category: {category: level id}; assume_chosen: the preview of a level (the person has not picked it yet)
// counts the questions that level WOULD ask and leaves the "how much to ask" question out.
Result<Json> build_plan(const Json& scenario, const Json& state, std::string_view level_id, const Json& by_category, bool assume_chosen) {
  if (!json::find(scenario, "detail_levels")) return make_error(Errc::Unsupported, "scenario has no detail_levels");
  const auto* global = level_def(scenario, level_id);
  if (!global) return invalid("unknown detail level: " + std::string(level_id));
  if (!by_category.is_object()) return invalid("by_category must be an object");
  for (const auto& [category, id] : by_category.items()) {
    (void)category;
    if (!id.is_string() || !level_def(scenario, id.get_ref<const std::string&>())) return invalid("by_category names an unknown detail level");
  }
  bool pending = false;  // the "how much to ask" question has not been answered, declined or refused yet
  for (const auto& descriptor : scenario["fields"]) {
    if (!descriptor.contains("setting_key")) continue;
    const auto* f = field(state, json::get_string(descriptor, "id"));
    if (f && json::get_string(*f, "status") == "unknown" && !known_status(*f, "never") && !known_status(*f, "declined")) pending = true;
  }
  pending = pending && !assume_chosen;
  Json asks = Json::array(), skipped = Json::array();
  std::size_t stores = 0, model_sees = 0;
  Json by_reason = Json::object();
  auto skip = [&](const Json& descriptor, std::string_view reason, std::string detail = "") {
    Json row{{"field", descriptor["id"]}, {"category", descriptor["category"]}, {"reason", reason}};
    if (const auto* t = json::find(scenario, "skip_reasons")) if (const auto* r = json::find(*t, reason)) row["reason_text"] = *r;
    if (!detail.empty()) row["detail"] = detail;
    skipped.push_back(std::move(row));
    by_reason[std::string(reason)] = by_reason.value(std::string(reason), 0) + 1;
  };
  for (const auto& descriptor : scenario["fields"]) {
    const auto id = json::get_string(descriptor, "id");
    const auto* f = field(state, id);
    if (!f) return invalid("scenario field is missing in the profile: " + id);
    const auto category = json::get_string(*f, "category");
    const bool meta = descriptor.contains("setting_key");
    if (meta && assume_chosen) { skip(descriptor, "setting"); continue; }
    if (known_status(*f, "never")) { skip(descriptor, "never"); continue; }
    if (known_status(*f, "declined")) { skip(descriptor, "declined"); continue; }
    if (json::get_string(*f, "status") == "known") {
      skip(descriptor, "known");
      if (!meta) {
        const auto* level = level_def(scenario, json::get_string(by_category, category, std::string(level_id)));
        if (f->at("detail").get<double>() <= cap_of(*level, "max_detail") && f->at("sensitivity").get<double>() <= cap_of(*level, "max_sensitivity")) { ++stores; ++model_sees; }
      }
      continue;
    }
    if (meta) {
      if (pending) asks.push_back(Json{{"field", id}, {"section", descriptor["section"]}, {"category", category}, {"detail", f->at("detail")},
                                       {"sensitivity", f->at("sensitivity")}, {"topic", descriptor.value("topic", Json(nullptr))}});
      else skip(descriptor, "setting");
      continue;
    }
    if (pending) { skip(descriptor, "level_not_chosen"); continue; }
    const auto* level = level_def(scenario, json::get_string(by_category, category, std::string(level_id)));
    if (f->at("detail").get<double>() > cap_of(*level, "max_detail")) { skip(descriptor, "detail_above_level"); continue; }
    if (f->at("sensitivity").get<double>() > cap_of(*level, "max_sensitivity")) { skip(descriptor, "sensitivity_above_level"); continue; }
    LOOM_TRY_ASSIGN(auto ask, privacy_decision(state, checked_request(*f, id, "ask")));
    if (!ask["allowed"].get<bool>()) { skip(descriptor, "privacy_denied", json::get_string(ask, "reason")); continue; }
    LOOM_TRY_ASSIGN(auto store, privacy_decision(state, checked_request(*f, id, "store")));
    if (store["allowed"].get<bool>()) ++stores;
    asks.push_back(Json{{"field", id}, {"section", descriptor["section"]}, {"category", category}, {"detail", f->at("detail")},
                        {"sensitivity", f->at("sensitivity")}, {"topic", descriptor.value("topic", Json(nullptr))}});
  }
  return Json{{"level", level_id}, {"by_category", by_category}, {"level_choice_pending", pending}, {"asks", asks}, {"skipped", skipped},
              {"counts", {{"asks", asks.size()}, {"skipped", skipped.size()}, {"stores", stores}, {"model_sees", model_sees}, {"skipped_by_reason", by_reason}}}};
}

// ---- remarks: whole words/phrases, case-insensitive, Polish diacritics folded by the project's own normalizer (kb::Normalizer::fold)
struct NormalizerHolder {
  std::shared_ptr<const kb::Pack> pack;
  std::optional<kb::Normalizer> norm;
  std::string error;
};
const NormalizerHolder& normalizer_holder() {
  static const NormalizerHolder holder = [] {
    NormalizerHolder h;
    auto pack = kb::Pack::load_builtin();
    if (!pack) { h.error = pack.error().message; return h; }
    h.pack = pack.value();
    auto n = kb::Normalizer::create(*h.pack);
    if (!n) { h.error = n.error().message; return h; }
    h.norm.emplace(std::move(n).value());
    return h;
  }();
  return holder;
}
std::vector<std::string> split_words(const std::string& folded) {
  std::vector<std::string> words;
  std::string cur;
  for (unsigned char c : folded) {
    if (std::isalnum(c) || c >= 0x80) cur.push_back(static_cast<char>(c));
    else if (!cur.empty()) { words.push_back(std::move(cur)); cur.clear(); }
  }
  if (!cur.empty()) words.push_back(std::move(cur));
  return words;
}
std::size_t find_phrase(const std::vector<std::string>& words, const std::vector<std::string>& phrase) {
  if (phrase.empty() || phrase.size() > words.size()) return std::string::npos;
  for (std::size_t at = 0; at + phrase.size() <= words.size(); ++at)
    if (std::equal(phrase.begin(), phrase.end(), words.begin() + static_cast<std::ptrdiff_t>(at))) return at;
  return std::string::npos;
}
}  // namespace

// K2b-4. The language of a conversation decides the language of the lines on the screen; it does not decide what the person is understood in.
Result<std::vector<std::string>> match_languages(std::string_view locale, const Json& spec, const Json& present) {
  enum class Mode { all, conversation, list } mode = Mode::all;
  std::set<std::string> listed;
  if (spec.is_string()) {
    const auto& name = spec.get_ref<const std::string&>();
    if (name == "conversation") mode = Mode::conversation;
    else if (name != "all" && name != "any") return invalid("match_languages must be \"all\", \"conversation\" or a list of language ids, not \"" + name + "\"");
  } else if (spec.is_array()) {
    mode = Mode::list;
    for (const auto& l : spec) {
      if (!l.is_string()) return invalid("match_languages: a list holds language ids (strings)");
      listed.insert(l.get<std::string>());
    }
  } else if (!spec.is_null()) {
    return invalid("match_languages must be \"all\", \"conversation\" or a list of language ids");
  }
  const bool anywhere = locale.empty() || locale == "any";  // no language named: nothing is "the conversation's"
  const std::string here = anywhere ? std::string() : std::string(locale);
  const std::string fallback = here == "pl" ? "pl" : "en";  // a language the data has no words in is read as English, as before
  const bool has = present.is_object();
  const std::string primary = has && present.contains(here) ? here : fallback;
  std::vector<std::string> out;
  auto take = [&](const std::string& lang) {
    if (!has || !present.contains(lang) || std::find(out.begin(), out.end(), lang) != out.end()) return;
    const bool allowed = mode == Mode::all || (mode == Mode::list && listed.count(lang) > 0) || (mode == Mode::conversation && (anywhere || lang == primary));
    if (allowed) out.push_back(lang);
  };
  take(primary);
  take(fallback);
  if (has)
    for (auto it = present.begin(); it != present.end(); ++it) take(it.key());
  return out;
}

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
      // A field's classification is authoritative graph data. Omitting or
      // understating it in a caller request must not bypass the selected cap.
      const auto* descriptor = id.empty() ? nullptr : field(state, id);
      const auto* declared = descriptor ? json::find(*descriptor, key) : nullptr;
      if (value && (!value->is_number() || value->get<double>() < 0)) return invalid("detail and sensitivity must be nonnegative numbers");
      if (declared && (!declared->is_number() || declared->get<double>() < 0))
        return invalid("profile detail and sensitivity must be nonnegative numbers");
      if (!(*rule)[cap].is_null()) {
        if (!value && !declared) return invalid(std::string("privacy request needs ") + key + " for a capped category");
        const double limit = (*rule)[cap].get<double>();
        if ((value && value->get<double>() > limit) || (declared && declared->get<double>() > limit))
          return deny(std::string("requested ") + key + " exceeds category rule");
      }
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
    LOOM_TRY(validate_extensions(scenario));
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
        if (!question.is_object() || !text(question, "field") || !question.contains("text") || !valid_text(question["text"]) ||
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

Result<Json> ProfileSession::model_request(std::string_view provider, std::string_view locale) const {
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
      // The model gets one string in one language (its reply schema says so); English unless the host names another language.
      bool fell_back = false;
      if (allow["allowed"].get<bool>()) result["questions"].push_back(Json{{"field", fid}, {"text", pick_text(question["text"], locale, fell_back)}});
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
Json ProfileSession::questions(std::string_view locale) const {
  Json out = Json::array();
  for (const auto& section : scenario_["sections"]) {
    bool title_fell_back = false;
    Json row{{"id", section["id"]}, {"title", section.contains("title") ? pick_text(section["title"], locale, title_fell_back) : json::get_string(section, "id")},
             {"questions", Json::array()}};
    if (title_fell_back) row["fallback"] = true;
    for (const auto& question : section["questions"]) {
      bool fell_back = false;
      Json q{{"field", question["field"]}, {"text", pick_text(question["text"], locale, fell_back)}};
      if (fell_back) q["fallback"] = true;
      row["questions"].push_back(std::move(q));
    }
    out.push_back(std::move(row));
  }
  return out;
}

Result<Json> ProfileSession::plan(const Json& options) const {
  try {
    if (!options.is_object()) return invalid("plan options must be an object");
    const auto level = json::get_string(options, "level");
    if (level.empty()) return invalid("plan needs level (the value of the setting profile_detail_level)");
    const auto* by_category = json::find(options, "by_category");
    return build_plan(scenario_, state_, level, by_category ? *by_category : Json::object(), false);
  } catch (const std::exception& e) { return invalid(std::string("malformed plan input: ") + e.what()); }
}

Result<Json> ProfileSession::levels(std::string_view locale) const {
  try {
    const auto* defs = json::find(scenario_, "detail_levels");
    if (!defs) return make_error(Errc::Unsupported, "scenario has no detail_levels");
    Json out = Json::array();
    for (const auto& level : *defs) {
      LOOM_TRY_ASSIGN(auto plan, build_plan(scenario_, state_, json::get_string(level, "id"), Json::object(), true));
      bool fell_back = false;
      std::string topics;
      for (const auto& row : plan["asks"]) {
        if (!topics.empty()) topics += ", ";
        topics += row.contains("topic") && !row["topic"].is_null() ? pick_text(row["topic"], locale, fell_back) : json::get_string(row, "field");
      }
      Json preview = Json::object();
      for (const auto& [part, tmpl] : level["preview"].items()) {
        auto line = pick_text(tmpl, locale, fell_back);
        replace_all(line, "{topics}", topics);
        replace_all(line, "{asks}", std::to_string(plan["counts"]["asks"].get<std::size_t>()));
        replace_all(line, "{stores}", std::to_string(plan["counts"]["stores"].get<std::size_t>()));
        replace_all(line, "{model_sees}", std::to_string(plan["counts"]["model_sees"].get<std::size_t>()));
        preview[part] = line;
      }
      Json row{{"id", level["id"]}, {"label", pick_text(level["label"], locale, fell_back)}, {"max_detail", level["max_detail"]},
               {"max_sensitivity", level["max_sensitivity"]}, {"preview", preview},
               {"counts", {{"asks", plan["counts"]["asks"]}, {"stores", plan["counts"]["stores"]}, {"model_sees", plan["counts"]["model_sees"]}}}};
      if (fell_back) row["fallback"] = true;
      out.push_back(std::move(row));
    }
    return out;
  } catch (const std::exception& e) { return invalid(std::string("malformed levels input: ") + e.what()); }
}

Result<Json> ProfileSession::apply_remark(std::string_view remark, std::string_view locale, const Json& options) {
  const Json before = state_;
  try {
    const auto* rules = json::find(scenario_, "remark_rules");
    if (!rules) return make_error(Errc::Unsupported, "scenario has no remark_rules");
    if (!options.is_object()) return invalid("remark options must be an object");
    const auto& holder = normalizer_holder();
    if (!holder.norm) return make_error(Errc::Unavailable, "remarks need the text normalizer: " + holder.error);
    const bool apply = json::get_bool(options, "apply");
    if (apply) LOOM_TRY(event_identity(options));
    const auto text_words = split_words(holder.norm->fold(remark));
    // The languages the rules have words in, and the ones the person is read in (preset 'all'; options.match_languages names another reading).
    Json present = Json::object();
    for (const auto& rule : *rules)
      if (const auto* pattern = json::find(rule, "pattern"); pattern && pattern->is_object())
        for (auto it = pattern->begin(); it != pattern->end(); ++it) present[it.key()] = true;
    const auto* spec = json::find(options, "match_languages");
    LOOM_TRY_ASSIGN(const auto tried, match_languages(locale, spec ? *spec : Json(nullptr), present));
    const auto* negators = json::find(scenario_, "remark_negators");
    const auto level_now = json::get_string(options, "level");
    const auto* defs = json::find(scenario_, "detail_levels");
    Json matched = Json::array(), proposals = Json::array(), understood = Json::array();
    const std::string display = locale == "pl" ? "pl" : "en";
    for (const auto& rule : *rules) {
      const auto& effect = rule["effect"];
      const bool is_never = json::get_string(effect, "status") == "never";
      bool hit = false;
      for (const auto& lang : tried) {
        const auto* list = json::find(rule["pattern"], lang);
        if (!list) continue;
        for (const auto& pattern : *list) {
          const auto at = find_phrase(text_words, split_words(holder.norm->fold(pattern.get_ref<const std::string&>())));
          if (at == std::string::npos) continue;
          // "nie wystarczy", "do not ask more": a negator right before a plain phrase turns it round; never guess, so no effect.
          bool negated = false;
          if (!is_never && negators) {
            const auto* words = json::find(*negators, lang);
            for (std::size_t back = 1; words && back <= 2 && back <= at; ++back)
              for (const auto& n : *words) if (n.is_string() && holder.norm->fold(n.get<std::string>()) == text_words[at - back]) negated = true;
          }
          if (!negated) { hit = true; break; }
        }
        if (hit) break;
      }
      if (!hit) continue;
      matched.push_back(rule["id"]);
      bool fell_back = false;
      Json proposal{{"rule", rule["id"]}, {"understood", pick_text(rule["understood"], display, fell_back)}};
      understood.push_back(proposal["understood"]);
      if (effect.contains("detail_level_delta")) {
        const int delta = effect["detail_level_delta"].get<int>();
        proposal["kind"] = "detail_level"; proposal["delta"] = delta; proposal["from"] = nullptr; proposal["to"] = nullptr;
        if (defs && !level_now.empty()) {
          int index = -1, i = 0;
          for (const auto& level : *defs) { if (json::get_string(level, "id") == level_now) index = i; ++i; }
          if (index < 0) return invalid("remark option level names an unknown detail level");
          const int target = std::clamp(index + delta, 0, static_cast<int>(defs->size()) - 1);
          proposal["from"] = level_now; proposal["to"] = (*defs)[static_cast<std::size_t>(target)]["id"]; proposal["at_limit"] = target == index;
        } else proposal["needs_level"] = true;
        proposal["write"] = "profile_detail_level";  // a setting: the caller writes it after the person has seen the line above
      } else {
        const auto status = json::get_string(effect, "status");
        proposal["kind"] = "status"; proposal["status"] = status;
        if (effect.contains("category")) proposal["category"] = effect["category"];
        std::set<std::string> targets;
        for (const auto& [fid, f] : state_["fields"].items()) {
          const bool named = effect.contains("fields") && effect["fields"].is_array() &&
                             std::find(effect["fields"].begin(), effect["fields"].end(), fid) != effect["fields"].end();
          if (named || (effect.contains("category") && json::get_string(f, "category") == json::get_string(effect, "category"))) targets.insert(fid);
        }
        Json fields = Json::array(), erases = Json::array();
        for (const auto& fid : targets) {
          const auto& f = state_["fields"][fid];
          if (f.contains("setting_key")) continue;  // the how-much question is not switched off by a remark about a topic
          if (status == "never" ? !known_status(f, "never") : (known_status(f, "never") || known_status(f, "declined"))) {
            fields.push_back(fid);
            if (status == "never" && json::get_string(f, "status") == "known") erases.push_back(fid);
          }
        }
        proposal["fields"] = fields; proposal["erases"] = erases; proposal["already"] = fields.empty();
      }
      proposals.push_back(std::move(proposal));
    }
    // Two matched rules that pull in opposite directions are reported, never resolved by guessing.
    Json conflicts = Json::array();
    for (std::size_t i = 0; i < proposals.size(); ++i) for (std::size_t j = i + 1; j < proposals.size(); ++j) {
      const auto& a = proposals[i]; const auto& b = proposals[j];
      const bool levels_opposed = a["kind"] == "detail_level" && b["kind"] == "detail_level" && a["delta"] != b["delta"];
      bool overlap = false;
      if (a["kind"] == "status" && b["kind"] == "status" && a["status"] != b["status"]) {
        for (const auto& fid : a["fields"]) if (std::find(b["fields"].begin(), b["fields"].end(), fid) != b["fields"].end()) overlap = true;
        if (a["fields"].empty() || b["fields"].empty()) overlap = a.value("category", "") == b.value("category", "") && a.contains("category");
      }
      if (levels_opposed || overlap) conflicts.push_back(Json::array({a["rule"], b["rule"]}));
    }
    Json applied = Json::array();
    if (apply) {
      if (!conflicts.empty()) return make_error(Errc::Conflict, "remark matches rules that contradict each other; nothing applied");
      for (const auto& p : proposals) if (p["kind"] == "status" && !p["erases"].empty() && !json::get_bool(options, "allow_erase"))
        return make_error(Errc::Conflict, "remark would erase stored answers; pass allow_erase after the person has seen them");
      for (const auto& p : proposals) {
        if (p["kind"] != "status") continue;
        for (const auto& fid : p["fields"]) {
          Json action{{"op", "status"}, {"status", p["status"]}, {"field", fid}, {"origin", "remark"}, {"rule", p["rule"]},
                      {"id", json::get_string(options, "id") + "/" + p["rule"].get<std::string>() + "/" + fid.get<std::string>()},
                      {"time", options["time"]}, {"source_refs", options.value("source_refs", Json::array())}};
          auto done = dispatch(action);
          if (!done) { state_ = before; return done.error(); }
          applied.push_back(Json{{"rule", p["rule"]}, {"field", fid}, {"status", p["status"]}});
        }
      }
    }
    return Json{{"matched", matched}, {"proposals", proposals}, {"applied", applied}, {"conflicts", conflicts}, {"understood", understood}, {"applies", apply},
                {"match_languages", tried}};
  } catch (const std::exception& e) { state_ = before; return invalid(std::string("malformed remark input: ") + e.what()); }
}
}  // namespace loom::onboarding
