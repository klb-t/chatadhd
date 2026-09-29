// Internal: the pack resources the extractor reads (lexicons, cue classes,
// gazetteer, profile aliases, relation and version patterns), prepared once
// per Extractor. Everything is derived from pack data; the code only holds the
// matching algorithms (I6). Not installed.
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "loom/kb.h"
#include "loom/re/regex.h"
#include "loom/util/json.h"

namespace loom::extract::detail {

// A lowercase, diacritic-folded cue phrase; `prefix` = trailing '*' (word prefix).
struct Phrase {
  std::string text;
  bool prefix = false;
  double w = 1.0;
};

// Word-boundary search of `p` in folded text from byte `from`; npos when absent.
// *len receives the matched length (prefix phrases extend to the word end).
std::size_t find_phrase(std::string_view text, const Phrase& p, std::size_t from, std::size_t* len);

struct CueHit {
  std::string phrase;   // folded phrase (without '*')
  double w = 0.0;
  std::size_t pos = 0;  // byte offset in the folded text
  std::size_t len = 0;
  bool negated = false;  // a negation particle precedes it (same clause, <= 2 words)
};

// A word token with byte offsets into the text it came from.
struct Token {
  std::string lower;  // lowercased surface
  std::string surface;
  std::size_t start = 0;
  std::size_t end = 0;
};
// Letter/digit runs ('+'/'#' after letters: "c++"), like kb::Normalizer::tokens,
// with offsets; tokens without a letter are kept too when `keep_numbers`.
std::vector<Token> tokenize(std::string_view text, bool keep_numbers = false);

// One lexicon entry that yields typed entity mentions: a gazetteer entry, a
// profile project, or a name discovered in another unit of the same run.
struct LexEntry {
  std::string id;
  std::string cls;        // gazetteer class / "project"
  std::string kind;       // entity kind
  std::string label;
  std::string label_pl;
  std::string canonical;  // canonical key (normalizer phrase key of the label)
  std::string source;     // gazetteer | profile | discovered
  std::string merged_into;
};
struct LexForm {
  std::size_t entry = 0;
  std::string surface;
  bool ambiguous = false;
  std::vector<std::string> requires_ctx;  // folded terms (prefix match)
  int ctx_min = 1;
  std::vector<std::string> negative_ctx;  // folded terms
  bool inflect = false;  // discovered names: a token may extend the key by <= 3 chars (PL inflection)
  std::size_t tokens = 1;
};

// A relation template (lexicons/relation_patterns.json).
struct PatElem {
  enum Kind { Literal, Alt, Slot, Wild } kind = Literal;
  std::vector<std::vector<Phrase>> alts;  // Literal: one alt of one word; Alt: alternatives of word sequences
  std::string slot;                        // subj | obj
  std::vector<std::string> types;          // entity kinds / gazetteer classes / any / text
  bool plus = false;                       // platform+ : one or more joined
};
struct RelPattern {
  std::string id;
  std::string rel;
  double confidence = 0.5;
  std::string subject_ref;  // "$context_project" or ""
  std::vector<PatElem> elems;
};

struct VersionDecl {
  std::string id;
  re::Regex re;
  double confidence = 0.8;
};

class Lexicons {
 public:
  // `discovered`: [{"kind","label","aliases":[...]}] names found by a first
  // pass over the other units of the same run.
  Lexicons(const kb::Pack& pack, const Json& discovered);

  kb::Normalizer norm;
  std::string fold(std::string_view s) const { return norm.fold(s); }

  // ── cue classes (cues.json) ─────────────────────────────────────
  bool has_class(std::string_view cls) const { return cues_.count(std::string(cls)) > 0; }
  const std::vector<Phrase>& phrases(std::string_view cls) const;
  // Hits of a cue class in folded text (non-overlapping per phrase).
  std::vector<CueHit> match(std::string_view cls, std::string_view folded) const;
  // Sum of weights of the non-negated hits.
  double score(std::string_view cls, std::string_view folded) const;
  bool negated_at(std::string_view folded, std::size_t pos) const;
  std::vector<std::string> classes_with_prefix(std::string_view prefix) const;

  // ── item cues (item_cues.json) ──────────────────────────────────
  std::vector<std::string> item_types;                       // type_order
  std::map<std::string, std::vector<Phrase>> item_cues;       // type -> phrases
  std::vector<std::pair<std::string, Phrase>> heading_hints;  // type, phrase
  std::set<std::string, std::less<>> negators;                // folded
  std::set<std::string, std::less<>> generic_words;           // folded

  // ── typed lexicon entries (gazetteer, profile, discovered) ──────
  std::vector<LexEntry> entries;
  std::unordered_map<std::string, std::vector<LexForm>> forms;  // phrase key -> forms
  std::vector<std::pair<std::string, LexForm>> inflecting;      // single-token discovered forms: key -> form
  std::size_t max_form_tokens = 1;
  std::set<std::string, std::less<>> first_keys;                // first word of every form key
  std::map<std::string, std::string, std::less<>> class_kind;   // gazetteer class -> entity kind

  // ── versions (version_patterns.json) ────────────────────────────
  std::optional<re::Regex> version_re;
  std::vector<std::string> version_anchors;  // folded
  std::vector<VersionDecl> version_decls;
  std::vector<std::string> version_excludes;  // folded
  int version_window = 12;
  std::set<std::string, std::less<>> third_party_kinds;  // entity kinds whose version is not the project's

  // ── relation patterns ───────────────────────────────────────────
  std::vector<RelPattern> patterns;

  // ── plausibility of mined names (name_rules.json) ───────────────
  // False for a code fragment, file name, or (projects) a phrase that starts
  // or ends with a function word / a bare lowercase common noun. `strict`
  // false: only the syntax rules (the caller has a strong structural cue such
  // as an explicit 'projects (A, B, C)' head).
  // False for a code line / code comment (prose gate of the sentence extractors).
  bool prose_ok(std::string_view text) const;
  bool name_ok(std::string_view kind, std::string_view label, std::string_view artifact_type = {},
               bool strict = true) const;

  // ── thresholds (policy/thresholds.json) ─────────────────────────
  int context_window = 30;
  double status_min_weight = 1.5;
  Json thresholds = Json::object();

 private:
  Json name_rules_ = Json::object();
  std::vector<re::Regex> name_reject_;
  std::vector<re::Regex> prose_reject_;
  std::map<std::string, std::vector<Phrase>> cues_;
  std::vector<Phrase> empty_;
  std::vector<Phrase> neg_particles_;
  void add_form(std::size_t entry, std::string_view surface, LexForm f);
};

// Folded phrase (lowercase + diacritics folded, '*' stripped into prefix).
Phrase make_phrase(const kb::Normalizer& norm, std::string_view p, double w);

}  // namespace loom::extract::detail
