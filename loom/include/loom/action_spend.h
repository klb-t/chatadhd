// loom/action_spend.h — spending limits per period for action runs (P17; the owner's rule: the user decides, a preset is not a rule).
//
// The user's limits are the setting `usage_spend_limits` (data: runtime profile action_run, `spend_limits`): a list of
// {period: day|week|month, amount, currency, on_exceed: confirm|deny|notify, rate?}. The preset is an empty list, that is no limit at all.
// P18: the limits are enforced by the central admission of the usage ledger (usage_spend.h), so a plain chat send, the routing model call,
// the background analysis, embeddings and an action's model call all obey them with the same on_exceed. What stays here is the PREVIEW of a
// plan: the proposal says "this would exceed your weekly limit" before anything runs (deny: the proposal is refused; confirm and notify: a
// notice - the asking happens at the model call, with a receipt like the x10 hold). The x10 growth guard is unchanged.
//
// The ledger counts what providers report, in USD. A limit in another currency is converted with `rate` (USD per unit): the one the limit
// carries, else the preset of the data (approximate, dated, and said so when the limit is set).
#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include "loom/config.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::actions::spend {

struct Entry {
  std::string period;    // day | week | month
  std::string currency;  // canonical code (PLN, USD ...)
  std::string on_exceed;  // confirm | deny | notify
  double amount = 0;     // in `currency`
  double rate = 1;       // USD per unit of `currency`
  std::string rate_source;  // "identity" (USD) | "preset" (the data's table) | "user" (the limit's own)
  double usd() const { return amount * rate; }
  Json to_json() const;
};

// The `spend_limits` object of the runtime profile action_run (the overlay of the user included); {} when there is none.
Json data_of(const std::filesystem::path& root);

struct Stored {
  std::vector<Entry> entries;
  Json issues = Json::array();  // entries of the setting that are not usable, each {index, reason}
};
// The limits the user has set. An entry that cannot be used (no positive amount, an unknown period, a currency without a rate) is left out
// and listed in `issues`: the limit is never guessed.
Stored stored(const Config& config, const Json& data);

// A limit from the words and numbers of a sentence or of an API call: {amount, currency?, period, on_exceed?, rate?}. Words are folded
// (lower case, no diacritics) by the caller. Error InvalidArgument names what is missing or unknown.
Result<Entry> entry_from(const Json& data, const Json& args);
// Sets the limit of its period (a new limit of the same period replaces the old one) and saves the setting. {written, entry, previous?}.
Result<Json> put(Config& config, const Json& data, const Entry& entry);
// Removes the limit of a period; {removed: bool, previous?}.
Result<Json> clear(Config& config, const Json& data, const std::string& period);
// The period a word names ("miesiac", "weekly" ...) or "".
std::string period_of_word(const Json& data, const std::string& folded_word);
// The first period any word of a folded text names, or "".
std::string period_in(const Json& data, const std::string& folded_text);
// The label of an id in a language ("miesięczny"), or the id.
std::string period_label(const Json& data, const std::string& period, const std::string& lang);
std::string on_exceed_label(const Json& data, const std::string& on_exceed, const std::string& lang);

// The limits in the form the usage policy carries in its options (key `spend_limits`, see usage_spend.h) so that the central admission
// applies them to every model use: {limits, model_use, cost_resources, spent_resource, texts, period_labels}, or null while the user has set
// none (the preset) - then nothing is read but the setting.
Json policy_options(const Config& config, const std::filesystem::path& root);

struct Window {
  std::string from, to;  // [from, to) as the ledger stamps them ("YYYY-MM-DD HH:MM:SS", UTC)
};
Window window(const Json& data, const std::string& period, std::time_t now);

struct Spent {
  bool ledger = false;      // a usage ledger exists
  bool cost_known = false;  // at least one operation of the window reported a cost
  double usd = 0;
  std::int64_t operations = 0;
};
Result<Spent> spent_in(const std::filesystem::path& root, const Window& window);

// Where every limit stands now: [{period, amount, currency, on_exceed, rate, rate_source, limit_usd, from, to, spent_usd|null, spent,
// share|null, operations}] and the issues of the setting. `spent` is in the limit's currency.
Json status(const std::filesystem::path& root, const Config& config, const Json& data, std::time_t now = 0);

// The gates for a plan, in the shape of the gates of the rules (decision, status, rule, effect, reason, initiator ...): none when no limit
// is set, when the plan calls no model, or when nothing is passed. `estimate` is the plan's estimate (action_estimate.h); an unknown
// cost of the run counts as zero and the reason says so. The numbers are `volatile`: they do not enter the plan receipt, only the
// decision does.
Result<Json> gates(const std::filesystem::path& root, const Config& config, const Json& data, const Json& plan, const Json& estimate,
                   std::time_t now = 0);

}  // namespace loom::actions::spend
