// Internal types and pure helpers of the catalog area (include/loom/catalog.h).
// Not installed; the catalog unit tests include it directly. Everything here
// is deterministic: given the same bytes and the same SketchParams / pack, the
// output is byte-identical regardless of thread count (I5).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "loom/catalog.h"
#include "loom/kb.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::catalog::internal {

// Shared by source scanning and the knowledge input fingerprint. Active
// runtime storage and generated products must never become their own input.
// No name-based build/vendor exclusions: explicit full imports keep scope.
bool skip_source_directory(const std::filesystem::path& path, const std::filesystem::path& active_data);

// ── Bloom filter ──────────────────────────────────────────────────────
// Fixed-size bit array + k independent hashes (double hashing: h_i = h1 +
// i*h2, Kirsch-Mitzenmacher). Sized from an estimate of the number of
// distinct terms and the target false-positive rate.
struct Bloom {
  int bits = 0;
  int hashes = 1;
  std::vector<std::uint64_t> words;  // 64 bits each, ceil(bits/64) words

  static Bloom sized(int n_estimate, double fpr);
  void add(std::string_view term);
  bool maybe_contains(std::string_view term) const;
  std::string to_base64() const;
  static Bloom from_base64(std::string_view b64, int bits, int hashes);
};

// ── MinHash ───────────────────────────────────────────────────────────
// K independent min-hash lanes over word 3-shingles of the (stemmed) content
// tokens. Deterministic per-lane seeds (splitmix64 of the lane index), so the
// same text always yields the same signature regardless of machine/thread.
struct MinHash {
  std::vector<std::uint64_t> sig;  // one value per lane; UINT64_MAX = no shingles seen

  static MinHash build(const std::vector<std::string>& stemmed_tokens, int lanes);
  double jaccard(const MinHash& other) const;
  Json to_json() const;
  static MinHash from_json(const Json& j);
};

// One (term, count) pair, as kept by the top-K list.
using TermCount = std::pair<std::string, int>;

// ── Sketch ────────────────────────────────────────────────────────────
// Everything scored against a unit without re-reading its bytes: top-K terms
// (exact, Misra-Gries-then-recount, so results are deterministic even though
// Misra-Gries itself is an approximation of frequency, not of the *set*),
// a Bloom filter over every distinct content term (covers terms that fall out
// of the top-K), and a MinHash signature for approximate document
// similarity/continuation links. `truncated` is set when the input text
// exceeded the per-unit sketching cap (kSketchByteCap): the sketch still
// covers the whole capped prefix, never fewer bytes than what was read.
struct Sketch {
  std::vector<TermCount> top_terms;  // sorted by count desc, then term asc
  Bloom bloom;
  MinHash minhash;
  std::string lang;  // "pl" | "en" | "mixed" | ""
  std::int64_t n_chars = 0;
  std::int64_t n_code_chars = 0;
  bool truncated = false;

  // Bytes of `text` beyond SketchParams-derived caps are not read (bounded
  // memory regardless of unit size). `code_text` (fenced code blocks,
  // concatenated) is tokenised separately and also counted into n_code_chars;
  // its tokens still enter the same top-K/bloom/minhash (stronger evidence,
  // handled by the caller via the term-class weighting, not here).
  static Sketch build(std::string_view text, std::string_view code_text, const SketchParams& params,
                      const kb::Normalizer& norm);
  bool contains(std::string_view term) const;  // top-K exact hit or bloom probe
  int topk_tf(std::string_view term) const;    // 0 when absent from the top-K (bloom-only lower bound is 1)
  Json to_json() const;
  static Result<Sketch> from_json(const Json& j);
};

// Per-unit sketching reads at most this many bytes of prose (Misra-Gries +
// bloom + minhash cost is linear in input size; a single pathological unit
// must not blow the per-worker memory budget). Matches proposal_scale.md §3.
inline constexpr std::int64_t kSketchByteCap = 4LL << 20;
// A single element read while streaming a JSON member is bounded too: past
// this, the element's body is not DOM-parsed (attrs.truncated=true), only
// hashed + sketched on the capped prefix.
inline constexpr std::int64_t kElementByteCap = 32LL << 20;
// Read chunk size used for both plain files and zip streaming extraction.
inline constexpr std::size_t kReadChunk = 256 * 1024;

// ── Alias / mention matching ────────────────────────────────────────────
// One probe term of the self-profile, expanded from profiles/self.json (or a
// ProfileConfig override): a project alias, a philosophy-probe anchor, or a
// term discovered by expansion. Context checks are plain substring search
// over a bounded window around the occurrence in folded (lowercased,
// diacritics stripped) text -- the pack writes cues as stems ("aplikacj").
struct AliasTerm {
  std::string project;    // profile project id ("" for the philosophy probe)
  std::string surface;    // as written in the pack ("ChatADHD")
  std::string folded;     // Normalizer::fold(surface), the substring probed
  std::string term_class = "alias";  // alias | principle | concept | path | identifier | config_key | table
  bool ambiguous = false;
  bool prefix = false;  // intentional word-prefix match; identity aliases default to whole words
  std::vector<std::string> requires_any;  // folded context cues
  int requires_min = 1;
  std::vector<std::string> negative;      // folded negative-context cues
  double weight = 3.0;
  // Whole-token inflection (AliasIndex::enable_inflection): the alias's
  // word tokens, their Polish match keys and the byte prefix every
  // inflected form must start with. Empty = exact matching only.
  std::vector<std::string> word_tokens, word_keys, word_anchors;
};

struct Mention {
  std::string kind;   // "alias" | "version" | "principle"
  std::string key;     // project id / principle id / version string
  std::string snippet;
  std::int64_t offset = 0;
  bool trap = false;         // an ambiguous alias whose negative context dominated
  std::string trap_reason;
  Json to_json() const;
  static Mention from_json(const Json& j);
};

// Built once per scan/score pass from a SelfProfile (or straight from the
// pack when no profile was explicitly built yet).
class AliasIndex {
 public:
  static AliasIndex from_profile(const SelfProfile& profile, int context_window_tokens = 30);
  static AliasIndex from_pack(const kb::Pack& pack);

  // Every match against `folded_text` (already Normalizer::fold()-ed), valid
  // and trap alike, capped at `max_mentions` (traps count against the same
  // cap so a flooded trap unit does not starve real evidence). Offsets are
  // byte offsets into `folded_text`. Folding CAN change byte lengths; these
  // are approximate display anchors, never exact source spans. Ambiguous
  // context uses at most context_window_tokens Unicode word tokens before
  // and after this occurrence, rather than the entire conversation.
  std::vector<Mention> find(std::string_view folded_text, int max_mentions) const;
  const std::vector<AliasTerm>& terms() const noexcept { return terms_; }

  // Morphology-aware identity matching, still at whole-word boundaries
  // (never substrings): an alias made only of space-separated word tokens
  // also matches when every text token is an inflected form of the alias
  // token at its position -- the alias token plus one inflectional ending
  // of the pack's stemming lexicon ("stroz" -> "stroza", "noteflow" ->
  // "noteflowie"), or the same Polish light-stem key ("plyta" -> "plyty",
  // "kaucje" -> "kaucji"). Alias tokens shorter than `min_chars` code points
  // ("EP", "ten", "z") stay exact; aliases with punctuation ("stroz.py",
  // "c++", "note-app") and intentional prefixes keep their exact path.
  // Context gates and negative contexts apply unchanged. `norm` must
  // outlive this index.
  void enable_inflection(const kb::Normalizer& norm, const kb::Pack& pack, std::size_t min_chars = 4);

 private:
  bool inflected_equal(std::string_view token, const AliasTerm& at, std::size_t k) const;
  std::vector<AliasTerm> terms_;
  int context_window_tokens_ = 30;
  const kb::Normalizer* norm_ = nullptr;      // set by enable_inflection
  std::vector<std::string> endings_;          // folded inflectional endings
};

// Shared by scan's from_pack and score's explicit profile index.
int alias_context_window_tokens(const kb::Pack& pack);

// Builds the flat SelfProfile.terms/.projects JSON (see catalog.h's
// SelfProfile doc comment) from profiles/self.json, the pack's principle
// phrasings (R2), ProfileConfig.extra_terms and, when cfg.repo is set, a
// light best-effort scan of file stems under it ("path" class terms). Shared
// by Catalog::build_profile (profile.cpp) and AliasIndex::from_pack, so
// scan() has usable mentions even before build_profile() is ever called.
Json flatten_self_profile(const kb::Pack& pack, const kb::Normalizer& norm, const ProfileConfig& cfg);

// Folds `text` and searches for a version string near an alias mention
// (self.json-style version_pattern is not present in this pack revision, so
// a fixed heuristic regex-free scan is used: a token matching
// [0-9]+(\.[0-9]+){1,2} within `window_chars` folded-text bytes of a non-trap
// kind="alias" hit). Principle probes and rejected aliases cannot bind versions.
std::vector<Mention> find_version_mentions(std::string_view text, const std::vector<Mention>& alias_hits,
                                           int window_chars);

// ── Semantic profile channels (semantic.cpp) ───────────────────────────
// Offline vector-space evidence between a unit and a *profile* (a weighted
// centroid of confidently identified units plus the self-profile terms).
// Two independent spaces are kept so every contribution stays explainable
// per channel: (1) sublinear-tf x idf over the sketch's stemmed terms, and
// (2) the same over character n-grams of those terms (robust to inflection,
// derivation and compounds that whole-word matching cannot see). Vectors
// are derived from the sketch (top-K terms with exact tf) on demand and
// never retained per unit, so memory is O(vocabulary), not O(units x
// n-grams). Everything is deterministic: ids are assigned in unit order,
// sums run in a fixed order, callers round the results.
struct SemanticProfile {
  std::vector<double> word;  // unnormalised weighted sum of L2-normalised unit vectors
  std::vector<double> gram;
  double word_sq = 0.0, gram_sq = 0.0;  // squared L2 norms (after finish())
  double mass = 0.0;                    // sum of unit weights added
  int units = 0;                        // units added (excludes term pseudo-documents)
};

struct ChannelCosine {
  double word = 0.0, gram = 0.0;
};

class SemanticSpace {
 public:
  // `sketches` in a stable order; index i is the unit index used below.
  explicit SemanticSpace(const std::vector<const Sketch*>& sketches, int ngram = 4);
  std::size_t size() const noexcept { return sketches_.size(); }
  SemanticProfile new_profile() const;
  // profile += weight * v_unit (v_unit L2-normalised in each space).
  void add_unit(SemanticProfile& p, std::size_t unit, double weight) const;
  // profile += weight * v_terms, a pseudo-document made of (stemmed key,
  // weight) pairs (the self-profile). Keys that no unit contains are ignored.
  void add_terms(SemanticProfile& p, const std::vector<std::pair<std::string, double>>& terms, double weight) const;
  void finish(SemanticProfile& p) const;
  // cos(v_unit, profile). `own_weight` > 0 removes the unit's own
  // contribution first (leave-one-out) so a seed cannot vouch for itself.
  ChannelCosine cosine(std::size_t unit, const SemanticProfile& p, double own_weight = 0.0) const;
  // cos(v_a, v_b) of two units (both spaces averaged), used to corroborate
  // continuity: temporal proximity alone must never make a unit relevant.
  double similarity(std::size_t a, std::size_t b) const;
  // Number of distinct word terms / n-grams (diagnostics).
  std::size_t word_vocabulary() const noexcept { return word_idf_.size(); }
  std::size_t gram_vocabulary() const noexcept { return gram_idf_.size(); }

 private:
  struct UnitVec {
    std::vector<std::pair<std::uint32_t, double>> word, gram;
  };
  UnitVec unit_vec(std::size_t unit) const;
  void grams_of(std::string_view term, std::vector<std::uint64_t>& out) const;
  std::vector<const Sketch*> sketches_;
  int ngram_ = 4;
  std::unordered_map<std::string, std::uint32_t> word_ids_;
  std::unordered_map<std::uint64_t, std::uint32_t> gram_ids_;
  std::vector<double> word_idf_, gram_idf_;
};

// ── Linking pass (links.cpp) ────────────────────────────────────────────
// Everything the linker needs about a unit; no raw bytes are ever read.
struct LinkUnit {
  const Sketch* sketch = nullptr;
  std::string platform;
  std::string project;         // project_ext_id ("" = none)
  std::optional<double> time;  // epoch seconds when the unit's date parses
};
struct LinkParams {
  double min_jaccard = 0.4;       // policy: link_minhash_jaccard
  int shared_rare_min = 3;        // policy: link_shared_rare_terms
  double session_hours = 2.0;     // policy: link_same_session_hours
  std::size_t exact_max_units = 2000;  // <= : exhaustive reference scan
  int lsh_rows = 2;               // lanes per LSH band (recall >= 99.6 % at J = 0.4 with 64 lanes)
  int lsh_max_bucket = 256;       // larger buckets are compared as a sliding window
  int session_max_neighbours = 32;  // nearest same_session neighbours kept per unit
  int project_max_clique = 32;    // larger project groups are aggregated, not materialised
};
struct Link {
  int i = 0, j = 0;  // unit indices, i < j
  std::string type;  // same_project | continuation | shared_rare | same_session
  double strength = 0.0;
};
struct LinkResult {
  std::vector<Link> links;                      // sorted by (i, j, type)
  std::vector<std::vector<int>> project_groups;  // same_project groups above project_max_clique
  Json stats = Json::object();
};
LinkResult build_links(const std::vector<LinkUnit>& units, const LinkParams& params);

// p-th percentile (0..100, nearest rank on the sorted copy); 0 for empty.
double percentile_of(std::vector<double> values, double pct);

// ── Sax-ish text extraction from one catalogued unit kind ───────────────
// Concatenates message/section texts of a conversation/project/memory
// element into one prose blob (for sketching) plus a separate code-fence
// blob (```...``` bodies), and returns simple stats.
struct ExtractedText {
  std::string prose;
  std::string code;
  std::string lang_hint;  // "" unless the caller already knows (unused here)
  // Populated for kind == chatgpt|claude|claude_projects (0/"" otherwise):
  // avoids a second walk_chatgpt/walk_claude pass in scan.cpp.
  int n_msgs = 0;
  int n_forks = 0;
  std::string title;
  std::string date;   // creation date, normalised ISO ("" unknown)
  std::string head;   // first user message, clipped to ~200 code points
  std::vector<std::string> attachments;
};
ExtractedText extract_text(const Json& element, std::string_view kind);

// ── Streaming JSON array scanner with byte offsets ──────────────────────
// Same bracket/string/escape state machine as loom::JsonArrayStreamer
// (src/import/importer_core.cpp), plus the absolute [begin,end) byte range
// of each flushed element in the ORIGINAL byte stream (needed for locators
// and for re-reading one element later without a full rescan). Bounded
// memory: only the current element's bytes are buffered, never the whole
// member. Not shared code with the import area (see catalog.h ownership
// map) because JsonArrayStreamer does not carry offsets and importer.h is
// out of this area's ownership; the state machine itself is intentionally
// identical so scan() sees exactly what the importer would see.
class OffsetArrayScanner {
 public:
  // Returns false when `on_element` asked to stop, or once the closing ']'
  // of the top level array is seen (`finished()` becomes true either way).
  // `on_element(element_bytes, begin, end)` -> false to stop early.
  using ElementFn = std::function<bool(std::string_view element, std::int64_t begin, std::int64_t end)>;
  void feed(std::string_view chunk, const ElementFn& on_element);
  bool not_array() const noexcept { return not_array_; }
  bool finished() const noexcept { return done_; }
  bool stopped() const noexcept { return stopped_; }

 private:
  std::string buf_;
  int depth_ = 0;
  bool started_ = false;
  bool in_string_ = false;
  bool escape_ = false;
  bool done_ = false;
  bool not_array_ = false;
  bool have_start_ = false;
  bool stopped_ = false;
  std::int64_t abs_pos_ = 0;
  std::int64_t elem_start_ = 0;
};

// ── Zip streaming (miniz), never extract-to-temp ────────────────────────
struct ZipEntry {
  std::string name;
  unsigned index = 0;
  std::int64_t uncompressed_size = 0;
  bool is_dir = false;
};
// Central directory only (milliseconds even for huge archives).
Result<std::vector<ZipEntry>> list_zip_entries(const std::filesystem::path& zip_path);
// Streams member `index` of `zip_path` through `on_chunk` in <= kReadChunk
// pieces via miniz's pull iterator (mz_zip_reader_extract_iter_*): the
// member is inflated directly into a small caller buffer, never written to a
// temp file and never held whole in memory.
Status stream_zip_member(const std::filesystem::path& zip_path, unsigned index,
                         const std::function<void(std::string_view)>& on_chunk);

}  // namespace loom::catalog::internal
