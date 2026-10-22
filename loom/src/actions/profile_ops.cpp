#include "profile_ops.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <vector>

#include "loom/onboarding_store.h"
#include "loom/util/ids.h"
#include "loom/util/time.h"

namespace loom::actions::profile {

namespace {

using onboarding::OnboardingStore;

Error err(Errc code, std::string message) { return Error(code, std::move(message)); }

std::string pick(const Env& e, const Json& v) {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_object())
    for (const std::string& k : {e.lang, std::string("en")})
      if (const Json* s = json::find(v, k); s && s->is_string()) return s->get<std::string>();
  return std::string();
}

std::string value_text(const Json& v) { return v.is_string() ? v.get<std::string>() : json::dump(v); }

struct Loaded {
  std::string user;
  Json state;
  std::int64_t rev = 0;
};

// The profile of the call: the one named, else the preset of the pack. `create`: open it when it does not exist yet (start only).
Result<Loaded> load(const Env& e, const Json& args, bool create) {
  Loaded l;
  l.user = json::get_string(args, "user_id");
  if (l.user.empty()) l.user = e.default_user;
  if (l.user.empty()) return err(Errc::InvalidArgument, e.say("profile_no_user", Json::object()));
  OnboardingStore store(e.db);
  auto r = store.read(l.user);
  if (!r && r.error().code == Errc::NotFound && create) r = store.open(l.user);
  if (!r) {
    if (r.error().code == Errc::NotFound) return err(Errc::NotFound, e.say("profile_not_started", Json{{"user", l.user}}));
    return r.error();
  }
  l.state = *r;
  l.rev = l.state.at("revision").get<std::int64_t>();
  // The caller may name the revision it saw: a write against another revision is a conflict, reported and never merged.
  if (const Json* given = json::find(args, "expected_revision"); given && !given->is_null()) {
    std::int64_t v = -1;
    if (given->is_number_integer()) v = given->get<std::int64_t>();
    else if (given->is_string()) {
      const auto& s = given->get_ref<const std::string&>();
      const auto parsed = std::from_chars(s.data(), s.data() + s.size(), v, 10);
      if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size()) v = -1;
    }
    if (v < 0) return err(Errc::InvalidArgument, e.say("profile_bad_revision", Json::object()));
    l.rev = v;
  }
  return l;
}

Result<Json> view(const Env& e, const Loaded& l, Json request) {
  if (!request.contains("locale")) request["locale"] = e.lang;
  auto r = OnboardingStore(e.db).interview_view(l.user, request);
  if (!r) {
    if (r.error().code == Errc::Unsupported) return err(Errc::Unsupported, e.say("profile_scenario_old", Json{{"detail", r.error().message}}));
    return r.error();
  }
  return r->at("view");
}

// One write through the store with the outer revision as the CAS token; the next write uses the revision the store answered with.
Result<Json> write(const Env& e, Loaded& l, Json action) {
  if (!action.contains("id")) action["id"] = gen_id("iv_");
  if (!action.contains("time")) action["time"] = timeutil::utc_now_iso();
  if (!action.contains("source_refs")) action["source_refs"] = Json::array({"interview/" + action.at("id").get<std::string>()});
  auto r = OnboardingStore(e.db).apply(l.user, l.rev, action);
  if (!r) {
    if (r.error().code == Errc::Conflict && r.error().message.find("revision changed") != std::string::npos)
      return err(Errc::Conflict, e.say("profile_conflict", Json{{"expected", l.rev}}));
    return r.error();
  }
  l.state = *r;
  l.rev = l.state.at("revision").get<std::int64_t>();
  return *r;
}

std::string topic_of(const Env& e, const Loaded& l, const std::string& field) {
  const Json defs = l.state.at("scenario_definition").value("fields", Json::array());
  for (const auto& f : defs)
    if (json::get_string(f, "id") == field) {
      const std::string t = f.contains("topic") ? pick(e, f.at("topic")) : std::string();
      return t.empty() ? field : t;
    }
  return field;
}

std::string section_title(const Env& e, const Loaded& l, const std::string& section) {
  const Json defs = l.state.at("scenario_definition").value("sections", Json::array());
  for (const auto& s : defs)
    if (json::get_string(s, "id") == section) {
      const std::string t = s.contains("title") ? pick(e, s.at("title")) : std::string();
      return t.empty() ? section : t;
    }
  return section;
}

const Json* field_of(const Loaded& l, const std::string& field) {
  const Json& fields = l.state.at("profile").at("fields");
  if (!fields.is_object() || !fields.contains(field)) return nullptr;
  return &fields.at(field);
}

std::set<std::string> pending_fields(const Loaded& l) {
  std::set<std::string> out;
  const Json& candidates = l.state.at("profile").at("candidates");
  for (const auto& [id, c] : candidates.items()) {
    (void)id;
    if (json::get_string(c, "review") == "pending") out.insert(json::get_string(c, "field"));
  }
  return out;
}

std::set<std::string> skipped_sections(const Loaded& l) {
  std::set<std::string> out;
  const Json session = l.state.at("profile").value("session", Json::object());
  const Json sections = session.value("sections", Json::object());
  for (const auto& [id, s] : sections.items())
    if (json::get_string(s, "status") == "skipped") out.insert(id);
  return out;
}

Result<std::string> level_of(const Env& e) {
  const Json v = e.get_setting("profile_detail_level");
  if (!v.is_string() || v.get_ref<const std::string&>().empty()) return err(Errc::Unavailable, e.say("profile_no_level", Json::object()));
  return v.get<std::string>();
}

Result<Json> plan_of(const Env& e, const Loaded& l) {
  LOOM_TRY_ASSIGN(auto level, level_of(e));
  return view(e, l, Json{{"what", "plan"}, {"level", level}});
}

// The first question that is still to be asked at the level: not pending confirmation, not in a section skipped for this session.
struct Next {
  Json ask = nullptr;
  std::int64_t remaining = 0;
};
Next next_of(const Loaded& l, const Json& plan) {
  Next n;
  const auto pending = pending_fields(l);
  const auto skipped = skipped_sections(l);
  const Json asks = plan.at("asks");
  for (const auto& a : asks) {
    if (pending.count(json::get_string(a, "field")) || skipped.count(json::get_string(a, "section"))) continue;
    ++n.remaining;
    if (n.ask.is_null()) n.ask = a;
  }
  return n;
}

Json base_data(const Loaded& l) { return Json{{"user_id", l.user}, {"revision", std::to_string(l.rev)}}; }

Result<Reply> next_reply(const Env& e, const Loaded& l, const std::string& head, Json data) {
  LOOM_TRY_ASSIGN(auto level, level_of(e));
  LOOM_TRY_ASSIGN(auto plan, plan_of(e, l));
  const Next n = next_of(l, plan);
  data["level"] = level;
  data["level_choice_pending"] = plan.value("level_choice_pending", false);
  data["remaining"] = n.remaining;
  data["mode"] = "form";
  std::string text = head;
  auto add = [&](const std::string& line) { if (!line.empty()) text += (text.empty() ? "" : "\n") + line; };
  if (n.ask.is_null()) {
    data["question"] = nullptr;
    data["done"] = true;
    add(e.say("profile_done", Json{{"level", level}}));
    return Reply{text, data};
  }
  const std::string field = json::get_string(n.ask, "field");
  LOOM_TRY_ASSIGN(auto qs, view(e, l, Json{{"what", "questions"}}));
  std::string question;
  bool fell_back = false;
  for (const auto& sec : qs) {
    const Json rows = sec.value("questions", Json::array());
    for (const auto& q : rows)
      if (json::get_string(q, "field") == field) { question = json::get_string(q, "text"); fell_back = q.value("fallback", false); }
  }
  Json q{{"field", field}, {"section", n.ask.value("section", "")}, {"topic", topic_of(e, l, field)}, {"text", question}, {"fallback", fell_back}};
  const Json* f = field_of(l, field);
  const bool is_level = f && f->contains("setting_key");
  q["is_level_question"] = is_level;
  data["done"] = false;
  add(question);
  if (is_level) {
    LOOM_TRY_ASSIGN(auto levels, view(e, l, Json{{"what", "levels"}}));
    Json rows = Json::array();
    for (const auto& row : levels) {
      const Json preview = row.value("preview", Json::object());
      rows.push_back(Json{{"id", row.value("id", "")}, {"label", row.value("label", "")}, {"preview", preview}, {"counts", row.value("counts", Json::object())}});
      add("• " + e.say("profile_level_line", Json{{"id", row.value("id", "")}, {"label", row.value("label", "")}, {"asks", preview.value("asks", "")},
                                                  {"stores", preview.value("stores", "")}, {"model_sees", preview.value("model_sees", "")}}));
    }
    q["levels"] = rows;
    add(e.say("profile_level_hint", Json::object()));
  } else {
    add(e.say("profile_question_hint", Json{{"remaining", n.remaining}, {"level", level}}));
  }
  data["question"] = q;
  return Reply{text, data};
}

Json proposal_of(const Env& e, const std::string& kind, Json with, const std::string& title) {
  with["kind"] = kind;
  return Json{{"action", "profile.confirm"}, {"with", with}, {"status", "pending"}, {"title", title}, {"gate", "confirm:profile.confirm"},
              {"how", e.say("profile_proposal_how", Json::object())}};
}

// What a field is called when the person is told about it.
std::string fields_text(const Env& e, const Loaded& l, const Json& list) {
  std::string out;
  for (const auto& f : list) out += (out.empty() ? "" : ", ") + topic_of(e, l, f.get<std::string>());
  return out;
}

// The answer to the level question: an id, a label, the number of the level or a sentence naming exactly one of them.
Result<std::string> match_level(const Env& e, const Json& levels, const std::string& text) {
  const std::string typed = e.fold(text);
  std::vector<std::string> hits;
  int index = 0;
  for (const auto& row : levels) {
    ++index;
    const std::string id = row.value("id", "");
    const std::string label = e.fold(row.value("label", ""));
    if (typed == e.fold(id) || typed == label || typed == std::to_string(index)) return id;
    if ((!label.empty() && typed.find(label) != std::string::npos) || typed.find(e.fold(id)) != std::string::npos) hits.push_back(id);
  }
  if (hits.size() == 1) return hits[0];
  std::string options;
  for (const auto& row : levels) options += (options.empty() ? "" : ", ") + row.value("label", "") + " (" + row.value("id", "") + ")";
  return err(Errc::InvalidArgument, e.say("profile_level_unknown", Json{{"text", text}, {"options", options}}));
}

std::string section_for_review(const Loaded& l, const Json& args) {
  if (const std::string given = json::get_string(args, "section"); !given.empty()) return given;
  // The section of the newest answer waiting for a yes, else the section the session is in.
  std::string newest_time, section;
  const Json& candidates = l.state.at("profile").at("candidates");
  for (const auto& [id, c] : candidates.items()) {
    (void)id;
    if (json::get_string(c, "review") != "pending") continue;
    const std::string time = json::get_string(c, "time");
    if (time >= newest_time) {
      newest_time = time;
      if (const Json* f = field_of(l, json::get_string(c, "field"))) section = json::get_string(*f, "section");
    }
  }
  if (!section.empty()) return section;
  return json::get_string(l.state.at("profile").value("session", Json::object()), "section");
}

Json pending_in(const Loaded& l, const std::string& section) {
  Json out = Json::array();
  const Json& candidates = l.state.at("profile").at("candidates");
  for (const auto& [id, c] : candidates.items()) {
    if (json::get_string(c, "review") != "pending") continue;
    const Json* f = field_of(l, json::get_string(c, "field"));
    if (f && json::get_string(*f, "section") == section) out.push_back(Json{{"candidate", id}, {"field", json::get_string(c, "field")}, {"value", c.value("value", Json(nullptr))}});
  }
  return out;
}

Result<Reply> refuse_mode(const Env& e, const Json& args) {
  if (json::get_string(args, "mode") == "model") return err(Errc::Unsupported, e.say("profile_model_denied", Json::object()));
  return Reply{"", Json::object()};
}

// ── ops ─────────────────────────────────────────────────────────────

Result<Reply> op_start(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, true));
  // "Skip" holds for one session: a new start brings the skipped sections back.
  Json back = Json::array();
  for (const auto& id : skipped_sections(l)) {
    LOOM_TRY(write(e, l, Json{{"op", "repeat"}, {"section", id}}));
    back.push_back(id);
  }
  Json data = base_data(l);
  data["sections_back"] = back;
  LOOM_TRY_ASSIGN(auto first, plan_of(e, l));
  const bool chosen = !first.value("level_choice_pending", false);
  return next_reply(e, l, e.say(chosen ? "profile_start_resume" : "profile_start", Json::object()), data);
}

Result<Reply> op_answer(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, false));
  const std::string text = json::get_string(args, "text");
  if (text.empty()) return err(Errc::InvalidArgument, e.say("profile_no_text", Json::object()));
  std::string slot = json::get_string(args, "slot");
  LOOM_TRY_ASSIGN(auto level, level_of(e));
  if (slot.empty()) {
    LOOM_TRY_ASSIGN(auto plan, plan_of(e, l));
    const Next n = next_of(l, plan);
    if (n.ask.is_null()) return err(Errc::InvalidArgument, e.say("profile_no_slot", Json::object()));
    slot = json::get_string(n.ask, "field");
  }
  const Json* f = field_of(l, slot);
  if (!f) return err(Errc::NotFound, e.say("profile_unknown_slot", Json{{"slot", slot}}));
  const std::string topic = topic_of(e, l, slot);

  // A remark of the person ("nie pytaj o zdrowie", "wystarczy") is read first: it returns what was understood and writes NOTHING; the yes is the
  // gate of profile.confirm.
  LOOM_TRY_ASSIGN(auto remark, view(e, l, Json{{"what", "remark"}, {"text", text}, {"level", level}}));
  const Json matched = remark.value("matched", Json::array());
  if (!matched.empty()) {
    const Json proposals = remark.value("proposals", Json::array());
    const Json conflicts = remark.value("conflicts", Json::array());
    std::string out = e.say("profile_remark_head", Json::object());
    bool anything_to_do = false;
    const Json understood = remark.value("understood", Json::array());
    for (const auto& u : understood) out += "\n• " + u.get<std::string>();
    for (const auto& p : proposals) {
      if (json::get_string(p, "kind") == "status") {
        if (!p.value("already", false)) anything_to_do = true;
        else out += "\n  " + e.say("profile_remark_already", Json::object());
        if (const Json erases = p.value("erases", Json::array()); !erases.empty())
          out += "\n  " + e.say("profile_remark_erases", Json{{"fields", fields_text(e, l, erases)}});
      } else if (!p.value("at_limit", false)) {
        anything_to_do = true;
        out += "\n  " + e.say("profile_remark_level", Json{{"from", json::get_string(p, "from", "?")}, {"to", json::get_string(p, "to")}});
      }
    }
    Json data = base_data(l);
    data["written"] = false;
    data["remark"] = remark;
    if (!conflicts.empty()) {
      out += "\n" + e.say("profile_remark_conflict", Json::object());
      data["proposal"] = nullptr;
    } else if (anything_to_do) {
      out += "\n" + e.say("profile_remark_confirm", Json::object());
      data["proposal"] = proposal_of(e, "remark", Json{{"text", text}, {"locale", e.lang}, {"user_id", l.user}}, e.say("profile_remark_head", Json::object()));
    } else {
      data["proposal"] = nullptr;
    }
    return Reply{out, data};
  }

  if (json::get_string(*f, "status") == "never")
    return err(Errc::Auth, e.say("profile_never_blocked", Json{{"topic", topic}, {"slot", slot}}));

  if (f->contains("setting_key")) {
    // The level question: the answer is the person's own choice, so it is confirmed at once; the level goes to the setting.
    const std::string key = json::get_string(*f, "setting_key");
    LOOM_TRY_ASSIGN(auto levels, view(e, l, Json{{"what", "levels"}}));
    LOOM_TRY_ASSIGN(auto chosen, match_level(e, levels, text));
    const std::string cid = gen_id("iv_");
    LOOM_TRY(write(e, l, Json{{"op", "answer"}, {"id", cid}, {"field", slot}, {"value", chosen}, {"provenance", "user_stated"}}));
    LOOM_TRY(write(e, l, Json{{"op", "review"}, {"candidate", cid}, {"decision", "confirmed"}}));
    Json data = base_data(l);
    data["written"] = true;
    data["level_chosen"] = chosen;
    if (auto st = e.set_setting(key, Json(chosen)); !st)
      return err(st.error().code, e.say("profile_level_not_stored", Json{{"level", chosen}, {"setting", key}, {"reason", st.error().message}}));
    std::string label = chosen;
    for (const auto& row : levels) if (row.value("id", "") == chosen) label = row.value("label", chosen);
    return next_reply(e, l, e.say("profile_level_saved", Json{{"label", label}, {"id", chosen}, {"setting", key}}), data);
  }

  const std::string cid = gen_id("iv_");
  LOOM_TRY(write(e, l, Json{{"op", "answer"}, {"id", cid}, {"field", slot}, {"value", text}, {"provenance", "user_stated"}}));
  Json data = base_data(l);
  data["written"] = true;
  data["answered"] = Json{{"field", slot}, {"candidate", cid}, {"review", "pending"}};
  return next_reply(e, l, e.say("profile_answer_saved", Json{{"topic", topic}}), data);
}

Result<Reply> op_review(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, false));
  const std::string section = section_for_review(l, args);
  const std::string title = section_title(e, l, section);
  const Json pending = pending_in(l, section);
  Json data = base_data(l);
  data["section"] = section;
  data["written"] = false;
  if (pending.empty()) {
    data["items"] = Json::array();
    data["proposal"] = nullptr;
    return Reply{e.say("profile_review_none", Json{{"section", title}}), data};
  }
  std::string text = e.say("profile_review_head", Json{{"section", title}});
  Json items = Json::array();
  for (const auto& p : pending) {
    const std::string field = p.value("field", "");
    items.push_back(Json{{"field", field}, {"topic", topic_of(e, l, field)}, {"value", p.value("value", Json(nullptr))}, {"candidate", p.value("candidate", "")}});
    text += "\n• " + e.say("profile_review_line", Json{{"topic", topic_of(e, l, field)}, {"value", value_text(p.value("value", Json(nullptr)))}});
  }
  text += "\n" + e.say("profile_review_ask", Json::object());
  data["items"] = items;
  data["proposal"] = proposal_of(e, "section", Json{{"section", section}, {"user_id", l.user}}, e.say("profile_review_head", Json{{"section", title}}));
  return Reply{text, data};
}

Result<Reply> op_skip(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, false));
  std::string section = json::get_string(args, "section");
  if (section.empty()) {
    if (const std::string slot = json::get_string(args, "slot"); !slot.empty()) {
      if (const Json* f = field_of(l, slot)) section = json::get_string(*f, "section");
    } else {
      LOOM_TRY_ASSIGN(auto plan, plan_of(e, l));
      const Next n = next_of(l, plan);
      if (!n.ask.is_null()) section = json::get_string(n.ask, "section");
    }
  }
  if (section.empty()) return err(Errc::InvalidArgument, e.say("profile_nothing_to_skip", Json::object()));
  LOOM_TRY(write(e, l, Json{{"op", "skip"}, {"section", section}}));
  Json data = base_data(l);
  data["written"] = true;
  data["skipped_section"] = section;
  return next_reply(e, l, e.say("profile_skipped", Json{{"section", section_title(e, l, section)}}), data);
}

// Targets of never / decline: one slot, or every slot of a category (not the question that sets the level: it is not a question about the person).
Result<std::vector<std::string>> targets_of(const Env& e, const Loaded& l, const Json& args) {
  std::vector<std::string> out;
  if (const std::string slot = json::get_string(args, "slot"); !slot.empty()) {
    if (!field_of(l, slot)) return err(Errc::NotFound, e.say("profile_unknown_slot", Json{{"slot", slot}}));
    out.push_back(slot);
    return out;
  }
  const std::string category = json::get_string(args, "category");
  if (category.empty()) return err(Errc::InvalidArgument, e.say("profile_no_slot", Json::object()));
  const Json& fields = l.state.at("profile").at("fields");
  for (const auto& [id, f] : fields.items())
    if (json::get_string(f, "category") == category && !f.contains("setting_key")) out.push_back(id);
  if (out.empty()) return err(Errc::NotFound, e.say("profile_unknown_category", Json{{"category", category}}));
  return out;
}

Result<Reply> op_decline(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, false));
  Json named = args;
  if (json::get_string(named, "slot").empty() && json::get_string(named, "category").empty()) {
    LOOM_TRY_ASSIGN(auto plan, plan_of(e, l));
    const Next n = next_of(l, plan);
    if (!n.ask.is_null()) named["slot"] = n.ask.value("field", "");
  }
  LOOM_TRY_ASSIGN(auto targets, targets_of(e, l, named));
  Json done = Json::array();
  for (const auto& t : targets) {
    const Json* f = field_of(l, t);
    if (!f || json::get_string(*f, "status") == "never" || json::get_string(*f, "status") == "known") continue;  // a never stays never; an answer is not erased by "not now"
    LOOM_TRY(write(e, l, Json{{"op", "status"}, {"status", "declined"}, {"field", t}}));
    done.push_back(t);
  }
  Json data = base_data(l);
  data["written"] = !done.empty();
  data["declined"] = done;
  return next_reply(e, l, e.say("profile_declined", Json{{"what", fields_text(e, l, done.empty() ? Json(targets) : done)}}), data);
}

Result<Reply> op_never(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, false));
  LOOM_TRY_ASSIGN(auto targets, targets_of(e, l, args));
  Json data = base_data(l);
  if (json::get_bool(args, "reopen", false)) {
    // The person's own reopening: only a never is reopened (to unknown, so the question may come again).
    Json done = Json::array();
    for (const auto& t : targets) {
      const Json* f = field_of(l, t);
      if (!f || json::get_string(*f, "status") != "never") continue;
      LOOM_TRY(write(e, l, Json{{"op", "status"}, {"status", "unknown"}, {"field", t}}));
      done.push_back(t);
    }
    data["written"] = !done.empty();
    data["reopened"] = done;
    return next_reply(e, l, e.say(done.empty() ? "profile_reopen_none" : "profile_reopened", Json{{"what", fields_text(e, l, done)}}), data);
  }
  Json erases = Json::array(), todo = Json::array();
  for (const auto& t : targets) {
    const Json* f = field_of(l, t);
    if (!f || json::get_string(*f, "status") == "never") continue;
    todo.push_back(t);
    if (json::get_string(*f, "status") == "known") erases.push_back(t);
  }
  if (!erases.empty() && !json::get_bool(args, "erase", false)) {
    // Nothing is erased behind the person's back: it says what would go and asks for the same call with erase.
    data["written"] = false;
    data["would_erase"] = erases;
    return Reply{e.say("profile_never_erase_needed", Json{{"fields", fields_text(e, l, erases)}}), data};
  }
  for (const auto& t : todo) LOOM_TRY(write(e, l, Json{{"op", "status"}, {"status", "never"}, {"field", t}}));
  data["written"] = !todo.empty();
  data["never"] = todo;
  data["erased"] = erases;
  std::string head = e.say(todo.empty() ? "profile_never_already" : "profile_never_done", Json{{"what", fields_text(e, l, todo)}, {"count", todo.size()}});
  if (!erases.empty()) head += "\n" + e.say("profile_erased", Json{{"fields", fields_text(e, l, erases)}});
  return next_reply(e, l, head, data);
}

Result<Reply> op_status(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, false));
  LOOM_TRY_ASSIGN(auto level, level_of(e));
  LOOM_TRY_ASSIGN(auto plan, plan_of(e, l));
  const Next n = next_of(l, plan);
  std::int64_t known = 0, declined = 0, never = 0;
  const Json& fields = l.state.at("profile").at("fields");
  for (const auto& [id, f] : fields.items()) {
    (void)id;
    if (f.contains("setting_key")) continue;  // the question that sets the level is not a question about the person
    const std::string st = json::get_string(f, "status");
    if (st == "known") ++known;
    else if (st == "declined") ++declined;
    else if (st == "never") ++never;
  }
  const std::int64_t pending = static_cast<std::int64_t>(pending_fields(l).size());
  Json skipped = Json::array();
  std::string skipped_titles;
  for (const auto& s : skipped_sections(l)) {
    skipped.push_back(s);
    skipped_titles += (skipped_titles.empty() ? "" : ", ") + section_title(e, l, s);
  }
  const std::string session_status = json::get_string(l.state.at("profile").value("session", Json::object()), "status");
  Json data = base_data(l);
  data["level"] = level;
  data["level_choice_pending"] = plan.value("level_choice_pending", false);
  data["answered"] = known;
  data["declined"] = declined;
  data["never"] = never;
  data["pending_confirmation"] = pending;
  data["remaining"] = n.remaining;
  data["skipped_sections"] = skipped;
  data["session_status"] = session_status;
  data["mode"] = "form";
  std::string text;
  if (plan.value("level_choice_pending", false)) text = e.say("profile_status_level_pending", Json::object());
  else text = e.say("profile_status_head", Json{{"level", level}, {"answered", known}, {"declined", declined}, {"never", never}, {"pending", pending}, {"remaining", n.remaining}});
  if (!skipped.empty()) text += "\n" + e.say("profile_status_skipped", Json{{"sections", skipped_titles}});
  if (session_status == "paused") text += "\n" + e.say("profile_status_paused", Json::object());
  return Reply{text, data};
}

}  // namespace

Result<Reply> interview(const Env& e, const Json& args) {
  if (!args.is_object()) return err(Errc::InvalidArgument, e.say("profile_op_unknown", Json{{"op", ""}}));
  LOOM_TRY_ASSIGN(auto mode, refuse_mode(e, args));
  (void)mode;
  const std::string op = json::get_string(args, "op", "status");
  if (op == "start") return op_start(e, args);
  if (op == "answer") return op_answer(e, args);
  if (op == "review") return op_review(e, args);
  if (op == "skip") return op_skip(e, args);
  if (op == "decline") return op_decline(e, args);
  if (op == "never") return op_never(e, args);
  if (op == "status") return op_status(e, args);
  return err(Errc::InvalidArgument, e.say("profile_op_unknown", Json{{"op", op}}));
}

Result<Reply> show(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, false));
  LOOM_TRY_ASSIGN(auto level, level_of(e));
  LOOM_TRY_ASSIGN(auto plan, plan_of(e, l));
  const Next n = next_of(l, plan);
  std::set<std::string> asked;
  const Json asks = plan.at("asks");
  for (const auto& a : asks) asked.insert(json::get_string(a, "field"));
  std::map<std::string, Json> why;
  std::map<std::string, std::int64_t> reasons;
  const Json skipped = plan.value("skipped", Json::array());
  for (const auto& s : skipped) {
    why[json::get_string(s, "field")] = s;
    ++reasons[json::get_string(s, "reason")];
  }
  const auto pending = pending_fields(l);
  Json slots = Json::array();
  std::string text = e.say("profile_show_head", Json{{"level", level}});
  const Json& fields = l.state.at("profile").at("fields");
  for (const auto& [id, f] : fields.items()) {
    const std::string status = json::get_string(f, "status", "unknown");
    Json row{{"slot", id}, {"label", topic_of(e, l, id)}, {"category", json::get_string(f, "category")}, {"section", json::get_string(f, "section")},
             {"state", status}, {"asked_at_level", asked.count(id) > 0}};
    // Only a known slot has a value, and only its provenance: unknown, declined and never say nothing about the person.
    if (status == "known") {
      row["value"] = f.value("value", Json(nullptr));
      row["provenance"] = f.value("provenance", Json(nullptr));
    }
    if (status == "unknown" && pending.count(id)) row["waiting_for_confirmation"] = true;
    std::string reason_text;
    if (!asked.count(id)) {
      const auto it = why.find(id);
      if (it != why.end()) {
        const std::string raw = json::get_string(it->second, "reason");
        // Above the chosen level (detail or sensitivity) is one honest reason for the person: it will not be asked at this level.
        const bool above = raw == "detail_above_level" || raw == "sensitivity_above_level";
        row["reason"] = above ? "above_level" : raw;
        if (above) row["reason_detail"] = raw;
        reason_text = it->second.contains("reason_text") ? pick(e, it->second.at("reason_text")) : raw;
        row["reason_text"] = reason_text;
      }
    }
    std::string line = "• " + e.say("profile_show_line", Json{{"label", topic_of(e, l, id)}, {"state", e.say("profile_state_" + status, Json::object())}});
    if (status == "known") {
      line += " — " + value_text(f.value("value", Json(nullptr)));
      const std::string prov = json::get_string(f, "provenance");
      if (!prov.empty()) line += " (" + e.say("profile_prov_" + prov, Json::object()) + ")";
    } else if (!asked.count(id) && !reason_text.empty() && status == "unknown") {
      line += " — " + reason_text;
    }
    text += "\n" + line;
    slots.push_back(std::move(row));
  }
  Json reasons_json = Json::object();
  for (const auto& [reason, count] : reasons) reasons_json[reason] = count;
  Json plan_summary{{"level", level}, {"level_choice_pending", plan.value("level_choice_pending", false)}, {"remaining", n.remaining},
                    {"counts", plan.value("counts", Json::object())}, {"skipped_reasons", reasons_json}};
  text += "\n" + e.say("profile_show_plan", Json{{"remaining", n.remaining}, {"level", level}});
  Json data = base_data(l);
  data["slots"] = slots;
  data["level"] = level;
  data["plan"] = plan_summary;
  return Reply{text, data};
}

Result<Reply> set(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, false));
  const std::string slot = json::get_string(args, "slot");
  if (slot.empty()) return err(Errc::InvalidArgument, e.say("profile_no_slot", Json::object()));
  const Json* given = json::find(args, "value");
  if (!given || given->is_null()) return err(Errc::InvalidArgument, e.say("profile_no_value", Json::object()));
  const Json* f = field_of(l, slot);
  if (!f) return err(Errc::NotFound, e.say("profile_unknown_slot", Json{{"slot", slot}}));
  const std::string topic = topic_of(e, l, slot);
  if (json::get_string(*f, "status") == "never") return err(Errc::Auth, e.say("profile_never_blocked", Json{{"topic", topic}, {"slot", slot}}));
  if (f->contains("setting_key")) return err(Errc::InvalidArgument, e.say("profile_set_level_slot", Json{{"slot", slot}, {"setting", json::get_string(*f, "setting_key")}}));
  const std::string cid = gen_id("iv_");
  // The person typed it and confirmed through the gate: answer and confirmation in one, provenance user_stated; the history is the store's own.
  LOOM_TRY(write(e, l, Json{{"op", "answer"}, {"id", cid}, {"field", slot}, {"value", *given}, {"provenance", "user_stated"}}));
  LOOM_TRY(write(e, l, Json{{"op", "review"}, {"candidate", cid}, {"decision", "confirmed"}}));
  Json data = base_data(l);
  data["written"] = true;
  data["slot"] = slot;
  data["provenance"] = "user_stated";
  return Reply{e.say("profile_set_done", Json{{"topic", topic}, {"value", value_text(*given)}}), data};
}

Result<Reply> confirm(const Env& e, const Json& args) {
  LOOM_TRY_ASSIGN(auto l, load(e, args, false));
  const std::string kind = json::get_string(args, "kind");
  Json data = base_data(l);
  if (kind == "section") {
    const std::string section = section_for_review(l, args);
    const std::string title = section_title(e, l, section);
    const std::string decision = json::get_string(args, "decision", "confirmed");
    if (decision != "confirmed" && decision != "rejected") return err(Errc::InvalidArgument, e.say("profile_bad_decision", Json::object()));
    const Json pending = pending_in(l, section);
    if (pending.empty()) return err(Errc::NotFound, e.say("profile_review_none", Json{{"section", title}}));
    for (const auto& p : pending)
      LOOM_TRY(write(e, l, Json{{"op", "review"}, {"candidate", p.value("candidate", "")}, {"decision", decision}}));
    LOOM_TRY(write(e, l, Json{{"op", "confirm_section"}, {"section", section}, {"decision", decision}}));
    data["revision"] = std::to_string(l.rev);
    data["written"] = true;
    data["section"] = section;
    data["decision"] = decision;
    data["count"] = pending.size();
    return Reply{e.say(decision == "confirmed" ? "profile_section_confirmed" : "profile_section_rejected", Json{{"section", title}, {"count", pending.size()}}), data};
  }
  if (kind == "remark") {
    const std::string text = json::get_string(args, "text");
    if (text.empty()) return err(Errc::InvalidArgument, e.say("profile_no_text", Json::object()));
    LOOM_TRY_ASSIGN(auto level, level_of(e));
    // Read again against the profile as it is now: what is written is what matches now, and the yes covered what the proposal said.
    LOOM_TRY_ASSIGN(auto remark, view(e, l, Json{{"what", "remark"}, {"text", text}, {"level", level}}));
    if (remark.value("matched", Json::array()).empty()) return err(Errc::NotFound, e.say("profile_confirm_nothing", Json::object()));
    if (!remark.value("conflicts", Json::array()).empty()) return err(Errc::Conflict, e.say("profile_remark_conflict", Json::object()));
    std::string out;
    Json applied = Json::array(), erased = Json::array();
    const Json proposals = remark.value("proposals", Json::array());
    for (const auto& p : proposals) {
      if (json::get_string(p, "kind") == "status") {
        const Json fields = p.value("fields", Json::array());
        const std::string status = json::get_string(p, "status");
        for (const auto& fid : fields) {
          LOOM_TRY(write(e, l, Json{{"op", "status"}, {"status", status}, {"field", fid}, {"origin", "remark"}, {"rule", p.value("rule", "")}}));
          applied.push_back(Json{{"field", fid}, {"status", status}, {"rule", p.value("rule", "")}});
        }
        const Json erases = p.value("erases", Json::array());
        for (const auto& fid : erases) erased.push_back(fid);
        if (!fields.empty())
          out += (out.empty() ? "" : "\n") + e.say(status == "never" ? "profile_confirmed_never" : "profile_confirmed_status",
                                                  Json{{"fields", fields_text(e, l, fields)}, {"count", fields.size()}});
      } else if (!p.value("at_limit", false) && p.contains("to") && p["to"].is_string()) {
        if (auto st = e.set_setting("profile_detail_level", p["to"]); !st)
          return err(st.error().code, e.say("profile_level_not_stored", Json{{"level", json::get_string(p, "to")}, {"setting", "profile_detail_level"}, {"reason", st.error().message}}));
        applied.push_back(Json{{"setting", "profile_detail_level"}, {"from", p.value("from", Json(nullptr))}, {"to", p["to"]}});
        out += (out.empty() ? "" : "\n") + e.say("profile_confirmed_level", Json{{"from", json::get_string(p, "from")}, {"to", json::get_string(p, "to")}});
      }
    }
    if (!erased.empty()) out += "\n" + e.say("profile_erased", Json{{"fields", fields_text(e, l, erased)}});
    data["revision"] = std::to_string(l.rev);
    data["written"] = !applied.empty();
    data["applied"] = applied;
    data["erased"] = erased;
    if (out.empty()) out = e.say("profile_remark_already", Json::object());
    return Reply{out, data};
  }
  return err(Errc::InvalidArgument, e.say("profile_confirm_kind", Json{{"kind", kind}}));
}

}  // namespace loom::actions::profile
