// One formatter for a sum of money shown to people (header: loom/money_format.h).
#include "loom/money_format.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace loom {
namespace {

struct Rules {
  double min_unit = 0.01;
  int decimals = 2;
  int significant = 2;
  std::string zero = "0";
  std::string unknown = "unknown";
};

Rules read_rules(const Json& rules) {
  Rules r;
  if (!rules.is_object()) return r;
  if (const auto it = rules.find("min_unit"); it != rules.end() && it->is_number() && std::isfinite(it->get<double>()) && it->get<double>() > 0)
    r.min_unit = it->get<double>();
  if (const auto it = rules.find("decimals"); it != rules.end() && it->is_number_integer() && it->get<long long>() >= 0 && it->get<long long>() <= 12)
    r.decimals = static_cast<int>(it->get<long long>());
  if (const auto it = rules.find("significant"); it != rules.end() && it->is_number_integer() && it->get<long long>() >= 1 && it->get<long long>() <= 12)
    r.significant = static_cast<int>(it->get<long long>());
  if (const auto it = rules.find("zero"); it != rules.end() && it->is_string()) r.zero = it->get<std::string>();
  if (const auto it = rules.find("unknown"); it != rules.end() && it->is_string()) r.unknown = it->get<std::string>();
  return r;
}

}  // namespace

const Json& money_format_preset() {
  static const Json preset{{"min_unit", 0.01}, {"decimals", 2}, {"significant", 2}, {"zero", "0"}, {"unknown", "unknown"}};
  return preset;
}

std::string format_money(double amount, const Json& rules) {
  const Rules r = read_rules(rules);
  if (!std::isfinite(amount)) return r.unknown;
  if (amount == 0) return r.zero;
  const double mag = std::fabs(amount);
  const std::string sign = amount < 0 ? "-" : "";
  char buf[160];
  if (mag >= r.min_unit) {
    std::snprintf(buf, sizeof buf, "%.*f", r.decimals, mag);
    return sign + buf;
  }
  // Below the unit: enough decimals to keep `significant` digits, in fixed notation (never an exponent, never a zero for a nonzero sum).
  const int exponent = static_cast<int>(std::floor(std::log10(mag)));
  int decimals = r.significant - 1 - exponent;
  if (decimals < r.decimals) decimals = r.decimals;
  if (decimals > 120) decimals = 120;
  std::snprintf(buf, sizeof buf, "%.*f", decimals, mag);
  // Rounding can carry into the unit above (0.0099999 -> 0.010): then the ordinary form is the honest one.
  if (std::strtod(buf, nullptr) >= r.min_unit) {
    std::snprintf(buf, sizeof buf, "%.*f", r.decimals, mag);
    return sign + buf;
  }
  // 0.000010 is 0.00001: trailing zeros past the ordinary decimals carry nothing.
  std::string text = buf;
  const std::size_t point = text.find('.');
  if (point != std::string::npos)
    while (text.size() > point + 1 + static_cast<std::size_t>(r.decimals) && text.back() == '0') text.pop_back();
  return sign + text;
}

std::string format_money(const Json& amount, const Json& rules) {
  if (amount.is_number()) return format_money(amount.get<double>(), rules);
  return read_rules(rules).unknown;
}

}  // namespace loom
