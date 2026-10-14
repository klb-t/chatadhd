// The long pipelines as background jobs a conversation can start. See include/loom/action_pipelines.h.
#include "loom/action_pipelines.h"

#include <algorithm>

#include "loom/archive.h"
#include "loom/catalog.h"
#include "loom/knowledge.h"
#include "loom/log.h"
#include "loom/runtime.h"
#include "loom/util/time.h"

namespace loom::actions::pipelines {

struct Jobs::Job {
  std::string kind;
  mutable std::mutex mu;
  std::thread thread;
  CancelToken token;
  std::string state = "idle";
  std::string started, finished;
  Json config = nullptr;
  Json progress = nullptr;
  Json result = nullptr;
  Json error = nullptr;
};

bool is_kind(std::string_view kind) { return kind == kArchive || kind == kKnowledge || kind == kCatalog; }

Jobs::Jobs(Runtime& rt) : rt_(rt) {}

Jobs::~Jobs() {
  std::vector<Job*> all;
  {
    std::lock_guard lk(mu_);
    for (auto& [kind, j] : jobs_) all.push_back(j.get());
  }
  for (Job* j : all) j->token.cancel();
  for (Job* j : all)
    if (j->thread.joinable()) j->thread.join();
}

Jobs::Job& Jobs::job(std::string_view kind) const {
  std::lock_guard lk(mu_);
  auto it = jobs_.find(kind);
  if (it == jobs_.end()) {
    auto fresh = std::make_unique<Job>();
    fresh->kind = std::string(kind);
    it = jobs_.emplace(std::string(kind), std::move(fresh)).first;
  }
  return *it->second;
}

Result<Json> Jobs::start(std::string_view kind, const Json& config) {
  if (!is_kind(kind)) return Error(Errc::InvalidArgument, "unknown pipeline '" + std::string(kind) + "'");
  // The configuration is checked as the pipeline checks it: a wrong key is the caller's to hear about now, not from a thread.
  if (kind == kArchive) {
    LOOM_TRY_ASSIGN(auto cfg, archive::ArchiveConfig::from_json(config));
    (void)cfg;
  } else if (kind == kKnowledge) {
    LOOM_TRY_ASSIGN(auto cfg, knowledge::KnowledgeConfig::from_json(config));
    (void)cfg;
  } else {
    LOOM_TRY_ASSIGN(auto cfg, catalog::ScanConfig::from_json(config));
    (void)cfg;
  }
  Job& j = job(kind);
  {
    std::lock_guard lk(j.mu);
    if (j.state == "running") return Error(Errc::Busy, "a " + std::string(kind) + " run is already in progress");
    if (j.thread.joinable()) j.thread.join();  // the last one has ended: its thread is only waiting to be collected
    j.token = CancelToken();
    j.state = "running";
    j.started = timeutil::utc_now_iso();
    j.finished.clear();
    j.config = config;
    j.progress = nullptr;
    j.result = nullptr;
    j.error = nullptr;
    j.thread = std::thread([this, name = std::string(kind), &j, config] { run(name, j, config); });
  }
  return Json{{"kind", std::string(kind)}, {"state", "running"}, {"started", j.started}, {"config", config}};
}

void Jobs::run(std::string_view kind, Job& j, Json config) {
  auto progress = [&j](std::string_view stage, std::int64_t current, std::int64_t total, std::string_view message) {
    std::lock_guard lk(j.mu);
    j.progress = Json{{"stage", std::string(stage)}, {"current", current}, {"total", total < 0 ? Json(nullptr) : Json(total)}, {"message", std::string(message)}};
  };
  std::string state = "done";
  Json result = nullptr, error = nullptr;
  try {
    if (kind == kArchive) {
      auto cfg = archive::ArchiveConfig::from_json(config);
      auto r = cfg ? rt_.archive().run(*cfg, progress, &j.token) : Result<archive::ArchiveRunResult>(cfg.error());
      if (!r) {
        state = "failed";
        error = Json{{"code", std::string(errc_name(r.error().code))}, {"message", r.error().message}};
      } else {
        state = r->status == "paused" ? (j.token.cancelled() ? "cancelled" : "paused") : (r->status == "failed" ? "failed" : "done");
        result = Json{{"status", r->status}, {"run_id", r->run_id}, {"summary", r->summary}, {"stages", r->stages.size()}};
      }
    } else if (kind == kKnowledge) {
      auto cfg = knowledge::KnowledgeConfig::from_json(config);
      auto r = cfg ? rt_.knowledge().run(*cfg, progress, &j.token) : Result<knowledge::RunResult>(cfg.error());
      if (!r) {
        state = "failed";
        error = Json{{"code", std::string(errc_name(r.error().code))}, {"message", r.error().message}};
      } else {
        state = r->status == "paused" ? (j.token.cancelled() ? "cancelled" : "paused") : (r->status == "failed" ? "failed" : (r->status == "cancelled" ? "cancelled" : "done"));
        result = Json{{"status", r->status}, {"run_id", r->run}, {"task_id", r->task_id}, {"summary", r->summary}, {"stages", r->stages.size()}};
        if (!r->error.empty()) error = Json{{"message", r->error}};
      }
    } else {
      auto cfg = catalog::ScanConfig::from_json(config);
      auto pack = rt_.knowledge().pack();
      if (!cfg || !pack) {
        state = "failed";
        const Error& e = !cfg ? cfg.error() : pack.error();
        error = Json{{"code", std::string(errc_name(e.code))}, {"message", e.message}};
      } else {
        catalog::Catalog cat(rt_, *pack);
        auto r = cat.scan(*cfg, progress, &j.token);
        if (!r) {
          state = j.token.cancelled() ? "cancelled" : "failed";
          error = Json{{"code", std::string(errc_name(r.error().code))}, {"message", r.error().message}};
        } else {
          result = Json{{"status", "done"}, {"summary", *r}};
        }
      }
    }
  } catch (const std::exception& e) {
    state = "failed";
    error = Json{{"code", "internal"}, {"message", e.what()}};
  }
  if (state == "failed") log::write(log::Level::Warning, "loom.pipelines", std::string(kind) + " run failed: " + (error.is_null() ? std::string() : json::get_string(error, "message")));
  std::lock_guard lk(j.mu);
  j.state = state;
  j.finished = timeutil::utc_now_iso();
  j.result = std::move(result);
  j.error = std::move(error);
}

Json Jobs::status(std::string_view kind) const {
  if (!is_kind(kind)) return Json{{"kind", std::string(kind)}, {"state", "unknown"}};
  Job& j = job(kind);
  std::lock_guard lk(j.mu);
  Json out{{"kind", std::string(kind)}, {"state", j.state}};
  if (!j.started.empty()) out["started"] = j.started;
  if (!j.finished.empty()) out["finished"] = j.finished;
  if (!j.config.is_null()) out["config"] = j.config;
  if (!j.progress.is_null()) out["progress"] = j.progress;
  if (!j.result.is_null()) out["result"] = j.result;
  if (!j.error.is_null()) out["error"] = j.error;
  return out;
}

Result<Json> Jobs::cancel(std::string_view kind) {
  if (!is_kind(kind)) return Error(Errc::InvalidArgument, "unknown pipeline '" + std::string(kind) + "'");
  Job& j = job(kind);
  std::lock_guard lk(j.mu);
  if (j.state != "running") return Error(Errc::NotFound, "no " + std::string(kind) + " run is in progress");
  j.token.cancel();
  return Json{{"kind", std::string(kind)}, {"state", "cancelling"}};
}

void Jobs::wait(std::string_view kind) {
  if (!is_kind(kind)) return;
  Job& j = job(kind);
  {
    std::lock_guard lk(j.mu);
    if (!j.thread.joinable()) return;
  }
  for (;;) {  // the thread takes j.mu to say it has ended, so the state is polled without holding it
    {
      std::lock_guard lk(j.mu);
      if (j.state != "running") break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  std::lock_guard lk(j.mu);
  if (j.thread.joinable()) j.thread.join();
}

}  // namespace loom::actions::pipelines
