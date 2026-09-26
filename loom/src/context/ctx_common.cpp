#include "ctx_common.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace loom::ctx {

Result<std::string> resolve_run(kb::KnowledgeStore& store, std::string_view run) {
  if (!run.empty()) return std::string(run);
  LOOM_TRY_ASSIGN(auto runs, store.list_runs(50));
  for (const auto& r : runs) {
    if (r.status == "done") return r.id;
  }
  return Error(Errc::NotFound, "no finished knowledge run: pass 'run' explicitly or run the knowledge pipeline first");
}

std::string pick_text(const model::Text& t, std::string_view lang) {
  if (!lang.empty()) {
    auto it = t.find(std::string(lang));
    if (it != t.end() && !it->second.empty()) return it->second;
  }
  auto en = t.find("en");
  if (en != t.end() && !en->second.empty()) return en->second;
  for (const auto& [k, v] : t) {
    if (!v.empty()) return v;
  }
  return "";
}

namespace {
// Howard Hinnant's civil_from_days / days_from_civil (proleptic Gregorian,
// day 0 = 1970-01-01). https://howardhinnant.github.io/date_algorithms.html
long days_from_civil(long y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}
}  // namespace

std::optional<long> parse_date_days(std::string_view iso) {
  if (iso.size() < 10) return std::nullopt;
  auto digit = [](char c) { return c >= '0' && c <= '9'; };
  if (!digit(iso[0]) || !digit(iso[1]) || !digit(iso[2]) || !digit(iso[3]) || iso[4] != '-' || !digit(iso[5]) ||
      !digit(iso[6]) || iso[7] != '-' || !digit(iso[8]) || !digit(iso[9])) {
    return std::nullopt;
  }
  long y = std::strtol(std::string(iso.substr(0, 4)).c_str(), nullptr, 10);
  unsigned mo = static_cast<unsigned>(std::strtol(std::string(iso.substr(5, 2)).c_str(), nullptr, 10));
  unsigned da = static_cast<unsigned>(std::strtol(std::string(iso.substr(8, 2)).c_str(), nullptr, 10));
  if (mo < 1 || mo > 12 || da < 1 || da > 31) return std::nullopt;
  return days_from_civil(y, mo, da);
}

double freshness_score(std::string_view date, std::string_view anchor, double half_life_days) {
  auto d = parse_date_days(date);
  auto a = parse_date_days(anchor);
  if (!d || !a) return 0.6;
  double age_days = static_cast<double>(*a - *d);
  if (age_days <= 0.0) return 1.0;
  double v = std::pow(0.5, age_days / half_life_days);
  return std::clamp(v, 0.0, 1.0);
}

double authority_score(model::Origin o) noexcept { return model::authority_rank(o) / 5.0; }

namespace {
std::string replace_all(std::string s, std::string_view from, std::string_view to) {
  if (from.empty()) return s;
  std::size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

std::string fmt_confidence(double c) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%.2f", c);
  return buf;
}
}  // namespace

std::string evidence_markdown(const kb::Pack& pack, model::EvidenceClass ev, model::Origin origin, double confidence,
                              std::string_view value_text, const std::optional<kb::ExpectedProperty>& expected,
                              std::string_view basis, const Json& fill_query) {
  const Json& enc = pack.file("policy/evidence_encoding.json");
  std::string ev_key(model::to_string(ev));
  std::string tmpl = "{value}";
  if (const Json* e = json::find(enc, "evidence"); e) {
    if (const Json* node = json::find(*e, ev_key); node) tmpl = json::get_string(*node, "markdown", "{value}");
  }
  std::string out = tmpl;
  out = replace_all(out, "{value}", value_text);
  out = replace_all(out, "{confidence}", fmt_confidence(confidence));
  out = replace_all(out, "{expected_property}", expected ? expected->render() : "");
  out = replace_all(out, "{basis}", basis);
  out = replace_all(out, "{query}", fill_query.is_null() ? "?" : json::dump(fill_query));

  std::string origin_key(model::to_string(origin));
  if (const Json* o = json::find(enc, "origin"); o) {
    if (const Json* node = json::find(*o, origin_key); node) out += json::get_string(*node, "markdown", "");
  }
  return out;
}

std::string principle_badge(model::ValidationStatus validation, model::PrincipleLevel level, model::PrincipleForm form) {
  return "[" + std::string(model::to_string(validation)) + ", " + std::string(model::to_string(level)) + "/" +
         std::string(model::to_string(form)) + "]";
}

Result<std::vector<model::GoalType>> list_goal_types(const kb::Pack& pack) {
  const Json& d = pack.file("goals/goal_types.json");
  const Json* arr = json::find(d, "goal_types");
  if (!arr || !arr->is_array()) return Error(Errc::NotFound, "pack has no goals/goal_types.json goal_types array");
  std::vector<model::GoalType> out;
  for (const auto& e : *arr) {
    LOOM_TRY_ASSIGN(auto gt, model::GoalType::from_json(e));
    out.push_back(std::move(gt));
  }
  std::sort(out.begin(), out.end(), [](const model::GoalType& a, const model::GoalType& b) { return a.id < b.id; });
  return out;
}

}  // namespace loom::ctx
