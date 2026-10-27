// loom/advisory.h — the one shape of a non-blocking advisory (R58; K3 design 1i).
//
//   {code, level, text{pl,en}, params}            the same four keys as the `reason` of src/views/view_engine.cpp
//   + ask{key, question{pl,en}}                   only when the catalogue entry has a question: "is this intended?" and the
//                                                 preference key under which "do not ask next time" is remembered
//
// An advisory never blocks: the caller gets its answer and the advisories next to it. The texts are data (a catalogue of
// code -> {level, text{pl,en}, question{pl,en}}), `{name}` in a text is replaced by the param of that name. A code that
// the catalogue does not know still produces an advisory (the text is the code), so nothing is lost silently.
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::advisory {

class Catalogue {
 public:
  Catalogue() = default;
  explicit Catalogue(Json entries) : entries_(entries.is_object() ? std::move(entries) : Json::object()) {}

  bool has(const std::string& code) const { return entries_.contains(code); }
  std::vector<std::string> codes() const;
  const Json& entries() const { return entries_; }

  // {pl,en} text of `code` with the params substituted.
  Json say(const std::string& code, const Json& params = Json::object()) const;
  // The advisory. `level` empty = the level of the catalogue entry ("info" when there is none).
  Json reason(const std::string& code, const std::string& level = "", const Json& params = Json::object()) const;

 private:
  Json entries_ = Json::object();
};

// Collects the advisories of one operation. An advisory identical to one already collected (same code and params) is kept once.
class Collector {
 public:
  explicit Collector(const Catalogue* catalogue = nullptr) : catalogue_(catalogue) {}

  void add(const std::string& code, const Json& params = Json::object(), const std::string& level = "");
  void append(const std::vector<Json>& items);
  const std::vector<Json>& items() const { return items_; }
  bool has(const std::string& code) const { return count(code) > 0; }
  std::size_t count(const std::string& code) const;
  const Catalogue* catalogue() const { return catalogue_; }

 private:
  const Catalogue* catalogue_;
  std::vector<Json> items_;
};


// ── Remembered answers: `advisory_ack` (G3, 2026-10-27; schema shared with the window system W4) ──
//
// An engine that raises an advisory about a CONFIGURATION gives it a fingerprint of that configuration and lets annotate() decide
// whether to ask. The record then carries, next to {code, level, text, params}:
//   ask: {key, question{pl,en}, fingerprint, choices: [{id, label{pl,en}}], asking: bool, state: "ask" | "acknowledged" | "muted", ack?}
//   asking false = the person said "don't ask" for this code + fingerprint (state acknowledged, ack = the entry) or the policy never
//   asks for this code (state muted). The record is listed either way: nothing is hidden from the data; a client decides what to show.
//   An answer whose choice is in the policy's `transient_choices` (preset ["keep"]: "keep it this time") is recorded and shown (ask.ack,
//   ask.answered true) but the advisory keeps asking (G3b, 2026-10-27; reversal: an empty transient_choices list = every answer silences).
//   (`key` stays "advisory.<code>.ask_every": the per-code policy in the store, policy.codes[<code>].)
//
// Store "loom.advisory_ack/1" = <data_dir>/advisory_ack.json (level `person`: it travels with the data directory):
//   {schema, acks: {"<code>|<fingerprint>": {code, fingerprint, choice, ack_at, context?}},
//    policy: {ask?: bool, remember_days?: number | null, codes?: {<code>: {ask?, remember_days?}}}}
// Policy layers: preset (engine data, e.g. graph_views view_rules `advisory_policy`) <- store policy <- store policy.codes[code]; a null
// value clears a key back to the layer below. ask false = never ask (the "how often" knob turned to never); remember_days null = an
// answer holds forever, a number = it expires after that many days and the advisory asks again (0 = never remembered: always ask).
// Open values, no closed list. Reversal: forget(code, fingerprint), forget_all(), set_policy({key: null}).
// A missing store reads as empty; an unreadable one reads as empty with an entry in issues() — never an error of the caller.

inline constexpr const char* kAckStoreFile = "advisory_ack.json";

// Canonical fingerprint of a configuration: sha256 over the JSON with object keys sorted (insertion order never matters), 16 hex.
std::string fingerprint(const Json& configuration);

// The effective policy for `code`: preset <- store policy <- store policy.codes[code].
Json effective_policy(const Json& preset, const Json& store_policy, const std::string& code);

class AckStore {
 public:
  // Reads <data_dir>/advisory_ack.json. Empty data_dir = memory only (writes are kept in memory).
  static AckStore open(const std::filesystem::path& data_dir);

  Json find(const std::string& code, const std::string& fp) const;  // the ack entry or null
  // Remember the answer (choice: an open string, e.g. "dont_ask" or "keep"); `now` empty = now. Written atomically.
  Status acknowledge(const std::string& code, const std::string& fp, const std::string& choice, const Json& context = Json::object(), std::string now = "");
  Result<int> forget(const std::string& code, const std::string& fp);  // how many were removed (0 or 1)
  Result<int> forget_all();
  Status set_policy(const Json& patch);  // merge; null removes the key; `codes` merges per code

  // Fill record.ask (fingerprint, choices, asking, state, ack) from the answers and the policy. The record keeps every other key.
  Json annotate(Json record, const std::string& fp, const Json& preset_policy, const std::string& now = "") const;

  const Json& document() const { return doc_; }
  const Json& issues() const { return issues_; }

 private:
  Status save();
  std::filesystem::path path_;
  Json doc_ = Json::object();
  Json issues_ = Json::array();
};

}  // namespace loom::advisory
