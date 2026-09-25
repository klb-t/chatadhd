#include "loom/tasks.h"

#include <chrono>
#include <exception>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/log.h"
#include "loom/provenance.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom {
namespace {
constexpr std::string_view kLog = "loom.tasks";
constexpr std::string_view kCols =
    "id, kind, status, params, input_hash, output_hash, checkpoint, attempts, max_attempts, error, parent_id, "
    "result, created, updated";

std::optional<std::string> none_if_empty(const std::string& s) {
  if (s.empty()) return std::nullopt;
  return s;
}

TaskRecord read_task(const sql::Stmt& st) {
  TaskRecord t;
  t.id = st.get_text(0);
  t.kind = st.get_text(1);
  t.status = st.get_text(2);
  t.params = json::parse_or(st.get_text(3), Json::object());
  t.input_hash = st.get_opt_text(4).value_or("");
  t.output_hash = st.get_opt_text(5).value_or("");
  if (!st.is_null(6)) t.checkpoint = json::parse_or(st.get_text(6), Json(nullptr));
  t.attempts = static_cast<int>(st.get_int(7));
  t.max_attempts = static_cast<int>(st.get_int(8));
  t.error = st.get_opt_text(9).value_or("");
  t.parent_id = st.get_opt_text(10).value_or("");
  if (!st.is_null(11)) t.result = json::parse_or(st.get_text(11), Json(nullptr));
  t.created = st.get_text(12);
  t.updated = st.get_text(13);
  return t;
}
}  // namespace

bool task_status::is_terminal(std::string_view s) noexcept { return s == kDone || s == kFailed || s == kCancelled; }

Json TaskRecord::to_json() const {
  return Json{{"id", id},
              {"kind", kind},
              {"status", status},
              {"params", params},
              {"input_hash", input_hash},
              {"output_hash", output_hash},
              {"checkpoint", checkpoint ? *checkpoint : Json(nullptr)},
              {"attempts", attempts},
              {"max_attempts", max_attempts},
              {"error", error},
              {"parent_id", parent_id},
              {"result", result ? *result : Json(nullptr)},
              {"created", created},
              {"updated", updated}};
}

// ── Context ────────────────────────────────────────────────────────
class TaskEngine::Ctx final : public TaskContext {
 public:
  Ctx(TaskEngine& eng, TaskRecord& rec) : eng_(eng), rec_(rec) {}
  const TaskRecord& task() const override { return rec_; }
  std::optional<Json> checkpoint() const override { return rec_.checkpoint; }
  Status save_checkpoint(const Json& state) override {
    auto lk = eng_.db_.lock();
    rec_.checkpoint = state;
    rec_.updated = timeutil::utc_now_iso();
    return eng_.db_.conn().run("UPDATE loom_tasks SET checkpoint = ?, updated = ? WHERE id = ?", json::py_dumps(state),
                               rec_.updated, rec_.id);
  }
  void progress(std::int64_t current, std::int64_t total, std::string_view status) override {
    if (eng_.bus_) {
      eng_.bus_->emit(events::kTaskProgress,
                      Json{{"id", rec_.id}, {"kind", rec_.kind}, {"current", current}, {"total", total},
                           {"status", std::string(status)}});
    }
  }
  void set_result(Json result) override { result_ = std::move(result); }
  bool cancelled() const override {
    std::lock_guard lk(eng_.mu_);
    return eng_.cancel_requests_.count(rec_.id) > 0;
  }
  bool pause_requested() const override {
    std::lock_guard lk(eng_.mu_);
    return eng_.pause_requests_.count(rec_.id) > 0;
  }
  void emit(std::string_view event, const Json& data) override {
    if (eng_.bus_) eng_.bus_->emit(event, data);
  }
  std::optional<Json> result_;

 private:
  TaskEngine& eng_;
  TaskRecord& rec_;
};

// ── Engine ─────────────────────────────────────────────────────────
TaskEngine::TaskEngine(Database& db, EventLog& log, EventBus* bus) : db_(db), log_(log), bus_(bus) {}

TaskEngine::~TaskEngine() { stop(); }

void TaskEngine::register_handler(std::string kind, TaskHandler handler) {
  std::lock_guard lk(mu_);
  handlers_[std::move(kind)] = std::move(handler);
}

bool TaskEngine::has_handler(std::string_view kind) const {
  std::lock_guard lk(mu_);
  return handlers_.find(kind) != handlers_.end();
}

Status TaskEngine::write_record(const TaskRecord& r) {
  auto lk = db_.lock();
  return db_.conn().run(
      "UPDATE loom_tasks SET status = ?, output_hash = ?, checkpoint = ?, attempts = ?, error = ?, result = ?, "
      "updated = ? WHERE id = ?",
      r.status, none_if_empty(r.output_hash),
      r.checkpoint ? std::optional<std::string>(json::py_dumps(*r.checkpoint)) : std::nullopt, r.attempts,
      none_if_empty(r.error), r.result ? std::optional<std::string>(json::py_dumps(*r.result)) : std::nullopt,
      r.updated, r.id);
}

Status TaskEngine::transition(TaskRecord& rec, std::string_view status, const std::string& error) {
  rec.status = std::string(status);
  rec.error = error;
  rec.updated = timeutil::utc_now_iso();
  LOOM_TRY(write_record(rec));
  Json payload{{"kind", rec.kind}, {"attempts", rec.attempts}};
  if (!error.empty()) payload["error"] = error;
  auto ev = log_.append("task:" + rec.status, rec.id, payload, rec.input_hash, rec.output_hash);
  if (!ev) log::warn(kLog, "event log append failed: {}", ev.error().message);
  if (bus_) {
    bus_->emit(events::kTaskChanged, Json{{"id", rec.id}, {"kind", rec.kind}, {"status", rec.status},
                                          {"attempts", rec.attempts}, {"error", rec.error}});
  }
  return {};
}

Result<std::string> TaskEngine::submit(std::string_view kind, const Json& params, const SubmitOptions& opts) {
  if (kind.empty()) return Error(Errc::InvalidArgument, "task kind required");
  TaskRecord rec;
  rec.id = gen_id(id_prefix::kTask);
  rec.kind = std::string(kind);
  rec.params = params.is_null() ? Json::object() : params;
  rec.input_hash = opts.input_hash.empty() ? Sha256::hex(json::canonical(rec.params)) : opts.input_hash;
  rec.max_attempts = opts.max_attempts > 0 ? opts.max_attempts : 1;
  rec.parent_id = opts.parent_id;
  rec.created = rec.updated = timeutil::utc_now_iso();
  {
    auto lk = db_.lock();
    if (opts.dedupe) {
      LOOM_TRY_ASSIGN(auto existing, db_.conn().query_text(
                                         "SELECT id FROM loom_tasks WHERE kind = ? AND input_hash = ? AND status NOT IN "
                                         "('failed', 'cancelled') ORDER BY created DESC, rowid DESC LIMIT 1",
                                         rec.kind, rec.input_hash));
      if (existing) return *existing;
    }
    LOOM_TRY(db_.conn().run("INSERT INTO loom_tasks (" + std::string(kCols) +
                                ") VALUES (?, ?, 'pending', ?, ?, NULL, NULL, 0, ?, NULL, ?, NULL, ?, ?)",
                            rec.id, rec.kind, json::py_dumps(rec.params), rec.input_hash, rec.max_attempts,
                            none_if_empty(rec.parent_id), rec.created, rec.updated));
  }
  auto ev = log_.append("task:pending", rec.id, Json{{"kind", rec.kind}, {"attempts", 0}}, rec.input_hash);
  if (!ev) log::warn(kLog, "event log append failed: {}", ev.error().message);
  if (bus_) bus_->emit(events::kTaskChanged, Json{{"id", rec.id}, {"kind", rec.kind}, {"status", "pending"}});
  wake();
  return rec.id;
}

Result<std::optional<TaskRecord>> TaskEngine::get(std::string_view id) {
  auto lk = db_.lock();
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare("SELECT " + std::string(kCols) + " FROM loom_tasks WHERE id = ?"));
  st.bind(1, id);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return std::optional<TaskRecord>{};
  return std::optional<TaskRecord>(read_task(st));
}

Result<std::vector<TaskRecord>> TaskEngine::list(const TaskFilter& f) {
  auto lk = db_.lock();
  std::string sql = "SELECT " + std::string(kCols) + " FROM loom_tasks WHERE 1=1";
  if (f.kind) sql += " AND kind = ?";
  if (f.status) sql += " AND status = ?";
  if (f.parent_id) sql += " AND parent_id = ?";
  sql += " ORDER BY created DESC, rowid DESC LIMIT ?";
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare(sql));
  int i = 1;
  if (f.kind) st.bind(i++, *f.kind);
  if (f.status) st.bind(i++, *f.status);
  if (f.parent_id) st.bind(i++, *f.parent_id);
  st.bind(i, f.limit);
  std::vector<TaskRecord> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    out.push_back(read_task(st));
  }
  return out;
}

Result<bool> TaskEngine::claim(std::string_view id) {
  auto lk = db_.lock();
  LOOM_TRY(db_.conn().run(
      "UPDATE loom_tasks SET status = 'running', attempts = attempts + 1, updated = ? WHERE id = ? AND status = "
      "'pending'",
      timeutil::utc_now_iso(), id));
  return db_.conn().changes() == 1;
}

Result<std::optional<TaskRecord>> TaskEngine::claim_next() {
  std::vector<std::pair<std::string, std::string>> pending;
  {
    auto lk = db_.lock();
    LOOM_TRY_ASSIGN(auto st, db_.conn().prepare(
                                 "SELECT id, kind FROM loom_tasks WHERE status = 'pending' ORDER BY created, rowid "
                                 "LIMIT 64"));
    while (true) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      pending.emplace_back(st.get_text(0), st.get_text(1));
    }
  }
  for (const auto& [id, kind] : pending) {
    if (!has_handler(kind)) continue;
    LOOM_TRY_ASSIGN(bool ok, claim(id));
    if (!ok) continue;
    LOOM_TRY_ASSIGN(auto rec, get(id));
    if (rec) return rec;
  }
  return std::optional<TaskRecord>{};
}

Result<TaskRecord> TaskEngine::execute(TaskRecord rec) {
  // `rec` is already claimed (status running, attempts incremented).
  TaskHandler handler;
  {
    std::lock_guard lk(mu_);
    auto it = handlers_.find(rec.kind);
    if (it != handlers_.end()) handler = it->second;
  }
  auto ev = log_.append("task:running", rec.id, Json{{"kind", rec.kind}, {"attempts", rec.attempts}}, rec.input_hash);
  if (!ev) log::warn(kLog, "event log append failed: {}", ev.error().message);
  if (bus_) bus_->emit(events::kTaskChanged, Json{{"id", rec.id}, {"kind", rec.kind}, {"status", "running"},
                                                  {"attempts", rec.attempts}});
  {
    std::lock_guard lk(mu_);
    running_ids_.insert(rec.id);
  }
  Status outcome;
  Ctx ctx(*this, rec);
  if (!handler) {
    outcome = Error(Errc::NotFound, "no handler registered for task kind " + rec.kind);
  } else {
    try {
      outcome = handler(ctx);
    } catch (const std::exception& e) {
      outcome = Error(Errc::Internal, std::string("handler threw: ") + e.what());
    } catch (...) {
      outcome = Error(Errc::Internal, "handler threw a non-standard exception");
    }
  }
  bool cancel_req = ctx.cancelled();
  bool pause_req = ctx.pause_requested();
  bool shutting_down = false;
  {
    std::lock_guard lk(mu_);
    cancel_requests_.erase(rec.id);
    pause_requests_.erase(rec.id);
    running_ids_.erase(rec.id);
    shutting_down = stopping_;
  }
  if (outcome) {
    rec.result = ctx.result_ ? *ctx.result_ : Json(nullptr);
    rec.output_hash = Sha256::hex(json::canonical(*rec.result));
    LOOM_TRY(transition(rec, task_status::kDone));
  } else if (outcome.error().code == Errc::Cancelled || cancel_req) {
    LOOM_TRY(transition(rec, task_status::kCancelled, outcome.error().message));
  } else if (outcome.error().code == Errc::Paused || pause_req) {
    rec.attempts = std::max(0, rec.attempts - 1);  // yielding is not a failed attempt
    // A yield forced by engine shutdown goes back to pending so the next
    // start() resumes it from its checkpoint; a user pause stays paused.
    LOOM_TRY(transition(rec, shutting_down ? task_status::kPending : task_status::kPaused,
                        shutting_down ? "interrupted by shutdown" : ""));
  } else if (rec.attempts < rec.max_attempts && outcome.error().code != Errc::NotFound) {
    log::warn(kLog, "task {} ({}) attempt {}/{} failed: {}", rec.id, rec.kind, rec.attempts, rec.max_attempts,
              outcome.error().to_string());
    LOOM_TRY(transition(rec, task_status::kPending, outcome.error().to_string()));
  } else {
    log::error(kLog, "task {} ({}) failed: {}", rec.id, rec.kind, outcome.error().to_string());
    LOOM_TRY(transition(rec, task_status::kFailed, outcome.error().to_string()));
  }
  return rec;
}

Result<TaskRecord> TaskEngine::run_sync(std::string_view id) {
  while (true) {
    LOOM_TRY_ASSIGN(auto rec, get(id));
    if (!rec) return Error(Errc::NotFound, "task not found: " + std::string(id));
    if (rec->status != task_status::kPending) {
      if (rec->status == task_status::kRunning) return Error(Errc::Busy, "task is already running: " + rec->id);
      return *rec;
    }
    if (!has_handler(rec->kind)) return Error(Errc::NotFound, "no handler registered for task kind " + rec->kind);
    LOOM_TRY_ASSIGN(bool claimed, claim(id));
    if (!claimed) continue;  // raced with a worker; re-read
    LOOM_TRY_ASSIGN(auto fresh, get(id));
    if (!fresh) return Error(Errc::NotFound, "task vanished: " + std::string(id));
    LOOM_TRY_ASSIGN(auto done, execute(*fresh));
    if (done.status != task_status::kPending) return done;
  }
}

Result<int> TaskEngine::run_pending() {
  int runs = 0;
  while (true) {
    LOOM_TRY_ASSIGN(auto rec, claim_next());
    if (!rec) break;
    LOOM_TRY(execute(*rec));
    ++runs;
  }
  return runs;
}

Result<int> TaskEngine::recover_interrupted() {
  std::vector<std::string> ids;
  {
    auto lk = db_.lock();
    LOOM_TRY_ASSIGN(auto st, db_.conn().prepare("SELECT id FROM loom_tasks WHERE status = 'running'"));
    while (true) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      ids.push_back(st.get_text(0));
    }
  }
  int n = 0;
  for (const auto& id : ids) {
    LOOM_TRY_ASSIGN(auto rec, get(id));
    if (!rec || rec->status != task_status::kRunning) continue;
    // The interrupted run does not count as a failed attempt.
    rec->attempts = std::max(0, rec->attempts - 1);
    LOOM_TRY(transition(*rec, task_status::kPending, "interrupted; resuming from checkpoint"));
    ++n;
  }
  if (n > 0) {
    log::info(kLog, "Recovered {} interrupted task(s)", n);
    wake();
  }
  return n;
}

Status TaskEngine::cancel(std::string_view id) {
  LOOM_TRY_ASSIGN(auto rec, get(id));
  if (!rec) return Error(Errc::NotFound, "task not found: " + std::string(id));
  if (rec->status == task_status::kCancelled) return {};
  if (task_status::is_terminal(rec->status)) return Error(Errc::Conflict, "task already " + rec->status);
  if (rec->status == task_status::kRunning) {
    std::lock_guard lk(mu_);
    cancel_requests_.emplace(rec->id);
    return {};
  }
  return transition(*rec, task_status::kCancelled, "cancelled");
}

Status TaskEngine::pause(std::string_view id) {
  LOOM_TRY_ASSIGN(auto rec, get(id));
  if (!rec) return Error(Errc::NotFound, "task not found: " + std::string(id));
  if (rec->status == task_status::kPaused) return {};
  if (rec->status == task_status::kRunning) {
    std::lock_guard lk(mu_);
    pause_requests_.emplace(rec->id);
    return {};
  }
  if (rec->status != task_status::kPending) return Error(Errc::Conflict, "cannot pause a task that is " + rec->status);
  return transition(*rec, task_status::kPaused);
}

Status TaskEngine::resume(std::string_view id) {
  LOOM_TRY_ASSIGN(auto rec, get(id));
  if (!rec) return Error(Errc::NotFound, "task not found: " + std::string(id));
  if (rec->status == task_status::kPending || rec->status == task_status::kRunning) return {};
  if (rec->status == task_status::kFailed) {
    rec->attempts = 0;
  } else if (rec->status != task_status::kPaused) {
    return Error(Errc::Conflict, "cannot resume a task that is " + rec->status);
  }
  LOOM_TRY(transition(*rec, task_status::kPending));
  wake();
  return {};
}

void TaskEngine::start(int workers) {
  std::lock_guard lk(mu_);
  if (started_.load()) return;
  stopping_ = false;
  started_.store(true);
  int n = workers > 0 ? workers : 1;
  for (int i = 0; i < n; ++i) workers_.emplace_back([this] { worker_loop(); });
}

void TaskEngine::stop() {
  std::vector<std::thread> ws;
  {
    std::lock_guard lk(mu_);
    if (!started_.load()) return;
    stopping_ = true;
    // Ask running handlers to yield; they go back to pending (resumable).
    for (const auto& id : running_ids_) pause_requests_.insert(id);
    ws.swap(workers_);
  }
  cv_.notify_all();
  for (auto& t : ws) {
    if (t.joinable()) t.join();
  }
  std::lock_guard lk(mu_);
  started_.store(false);
}

void TaskEngine::wake() {
  {
    std::lock_guard lk(mu_);
    wake_flag_ = true;
  }
  cv_.notify_all();
}

void TaskEngine::worker_loop() {
  while (true) {
    {
      std::unique_lock lk(mu_);
      if (stopping_) return;
    }
    auto rec = claim_next();
    if (!rec) {
      log::warn(kLog, "claim failed: {}", rec.error().message);
    } else if (rec->has_value()) {
      auto r = execute(**rec);
      if (!r) log::warn(kLog, "task execution bookkeeping failed: {}", r.error().message);
      continue;
    }
    std::unique_lock lk(mu_);
    cv_.wait_for(lk, std::chrono::seconds(1), [this] { return stopping_ || wake_flag_; });
    wake_flag_ = false;
    if (stopping_) return;
  }
}

}  // namespace loom
