// loom/resolve.h — entity resolution, lineage (incl. code lineage) and
// assessment (LOOM_CONCEPTUAL_MODEL §2.1, §2.3, §6.3–§6.4). Area:
// extract+resolve. STATUS: contract + stubs ("// STUB: knowledge-wave",
// Errc::NotImplemented).
//
// ── Entity resolution ───────────────────────────────────────────────
// Candidates are blocked by normalizer phrase key (PL + EN, glossary: "czat
// ADHD" == "chat adhd"), alias key and trigram bucket; a pair is scored from
// key equality, lexicon aliases, acronym/camel splits, context vectors
// (co-occurring concept keys) and kind agreement; pairs are merged by
// union-find in (score desc, id, id) order above policy thresholds
// (thresholds.concepts.merge_tau). Context gates: an ambiguous alias counts
// only with its required context (profiles/self.json). Split guard: a
// would-be cluster whose context vectors are bimodal ("Loom" the kernel vs
// "loom" the weaving word) is not merged (thresholds.concepts.
// split_guard_bimodality). Every merge is a `same_as` claim with reasons;
// the owner's merge/split judgements are applied last (KnowledgeStore
// replay). LINEAGE (evolved_into, merged_into, forked_from) is a claim
// between entities, NEVER an alias merge.
//
// ── Code lineage ────────────────────────────────────────────────────
// For a snapshot (a recovered source tree) and a history (git revisions):
// for every file, the nearest revision by content (smallest diff: changed
// lines; ties -> earlier date, then id); then a vote over files (weighted by
// file size, unchanged files count most) picks the base revision. This is
// how 0.8.3 <- 0.7.9 and 0.9.0 <- 0.7.10 (before the abspath fix) were
// recovered (docs/history/ANALIZA_v0.8.3_v0.9.0.md). The result becomes
// `forked_from` / `based_on` claims (evidence derived, origin repo) with the
// per-file votes as support, and a Fork of kind code_lineage.
//
// ── Assessment ──────────────────────────────────────────────────────
// calibrate(): raw support -> calibrated confidence (unit-deduplicated
// noisy-OR of extractor/rule reliabilities, then the isotonic map per
// evidence class: policy/calibration.json). detect_conflicts(): two or more
// supported, incompatible values of one (subject, predicate[, qualifiers])
// -> all stay; each is marked contested, the conflict records the candidate
// resolution (later explicit decision, reversal, authority order of
// origins: user > archive = repo > external_authority > system >
// model_knowledge). Conflict is a state, not an evidence class.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/kb.h"
#include "loom/model.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
namespace knowledge {
struct StageContext;
}

namespace resolve {

inline constexpr std::string_view kResolverVersion = "1";

// {"a","b","score","merged","blocked_by":"" | "kind" | "split_guard" | "negative_context" | "judgement",
//  "reasons":[...]}
struct MergeDecision {
  std::string a;
  std::string b;
  double score = 0.0;
  bool merged = false;
  std::string blocked_by;
  Json reasons = Json::array();
  Json to_json() const;
};

// {"entities":[...],"remap":{"<old id>":"<canonical id>"},"same_as":[claims],"decisions":[...]}
struct ResolveResult {
  std::vector<model::Entity> entities;          // canonical entities with merged aliases
  std::map<std::string, std::string> remap;     // candidate id -> canonical id
  std::vector<model::Claim> same_as;            // one claim per merge (evidence derived, origin system)
  std::vector<MergeDecision> decisions;
  Json to_json() const;
};

class Resolver {
 public:
  explicit Resolver(std::shared_ptr<const kb::Pack> pack);
  // Resolves mention entities into canonical ones. `observations` give the
  // context windows (context gates, split guard).
  Result<ResolveResult> resolve(const std::vector<model::Entity>& candidates,
                                const std::vector<model::Observation>& observations) const;
  // Rewrites subject/object of claims through the remap (claim ids change:
  // the old claims are returned superseded, never deleted).
  std::vector<model::Claim> apply_remap(const std::vector<model::Claim>& claims, const ResolveResult& r) const;

 private:
  std::shared_ptr<const kb::Pack> pack_;
};

// A revision of a code base: a git commit / tag or a recovered snapshot.
//   {"id","label","date","files":{"<path>":"<content or sha256:hex>"}}
struct Revision {
  std::string id;
  std::string label;                              // "0.7.9", "1e3fa2b", "chatadhd_v0.9.0"
  std::string date;
  std::map<std::string, std::string> files;       // path -> content (or "sha256:<hex>" when only hashed)
  Json to_json() const;
  static Result<Revision> from_json(const Json& j);
};

// {"snapshot","base","confidence","votes":[{"revision","files","weight","similarity"}],
//  "per_file":{"<path>":{"revision","changed_lines","similarity"}}}
struct LineageResult {
  std::string snapshot;
  std::string base;                               // winning revision id
  double confidence = 0.0;
  Json votes = Json::array();
  Json per_file = Json::object();
  Json to_json() const;
};

// Nearest revision per file by content diff, then a vote (see above).
Result<LineageResult> code_lineage(const Revision& snapshot, const std::vector<Revision>& history);
// The lineage as claims (forked_from/based_on, evidence derived, origin repo)
// + a code_lineage Fork.
Result<std::vector<model::Claim>> lineage_claims(const LineageResult& lineage, std::string_view project_entity);

// {"subject","predicate","claims":[ids],"resolution":"later_decision|reversal|authority|user|open","winner"}
struct Conflict {
  std::string subject;
  std::string predicate;
  std::vector<std::string> claims;
  std::string resolution;
  std::string winner;                             // "" when open
  Json to_json() const;
};

// Calibrated confidence for every claim (in place, deterministic).
Status calibrate(const kb::Pack& pack, std::vector<model::Claim>& claims);
// Marks contested claims (status) and returns the conflicts.
Result<std::vector<Conflict>> detect_conflicts(std::vector<model::Claim>& claims);

// knowledge.resolve stage: resolve the run's mention entities, remap claims,
// code lineage for snapshots vs git history (config.repo), lineage claims.
// Params: {"snapshots"?:[dirs]}. -> {"output","stats":{"entities","merges","blocked","lineage":[...]}}
Result<Json> run_resolve_stage(knowledge::StageContext& ctx);
// knowledge.assess stage: calibrate, detect conflicts, replay the owner's
// judgements last (KnowledgeStore::replay_judgements).
// -> {"output","stats":{"claims","contested","replayed"}}
Result<Json> run_assess_stage(knowledge::StageContext& ctx);

}  // namespace resolve
}  // namespace loom
