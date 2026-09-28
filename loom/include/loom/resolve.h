// loom/resolve.h — entity resolution, lineage (incl. code lineage) and
// assessment (LOOM_CONCEPTUAL_MODEL §2.1, §2.3, §6.3–§6.4). Area:
// extract+resolve. STATUS: implemented (src/resolve/).
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

#include <filesystem>
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
class EmbeddingProvider;

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
// The code_lineage Fork of a lineage: base = the winning revision, sides =
// the snapshot (the fork) and the main line (the next history revision after
// the base, when there is one).
model::Fork lineage_fork(const LineageResult& lineage, const std::vector<Revision>& history,
                         std::string_view project_entity);
// Entity id of a snapshot / revision as a version entity (kind "version").
std::string revision_entity(std::string_view label);

// Revisions of a git repository: every non-merge commit (full history) that
// touched one of `paths`, each with the contents of those paths at that
// commit (each blob read once). id = full hash, label = the version of a
// "Version x.y.z" subject or the short hash, date = author date (UTC).
// Desktop only (runs `git`); Errc::Unsupported elsewhere.
Result<std::vector<Revision>> git_revisions(const std::filesystem::path& repo, const std::vector<std::string>& paths);
// A recovered source tree as a revision: text files relative to `dir`
// (skips .git, __pycache__, binaries). id = "snapshot:" + label.
Result<Revision> snapshot_revision(const std::filesystem::path& dir, std::string_view label = "");
// Changed lines between two texts (Myers: deleted + inserted lines).
std::size_t changed_lines(std::string_view a, std::string_view b);

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

// ── Vector layer (project attribution, similarity) ──────────────────
// One input to a vector space: text by default, or a media item by
// reference for a multimodal embedder (text + image + audio/video in one
// space). Vectors are cached by content hash + model id.
struct EmbedInput {
  std::string id;
  std::string modality = "text";  // text | image | audio | video
  std::string text;               // text modality (also a caption for media)
  std::string blob;               // media reference (path / blob id), provider-specific
  std::string content_hash;       // sha256 hex of the content ("" = of `text`)
};
// Sparse, l2-normalised vector (dense embeddings use keys "#0", "#1", ...).
using SparseVec = std::map<std::string, double>;
double cosine(const SparseVec& a, const SparseVec& b);

class VectorSpace {
 public:
  virtual ~VectorSpace() = default;
  // "tfidf" | "embedding:<model id>" — recorded on every claim it produced.
  virtual std::string method() const = 0;
  virtual std::vector<std::string> modalities() const = 0;
  // Corpus statistics (TF-IDF idf); embeddings ignore it.
  virtual Status fit(const std::vector<EmbedInput>& corpus) = 0;
  // One vector per input; an input of a modality the space lacks -> empty vector.
  virtual Result<std::vector<SparseVec>> vectors(const std::vector<EmbedInput>& inputs) = 0;
};

// Offline default: TF-IDF over normalizer match keys (PL + EN, glossary):
// smooth idf ln((1+n)/(1+df)) + 1, raw tf, l2 norm. Text only.
std::unique_ptr<VectorSpace> make_tfidf_space(std::shared_ptr<const kb::Pack> pack);

// A multimodal embedding capability (ProviderRegistry resource "embedding",
// capability "embed", constraints {"modalities":[...]}), e.g. a Google
// multimodal embedding model. Text-only EmbeddingProviders (selector.h)
// plug in through embedder_from_text_provider().
class MultimodalEmbedder {
 public:
  virtual ~MultimodalEmbedder() = default;
  virtual std::string model_id() const = 0;
  virtual std::vector<std::string> modalities() const = 0;
  virtual Result<std::vector<std::vector<float>>> embed(const std::vector<EmbedInput>& inputs) = 0;
};
std::shared_ptr<MultimodalEmbedder> embedder_from_text_provider(std::shared_ptr<EmbeddingProvider> provider);

// Vectors keyed by (content hash, model id). The memory cache serialises to
// JSON so a stage can persist it next to the data directory.
class EmbeddingCache {
 public:
  virtual ~EmbeddingCache() = default;
  virtual std::optional<std::vector<float>> get(std::string_view content_hash, std::string_view model) const = 0;
  virtual void put(std::string_view content_hash, std::string_view model, std::vector<float> v) = 0;
};
class MemoryEmbeddingCache : public EmbeddingCache {
 public:
  std::optional<std::vector<float>> get(std::string_view content_hash, std::string_view model) const override;
  void put(std::string_view content_hash, std::string_view model, std::vector<float> v) override;
  std::size_t size() const noexcept { return m_.size(); }
  Json to_json() const;  // {"<model>\x1f<hash>": [floats]}
  static MemoryEmbeddingCache from_json(const Json& j);

 private:
  std::map<std::string, std::vector<float>, std::less<>> m_;
};
std::unique_ptr<VectorSpace> make_embedding_space(std::shared_ptr<MultimodalEmbedder> embedder,
                                                  std::shared_ptr<EmbeddingCache> cache = nullptr);

// ── Project attribution (units that never name their project) ───────
// Profiles: one vector per project from its confidently attributed units
// (units that name it) — their text, the labels of the entities they
// mention (components, symbols, paths, architecture terms), principles and
// the project's aliases. Every unattributed unit is scored against every
// profile: score = w_cosine * cosine + w_context * context, where context
// combines temporal neighbours (exp(-days / time_scale) to the project's
// nearest unit), the same Claude project / gizmo, and shared rare
// identifiers (entities seen in <= rare_df units). Fixpoint: units above
// tau_confident and unambiguous join their project's profile; repeat
// (bounded, deterministic). Final pass: every candidate >= tau and >=
// ambiguity * best is chosen — ambiguous units keep several (multi-label).
// Every chosen label independently needs min_cosine or min_shared_terms rare
// identifiers. A scored centroid excludes the unit itself (including after
// fixpoint admission), so inferred membership cannot confirm itself.
// Output: an `about` claim per chosen target (evidence inferred, origin
// system, calibrated confidence, Expected Property "the unit mentions >= k
// terms of P", competing alternatives with scores, method recorded), and
// for an unambiguous unit inferred copies of its claims re-subjected to the
// project (original subject in qualifiers.extra.original_subject, the
// original and the `about` claim as premises).
//
// Shared foundations (multi-label): an entity (component, concept, protocol,
// storage, ...) used by >= 2 projects becomes a foundation entity (kind
// "foundation"); the entity is `instance_of` it and every project
// `uses_foundation` it (derived, origin system). Units whose best match is a
// foundation are `about` the foundation, and so reach every project using it.
// A located mention of its source entity provides direct affiliation context;
// the scored unit remains excluded from the foundation profile itself.
// Small project entities whose profile matches another project's profile
// (>= alias_tau, unambiguous) are inferred to be the same project: an
// inferred `same_as` claim and their aliases carried over (method "inferred").
struct AttributionConfig {
  // Scores are small on short texts (cosines of TF-IDF vectors), so the
  // fixpoint accepts by MARGIN over the runner-up, not by absolute level.
  int max_passes = 64;  // one unit per project per pass: bounded by the unit count
  double tau = 0.06;            // lowest score that may be chosen at all
  double tau_confident = 0.08;  // lowest score a fixpoint pass accepts ...
  double margin = 1.5;          // ... when best >= margin * runner-up
  double ambiguity = 0.8;       // final pass: every candidate >= ambiguity * best is kept
  double min_cosine = 0.1;      // evidence floor per label (or min_shared_terms rare identifiers)
  double w_cosine = 0.65;
  double w_context = 0.35;
  double time_scale_days = 45.0;
  int rare_df = 3;
  int min_shared_terms = 2;
  bool foundations = true;
  double alias_tau = 0.2;
  int alias_max_units = 3;
  // policy/thresholds.json "attribution" (missing keys keep the defaults)
  static AttributionConfig from_policy(const kb::Pack& pack);
};

struct AttributionCandidate {
  std::string target;  // project / foundation entity id
  std::string kind;    // project | foundation
  double score = 0.0;
  double cosine = 0.0;
  double context = 0.0;
  std::vector<std::string> shared;  // shared rare identifiers (labels)
  Json to_json() const;
};
struct UnitAttribution {
  std::string unit;
  std::string subject;                     // the unit's document entity
  std::vector<AttributionCandidate> candidates;  // best first
  std::vector<std::string> chosen;         // targets (several = ambiguous / multi-label)
  int pass = 0;                            // fixpoint pass that settled it (0 = final pass)
  Json to_json() const;
};
struct AttributionResult {
  std::string method;                      // vector space used
  std::vector<UnitAttribution> units;
  std::vector<model::Entity> foundations;
  std::vector<model::Entity> updated_entities;  // projects that gained inferred aliases
  std::vector<model::Claim> claims;        // about, uses_foundation, instance_of, inferred same_as
  std::vector<model::Claim> resubjected;   // inferred copies on the attributed project
  std::map<std::string, std::string> subject_of_unit;  // unit -> project (unambiguous only)
  Json stats = Json::object();
  Json to_json() const;
};
Result<AttributionResult> attribute_units(const kb::Pack& pack, const std::vector<model::Entity>& entities,
                                          const std::vector<model::Claim>& claims,
                                          const std::vector<model::Observation>& observations, VectorSpace& space,
                                          const AttributionConfig& cfg);

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

