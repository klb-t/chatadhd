// Spending limits per period for action runs. See include/loom/action_spend.h.
#include "loom/action_spend.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "loom/log.h"
#include "loom/runtime_profile.h"
#include "loom/usage_policy.h"
#include "loom/util/utf8.h"

namespace loom::actions::spend {

namespace {

std::vector<std::string> strings_of(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

std::string lower(std::string_view s) { return utf8::to_lower(s); }

// A folded word against a list of words; a trailing * is any continuation.
bool word_in(const std::vector<std::string>& words, const std::string& token) {
  for (const auto& w : words) {
    if (w.empty()) continue;
    if (w.back() == '*') {
      if (token.rfind(w.substr(0, w.size() - 1), 0) == 0) return true;
    } else if (token == w) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> tokens_of(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] {
    if (!cur.empty()) out.push_back(cur);
    cur.clear();
  };
  for (char c : lower(text)) {
    if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == ';' || c == '/') flush();
    else cur += c;
  }
  flush();
  return out;
}

const Json* find_by_id(const Json& list, const std::string& id) {
  if (list.is_array())
    for (const auto& x : list)
      if (json::get_string(x, "id") == id) return &x;
  return nullptr;
}

std::string stamp(std::time_t t) {
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

std::string money_text(double v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.2f", v);
  if (v > 0 && std::string(buf) == "0.00") std::snprintf(buf, sizeof(buf), "%.4f", v);
  return buf;
}

std::string amount_text(double v) {
  char buf[48];
  if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e12) std::snprintf(buf, sizeof(buf), "%.0f", v);
  else std::snprintf(buf, sizeof(buf), "%g", v);
  return buf;
}

std::string render(const std::string& templ, const Json& vars) {
  auto r = render_profile_template(templ, vars);
  return r ? *r : templ;
}

// The text of a {en, pl, ...} table rendered in every language it has.
Json rendered(const Json& texts, const std::string& id, const Json& vars_by_lang) {
  Json out = Json::object();
  const Json* t = json::find(texts, id);
  if (!t || !t->is_object()) return out;
  for (auto it = t->begin(); it != t->end(); ++it)
    if (it->is_string()) out[it.key()] = render(it->get<std::string>(), vars_by_lang.contains(it.key()) ? vars_by_lang[it.key()] : vars_by_lang.value("en", Json::object()));
  return out;
}

std::string pick(const Json& v, const std::string& lang) {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_object()) {
    for (const std::string& k : {lang, std::string("en")})
      if (const Json* s = json::find(v, k); s && s->is_string()) return s->get<std::string>();
  }
  return std::string();
}

const Json* currency_of(const Json& data, const std::string& code) {
  const Json* all = json::find(data, "currencies");
  if (!all || !all->is_object()) return nullptr;
  for (auto it = all->begin(); it != all->end(); ++it)
    if (lower(it.key()) == lower(code)) return &it.value();
  return nullptr;
}

std::string currency_code(const Json& data, const std::string& code) {
  const Json* all = json::find(data, "currencies");
  if (all && all->is_object())
    for (auto it = all->begin(); it != all->end(); ++it)
      if (lower(it.key()) == lower(code)) return it.key();
  std::string up;
  for (char c : code) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return up;
}

std::string setting_of(const Json& data) { return json::get_string(data, "setting", "usage_spend_limits"); }

std::vector<std::string> period_ids(const Json& data) {
  std::vector<std::string> out;
  if (const Json* p = json::find(data, "periods"); p && p->is_array())
    for (const auto& x : *p) out.push_back(json::get_string(x, "id"));
  return out;
}

Json entry_json(const Entry& e) {
  Json j = Json::object();
  j["period"] = e.period;
  j["amount"] = e.amount;
  j["currency"] = e.currency;
  j["on_exceed"] = e.on_exceed;
  j["rate"] = e.rate;
  j["rate_source"] = e.rate_source;
  return j;
}

std::optional<double> number_of(const Json& v) {
  double out = 0;
  if (v.is_number()) {
    out = v.get<double>();
  } else if (v.is_string()) {
    std::string s = v.get<std::string>();
    std::replace(s.begin(), s.end(), ',', '.');
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || (end && *end != '\0')) return std::nullopt;
  } else {
    return std::nullopt;
  }
  if (!std::isfinite(out)) return std::nullopt;
  return out;
}

// Fills the rate of an entry whose currency is known to the data (or carries its own).
Result<Entry> complete(const Json& data, Entry e, const Json& own_rate) {
  if (own_rate.is_number() && own_rate.get<double>() > 0 && std::isfinite(own_rate.get<double>())) {
    e.rate = own_rate.get<double>();
    e.rate_source = "user";
    return e;
  }
  if (const Json* c = currency_of(data, e.currency)) {
    e.rate = c->value("rate", 0.0);
    if (!(e.rate > 0)) return Error(Errc::InvalidArgument, "the currency '" + e.currency + "' has no rate; give one (USD per unit) as `rate`");
    e.rate_source = lower(e.currency) == "usd" ? "identity" : "preset";
    return e;
  }
  return Error(Errc::InvalidArgument, "the currency '" + e.currency + "' has no rate; give one (USD per unit) as `rate`");
}

}  // namespace

Json Entry::to_json() const { return entry_json(*this); }

Json data_of(const std::filesystem::path& root) {
  auto profile = RuntimeProfile::load("action_run", root);
  if (!profile) return Json::object();
  const Json* d = json::find(profile->values(), "spend_limits");
  return d && d->is_object() ? *d : Json::object();
}

std::string period_of_word(const Json& data, const std::string& folded_word) {
  if (const Json* periods = json::find(data, "periods"); periods && periods->is_array())
    for (const auto& p : *periods)
      if (word_in(strings_of(json::find(p, "words")), lower(folded_word))) return json::get_string(p, "id");
  return std::string();
}

std::string period_in(const Json& data, const std::string& folded_text) {
  for (const auto& token : tokens_of(folded_text))
    if (const std::string id = period_of_word(data, token); !id.empty()) return id;
  return std::string();
}

std::string period_label(const Json& data, const std::string& period, const std::string& lang) {
  const Json periods = data.value("periods", Json::array());
  const Json* p = find_by_id(periods, period);
  return p ? pick(p->value("label", Json::object()), lang) : period;
}

std::string on_exceed_label(const Json& data, const std::string& on_exceed, const std::string& lang) {
  const Json list = data.value("on_exceed", Json::array());
  const Json* p = find_by_id(list, on_exceed);
  return p ? pick(p->value("label", Json::object()), lang) : on_exceed;
}

Stored stored(const Config& config, const Json& data) {
  Stored out;
  const Json raw = config.get(setting_of(data), Json::array());
  if (raw.is_null()) return out;
  if (!raw.is_array()) {
    out.issues.push_back(Json{{"index", -1}, {"reason", "the setting is not a list"}});
    return out;
  }
  const auto periods = period_ids(data);
  const Json on_exceed_list = data.value("on_exceed", Json::array());
  std::size_t index = 0;
  for (const auto& item : raw) {
    const std::size_t at = index++;
    auto bad = [&](const std::string& why) { out.issues.push_back(Json{{"index", at}, {"reason", why}}); };
    if (!item.is_object()) {
      bad("not an object");
      continue;
    }
    Entry e;
    e.period = json::get_string(item, "period");
    if (std::find(periods.begin(), periods.end(), e.period) == periods.end()) {
      bad("unknown period '" + e.period + "'");
      continue;
    }
    const auto amount = item.contains("amount") ? number_of(item["amount"]) : std::nullopt;
    if (!amount || !(*amount > 0)) {
      bad("amount must be a positive number");
      continue;
    }
    e.amount = *amount;
    e.on_exceed = json::get_string(item, "on_exceed", json::get_string(data, "default_on_exceed", "confirm"));
    if (!find_by_id(on_exceed_list, e.on_exceed)) {
      bad("unknown on_exceed '" + e.on_exceed + "'");
      continue;
    }
    e.currency = currency_code(data, json::get_string(item, "currency", json::get_string(data, "default_currency", "USD")));
    auto done = complete(data, e, item.value("rate", Json(nullptr)));
    if (!done) {
      bad(done.error().message);
      continue;
    }
    if (item.contains("rate_source") && item["rate_source"].is_string() && item.value("rate", Json(nullptr)).is_number())
      done->rate_source = item["rate_source"].get<std::string>();
    out.entries.push_back(*done);
  }
  return out;
}

Result<Entry> entry_from(const Json& data, const Json& args) {
  Entry e;
  const auto amount = args.contains("amount") ? number_of(args["amount"]) : std::nullopt;
  if (!amount || !(*amount > 0)) return Error(Errc::InvalidArgument, "the amount of a limit must be a positive number");
  e.amount = *amount;
  // the period: a word naming it
  for (const auto& token : tokens_of(json::get_string(args, "period"))) {
    e.period = period_of_word(data, token);
    if (!e.period.empty()) break;
  }
  if (e.period.empty()) {
    std::string known;
    for (const auto& id : period_ids(data)) known += (known.empty() ? "" : ", ") + id;
    return Error(Errc::InvalidArgument, "the period of a limit is one of: " + known);
  }
  e.on_exceed = json::get_string(data, "default_on_exceed", "confirm");
  if (const std::string said = json::get_string(args, "on_exceed"); !said.empty()) {
    std::string found;
    if (const Json* list = json::find(data, "on_exceed"); list && list->is_array())
      for (const auto& token : tokens_of(said))
        for (const auto& row : *list)
          if (found.empty() && (lower(token) == json::get_string(row, "id") || word_in(strings_of(json::find(row, "words")), token))) found = json::get_string(row, "id");
    if (found.empty()) return Error(Errc::InvalidArgument, "on_exceed is one of: confirm, deny, notify");
    e.on_exceed = found;
  }
  // the currency: a code or a word; none said = the preset's
  e.currency = currency_code(data, json::get_string(data, "default_currency", "USD"));
  if (const std::string said = json::get_string(args, "currency"); !said.empty()) {
    std::string found;
    if (const Json* all = json::find(data, "currencies"); all && all->is_object())
      for (const auto& token : tokens_of(said))
        for (auto it = all->begin(); it != all->end(); ++it)
          if (found.empty() && (lower(token) == lower(it.key()) || word_in(strings_of(json::find(it.value(), "words")), token))) found = it.key();
    if (found.empty()) {
      // A currency the data does not know is accepted only with a rate of the user's own.
      const Json rate = args.value("rate", Json(nullptr));
      if (!(rate.is_number() && rate.get<double>() > 0)) return Error(Errc::InvalidArgument, "the currency '" + said + "' is not in the preset; give its rate (USD per unit) as `rate`");
      found = currency_code(data, said);
    }
    e.currency = found;
  }
  return complete(data, std::move(e), args.value("rate", Json(nullptr)));
}

Result<Json> put(Config& config, const Json& data, const Entry& entry) {
  const std::string key = setting_of(data);
  Json raw = config.get(key, Json::array());
  if (!raw.is_array()) raw = Json::array();
  Json next = Json::array();
  Json previous = nullptr;
  for (const auto& item : raw) {
    if (item.is_object() && json::get_string(item, "period") == entry.period) {
      if (previous.is_null()) previous = item;
      continue;
    }
    next.push_back(item);
  }
  next.push_back(entry_json(entry));
  const auto order = period_ids(data);
  std::stable_sort(next.begin(), next.end(), [&](const Json& a, const Json& b) {
    auto at = [&](const Json& x) { return std::find(order.begin(), order.end(), json::get_string(x, "period")) - order.begin(); };
    return at(a) < at(b);
  });
  config.set(key, next);
  LOOM_TRY(config.save());
  return Json{{"written", true}, {"entry", entry_json(entry)}, {"previous", previous}, {"setting", key}};
}

Result<Json> clear(Config& config, const Json& data, const std::string& period) {
  const std::string key = setting_of(data);
  Json raw = config.get(key, Json::array());
  Json next = Json::array();
  Json previous = nullptr;
  if (raw.is_array())
    for (const auto& item : raw) {
      if (item.is_object() && json::get_string(item, "period") == period) {
        if (previous.is_null()) previous = item;
        continue;
      }
      next.push_back(item);
    }
  const bool removed = !previous.is_null();
  if (removed) {
    config.set(key, next);
    LOOM_TRY(config.save());
  }
  return Json{{"removed", removed}, {"previous", previous}, {"setting", key}};
}

Window window(const Json& data, const std::string& period, std::time_t now) {
  if (now == 0) now = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&now, &tm);
  std::tm midnight = tm;
  midnight.tm_hour = midnight.tm_min = midnight.tm_sec = 0;
  std::string kind = "today";
  if (const Json* p = json::find(data, "periods"); p && p->is_array())
    for (const auto& x : *p)
      if (json::get_string(x, "id") == period) kind = json::get_string(x, "kind", "today");
  std::time_t from = timegm(&midnight);
  if (kind == "week") {
    const int since_monday = (tm.tm_wday + 6) % 7;
    from -= static_cast<std::time_t>(since_monday) * 86400;
  } else if (kind == "month") {
    std::tm first = midnight;
    first.tm_mday = 1;
    from = timegm(&first);
  }
  return Window{stamp(from), stamp(now + 1)};
}

Result<Spent> spent_in(const std::filesystem::path& root, const Window& w) {
  Spent out;
  const auto path = root / "usage-policy.sqlite";
  // A question never creates the ledger: without one nothing was recorded.
  if (!std::filesystem::exists(path)) return out;
  out.ledger = true;
  LOOM_TRY_ASSIGN(auto policy, UsagePolicy::open(path, Json::object()));
  LOOM_TRY_ASSIGN(Json spent, policy->spend_between(w.from, w.to));
  out.operations = spent.value("operations", 0);
  const Json totals = spent.value("totals", Json::object());
  if (totals.contains("cost_usd") && totals["cost_usd"].is_number()) {
    out.cost_known = true;
    out.usd = totals["cost_usd"].get<double>();
  }
  return out;
}

Json status(const std::filesystem::path& root, const Config& config, const Json& data, std::time_t now) {
  Stored s = stored(config, data);
  Json items = Json::array();
  for (const auto& e : s.entries) {
    const Window w = window(data, e.period, now);
    Json row = entry_json(e);
    row["limit_usd"] = e.usd();
    row["from"] = w.from;
    row["to"] = w.to;
    auto spent = spent_in(root, w);
    row["spent_usd"] = nullptr;
    row["spent"] = nullptr;
    row["share"] = nullptr;
    row["operations"] = 0;
    if (!spent) {
      row["error"] = spent.error().message;
    } else {
      row["operations"] = spent->operations;
      row["ledger"] = spent->ledger;
      if (spent->cost_known) {
        row["spent_usd"] = spent->usd;
        row["spent"] = spent->usd / e.rate;
        row["share"] = e.usd() > 0 ? Json(spent->usd / e.usd()) : Json(nullptr);
      }
    }
    items.push_back(std::move(row));
  }
  return Json{{"limits", items}, {"issues", s.issues}, {"setting", setting_of(data)}};
}

Result<Json> gates(const std::filesystem::path& root, const Config& config, const Json& data, const Json& plan, const Json& estimate,
                   std::time_t now) {
  Json out = Json::array();
  if (data.empty()) return out;
  const Stored s = stored(config, data);
  for (const auto& issue : s.issues)
    log::write(log::Level::Debug, "loom.spend", "a spending limit is not usable: " + issue.value("reason", ""));
  if (s.entries.empty()) return out;
  // A plan that calls no model spends nothing: a limit never stands in the way of reading, of editing a setting or of raising the limit.
  if (plan.value("cost", Json::object()).value("model_calls", 0) <= 0 && !plan.value("cost", Json::object()).value("pipeline_model_use", false)) return out;
  const Json items = plan.value("items", Json::array());
  const std::string item_id = items.empty() ? std::string() : json::get_string(items[0], "id");
  const std::string action = items.empty() ? std::string() : json::get_string(items[0], "action");
  const std::string bound = json::get_string(data, "estimate_bound", "high");
  const Json money = estimate.value("money", Json::object());
  const bool estimate_known = json::get_bool(money, "known", false) && money.contains(bound) && money[bound].is_number();
  const double estimate_usd = estimate_known ? money[bound].get<double>() : 0.0;
  const Json texts = data.value("texts", Json::object());
  const Json periods = data.value("periods", Json::array());
  for (const auto& e : s.entries) {
    LOOM_TRY_ASSIGN(Spent spent, spent_in(root, window(data, e.period, now)));
    const double projected = spent.usd + estimate_usd;
    if (!(projected > e.usd() + 1e-12)) continue;
    const bool already = spent.usd > e.usd() + 1e-12;
    const Json* period = find_by_id(periods, e.period);
    Json vars_by_lang = Json::object();
    for (const char* lang : {"en", "pl"}) {
      vars_by_lang[lang] = Json{{"period", period ? pick(period->value("label", Json::object()), lang) : e.period},
                                {"amount", amount_text(e.amount)},
                                {"currency", e.currency},
                                {"spent", money_text(spent.usd / e.rate)},
                                {"estimate", money_text(estimate_usd / e.rate)}};
    }
    Json reason = rendered(texts, already ? "already_over" : "would_exceed", vars_by_lang);
    if (!estimate_known) {
      const Json extra = rendered(texts, "estimate_unknown", vars_by_lang);
      for (auto it = reason.begin(); it != reason.end(); ++it)
        if (extra.contains(it.key())) it.value() = it->get<std::string>() + " " + extra[it.key()].get<std::string>();
    }
    Json g = Json::object();
    const std::string id = "spend:" + e.period;
    g["id"] = id;
    g["item"] = item_id;
    g["action"] = action;
    g["for"] = item_id;
    g["title"] = pick(texts.value("title", Json::object()), "en");
    g["rule"] = "spend_limit:" + e.period;
    g["initiator"] = "user";
    g["spend"] = Json{{"period", e.period}, {"limit", entry_json(e)}, {"limit_usd", e.usd()}, {"spent_usd", spent.usd},
                      {"spent_cost_known", spent.cost_known}, {"estimate_usd", estimate_known ? Json(estimate_usd) : Json(nullptr)},
                      {"projected_usd", projected}, {"already_over", already}, {"estimate_bound", bound}};
    if (e.on_exceed == "notify") {
      g["decision"] = "allow";
      g["status"] = "applicable";
      g["effect"] = "allow";
      Json notice = Json::object();
      for (auto it = reason.begin(); it != reason.end(); ++it) {
        const Json* t = json::find(texts, "on_notify");
        notice[it.key()] = t ? render(pick(*t, it.key()), Json{{"what", it->get<std::string>()}}) : it->get<std::string>();
      }
      g["notice"] = notice;
      g["volatile"] = Json::array({"notice", "spend"});
    } else {
      g["decision"] = e.on_exceed;
      g["status"] = e.on_exceed == "deny" ? "denied" : "needs_confirmation";
      g["effect"] = e.on_exceed;
      if (e.on_exceed == "deny") {
        for (auto it = reason.begin(); it != reason.end(); ++it) {
          const Json* t = json::find(texts, "on_deny");
          if (t) it.value() = it->get<std::string>() + " " + render(pick(*t, it.key()), vars_by_lang.value(it.key(), Json::object()));
        }
      }
      g["reason"] = reason;
      g["volatile"] = Json::array({"reason", "spend"});
    }
    out.push_back(std::move(g));
  }
  return out;
}

}  // namespace loom::actions::spend
