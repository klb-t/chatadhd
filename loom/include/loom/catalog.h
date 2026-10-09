// loom/catalog.h — R1: catalogue multi-GB archives WITHOUT importing them,
// score every unit against the self-profile, select by policy rules and the
// owner's overrides, import only the selected units, by locator
// (LOOM_CONCEPTUAL_MODEL §1, §6.1). Area: catalog. STATUS: contract +
// stubs ("// STUB: knowledge-wave", Errc::NotImplemented); the catalog area
// implements src/catalog/ and src/capi/capi_catalog.cpp.
//
// ── Semantics ───────────────────────────────────────────────────────
// * A Unit (model.h) is an addressable part of an immutable source, found by
//   streaming the source (zip members inflated in a stream, JSON arrays
//   element by element with byte offsets): a conversation, a message, a
//   file, a commit, an e-mail, a recording segment. Units are catalogued
//   before anything is imported; core tables are never written by scan or
//   score (only import_selected writes conversations/messages, and only for
//   selected units, with a provenance row per message).
// * Unit ids are content-derived (model::Unit::make_id(source, locator));
//   the source id is "sha256:<hex>" of the raw file (or its loom_sources id
//   once registered), so catalogues are identical in any data directory.
// * The self-profile is built from the repository (paths, declared symbols,
//   config keys, git versions), the MEGA MASTER and the pack
//   (profiles/self.json aliases with context gates, philosophy phrasings —
//   filtered by the run's PriorFilter). Ambiguous aliases count only inside
//   their context window; negative contexts mark noise-trap candidates.
// * Scores: sigmoid(bias + sum w_i f_i) with weights from
//   policy/relevance.json; labels relevant (>= catalog.tau_relevant) |
//   candidate | irrelevant (policy/thresholds.json). Terms discovered by
//   later stages feed back as profile terms (origin "expansion").
// * Selection precedence (fixed, code): 1. owner override (include |
//   exclude | pin) 2. policy/selection_rules.json in order, first match wins
//   3. the label default (relevant -> include, candidate -> review,
//   irrelevant -> exclude). Every decision records decided_by and reasons.
//
// ── Tables (catalog-owned, lazily created by ensure_schema) ─────────
//   loom_cat_units       (id PK, source, kind, locator JSON, title, date, lang, bytes, platform, ext_id,
//                         content_hash, prev_version, sketch BLOB, body JSON)
//   loom_cat_profiles    (id PK, input_hash, body JSON, created)
//   loom_cat_scores      (run_id, unit_id) PK, score, label, features JSON, reasons JSON, projects JSON
//   loom_cat_overrides   (unit_id PK, action, reason, created)            owner input, append-only by seq
//   loom_cat_decisions   (run_id, unit_id) PK, selected, decided_by, reasons JSON
//   loom_cat_imports     (unit_id PK, conv_id, raw_blob, source_id, task_id, created)
// (A heavy sketch index may live in a separate derived file
// chatadhd.catalog.db, like chatadhd.fts.db: safe to delete, rebuilt by scan.)
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/kb.h"
#include "loom/model.h"
#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {

class Database;
class Runtime;
namespace knowledge {
struct StageContext;
}

namespace catalog {

inline constexpr std::string_view kScannerVersion = "4";

struct SketchParams {
  int top_k = 128;             // top-K normalised terms per unit
  double bloom_fpr = 0.01;
  int minhash = 64;            // MinHash lanes over word 5-shingles
  int max_mentions = 50;       // alias/version/principle mentions kept per unit
  std::int64_t access_interval = 64LL << 20;  // inflate snapshot every N uncompressed bytes (seek + resume)
  static SketchParams mobile();                // smaller K, fewer lanes
  Json to_json() const;
  static Result<SketchParams> from_json(const Json& j);
};

struct ScanConfig {
  std::vector<std::string> sources;   // files, directories, zips
  SketchParams sketch;
  int threads = 0;                    // 0 = cores - 1 (ordered commit keeps output identical)
  std::int64_t inflight_bytes = 64LL << 20;
  std::string retain_raw = "selected";  // selected | all | none (raw element bytes into the BlobStore)
  bool force = false;
  // Keys: sources, sketch, threads, inflight_bytes, retain_raw, force.
  static Result<ScanConfig> from_json(const Json& j);
  Json to_json() const;
};

// One catalogued unit: the model Unit plus what the catalog knows about it.
//   JSON: {"unit":{model Unit},"platform":"chatgpt|claude|file|git|email|...","ext_id",
//          "content_hash","n_msgs","n_chars","n_code_chars","n_forks","attachments":[...],
//          "project_ext_id","head","prev_version","mentions":[{"kind","key","snippet","offset"}]}
struct CatalogUnit {
  model::Unit unit;
  std::string platform;
  std::string ext_id;          // export id exactly as exported (evaluation join key)
  std::string content_hash;    // sha256 of the element's exact bytes
  int n_msgs = 0;
  std::int64_t n_chars = 0;
  std::int64_t n_code_chars = 0;
  int n_forks = 0;
  std::vector<std::string> attachments;
  std::string project_ext_id;  // Claude project / ChatGPT gizmo
  std::string head;            // first user message, clipped
  std::string prev_version;    // unit id of the same ext_id in an older export
  Json mentions = Json::array();
  Json to_json() const;
  static Result<CatalogUnit> from_json(const Json& j);
};

// The self-profile: typed probe terms with provenance.
//   JSON: {"id","input_hash","projects":[{"id","name","project_kind","facets"}],
//          "terms":[{"term","key","class":"alias|identifier|path|config_key|table|concept|principle|version|expansion",
//                    "weight","project","provenance","ambiguous","requires_context","negative_context"}]}
struct SelfProfile {
  std::string id;
  std::string input_hash;
  Json projects = Json::array();
  Json terms = Json::array();
  Json to_json() const;
  static Result<SelfProfile> from_json(const Json& j);
};

struct ProfileConfig {
  std::optional<std::string> repo;
  bool git = true;
  std::vector<std::string> documents;    // extra owner documents (MEGA MASTER, requirements)
  std::vector<std::string> extra_terms;  // owner-provided terms (class "alias", weight 3)
  model::PriorFilter priors;             // which philosophy phrasings may probe
  static Result<ProfileConfig> from_json(const Json& j);
  Json to_json() const;
};

struct ScoreConfig {
  std::string profile_id;       // "" = latest
  int max_passes = 3;           // expansion passes (policy caps it)
  std::string llm = "off";      // "off" | "batch" | "auto": optional triage of the candidate band only
  int verify_max_units = 3000;  // exact re-read of units admitted through sketch-only evidence
  static Result<ScoreConfig> from_json(const Json& j);
  Json to_json() const;
};

// {"unit_id","score","label":"relevant|candidate|irrelevant","features":{...},
//  "reasons":[{"feature","contribution","evidence"}],"projects":[...],"trap":"" | "<reason>"}
struct UnitScore {
  std::string unit_id;
  double score = 0.0;
  std::string label;
  Json features = Json::object();
  Json reasons = Json::array();
  std::vector<std::string> projects;
  std::string trap;             // noise-trap reason when a negative context dominated
  Json to_json() const;
  static Result<UnitScore> from_json(const Json& j);
};

// An owner override (always wins; recorded, replayed).
struct Override {
  std::string unit_id;
  std::string action;           // include | exclude | pin
  std::string reason;
  static Result<Override> from_json(const Json& j);
  Json to_json() const;
};

// {"unit_id","selected","decided_by":"user|rule:<id>|score","label","score","reasons":[...]}
struct Decision {
  std::string unit_id;
  bool selected = false;
  std::string decided_by;
  std::string label;
  double score = 0.0;
  Json reasons = Json::array();
  Json to_json() const;
  static Result<Decision> from_json(const Json& j);
};

struct UnitQuery {
  std::optional<std::string> label;
  std::optional<std::string> project;
  std::optional<std::string> text;     // match on title/head/terms
  std::optional<bool> selected;
  std::string run_id;                  // score run ("" = latest)
  std::string sort = "score";          // score | date | id
  int limit = 100;
  int offset = 0;
  static Result<UnitQuery> from_json(const Json& j);
  Json to_json() const;
};

// R1 asks for two import modes, both owner-toggleable (never a hidden
// default): a **full** import that losslessly ingests every catalogued unit
// of a source (no scoring/selection in the loop at all — the point of
// catalog+score+select is to make *selective* import possible, not to make
// full import impossible), and a **selective** import (the default) driven
// by select()'s decisions, expanded by a small set of independent inclusion
// rules that each default to what a careful reviewer would tick by hand:
//   - the selected unit itself;
//   - `include_project_siblings`: every other unit that shares its
//     project_ext_id (a Claude project / ChatGPT gizmo) — "everything inside
//     a matching project" (project_ext_id is not populated by scan() yet,
//     a disclosed gap; the option is wired so it activates for free once it
//     is);
//   - `related_time_window_hours`: same-platform units whose date falls
//     within this many hours of a selected unit's date (a lightweight
//     "same session" / temporally-nearby heuristic for related data);
//   - off-topic-thread stripping and referenced/linked-conversation
//     inclusion are DESIGNED but not implemented (they need the
//     per-message-span extraction and the cross-unit linking pass this area
//     does not yet have; see catalog.h's ownership map for extract/generalize).
// `store_mode` answers "copy vs. link to the source file": "copy" (default)
// retains each unit's hash-verified representation in the BlobStore and optionally writes
// message text into core tables. mode="full" additionally retains complete
// exact source files/ZIPs, including unknown metadata and binary members. Selective
// copy retains only chosen units (wrapper JSON elements may be reserialized).
// Normalised messages are a derived view;
// retained raw sources remain authoritative. "link" writes a placeholder carrying the
// unit's locator/content_hash in its metadata plus a provenance row, so nothing
// is duplicated into the database and the source file stays the copy of
// record (re-read on demand via read_unit(), verified against content_hash).
struct ImportOptions {
  std::string run_id;                  // decisions of this score run ("" = latest); ignored when mode == "full"
  bool dry_run = false;                // report units/bytes only
  bool import_messages = true;         // conversations -> core tables (with provenance)
  std::string mode = "selective";      // "selective" (select()'s decisions) | "full" (every catalogued unit, lossless)
  bool include_project_siblings = false;  // pull in every unit sharing a selected unit's project_ext_id
  int related_time_window_hours = 0;      // > 0: also pull in same-platform units within this many hours
  std::string store_mode = "copy";        // "copy" (durable raw bytes + optional messages) | "link" (locator only)
  static Result<ImportOptions> from_json(const Json& j);
  Json to_json() const;
};

using ProgressFn =
    std::function<void(std::string_view step, std::int64_t current, std::int64_t total, std::string_view message)>;

class Catalog {
 public:
  Catalog(Runtime& rt, std::shared_ptr<const kb::Pack> pack);

  // Creates the loom_cat_* tables (idempotent, lazy; see the header comment).
  static Status ensure_schema(Database& db);

  // Streams every source into units + sketches. Resumable (checkpoint =
  // file index + inflate snapshot). -> {"units","new","unchanged","versions","bytes","warnings":[...]}
  Result<Json> scan(const ScanConfig& cfg, const ProgressFn& progress = {}, const CancelToken* cancel = nullptr);
  Result<SelfProfile> build_profile(const ProfileConfig& cfg);
  // Scores every unit (passes: identity -> BM25 over sketches -> expansion +
  // links). -> {"run_id","relevant","candidate","irrelevant","traps","expanded_terms":[...]}
  Result<Json> score(const ScoreConfig& cfg, const ProgressFn& progress = {}, const CancelToken* cancel = nullptr);
  // Applies overrides + selection rules + label defaults.
  Result<std::vector<Decision>> select(std::string_view score_run = "");
  Status set_override(const Override& o);
  Result<std::vector<CatalogUnit>> query(const UnitQuery& q);
  // Metadata, score reasons, verified snippets, neighbours, cost estimate.
  Result<Json> preview(std::string_view unit_id);
  // The unit's exact bytes, read by locator (verified against content_hash).
  Result<std::string> read_unit(std::string_view unit_id);
  // Read-only graph projection through the same verified locator and provider
  // parsers as import. A failed refresh reports its error and retains the last
  // successful projection; retained data is never presented as current.
  Result<Json> read_resource(std::string_view unit_id, const Json& read_options = Json::object());
  // Trusted caller grant is separate from source/adapter metadata. The trace
  // uses the existing method registry and GraphPacket store, never a second engine.
  Result<Json> execute_resource(std::string_view unit_id, const Json& read_options, bool read_authorized);
  // Imports the selected units by locator. -> {"imported","skipped","bytes","conversations":[...]}
  Result<Json> import_selected(const ImportOptions& opts, const ProgressFn& progress = {},
                               const CancelToken* cancel = nullptr);
  // Terms discovered by later stages feed back into the profile.
  Status add_profile_terms(const Json& terms);
  // {"units","sources","profiles","score_runs","selected","imported"}
  Result<Json> status();

 private:
  Runtime& rt_;
  std::shared_ptr<const kb::Pack> pack_;
};

// knowledge.catalog stage: scan config.sources (+ repo), build the profile,
// score, select, import the selected units. Params (stage_params.catalog):
// {"scan":{ScanConfig},"score":{ScoreConfig},"import":{ImportOptions}}.
// -> {"output","stats","units":[selected unit ids],"profile_id","score_run"}
Result<Json> run_stage(knowledge::StageContext& ctx);

}  // namespace catalog
}  // namespace loom
