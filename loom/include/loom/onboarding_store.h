#pragma once

#include "loom/db.h"
#include "loom/util/json.h"

namespace loom::onboarding {

// Generated from canonical .pack documents. self.json stays an application
// probe definition, never a claim that a new user works on its projects.
Result<Json> builtin_pack();
Result<Json> builtin_scenario();

// Project a checked profile/layer snapshot to the existing native DTOs.
// Caller vocabulary and seed definitions are data, not dispatch names.
Result<Json> project_graph(const Json& pack, const Json& scenario,
                           const Json& profile, const Json& layers,
                           std::string_view user);

// Native persistence, optimistic concurrency and scoped graph replacement
// share one Database transaction. Existing conversations/import runs are
// never migrated or cleared. Snapshot revision is the caller's CAS token.
class OnboardingStore {
 public:
  explicit OnboardingStore(Database& db) : db_(db) {}
  Result<Json> open(std::string_view user, const Json& legacy = Json::object());
  Result<Json> read(std::string_view user);
  Result<Json> apply(std::string_view user, std::int64_t expected_revision,
                     const Json& action);
  // Updating installed defaults is explicit, atomic and forward-only.
  Result<Json> update_pack(std::string_view user, std::int64_t expected_revision,
                           const Json& pack, const Json& scenario);
  // Install missing definitions natively so browser number conversion cannot
  // rewrite existing opaque pack values. Resolution/migration stays update_pack.
  Result<Json> install_entries(std::string_view user, std::int64_t expected_revision,
                               const Json& extension);
  Result<Json> model_request(std::string_view user, std::string_view provider);
  // Selector/writer gate over EFFECTIVE policy, including disabled/excluded
  // privacy defaults. Raw retained profile rules are inspection, not authority.
  Result<Json> policy_decision(std::string_view user, const Json& request);

 private:
  Status ensure_schema();
  Result<Json> read_locked(std::string_view user);
  Status save_locked(const Json& state);
  Database& db_;
};

}  // namespace loom::onboarding
