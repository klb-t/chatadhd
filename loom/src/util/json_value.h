// Shared native DTO equality. Object order is irrelevant; array order, fields,
// scalar types and exact numeric values are preserved. Canonical hashes and
// original source bytes remain separate identities and are never rewritten.
#pragma once

#include <cmath>

#include "loom/util/json.h"

namespace loom::json {
inline bool equivalent_number(const Json& left, const Json& right) {
  if (left.is_number_float() && right.is_number_float())
    return left.get<double>() == right.get<double>();
  if (left.is_number_float() || right.is_number_float()) {
    const auto& integer = left.is_number_float() ? right : left;
    const double floating = (left.is_number_float() ? left : right).get<double>();
    if (!std::isfinite(floating) || std::trunc(floating) != floating) return false;
    // Half-open powers-of-two bounds are exactly representable in double.
    // Check before conversion: a rounded integer must not compare equal to
    // the original value, and out-of-range casts must never be attempted.
    if (integer.is_number_unsigned()) {
      if (floating < 0.0 || floating >= std::ldexp(1.0, 64)) return false;
      return static_cast<std::uint64_t>(floating) == integer.get<std::uint64_t>();
    }
    if (floating < -std::ldexp(1.0, 63) || floating >= std::ldexp(1.0, 63)) return false;
    return static_cast<std::int64_t>(floating) == integer.get<std::int64_t>();
  }
  if (left.is_number_unsigned() && right.is_number_unsigned())
    return left.get<std::uint64_t>() == right.get<std::uint64_t>();
  if (!left.is_number_unsigned() && !right.is_number_unsigned())
    return left.get<std::int64_t>() == right.get<std::int64_t>();
  const auto& signed_number = left.is_number_unsigned() ? right : left;
  const auto& unsigned_number = left.is_number_unsigned() ? left : right;
  const auto value = signed_number.get<std::int64_t>();
  return value >= 0 && static_cast<std::uint64_t>(value) == unsigned_number.get<std::uint64_t>();
}

inline bool equivalent(const Json& left, const Json& right) {
  if (left.is_object() && right.is_object()) {
    if (left.size() != right.size()) return false;
    for (auto field = left.begin(); field != left.end(); ++field) {
      auto other = right.find(field.key());
      if (other == right.end() || !equivalent(field.value(), other.value())) return false;
    }
    return true;
  }
  if (left.is_array() && right.is_array()) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
      if (!equivalent(left[index], right[index])) return false;
    return true;
  }
  if (left.is_number() && right.is_number()) return equivalent_number(left, right);
  // Array order and nonnumeric scalar values remain meaningful.
  return left == right;
}

}  // namespace loom::json
