// loom/chat_engine.h — port of engine/chat_engine.py.      [OWNER: wave 2 net/chat/worker]
//
// send(text, ...) — behaviour to preserve:
//   1. conversation: opts.conv_id, else the current one, else new_conv()
//   2. persist user message (attachments) -> emit message:created
//      {"id","text","conv_id","role":"user"}
//   3. build_messages(): system_prompt (config or override) as system msg;
//      memory: "User's memory/context:\n" + memory.get_active_context() when
//      non-empty; graph: graph_memory.select_context(text, current_conv_id)
//      when non-empty (errors logged, ignored); history: active messages of
//      the conversation EXCEPT the user message saved in step 2 (deviation:
//      Python sent it twice - once from history, once as the current turn),
//      content prefixed "[IMPORTANT] " when weight > 1.5, "[low priority] "
//      when weight < 0.5; finally the current user message with attachments
//      via build_content()
//   4. build_content(): no attachments -> plain string; else parts
//      [{"type":"text","text":text}] + images (.png .jpg .jpeg .gif .webp ->
//      {"type":"image_url","image_url":{"url":"data:image/<ext>;base64,..."}},
//      jpg -> image/jpeg) + text files (_TEXT_EXTS, first 15000 chars, decoded
//      with errors="replace") as {"type":"text","text":"\n--- FILE: <name>
//      ---\n<text>\n--- END FILE ---"}; missing files skipped with a warning
//   5. POST {base_url}/chat/completions, headers Authorization Bearer
//      api_key, Content-Type json, HTTP-Referer https://github.com/chatadhd,
//      X-Title ChatADHD; payload {model, messages, temperature, max_tokens,
//      stream} + plugins [{"id":"web"}] for web_search (deep_research adds
//      "max_results":10 and web_search_options {"search_context_size":
//      "high"}) + configure_reasoning() + "include_reasoning": true; timeout
//      180 s; no api_key -> Errc::Auth "No API key configured - open
//      Settings"; status != 200 -> Errc::Http "API error <code>: <body[:500]>"
//   6. stream: SSE "data:" JSON, choices[0].delta.content -> on_chunk,
//      delta.reasoning -> on_reasoning, "[DONE]" ends; malformed chunks
//      skipped; usage (when present in the final chunk) recorded.
//      non-stream: choices[0].message.content / .reasoning
//   7. assistant metadata: {"reasoning": reasoning[:2000]} when any, plus
//      {"topics": analyzer.extract_topics(response, 2)} when non-empty;
//      persist with model -> emit message:created (role assistant)
//   8. auto-title: when the conversation has <= 2 active messages and
//      config.auto_title: title = text[:30] + "..." if len(text) > 30
// configure_reasoning(payload, model, effort): only for models whose
//   lowercase id contains opus|sonnet|o1|o3|r1|thinking|deepseek; reasoning
//   = {"enabled": true}; "new Claude" ids (contain 4.6|4-6|4.5|4-5): effort
//   "max" -> payload.verbosity="max"; other non-"adaptive" efforts ->
//   reasoning.max_tokens = {low:5000, medium:15000, high:30000}[effort]
//   (default 15000); other models: reasoning.effort = effort when given.
// Loom additions: explicit conv_id (thread-safe use from the C API), model/
// temperature/max_tokens/system prompt overrides, cancellation (partial
// assistant text is persisted with metadata {"cancelled": true}), usage.
#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/db.h"
#include "loom/context_engine.h"
#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {

class Config;
class Secrets;
class EventBus;
class SemanticAnalyzer;
class MemoryEngine;
class GraphMemorySelector;
namespace net {
class HttpTransport;
}

struct ChatOptions {
  std::optional<std::string> conv_id;
  std::optional<std::string> model;            // default: config.default_model
  std::vector<std::string> attachments;        // file paths
  bool web_search = false;
  bool deep_research = false;
  std::optional<std::string> reasoning_effort; // low | medium | high | max | adaptive
  std::optional<double> temperature;
  std::optional<int> max_tokens;
  std::optional<std::string> system_prompt;    // replaces config.system_prompt
  std::optional<int> context_depth;            // graph memory depth (0 = disable graph context)
  std::optional<bool> stream;                  // unset: config.stream; requires on_chunk
  // Independent composition controls. Existing callers keep the legacy recipe.
  bool include_memory = true;
  bool include_graph_memory = true;
  bool include_history = true;
  std::optional<context::ContextRequest> knowledge_context;  // explicitly opt in; offline selection
  std::optional<bool> trace_context;           // unset: automatic for knowledge / changed recipe; false opts out

  // {"conv_id","model","attachments":[...],"web_search","deep_research",
  //  "reasoning_effort","temperature","max_tokens","system_prompt",
  //  "context_depth","stream","include_memory","include_graph_memory",
  //  "include_history","knowledge_context","trace_context"} — loom_chat_ex.
  static Result<ChatOptions> from_json(const Json& j);
};

struct ChatCallbacks {
  // Called once, right after the user message is persisted (before the API
  // call): lets clients show the message and learn the conversation id.
  std::function<void(std::string_view conv_id, std::string_view user_message_id)> on_start;
  std::function<void(std::string_view)> on_chunk;
  std::function<void(std::string_view)> on_reasoning;
};

struct ChatResult {
  std::string conv_id;
  std::string user_message_id;
  std::string assistant_message_id;
  std::string text;
  std::optional<std::string> reasoning;
  std::string model;
  Json usage = Json::object();
  std::optional<std::string> new_title;  // set when auto-title ran
  bool cancelled = false;
  Json context_trace;  // compiled messages/selection, not evidence of provider receipt
  Json to_json() const;
};

class ChatEngine {
 public:
  ChatEngine(const Config& cfg, const Secrets& secrets, Database& db, EventBus& bus, net::HttpTransport& http,
             const SemanticAnalyzer& analyzer, MemoryEngine* memory = nullptr,
             GraphMemorySelector* graph_memory = nullptr);

  // Python: the last conversation (list_convs(limit=1)) becomes current.
  Status resume_last();
  Result<Conversation> new_conv(std::string_view title = "New Chat");
  Result<std::optional<Conversation>> load_conv(std::string_view conv_id);
  std::optional<Conversation> current_conv() const;

  Result<ChatResult> send(std::string_view text, const ChatOptions& opts = {}, const ChatCallbacks& cb = {},
                          const CancelToken* cancel = nullptr);

  using KnowledgeContextBuilder = std::function<Result<Json>(const context::ContextRequest&)>;
  // Runtime installs its offline ContextEngine adapter. Standalone callers may
  // install the same capability; an absent adapter is an explicit error on opt-in.
  void set_knowledge_context_builder(KnowledgeContextBuilder builder);

  // Exposed for tests and for wave-3 tooling (prompt inspection).
  Result<Json> build_messages(std::string_view conv_id, std::string_view current_text,
                              const std::vector<std::string>& attachments, const ChatOptions& opts = {},
                              std::string_view exclude_message_id = "", Json* context_trace = nullptr);
  Json build_content(std::string_view text, const std::vector<std::string>& attachments) const;
  static void configure_reasoning(Json& payload, std::string_view model, const std::optional<std::string>& effort);

  std::optional<std::string> last_reasoning() const;

 private:
  const Config& cfg_;
  const Secrets& secrets_;
  Database& db_;
  EventBus& bus_;
  net::HttpTransport& http_;
  const SemanticAnalyzer& analyzer_;
  MemoryEngine* memory_;
  GraphMemorySelector* graph_memory_;
  KnowledgeContextBuilder knowledge_context_builder_;
  mutable std::mutex mu_;
  std::optional<Conversation> conv_;
  std::optional<std::string> last_reasoning_;
};

}  // namespace loom
