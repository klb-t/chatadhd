// The receipt flow of a confirmed request (P14 7): `confirmed:true` is honoured only with the `plan_receipt` of a proposal of exactly this
// plan. These helpers do what a client does: ask for the proposal (nothing runs), take its plan_receipt, run the same request with it.
#pragma once

#include <string>
#include <string_view>

#include "loom/action_runner.h"
#include "loom/result.h"
#include "loom/runtime.h"
#include "loom/util/json.h"

namespace loom::test {

// actions.run of `request` with its confirmation backed by a receipt.
inline Result<Json> run_confirmed(Runtime& rt, Json request) {
  Json ask = request;
  ask.erase("confirmed");
  ask.erase("receipt_id");
  ask["propose"] = true;
  auto proposed = rt.actions().run(ask);
  if (!proposed) return proposed.error();
  request["confirmed"] = true;
  request["receipt_id"] = proposed->value("plan_receipt", "");
  return rt.actions().run(request);
}

// loom_invoke("actions", action) of a gated write (rules.set, actions.define ...) with its confirmation backed by a receipt.
inline Result<Json> invoke_confirmed(Runtime& rt, std::string_view action, Json request) {
  Json ask = request;
  ask.erase("confirmed");
  ask.erase("receipt_id");
  ask["propose"] = true;
  auto proposed = rt.actions().invoke(action, ask);
  if (!proposed) return proposed.error();
  request["confirmed"] = true;
  request["receipt_id"] = proposed->value("plan_receipt", "");
  return rt.actions().invoke(action, request);
}

}  // namespace loom::test
