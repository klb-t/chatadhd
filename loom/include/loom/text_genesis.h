// loom/text_genesis.h — text genesis in the graph (R102 G2, "geneza tekstu w grafie").
//
// What the code does today (2026-10-30):
//  * A SOURCE is one text with its provenance: a message version (its version group is the chain), a conversation
//    (every version group it holds), or any text a caller hands over with a SourceRef (kinds are strings: open).
//  * compute_chain() aligns consecutive versions (text_fragments.h, level and options from data) and derives EVENT
//    records: every op of an alignment whose kind the pack does not list as silent becomes one event. The event kind is
//    a data mapping from the op kind (presets: delete→removal, insert→addition, replace→replacement, move→move); an op
//    kind the mapping does not name becomes an event of that very name — nothing is dropped. Each event carries the
//    fragment (fingerprint), its successor (replacement/move), the version before and after, where (conversation,
//    message), when (the time of the version after) and who/what (the actor of the version after). Pure: writes nothing.
//  * lineage_of() follows one fragment of a version back through the chain to its origin: the first version, or
//    "new in version k" (an insert), through replacements and moves. Every fragment of every version has exactly one
//    lineage because an alignment covers every b unit exactly once.
//  * materialize() writes the chain into the graph (nodes + links tables), lazily — only what a caller asks for, and
//    idempotently: fragment nodes keyed by fingerprint (kind text_fragment; shared across versions AND conversations,
//    which is how the same sentence in another conversation gets joined), version nodes keyed by the source id, event
//    nodes keyed by a deterministic key. Links are upserts; event links retain every occurrence in metadata.events
//    (the singular fields remain a deterministic representative for older readers). Running it twice leaves the graph as after once.
//    Node kinds and edge kinds are data (pack `genesis`), listed in Options::names; new ones are added by data.
//  * query() answers questions read-only ("origin", "replaced", "missing", "events" are the ones this build knows;
//    an unknown question is an explicit error that names them). It computes live from the messages (deterministic,
//    no model, no write) and adds what the graph already holds (occurrences of the fingerprint in other
//    conversations). Nothing is written by a query.
// Not here: paste events and the fingerprint index over pastes (G3), the "Geneza tekstu" window (G4), significance
// of a removal (an optional separate rating, §4.4), automatic language detection.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "loom/db.h"
#include "loom/result.h"
#include "loom/text_fragments.h"
#include "loom/util/json.h"

namespace loom::text::genesis {

// Where a text came from. Every field is open data; `kind` names what the id is (message, conversation, paste, import …).
struct SourceRef {
  std::string id;
  std::string kind = "message";
  std::string conv_id;
  std::string group;           // version_group_id for a message version; the conversation for a conversation
  std::int64_t ordinal = 0;    // version_num / position in the chain (1-based as the DB numbers versions)
  std::string when;            // ISO timestamp of this version
  std::string role;            // "user" / "assistant" / … (open)
  std::string actor;           // who or what produced it: "user", a model name, a track pseudonym … (open)
  Json to_json() const;
  static SourceRef from_message(const Message& m);
};

struct VersionText {
  SourceRef source;
  std::string text;
};

struct Options {
  std::string level = "sentence";          // fragment level of the chain (preset genesis.level)
  std::string language;                    // "" = the pack's default_language
  AlignOptions align;                      // alignment presets (pack alignment)
  bool record_initial_additions = false;   // also an "addition" event for every fragment of the first version
  Json names;                              // pack genesis.{nodes,edges,events,silent_ops}: the kinds in effect
  static Result<Options> from_pack(const Json& pack);
  std::string node_kind(std::string_view which) const;    // "fragment" | "version" | "event" → the kind string in effect
  std::string edge_kind(std::string_view which) const;    // "occurrence" | "version_chain" | "about" | "in" | "from" | "successor" | <event kind>
  std::string event_kind(std::string_view op_kind) const; // op kind → event kind ("" = silent)
};

struct Occurrence {
  std::string fingerprint;
  std::size_t index = 0, begin = 0, end = 0;  // fragment index in the version; byte range in its text
  std::string text;                           // the fragment's core text
  Json to_json() const;
};

struct VersionFragments {
  SourceRef source;
  std::vector<Occurrence> fragments;
  Json to_json() const;
};

struct Event {
  std::string kind;        // event kind in effect (removal, addition, replacement, move, … open)
  std::string op_kind;     // the alignment op it came from
  std::string key;         // deterministic identity: kind|from.id|to.id|a_index|b_index
  std::string fragment;    // fingerprint of the fragment the event is about (a-side; b-side for an addition)
  std::string successor;   // fingerprint of the b-side fragment for replacement / move ("" otherwise)
  std::string text_before, text_after;
  SourceRef from, to;      // version before and after; `to` says where, when and who
  std::size_t a_index = kNone, b_index = kNone;
  double similarity = 1.0;
  Json inner = Json::array();  // inner word diff of a replacement (as EditOp::to_json)
  Json to_json() const;
};

struct Chain {
  std::vector<VersionFragments> versions;  // in chain order
  std::vector<Alignment> steps;            // steps[i] aligns versions[i] (a) with versions[i+1] (b); size = versions-1
  std::vector<Event> events;
  std::string level, language;
  std::vector<std::string> advisories;
  Json to_json() const;
};

// One step of a lineage: the fragment at (version, index), and how it got there from the previous version.
struct LineageStep {
  std::size_t version = 0, index = 0;
  std::string op_kind;      // "equal" | "replace" | "move" | … from the previous version; "origin" for the first step
  std::string event_key;    // the event that produced this step ("" for equal / origin)
  Json to_json() const;
};
struct Lineage {
  std::vector<LineageStep> steps;  // from the asked fragment back to its origin (last element)
  std::string origin;              // "first_version" | "new_in_version" (the fragment appeared by an insert)
  std::size_t origin_version = 0;  // chain index of the origin
  Json to_json() const;
};

// Pure: nothing is read from or written to the database.
Result<Chain> compute_chain(const Segmenter& seg, const std::vector<VersionText>& versions, const Options& opt);
Result<Lineage> lineage_of(const Chain& chain, std::size_t version, std::size_t index);

// Sources from the database (read-only).
Result<std::vector<VersionText>> versions_of_message(Database& db, std::string_view message_id);
Result<std::vector<VersionText>> versions_of_group(Database& db, std::string_view version_group_id);
// Every chain of a conversation: one per version group, a lone message is a chain of one.
Result<std::vector<std::vector<VersionText>>> chains_of_conversation(Database& db, std::string_view conv_id);

struct MaterializeSummary {
  std::size_t versions = 0, fragments = 0, events = 0;
  std::size_t nodes_created = 0, nodes_reused = 0, links_written = 0;
  std::vector<std::string> advisories;
  Json to_json() const;
};
// Writes the chain into the graph. Idempotent: a second call creates nothing new (nodes_created == 0).
Result<MaterializeSummary> materialize(Database& db, const Chain& chain, const Options& opt);

struct Query {
  std::string question;      // "origin" | "replaced" | "missing" | "events" (open: the pack lists what this build answers)
  std::string text;          // the sentence asked about (matched by fingerprint of its normalization, else by containment)
  std::string fingerprint;   // or its fingerprint directly
  std::string message_id;    // the version / chain to look in (any version of the group)
  std::string version_group_id;
  std::string conv_id;       // "origin"/"events" over every chain of a conversation when no message is named
  std::string from_id, to_id;  // "missing": version A and version B (defaults: the previous version and this one)
  std::size_t limit = 50;
};
// Read-only. The answer always says how it was computed ("computed": "live" and/or "graph").
Result<Json> query(Database& db, const Segmenter& seg, const Query& q, const Options& opt);
// The questions this build answers, with their PL/EN labels (from the pack).
Json known_questions(const Options& opt);

}  // namespace loom::text::genesis
