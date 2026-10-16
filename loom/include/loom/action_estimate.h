// loom/action_estimate.h — what a plan would cost before it runs (GRAF_HUB_KOD_SILNIK §5: a chain is proposed
// "with its cost").
//
// The relative cost (1 = one ordinary model call) of the catalogue stays; this adds an estimate of tokens and, where
// the model registry knows the model's price, of money: a range from the characters the model would receive and a band
// of characters per token and of output size (runtime profile action_run, `estimate`: data, calibration). When a model
// has no price in the registry - or the output is not text (an image) - the money stays UNKNOWN and says why. It is
// never zero for a price that is not known: a free model with a listed price of 0 is a known zero, an unlisted model
// is not.
//
// `reason_code` names why the money is unknown (profile action_run, estimate.reasons: the texts, per language, are data); `reason` is
// the text in English. An empty registry (a fresh data directory, the model list not fetched) is its own code, not "no price".
//
// An estimate is a number to decide by, not a limit and not a promise: the usage guard (x10 growth) and the rules are
// separate and unchanged.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include "loom/util/json.h"

namespace loom {
class Config;
class ModelRegistry;
}  // namespace loom

namespace loom::actions {

inline constexpr std::string_view kEstimateSchema = "loom.action_estimate/1";

// What the usage ledger measured for an action and a model (P31): the samples of one resource.
struct ObservedUsage {
  std::int64_t samples = 0;
  double mean = 0, min = 0, max = 0;
};

struct EstimateServices {
  const ModelRegistry* models = nullptr;  // prices (models.json as OpenRouter lists them); null = no price is known
  const Config* config = nullptr;         // default model, completion limit
  Json settings;                          // values of the runtime profile action_run
  Json catalogue;                         // the action catalogue (content_op of the actions)
  // Measured output of (action, model, resource) from the usage ledger; empty = no calibration. Never creates a ledger.
  std::function<std::optional<ObservedUsage>(const std::string& action, const std::string& model, const std::string& resource)> observed;
  std::string registry_failure;           // why the automatic fetch of the model list failed (empty: none was tried or it worked)
};

// plan: the plan of Interpreter::preview; args: the run's arguments (a step may take its model from one);
// input_chars: characters of the content the first model step receives.
//   {"schema","input_chars","calls":[{"step","action","model","kind","tokens":{"input":{low,high},"output":{low,high}},
//    "price":{"known",...},"usd":{low,high}|null}],"tokens":{"input","output"},
//    "money":{"known":true,"currency","low","high"} | {"known":false,"currency","reason","reason_code","reason_params"?,"models":[...]},
//    "model_calls","assumptions"}
Json estimate_plan(const EstimateServices& services, const Json& plan, const Json& args, std::int64_t input_chars);

}  // namespace loom::actions
