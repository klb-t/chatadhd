// loom/extract.h — parse units per artifact type into observations and
// observed claims (LOOM_CONCEPTUAL_MODEL §1, §3.7, §6.2). Area:
// extract+resolve. STATUS: contract + stubs ("// STUB: knowledge-wave",
// Errc::NotImplemented).
//
// ── Semantics ───────────────────────────────────────────────────────
// * detect(): which artifact types a unit is (artifact_types/*.json
//   "detect" ops, kb::kDetectOps), best first; ties by id.
// * segment(): the artifact type's segmenters (kb::kSegmenters) cut the unit
//   into located, immutable Observations (I1): each has a Locator into the
//   source (byte range / JSON pointer / time range) and its exact text.
//   Observation ids are content-derived (model::Observation::make_id).
// * extract(): the artifact type's extractors (kb::kExtractors) turn
//   observations into OBSERVED claims only (evidence observed, origin from the
//   source: archive | repo | external_authority for quoted statutes | user
//   for the current conversation), each with Support pointing at its
//   observations. Nothing here infers. Specifically:
//     items / decisions / status_cues   decision, rejected_option, open_question, requirement, invariant,
//                                       ... (archive item cues from lexicons/item_cues.json); Decisions
//                                       with recorded alternatives; StatusRecords per branch/version cue
//     forks                             conversation branch forks (edited messages keep both sides)
//     versions                          anchored version mentions (lexicons/version_patterns.json)
//     normative                         candidate principle statements (lexicons/cues.json "normative")
//     generalizations / areas (R10)     umbrella statements that delimit an Area; the statement becomes a
//                                       candidate Principle scoped to the area; listed items become observed
//                                       member claims; an area without members is flagged as a gap
//     entities_lexicon / relation_patterns   typed entity mentions (gazetteer, profile aliases) and
//                                       claims from lexicons/relation_patterns.json
//     code_symbols / citations / dates / speakers / headers   per medium
// * Brainstorm segmentation (R10): headings and list structure give items;
//   each item is classified against the project kind's domain kinds by their
//   anchors (terms/lexicon/cues) -> (role, kind) with a score; an item that
//   matches no kind keeps role "" and is reported (never forced).
// * Language: every observation carries lang (kb::Normalizer::guess_lang);
//   match keys come from the Normalizer (PL + EN, glossary).
#pragma once

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

namespace extract {

inline constexpr std::string_view kExtractorVersion = "1";

// What a unit is. {"artifact_type","score","reasons":[{"op","value","weight"}]}
struct Detection {
  std::string artifact_type;
  double score = 0.0;
  Json reasons = Json::array();
  Json to_json() const;
};

// The content of one unit as handed to the extractor.
struct UnitContent {
  model::Unit unit;
  std::string text;               // decoded text (UTF-8); "" for purely structured units
  Json structured;                // parsed JSON element (chat export element, e-mail headers), or null
  model::Origin origin = model::Origin::Archive;
};

// A brainstorm item classified against a project kind (R10).
//   {"observation","text","role","kind","score","area"}
struct ClassifiedItem {
  std::string observation;
  std::string text;
  std::optional<model::Role> role;
  std::string kind;               // domain kind id ("" unclassified)
  double score = 0.0;
  std::string area;               // area id when inside an area
  Json to_json() const;
};

// Everything extracted from one unit. All ids content-derived.
//   {"observations":[...],"entities":[...],"claims":[...],"areas":[...],"principles":[...],
//    "decisions":[...],"forks":[...],"statuses":[...],"items":[...],"stats":{...}}
struct Extraction {
  std::vector<model::Observation> observations;
  std::vector<model::Entity> entities;       // mentioned entities (pre-resolution)
  std::vector<model::Claim> claims;          // observed claims only
  std::vector<model::Area> areas;
  std::vector<model::Principle> principles;  // candidates (generalizations, normative statements)
  std::vector<model::Decision> decisions;
  std::vector<model::Fork> forks;
  std::vector<model::StatusRecord> statuses;
  std::vector<ClassifiedItem> items;         // brainstorm items
  Json stats = Json::object();
  Json to_json() const;
};

class Extractor {
 public:
  explicit Extractor(std::shared_ptr<const kb::Pack> pack);

  // Artifact types of the unit, best first (empty = unknown).
  Result<std::vector<Detection>> detect(const UnitContent& content) const;
  // Located observations of the unit per the artifact type's segmenters.
  Result<std::vector<model::Observation>> segment(const model::ArtifactType& type, const UnitContent& content) const;
  // Observed claims etc. per the artifact type's extractors.
  Result<Extraction> extract(const model::ArtifactType& type, const UnitContent& content,
                             const std::vector<model::Observation>& observations) const;
  // detect -> segment -> extract with the best artifact type.
  Result<Extraction> process(const UnitContent& content) const;
  // R10: classify brainstorm items against a project kind (+ its facets).
  Result<std::vector<ClassifiedItem>> classify_items(const model::ProjectKind& kind,
                                                     const std::vector<model::Facet>& facets,
                                                     const std::vector<model::Observation>& items) const;

 private:
  std::shared_ptr<const kb::Pack> pack_;
};

// knowledge.extract stage: every unit selected by the catalog -> Extraction
// -> KnowledgeStore (observations, entities, observed claims, areas,
// candidate principles, decisions, forks, statuses). Params
// (stage_params.extract): {"max_units"?, "types"?:[artifact type ids]}.
// -> {"output","stats":{"units","observations","claims","areas",...}}
Result<Json> run_stage(knowledge::StageContext& ctx);

}  // namespace extract
}  // namespace loom
