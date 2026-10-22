// M1-K2b-1: the interview of the person ("Ty") as actions: profile.interview, profile.show, profile.set, profile.confirm.
// Pure Json in, a text and data out; the words come from the pack chat_ops (env.say), the setting profile_detail_level is written through
// env.set_setting (the same rules as settings.set), the profile through OnboardingStore::apply (the outer revision is the CAS token).
// No model is called and no network is touched here: FORM MODE only (the model path is K2b-2 and needs privacy_decision + a cost estimate).
#pragma once

#include <functional>
#include <string>

#include "loom/db.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::actions::profile {

struct Env {
  Database& db;
  std::string lang;          // "pl" | "en": the language of the words and of the questions
  std::string default_user;  // the profile used when the call names none (pack chat_ops: profile.default_user)
  std::function<std::string(const std::string&, const Json&)> say;  // a sentence of the pack in the language of the run
  std::function<std::string(const std::string&)> fold;              // letters folded for comparing the person's words
  std::function<Json(const std::string&)> get_setting;
  std::function<Status(const std::string&, const Json&)> set_setting;
};

struct Reply {
  std::string text;
  Json data;
};

Result<Reply> interview(const Env& env, const Json& args);  // op: start|answer|review|skip|decline|never|status
Result<Reply> show(const Env& env, const Json& args);       // read only
Result<Reply> set(const Env& env, const Json& args);        // gated: confirm:profile.set
Result<Reply> confirm(const Env& env, const Json& args);    // gated: confirm:profile.confirm (a remark or a section card)

}  // namespace loom::actions::profile
