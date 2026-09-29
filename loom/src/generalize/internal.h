// Internal helpers of the generalize area (not installed). Everything here is
// deterministic: containers are ordered, ties are broken by id.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loom/generalize.h"
#include "loom/kb.h"
#include "loom/model.h"
#include "loom/util/json.h"

namespace loom::generalize::detail {

// ── dates ───────────────────────────────────────────────────────────
// First 10 chars ("YYYY-MM-DD") of an ISO date ("" stays "").
std::string day(std::string_view iso);
// Days between two ISO dates (b - a); 0 when either is empty/invalid.
long days_between(std::string_view a, std::string_view b);

// ── index over an Evidence ──────────────────────────────────────────
struct Index {
  explicit Index(const Evidence& ev);
  const Evidence& ev;
  std::map<std::string, const model::Entity*, std::less<>> entity;
  std::map<std::string, const model::Observation*, std::less<>> obs;
  std::map<std::string, const model::Claim*, std::less<>> claim;
  std::map<std::string, std::vector<const model::Claim*>, std::less<>> by_subject;   // sorted by id
  std::map<std::string, std::vector<const model::Observation*>, std::less<>> unit_obs;  // by ordinal, id
  std::map<std::string, std::set<std::string>, std::less<>> subject_units;          // entity -> unit ids
  std::map<std::string, const model::Decision*, std::less<>> decision;
  std::string corpus_end;  // max observation / decision date (day)

  // Date of a claim: valid_from, else the earliest support observation date.
  std::string claim_date(const model::Claim& c) const;
  // Unit ids a claim is supported from.
  std::vector<std::string> claim_units(const model::Claim& c) const;
  // Observations of the decision's `decides` claim (support), sorted.
  std::vector<const model::Observation*> decision_obs(const model::Decision& d) const;
  const model::Claim* find_claim(std::string_view id) const;
  const model::Entity* find_entity(std::string_view id) const;
  const model::Observation* find_obs(std::string_view id) const;
};

// Items dated <= cut (before=true; undated items are kept) or > cut.
Evidence slice(const Evidence& ev, std::string_view cut, bool before);

// ── text ────────────────────────────────────────────────────────────
// Content terms of a text: the normalizer's phrase-key tokens (stemmed,
// folded, stop words removed, glossary-mapped), deduplicated, sorted.
std::vector<std::string> terms(const kb::Normalizer& norm, std::string_view text);
// A cue phrase ("zasad*", "must not", "≠") found in a text (word-aware,
// diacritic-folded; a trailing '*' is a word prefix).
bool cue_hit(const kb::Normalizer& norm, std::string_view folded_text, std::string_view phrase);
// Sum of the weights of the cue phrases of a class list found in the text.
// `classes` is a JSON object {"phrases":[{"p","w"}]} (lexicons/cues.json shape).
double cue_score(const kb::Normalizer& norm, std::string_view text, const Json& cls);
// lexicons/cues.json class by name (null when absent).
const Json& cue_class(const kb::Pack& pack, std::string_view name);
// A cue class from the pack when present, else the built-in default of this
// area (documented in generalize.h; promotion into the pack is a lead edit).
Json cue_class_or_default(const kb::Pack& pack, std::string_view name);

// A text folded and tokenized once: the input of cue matching. Cue matching
// of one text against several classes (or of one observation for several
// subjects) must not repeat this work.
struct FoldedText {
  std::string folded;
  std::vector<std::string> toks;  // tokens of the folded text
};
FoldedText fold_text(const kb::Normalizer& norm, std::string_view text);

// A cue class (lexicons/cues.json shape) prepared once: every phrase folded
// and tokenized. score() is exactly cue_score() (same phrases, same order of
// summation), without re-normalizing the phrases for every text.
class PreparedCues {
 public:
  PreparedCues() = default;
  PreparedCues(const kb::Normalizer& norm, const Json& cls);
  double score(const FoldedText& t) const;
  // Adds the indexes of the phrases hitting `t` to `seen` (distinct-phrase evidence).
  void hit_phrases(const FoldedText& t, std::set<std::size_t>& seen) const;
  double weight_of(std::size_t phrase) const { return phrases_[phrase].w; }
  bool empty() const noexcept { return phrases_.empty(); }

 private:
  struct Phrase {
    std::string text;                // folded, without the trailing '*'
    std::vector<std::string> toks;   // tokens of `text`
    bool star = false;               // the last token is a prefix
    bool symbolic = false;           // no word character: substring match
    double w = 1.0;
  };
  bool hits(const Phrase& p, const FoldedText& t) const;
  std::vector<Phrase> phrases_;
};

// Document-frequency weights over a corpus of term sets: w(t) = log(1 + N/df).
class TermWeights {
 public:
  void add(const std::vector<std::string>& doc);
  double w(const std::string& t) const;
  std::size_t docs() const noexcept { return n_; }
  // Read access for callers that need the raw counts (df, N).
  const std::map<std::string, int, std::less<>>& df() const noexcept { return df_; }

 private:
  std::map<std::string, int, std::less<>> df_;
  std::size_t n_ = 0;
};

// Weighted Jaccard of two sorted term sets. `W` is anything with
// `double w(const std::string&) const` (TermWeights or an overlay of it).
template <class W>
double wjaccard(const std::vector<std::string>& a, const std::vector<std::string>& b, const W& w) {
  double inter = 0, uni = 0;
  std::size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    if (j == b.size() || (i < a.size() && a[i] < b[j])) {
      uni += w.w(a[i++]);
    } else if (i == a.size() || b[j] < a[i]) {
      uni += w.w(b[j++]);
    } else {
      double x = w.w(a[i]);
      inter += x;
      uni += x;
      ++i;
      ++j;
    }
  }
  return uni > 0 ? inter / uni : 0.0;
}
// Weighted overlap coefficient (|a∩b| / min(|a|,|b|), weighted).
template <class W>
double woverlap(const std::vector<std::string>& a, const std::vector<std::string>& b, const W& w) {
  double inter = 0, sa = 0, sb = 0;
  for (const auto& t : a) sa += w.w(t);
  for (const auto& t : b) sb += w.w(t);
  std::size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i] < b[j]) {
      ++i;
    } else if (b[j] < a[i]) {
      ++j;
    } else {
      inter += w.w(a[i]);
      ++i;
      ++j;
    }
  }
  double m = std::min(sa, sb);
  return m > 0 ? inter / m : 0.0;
}
std::vector<std::string> set_union(const std::vector<std::string>& a, const std::vector<std::string>& b);
std::vector<std::string> set_minus(const std::vector<std::string>& a, const std::vector<std::string>& b);

// ── policy ──────────────────────────────────────────────────────────
// thresholds.json <section>.<key>, else `fallback`.
double threshold(const kb::Pack& pack, std::string_view section, std::string_view key, double fallback);
// Isotonic map of policy/calibration.json for an evidence class (identity when absent).
double calibrate(const kb::Pack& pack, model::EvidenceClass e, double raw);
double clamp01(double v);

// ── claims ──────────────────────────────────────────────────────────
// A claim may be used as a premise / as evidence of a slot by the engine:
// not absent, not extrapolated, not rejected.
bool usable(const model::Claim& c);
// Observed-grade evidence (observed | derived | user).
bool observed_grade(const model::Claim& c);
// Transferred (derivation names a morphism).
bool transferred(const model::Claim& c);
// Comparable key of a claim's value: the object entity's canonical key (or
// id), else the normalizer phrase key of a string value, else canonical JSON.
std::string value_key(const kb::Normalizer& norm, const Index& ix, const model::Claim& c);
std::string value_key(const kb::Normalizer& norm, const Json& v);
// Display text of a claim's value.
std::string value_text(const Index& ix, const model::Claim& c);
// Finalises id (content key) and returns the claim.
model::Claim finish(model::Claim c);
// Sorts by id and removes duplicate ids (the first occurrence wins).
void dedupe(std::vector<model::Claim>& v);

// ── expressions (conditions, values, Expected Properties) ───────────
// Everything an expression can refer to while a rule / constraint / morphism
// is evaluated. Bound references ({"ref":...}) need only `ix` and `norm`, so
// evaluate_property() can re-check a stored property without the pack.
struct ExprCtx {
  const Index& ix;
  const kb::Normalizer& norm;
  const kb::Pack* pack = nullptr;            // null: bound refs only
  const Match* inst = nullptr;               // the instance the rule runs on
  const std::vector<Match>* matches = nullptr;
  std::string subject;                       // entity id
  Json value;                                // $value (null: none)
  std::string self_claim;                    // claim being checked (excluded from not_contradicted_by)
  const std::set<std::string>* active_principles = nullptr;
  std::vector<std::string>* used_claims = nullptr;  // premises collected while resolving refs
};

// Replaces pack references ("$slot:x", "$rejected_values", "$git_max_version",
// "$temporal:versions", "$analog:x", "$corpus_end_date", class/enum names of
// in_class / in_enum) by bound references ({"ref":"claims",...},
// {"members":[...]}) that evaluate against the evidence alone. "$value"
// stays. Unsupported references are kept verbatim (they evaluate pending).
Json bind(const Json& expr, const ExprCtx& ctx);
// Resolves an argument to a JSON array of values ([] when nothing).
Json resolve_list(const Json& arg, const ExprCtx& ctx);
// Three-valued predicate evaluation (holds | violated | pending); unknown
// operators evaluate pending (capability honesty, I9).
model::CheckState eval_pred(const Json& expr, const ExprCtx& ctx);
// Rule/morphism condition ("when"): true only when it holds.
bool eval_cond(const Json& expr, const ExprCtx& ctx);
// Value expression; nullopt + reason when the value op is not available.
struct ValueResult {
  Json values = Json::array();                 // one or more values
  std::vector<model::Alternative> alternatives;  // runner-ups
  std::string unsupported;                     // non-empty: why the op could not run
};
ValueResult eval_value(const Json& expr, const ExprCtx& ctx);

// Claims of an instance slot (non-absent, usable; slot = domain kind id or
// "<facet>.<kind>").
std::vector<const model::Claim*> slot_claims(const Match& m, std::string_view slot, const Index& ix);
// Domain kind of an instance slot (project kind / facet), null when none.
const model::DomainKind* slot_kind(const kb::Pack& pack, const Match& m, std::string_view slot,
                                   model::ProjectKind* pk_buf, model::Facet* facet_buf);

// Puts a produced claim into its instance slot (replacing an absent value
// unless the claim is extrapolated).
void attach(Match& m, const model::Claim& c, const std::string& slot, std::optional<model::Role> role);

// Principles considered active for principle_active / rule bases: visible
// seeds (after the PriorFilter) that are not rejected, plus every principle
// of the evidence.
std::set<std::string> active_principles(const kb::Pack& pack, const Evidence& ev, const model::PriorFilter& priors);

// The built-in pack (for functions whose contract has no pack parameter:
// its normalizer tables). Null when it cannot be loaded.
std::shared_ptr<const kb::Pack> builtin_pack();

// sha256 over canonical JSON (the "output" hash of the stage).
std::string hash_json(const Json& j);

}  // namespace loom::generalize::detail
