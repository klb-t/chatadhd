// OWNER: wave 2 semantic/graph. Lifecycle is real; graph writes are stubs.
#include "loom/graph_engine.h"

#include "loom/db.h"
#include "stub.h"

namespace loom {

GraphEngine::GraphEngine(Database& db, EventBus& bus, const SemanticAnalyzer& regex, SemanticLLM* llm,
                         RelationRegistry* relations)
    : db_(db), bus_(bus), regex_(regex), llm_(llm), relations_(relations) {}

GraphEngine::~GraphEngine() { stop(); }

void GraphEngine::start() {
  if (active_.exchange(true)) return;
  sub_ = ScopedSubscription(bus_, bus_.on(events::kMsgCreated,
                                          [this](std::string_view, const Json& data) { on_message(data); }));
}

void GraphEngine::stop() {
  active_.store(false);
  sub_.reset();
}

void GraphEngine::on_message(const Json& data) {
  (void)data;  // STUB: wave2
}

bool GraphEngine::ingest_analysis(std::string_view, std::string_view, const Json&) {
  return false;  // STUB: wave2
}

int GraphEngine::reindex_conversation(std::string_view) {
  return 0;  // STUB: wave2
}

int GraphEngine::reindex_all() {
  return 0;  // STUB: wave2
}

}  // namespace loom
