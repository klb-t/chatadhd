// loom/semantic_llm.h — port of engine/semantic_llm.py.    [OWNER: wave 2 net/chat/worker]
//
// LLM-powered analysis with regex fallback. Behaviour to preserve:
//   enabled() = !disabled_by_errors && config.semantic_analysis (default true)
//               && config.semantic_model non-empty && secrets.api_key non-empty
//   analyse(text):
//     1. regex = analyzer.analyse(text) (always, first)
//     2. if !enabled() or len(text) < 20 (code points): return convert_regex
//     3. call_llm(text): POST {base_url}/chat/completions
//          headers Authorization: Bearer <api_key>, Content-Type json,
//                  HTTP-Referer https://github.com/chatadhd, X-Title ChatADHD-Semantic
//          body {"model": semantic_model, "messages":[{"role":"user",
//                "content": kAnalysisPrompt + text[:3000]}], "temperature":0.1,
//                "max_tokens":800}, timeout 30 s
//        non-200 -> nullopt (logged); content -> strip ``` fences -> JSON
//     4. success -> reset failures, return merge(llm, regex)
//        failure/exception -> ++consecutive_failures; >= 5 -> disabled
//        (log warning); return convert_regex(regex)
//   Changing semantic enabled/model/base URL/API key resets the failure latch
//   on the next enabled()/analyse() check. Completions from older settings do
//   not affect the current failure state. No automatic replay is scheduled.
//   merge(): setdefault entities/topics/relations to [], source="llm", then
//     append regex entities whose text.lower() is not among the LLM entity
//     names (lowercased) as {"name","kind","relevance": confidence*0.8}.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Config;
class Secrets;
class SemanticAnalyzer;
struct Analysis;
namespace net {
class HttpTransport;
}

// Exact text of Python's _ANALYSIS_PROMPT (shared with batch_api/semantic_worker).
extern const std::string_view kAnalysisPrompt;

class SemanticLLM {
 public:
  static constexpr int kMaxConsecutiveFailures = 5;
  static constexpr std::size_t kMaxAnalysisChars = 3000;

  SemanticLLM(const Config& cfg, const Secrets& secrets, net::HttpTransport& http, const SemanticAnalyzer& regex);

  bool enabled() const;
  // Never fails: falls back to the regex analysis (unified dict, see
  // SemanticAnalyzer::to_unified). Thread-safe.
  Json analyse(std::string_view text);

  // One LLM call. nullopt when config is incomplete, status != 200, or the
  // content is not JSON. Transport errors are returned as errors.
  Result<std::optional<Json>> call_llm(std::string_view text);

  // Strip ``` fences (first line when the text starts with ```, trailing ```)
  // and parse. Shared by the batch paths.
  static std::optional<Json> parse_response_json(std::string_view raw);
  static Json convert_regex(const Analysis& regex);
  static Json merge(Json llm, const Analysis& regex);

  int consecutive_failures() const noexcept { return failures_.load(); }
  bool disabled_by_errors() const noexcept { return disabled_.load(); }
  void reset_failures() noexcept;  // Explicit retry, also invalidates old completions.

 private:
  struct Settings {
    bool active = false;
    std::string model;
    std::string base;
    std::string key;  // Request-local only; recovery identity stores its digest.
  };
  Settings settings_locked() const;
  void refresh_identity_locked(const Settings& settings) const;
  void record_result(std::uint64_t generation, bool success);
  Result<std::optional<Json>> call_llm(std::string_view text, const Settings& settings);

  const Config& cfg_;
  const Secrets& secrets_;
  net::HttpTransport& http_;
  const SemanticAnalyzer& regex_;
  mutable std::mutex state_mu_;
  mutable std::optional<Json> config_identity_;
  mutable std::uint64_t config_generation_ = 0;
  mutable std::atomic<int> failures_{0};
  mutable std::atomic<bool> disabled_{false};
};

}  // namespace loom
