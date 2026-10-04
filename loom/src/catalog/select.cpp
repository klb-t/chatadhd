// catalog.h: Catalog::select / set_override — fixed precedence (proposal
// §6): 1. owner override (loom_cat_overrides, always wins) 2.
// policy/selection_rules.json in order, first match wins 3. the label
// default (relevant -> include, candidate -> review i.e. not auto-selected,
// irrelevant -> exclude). Every decision records decided_by and reasons.
#include "loom/catalog.h"

#include <regex>

#include "catalog_internal.h"
#include "loom/db.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/time.h"

namespace loom::catalog {

using namespace loom::catalog::internal;

namespace {

struct UnitRow {
  std::string unit_id;
  double score = 0.0;
  std::string label;
  Json projects = Json::array();
  Json reasons = Json::array();
  Json features = Json::object();
  std::string title;
  std::string lang;
  std::int64_t n_chars = 0;
  std::string date;
};

// Matches one selection_rules.json rule's "match" object against a scored
// unit. Supported keys: label, project, score_min, score_max, lang,
// min_chars, max_chars, title_regex. date_from/date_to/file_glob are not yet
// implemented (disclosed gap; the shipped default rules do not use them).
bool rule_matches(const Json& match, const UnitRow& u) {
  if (const Json* v = json::find(match, "label"); v && v->is_string() && v->get<std::string>() != u.label) return false;
  if (const Json* v = json::find(match, "project"); v && v->is_string()) {
    std::string want = v->get<std::string>();
    bool found = false;
    for (const auto& p : u.projects) {
      if (p.is_string() && p.get<std::string>() == want) {
        found = true;
        break;
      }
    }
    // "owner" = a philosophy/principle-probe hit with no specific project
    // (AliasIndex::find tags those mentions' key "owner"; alias.cpp).
    if (!found) return false;
  }
  if (const Json* v = json::find(match, "score_min"); v && v->is_number() && u.score < v->get<double>()) return false;
  if (const Json* v = json::find(match, "score_max"); v && v->is_number() && u.score > v->get<double>()) return false;
  if (const Json* v = json::find(match, "lang"); v && v->is_string() && v->get<std::string>() != u.lang) return false;
  if (const Json* v = json::find(match, "min_chars"); v && v->is_number() && u.n_chars < v->get<std::int64_t>()) return false;
  if (const Json* v = json::find(match, "max_chars"); v && v->is_number() && u.n_chars > v->get<std::int64_t>()) return false;
  if (const Json* v = json::find(match, "title_regex"); v && v->is_string()) {
    try {
      if (!std::regex_search(u.title, std::regex(v->get<std::string>(), std::regex::icase))) return false;
    } catch (const std::exception&) {
      return false;
    }
  }
  return true;
}

}  // namespace

Result<std::vector<Decision>> Catalog::select(std::string_view score_run) {
  LOOM_TRY(ensure_schema(rt_.db()));
  auto lk = rt_.db().lock();
  sql::Connection& c = rt_.db().conn();

  std::string run_id(score_run);
  if (run_id.empty()) {
    auto latest = c.query_text("SELECT run_id FROM loom_cat_scores ORDER BY rowid DESC LIMIT 1");
    if (!latest) return latest.error();
    if (!*latest) return Error(Errc::NotFound, "no score run yet; call score() first");
    run_id = **latest;
  }

  std::vector<UnitRow> rows;
  {
    LOOM_TRY_ASSIGN(sql::Stmt st,
                    c.prepare("SELECT s.unit_id, s.score, s.label, s.projects, s.reasons, u.title, u.lang, u.bytes, "
                              "u.date, u.body, s.features FROM loom_cat_scores s JOIN loom_cat_units u ON u.id = s.unit_id "
                              "WHERE s.run_id = ?"));
    st.bind(1, run_id);
    while (true) {
      LOOM_TRY_ASSIGN(bool has, st.step());
      if (!has) break;
      UnitRow u;
      u.unit_id = st.get_text(0);
      u.score = st.get_double(1);
      u.label = st.get_text(2);
      if (auto j = json::parse(st.get_text(3))) u.projects = *j;
      if (auto j = json::parse(st.get_text(4))) u.reasons = *j;
      u.title = st.get_text(5);
      u.lang = st.get_text(6);
      u.date = st.get_text(8);
      if (auto body = json::parse(st.get_text(9)); body) u.n_chars = json::get_int(*body, "n_chars");
      if (auto features = json::parse(st.get_text(10)); features) u.features = *features;
      rows.push_back(std::move(u));
    }
  }

  const Json& rules_doc = pack_->policy("selection_rules");
  const Json* rules = json::find(rules_doc, "rules");

  std::vector<Decision> decisions;
  decisions.reserve(rows.size());
  for (const auto& u : rows) {
    Decision d;
    d.unit_id = u.unit_id;
    d.label = u.label;
    d.score = u.score;
    for (const auto& p : u.projects) if (p.is_string()) d.reasons.push_back(Json{{"project", p}});
    if (const auto* evidence = json::find(u.features, "external_semantic_evidence")) {
      d.reasons.push_back(Json{{"semantic_candidate", *evidence},
                               {"fusion", json::get_string(u.features, "external_semantic_fusion")},
                               {"meets_channel_threshold", json::get_bool(u.features, "external_semantic_relevant")}});
    }

    auto override_action = c.query_text("SELECT action FROM loom_cat_overrides WHERE unit_id = ?", u.unit_id);
    if (!override_action) return override_action.error();
    if (*override_action) {
      d.selected = (**override_action == "include" || **override_action == "pin");
      d.decided_by = "user";
      d.reasons.push_back(Json{{"override", **override_action}});
    } else {
      bool matched = false;
      if (rules && rules->is_array()) {
        for (const auto& r : *rules) {
          const Json* match = json::find(r, "match");
          if (!match || !rule_matches(*match, u)) continue;
          std::string action = json::get_string(r, "action");
          d.decided_by = "rule:" + json::get_string(r, "id");
          d.selected = (action == "include");
          d.reasons.push_back(Json{{"rule", json::get_string(r, "id")}, {"why", json::get_string(r, "why")}});
          matched = true;
          break;
        }
      }
      if (!matched) {
        d.decided_by = "score";
        d.selected = (u.label == "relevant");
      }
    }
    LOOM_TRY(c.run(
        "INSERT OR REPLACE INTO loom_cat_decisions (run_id, unit_id, selected, decided_by, label, score, reasons) "
        "VALUES (?,?,?,?,?,?,?)",
        run_id, d.unit_id, d.selected ? 1 : 0, d.decided_by, d.label, d.score, json::dump(d.reasons)));
    decisions.push_back(std::move(d));
  }
  return decisions;
}

Status Catalog::set_override(const Override& o) {
  LOOM_TRY(ensure_schema(rt_.db()));
  auto lk = rt_.db().lock();
  auto next_seq = rt_.db().conn().query_int("SELECT COALESCE(MAX(seq), 0) + 1 FROM loom_cat_overrides");
  if (!next_seq) return next_seq.error();
  return rt_.db().conn().run(
      "INSERT OR REPLACE INTO loom_cat_overrides (unit_id, action, reason, seq, created) VALUES (?,?,?,?,?)",
      o.unit_id, o.action, o.reason, **next_seq, timeutil::utc_now_iso());
}

}  // namespace loom::catalog
