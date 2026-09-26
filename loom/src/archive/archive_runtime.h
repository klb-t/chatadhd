// Internal: the parts of the archive pipeline that touch the Runtime
// (planning, ingest adapters) and the synthesis renderer.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "archive/archive_internal.h"
#include "loom/archive.h"
#include "loom/util/cancel.h"

namespace loom {
class Runtime;
class TaskContext;
}  // namespace loom

namespace loom::archive {

// ── Planning (src/archive/ingest.cpp) ───────────────────────────────
struct PlannedFile {
  std::string path;     // absolute (never hashed into input hashes)
  std::string uri;      // display path
  std::string adapter;  // text | code | json | zip | import
  std::string hash;     // sha256 of the bytes
  std::int64_t size = 0;
  Json to_json(bool with_path) const;
  static PlannedFile from_json(const Json& j);
};

struct Plan {
  std::vector<PlannedFile> files;  // sorted, deduplicated
  std::string repo_path;           // absolute ("" = none)
  std::string repo_name;
  bool git = false;                // git adapter enabled and available
  std::string git_head;
  bool include_db = false;
  std::string db_fingerprint;
  std::string project;
  std::vector<std::string> warnings;
  // Stable fingerprint (no absolute paths) used for the ingest input hash.
  Json fingerprint() const;
  Json to_json() const;  // with absolute paths (task params)
  static Plan from_json(const Json& j);
};

Result<Plan> make_plan(Runtime& rt, const ArchiveConfig& cfg);

// Runs `git` (desktop-only adapter). Errc::Unsupported on platforms without
// a shell; Errc::Unavailable when git is missing or the path is not a repo.
Result<std::string> run_git(const std::string& repo, const std::vector<std::string>& args);

struct IngestResult {
  Corpus corpus;
  Json bindings = Json::object();  // doc key -> {"msg","conv"} (data-dir specific)
  Json stats = Json::object();
};

// Progress + cooperative stop for long stages. should_stop() true -> the
// stage saves its checkpoint and returns Errc::Paused.
struct StageControl {
  std::function<void(std::int64_t, std::int64_t, std::string_view)> progress;
  std::function<bool()> should_stop;
  std::function<Status(const Json&)> checkpoint;  // persist resumable state
  std::optional<Json> resume_from;
};

Result<IngestResult> run_ingest(Runtime& rt, const Plan& plan, StageControl& ctl);

// ── Synthesis (src/archive/synth.cpp) ───────────────────────────────
struct SynthesisInput {
  const Corpus* corpus = nullptr;
  const CorpusStats* stats = nullptr;
  std::string project;
  std::vector<TermRecord> vocab;
  Json passes = Json::array();    // [{"pass","terms","hits","new_hits","added"}]
  Json hits = Json::array();      // final retrieval hits [{"key","score","terms"}]
  Json themes = Json::array();    // cluster output
  Json doc_theme = Json::object();
  Json global_terms = Json::array();
  Json timeline = Json::array();  // per-theme timelines
  std::vector<Item> items;
  std::vector<ItemEdge> edges;
  int round = 0;
  Json settings = Json::object(); // deterministic config echo
};

struct SynthesisOutput {
  std::map<std::string, std::string> files;  // name -> content
  std::vector<std::string> discovered_terms;
  Json gap = Json::object();
};

SynthesisOutput synthesize(const SynthesisInput& in);

// "[title › label @ date]"
std::string source_ref(const Doc& d);

}  // namespace loom::archive
