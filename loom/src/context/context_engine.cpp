// context_engine.h — STUB: knowledge-wave. The context+materialize area
// replaces this file (and may add more under src/context/).
#include "loom/context_engine.h"

#include "loom/util/utf8.h"
#include "stub.h"

namespace loom::context {

Result<ContextRequest> ContextRequest::from_json(const Json&) { return LOOM_KNOWLEDGE_STUB("ContextRequest::from_json"); }  // STUB: knowledge-wave
Json ContextRequest::to_json() const { return Json{{"stub", "ContextRequest"}}; }  // STUB: knowledge-wave

ContextEngine::ContextEngine(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack)
    : rt_(rt), store_(store), pack_(std::move(pack)) {}
Result<model::Goal> ContextEngine::type_goal(const ContextRequest&) {
  (void)rt_;
  (void)store_;
  (void)pack_;
  return LOOM_KNOWLEDGE_STUB("ContextEngine::type_goal");  // STUB: knowledge-wave
}
Result<model::ContextSet> ContextEngine::select(const ContextRequest&) { return LOOM_KNOWLEDGE_STUB("ContextEngine::select"); }  // STUB: knowledge-wave
Result<std::string> ContextEngine::render(const model::ContextSet&) { return LOOM_KNOWLEDGE_STUB("ContextEngine::render"); }  // STUB: knowledge-wave

int ContextEngine::estimate_tokens(std::string_view text) noexcept {
  std::size_t n = utf8::length(text);
  return n == 0 ? 0 : static_cast<int>(std::max<std::size_t>(1, n / 4));
}

}  // namespace loom::context
