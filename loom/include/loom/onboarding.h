// Data-driven user profile, privacy and resumable interview. No network calls.
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::onboarding {

// request: {op: ask|store|send|infer, category, field?, provider?, detail?,
// sensitivity?, provenance?}. The result explains the selected rule/denial.
Result<Json> privacy_decision(const Json& state, const Json& request);

// K2b-4: the languages a free message of the person is read in, in the order they are tried. `locale` is the language of the conversation (the
// language of the lines on the screen); `present` is an object keyed by the languages the data has words in; `spec` is the preset: null or "all" =
// every language of `present` (the conversation's first), "conversation" = only the conversation's (English when `present` has no words in it), a
// list of language ids = those. Anything else is InvalidArgument. An empty locale or "any" names no conversation: "conversation" then means all.
Result<std::vector<std::string>> match_languages(std::string_view locale, const Json& spec, const Json& present);

class ProfileSession {
 public:
  // Existing state is copied, never modified in place; only missing fields are
  // added. Future schemas are rejected rather than migrated backwards.
  static Result<ProfileSession> create(const Json& scenario, const Json& defaults,
                                       const Json& existing = Json::object());
  Json snapshot() const { return state_; }
  Json scenario() const { return scenario_; }
  // Caller supplies event id/time/source_refs. Returns the complete snapshot.
  // answer creates a candidate; review confirms/rejects/corrects it. A form uses
  // these same operations with provenance=form. Statuses are explicit data.
  Result<Json> dispatch(const Json& action);
  // Builds a provider-filtered request. The caller's existing model adapter
  // executes it; host attaches the prepared provider/request_token to its reply
  // (the model must not choose these). Stale/unrelated responses are rejected.
  // The questions go to the model as one string in `locale` (English unless the host names another language).
  Result<Json> model_request(std::string_view provider, std::string_view locale = "en") const;
  Result<Json> ingest_model_reply(const Json& reply);

  // Scenario texts in one language: [{id, title, questions: [{field, text}]}]. A text missing in `locale` falls back to English and the
  // row says so ("fallback": true); the builtin scenario has none (a test keeps it so).
  Json questions(std::string_view locale) const;
  // What the interview asks at a detail level. options: {level: id (the value of the setting profile_detail_level), by_category?:
  // {category: level id}}. Result: {level, level_choice_pending, asks: [{field, section, category, detail, sensitivity, topic}],
  // skipped: [{field, category, reason, reason_text{en,pl}}], counts}. A field is asked when its detail and sensitivity are within the
  // level's caps and it is not known, declined or never (and the privacy rules allow asking). While the first question ("how much
  // to ask") is unanswered, only that question is asked.
  Result<Json> plan(const Json& options) const;
  // The levels with their previews in `locale`; the numbers in the texts are the real counts of the profile as it is now.
  Result<Json> levels(std::string_view locale = "en") const;
  // A free remark of the person ("nie pytaj o zdrowie", "wystarczy") matched against scenario.remark_rules. Whole words/phrases,
  // case-insensitive, diacritics folded; no match means no effect. The effects are PROPOSALS ({matched, proposals, understood, conflicts});
  // nothing is written unless options.apply is true (then options.id and time are needed, and options.allow_erase when a never would
  // erase stored answers). Status effects reuse op "status" (the same never as the person's own). A detail-level effect is never applied
  // here: the level is the setting profile_detail_level, written by the caller. options.level names the current level for from/to.
  Result<Json> apply_remark(std::string_view remark, std::string_view locale = "", const Json& options = Json::object());

 private:
  Json scenario_;
  Json state_;
  Result<Json> apply(const Json& action);
  Status sync_graph();
};
}  // namespace loom::onboarding
