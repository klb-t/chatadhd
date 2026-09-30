// Internal durable chat acceptance adapter. No schema migration or public ABI.
#pragma once
#include <map>
#include "loom/db.h"

namespace loom::chat {
struct ActiveTaskHistory {
  Json snapshots = Json::array();
  // Originating message IDs are projections, never lookup authority.
  std::map<std::string, Json> projections;
  bool baseline_complete = false;
};
bool valid_active_task_snapshot(const Json& snapshot);
bool same_active_task_identity(const Json& a, const Json& b);
// Read-only, including legacy discovery. Caller holds db.lock().
Result<ActiveTaskHistory> read_active_task_history(Database& db, std::string_view conversation,
    const std::map<std::string, Json>& in_flight = {});
// Caller owns the outer transaction; these never commit it.
Status append_active_task_acceptance(Database& db, std::string_view conversation,
    std::string_view message_id, const Json& snapshot, const ActiveTaskHistory& history);
}  // namespace loom::chat
