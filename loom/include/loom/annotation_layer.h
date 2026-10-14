// loom/annotation_layer.h — the shared segment-annotation layer as a contract (P23; docs/architecture/PRZEGLAD_STRAZNIK_SRODOWISKA_2026-10-15.md §2).
//
// An annotation is the claim of a PRODUCER that a segment of a source (a message, a range of codepoints) has a CATEGORY, with a computed confidence,
// the cost of making it and a trace of the method that made it. It never changes the source. It can be replaced by a newer version of the same claim
// (state superseded) or taken back (state retracted); nothing is deleted.
//
//   producer  does not know its consumers; a consumer does not know its producers: it knows categories, confidences, qualifiers.
//   storage   the table of message annotations (db.h); the contract lives in the metadata of each row:
//               schema, category, producer, method {name, version}, confidence, cost {calls, input_bytes, output_bytes, receipt},
//               qualifier, lifecycle (active | superseded | retracted), superseded_by, plus category data (e.g. `thread` label).
//             The database state stays active | retracted: a superseded annotation is stored as retracted with lifecycle "superseded", so every older
//             reader sees it as not in force, and the layer can tell "replaced by a better one" from "taken back".
//   many      any number of annotations of any categories may share a segment (a sentence can be a correction and a digression).
//   conflict  two active annotations of one category on overlapping ranges: the user wins, then the higher confidence, then the newer method version;
//             the loser stays as superseded. A signal (class `signal`: correction, instruction, preference, exception ...) protects the segment it covers
//             from being dropped as noise (class `droppable`): see list() — "signal beats noise" is data (annotations.conflicts), not a consumer's rule.
//   positive  categories have a polarity (acceptance, preference are positive signals); the layer does not care.
// All of it (classes, polarity, thresholds) is the `annotations` section of the runtime profile `guardian`; a user overlay changes any of it.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "loom/db.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::annotations {

inline constexpr const char* kSchema = "loom.segment_annotation/2";

// The `annotations` section of the profile guardian (values, overlays applied).
struct Policy {
  Json p = Json::object();
  static Result<Policy> load(const std::filesystem::path& data_root);
  std::string label_prefix() const;
  std::string node_kind() const;
  // The class of a category (signal | droppable | structure | ...); "" when the data does not say.
  std::string class_of(const std::string& category) const;
  // positive | negative | neutral
  std::string polarity_of(const std::string& category) const;
};

struct Claim {
  std::string message_id, category;
  std::int64_t start_char = 0;
  std::int64_t end_char = -1;  // -1 = the whole message
  std::string producer, method, method_version;
  double confidence = 1.0;
  std::string origin = "recorded";  // recorded | model | user
  std::string qualifier;            // "" | negative | hypothetical | quoted | ... : what kind of statement the segment is
  Json cost = Json::object();       // {calls, input_bytes, output_bytes, receipt}; heuristics cost nothing: {}
  Json extra = Json::object();      // category data merged into the metadata (never over the contract fields)
};

enum class Outcome {
  Created,         // new, in force
  Duplicate,       // the same claim of the same producer and method version is already in force
  Replaced,        // new, in force; it superseded older claims (`superseded` lists them)
  Lost,            // new, stored as superseded: a stronger claim stands (`superseded_by`)
  Suppressed       // the user took this claim back before; it is not written again
};

struct Written {
  std::string id;
  Outcome outcome = Outcome::Created;
  std::vector<std::string> superseded;
  std::string superseded_by;
};

Result<Written> write(Database& db, const Policy& policy, const Claim& claim);

// effective | superseded | retracted
std::string state_of(const MessageAnnotation& a);

struct ListOptions {
  bool include_inactive = false;  // superseded and retracted too (history)
  bool shield = true;             // drop noise a signal covers
  double min_confidence = 0.0;
};

// The annotations of a message as the contract reads them: [{id, category, class, polarity, producer, method, method_version, origin, confidence, qualifier,
// cost, start_char, end_char, excerpt, state, revision, shielded_by?, data}]. In force only unless include_inactive.
Result<Json> list(Database& db, const Policy& policy, const std::string& message_id, const ListOptions& opts = {});

// Supersede / restore one annotation (a user's wish: "this was right after all"). Both are revisions; nothing is lost.
Result<Json> supersede(Database& db, const std::string& id, const std::string& by);
Result<Json> restore(Database& db, const std::string& id);

// A receipt that a producer has read a message, kept even when it found nothing (so no consumer asks a model for the same reading again).
// Node `run:<message>:<producer>` of kind annotation_run, with the method version, the categories asked, the cost and the text hash read.
Status record_run(Database& db, const Policy& policy, const std::string& message_id, const std::string& producer, const std::string& method_version,
                  const std::vector<std::string>& categories, const Json& cost);
// The runs recorded for the message whose text is still the text that was read: [{producer, method_version, categories, cost, at}].
Result<Json> runs_of(Database& db, const Policy& policy, const std::string& message_id);

// The method version of a producer: a short hash over the data that decides its answers (a list of phrases, a prompt).
std::string method_version(const std::string& material);

}  // namespace loom::annotations
