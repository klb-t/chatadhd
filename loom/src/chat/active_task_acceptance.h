#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Database;

namespace chat {

inline constexpr std::string_view kActiveTaskAcceptedEvent = "chat.active_task.accepted.v1";
inline constexpr std::string_view kActiveTaskBaselineEvent = "chat.active_task.acceptance_baseline.v1";

struct ActiveTaskAcceptance {
  std::int64_t seq = 0;
  std::string originating_message_id;
  std::string acceptance;
  Json snapshot;
};

struct ActiveTaskAuthority {
  bool baseline_complete = false;
  std::vector<ActiveTaskAcceptance> acceptances;
};

// Structural and content validation for a complete retained acceptance
// snapshot. This deliberately validates embedded native source bytes rather
// than consulting their mutable message rows.
bool valid_active_task_snapshot(const Json& snapshot);

// Read and validate every page of the append-only authority for a conversation.
// Any malformed record fails closed.
Result<ActiveTaskAuthority> load_active_task_authority(Database& db, std::string_view conversation_id);

// Called only inside a caller-owned top-level write transaction. Before the
// baseline marker exists, imports valid top-level legacy metadata without
// inventing its original acceptance time, then appends the completed marker.
Result<ActiveTaskAuthority> ensure_active_task_baseline(Database& db, std::string_view conversation_id);

// Append one complete accepted snapshot. The caller owns the transaction that
// also creates originating_message_id.
Result<std::int64_t> append_active_task_acceptance(Database& db, std::string_view conversation_id,
                                                   std::string_view originating_message_id,
                                                   const Json& snapshot);

}  // namespace chat
}  // namespace loom
