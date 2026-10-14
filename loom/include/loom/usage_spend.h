// loom/usage_spend.h — spending limits per period, applied by the central admission of the usage ledger (P18).
//
// The user's limits are the setting `usage_spend_limits` (see action_spend.h). P17 applied them from the action side only; here every caller
// of the ledger obeys them, because UsagePolicy::assess (so request, preview and confirm) reads the limits from the options it was opened
// with: effective_usage_policy_options adds the key `spend_limits` when the user has set any (an empty setting - the preset - adds nothing,
// so the options and the receipts of a user without limits stay byte for byte as they were).
//
// An admission is a model use when its estimate says so (data `model_use`: a field such as `model`, or a resource such as `requests`);
// importing a file or reading is never stopped by a limit. For such an admission the spend of the period (what the providers reported
// as `cost_usd`) plus the estimate of the call, when the caller knows its cost, is compared with every limit:
//   confirm  the admission waits for the user's yes, with a receipt like the x10 hold (status requires_confirmation);
//   deny     the admission is refused (status denied) with the reason;
//   notify   the call is admitted and the decision carries a notice.
// The decision gets: `limits` (the receipt-bound part: [{period, on_exceed, limit_usd}] of the limits that hold or refuse), `limit_report`
// (the numbers and the reasons, per language, of every limit that is passed; not part of the receipt, a number of the moment), `notices`
// (the passed notify limits) and `reason_codes` (["spend_limit"], plus "growth" when the x10 guard holds too).
#pragma once

#include <ctime>
#include <functional>
#include <string>
#include <string_view>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

// The window of a period in the timestamps of the ledger ("YYYY-MM-DD HH:MM:SS", UTC): kind `today` (since midnight), `week` (since
// Monday), `month` (since the 1st). `now` 0 is the present.
struct UsageSpendWindow {
  std::string from, to;
};
UsageSpendWindow usage_spend_window(std::string_view kind, std::time_t now = 0);

// The shape of the options key `spend_limits` (checked when the options are validated):
//   {limits: [{period, kind, amount, currency, on_exceed, rate, rate_source, limit_usd}], model_use: {estimate_fields, resources},
//    cost_resources: [...], spent_resource, texts: {id: {lang: text}}, period_labels: {period: {lang: text}}}.
Status validate_spend_limits(const Json& spend_limits);

// Whether an estimate is a model use that the limits apply to.
bool spend_model_use(const Json& spend_limits, const Json& estimate);

// What the ledger spent in [from, to): {usd, known, operations}. `known` false: no operation of the window reported a cost.
using SpendLookup = std::function<Result<Json>(const std::string& from, const std::string& to)>;

struct SpendCheck {
  Json holds = Json::array();    // receipt-bound: [{period, on_exceed, limit_usd}] of the passed limits that confirm or deny
  Json report = Json::array();   // every passed limit with its numbers and its reason in every language
  Json notices = Json::array();  // the passed limits that only tell: [{code, period, text: {lang: text}}]
};
Result<SpendCheck> check_spend_limits(const Json& spend_limits, const Json& estimate, const SpendLookup& spent, std::time_t now = 0);

// Why a decision (or a pending hold built from one) waits or refuses because of a spending limit, in the form every surface shows:
// {limits: [{period, on_exceed, code, reason: {lang: text}, ...}], reason_codes: [...], status}, or {} when no limit is involved.
Json usage_limit_reasons(const Json& decision);

}  // namespace loom
