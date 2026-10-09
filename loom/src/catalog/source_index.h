#pragma once
#include <limits>
#include <optional>
#include "loom/catalog.h"

namespace loom::catalog::internal {
// Old catalog rows carry only a byte locator. Absence means a unit-relative
// mapping index, never an observed position in the original source array.
inline Result<std::optional<int>> source_index(const CatalogUnit& unit) {
  const auto* value = json::find(unit.unit.attrs, "source_index");
  if (!value) return std::optional<int>{};
  if (!value->is_number_integer() || (value->is_number_unsigned() && value->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) ||
      value->get<std::int64_t>() < 0 || value->get<std::int64_t>() > std::numeric_limits<int>::max())
    return Error(Errc::InvalidArgument, "catalog source index is outside the parser representation");
  return std::optional<int>{value->get<int>()};
}
}
