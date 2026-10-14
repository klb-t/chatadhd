// Internal declarations shared by the guardian's translation units.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/guardian.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::guardian::detail {

inline constexpr const char* kLog = "loom.guardian";

struct Data {
  Json p;                      // runtime profile guardian (values), guardian_levels merged over levels.defs
  std::filesystem::path root;  // data directory
  Json fold;                   // tokens.fold
};

Result<Data> load_data(const Config& cfg, const std::filesystem::path& root);
// The text id in a language (English when the language has none), {{name}} substituted.
std::string say(const Data& d, const std::string& lang, const std::string& id, const Json& params = Json::object());
std::string scope_name(const Data& d, const std::string& lang, const std::string& scope);
// A setting over the profile's preset: the value of `section.setting` when it names a preset, else section.unknown / section.active.
std::string choose(const Config& cfg, const Json& section);
// A message that is the system's own traffic (commands, proposals, answers, the guardian's notices): not the user's content.
bool is_traffic(const Data& d, const Message& m);
// The language of a text: "pl" when Polish letters or more Polish than English markers, else "en".
std::string guess_lang(const Data& d, const std::string& text);

// ── shared annotation layer and the lossy levels (levels.cpp) ──────────────────────────────

struct Dropped {
  std::string category, annotation_id, producer, text;
  std::int64_t start = 0, end = 0;
};
// Writes the cheap categories (fillers, repetitions, retractions) as annotations of the message (once per producer).
Result<int> annotate_message(Database& db, const Data& d, const Message& m);
// (Re)builds the level nodes of one message from all active annotations of any producer. Returns {levels: {L1: {text, dropped: [...]}}}.
Result<Json> build_levels(Database& db, const Data& d, const Message& m);
// The category of every active annotation of the message, by producer: [{id, category, producer, origin, confidence, start, end}].
Result<Json> annotations_of(Database& db, const Data& d, const std::string& message_id);
std::string level_label(const std::string& message_id, const std::string& level);
// The text of the message at the level, from a fresh level node; nullopt when the level changes nothing or is stale.
std::optional<std::string> level_text(Database& db, const Data& d, const Message& m, const std::string& level);
std::string sha256_hex(const std::string& s);

// ── representation (representation.cpp) ──────────────────────────────────────────────────────

// Rebuilds the compiled representation node of the conversation from the promoted hypotheses. Returns the node (null when there is
// nothing to compile) as {id, instruction, statements, messages}.
Result<Json> rebuild_representation(Database& db, const Data& d, const std::string& conv_id, const std::string& include_hyp = std::string(), bool write = true);
// The spec the node stands for, with version and ancestry chosen against what the conversation has retained. {spec, bindings} or an error.
Result<Json> materialize(Database& db, const Data& d, const std::string& conv_id, const Json& node_metadata);
// The hypotheses of a conversation (nodes of the hypothesis kind).
Result<std::vector<Node>> hypotheses_of(Database& db, const Data& d, const std::string& conv_id);
// Preview: the compiled instruction text of the statements.
Result<std::string> instruction_of(const Json& statements, const std::vector<std::string>& message_ids, Database& db, const std::string& conv_id);

// Tokens of a statement/claim for overlap and polarity (folded, stop words removed).
std::vector<std::string> subject_tokens(const Data& d, const std::string& text);
bool negative(const Data& d, const std::string& text);
double overlap(const std::vector<std::string>& a, const std::vector<std::string>& b);

}  // namespace loom::guardian::detail
