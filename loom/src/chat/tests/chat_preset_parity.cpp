// Offline fixture: compile with LOOM_CHAT_PRESET_PARITY_MAIN to emit the
// actual linked ChatEngine outputs. The core's recursive source glob sees
// an empty unit; this file contributes no executable production behavior.
#if defined(LOOM_CHAT_PRESET_PARITY_MAIN)
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "loom/chat_engine.h"

int main() {
  using loom::Json;
  // Inputs intentionally preserve historical substring matches, including
  // versions/markers found in unrelated provider or model-name fragments.
  const std::vector<std::string> models = {
      "", "gpt-4.1", "claude-4.6", "vendor/4.5", "glm-4.6",
      "opus", "OPUS", "vendor/OpUs-tail", "sonnet", "SONNET", "vendor/SoNnEt-tail",
      "o1", "O1", "custom/no1-match", "o3", "O3", "custom/no3-match",
      "r1", "R1", "vendor/architecture-r1-tail", "thinking", "THINKING", "unthinking-name",
      "deepseek", "DEEPSEEK", "vendor/DeepSeek-tail",
      "claude-sonnet-3.7", "CLAUDE-SONNET-3.7", "claude-opus-4", "CLAUDE-OPUS-4",
      "claude-sonnet-4.6", "CLAUDE-SONNET-4.6", "claude-sonnet-4-6", "CLAUDE-SONNET-4-6",
      "claude-opus-4.5", "CLAUDE-OPUS-4.5", "claude-opus-4-5", "CLAUDE-OPUS-4-5",
      "vendor/o1-4.6", "vendor/O1-4-6", "vendor/r1-4.5", "vendor/R1-4-5",
      "unrelated-thinking/4.60", "vendor/deepseek-4.50", "vendor/sonnet-14.6",
      "vendor/opus-4.5.1", "vendor/4.6-before-thinking", "vendor/thinking-before-4-5",
      "vendor/opus-4.4", "vendor/sonnet-4-7", "vendor/sonnet-4_6", "vendor/sonnet-4/6"};
  const std::vector<std::optional<std::string>> efforts = {
      std::nullopt, "low", "medium", "high", "max", "adaptive", "unknown", "",
      "LOW", "HIGH", "MAX", "ADAPTIVE"};
  const std::vector<Json> payloads = {
      Json{{"keep", true}},
      Json{{"keep", true}, {"reasoning", Json{{"caller_owned", "preserve-unless-replaced"},
          {"enabled", false}, {"max_tokens", 987}}}, {"verbosity", "caller-owned-verbosity"}}};
  Json rows = Json::array();
  for (const auto& model : models) {
    for (const auto& effort : efforts) {
      for (std::size_t variant = 0; variant < payloads.size(); ++variant) {
        Json payload = payloads[variant];
        loom::ChatEngine::configure_reasoning(payload, model, effort);
        rows.push_back(Json{{"model", model}, {"effort", effort ? Json(*effort) : Json(nullptr)},
            {"payload_variant", variant}, {"payload_before", payloads[variant]}, {"payload_after", payload}});
      }
    }
  }
  std::cout << loom::json::canonical(Json{{"schema", "loom.chat_reasoning_preset_parity/1"},
      {"model_count", models.size()}, {"effort_count", efforts.size()}, {"payload_variant_count", payloads.size()},
      {"row_count", rows.size()}, {"rows", rows}}) << '\n';
  return std::cout ? 0 : 1;
}
#endif
