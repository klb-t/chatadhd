// kb.h: evidence vocabulary, expected properties, closed-set lookup,
// stable ids and version normalisation.
#include <algorithm>
#include <cctype>

#include "loom/kb.h"
#include "loom/util/sha256.h"

namespace loom::kb {

namespace {
constexpr std::pair<Evidence, std::string_view> kEvidenceNames[] = {
    {Evidence::Observed, "observed"}, {Evidence::Derived, "derived"}, {Evidence::Inferred, "inferred"},
    {Evidence::Extrapolated, "extrapolated"}, {Evidence::Absent, "absent"}, {Evidence::User, "user"}};
constexpr std::pair<CheckState, std::string_view> kCheckNames[] = {
    {CheckState::Pending, "pending"}, {CheckState::Holds, "holds"}, {CheckState::Violated, "violated"},
    {CheckState::NotApplicable, "n/a"}};

std::vector<std::string> string_list(const Json& j) {
  std::vector<std::string> out;
  if (!j.is_array()) return out;
  for (const auto& x : j) {
    if (x.is_string()) out.push_back(x.get<std::string>());
  }
  return out;
}

void render_expr(const Json& e, std::string& out) {
  if (e.is_object() && e.contains("op")) {
    out += json::get_string(e, "op");
    out += '(';
    const Json* args = json::find(e, "args");
    bool first = true;
    if (args && args->is_array()) {
      for (const auto& a : *args) {
        if (!first) out += ", ";
        first = false;
        render_expr(a, out);
      }
    }
    out += ')';
  } else if (e.is_string()) {
    out += e.get<std::string>();
  } else {
    out += json::dump(e);
  }
}
}  // namespace

std::string_view to_string(Evidence e) noexcept {
  for (const auto& [v, n] : kEvidenceNames) {
    if (v == e) return n;
  }
  return "absent";
}

std::optional<Evidence> evidence_from_string(std::string_view s) noexcept {
  for (const auto& [v, n] : kEvidenceNames) {
    if (n == s) return v;
  }
  return std::nullopt;
}

std::string_view gt_evidence(Evidence e) noexcept {
  switch (e) {
    case Evidence::Observed:
    case Evidence::Derived:
    case Evidence::User:
      return "observed";
    case Evidence::Inferred:
      return "inferable";
    case Evidence::Absent:
      return "absent";
    case Evidence::Extrapolated:
      return "";
  }
  return "";
}

std::string_view to_string(CheckState s) noexcept {
  for (const auto& [v, n] : kCheckNames) {
    if (v == s) return n;
  }
  return "n/a";
}

std::optional<CheckState> check_state_from_string(std::string_view s) noexcept {
  for (const auto& [v, n] : kCheckNames) {
    if (n == s) return v;
  }
  return std::nullopt;
}

Json ExpectedProperty::to_json() const {
  Json c = Json::array();
  for (const auto& s : confirm_if) c.push_back(s);
  Json r = Json::array();
  for (const auto& s : refute_if) r.push_back(s);
  return Json{{"expr", expr}, {"rationale", rationale}, {"confirm_if", c}, {"refute_if", r}};
}

Result<ExpectedProperty> ExpectedProperty::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "expected_property must be an object");
  ExpectedProperty p;
  const Json* e = json::find(j, "expr");
  if (!e || !e->is_object() || !e->contains("op")) {
    return Error(Errc::InvalidArgument, "expected_property.expr must be an {\"op\",\"args\"} object");
  }
  std::string op = json::get_string(*e, "op");
  if (!in_closed_set(op, kPredicates)) return Error(Errc::InvalidArgument, "unknown predicate: " + op);
  p.expr = *e;
  p.rationale = json::get_string(j, "rationale");
  if (const Json* c = json::find(j, "confirm_if")) p.confirm_if = string_list(*c);
  if (const Json* r = json::find(j, "refute_if")) p.refute_if = string_list(*r);
  return p;
}

std::string ExpectedProperty::render() const {
  std::string out;
  render_expr(expr, out);
  return out;
}

bool in_closed_set(std::string_view name, const std::string_view* begin, const std::string_view* end) noexcept {
  return std::find(begin, end, name) != end;
}

std::string_view to_string(Lang l) noexcept {
  switch (l) {
    case Lang::En:
      return "en";
    case Lang::Pl:
      return "pl";
    case Lang::Mixed:
      return "mixed";
    case Lang::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string normalize_version(std::string_view s) {
  std::size_t i = 0;
  if (i < s.size() && (s[i] == 'v' || s[i] == 'V')) ++i;
  std::vector<std::string> parts;
  std::string cur;
  bool any = false;
  for (; i < s.size(); ++i) {
    char c = s[i];
    if (std::isdigit(static_cast<unsigned char>(c))) {
      cur.push_back(c);
      any = true;
    } else if (c == '.') {
      if (cur.empty()) return "";
      parts.push_back(cur);
      cur.clear();
    } else {
      return "";
    }
  }
  if (!any || cur.empty()) return "";
  parts.push_back(cur);
  if (parts.size() < 2 || parts.size() > 3) return "";
  std::string out;
  for (std::size_t k = 0; k < parts.size(); ++k) {
    std::string p = parts[k];
    std::size_t nz = p.find_first_not_of('0');
    p = nz == std::string::npos ? "0" : p.substr(nz);
    if (p.size() > 6) return "";
    if (k) out += '.';
    out += p;
  }
  return out;
}

int compare_versions(std::string_view a, std::string_view b) {
  std::string na = normalize_version(a);
  std::string nb = normalize_version(b);
  if (na.empty() || nb.empty()) return na.empty() == nb.empty() ? 0 : (na.empty() ? -1 : 1);
  auto split = [](const std::string& v) {
    std::vector<long> out;
    std::size_t start = 0;
    while (start <= v.size()) {
      std::size_t dot = v.find('.', start);
      std::string part = v.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
      out.push_back(std::stol(part));
      if (dot == std::string::npos) break;
      start = dot + 1;
    }
    while (out.size() < 3) out.push_back(0);
    return out;
  };
  auto va = split(na);
  auto vb = split(nb);
  for (std::size_t i = 0; i < 3; ++i) {
    if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
  }
  return 0;
}

std::string stable_id(std::string_view prefix, std::string_view key) {
  return std::string(prefix) + Sha256::hex(key).substr(0, 12);
}

}  // namespace loom::kb
