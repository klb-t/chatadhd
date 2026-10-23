#include "import/import_truth.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "loom/log.h"
#include "loom/runtime_profile.h"

namespace loom::import_truth {

namespace {

constexpr std::string_view kLog = "loom.import.truth";

// A condition of the data (`when`) against the facts of one element. An unknown key never matches (data from a later version is not guessed at).
bool matches(const Json& when, const Json& facts) {
  if (!when.is_object()) return false;
  const auto number = [&](const char* name) -> std::optional<std::int64_t> {
    const Json* v = json::find(facts, name);
    if (!v || !v->is_number_integer()) return std::nullopt;
    return v->get<std::int64_t>();
  };
  for (auto it = when.begin(); it != when.end(); ++it) {
    const std::string& key = it.key();
    const Json& want = it.value();
    if (key == "path" || key == "provider") {
      const std::string have = json::get_string(facts, key);
      bool hit = false;
      if (want.is_string()) {
        hit = want.get<std::string>() == have;
      } else if (want.is_array()) {
        for (const auto& w : want) hit = hit || (w.is_string() && w.get<std::string>() == have);
      }
      if (!hit) return false;
    } else if (key == "archive" || key == "cancelled" || key == "partial_import") {
      if (!want.is_boolean() || json::get_bool(facts, key) != want.get<bool>()) return false;
    } else if (key == "found_min" || key == "imported_min" || key == "imported_max" || key == "missing_min" || key == "files_min" || key == "unreferenced_min") {
      if (!want.is_number_integer()) return false;
      const bool upper = key.size() > 4 && key.compare(key.size() - 4, 4, "_max") == 0;
      const std::string fact = key.substr(0, key.find('_'));
      const auto have = number(fact.c_str());
      if (!have) return false;  // a fact the reader does not know (found = null) satisfies no numeric condition
      const std::int64_t bound = want.get<std::int64_t>();
      if (upper ? *have > bound : *have < bound) return false;
    } else {
      log::write(log::Level::Debug, kLog, "import_truth: unknown condition '" + key + "' does not match");
      return false;
    }
  }
  return true;
}

std::string text_in(const Json& words, const std::string& lang, const char* id) {
  const Json* table = json::find(words, "texts");
  const Json* l = table ? json::find(*table, lang) : nullptr;
  return l ? json::get_string(*l, id) : std::string();
}

std::string render(const std::string& templ, const Json& vars) {
  if (templ.empty()) return templ;
  auto out = render_profile_template(templ, vars);
  return out ? *out : templ;
}

Json rendered_both(const Json& texts, const Json& vars) {
  Json out = Json::object();
  for (const char* lang : {"pl", "en"}) out[lang] = render(json::get_string(texts, lang), vars);
  return out;
}

}  // namespace

Json build(const ImportResult& r, const Json& words) {
  const bool has_report = r.export_report.is_object();
  const Json report = has_report ? r.export_report : Json::object();
  const Json counts = report.value("counts", Json::object());
  const bool exported = has_report;
  const bool cancelled = r.cancelled;
  const bool partial = has_report && report.value("partial", false);
  const std::string provider = has_report ? json::get_string(report, "provider") : std::string();
  const bool archive = r.format == "zip";
  const std::int64_t files = has_report && report.value("asset_members", Json::array()).is_array() ? static_cast<std::int64_t>(report.value("asset_members", Json::array()).size()) : 0;
  const std::int64_t unreferenced = has_report && report.value("unreferenced_members", Json::array()).is_array()
                                        ? static_cast<std::int64_t>(report.value("unreferenced_members", Json::array()).size()) : 0;
  const Json report_errors = report.value("errors", Json::array());
  const std::int64_t errors = report_errors.is_array() ? static_cast<std::int64_t>(report_errors.size()) : 0;
  // The "already imported" marker is a warning the importer adds itself; it is not a problem with the data.
  const std::int64_t warnings = std::max<std::int64_t>(0, static_cast<std::int64_t>(r.warnings.size()) - (r.already_imported ? 1 : 0));

  Json out = Json::object();
  out["schema"] = "loom.import_truth/1";
  out["path"] = exported ? "export" : "legacy";
  out["provider"] = provider;
  out["archive"] = archive;
  out["already_imported"] = r.already_imported;
  out["cancelled"] = cancelled;
  out["partial"] = partial;
  out["warnings"] = warnings;
  out["errors"] = errors;

  const Json elements = words.is_object() ? words.value("elements", Json::array()) : Json::array();
  const Json reasons = words.is_object() ? words.value("reasons", Json::array()) : Json::array();
  const Json notes = words.is_object() ? words.value("notes", Json::array()) : Json::array();
  Json rows = Json::array();
  struct Shown {
    bool show = false;
    std::string kind;
    Json facts;
    std::string status;
    Json reason = nullptr;
    Json notes = Json::array();
    Json label = Json::object();
  };
  std::vector<Shown> shown;
  for (const auto& el : elements) {
    const std::string kind = json::get_string(el, "kind");
    const bool always = json::get_bool(el, "always");
    std::optional<std::int64_t> found;
    std::int64_t imported = 0;
    const auto result_number = [&](const std::string& from) -> std::optional<std::int64_t> {
      if (from == "result_conversations") return static_cast<std::int64_t>(r.conversations.size());
      if (from == "result_messages") return r.messages;
      return std::nullopt;
    };
    if (exported) {
      found = json::get_int(counts, json::get_string(el, "counts"));
      const std::string linked = json::get_string(el, "linked");
      imported = linked.empty() ? *found : json::get_int(counts, linked);
      if (auto n = result_number(json::get_string(el, "imported_from"))) imported = *n;
    } else {
      const std::string legacy = json::get_string(el, "legacy");
      if (legacy.empty()) continue;  // a kind only the provider-export reader knows
      if (auto n = result_number(legacy)) {
        found = n;
        imported = *n;
      }  // "unread": the reader does not read this kind: found stays null, imported 0
    }
    Shown s;
    s.kind = kind;
    s.show = always || found.value_or(0) > 0;
    s.label = el.value("label", Json::object());
    if (!found) s.status = "not_counted";
    else if (*found == 0 && imported == 0) s.status = "empty";
    else if (imported == *found) s.status = "all";
    else if (imported == 0) s.status = "none";
    else s.status = "some";
    s.facts = Json{{"path", exported ? "export" : "legacy"}, {"provider", provider}, {"archive", archive}, {"cancelled", cancelled},
                   {"partial_import", partial}, {"imported", imported}, {"files", files}, {"unreferenced", unreferenced}};
    if (found) {
      s.facts["found"] = *found;
      s.facts["missing"] = std::max<std::int64_t>(0, *found - imported);
    }
    if (s.status != "all" && s.status != "empty") {
      for (const auto& reason : reasons) {
        const std::string for_kind = json::get_string(reason, "kind");
        if ((for_kind == kind || for_kind == "*") && matches(reason.value("when", Json::object()), s.facts)) {
          s.reason = reason;
          break;
        }
      }
    }
    for (const auto& note : notes)
      if (json::get_string(note, "kind") == kind && matches(note.value("when", Json::object()), s.facts)) s.notes.push_back(note);
    Json row = Json{{"kind", kind}, {"label", s.label}, {"found", found ? Json(*found) : Json(nullptr)}, {"imported", imported}, {"status", s.status}};
    if (!s.reason.is_null()) {
      Json reason = Json{{"id", json::get_string(s.reason, "id")}, {"text", rendered_both(s.reason.value("text", Json::object()), s.facts)}};
      if (const Json* observed = json::find(s.reason, "observed")) reason["observed"] = *observed;
      row["reason"] = reason;
    } else {
      row["reason"] = nullptr;
    }
    Json row_notes = Json::array();
    for (const auto& n : s.notes) row_notes.push_back(Json{{"id", json::get_string(n, "id")}, {"text", rendered_both(n.value("text", Json::object()), s.facts)}});
    row["notes"] = row_notes;
    row["shown"] = s.show;
    rows.push_back(row);
  }
  out["elements"] = rows;

  if (!words.is_object() || words.value("texts", Json::object()).empty()) return out;
  // One sentence per language from the rows, nothing but the data's templates.
  Json summary = Json::object();
  for (const char* lang : {"pl", "en"}) {
    std::vector<std::string> parts;
    parts.push_back(text_in(words, lang, r.already_imported ? "head_cached" : "head_done"));
    for (const auto& row : rows) {
      if (!row.value("shown", false)) continue;
      const std::string status = json::get_string(row, "status");
      Json vars = Json{{"label", json::get_string(row.value("label", Json::object()), lang)}, {"imported", row.value("imported", 0)}};
      if (row.contains("found") && !row["found"].is_null()) vars["found"] = row["found"];
      std::string reason_part;
      if (row.contains("reason") && row["reason"].is_object())
        reason_part = render(text_in(words, lang, "reason_join"), Json{{"reason", json::get_string(row["reason"].value("text", Json::object()), lang)}});
      vars["reason"] = reason_part;
      const char* which = status == "not_counted" ? "element_uncounted" : (status == "all" || status == "empty") ? "element_all" : "element_short";
      parts.push_back(render(text_in(words, lang, which), vars));
      for (const auto& n : row.value("notes", Json::array())) parts.push_back(json::get_string(n.value("text", Json::object()), lang));
    }
    if (warnings > 0) parts.push_back(render(text_in(words, lang, "warnings"), Json{{"count", warnings}}));
    if (errors > 0) parts.push_back(render(text_in(words, lang, "errors"), Json{{"count", errors}}));
    std::string joined;
    const std::string sep = text_in(words, lang, "sentence_join");
    for (const auto& p : parts) {
      if (p.empty()) continue;
      joined += (joined.empty() ? "" : sep) + p;
    }
    summary[lang] = joined;
  }
  out["summary"] = summary;
  return out;
}

Json for_runtime(const ImportResult& result, const std::filesystem::path& data_root) {
  auto profile = RuntimeProfile::load("chat_ops", data_root);
  if (!profile) {
    log::write(log::Level::Debug, kLog, "chat_ops not readable: " + profile.error().message);
    Json out = build(result, Json(nullptr));
    out["words_unavailable"] = true;
    return out;
  }
  const Json words = profile->values().value("import_truth", Json::object());
  return build(result, words);
}

}  // namespace loom::import_truth
