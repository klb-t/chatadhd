#include "export_internal.h"

namespace loom::xport {
Result<std::optional<Checkpoint>> read_checkpoint(Env& env, const std::string& member,
                                                 std::int64_t archive_index, std::int64_t source_index) {
  if (!env.opts.resume || env.checkpoint_source_id.empty()) return std::optional<Checkpoint>{};
  auto lock = env.db.lock();
  LOOM_TRY_ASSIGN(auto statement, env.db.conn().prepare(
      "SELECT conversation_id,metadata FROM loom_import_checkpoints "
      "WHERE source_id=? AND member=? AND archive_index=? AND source_index=?"));
  statement.bind_all(env.checkpoint_source_id, member, archive_index, source_index);
  LOOM_TRY_ASSIGN(bool found, statement.step());
  if (!found) return std::optional<Checkpoint>{};
  LOOM_TRY_ASSIGN(auto metadata, json::parse(statement.get_text(1)));
  return std::optional<Checkpoint>(Checkpoint{statement.get_text(0), std::move(metadata)});
}
Status write_checkpoint(Env& env, const std::string& member, std::int64_t archive_index,
                        std::int64_t source_index, const std::string& conversation_id, const Json& metadata) {
  if (!env.opts.resume || env.checkpoint_source_id.empty()) return {};
  auto lock = env.db.lock();
  return env.db.conn().run(
      "INSERT INTO loom_import_checkpoints(source_id,member,archive_index,source_index,conversation_id,metadata) "
      "VALUES (?,?,?,?,?,?) ON CONFLICT(source_id,member,archive_index,source_index) "
      "DO UPDATE SET conversation_id=excluded.conversation_id,metadata=excluded.metadata",
      env.checkpoint_source_id, member, archive_index, source_index, conversation_id, json::py_dumps(metadata));
}
}  // namespace loom::xport
