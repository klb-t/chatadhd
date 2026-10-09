// loom/graph_engine.h — port of engine/graph_engine.py.    [OWNER: wave 2 semantic/graph]
//
// Materialises semantic results using the existing graph_ingest runtime recipe.
// Checked ingestion is transactional and propagates storage failures. The
// begin/complete/fail lifecycle binds one source version and profile before
// analysis; interrupted/failed attempts require explicit requeue, never retry
// merely because the process reopened. A successful empty analysis is valid.
#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

#include "loom/analyzer_binding.h"
#include "loom/event_bus.h"
#include "loom/result.h"
#include "loom/runtime_profile.h"
#include "loom/util/json.h"

namespace loom {

class Database;
class SemanticLLM;
class SemanticAnalyzer;
class RelationRegistry;

// A process-local handle to a persisted claim, not permission to retry/adopt.
struct SemanticAttempt {
  std::string id;
  std::string message_id;
  std::string conv_id;
  std::string text;
  Json source;
  RuntimeProfile graph_profile;
};

class GraphEngine {
 public:
  // Direct clients retain their supplied default analyzer. Runtime binds the
  // current resolved profile explicitly, so overlay reset/removal also applies.
  GraphEngine(Database& db, EventBus& bus, const SemanticAnalyzer& regex, SemanticLLM* llm = nullptr,
              RelationRegistry* relations = nullptr, AnalyzerBinding analyzer_binding = AnalyzerBinding::ConstructorDefault);
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
  Result<SemanticAttempt> begin_analysis(std::string_view msg_id, std::string_view expected_text,
      std::string_view expected_conv_id, const RuntimeProfile& graph_profile,
      const Json& provenance = Json::object(), bool reanalyse_completed = false,
      std::optional<std::string_view> expected_status = std::nullopt);
  Result<bool> complete_analysis(const SemanticAttempt& attempt, const Json& analysis,
                                 bool live_summary = false);
  Status fail_analysis(const SemanticAttempt& attempt, const Error& error,
                       const Json& analysis = nullptr);
  Result<Json> profile_inspection(const Json& overrides = Json::object()) const;
  int reindex_conversation(std::string_view conv_id);
  int reindex_all();

 private:
  Database& db_;
  EventBus& bus_;
  const SemanticAnalyzer& regex_;
  AnalyzerBinding analyzer_binding_;
  SemanticLLM* llm_;
  RelationRegistry* relations_;
  std::atomic<bool> active_{false};
  ScopedSubscription sub_;
};

}  // namespace loom
