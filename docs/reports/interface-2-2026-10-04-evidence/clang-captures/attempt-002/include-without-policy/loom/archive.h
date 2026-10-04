// loom/archive.h — Archive Intelligence / Project Compiler (MEGA MASTER §4.9,
// §7 self-hosting test, §13, §16 "Archive-to-Project vertical slice").
//
// Input: any mix of chat exports (ChatGPT / Claude incl. projects and
// memories), documents, a code repository and its git history.
// Output: a canonical source store (BlobStore + loom_sources + provenance),
// a graph, a source map, a decision/fork timeline, MASTER.md, a gap report
// (spec <-> repository), a machine-readable project manifest and a
// resumable task log.
//
// Pipeline (every stage is a TaskEngine task, kind "archive.<stage>"):
//   ingest    sources -> corpus of documents ("docs"), each with a stable key
//             derived from content hashes (never from random row ids), so the
//             same inputs give the same corpus in any data directory
//   retrieve  lexical pass (Database FTS5/BM25, LIKE fallback) with the
//             current vocabulary; titles are never used as a filter
//   expand    salient terms of the hits (TF-IDF contrast against the corpus,
//             regex NER entities, co-occurrence with the vocabulary); every
//             added term records why + evidence (provenance). retrieve/expand
//             alternate until no new terms appear or max_passes is reached
//   graph     semantic analysis of hits (SemanticLLM when configured, regex
//             otherwise) into the knowledge graph + a per-document term graph
//   cluster   deterministic Louvain community detection on the term graph
//   timeline  chronological path per theme, ChatGPT branch forks, message
//             version groups, commit history
//   items     typed items: idea, decision, rejected_option, open_question,
//             implementation, bug, requirement, invariant, rationale
//             (bilingual PL+EN cue phrases, deterministic, with confidence;
//             optional LLM refinement when llm = "auto" and a key exists)
//   relate    supersedes / contradicts edges between items (nothing deleted)
//   synthesize MASTER.md, source_map.csv, timeline.json, items.jsonl,
//             graph.json, project_manifest.json, gap_report.md, task_log.jsonl
//             (+ terms discovered during synthesis feed another round,
//             bounded by max_synthesis_rounds)
//   materialize artifacts -> BlobStore + loom_artifacts (+ out_dir), items
//             and themes -> graph nodes with supersedes/contradicts links
//
// Caching and resume: each stage's input hash covers its parameters and the
// output hashes of the stages it reads. Re-running with unchanged inputs finds
// the finished task (cache hit). A stage interrupted through the cancel token
// saves a checkpoint and yields (task status "paused"); the next run with the
// same inputs resumes it. A process crash leaves it "running", which
// TaskEngine::recover_interrupted() turns back into "pending".
//
// Determinism: without network (llm = "off", the default) every artifact is
// a pure function of the input bytes: no random ids, no wall-clock times,
// stable orderings with explicit tie-breaks.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {

class Runtime;

namespace archive {

inline constexpr std::string_view kPipelineVersion = "1";

struct ArchiveConfig {
  std::vector<std::string> sources;     // files, directories or zips (chat exports, docs)
  std::optional<std::string> repo;      // code repository root (code files + git history)
  bool code = true;                     // ingest repository files
  bool git = true;                      // ingest git history (desktop-only adapter)
  std::vector<std::string> seed_terms;  // empty -> derived (project manifest, repo name)
  std::string out_dir;                  // "" -> artifacts are only stored in the BlobStore
  std::string project;                  // display name; "" -> derived
  int max_passes = 3;                   // retrieve/expand iterations (>= 1)
  int max_new_terms = 8;                // terms added per expansion pass
  int max_hits_per_term = 0;            // BM25 top-k per term; 0 = auto (corpus/10, 25..200)
  int max_synthesis_rounds = 1;         // extra rounds fed by synthesis terms (k)
  std::string llm = "off";              // "off" | "auto" (refine items when a key exists)
  bool include_db = false;              // existing conversations join the corpus
  std::vector<std::string> exclude;     // extra path fragments skipped in the repo walk
  std::int64_t max_file_bytes = 1'000'000;
  bool force = false;                   // ignore cached stage results

  // Keys: sources, repo, code, git, seed_terms, out_dir, project, max_passes,
  // max_new_terms, max_hits_per_term, max_synthesis_rounds, llm, include_db,
  // exclude, max_file_bytes, force. Unknown keys -> InvalidArgument.
  static Result<ArchiveConfig> from_json(const Json& j);
  Json to_json() const;
};

// (stage, current, total, message). total < 0 when unknown.
using ArchiveProgressFn =
    std::function<void(std::string_view stage, std::int64_t current, std::int64_t total, std::string_view message)>;

struct StageRun {
  std::string stage;        // "ingest", "retrieve#2", ...
  std::string task_id;
  std::string input_hash;
  std::string output_hash;  // hash of the stage's deterministic payload
  bool cache_hit = false;
  bool resumed = false;     // continued from a checkpoint
  Json stats = Json::object();
  Json to_json() const;
};

struct ArchiveRunResult {
  std::string run_id;       // task id of the "archive.run" task
  std::string status;       // "done" | "paused" (cancelled; resumable) | "failed"
  std::vector<StageRun> stages;
  Json summary = Json::object();  // counts, themes, artifact list, out_dir
  Json to_json() const;
};

class ArchiveIntelligence {
 public:
  // Registers the "archive.*" task handlers on rt.tasks().
  explicit ArchiveIntelligence(Runtime& rt);
  ~ArchiveIntelligence();
  ArchiveIntelligence(const ArchiveIntelligence&) = delete;
  ArchiveIntelligence& operator=(const ArchiveIntelligence&) = delete;

  // Runs (or resumes) the whole pipeline in the calling thread. One run at a
  // time per runtime (Errc::Busy otherwise). Progress may be reported from a
  // Loom worker thread when a background worker picked up a stage task.
  // Cancellation -> status "paused" (resumable), not an error.
  Result<ArchiveRunResult> run(const ArchiveConfig& cfg, const ArchiveProgressFn& progress = {},
                               const CancelToken* cancel = nullptr);

  // Latest run (run_id empty) or a specific one: the run task, its stage
  // tasks and the artifacts it produced.
  Result<Json> status(std::string_view run_id = "");

  struct State;  // src/archive only

 private:
  Runtime& rt_;
  std::unique_ptr<State> st_;
};

}  // namespace archive
}  // namespace loom
