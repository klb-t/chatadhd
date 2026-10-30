// loom/text_fragments.h — text fragments and version alignment (R102 G1, "geneza tekstu").
//
// What the code does today (2026-10-30):
//  * Segmentation: a text is cut into fragments on a LEVEL (presets: paragraph, line, sentence, word; the set of levels
//    is data — any level whose method is known can be added by a pack overlay). Levels form a parent chain ending in
//    "document" (the whole text). Rules are per LANGUAGE as data (presets "*", pl, en, ja, zh; any other language code
//    is added by data; an unknown code falls back to the pack's default_language and says so in `advisories`).
//    Fragments tile the input exactly: concat(text[f.begin, f.end)) == input for every level and every input,
//    including invalid UTF-8 (byte offsets; a bad byte is one codepoint of its own).
//  * Fingerprint: hash of the NORMALIZED core (normalization is data: case, whitespace, punctuation, strip).
//  * Alignment: generalised Myers O((N+M)D) over any units, returning an edit SCRIPT. Base kinds: "equal", "insert",
//    "delete"; then two passes as presets: approximate matching pairs a delete with an insert into "replace" (similarity
//    method and threshold are data; inner word diff inside), and "move" pairs a delete with an insert anywhere by
//    fingerprint (or by similarity >= move_threshold when it is < 1). Kinds are strings: the list is open; consumers
//    must treat an unknown kind as "something changed" and read its a/b sides.
//  * apply(script, a_units) rebuilds b_units exactly (ops carry the b-side text where the a-side does not give it).
// Not yet: graph nodes, events and C ABI (G2), paste matching (G3), vector/model similarity (named in data, refused
// with an explicit error until a later package implements them), automatic language detection.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::text {

inline constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// The embedded preset pack (loom/data/text/segmentation.pack, schema loom.text_segmentation/1).
const Json& builtin_pack();
std::string_view builtin_pack_source();
// Deep merge of objects (overlay wins; arrays and scalars are replaced; null in the overlay removes the key).
Json merge_pack(const Json& base, const Json& overlay);

struct Fragment {
  std::string level;
  std::size_t begin = 0, end = 0;            // byte range in the input; fragments of one level tile the input
  std::size_t core_begin = 0, core_end = 0;  // the range without leading/trailing whitespace (may be empty)
  std::string fingerprint;                   // of the normalized core
  Json to_json() const;
};

struct Segmentation {
  std::string level, language;
  std::vector<Fragment> fragments;
  std::vector<std::string> advisories;  // non-blocking: fallbacks the caller should know about
  Json to_json() const;
};

class Segmenter {
 public:
  // `pack` is a whole pack (builtin_pack() or merge_pack(builtin_pack(), overlay)).
  static Result<Segmenter> from_pack(const Json& pack, std::string_view language);
  static Result<Segmenter> builtin(std::string_view language) { return from_pack(builtin_pack(), language); }

  Result<Segmentation> segment(std::string_view text, std::string_view level) const;
  std::string normalize(std::string_view s) const;
  std::string fingerprint(std::string_view s) const;  // of normalize(s)
  const std::string& language() const { return language_; }
  const Json& pack() const { return pack_; }
  const Json& rules() const { return rules_; }  // the merged language rules in effect
  const std::vector<std::string>& advisories() const { return advisories_; }

 private:
  Json pack_, rules_;
  std::string language_;
  std::vector<std::string> advisories_;
};

// ── Alignment ────────────────────────────────────────────────────────────────────────────────────────────────────────

struct EditOp {
  std::string kind;           // "equal" | "insert" | "delete" | "replace" | "move" | … (open)
  std::size_t a = kNone;      // unit index in a (kNone when the op has no a-side)
  std::size_t b = kNone;      // unit index in b (kNone when the op has no b-side)
  double similarity = 1.0;    // 1 for equal / exact move; the measured value for replace / approximate move
  std::optional<std::string> text;  // b-side text when a[a] does not give it exactly
  std::vector<EditOp> inner;  // the diff inside a replace / approximate move (inner_level units)
  Json to_json() const;
};

struct AlignOptions {
  std::string similarity = "words";  // "words" (2·LCS/(n+m) of normalized words) | "chars" (bigram Dice) | data names
  double replace_threshold = 0.5;    // pair delete+insert of one gap as "replace" when similarity >= this; > 1 = never
  double move_threshold = 1.0;       // 1 = exact fingerprint only; < 1 also pairs changed moves; > 1 = no moves
  std::string inner_level = "word";  // "" = no inner diff
  static Result<AlignOptions> from_pack(const Json& pack);
};

struct Alignment {
  std::vector<EditOp> ops;  // in b order, deletes placed after the b unit they follow
  std::size_t count(std::string_view kind) const;
  Json to_json() const;
};

// Pure Myers over interned ids: a shortest script of "equal"/"insert"/"delete" (one op per unit).
std::vector<EditOp> myers_script(const std::vector<int>& a, const std::vector<int>& b);
// Its length D (inserts + deletes), the quantity resolve::changed_lines reports for lines.
std::size_t myers_distance(const std::vector<int>& a, const std::vector<int>& b);

// Similarity in [0, 1] of two strings under a named method (symmetric; two empty strings = 1).
Result<double> similarity(const Segmenter& seg, std::string_view method, std::string_view x, std::string_view y);

// Align two unit sequences (texts as cut by the caller). keys = fingerprints (equality of units).
Result<Alignment> align_units(const Segmenter& seg, const std::vector<std::string>& a, const std::vector<std::string>& b,
                              const AlignOptions& opt);
// Segment both texts on `level` and align the fragments (units = the full fragment text, so concat(apply) == b).
Result<Alignment> align_texts(const Segmenter& seg, std::string_view a, std::string_view b, std::string_view level,
                              const AlignOptions& opt);
// Rebuild b from a and the script; an error when the script does not cover a and b exactly once.
Result<std::vector<std::string>> apply(const Alignment& al, const std::vector<std::string>& a);

std::vector<std::string> fragment_texts(std::string_view text, const Segmentation& s);

}  // namespace loom::text
