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
// An estimate is a number to decide by, not a limit and not a promise: the usage guard (x10 growth) and the rules are
// separate and unchanged.
#pragma once

#include <cstdint>
#include <string_view>

#include "loom/util/json.h"

namespace loom {
class Config;
class ModelRegistry;
}  // namespace loom

namespace loom::actions {

inline constexpr std::string_view kEstimateSchema = "loom.action_estimate/1";

struct EstimateServices {
  const ModelRegistry* models = nullptr;  // prices (models.json as OpenRouter lists them); null = no price is known
  const Config* config = nullptr;         // default model, completion limit
  Json settings;                          // values of the runtime profile action_run
  Json catalogue;                         // the action catalogue (content_op of the actions)
};

// plan: the plan of Interpreter::preview; args: the run's arguments (a step may take its model from one);
// input_chars: characters of the content the first model step receives.
//   {"schema","input_chars","calls":[{"step","action","model","kind","tokens":{"input":{low,high},"output":{low,high}},
//    "price":{"known",...},"usd":{low,high}|null}],"tokens":{"input","output"},
//    "money":{"known":true,"currency","low","high"} | {"known":false,"currency","reason","models":[...]},
//    "model_calls","assumptions"}
Json estimate_plan(const EstimateServices& services, const Json& plan, const Json& args, std::int64_t input_chars);

}  // namespace loom::actions
