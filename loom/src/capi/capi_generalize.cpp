// C API: generalize — predictions for a temporal-holdout cut, computed on
// demand (read-only: nothing is written to the store).
//   {"run"?: kr_ id (default: latest done run), "cut": "YYYY-MM-DD", "priors"?: true}
//   -> {"run","cut","operators":[...],"predictions":[...],
//       "summary":{"operators","predictions","holds","violated","pending"}}
#include "context.h"
#include "loom/generalize.h"
#include "loom/knowledge.h"
#include "loom/knowledge_store.h"

using namespace loom;
using namespace loom::capi;

namespace {

Result<Json> predict_request(LoomContext* ctx, const Json& req) {
  auto& ks = ctx->rt->knowledge().store();
  std::string run = json::get_string(req, "run");
  if (run.empty()) {
    LOOM_TRY_ASSIGN(auto runs, ks.list_runs(50));
    for (const auto& r : runs) {
      if (r.status == "done") {
        run = r.id;
        break;
      }
    }
    if (run.empty() && !runs.empty()) run = runs.front().id;
    if (run.empty()) return Error(Errc::NotFound, "no knowledge run yet");
  }
  std::string cut = json::get_string(req, "cut");
  if (cut.size() < 10) return Error(Errc::InvalidArgument, "cut (YYYY-MM-DD) is required");
  LOOM_TRY_ASSIGN(auto pack, ctx->rt->knowledge().pack());
  LOOM_TRY_ASSIGN(auto ev, generalize::Evidence::load(ks, run));
  model::PriorFilter priors = json::get_bool(req, "priors", true) ? model::PriorFilter::as_of_date(cut) : model::PriorFilter::none();
  // Only what existed at the cut is used to induce the generator.
  generalize::Evidence before, after;
  before.entities = after.entities = ev.entities;
  auto keep = [&](const std::string& d, bool b) { return d.empty() ? b : (b ? d.substr(0, 10) <= cut : d.substr(0, 10) > cut); };
  for (const auto& o : ev.observations) (keep(o.date, true) ? before : after).observations.push_back(o);
  for (const auto& d : ev.decisions) (keep(d.date, true) ? before : after).decisions.push_back(d);
  before.claims = after.claims = ev.claims;  // claim dates come from their observations
  before.areas = ev.areas;
  LOOM_TRY_ASSIGN(auto rep, generalize::discover_principles(*pack, before, priors));
  before.principles = rep.principles;
  LOOM_TRY_ASSIGN(auto ops, generalize::mine_operators(*pack, before, priors));
  LOOM_TRY_ASSIGN(auto preds, generalize::predict(ops, before, cut));
  // after-cut decisions still need their conversations for the signature
  after.observations = ev.observations;
  LOOM_TRY_ASSIGN(preds, generalize::evaluate_predictions(std::move(preds), after));
  Json o = Json::array(), p = Json::array();
  for (const auto& x : ops) o.push_back(x.to_json());
  int holds = 0, violated = 0, pending = 0;
  for (const auto& x : preds) {
    p.push_back(x.to_json());
    holds += x.outcome == model::CheckState::Holds;
    violated += x.outcome == model::CheckState::Violated;
    pending += x.outcome == model::CheckState::Pending;
  }
  return Json{{"run", run},
              {"cut", cut.substr(0, 10)},
              {"operators", o},
              {"predictions", p},
              {"summary", Json{{"operators", ops.size()}, {"predictions", preds.size()}, {"holds", holds}, {"violated", violated}, {"pending", pending}}}};
}

}  // namespace

extern "C" {

LOOM_API const char* loom_generalize_predict(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_generalize_predict", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto req = parse_arg(request_json);
    if (!req) return out_error(req.error());
    if (!req->is_object()) return out_error(Errc::InvalidArgument, "request must be a JSON object");
    return out_result(predict_request(ctx, *req));
  });
}

}  // extern "C"
