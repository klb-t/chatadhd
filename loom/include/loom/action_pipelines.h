// loom/action_pipelines.h — the long pipelines (archive analysis, knowledge build, catalogue scan) as background jobs a conversation can start, ask about
// and stop (P17 4). The pipelines themselves block until they finish; a sentence cannot wait for them, so each runs on a thread of its own here, one per
// kind, with its progress and its result kept for the question "how is it going". Nothing about a pipeline is changed: the same configuration, the same
// resumable tasks, the same single run at a time that the pipeline itself keeps (Busy otherwise). The actions that use this are archive.start, archive.status,
// archive.cancel and the same for knowledge and catalog (ops.cpp); what the user is asked before one starts is the proposal of the action (the rules, the
// spending limits, the x10 guard apply as to any action).
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::actions::pipelines {

inline constexpr std::string_view kArchive = "archive";
inline constexpr std::string_view kKnowledge = "knowledge";
inline constexpr std::string_view kCatalog = "catalog";
bool is_kind(std::string_view kind);

class Jobs {
 public:
  explicit Jobs(Runtime& rt);
  ~Jobs();  // stops every job at its next checkpoint and waits for the threads
  Jobs(const Jobs&) = delete;
  Jobs& operator=(const Jobs&) = delete;

  // Checks the configuration the way the pipeline itself does (an unknown key is refused here, before anything runs) and starts it on a thread of its
  // own. Busy while a job of this kind runs. -> {kind, state: "running", started, config}
  Result<Json> start(std::string_view kind, const Json& config);
  // Where the last job of this kind stands: {kind, state: idle | running | done | paused | cancelled | failed, started?, finished?, progress?: {stage,
  // current, total, message}, result?: {status, run_id?, summary}, error?, config?}. "idle": none ran in this process.
  Json status(std::string_view kind) const;
  // Asks a running job to pause at its next checkpoint (a paused archive or knowledge run resumes on the next start with the same inputs).
  // NotFound when none runs.
  Result<Json> cancel(std::string_view kind);
  // Waits for the job of this kind to end (tests, shutdown). Returns at once when none runs.
  void wait(std::string_view kind);

 private:
  struct Job;
  Job& job(std::string_view kind) const;
  void run(std::string_view kind, Job& job, Json config);

  Runtime& rt_;
  mutable std::mutex mu_;
  mutable std::map<std::string, std::unique_ptr<Job>, std::less<>> jobs_;
};

}  // namespace loom::actions::pipelines
