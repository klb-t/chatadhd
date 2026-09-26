// loom/kb.h — shared foundation of the knowledge layer
// (docs/architecture/LOOM_CONCEPTUAL_MODEL.md; the model types are in
// loom/model.h, storage in loom/knowledge_store.h). Foundation-owned.
//
// What every knowledge-layer area (catalog, extract, resolve, generalize,
// context, materialize, UI) needs and must agree on:
//
//   1. the evidence vocabulary (Evidence, CheckState, ExpectedProperty) and
//      its mapping to the ground-truth format;
//   2. the data pack (loom/data/, MEGA MASTER §2.B "policy as data"): loading,
//      schema validation, cross-file reference checks, a content hash that
//      every kb.* stage folds into its input hash; plus the CLOSED SETS that
//      code implements (binding kinds, anchor ops, conditions, value ops,
//      expected-property predicates) — the pack may only combine them;
//   3. text normalisation for match keys (PL + EN light stemming, diacritic
//      folding, stop words, glossary, version normalisation) and stable ids;
//   4. the Loom-only tables of the knowledge layer (loom_kb_*), created
//      lazily so a data directory that never runs it is unchanged.
//
// Everything here is deterministic and never throws.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Database;

namespace kb {

// ── Evidence vocabulary ─────────────────────────────────────────────
// observed     stated in a source (extractor + locator)
// derived      deterministic function of observed values (max version, ...)
// inferred     rule over facts/principles; carries an ExpectedProperty
// extrapolated philosophy-driven proposal; confidence capped
//              (thresholds.paradigm.extrapolation_cap); never an input to rules;
//              excluded from ground-truth-shaped output (goes to "proposals")
// absent       no candidate; the UI shows what would fill it
// user         asserted/confirmed by the user; always wins (constitution 4.6)
// A conflict is NOT an evidence class: it is a flag on a slot whose top
// values are incompatible (every value keeps its own evidence class).
enum class Evidence { Observed, Derived, Inferred, Extrapolated, Absent, User };
std::string_view to_string(Evidence e) noexcept;
std::optional<Evidence> evidence_from_string(std::string_view s) noexcept;
// Ground-truth mapping: observed/derived/user -> "observed", inferred ->
// "inferable", absent -> "absent", extrapolated -> "" (not reported).
std::string_view gt_evidence(Evidence e) noexcept;

// Re-evaluation state of an inferred value's expected property.
enum class CheckState { Pending, Holds, Violated, NotApplicable };
std::string_view to_string(CheckState s) noexcept;
std::optional<CheckState> check_state_from_string(std::string_view s) noexcept;

// What an inferred value vouches for: a checkable predicate (closed set,
// kPredicates) plus human text. The value itself is only the argmax
// candidate; the property is the claim.
struct ExpectedProperty {
  Json expr = Json::object();            // {"op": "<predicate>", "args": [...]}
  std::string rationale;                 // why the value must have the property
  std::vector<std::string> confirm_if;   // evidence that would confirm it
  std::vector<std::string> refute_if;    // evidence that would refute it
  Json to_json() const;                  // {"expr","rationale","confirm_if","refute_if"}
  static Result<ExpectedProperty> from_json(const Json& j);
  // One-line rendering of expr for text UIs and the ground-truth
  // "expected_property" string, e.g. "all(in_class(storage.embedded), ...)".
  std::string render() const;
};

// ── Closed sets implemented in code ─────────────────────────────────
// The pack validator rejects any name outside these. Adding a name is a
// code change (new semantics, constitution E), reviewed by the lead.
inline constexpr std::string_view kBindingKinds[] = {
    "subject", "fact",   "items",    "lexicon", "codebase", "slot",
    "principles", "forks", "enumeration", "versions", "checks"};
inline constexpr std::string_view kAnchorOps[] = {
    "fact_count", "kind_hint", "cue", "option_set", "has_items", "principle_count", "codebase"};
inline constexpr std::string_view kConditionOps[] = {
    "const", "nonempty", "empty", "all", "any", "not", "has_fact", "has_items", "principle_active", "eq", "gt"};
inline constexpr std::string_view kValueOps[] = {
    "const",           "slot",           "max_version",          "union",          "lexicon_pick",
    "codebase_field",  "derive_project_status", "derive_component_status", "order_chain",
    "option_from_decision", "complement", "versions_above",      "first_mention_date",
    "comentioned_components", "base_version", "version_forks",   "principle_template"};
inline constexpr std::string_view kPredicates[] = {
    "all",          "any",          "not",           "nonempty",        "in_enum",
    "in_class",     "not_in",       "subset_of",     "version_gt",      "version_gte",
    "version_between", "date_between", "co_mentioned_with", "consistent_with", "available_on",
    "exists_symbol", "matches",     "not_contradicted_by", "ordered_by_date"};
// Per-binding-kind field vocabularies.
inline constexpr std::string_view kSubjectFields[] = {"label", "aliases", "status", "first_seen", "last_seen"};
inline constexpr std::string_view kCodebaseFields[] = {"repo",        "languages", "dominant_language", "modules",
                                                       "entry_points", "tests",    "interfaces",        "declared_version",
                                                       "files"};
inline constexpr std::string_view kForkFields[] = {"options", "chosen", "abandoned"};
// Slot types: these, plus "entity:<entity kind>" and "enum:<types.enums key>".
inline constexpr std::string_view kSlotTypes[] = {"string", "text", "version", "date", "principle[]", "record[]"};
inline constexpr std::string_view kCardinalities[] = {"one", "many", "list"};
inline constexpr std::string_view kViolationActions[] = {"conflict", "reject_instance", "extend"};
inline constexpr std::string_view kCheckDetectors[] = {"string_array_literal", "regex_line", "regex_block"};
inline constexpr std::string_view kRuleProduces[] = {"derived", "inferred", "extrapolated"};
// Reference prefixes allowed in rule/constraint args ("$slot:x", "$value", ...).
inline constexpr std::string_view kRefPrefixes[] = {
    "$value", "$subject", "$slot:", "$field:", "$analog:", "$git_max_version", "$corpus_end_date",
    "$rejected_values", "$prev_version_date", "$next_version_date"};

bool in_closed_set(std::string_view name, const std::string_view* begin, const std::string_view* end) noexcept;
template <std::size_t N>
bool in_closed_set(std::string_view name, const std::string_view (&set)[N]) noexcept {
  return in_closed_set(name, set, set + N);
}

// ── Data pack ───────────────────────────────────────────────────────
// Layout: pack.json lists every file with its schema id
// ("loom.kb.<kind>/<n>"); see loom/data/pack.json. The built-in pack is the
// build-time copy of loom/data (src/kb/pack_embedded.inc, regenerated by
// tools/gen_kb_pack.py; test_kb_pack checks it equals the directory).
struct PackIssue {
  std::string file;      // relative path ("" = whole pack)
  std::string pointer;   // JSON pointer inside the file ("/slots/3/bind/0/kind")
  std::string message;
  Json to_json() const;
};

class Pack {
 public:
  // The embedded copy of loom/data.
  static Result<std::shared_ptr<const Pack>> load_builtin();
  // A directory with the loom/data layout (pack.json at its root).
  static Result<std::shared_ptr<const Pack>> load_dir(const std::filesystem::path& dir);
  // Built-in pack with the files of `overlay_dir` (same layout; may be
  // partial, may hold its own pack.json listing only its files) replacing the
  // built-in ones. A missing overlay dir = the built-in pack.
  static Result<std::shared_ptr<const Pack>> load_with_overlay(const std::filesystem::path& overlay_dir);
  // From in-memory documents (relative path -> parsed JSON), pack.json
  // included. All loaders end here: schema + cross-reference validation;
  // any issue -> Errc::InvalidArgument whose message lists the issues.
  static Result<std::shared_ptr<const Pack>> from_documents(std::map<std::string, Json> docs);

  // sha256 hex over the canonical JSON of every file, in path order. Every
  // kb.* / catalog.* stage input hash includes it.
  const std::string& hash() const noexcept { return hash_; }
  std::string id() const;
  int version() const;
  std::vector<std::string> files() const;              // relative paths, sorted
  const Json& file(std::string_view relpath) const;    // parsed document; null Json when absent
  // Typed views (null Json when absent).
  const Json& types() const;                            // schema/types.json
  Json paradigm_ids() const;                            // ["agent_system", ...] sorted
  const Json& paradigm(std::string_view id) const;      // paradigms/<id>.json
  const Json& rule(std::string_view id) const;          // element of rules/inference_rules.json
  const Json& principle(std::string_view id) const;     // element of philosophy/seed_principles.json
  const Json& policy(std::string_view name) const;      // policy/<name>.json
  const Json& lexicon(std::string_view name) const;     // lexicons/<name>.json
  const Json& profile(std::string_view id) const;       // profiles/<id>.json
  // {"id","version","hash","files":[{"path","schema","sha256"}]}
  Json manifest() const;

 private:
  std::map<std::string, Json, std::less<>> docs_;
  std::string hash_;
  Json empty_;
};

// Validates one document of the given schema id against its schema (types,
// required keys, closed sets). `types` is schema/types.json (entity kinds,
// enums, relations) for reference checks; may be null for the types file
// itself. Issues are appended; returns true when none were added.
bool validate_document(std::string_view relpath, std::string_view schema, const Json& doc, const Json& types,
                       std::vector<PackIssue>& issues);
// Cross-file checks over a whole pack: rule ids referenced by paradigms
// exist, principle ids referenced by rules/checks exist, check ids referenced
// by principles exist, lexicon classes referenced by paradigms exist,
// relations used by paradigms/patterns are declared in types.json, enum
// names resolve, pack.json lists exactly the files present.
void validate_cross_references(const std::map<std::string, Json, std::less<>>& docs, std::vector<PackIssue>& issues);

// ── Text normalisation (match keys; never display) ──────────────────
enum class Lang { En, Pl, Mixed, Unknown };
std::string_view to_string(Lang l) noexcept;

// Match keys for PL + EN text. The tables (fold map, suffix lists, markers,
// exceptions, stop words, glossary) are pack data (lexicons/stemming.json,
// stopwords*.json, glossary.json); the algorithm is code:
//
//   token language   Pl when the token carries a Polish signal: a diacritic,
//                    a Polish stop word, a Polish exception form or a Polish
//                    marker suffix ("-cji", "-ów", "-ych", ...); else En.
//   stem             exception form -> fixed key; tokens shorter than
//                    min_token are kept; the longest matching rewrite
//                    ("-acji" -> "-acja", "-ies" -> "-y", "-xes" -> "-x")
//                    is applied, then the longest suffix whose removal keeps
//                    min_stem code points is stripped once ("-s" not after
//                    keep_endings "ss"/"us"/"is"; verbal "-ing"/"-ed" only
//                    when the stem keeps a vowel, then undoubling
//                    "runn" -> "run" and e-restoration "stor" -> "store",
//                    "creat" -> "create").
//   key              fold(stem) (lowercase, diacritics folded).
//   phrase key       keys of the non-stop-word tokens joined by spaces; a
//                    phrase with any Polish signal is keyed as Polish.
//   glossary         left to right, the longest run of tokens whose English
//                    keys or Polish keys form a glossary phrase is replaced
//                    by the English phrase key: "czat ADHD" == "chat adhd"
//                    == "chat adhd", "grafu wiedzy" == "knowledge graph".
class Normalizer {
 public:
  // Reads lexicons/stemming.json, stopwords_base.json, stopwords.json and
  // glossary.json of the pack.
  explicit Normalizer(const Pack& pack);

  // Lowercase (full Unicode lowering) + diacritic folding (stemming.json "fold").
  std::string fold(std::string_view text) const;
  // Word tokens: runs of letters/digits (plus '+', '#' after letters: "c++",
  // "c#"), lowercased, not folded; pure numbers dropped.
  std::vector<std::string> tokens(std::string_view text) const;
  bool is_stopword(std::string_view token) const;  // surface or folded form
  // Pl or En for one lowercase token (see above); never Mixed/Unknown.
  Lang token_lang(std::string_view lower_token) const;
  // Light stem of a lowercase token with the given language's tables
  // (Unknown/Mixed -> token_lang). Not folded.
  std::string stem(std::string_view token, Lang lang) const;
  // Folded + stemmed key of one token; the language is token_lang() unless
  // given (Pl/En).
  std::string match_key(std::string_view token, Lang lang = Lang::Unknown) const;
  // Space-joined match keys of the non-stop-word tokens of a phrase; with
  // map_glossary the glossary maps PL and EN phrases to one English key.
  // `lang` Pl/En forces the language of every token without a contrary
  // signal (use the observation's language when known).
  std::string phrase_key(std::string_view phrase, bool map_glossary = true, Lang lang = Lang::Unknown) const;
  // Share of Polish stop words / diacritics decides Pl vs En; both above
  // 0.25 -> Mixed; too little text -> Unknown.
  Lang guess_lang(std::string_view text) const;

 private:
  struct Stemmer {
    std::size_t min_token = 0, min_stem = 0;
    std::vector<std::pair<std::string, std::string>> rewrite;  // suffix -> replacement, longest first
    std::vector<std::string> suffixes;                         // longest first
    std::vector<std::string> keep_endings;                     // a suffix is not stripped from these endings
    std::set<std::string, std::less<>> verbal;                 // suffixes followed by undouble / e-restoration
    std::vector<std::string> restore_e;                        // "at" -> "ate" after a verbal suffix
    std::vector<std::string> markers;                          // suffixes that mark a token as this language
    std::map<std::string, std::string, std::less<>> exceptions;
  };
  std::map<char32_t, std::string> fold_;
  Stemmer pl_, en_;
  std::set<std::string, std::less<>> stop_;
  std::set<std::string, std::less<>> pl_stop_;
  std::map<std::string, std::string, std::less<>> glossary_;  // phrase key (pl or en) -> en phrase key
  std::size_t glossary_max_tokens_ = 1;
  std::string stem_with(const Stemmer& s, std::string_view token) const;
  bool has_pl_signal(std::string_view lower_token) const;
  std::string key_as(std::string_view lower_token, Lang lang) const;
};

// Versions: "v0.07.09" -> "0.7.9"; "" when `s` is not a version (1-3
// numeric components). compare_versions is numeric per component with
// missing components = 0 ("0.9" == "0.9.0").
std::string normalize_version(std::string_view s);
int compare_versions(std::string_view a, std::string_view b);

// Stable ids: prefix + the first `hex_chars` hex chars of sha256(key).
// Content-derived, so every run of the same inputs yields the same ids in any
// data directory (I5). Knowledge-layer ids use 16 hex (64 bits: a collision
// needs ~4e9 ids); pass 12 for the Python id format when projecting into the
// core nodes/links tables ("n_" + 12 hex). Prefix table: include/loom/model.h.
inline constexpr std::size_t kStableIdHex = 16;
std::string stable_id(std::string_view prefix, std::string_view key, std::size_t hex_chars = kStableIdHex);

// ── Tables (Loom-only, lazily created) ──────────────────────────────
// Creates / upgrades the loom_kb_* tables (layout and semantics:
// include/loom/knowledge_store.h), idempotent and forward-only, guarded like
// the core migrations. Called before the first write; a data directory that
// never runs the knowledge layer keeps its schema unchanged (I10, Python
// compat). Records the version in loom_kb_meta.schema_version and in
// _meta.loom_kb_schema_version (never touches _meta.loom_schema_version).
// The catalog's loom_cat_* tables belong to the catalog area (catalog.h).
inline constexpr int kKbSchemaVersion = 2;
Status ensure_schema(Database& db);
bool has_schema(Database& db);  // no side effects

}  // namespace kb
}  // namespace loom
