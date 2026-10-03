// loom/model.h — the Loom Conceptual Model v1 in C++.
//
// Binding contract: docs/architecture/LOOM_CONCEPTUAL_MODEL.md ("§n" below
// refers to its sections). A term here has exactly the meaning the model gives
// it; code, table names, JSON keys, C ABI names and UI labels use these words
// in this sense. A component that needs a meaning that is not here asks for a
// model change instead of inventing a local one.
//
// ── What lives where (I6) ───────────────────────────────────────────
// CLOSED sets are enum classes in this header (evidence class, origin,
// principle level/form, validation status, universal role, resolution,
// status, check state, ...): adding a value is a model change. OPEN sets are
// strings whose values are pack data (loom/data, kb.h): entity kinds,
// relation types (claim predicates), project kinds, artifact types, facets,
// morphisms, principles, operators, goal types, lexicons, policies.
//
// ── JSON ────────────────────────────────────────────────────────────
// Every struct has `Json to_json() const` and a non-throwing
// `static Result<T> from_json(const Json&)`. to_json emits every key in a fixed
// order (byte-identical output for equal values). from_json requires the
// fields marked "required", accepts missing optional keys (defaults) and
// ignores unknown keys, but rejects wrong types and any name outside a closed
// set with Errc::InvalidArgument ("<path>: unknown <set> '<name>' (one of
// ...)"). from_json(to_json(x)).to_json() == x.to_json() for every value.
//
// ── Content-derived ids (I5) ────────────────────────────────────────
// id = kb::stable_id(prefix, key) = prefix + 16 hex of sha256(key); key parts
// are joined with '\x1f'. Same inputs + same pack hash + same judgements ->
// same ids in every data directory.
//
//   prefix  object          key
//   un_     Unit            source | canonical(locator)
//   ob_     Observation     unit | canonical(locator) | sha256(text)
//   e_      Entity          kind | canonical_key
//   cl_     Claim           subject | predicate | "o:"object or "v:"canonical(value) | canonical(qualifiers)
//                           (a Decision's id is the id of its `decides` claim)
//   p_      Principle       level | form | normalised statement   (pack principles: "p.<name>")
//   op_     Operator        situation | solution                   (pack: "op.<name>", rules "r.<name>",
//                                                                   extrapolations "x.<name>")
//   mo_     Morphism        use | from | to                        (pack: "m.<name>")
//   in_     Instance        paradigm | subject
//   ar_     Area            subject | observation | statement
//   fk_     Fork            kind | subject | base | sorted side refs
//   sr_     StatusRecord    entity | branch | version | status | date
//   pn_     Prediction      operator | situation | cut
//   md_     Model           name
//   g_      Goal            type | text | sorted targets
//   cx_     ContextSet      goal id | budget | pack hash
//   pd_     Product         kind | instance | run
//   ju_     Judgement       created | target | verdict | canonical(payload)
//   kr_     knowledge run   pack hash | canonical(inputs)
//   ca_     candidate       kind | canonical(payload)
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/kb.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::model {

// ═════════════════════════════════════════════════════════════════════
// Closed sets
// ═════════════════════════════════════════════════════════════════════

// §2.3 Evidence class — HOW a value was obtained (kb::Evidence):
// observed | derived | inferred | extrapolated | absent | user.
using EvidenceClass = kb::Evidence;
// §2.4 re-check state of an Expected Property: pending | holds | violated | n/a.
using CheckState = kb::CheckState;

// §2.3 Origin — FROM WHOM/WHAT, orthogonal to the evidence class.
enum class Origin {
  Archive,            // "archive": the owner's own sources
  Repo,               // "repo": code and git history
  User,               // "user": direct statement in the current interaction
  ExternalAuthority,  // "external_authority": statute, standard, official document
  ModelKnowledge,     // "model_knowledge": general knowledge of a language model (R7)
  System,             // "system": Loom's own computation
};

// §3.1 Principle level.
enum class PrincipleLevel { Value, Epistemic, Strategy };
// §3.1 Principle form: invariant | heuristic | default | meta | conflict_resolution.
enum class PrincipleForm { Invariant, Heuristic, Default, Meta, ConflictResolution };
// §3.1 Validation status of principles, operators, morphisms, paradigms and
// models: candidate | supported | confirmed | rejected | retired.
enum class ValidationStatus { Candidate, Supported, Confirmed, Rejected, Retired };

// §3.4 The fourteen universal roles (the system meta-model).
enum class Role {
  Intent, Constraint, Part, Actor, Resource, Interface, Flow,
  Event, Artifact, Transformation, Check, Output, Decision, Question,
};

// §4 Resolution at which an item enters a context: label | summary | full | raw.
enum class Resolution { Label, Summary, Full, Raw };
// §4 ContextSet bands, in prompt order: stable prefix -> project context -> goal tail.
enum class ContextBand { Stable, Project, Goal };

// §5 Status of a part/component/feature per branch and version.
// "lost" = existed, then disappeared; "restored" = came back after lost.
enum class StatusValue { Implemented, Partial, Planned, Abandoned, Superseded, Lost, Restored };
// §5 Decision status.
enum class DecisionStatus { Active, Superseded, Reverted };
// §5 Fork kinds: an edited conversation message, design alternatives, or two
// code versions derived from the same base.
enum class ForkKind { Conversation, Design, CodeLineage };

// §2.2 q5 status of a claim (also of an entity's existence): active |
// contested (supported incompatible values, unresolved; or an Expected
// Property violated) | superseded (a later claim replaced it; kept) |
// rejected (owner judgement or refuted; kept for audit).
enum class ClaimStatus { Active, Contested, Superseded, Rejected };

// §3.5 The paradigm family: project kind, artifact type, or a facet of a
// project kind.
enum class ParadigmKind { ProjectKind, ArtifactType, Facet };
// §3.6 Morphism use: anchoring (domain -> universal meta-model) or transfer
// (domain -> domain).
enum class MorphismUse { Anchoring, Transfer };
// §3.6 What a transfer morphism may infer on the target: that the target
// should HAVE an entity of the mapped kind (presence), or the source's value
// as a prior for the mapped slot (value). Depth 1, never chained (I3).
enum class TransferMode { Presence, Value };
// §3.2 Effect of a decision alternative on a value: "+", "-", "0".
enum class ValueEffect { Positive, Negative, Neutral };

// §1 Kinds of observation (atomic located content of a unit). "field" is a
// key/value pair of structured metadata (e-mail header, export field).
enum class ObservationKind { Sentence, ListItem, Heading, CodeBlock, Utterance, TableRow, Field };
// Medium of an artifact type (decides which segmenters apply; the catalog
// never inflates audio/video/image members).
enum class Medium { Text, Code, Audio, Video, Image, Structured };

// Kinds of model object a reference, a judgement or a context item points to.
enum class RefKind {
  Unit, Observation, Entity, Claim, Principle, Operator, Morphism, Instance,
  Area, Decision, Fork, StatusRecord, Prediction, Model, Product,
};
// §6.7 Owner judgements (events, replayed last on every rebuild, I4).
enum class Verdict { Confirm, Reject, Edit, Merge, Split };
// Cardinality of a slot / domain kind: one | many | list (ordered many).
enum class Cardinality { One, Many, List };

// ── Closed-set machinery ────────────────────────────────────────────
// ClosedSet<E>::kSet names the set for messages; kNames lists the JSON
// spellings in enumerator order (enumerators are 0..N-1).
template <class E>
struct ClosedSet;

#define LOOM_MODEL_CLOSED_SET(E, set_name, ...)                        \
  template <>                                                         \
  struct ClosedSet<E> {                                               \
    static constexpr std::string_view kSet = set_name;                \
    static constexpr std::array kNames = {__VA_ARGS__};               \
  }

using namespace std::string_view_literals;
LOOM_MODEL_CLOSED_SET(kb::Evidence, "evidence class", "observed"sv, "derived"sv, "inferred"sv, "extrapolated"sv,
                      "absent"sv, "user"sv);
LOOM_MODEL_CLOSED_SET(kb::CheckState, "check state", "pending"sv, "holds"sv, "violated"sv, "n/a"sv);
LOOM_MODEL_CLOSED_SET(Origin, "origin", "archive"sv, "repo"sv, "user"sv, "external_authority"sv, "model_knowledge"sv,
                      "system"sv);
LOOM_MODEL_CLOSED_SET(PrincipleLevel, "principle level", "value"sv, "epistemic"sv, "strategy"sv);
LOOM_MODEL_CLOSED_SET(PrincipleForm, "principle form", "invariant"sv, "heuristic"sv, "default"sv, "meta"sv,
                      "conflict_resolution"sv);
LOOM_MODEL_CLOSED_SET(ValidationStatus, "validation status", "candidate"sv, "supported"sv, "confirmed"sv,
                      "rejected"sv, "retired"sv);
LOOM_MODEL_CLOSED_SET(Role, "universal role", "intent"sv, "constraint"sv, "part"sv, "actor"sv, "resource"sv,
                      "interface"sv, "flow"sv, "event"sv, "artifact"sv, "transformation"sv, "check"sv, "output"sv,
                      "decision"sv, "question"sv);
LOOM_MODEL_CLOSED_SET(Resolution, "resolution", "label"sv, "summary"sv, "full"sv, "raw"sv);
LOOM_MODEL_CLOSED_SET(ContextBand, "context band", "stable"sv, "project"sv, "goal"sv);
LOOM_MODEL_CLOSED_SET(StatusValue, "status", "implemented"sv, "partial"sv, "planned"sv, "abandoned"sv, "superseded"sv,
                      "lost"sv, "restored"sv);
LOOM_MODEL_CLOSED_SET(DecisionStatus, "decision status", "active"sv, "superseded"sv, "reverted"sv);
LOOM_MODEL_CLOSED_SET(ForkKind, "fork kind", "conversation"sv, "design"sv, "code_lineage"sv);
LOOM_MODEL_CLOSED_SET(ClaimStatus, "claim status", "active"sv, "contested"sv, "superseded"sv, "rejected"sv);
LOOM_MODEL_CLOSED_SET(ParadigmKind, "paradigm kind", "project_kind"sv, "artifact_type"sv, "facet"sv);
LOOM_MODEL_CLOSED_SET(MorphismUse, "morphism use", "anchoring"sv, "transfer"sv);
LOOM_MODEL_CLOSED_SET(TransferMode, "transfer mode", "presence"sv, "value"sv);
LOOM_MODEL_CLOSED_SET(ValueEffect, "value effect", "+"sv, "-"sv, "0"sv);
LOOM_MODEL_CLOSED_SET(ObservationKind, "observation kind", "sentence"sv, "list_item"sv, "heading"sv, "code_block"sv,
                      "utterance"sv, "table_row"sv, "field"sv);
LOOM_MODEL_CLOSED_SET(Medium, "medium", "text"sv, "code"sv, "audio"sv, "video"sv, "image"sv, "structured"sv);
LOOM_MODEL_CLOSED_SET(RefKind, "reference kind", "unit"sv, "observation"sv, "entity"sv, "claim"sv, "principle"sv,
                      "operator"sv, "morphism"sv, "instance"sv, "area"sv, "decision"sv, "fork"sv, "status_record"sv,
                      "prediction"sv, "model"sv, "product"sv);
LOOM_MODEL_CLOSED_SET(Verdict, "verdict", "confirm"sv, "reject"sv, "edit"sv, "merge"sv, "split"sv);
LOOM_MODEL_CLOSED_SET(Cardinality, "cardinality", "one"sv, "many"sv, "list"sv);
#undef LOOM_MODEL_CLOSED_SET

template <class E>
concept ClosedSetEnum = requires {
  ClosedSet<E>::kSet;
  ClosedSet<E>::kNames;
};

template <ClosedSetEnum E>
constexpr std::size_t count() noexcept {
  return ClosedSet<E>::kNames.size();
}
// JSON spelling ("" for an out-of-range value).
template <ClosedSetEnum E>
constexpr std::string_view to_string(E e) noexcept {
  auto i = static_cast<std::size_t>(e);
  return i < count<E>() ? ClosedSet<E>::kNames[i] : std::string_view{};
}
template <ClosedSetEnum E>
constexpr std::optional<E> from_string(std::string_view s) noexcept {
  for (std::size_t i = 0; i < count<E>(); ++i) {
    if (ClosedSet<E>::kNames[i] == s) return static_cast<E>(i);
  }
  return std::nullopt;
}
// Every value of the set, in enumerator order.
template <ClosedSetEnum E>
constexpr std::array<E, ClosedSet<E>::kNames.size()> all() noexcept {
  std::array<E, ClosedSet<E>::kNames.size()> out{};
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<E>(i);
  return out;
}
// "a, b, c" (for messages and docs).
template <ClosedSetEnum E>
std::string names_list() {
  std::string out;
  for (auto n : ClosedSet<E>::kNames) {
    if (!out.empty()) out += ", ";
    out += n;
  }
  return out;
}
// Parse with a descriptive error ("<field>: unknown origin 'x' (one of ...)").
template <ClosedSetEnum E>
Result<E> parse(std::string_view s, std::string_view field = {}) {
  if (auto v = from_string<E>(s)) return *v;
  std::string where = field.empty() ? std::string() : std::string(field) + ": ";
  return Error(Errc::InvalidArgument, where + "unknown " + std::string(ClosedSet<E>::kSet) + " '" + std::string(s) +
                                          "' (one of: " + names_list<E>() + ")");
}

// ── Semantics of the closed sets that code must honour ──────────────
// §2.3 "may be a premise of inference?": observed/derived/user yes, inferred
// yes but depth-limited, extrapolated never, absent no.
bool may_be_premise(EvidenceClass e) noexcept;
// §2.3 authority order when claims compete (higher wins): user(5) >
// archive = repo(4) > external_authority(3) > system(2) > model_knowledge(1).
// Ties inside a rank are broken by recency and explicitness (engine policy).
// I3: model_knowledge never outranks the owner's sources.
int authority_rank(Origin o) noexcept;
// Evidence classes a rule/operator may produce: derived | inferred | extrapolated.
bool is_producible(EvidenceClass e) noexcept;

// ═════════════════════════════════════════════════════════════════════
// Shared small types
// ═════════════════════════════════════════════════════════════════════

// {"en": "...", "pl": "..."} — bilingual text; keys are language codes.
using Text = std::map<std::string, std::string>;
Json text_to_json(const Text& t);
Result<Text> text_from_json(const Json& j, std::string_view field);

// A reference into the owner's documents or into the model: a seed's
// provenance ({"doc":"MEGA_MASTER_2026-09-16.md","section":"§2.B"}), an
// operator example (claim/observation ids), a note.
struct Reference {
  std::string doc;          // document path or id (a source the owner wrote)
  std::string section;      // "§2.B", "R10", "Sesja 2"
  std::string date;         // "YYYY-MM-DD": when the cited text existed (temporal holdout); "" unknown
  std::string claim;        // claim id ("" none)
  std::string observation;  // observation id ("" none)
  std::string note;
  bool empty() const noexcept;
  Json to_json() const;
  static Result<Reference> from_json(const Json& j);
};

// ═════════════════════════════════════════════════════════════════════
// Layer A — sources, units, observations (§1)
// ═════════════════════════════════════════════════════════════════════

// Where a unit or an observation lives inside its source. Sources are
// content-addressed and immutable (I1): `source` is a loom_sources id or
// "sha256:<hex>" of the raw bytes, so every located span is verifiable.
struct Locator {
  std::string source;                        // loom_sources id or "sha256:<hex>"
  std::string member;                        // zip member / path inside the source ("" = whole source)
  std::string json_pointer;                  // RFC 6901 pointer into a JSON member ("" none)
  std::optional<std::int64_t> byte_start;    // byte range inside the member
  std::optional<std::int64_t> byte_len;
  std::optional<double> time_start;          // seconds (recordings)
  std::optional<double> time_end;
  std::optional<int> line;                   // 1-based line, for display only
  bool empty() const noexcept;
  Json to_json() const;
  static Result<Locator> from_json(const Json& j);
};

// §1 Unit: an addressable part of a source (conversation, message, commit,
// file, e-mail, recording segment, document page), catalogued before anything
// is imported (R1). `kind` is open (catalog data).
struct Unit {
  std::string id;             // un_
  std::string source;         // source id / "sha256:<hex>"
  std::string kind;           // "conversation", "message", "commit", "file", "email", "segment", "page", ...
  Locator locator;
  std::string title;
  std::string date;           // ISO date/time of the unit ("" unknown)
  std::string artifact_type;  // pack artifact type id once recognised ("" unknown)
  std::string lang;           // "pl" | "en" | "mixed" | ""
  std::int64_t bytes = 0;
  Json attrs = Json::object();
  static std::string make_id(std::string_view source, const Locator& loc);
  Json to_json() const;
  static Result<Unit> from_json(const Json& j);
};

// §1 Observation: an atomic, located, immutable piece of a unit. `text` is the
// quote. Every claim that says "observed" points to observations.
struct Observation {
  std::string id;             // ob_
  std::string unit;           // unit id
  ObservationKind kind = ObservationKind::Sentence;
  std::string text;
  Locator locator;
  std::string lang;           // "pl" | "en" | "mixed" | ""
  std::string date;           // date of the utterance/message/unit ("" unknown)
  int ordinal = 0;            // position inside the unit
  std::string artifact_type;  // artifact type that parsed it
  std::string speaker;        // message role / speaker label ("" none)
  Json attrs = Json::object();  // heading path, list depth, code language, ...
  static std::string make_id(std::string_view unit, const Locator& loc, std::string_view text);
  Json to_json() const;
  static Result<Observation> from_json(const Json& j);
};

// ═════════════════════════════════════════════════════════════════════
// Layer B — epistemic state (§2)
// ═════════════════════════════════════════════════════════════════════

// One alias of an entity. `method`: how it was obtained — "lexicon" (pack),
// "mined" (resolution), "user" (judgement), "merge" (from a merged entity).
struct Alias {
  std::string key;      // normalizer phrase key
  std::string surface;  // as written
  std::string lang;
  std::string method = "mined";
  int count = 0;
  double confidence = 1.0;
  Json to_json() const;
  static Result<Alias> from_json(const Json& j);
};

// §2.1 Entity: a thing the model talks about; identity maintained by entity
// resolution. Lineage (evolved_into, merged_into, forked_from) is a CLAIM
// between entities, never an alias merge. The evidence/origin/confidence/
// status fields assess the entity's existence and typing.
struct Entity {
  std::string id;             // e_ (kind | canonical_key)
  std::string kind;           // open set (schema/types.json entity_kinds)
  std::string canonical_key;  // normalizer phrase key
  std::string label;
  Text labels;                // {"en","pl"} display labels
  std::vector<Alias> aliases;
  std::string parent;         // primary parent entity ("" none)
  std::string first_seen;     // ISO dates of the first/last supporting observation
  std::string last_seen;
  EvidenceClass evidence = EvidenceClass::Observed;
  Origin origin = Origin::Archive;
  double confidence = 1.0;
  ClaimStatus status = ClaimStatus::Active;
  Json attrs = Json::object();
  static std::string make_id(std::string_view kind, std::string_view canonical_key);
  Json to_json() const;
  static Result<Entity> from_json(const Json& j);
};

// §2.2 q2 — one observation supporting a claim (quote with locator).
struct Support {
  std::string observation;  // observation id (required)
  Locator locator;          // copied, so provenance stands alone
  std::string quote;        // the supporting span
  std::string extractor;    // "<extractor>@<version>", e.g. "extract.items@1"
  double quality = 1.0;     // match quality q in [0, 1]
  Json to_json() const;
  static Result<Support> from_json(const Json& j);
};

// §2.2 q2 / §2.4 — how a derived/inferred/extrapolated claim was produced.
struct Derivation {
  std::string op;           // operator/rule id (required): "r.current_version", "op.…", "m.…"
  int op_version = 1;
  std::string morphism;     // transfer morphism id ("" none); transferred claims never chain (I3)
  int depth = 0;            // inference depth: 0 = premises are observed/derived/user only
  Json to_json() const;     // {"operator","operator_version","morphism","depth"}
  static Result<Derivation> from_json(const Json& j);
};

// §2.4 a competing candidate value with its score.
struct Alternative {
  std::string object;       // entity id ("" when a literal value)
  Json value;               // literal (null when object)
  double score = 0.0;
  Json to_json() const;
  static Result<Alternative> from_json(const Json& j);
};

// §2.2 q4 — what a claim depends on.
struct Premises {
  std::vector<std::string> claims;       // claim ids
  std::vector<std::string> principles;   // principle ids (the "supporting principles")
  std::vector<std::string> assumptions;  // stated assumptions (text)
  bool empty() const noexcept;
  Json to_json() const;
  static Result<Premises> from_json(const Json& j);
};
// §2.2 q5 — what contradicts it.
struct Counter {
  std::vector<std::string> observations;
  std::vector<std::string> claims;
  bool empty() const noexcept;
  Json to_json() const;
  static Result<Counter> from_json(const Json& j);
};
// §2.2 q6 — what follows from it.
struct Consequences {
  std::vector<std::string> claims;
  std::vector<std::string> predictions;
  std::vector<std::string> checks;       // check ids (rules/checks.json) or principle checks it enables
  bool empty() const noexcept;
  Json to_json() const;
  static Result<Consequences> from_json(const Json& j);
};
// §2.2 q7 — what is missing: unfilled slots ("<instance>/<slot>") and
// questions it raises (text), plus a query that would fill it (absent claims).
struct Open {
  std::vector<std::string> slots;
  std::vector<std::string> questions;
  Json fill_query;  // null, or e.g. {"terms":[...],"kinds":[...]} for a targeted catalog search
  bool empty() const noexcept;
  Json to_json() const;
  static Result<Open> from_json(const Json& j);
};

// §2.2 The Assessment: the seven questions, identical in every domain.
//   JSON: {"basis":{"support":[...],"derivation":{...}|null},
//          "evidence_class","origin","confidence","premises","counter","status",
//          "consequences","open","expected_property":{...}|null,"check_state",
//          "alternatives":[...]}
struct Assessment {
  // q2 how is it known
  std::vector<Support> support;
  std::optional<Derivation> derivation;
  // q3 how certain
  EvidenceClass evidence = EvidenceClass::Observed;
  Origin origin = Origin::Archive;
  double confidence = 0.0;  // calibrated, [0, 1]
  // q4 what does it depend on
  Premises premises;
  // q5 what contradicts it
  Counter counter;
  ClaimStatus status = ClaimStatus::Active;
  // q6 what follows from it
  Consequences consequences;
  // q7 what is missing
  Open open;
  // §2.4 inference: what the system vouches for, its re-check state, and the
  // other candidate values.
  std::optional<kb::ExpectedProperty> expected;
  CheckState check = CheckState::NotApplicable;
  std::vector<Alternative> alternatives;

  // I2/I3 invariants that one assessment can check on its own:
  //   confidence is finite and in [0, 1];
  //   observed -> at least one support; absent -> no support;
  //   derived/inferred/extrapolated -> a derivation naming its operator;
  //   inferred -> an expected property and a check state other than n/a;
  //   a check state other than n/a -> an expected property.
  // (Premise rules that need other claims — extrapolated never a premise,
  // transfer never chained — are checked by the KnowledgeStore on write.)
  Status validate() const;
  Json to_json() const;
  static Result<Assessment> from_json(const Json& j);
};

// §2.1 qualifiers of a claim: time, version, branch, scope, language, other.
struct Qualifiers {
  std::string valid_from;  // ISO date ("" open)
  std::string valid_to;
  std::string version;     // normalised version ("0.7.9")
  std::string branch;      // "" = main line (status and claims are per branch and version, §5)
  std::string scope;       // area / instance / context id
  std::string lang;        // language of the statement ("pl", "en")
  Json extra = Json::object();
  bool empty() const noexcept;
  Json to_json() const;
  static Result<Qualifiers> from_json(const Json& j);
};

// §2.1 Claim: (subject, predicate, object entity OR literal value, qualifiers)
// + its Assessment. There are no bare facts (I2). An `absent` claim has
// neither object nor value: it is the first-class "nothing known yet" of a
// slot or question, with Open saying what would fill it.
struct Claim {
  std::string id;          // cl_ (content_key)
  std::string subject;     // entity id (required)
  std::string predicate;   // relation type (open set; required)
  std::string object;      // entity id ("" when a literal value)
  Json value;              // literal value (null when object / absent)
  Qualifiers qualifiers;
  Assessment assessment;

  std::string content_key() const;
  static std::string make_id(std::string_view subject, std::string_view predicate, std::string_view object,
                             const Json& value, const Qualifiers& q);
  bool is_absent() const noexcept { return assessment.evidence == EvidenceClass::Absent; }
  // subject + predicate present; object xor value unless absent (then
  // neither); assessment.validate().
  Status validate() const;
  Json to_json() const;
  static Result<Claim> from_json(const Json& j);
};

// §2.5 Model: a named, consistent set of claims and principles explaining a
// body of observations. Competing models coexist and are scored, never merged.
struct Model {
  std::string id;          // md_
  std::string name;
  std::string description;
  std::vector<std::string> claims;
  std::vector<std::string> principles;
  std::vector<std::string> explains;   // observation ids it explains
  double explanatory = 0.0;            // §7 scores
  double predictive = 0.0;
  ValidationStatus validation = ValidationStatus::Candidate;
  static std::string make_id(std::string_view name);
  Json to_json() const;
  static Result<Model> from_json(const Json& j);
};

// ═════════════════════════════════════════════════════════════════════
// Layer C — generalizations (§3)
// ═════════════════════════════════════════════════════════════════════

// Where a principle applies; every list empty = everywhere.
struct Scope {
  std::vector<std::string> project_kinds;
  std::vector<std::string> facets;
  std::vector<std::string> artifact_types;
  std::vector<std::string> areas;       // area ids (§3.7)
  std::vector<std::string> products;    // product kinds (preferences, §3.1)
  std::vector<std::string> conditions;  // text
  bool empty() const noexcept;
  Json to_json() const;
  static Result<Scope> from_json(const Json& j);
};

// §3.1 predicts: situation type -> expected solution class.
struct Prediction;
struct SituationSolution {
  std::string situation;
  std::string solution;
  Json to_json() const;
  static Result<SituationSolution> from_json(const Json& j);
};

// §3.1 Principle: a general statement that constrains or generates decisions;
// hypothesis-like. A Preference is a principle with owner "user" whose scope
// names the owner's products (is_preference()). Seeds in the pack are PRIORS
// (validation candidate); the engine must still discover principles in the
// sources. Beyond §3.1: `origin` (who proposed it), `sources` (seed
// provenance), `checks` (check ids that detect violations, I8) and
// `value_constraints` (what consistent_with(principle) tests; opaque here,
// interpreted by the generalize area).
struct Principle {
  std::string id;                        // pack "p.<name>" or discovered p_
  Text statement;                        // {"pl","en"} (required, non-empty)
  std::vector<std::string> phrasings;    // verbatim
  PrincipleLevel level = PrincipleLevel::Strategy;   // required
  PrincipleForm form = PrincipleForm::Heuristic;     // required
  Scope scope;
  std::vector<std::string> protects;         // value principle ids (level value)
  std::vector<std::string> derived_from;     // more general principle ids
  std::vector<std::string> evidence_for;     // observation / decision (claim) ids
  std::vector<std::string> counterexamples;  // observation / claim ids
  std::vector<std::string> exceptions;       // text
  double confidence = 0.5;                   // for seeds: the prior
  std::vector<SituationSolution> predicts;
  std::vector<std::string> conflicts_with;
  std::vector<std::string> supersedes;
  ValidationStatus validation = ValidationStatus::Candidate;
  std::string owner;                         // "user" for the owner's principles, "" otherwise
  Origin origin = Origin::Archive;
  std::vector<Reference> sources;
  std::vector<std::string> checks;
  Json value_constraints = Json::object();
  bool is_preference() const noexcept { return owner == "user" && !scope.products.empty(); }
  Json to_json() const;
  static Result<Principle> from_json(const Json& j);
};

// §3.3 Operator: situation type -> solution class, with justification. Pack
// inference rules are operators that PRODUCE claims: they carry `produces`
// (derived | inferred | extrapolated), a machine-checkable `when` (condition
// expression over kb::kConditionOps), a `value` (kb::kValueOps), a `target`
// and — when inferred — an Expected Property. Design operators (the
// generator of the owner's decisions) carry situation/solution text and are
// mined, evaluated and used for predictions.
struct Operator {
  std::string id;                  // pack "op.*" / "r.*" / "x.*", mined op_
  int version = 1;
  Text situation;                  // human description of the situation type
  Text solution;                   // human description of the solution class (change template)
  Json when;                       // condition expression or null
  Json value;                      // value expression or null
  Json target;                     // rule target or null: {"paradigm","slot"[,"field"]} | {"temporal"} | {"entity_kind","attr"}
  std::optional<EvidenceClass> produces;  // rules only
  int stratum = -1;                // rules: 0 derived, 1 inferred, 2 extrapolated; -1 for design operators
  std::optional<kb::ExpectedProperty> expected;
  std::vector<std::string> principles;  // justifying principle ids
  std::vector<Reference> examples;      // historical decisions that applied it (with locators)
  int success = 0;
  int failure = 0;
  double confidence = 0.5;
  ValidationStatus validation = ValidationStatus::Candidate;
  Origin origin = Origin::Archive;
  std::vector<Reference> sources;
  Json basis = Json::object();          // rules: slots/lexicons/policy the rule reads (documentation)
  bool is_rule() const noexcept { return produces.has_value(); }
  // Rules: when/value/target present, stratum matches produces, inferred ->
  // expected property; design operators: situation and solution text.
  Status validate() const;
  Json to_json() const;
  static Result<Operator> from_json(const Json& j);
};

// §3.6 One end of a morphism: a domain kind (optionally one of its slots or
// relations) of a paradigm, or a universal role (anchoring target).
struct MorphismEnd {
  std::string paradigm;   // project kind / facet / artifact type id ("" for a role end)
  std::string kind;       // domain kind id
  std::string slot;       // attribute slot of the kind ("" none)
  std::string relation;   // relation the end is taken over ("" none)
  std::string target;     // domain kind at the other end of `relation` ("" none)
  std::optional<Role> role;  // anchoring target
  Json to_json() const;
  static Result<MorphismEnd> from_json(const Json& j);
};

// §3.6 Morphism: a structure-preserving map stored as data. Anchoring maps a
// domain kind onto a universal role; transfer maps domain kinds of two
// paradigms that anchor on the same role. Transfer depth is 1.
struct Morphism {
  std::string id;                  // pack "m.<name>" / mined mo_
  MorphismUse use = MorphismUse::Transfer;
  MorphismEnd from;
  MorphismEnd to;
  bool bidirectional = true;
  std::vector<Json> conditions;    // condition expressions (kb::kConditionOps)
  TransferMode mode = TransferMode::Presence;
  double confidence = 0.5;         // prior of a transferred inference (capped by policy)
  std::optional<kb::ExpectedProperty> expected;  // what a transferred inference vouches for
  std::string rationale;
  Origin origin = Origin::ModelKnowledge;
  ValidationStatus validation = ValidationStatus::Candidate;
  std::vector<Reference> sources;
  Json to_json() const;
  static Result<Morphism> from_json(const Json& j);
};

// ── Paradigm views over pack data (§3.5) ────────────────────────────
// Slot / attribute type: "string" | "text" | "version" | "date" |
// "principle[]" | "record[]" | "entity:<entity kind>" | "enum:<types enum>".
struct SlotSpec {
  std::string name;
  std::string type;
  Cardinality card = Cardinality::One;
  bool required = false;
  double weight = 1.0;
  std::string relation;            // predicate that carries it ("" = derived/computed)
  Json fields = Json::object();    // record[] field specs {name: {"type","card"}}
  Json bind = Json::array();       // bindings (kb::kBindingKinds)
  std::vector<std::string> rules;  // operator ids that may fill it
  std::string description;
  Json to_json() const;
  static Result<SlotSpec> from_json(const Json& j);
};

// A typed link from one domain kind to another (same paradigm or, for a
// facet, its host project kind).
struct DomainRelation {
  std::string rel;     // relation type (types.json)
  std::string target;  // domain kind id
  Cardinality card = Cardinality::Many;
  Json to_json() const;
  static Result<DomainRelation> from_json(const Json& j);
};

// §3.5 Domain kind: a data-defined kind of a project kind (module, scene,
// track, deadline), mapped to EXACTLY ONE universal role (its anchoring).
// Instances of the project kind have one slot per domain kind.
struct DomainKind {
  std::string id;
  Role role = Role::Part;          // required
  Text labels;
  std::string description;
  std::string entity_kind;         // entity kind of its values ("" = literal values)
  std::string value_type;          // slot type of the instance slot
  Cardinality card = Cardinality::Many;
  bool required = false;
  double weight = 1.0;
  std::string relation;            // predicate: project subject -> value
  std::vector<DomainRelation> relations;
  std::vector<SlotSpec> slots;     // attributes of entities of this kind
  Json anchors = Json::object();   // {"terms":{"en":[],"pl":[]},"lexicon":[classes],"cues":[classes]}
  Json bind = Json::array();       // how values are found (kb::kBindingKinds)
  std::vector<std::string> rules;  // operator ids targeting this slot
  Json to_json() const;
  static Result<DomainKind> from_json(const Json& j);
};

// Common header of every paradigm file.
struct ParadigmHeader {
  std::string id;
  int version = 1;
  Text title;
  std::string description;
  Origin origin = Origin::Archive;
  ValidationStatus validation = ValidationStatus::Candidate;
  std::vector<std::string> derived_from;  // morphism ids this paradigm was derived by (music <- film)
  std::vector<Reference> sources;
  Json to_json() const;
  static Result<ParadigmHeader> from_json(const Json& j);
};

// §3.5 Project kind (project_kinds/<id>.json).
struct ProjectKind {
  ParadigmHeader header;
  std::string subject_kind = "project";   // entity kind of the root
  Json anchors = Json::object();          // {"any"|"all":[anchor ops], "min_score"}
  std::vector<std::string> facets;        // facet ids that may apply
  std::vector<DomainKind> domain_kinds;
  Json constraints = Json::array();       // [{"id","expr"(predicate),"on_violation","why"}]
  std::vector<std::string> rules;         // operators applying to the whole instance
  const DomainKind* domain_kind(std::string_view id) const noexcept;
  Json to_json() const;
  static Result<ProjectKind> from_json(const Json& j);
};

// §3.5 Facet (facets/<id>.json): an optional, reusable sub-template.
struct Facet {
  ParadigmHeader header;
  std::vector<std::string> applies_to;    // project kind ids
  Json anchors = Json::object();
  std::vector<DomainKind> domain_kinds;
  Json constraints = Json::array();
  std::vector<std::string> rules;
  const DomainKind* domain_kind(std::string_view id) const noexcept;
  Json to_json() const;
  static Result<Facet> from_json(const Json& j);
};

// §3.5 Artifact type (artifact_types/<id>.json): how a unit is recognised,
// parsed into observations (segmenters, kb::kSegmenters) and claims
// (extractors, kb::kExtractors), and the IR structure of its instances.
struct ArtifactType {
  ParadigmHeader header;
  Medium medium = Medium::Text;
  Json detect = Json::object();           // {"any":[detect ops], "min_score"}
  Json parse = Json::object();            // {"segment":[segmenters], "observation_kinds":[...], ...}
  Json extract = Json::array();           // [{"op": extractor, ...params}]
  std::vector<SlotSpec> structure;        // IR schema of an instance
  Json to_json() const;
  static Result<ArtifactType> from_json(const Json& j);
};

// Anchoring meta-model (morphisms/anchoring.json): the relations between
// universal roles (the codomain of every anchoring morphism) and the map from
// domain relation types onto them. A domain relation `rel` from kind A to
// kind B anchors iff relation_map[rel] = R and role(A) in R.from and
// role(B) in R.to.
struct RoleRelation {
  std::string id;
  std::vector<Role> from;
  std::vector<Role> to;
  Text labels;
  Json to_json() const;
  static Result<RoleRelation> from_json(const Json& j);
};
struct AnchoringModel {
  std::vector<RoleRelation> role_relations;
  std::map<std::string, std::string> relation_map;  // domain relation -> role relation id
  const RoleRelation* role_relation(std::string_view id) const noexcept;
  // "" when rel (A -> B) anchors; otherwise the reason.
  std::string check(std::string_view rel, Role from, Role to) const;
  Json to_json() const;
  static Result<AnchoringModel> from_json(const Json& j);
};

// §3.7 Area: a region of the model (roles/kinds under a subject) delimited by
// a generalization inside an artifact. The generating statement becomes a
// principle (scope = the area, validation candidate); listed items are
// observed claims; the generalization infers unlisted members (inferred, with
// the generalization as premise) and an empty area is flagged as a gap.
struct Area {
  std::string id;                          // ar_
  std::string subject;                     // entity the area belongs to (project)
  std::string statement;                   // generating statement (text)
  std::string observation;                 // observation id of the generalization
  std::vector<Role> roles;
  std::vector<std::string> kinds;          // domain kind ids
  std::string principle;                   // principle created from the statement
  std::vector<std::string> members;        // claim ids of listed items (observed)
  std::vector<std::string> inferred_members;  // claim ids inferred from the generalization
  bool gap = false;                        // no member: flagged
  static std::string make_id(std::string_view subject, std::string_view observation, std::string_view statement);
  Json to_json() const;
  static Result<Area> from_json(const Json& j);
};

// §3.5 Instance: a paradigm applied to a subject. Slots are filled by claims
// (each with its own assessment); absent slots are claims with evidence
// `absent`. Temporal slots (versions, current_version, forks, lineage,
// status: schema/types.json "temporal_slots") have no role.
struct SlotValue {
  std::string slot;                 // domain kind id / facet domain kind id / IR field / temporal slot
  int ord = 0;
  std::string claim;                // claim id (required)
  std::optional<Role> role;         // role of the slot's domain kind (nullopt: temporal / IR field)
  bool conflict = false;            // §2.3 two supported, incompatible values in this slot
  Json to_json() const;
  static Result<SlotValue> from_json(const Json& j);
};

struct Instance {
  std::string id;                   // in_
  ParadigmKind paradigm_kind = ParadigmKind::ProjectKind;
  std::string paradigm;             // project kind / artifact type id
  int paradigm_version = 1;
  std::string subject;              // entity id (project) or unit/artifact entity id
  std::string subject_label;
  std::vector<std::string> facets;  // applied facet ids
  double score = 0.0;               // match score
  Json coverage = Json::object();   // {"<evidence class>": n, "required_filled": n, "required": n}
  std::string model;                // competing model id ("" = default model)
  std::vector<SlotValue> slots;
  static std::string make_id(std::string_view paradigm, std::string_view subject);
  Json to_json() const;
  static Result<Instance> from_json(const Json& j);
};

// ═════════════════════════════════════════════════════════════════════
// Layer D — intent and context (§4)
// ═════════════════════════════════════════════════════════════════════

// A goal type (goals/goal_types.json): which roles, principle levels and
// evidence classes are relevant, default resolutions per role ("*" = any
// other), and the budget split over the three bands (sums to 1).
struct GoalType {
  std::string id;
  Text labels;
  std::string description;
  Json cues = Json::object();                 // {"en":[...],"pl":[...]} phrases that type a prompt
  std::vector<Role> roles;                    // relevant roles, most important first
  std::vector<PrincipleLevel> principle_levels;
  std::vector<EvidenceClass> evidence;        // evidence classes allowed into the context
  std::map<std::string, Resolution> resolutions;  // role name or "*" -> resolution
  std::map<ContextBand, double> budget;       // band -> share of the token budget
  Json to_json() const;
  static Result<GoalType> from_json(const Json& j);
};

// §4 Goal: what one prompt or task wants, typed by a goal type.
struct Goal {
  std::string id;                  // g_
  std::string type;                // goal type id
  std::string text;                // the prompt / task text
  std::vector<std::string> targets;  // entity ids the goal is about
  std::string project;             // project entity (subject of the instance) ("" none)
  double confidence = 1.0;         // of the goal typing
  Json params = Json::object();
  static std::string make_id(std::string_view type, std::string_view text, const std::vector<std::string>& targets);
  Json to_json() const;
  static Result<Goal> from_json(const Json& j);
};

// §4 One item of a ContextSet, with WHY it is there.
struct ContextItem {
  RefKind ref_kind = RefKind::Claim;
  std::string ref;                 // id of the entity / claim / principle / observation / ...
  ContextBand band = ContextBand::Goal;
  Resolution resolution = Resolution::Summary;
  double score = 0.0;              // relevance x authority x freshness x confidence (with diversity)
  Json factors = Json::object();   // {"relevance","authority","freshness","confidence","diversity"}
  int tokens = 0;
  std::string why;                 // human explanation (required)
  std::vector<std::string> required_by;  // refs whose dependency closure pulled it in
  // Premises the dependency closure needed for this item but could not include
  // (over budget or unresolvable). Non-empty => the item is INCOMPLETE and is
  // rendered with an explicit marker; never a silent drop. Omitted from JSON when empty.
  std::vector<std::string> missing_premises;
  std::string text;                // rendered content at `resolution` ("" when not rendered)
  Json to_json() const;
  static Result<ContextItem> from_json(const Json& j);
};

// §4 ContextSet: the goal-directed selection for one model call, ordered
// stable -> project -> goal.
struct ContextSet {
  std::string id;                  // cx_
  Goal goal;
  int budget_tokens = 0;
  int used_tokens = 0;
  std::string pack_hash;
  std::vector<ContextItem> items;  // band order, then score desc, then ref
  std::vector<ContextItem> dropped;  // considered but excluded (why says why)
  static std::string make_id(std::string_view goal_id, int budget_tokens, std::string_view pack_hash);
  // Items are in band order and used_tokens <= budget_tokens.
  Status validate() const;
  Json to_json() const;
  static Result<ContextSet> from_json(const Json& j);
};

// ═════════════════════════════════════════════════════════════════════
// Layer E — decisions, actions, products (§5)
// ═════════════════════════════════════════════════════════════════════

// §3.2 Effect of an alternative on one value (a principle of level value).
struct ValueImpact {
  std::string value;               // value principle id
  ValueEffect effect = ValueEffect::Neutral;
  std::string rationale;
  Json to_json() const;
  static Result<ValueImpact> from_json(const Json& j);
};

struct DecisionAlternative {
  std::string label;
  std::string object;              // entity id ("" when literal)
  Json value;
  std::vector<ValueImpact> effects;
  bool chosen = false;
  Json to_json() const;
  static Result<DecisionAlternative> from_json(const Json& j);
};

// §5 Decision: a claim of predicate `decides` (id = that claim's id) with the
// chosen alternative among the recorded ones and its rationale: "follows from
// principle X, which protects value Y, under constraint Z".
struct Decision {
  std::string id;                  // the `decides` claim id
  std::string subject;             // what is decided about (entity id)
  std::vector<DecisionAlternative> alternatives;
  std::vector<std::string> principles;   // X
  std::vector<std::string> constraints;  // Z (claim / entity ids or text)
  std::string date;
  DecisionStatus status = DecisionStatus::Active;
  std::string superseded_by;       // decision id ("" none)
  std::vector<std::string> forks;  // fork ids
  const DecisionAlternative* chosen() const noexcept;
  Json to_json() const;
  static Result<Decision> from_json(const Json& j);
};

// §5 Fork: a branch point; both sides are kept.
struct ForkSide {
  std::string ref;                 // message / version / alternative / entity id
  std::string label;
  bool chosen = false;
  bool abandoned = false;
  std::string date;
  Json to_json() const;
  static Result<ForkSide> from_json(const Json& j);
};
struct Fork {
  std::string id;                  // fk_
  ForkKind kind = ForkKind::Design;
  std::string subject;             // what forks (conversation / topic / project entity)
  std::string base;                // base message / version ("" unknown)
  std::vector<ForkSide> sides;
  std::string date;
  static std::string make_id(ForkKind kind, std::string_view subject, std::string_view base,
                             const std::vector<ForkSide>& sides);
  Json to_json() const;
  static Result<Fork> from_json(const Json& j);
};

// §5 Status of a part per branch and version. Oscillation (lost -> restored
// -> lost again) is recorded, never overwritten.
struct StatusRecord {
  std::string id;                  // sr_
  std::string entity;              // part / component / feature entity id
  std::string branch;              // "" = main line
  std::string version;             // normalised version ("" unknown)
  StatusValue status = StatusValue::Planned;
  std::string date;
  std::string claim;               // claim carrying the assessment ("" none)
  std::optional<StatusValue> previous;  // status in the preceding record of this entity + branch
  bool oscillation = false;        // lost/restored seen before on this entity + branch
  static std::string make_id(std::string_view entity, std::string_view branch, std::string_view version,
                             StatusValue status, std::string_view date);
  Json to_json() const;
  static Result<StatusRecord> from_json(const Json& j);
};

// Orders one entity's records per branch (version, then date, then id),
// fills `previous`, and marks `oscillation` on a record that returns to
// implemented/partial/restored after `lost` when an earlier `lost` ->
// `restored` cycle exists, or goes `lost` again after a `restored`.
// Deterministic; other fields untouched.
std::vector<StatusRecord> order_status_history(std::vector<StatusRecord> records);

// §5 Prediction: an inferred claim about a future decision — in situation D
// the owner will choose solution class E — evaluated in the temporal holdout.
struct Prediction {
  std::string id;                  // pn_
  std::string situation;           // situation type (operator situation / text)
  Json features = Json::object();  // situation features at decision time
  std::string solution;            // predicted solution class
  std::string op;                  // operator id producing it
  std::vector<std::string> principles;
  double confidence = 0.0;
  std::string cut;                 // temporal holdout cut date T
  std::string claim;               // the inferred claim ("" none)
  CheckState outcome = CheckState::Pending;   // holds / violated after evaluation
  std::vector<std::string> evaluated_against;  // observation / claim ids after T
  static std::string make_id(std::string_view op, std::string_view situation, std::string_view cut);
  Json to_json() const;
  static Result<Prediction> from_json(const Json& j);
};

// I8 One check a product passed or failed (a preference violation is a
// failing check, not a style note).
struct ProductCheck {
  std::string check;               // check id (rules/checks.json) or principle id
  std::string principle;
  bool passed = false;
  std::string detail;
  Json to_json() const;
  static Result<ProductCheck> from_json(const Json& j);
};

// §5 Product: a materialized output of a project kind, with what it depends
// on and the checks it passed (I8).
struct Product {
  std::string id;                  // pd_
  std::string kind;                // product kind (artifact type id or materializer kind)
  std::string instance;            // instance it materializes
  std::string artifact;            // loom_artifacts id ("" not stored yet)
  std::vector<std::string> claims;       // depends on
  std::vector<std::string> principles;   // depends on (incl. preferences)
  std::vector<ProductCheck> checks;
  std::string run;                 // knowledge run it was built from
  bool passed() const noexcept;
  static std::string make_id(std::string_view kind, std::string_view instance, std::string_view run);
  Json to_json() const;
  static Result<Product> from_json(const Json& j);
};

// §6.7 Owner judgement: an append-only event, replayed last on every rebuild
// (I4). Payload per verdict: confirm/reject {} ; edit on a claim
// {"value": v} or {"object": id}, on an entity {"label": ...}, on a principle
// or operator the fields to replace ; merge {"into": entity id} ; split
// {"aliases": [alias keys], "key": new canonical key}.
struct Judgement {
  std::string id;                  // ju_
  std::int64_t seq = 0;            // append order (assigned by the store)
  RefKind target_kind = RefKind::Claim;
  std::string target;
  Verdict verdict = Verdict::Confirm;
  Json payload = Json::object();
  std::string author = "user";
  std::string reason;
  std::string created;             // wall clock (the only wall-clock field of the model)
  static std::string make_id(std::string_view created, std::string_view target, Verdict v, const Json& payload);
  // The verdict is allowed for the target kind and the payload has the
  // required keys (see above).
  Status validate() const;
  Json to_json() const;
  static Result<Judgement> from_json(const Json& j);
};

// ═════════════════════════════════════════════════════════════════════
// Views over the data pack
// ═════════════════════════════════════════════════════════════════════
// Typed, validated views of pack files (kb::Pack keeps the JSON). Ids are
// returned sorted; unknown id -> Errc::NotFound.
Result<ProjectKind> project_kind(const kb::Pack& pack, std::string_view id);
Result<Facet> facet(const kb::Pack& pack, std::string_view id);
Result<ArtifactType> artifact_type(const kb::Pack& pack, std::string_view id);
Result<Principle> principle(const kb::Pack& pack, std::string_view id);
Result<Operator> pack_operator(const kb::Pack& pack, std::string_view id);  // operators.json + inference rules
Result<Morphism> morphism(const kb::Pack& pack, std::string_view id);
Result<GoalType> goal_type(const kb::Pack& pack, std::string_view id);
Result<AnchoringModel> anchoring(const kb::Pack& pack);
Result<std::vector<Principle>> principles(const kb::Pack& pack);
Result<std::vector<Operator>> pack_operators(const kb::Pack& pack);

// ── Priors and the temporal holdout (§7.1) ──────────────────────────
// Seed principles and seed operators (philosophy/*.json) are PRIORS. For a
// temporal-holdout cut at T the engine must not see priors whose text did
// not exist yet: a prior is visible when its EARLIEST dated source is on or
// before `as_of` (ISO date compare on the first 10 chars). Undated priors are
// invisible under a cut (the pack validator requires dates, so this only
// affects overlay/mined data). enabled = false drops every prior.
struct PriorFilter {
  bool enabled = true;
  std::string as_of;  // "" = no cut
  static PriorFilter none() { return PriorFilter{false, {}}; }
  static PriorFilter as_of_date(std::string date) { return PriorFilter{true, std::move(date)}; }
};
// Smallest non-empty source date ("" when none is dated).
std::string earliest_source_date(const std::vector<Reference>& sources);
bool prior_visible(const std::vector<Reference>& sources, const PriorFilter& filter);
// Pack priors after the filter (sorted by id). pack_operators(pack, f)
// filters the design operators of philosophy/operators.json only; inference
// rules are pack mechanics, returned unfiltered — the generalize area skips
// a rule whose basis principles were filtered out.
Result<std::vector<Principle>> principles(const kb::Pack& pack, const PriorFilter& filter);
Result<std::vector<Operator>> pack_operators(const kb::Pack& pack, const PriorFilter& filter);
Result<std::vector<Morphism>> morphisms(const kb::Pack& pack);  // explicit transfer morphisms
// One anchoring morphism per domain kind of every project kind and facet
// ({"use":"anchoring","from":{"paradigm","kind"},"to":{"role"}}, id
// "m.anchor.<paradigm>.<kind>"), derived from the `role` of the domain kind.
Result<std::vector<Morphism>> anchoring_morphisms(const kb::Pack& pack);

}  // namespace loom::model
