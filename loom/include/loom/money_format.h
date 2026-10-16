// loom/money_format.h — one formatter for a sum of money shown to people (P31).
//
// Every place that says an amount (a proposal's estimate, "how much did I spend", a spending limit's reason) uses this one function, so no
// place floors a small sum to zero. The rules are data (runtime profile action_run, spend_limits.money_format; this file holds the same
// preset for a caller that has no profile at hand):
//   min_unit     from this amount up the sum is written with `decimals` decimals (0.01 -> "0.01", 1.5 -> "1.50")
//   decimals     decimals at or above min_unit (2)
//   significant  below min_unit the sum is written in fixed notation with this many significant digits (2.5e-5 -> "0.000025");
//                a nonzero sum is never written as zero
//   zero         the text for exactly zero ("0")
//   unknown      the text for a sum that is not a number ("unknown"): unknown is never shown as a zero
#pragma once

#include <string>

#include "loom/util/json.h"

namespace loom {

// The preset of the rules (what the profile ships).
const Json& money_format_preset();

// `rules` may be null or partial: missing fields keep the preset; a field of the wrong type is ignored.
std::string format_money(double amount, const Json& rules = Json());
// A JSON value: a number is formatted, anything else (null, text) is the rules' "unknown" text.
std::string format_money(const Json& amount, const Json& rules = Json());

}  // namespace loom
