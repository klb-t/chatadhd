// loom/knowledge.h — the knowledge pipeline (LOOM_CONCEPTUAL_MODEL §6) as
// resumable TaskEngine stages. Foundation-owned and implemented; the areas
// (catalog.h, extract.h, resolve.h, generalize.h, materialize.h) provide the
// stage functions, context_engine.h serves goal-directed context on demand.
//
//   stage        area header       task kind               reads (inputs)            writes
//   catalog      catalog.h         knowledge.catalog       sources, self-profile      loom_cat_* units/decisions; selected units
//   extract      extract.h         knowledge.extract       catalog                    observations, observed claims, areas, decisions, forks, statuses
//   resolve      resolve.h         knowledge.resolve       extract                    entities (resolved, aliases), lineage claims (incl. code lineage)
//   assess       resolve.h         knowledge.assess        resolve                    calibrated confidence, conflicts (contested), judgements replayed
//   generalize   generalize.h      knowledge.generalize    assess                     instances, principles, operators, models, inferred/extrapolated
//                                                                                      claims, predictions, transfers
//   materialize  materialize.h     knowledge.materialize   generalize                 self-description, dossiers, backlog, extrapolated spec (artifacts)
//
// Every stage is one TaskEngine task (kind "knowledge.<stage>") with
//   input_hash = sha256("knowledge.<stage>" | kPipelineVersion | pack hash |
//                       canonical(config fingerprint) | canonical(stage params) |
//                       output hashes of the stages it reads)
// so an unchanged re-run is a cache hit, any pack change (lexicon, rule,
// principle, threshold) re-runs exactly the dependent stages (I5), and a
// cancelled stage saves its checkpoint and resumes on the next run with the
// same inputs (the same contract as archive.h). The whole run is a
// "knowledge.run" task. The knowledge run id (kr_, KnowledgeStore) is
// KnowledgeRun::make_id(pack hash, config fingerprint + source content hashes): the same inputs write
// into the same run; derived rows are cleared by the stage that owns them
// before it writes (rebuild), and "assess" replays the owner's judgements
// last (I4).
//
// Relation to ArchiveIntelligence (archive.h): the knowledge pipeline is a
// separate run over the same sources and reuses the archive's pure helpers
// (src/archive/archive_internal.h: split_markdown, digest_code,
// walk_chatgpt/walk_claude, parse_git_log, classify_sentence). The archive
// pipeline stays as is; wiring `loom archive run --knowledge` to start a
// knowledge run after `relate` belongs to the catalog area (it owns the CLI
// commands of the layer).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/kb.h"
#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {

class Runtime;

namespace knowledge {

inline constexpr std::string_view kPipelineVersion = "7";

// The stages in execution order (closed set: wiring, not policy).
inline constexpr std::array<std::string_view, 6> kStages = {"catalog", "extract", "resolve", "assess", "generalize", "materialize"};
bool is_stage(std::string_view name) noexcept;
// The stage whose output a stage reads ("" for catalog).
std::string_view stage_input(std::string_view stage) noexcept;

struct KnowledgeConfig {
  std::vector<std::string> sources;       // exports / dirs / files: catalogued, imported selectively (R1)
  std::optional<std::string> repo;        // repository (code + git history)
  std::vector<std::string> stages;        // run these (in pipeline order); empty = all
  std::string prior_cut;                  // temporal holdout: model::PriorFilter::as_of ("" = none)
  bool priors = true;                     // false = no seed priors at all
  std::string llm = "off";                // "off" | "auto" (source-linked semantic candidates using semantic_model)
  std::string out_dir;                    // materialized files ("" = artifacts only)
  std::string project;                    // display name ("" = derived)
  bool force = false;                     // ignore cached stage results
  Json stage_params = Json::object();     // {"<stage>": {...}} per-stage options (documented by each area)

  // Keys: sources, repo, stages, prior_cut, priors, llm, out_dir, project,
  // force, stage_params. Unknown keys / stages -> InvalidArgument.
  static Result<KnowledgeConfig> from_json(const Json& j);
  Json to_json() const;
  // What the results depend on (no out_dir, no force): the run id input.
  Json fingerprint() const;
  model::PriorFilter prior_filter() const;
};

using ProgressFn =
    std::function<void(std::string_view stage, std::int64_t current, std::int64_t total, std::string_view message)>;

// Everything a stage function gets. Valid for the duration of the call.
struct StageContext {
  Runtime& rt;
  kb::KnowledgeStore& store;
  std::shared_ptr<const kb::Pack> pack;
  const kb::Normalizer& normalizer;
  const KnowledgeConfig& config;
  std::string run;                  // knowledge run id (kr_)
  std::string stage;                // "extract", ...
  Json params = Json::object();     // config.stage_params[stage]
  Json input = Json::object();      // result JSON of the stage it reads (null for catalog)
  model::PriorFilter priors;
  std::function<void(std::int64_t, std::int64_t, std::string_view)> progress;
  std::function<bool()> should_stop;              // true -> save a checkpoint, return Errc::Paused
  std::function<Status(const Json&)> checkpoint;  // persist resumable state
  std::optional<Json> resume_from;                // checkpoint of an interrupted attempt
};

// A stage returns its deterministic result:
//   {"output": "<sha256 of what it wrote, canonical>", "stats": {...}, ...stage-specific keys}
// "output" feeds the input hash of the next stage. Errc::NotImplemented from a
// stub stops the run with status "failed" and names the stage.
using StageFn = std::function<Result<Json>(StageContext&)>;

struct StageRun {
  std::string stage;
  std::string task_id;
  std::string input_hash;
  std::string output_hash;
  bool cache_hit = false;
  bool resumed = false;
  Json stats = Json::object();
  Json to_json() const;
};

struct RunResult {
  std::string task_id;          // knowledge.run task
  std::string run;              // knowledge run id (kr_)
  std::string pack_hash;
  std::string status;           // done | paused | failed | cancelled
  std::string error;            // "stage: message" when failed
  std::vector<StageRun> stages;
  Json summary = Json::object();
  Json to_json() const;
};

class KnowledgeEngine {
 public:
  // Registers the "knowledge.<stage>" and "knowledge.run" task handlers on
  // rt.tasks(). The default stage functions are the areas' run_stage
  // functions (see the table above).
  explicit KnowledgeEngine(Runtime& rt);
  ~KnowledgeEngine();
  KnowledgeEngine(const KnowledgeEngine&) = delete;
  KnowledgeEngine& operator=(const KnowledgeEngine&) = delete;

  // Built-in pack + <data_dir>/kb overlay, loaded once (thread-safe).
  Result<std::shared_ptr<const kb::Pack>> pack();
  kb::KnowledgeStore& store();

  // Replaces a stage implementation (tests; alternative implementations).
  void set_stage(std::string_view stage, StageFn fn);

  // Runs (or resumes) the pipeline in the calling thread; one run at a time
  // (Errc::Busy). Cancellation -> status "paused" (resumable).
  Result<RunResult> run(const KnowledgeConfig& cfg, const ProgressFn& progress = {}, const CancelToken* cancel = nullptr);
  // Latest knowledge.run task (task_id "") or a given one: {"run":{...},"stages":[...]}.
  Result<Json> status(std::string_view task_id = "");

  struct State;  // src/knowledge only

 private:
  Result<RunResult> orchestrate(const KnowledgeConfig& cfg, const std::string& run_task, const std::string& krun);
  Runtime& rt_;
  std::unique_ptr<State> st_;
};

}  // namespace knowledge
}  // namespace loom
