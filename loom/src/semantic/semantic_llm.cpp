// OWNER: wave 2 net/chat/worker. kAnalysisPrompt is real (contract data);
// the rest is a stub.
#include "loom/semantic_llm.h"

#include "loom/config.h"
#include "loom/semantic_analyzer.h"
#include "stub.h"

namespace loom {

// Exact copy of engine/semantic_llm.py _ANALYSIS_PROMPT (== batch_api's copy).
constexpr std::string_view kAnalysisPromptText = R"PROMPT(Analyse the following chat message. Return ONLY valid JSON (no markdown, no backticks).

{
  "entities": [{"name": "...", "kind": "person|org|place|code|file|concept|product|event", "relevance": 0.0-1.0}],
  "topics": [{"label": "...", "confidence": 0.0-1.0}],
  "relations": [{"subject": "...", "predicate": "mentions|depends_on|references|contradicts|implements|part_of", "object": "..."}],
  "summary": "one line summary",
  "sentiment": "positive|negative|neutral|mixed"
}

Rules:
- Extract ALL named entities (people, orgs, code modules, files, concepts).
- Topics should be domain labels (legal, finance, tech, ai, security, health, project, personal).
- Relations connect entities mentioned in the text.
- Be precise. Fewer high-confidence items > many low-confidence ones.
- If the message is trivial (greetings, "ok", etc.) return empty arrays.

Message:
)PROMPT";
const std::string_view kAnalysisPrompt = kAnalysisPromptText;

SemanticLLM::SemanticLLM(const Config& cfg, const Secrets& secrets, net::HttpTransport& http,
                         const SemanticAnalyzer& regex)
    : cfg_(cfg), secrets_(secrets), http_(http), regex_(regex) {}

bool SemanticLLM::enabled() const {
  return false;  // STUB: wave2
}

Json SemanticLLM::analyse(std::string_view text) {
  return convert_regex(regex_.analyse(text));  // STUB: wave2 (LLM path missing)
}

Result<std::optional<Json>> SemanticLLM::call_llm(std::string_view) {
  return LOOM_NOT_IMPLEMENTED("SemanticLLM::call_llm");  // STUB: wave2
}

std::optional<Json> SemanticLLM::parse_response_json(std::string_view) {
  return std::nullopt;  // STUB: wave2
}

Json SemanticLLM::convert_regex(const Analysis& regex) { return SemanticAnalyzer::to_unified(regex); }

Json SemanticLLM::merge(Json llm, const Analysis&) {
  return llm;  // STUB: wave2
}

void SemanticLLM::reset_failures() noexcept {
  failures_.store(0);
  disabled_.store(false);
}

}  // namespace loom
