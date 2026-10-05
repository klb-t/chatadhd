// Data-driven user profile, privacy and resumable interview. No network calls.
#pragma once
#include <string_view>
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::onboarding {

// request: {op: ask|store|send|infer, category, field?, provider?, detail?,
// sensitivity?, provenance?}. The result explains the selected rule/denial.
Result<Json> privacy_decision(const Json& state, const Json& request);

class ProfileSession {
 public:
  // Existing state is copied, never modified in place; only missing fields are
  // added. Future schemas are rejected rather than migrated backwards.
  static Result<ProfileSession> create(const Json& scenario, const Json& defaults,
                                       const Json& existing = Json::object());
  Json snapshot() const { return state_; }
  Json scenario() const { return scenario_; }
  // Caller supplies event id/time/source_refs. Returns the complete snapshot.
  // answer creates a candidate; review confirms/rejects/corrects it. A form uses
  // these same operations with provenance=form. Statuses are explicit data.
  Result<Json> dispatch(const Json& action);
  // Builds a provider-filtered request. The caller's existing model adapter
  // executes it; host attaches the prepared provider/request_token to its reply
  // (the model must not choose these). Stale/unrelated responses are rejected.
  Result<Json> model_request(std::string_view provider) const;
  Result<Json> ingest_model_reply(const Json& reply);

 private:
  Json scenario_;
  Json state_;
  Result<Json> apply(const Json& action);
  Status sync_graph();
};
}  // namespace loom::onboarding
