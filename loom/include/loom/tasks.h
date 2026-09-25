// loom/tasks.h — persistent, resumable, auditable jobs (MEGA MASTER 2.G, 4.7).
//
// A task is a row in loom_tasks: kind, params, status, attempts, checkpoint,
// input/output hashes, result. Handlers are registered per kind (code); tasks
// are data. Every status transition is appended to the EventLog
// ("task:<status>") and emitted on the bus (events::kTaskChanged).
//
// Status machine:
//   pending -> running -> done
//                      -> failed      (attempts >= max_attempts)
//                      -> pending     (retry: attempts < max_attempts)
//                      -> paused      (handler returned Errc::Paused after pause())
//                      -> cancelled   (cancel() or handler returned Errc::Cancelled)
//   paused -> pending (resume)
// A task found 'running' when the engine starts belonged to a crashed
// process: recover_interrupted() moves it back to 'pending' and the handler
// resumes from the last saved checkpoint.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Database;
class EventLog;
class EventBus;

namespace task_status {
inline constexpr std::string_view kPending = "pending";
inline constexpr std::string_view kRunning = "running";
inline constexpr std::string_view kPaused = "paused";
inline constexpr std::string_view kDone = "done";
inline constexpr std::string_view kFailed = "failed";
inline constexpr std::string_view kCancelled = "cancelled";
bool is_terminal(std::string_view s) noexcept;  // done | failed | cancelled
}  // namespace task_status

struct TaskRecord {
  std::string id;  // t_…
  std::string kind;
  std::string status = "pending";
  Json params = Json::object();
  std::string input_hash;   // sha256(canonical(params)) unless given
  std::string output_hash;  // sha256(canonical(result)) when done
  std::optional<Json> checkpoint;
  int attempts = 0;
  int max_attempts = 3;
  std::string error;
  std::string parent_id;
  std::optional<Json> result;
  std::string created;
  std::string updated;
  Json to_json() const;
};

// Handed to the handler for one run. Methods are safe to call from the
// handler thread only (except cancelled()/pause_requested(), any thread).
class TaskContext {
 public:
  virtual ~TaskContext() = default;
  virtual const TaskRecord& task() const = 0;
  const std::string& id() const { return task().id; }
  const Json& params() const { return task().params; }
  // Last checkpoint saved by a previous (possibly crashed) attempt.
  virtual std::optional<Json> checkpoint() const = 0;
  // Persists immediately (own transaction): progress survives a crash.
  virtual Status save_checkpoint(const Json& state) = 0;
  // Emits events::kTaskProgress (not persisted).
  virtual void progress(std::int64_t current, std::int64_t total, std::string_view status = "") = 0;
  // Stored as `result` (+ output_hash) when the handler returns success.
  virtual void set_result(Json result) = 0;
  // Cooperative stop signals: handlers should check between steps and
  // return Error(Errc::Cancelled) / Error(Errc::Paused) respectively.
  virtual bool cancelled() const = 0;
  virtual bool pause_requested() const = 0;
  virtual bool should_stop() const { return cancelled() || pause_requested(); }
  // Emit on the Runtime bus (no-op when the engine has no bus).
  virtual void emit(std::string_view event, const Json& data) = 0;
};

using TaskHandler = std::function<Status(TaskContext&)>;

struct SubmitOptions {
  int max_attempts = 3;
  std::string parent_id;
  std::string input_hash;  // default: sha256(json::canonical(params))
  // If a task of the same kind + input_hash exists and is not failed/
  // cancelled, return its id instead of creating a new one (idempotent submit).
  bool dedupe = false;
};

struct TaskFilter {
  std::optional<std::string> kind;
  std::optional<std::string> status;
  std::optional<std::string> parent_id;
  int limit = 100;
};

class TaskEngine {
 public:
  TaskEngine(Database& db, EventLog& log, EventBus* bus = nullptr);
  ~TaskEngine();  // stop()
  TaskEngine(const TaskEngine&) = delete;
  TaskEngine& operator=(const TaskEngine&) = delete;

  void register_handler(std::string kind, TaskHandler handler);
  bool has_handler(std::string_view kind) const;

  Result<std::string> submit(std::string_view kind, const Json& params = Json::object(),
                             const SubmitOptions& opts = {});
  Result<std::optional<TaskRecord>> get(std::string_view id);
  Result<std::vector<TaskRecord>> list(const TaskFilter& filter = {});

  // Runs one task to a stable state in the calling thread (retries included:
  // loops while the task goes back to pending and attempts remain).
  // Returns the final record.
  Result<TaskRecord> run_sync(std::string_view id);
  // Runs every pending task with a registered handler (calling thread).
  // Returns how many runs happened.
  Result<int> run_pending();

  // Crash recovery: running -> pending (checkpoint kept). Returns count.
  Result<int> recover_interrupted();

  Status cancel(std::string_view id);
  Status pause(std::string_view id);   // pending -> paused; running -> asks handler to yield
  Status resume(std::string_view id);  // paused/failed -> pending (failed: attempts reset)

  // Background workers (policy: config loom_task_workers). start() is
  // idempotent; stop() joins. Workers pick pending tasks with a handler.
  void start(int workers = 1);
  void stop();
  void wake();
  bool running() const noexcept { return started_.load(); }

 private:
  class Ctx;
  Result<std::optional<TaskRecord>> claim_next();
  Result<bool> claim(std::string_view id);
  Result<TaskRecord> execute(TaskRecord rec);
  Status transition(TaskRecord& rec, std::string_view status, const std::string& error = {});
  Status write_record(const TaskRecord& rec);
  void worker_loop();

  Database& db_;
  EventLog& log_;
  EventBus* bus_;

  mutable std::mutex mu_;
  std::map<std::string, TaskHandler, std::less<>> handlers_;
  std::set<std::string, std::less<>> cancel_requests_;
  std::set<std::string, std::less<>> pause_requests_;
  std::set<std::string, std::less<>> running_ids_;
  std::condition_variable cv_;
  std::vector<std::thread> workers_;
  std::atomic<bool> started_{false};
  bool stopping_ = false;
  bool wake_flag_ = false;
};

}  // namespace loom
