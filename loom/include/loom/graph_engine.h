// loom/graph_engine.h — port of engine/graph_engine.py.    [OWNER: wave 2 semantic/graph]
//
// Materialises entities / topics / relations from message analysis into the
// nodes + links tables. Behaviour to preserve (per analysed message `mid`):
//   on_message(data) (bus handler for message:created, data {"id","text",
//   "conv_id","role"}): skip when inactive or len(text) < 10; analysis =
//   llm->analyse(text) if llm && llm->enabled() else the regex unified dict;
//   then the same writes as ingest_analysis() PLUS:
//     * always link(mid -> conv_id, "part_of", 0.3) when conv_id non-empty
//       (ingest_analysis does this too);
//     * update_msg(mid, metadata={"semantic": {source, summary, sentiment,
//       entity_count, topic_count}}) — REPLACES metadata (Python quirk kept),
//       then mark_analysed(mid, analysis);
//     * emit graph:changed {"conv_id", "trigger": mid} when anything changed.
//   ingest_analysis(msg_id, conv_id, analysis):
//     entities: name (len >= 2), kind (default "entity"), relevance (default
//       0.5, skip < 0.3) -> get_or_create_node(name, kind) + link(msg ->
//       node, "mentions", relevance)
//     topics: dict {"label","confidence"} or plain string (confidence 0.5),
//       skip empty / < 0.3 -> get_or_create_node(label.lower(), "topic") +
//       link(msg -> node, "tagged_with", confidence)
//     relations: subject/object must both resolve via find_node(label) (any
//       kind) -> link(src -> dst, predicate or "related", 0.7)
//     conv edge (part_of 0.3); emit graph:changed if changed; returns changed.
//     Errors are logged and yield false (Python never raised).
//   reindex_conversation: on_message() for every message (include_all=True);
//   reindex_all: every conversation (list_convs(limit=9999)).
// Loom additions: unknown predicates are registered in the RelationRegistry
// (when given) so relation types stay discoverable data.
#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include "loom/event_bus.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Database;
class SemanticLLM;
class SemanticAnalyzer;
class RelationRegistry;

class GraphEngine {
 public:
  GraphEngine(Database& db, EventBus& bus, const SemanticAnalyzer& regex, SemanticLLM* llm = nullptr,
              RelationRegistry* relations = nullptr);
  ~GraphEngine();
  GraphEngine(const GraphEngine&) = delete;
  GraphEngine& operator=(const GraphEngine&) = delete;

  // Python subscribed in __init__; Loom subscribes here (Runtime calls it).
  void start();
  void stop();
  bool active() const noexcept { return active_.load(); }

  void on_message(const Json& data);
  Status on_message_checked(const Json& data, const Json& overrides = Json::object());
  bool ingest_analysis(std::string_view msg_id, std::string_view conv_id, const Json& analysis);
  Result<bool> ingest_analysis_checked(std::string_view msg_id, std::string_view conv_id, const Json& analysis,
                                       const Json& overrides = Json::object());
  Result<Json> profile_inspection(const Json& overrides = Json::object()) const;
  int reindex_conversation(std::string_view conv_id);
  int reindex_all();

 private:
  Database& db_;
  EventBus& bus_;
  const SemanticAnalyzer& regex_;
  SemanticLLM* llm_;
  RelationRegistry* relations_;
  std::atomic<bool> active_{false};
  ScopedSubscription sub_;
};

}  // namespace loom
