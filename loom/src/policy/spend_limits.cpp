// Spending limits per period at the central admission of the usage ledger (P18; header: loom/usage_spend.h).
#include "loom/usage_spend.h"

#include <cmath>
#include <cstdio>
#include <iterator>
#include <set>

#include "loom/money_format.h"
#include "loom/runtime_profile.h"

namespace loom {
namespace {

// What marks an admission as a model use when the data does not say otherwise.
const char* const kDefaultFields[] = {"model"};
const char* const kDefaultResources[] = {"requests", "input_tokens", "output_tokens", "cost_usd", "money_usd"};
const char* const kDefaultCostResources[] = {"cost_usd", "money_usd"};

std::set<std::string> names_of(const Json& v, const char* const* defaults, std::size_t n) {
  std::set<std::string> out;
  if (v.is_array()) {
    for (const auto& s : v)
      if (s.is_string()) out.insert(s.get<std::string>());
    return out;
  }
  for (std::size_t i = 0; i < n; ++i) out.insert(defaults[i]);
  return out;
}

std::string money_text(double v, const Json& sl) {
  return format_money(v, sl.value("money_format", Json(nullptr)));
}

std::string amount_text(double v) {
  char buf[48];
  if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e12) std::snprintf(buf, sizeof(buf), "%.0f", v);
  else std::snprintf(buf, sizeof(buf), "%g", v);
  return buf;
}

std::string stamp(std::time_t t) {
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

std::string render(const std::string& templ, const Json& vars) {
  auto r = render_profile_template(templ, vars);
  return r ? *r : templ;
}

// The languages a text id has.
void add_languages(const Json& texts, const char* id, std::set<std::string>& out) {
  const Json* t = json::find(texts, id);
  if (!t || !t->is_object()) return;
  for (auto it = t->begin(); it != t->end(); ++it)
    if (it->is_string()) out.insert(it.key());
}

std::string text_in(const Json& texts, const char* id, const std::string& lang) {
  const Json* t = json::find(texts, id);
  if (!t || !t->is_object()) return std::string();
  if (const Json* s = json::find(*t, lang); s && s->is_string()) return s->get<std::string>();
  if (const Json* s = json::find(*t, "en"); s && s->is_string()) return s->get<std::string>();
  return std::string();
}

std::string label_in(const Json& labels, const std::string& period, const std::string& lang) {
  const Json* p = json::find(labels, period);
  if (p && p->is_object()) {
    if (const Json* s = json::find(*p, lang); s && s->is_string()) return s->get<std::string>();
    if (const Json* s = json::find(*p, "en"); s && s->is_string()) return s->get<std::string>();
  }
  return period;
}

bool finite_positive(const Json& v) { return v.is_number() && std::isfinite(v.get<double>()) && v.get<double>() > 0; }

}  // namespace

UsageSpendWindow usage_spend_window(std::string_view kind, std::time_t now) {
  if (now == 0) now = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&now, &tm);
  std::tm midnight = tm;
  midnight.tm_hour = midnight.tm_min = midnight.tm_sec = 0;
  std::time_t from = timegm(&midnight);
  if (kind == "week") {
    const int since_monday = (tm.tm_wday + 6) % 7;
    from -= static_cast<std::time_t>(since_monday) * 86400;
  } else if (kind == "month") {
    std::tm first = midnight;
    first.tm_mday = 1;
    from = timegm(&first);
  }
  return UsageSpendWindow{stamp(from), stamp(now + 1)};
}

Status validate_spend_limits(const Json& sl) {
  if (!sl.is_object()) return Error(Errc::InvalidArgument, "spend_limits must be an object");
  if (sl.contains("limits")) {
    const auto& limits = sl["limits"];
    if (!limits.is_array()) return Error(Errc::InvalidArgument, "spend_limits.limits must be an array");
    for (const auto& l : limits) {
      if (!l.is_object() || json::get_string(l, "period").empty())
        return Error(Errc::InvalidArgument, "a spending limit needs a period");
      const std::string kind = json::get_string(l, "kind");
      if (kind != "today" && kind != "week" && kind != "month")
        return Error(Errc::InvalidArgument, "a spending limit's period kind is today, week or month");
      const std::string on_exceed = json::get_string(l, "on_exceed");
      if (on_exceed != "confirm" && on_exceed != "deny" && on_exceed != "notify")
        return Error(Errc::InvalidArgument, "a spending limit's on_exceed is confirm, deny or notify");
      if (!l.contains("limit_usd") || !finite_positive(l["limit_usd"]))
        return Error(Errc::InvalidArgument, "a spending limit needs a positive limit_usd");
    }
  }
  for (const char* key : {"cost_resources"}) {
    if (sl.contains(key) && !sl[key].is_array()) return Error(Errc::InvalidArgument, std::string("spend_limits.") + key + " must be an array");
  }
  if (sl.contains("model_use") && !sl["model_use"].is_object())
    return Error(Errc::InvalidArgument, "spend_limits.model_use must be an object");
  for (const char* key : {"texts", "period_labels"}) {
    if (sl.contains(key) && !sl[key].is_object()) return Error(Errc::InvalidArgument, std::string("spend_limits.") + key + " must be an object");
  }
  if (sl.contains("spent_resource") && !sl["spent_resource"].is_string())
    return Error(Errc::InvalidArgument, "spend_limits.spent_resource must be a string");
  return {};
}

bool spend_model_use(const Json& sl, const Json& estimate) {
  const Json model_use = sl.value("model_use", Json::object());
  const auto fields = names_of(model_use.value("estimate_fields", Json(nullptr)), kDefaultFields, std::size(kDefaultFields));
  const auto resources = names_of(model_use.value("resources", Json(nullptr)), kDefaultResources, std::size(kDefaultResources));
  for (const auto& f : fields) {
    const Json* v = json::find(estimate, f);
    if (v && v->is_string() && !v->get<std::string>().empty()) return true;
  }
  const Json* r = json::find(estimate, "resources");
  if (r && r->is_object())
    for (const auto& name : resources)
      if (r->contains(name)) return true;
  return false;
}

Result<SpendCheck> check_spend_limits(const Json& sl, const Json& estimate, const SpendLookup& spent, std::time_t now) {
  SpendCheck out;
  const Json* limits = json::find(sl, "limits");
  if (!limits || !limits->is_array() || limits->empty()) return out;
  if (!spend_model_use(sl, estimate)) return out;
  // The cost the caller knows for this call (null/absent is unknown, not zero: then only what was already spent is counted).
  double estimate_usd = 0;
  bool estimate_known = false;
  const auto cost_names = names_of(sl.value("cost_resources", Json(nullptr)), kDefaultCostResources, std::size(kDefaultCostResources));
  if (const Json* r = json::find(estimate, "resources"); r && r->is_object())
    for (const auto& name : cost_names) {
      const Json* v = json::find(*r, name);
      if (v && v->is_number() && std::isfinite(v->get<double>()) && v->get<double>() >= 0) {
        estimate_usd = v->get<double>();
        estimate_known = true;
        break;
      }
    }
  const Json texts = sl.value("texts", Json::object());
  const Json labels = sl.value("period_labels", Json::object());
  std::set<std::string> languages;
  for (const char* id : {"already_over", "use_would_exceed"}) add_languages(texts, id, languages);
  if (languages.empty()) languages.insert("en");
  for (const auto& l : *limits) {
    const std::string period = json::get_string(l, "period");
    const double limit_usd = json::get_number(l, "limit_usd", 0.0);
    const UsageSpendWindow w = usage_spend_window(json::get_string(l, "kind"), now);
    LOOM_TRY_ASSIGN(Json s, spent(w.from, w.to));
    const double spent_usd = s.value("usd", 0.0);
    const bool cost_known = s.value("known", false);
    const double projected = spent_usd + estimate_usd;
    if (!(projected > limit_usd + 1e-12)) continue;
    const bool already = spent_usd > limit_usd + 1e-12;
    const std::string on_exceed = json::get_string(l, "on_exceed", "confirm");
    const double rate = json::get_number(l, "rate", 1.0) > 0 ? json::get_number(l, "rate", 1.0) : 1.0;
    Json reason = Json::object();
    for (const auto& lang : languages) {
      const Json vars{{"period", label_in(labels, period, lang)},
                      {"amount", amount_text(json::get_number(l, "amount", limit_usd / rate))},
                      {"currency", json::get_string(l, "currency", "USD")},
                      {"spent", money_text(spent_usd / rate, sl)},
                      {"estimate", money_text(estimate_usd / rate, sl)}};
      std::string body = render(text_in(texts, already ? "already_over" : "use_would_exceed", lang), vars);
      if (!estimate_known && !already) {
        const std::string unknown = text_in(texts, "use_estimate_unknown", lang);
        if (!unknown.empty()) body += " " + render(unknown, vars);
      }
      if (on_exceed == "deny") {
        const std::string deny = text_in(texts, "on_deny", lang);
        if (!deny.empty()) body += " " + render(deny, vars);
      }
      if (!body.empty()) reason[lang] = body;
    }
    const std::string code = already ? "spend_limit_already_over" : "spend_limit_would_exceed";
    Json row{{"period", period}, {"kind", json::get_string(l, "kind")}, {"on_exceed", on_exceed}, {"code", code},
             {"limit", Json{{"amount", json::get_number(l, "amount", limit_usd / rate)}, {"currency", json::get_string(l, "currency", "USD")},
                            {"rate", rate}, {"rate_source", json::get_string(l, "rate_source")}}},
             {"limit_usd", limit_usd}, {"spent_usd", spent_usd}, {"spent_cost_known", cost_known},
             {"estimate_usd", estimate_known ? Json(estimate_usd) : Json(nullptr)}, {"projected_usd", projected},
             {"already_over", already}, {"window", Json{{"from", w.from}, {"to", w.to}}}, {"reason", reason}};
    if (on_exceed == "notify") {
      Json notice = Json::object();
      for (auto it = reason.begin(); it != reason.end(); ++it) {
        const std::string wrap = text_in(texts, "on_notify", it.key());
        notice[it.key()] = wrap.empty() ? it->get<std::string>() : render(wrap, Json{{"what", it->get<std::string>()}});
      }
      out.notices.push_back(Json{{"code", "spend_limit_notice"}, {"period", period}, {"limit_code", code}, {"text", notice}});
    } else {
      out.holds.push_back(Json{{"period", period}, {"on_exceed", on_exceed}, {"limit_usd", limit_usd}});
    }
    out.report.push_back(std::move(row));
  }
  return out;
}

Json usage_limit_reasons(const Json& decision) {
  const Json* holds = json::find(decision, "limits");
  if (!holds || !holds->is_array() || holds->empty()) return Json::object();
  Json limits = Json::array();
  if (const Json* report = json::find(decision, "limit_report"); report && report->is_array())
    for (const auto& row : *report)
      if (json::get_string(row, "on_exceed") != "notify") limits.push_back(row);
  // A record without the report (an older reader) still says that, and which, limit holds.
  if (limits.empty())
    for (const auto& h : *holds) limits.push_back(Json{{"period", json::get_string(h, "period")}, {"on_exceed", json::get_string(h, "on_exceed")},
                                                       {"code", "spend_limit"}, {"reason", Json::object()}});
  return Json{{"limits", limits}, {"reason_codes", decision.value("reason_codes", Json::array({"spend_limit"}))},
              {"status", json::get_string(decision, "status")}};
}

}  // namespace loom
