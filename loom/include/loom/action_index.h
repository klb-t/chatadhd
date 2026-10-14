// loom/action_index.h — the incremental index behind the detection of repeated sequences and of the classes an action is used on
// (R55; P12 3). Replaces rereading the run log after every command.
//
// The index is a fold over the stream of run checkpoints the run store already keeps (the event log): each completed run of a content
// action is folded in once, when it is first seen, and the fold keeps only what detection needs -
//   - per conversation, the last `max_length` items (the chain that ends at the newest run is read from there);
//   - per class, the last `chains_per_class` occurrences of a chain (a pipeline: each run worked on what the one before made),
//     counted by signature - so "how many times has this chain been run" is one lookup;
//   - per action, the classes it has been used on, in order of first use (the group detection reads that).
// A snapshot of this state is appended to the same event log every `snapshot_every` folded runs; a restart loads the latest snapshot and
// folds only the checkpoints written after it. Every refresh reads only the checkpoints written since the previous one, so the cost of
// detecting after a command does not grow with the length of the log. The index lives in the data directory it is part of (one per user;
// a server with accounts has one data directory per account).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "loom/action_run.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::actions {

// What detection needs of one completed run.
struct SequenceItem {
  std::string run_id, action, cls, element_id, conv, message_id, created, updated;
  std::set<std::string> produced;  // ids of what the run made: result nodes and posted messages
  Json to_json() const;
  static SequenceItem from_json(const Json& j);
};

// The same chain, each run on what the one before made, within `gap_seconds`: list[from, from+len) is a pipeline of one class.
bool is_pipeline(const std::vector<SequenceItem>& list, std::size_t from, std::size_t len, std::int64_t gap_seconds);
// "<class>|<action>+<action>..."
std::string chain_signature(const std::vector<SequenceItem>& list, std::size_t from, std::size_t len);

struct IndexSettings {
  std::size_t max_len = 3;             // emergent.sequences.max_length
  std::int64_t gap_seconds = 900;      // emergent.sequences.gap_seconds
  std::size_t chains_per_class = 200;  // emergent.sequences.window_runs: chain occurrences kept per class
  std::size_t convs = 64;              // emergent.sequences.index.convs: conversations whose tail is kept
  std::size_t snapshot_every = 100;    // emergent.sequences.index.snapshot_every: folded runs between snapshots
  std::size_t sources = 16;            // emergent.sequences.index.sources: messages kept per action (what a group offer is derived from)
  static IndexSettings from_routing(const Json& sequences);
};

class SequenceIndex {
 public:
  explicit SequenceIndex(RunStore& store) : store_(store) {}

  // Brings the index up to date: loads the latest snapshot once, then folds the checkpoints written since the last call. `is_content`
  // says which actions take part (the interpreted composites that work on content).
  Status refresh(const std::function<bool(const std::string&)>& is_content, const IndexSettings& settings);

  // The last items of a conversation, oldest first.
  std::vector<SequenceItem> tail(const std::string& conv) const;
  // How many occurrences of the chain the index holds (the per-class rings).
  std::size_t count(const std::string& signature) const;
  // The classes an action has been used on, in order of first use; and the messages of its latest runs, oldest first.
  std::vector<std::string> classes_of(const std::string& action) const;
  std::vector<std::string> messages_of(const std::string& action) const;

  // The snapshot document, and what the last refreshes did (tests, diagnostics).
  Json state() const;
  struct Stats {
    std::size_t refreshes = 0, checkpoints_read = 0, runs_folded = 0, snapshots = 0, loaded_from_snapshot = 0;
  };
  Stats stats() const;

 private:
  struct Occurrence { std::string signature; };
  struct Conv { std::int64_t touched = 0; std::vector<SequenceItem> tail; };
  void fold(const SequenceItem& item, std::int64_t seq, const IndexSettings& s);
  Json state_unlocked() const;
  void load_unlocked(const Json& snapshot);

  RunStore& store_;
  mutable std::mutex mu_;
  bool loaded_ = false;
  std::int64_t last_seq_ = 0;
  std::size_t since_snapshot_ = 0;
  std::vector<std::string> processed_;                       // the last completed run ids folded (a checkpoint may repeat)
  std::map<std::string, Conv> convs_;
  std::map<std::string, std::vector<Occurrence>> classes_;   // class -> chain occurrences, oldest first
  std::map<std::string, std::size_t> counts_;                // signature -> occurrences held
  struct Use { std::vector<std::string> classes, messages; };
  std::map<std::string, Use> actions_;                       // action -> where it was used
  Stats stats_;
};

}  // namespace loom::actions
