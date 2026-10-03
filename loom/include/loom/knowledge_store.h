// loom/knowledge_store.h — persistence of the knowledge layer (model.h) in
// Loom-only tables.
//
// ── Tables (created lazily and idempotently; I10) ───────────────────
// Nothing is created until the first KnowledgeStore write (or an explicit
// ensure_schema()), so a data directory that never runs the knowledge layer
// keeps exactly the wave-1 schema; the core schema v4 and every core table
// are never touched. The version of these tables is kb::kKbSchemaVersion,
// recorded in loom_kb_meta.schema_version and in _meta.loom_kb_schema_version
// (a separate key: _meta.loom_schema_version keeps tracking the wave-1 loom_*
// tables and stays "1"). Migrations are forward-only and guarded
// (CREATE ... IF NOT EXISTS, then ADD COLUMN for every missing column), so a
// directory with the v1 draft tables is upgraded in place.
//
// Derived state is keyed by run: every row carries run_id (a "knowledge run",
// id kr_ = hash(pack hash | inputs)); a rebuild clears the run and writes it
// again. Each row stores the object's canonical JSON (`body`, model to_json)
// plus the columns the indexed queries use:
//
//   loom_kb_runs            run_id PK, archive_run_id, pack_hash, status, inputs, summary, created,
//                           replayed_seq
//   loom_kb_observations    (run_id, id) PK; unit, kind, date                idx (run_id, unit)
//   loom_kb_entities        (run_id, id) PK; kind, canonical_key, parent, status
//                                                idx (run_id, kind), (run_id, canonical_key), (run_id, parent)
//   loom_kb_aliases         (run_id, entity_id, alias_key) PK; surface, lang, method, count, confidence
//                                                idx (run_id, alias_key)   [derived from Entity.aliases]
//   loom_kb_claims          (run_id, id) PK; subject, predicate, object, evidence, origin, status,
//                           confidence, branch, version
//                                                idx (run_id, subject, predicate), (run_id, object, predicate),
//                                                    (run_id, predicate), (run_id, evidence)
//   loom_kb_claim_support   (run_id, claim_id, observation_id) PK        idx (run_id, observation_id)
//   loom_kb_principles      (run_id, id) PK; level, form, validation, owner
//   loom_kb_operators       (run_id, id) PK; produces, validation
//   loom_kb_morphisms       (run_id, id) PK; use, validation
//   loom_kb_instances       (run_id, id) PK; paradigm_kind, paradigm, subject, model
//                                                idx (run_id, paradigm), (run_id, subject)
//   loom_kb_slot_values     (run_id, instance_id, slot, ord) PK; claim_id, role, conflict
//                                                idx (run_id, role), (run_id, claim_id)
//   loom_kb_areas           (run_id, id) PK; subject
//   loom_kb_decisions       (run_id, id) PK; subject, status, date
//   loom_kb_forks           (run_id, id) PK; kind, subject
//   loom_kb_status_records  (run_id, id) PK; entity, branch, version, status   idx (run_id, entity)
//   loom_kb_predictions     (run_id, id) PK; operator, cut, outcome
//   loom_kb_models          (run_id, id) PK; name, validation
//   loom_kb_products        (run_id, id) PK; kind, instance
//   loom_kb_judgements      id PK; seq (unique, increasing), target_kind, target, verdict, created, body
//                           APPEND-ONLY and global (not per run); the only wall-clock rows (I4)
//   loom_kb_candidates      id PK; kind, payload, support, eval, status, created   (learning track, §6.7)
//   loom_kb_policy_versions (pack, version) PK; hash, status, metrics, created
//   loom_kb_llm_cache       (prompt_hash, model) PK; response, created
//   loom_kb_graph_receipts  id PK; run_id, body — immutable N3 acceptance
//                           receipts, retained across clear_run/rebuild
//
// ── Semantics ───────────────────────────────────────────────────────
// * Writes upsert by (run_id, id) and validate first (Claim::validate, ...).
//   A claim write also enforces the premise rules of I3 against the claims of
//   the same batch and the run: a premise is never extrapolated or absent,
//   and a claim produced through a transfer morphism never has a premise that
//   was itself transferred (transfer depth 1). Violations -> InvalidArgument,
//   nothing of the batch is written.
// * Reads return model objects parsed from `body`; every list is ordered
//   deterministically (by id unless stated), so equal state gives equal
//   output (I5).
// * Judgements are appended once and replayed LAST on every rebuild
//   (replay_judgements), in seq order, so the owner's judgement always wins
//   (I4). Replay never deletes: a rejected claim stays with status
//   "rejected"; an edited claim becomes a new user claim and the old one is
//   rejected with a counter link; merged entities keep their row (status
//   rejected, attrs.merged_into) while their claims are re-keyed onto the
//   surviving entity (the old claims superseded; when the surviving entity
//   already has the same claim, that claim keeps its assessment and gains
//   the support). Replay is incremental: the run remembers the last applied
//   seq (loom_kb_runs.replayed_seq), so replaying again applies only newer
//   judgements; clear_run resets it, so a rebuild (clear, write, replay)
//   always ends in the same state.
//
// Thread safety: every call takes the Database lock; a batch is one
// transaction.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/model.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Database;

namespace kb {

// A knowledge run: one build of the epistemic state from given inputs.
struct KnowledgeRun {
  std::string id;              // kr_ (pack_hash | canonical(inputs))
  std::string archive_run_id;  // archive.run task id ("" none)
  std::string pack_hash;
  std::string status = "running";  // running | done | failed
  Json inputs = Json::object();    // what the run was built from (source hashes, settings)
  Json summary = Json::object();
  std::string created;
  static std::string make_id(std::string_view pack_hash, const Json& inputs);
  Json to_json() const;
};

struct ClaimQuery {
  std::optional<std::string> subject;
  std::optional<std::string> predicate;
  std::optional<std::string> object;
  std::optional<model::EvidenceClass> evidence;
  std::optional<model::Origin> origin;
  std::optional<model::ClaimStatus> status;
  std::optional<std::string> observation;  // claims supported by this observation
  std::optional<std::string> branch;
  int limit = 10000;
};

struct EntityQuery {
  std::optional<std::string> kind;
  std::optional<std::string> canonical_key;
  std::optional<std::string> alias_key;  // any alias with this normalizer key
  std::optional<std::string> parent;
  int limit = 10000;
};

struct SlotQuery {
  std::optional<std::string> instance;
  std::optional<model::Role> role;
  std::optional<std::string> slot;
  std::optional<std::string> claim;
  int limit = 10000;
};

// A slot value joined with its instance id (queries across instances).
struct SlotRow {
  std::string instance;
  model::SlotValue value;
  Json to_json() const;  // {"instance", ...slot value}
};

struct ReplayReport {
  int applied = 0;
  int skipped = 0;
  Json details = Json::array();  // [{"judgement","target","verdict","result":"applied|skipped","reason"}]
  Json to_json() const;
};

class KnowledgeStore {
 public:
  explicit KnowledgeStore(Database& db);

  // Creates / upgrades the loom_kb_* tables (idempotent). Called by every
  // write; call it explicitly before reads on a fresh directory if you want
  // empty results instead of NotFound-free empty lists (reads on a directory
  // without the tables return empty results and never create them).
  Status ensure_schema();
  // True when the tables exist (no side effects).
  bool has_schema();

  // ── runs ───────────────────────────────────────────────────────────
  // Creates the run (or returns the existing one with the same id).
  Result<KnowledgeRun> begin_run(std::string_view pack_hash, const Json& inputs, std::string_view archive_run_id = "");
  Status finish_run(std::string_view run_id, std::string_view status, const Json& summary);
  Result<std::optional<KnowledgeRun>> get_run(std::string_view run_id);
  // Newest first (created, then id); a nonempty status filters before LIMIT.
  Result<std::vector<KnowledgeRun>> list_runs(int limit = 50, std::string_view status = "");
  // Deletes every derived row of the run (not the run row, not judgements).
  Status clear_run(std::string_view run_id);

  // ── writes (upsert; one transaction per call) ─────────────────────
  Status put_observations(std::string_view run, const std::vector<model::Observation>& v);
  Status put_entities(std::string_view run, const std::vector<model::Entity>& v);
  Status put_claims(std::string_view run, const std::vector<model::Claim>& v);
  Status put_principles(std::string_view run, const std::vector<model::Principle>& v);
  Status put_operators(std::string_view run, const std::vector<model::Operator>& v);
  Status put_morphisms(std::string_view run, const std::vector<model::Morphism>& v);
  Status put_instances(std::string_view run, const std::vector<model::Instance>& v);
  Status put_areas(std::string_view run, const std::vector<model::Area>& v);
  Status put_decisions(std::string_view run, const std::vector<model::Decision>& v);
  Status put_forks(std::string_view run, const std::vector<model::Fork>& v);
  Status put_status_records(std::string_view run, const std::vector<model::StatusRecord>& v);
  Status put_predictions(std::string_view run, const std::vector<model::Prediction>& v);
  Status put_models(std::string_view run, const std::vector<model::Model>& v);
  Status put_products(std::string_view run, const std::vector<model::Product>& v);

  // ── point reads (nullopt when absent) ─────────────────────────────
  Result<std::optional<model::Observation>> get_observation(std::string_view run, std::string_view id);
  Result<std::optional<model::Entity>> get_entity(std::string_view run, std::string_view id);
  Result<std::optional<model::Claim>> get_claim(std::string_view run, std::string_view id);
  Result<std::optional<model::Principle>> get_principle(std::string_view run, std::string_view id);
  Result<std::optional<model::Operator>> get_operator(std::string_view run, std::string_view id);
  Result<std::optional<model::Morphism>> get_morphism(std::string_view run, std::string_view id);
  Result<std::optional<model::Instance>> get_instance(std::string_view run, std::string_view id);
  Result<std::optional<model::Area>> get_area(std::string_view run, std::string_view id);
  Result<std::optional<model::Decision>> get_decision(std::string_view run, std::string_view id);
  Result<std::optional<model::Fork>> get_fork(std::string_view run, std::string_view id);
  Result<std::optional<model::Prediction>> get_prediction(std::string_view run, std::string_view id);
  Result<std::optional<model::Model>> get_model(std::string_view run, std::string_view id);
  Result<std::optional<model::Product>> get_product(std::string_view run, std::string_view id);

  // ── indexed queries (deterministic order: by id) ──────────────────
  Result<std::vector<model::Claim>> query_claims(std::string_view run, const ClaimQuery& q);
  Result<std::vector<model::Entity>> query_entities(std::string_view run, const EntityQuery& q);
  // By paradigm and/or subject ("" = any).
  Result<std::vector<model::Instance>> query_instances(std::string_view run, std::string_view paradigm,
                                                       std::string_view subject = "");
  // Ordered by instance, slot, ord.
  Result<std::vector<SlotRow>> query_slots(std::string_view run, const SlotQuery& q);
  Result<std::vector<model::Observation>> observations_of_unit(std::string_view run, std::string_view unit);
  Result<std::vector<model::Principle>> list_principles(std::string_view run);
  Result<std::vector<model::Operator>> list_operators(std::string_view run);
  Result<std::vector<model::Morphism>> list_morphisms(std::string_view run);
  Result<std::vector<model::Area>> list_areas(std::string_view run, std::string_view subject = "");
  Result<std::vector<model::Decision>> list_decisions(std::string_view run, std::string_view subject = "");
  Result<std::vector<model::Fork>> list_forks(std::string_view run, std::string_view subject = "");
  Result<std::vector<model::Prediction>> list_predictions(std::string_view run);
  Result<std::vector<model::Model>> list_models(std::string_view run);
  Result<std::vector<model::Product>> list_products(std::string_view run);
  // The entity's records, ordered and annotated by model::order_status_history.
  Result<std::vector<model::StatusRecord>> status_history(std::string_view run, std::string_view entity);
  // {"<table>": rows, ...} for the run.
  Result<Json> stats(std::string_view run);
  // Reversible model/learning proposals, scoped by payload.run_id. Returns
  // {items,total,limit,offset,has_more}; these are never canonical Claims.
  Result<Json> query_candidates(std::string_view run, std::string_view kind = "",
                                int limit = 100, std::int64_t offset = 0);

  // ── judgements (global, append-only) ──────────────────────────────
  // Validates, fills created (UTC now) and id when empty, assigns seq.
  // Appending the same id twice returns the stored judgement.
  Result<model::Judgement> add_judgement(model::Judgement j);
  // seq > after_seq, in seq order; target "" = all.
  Result<std::vector<model::Judgement>> judgements(std::int64_t after_seq = 0, std::string_view target = "");
  // Applies the judgements not yet applied to the run, in seq order (see the
  // header comment). NotFound when the run does not exist.
  Result<ReplayReport> replay_judgements(std::string_view run);

 private:
  Database& db_;
};

}  // namespace kb
}  // namespace loom
