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
//   5. one llm::use_model call (use_model.h; the settings' provider supplies
//      address, credential and adapter - default preset: POST
//      {base_url}/chat/completions, headers Authorization Bearer api_key,
//      Content-Type json, HTTP-Referer https://github.com/chatadhd, X-Title
//      ChatADHD, pinned by golden tests); payload {model, messages, temperature, max_tokens,
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
#include <map>
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
  // Name of a chat_context order ("legacy" | "stable_first" | user-defined);
  // overrides config chat_context_order and the pack's context_order preset.
  std::optional<std::string> context_order;
  std::optional<int> context_depth;            // graph memory depth (0 = disable graph context)
  std::optional<bool> stream;                  // unset: config.stream; requires on_chunk
  // Shared usage guard (every send is admitted before transport). When a send
  // paused in requires_confirmation, re-send the same request with
  // {approved:bool, receipt_id, ref, operation_id} (all copied from the pending
  // result); approved:false cancels. Absent: first/ordinary admission.
  std::optional<Json> usage_confirmation;
  // Independent composition controls. Existing callers keep the legacy recipe.
  bool include_memory = true;
  bool include_graph_memory = true;
  bool include_history = true;
  std::optional<context::ContextRequest> knowledge_context;  // explicitly opt in; offline selection
  std::optional<bool> trace_context;           // unset: automatic for knowledge / changed recipe; false opts out
  // Explicit caller-supplied derived instruction over existing native messages.
  // The internal compiler rebuilds its text; arbitrary summaries are not inferred.
  std::optional<Json> active_task_spec;
  Json active_task_bindings = Json::object(); // event_id -> {message_id, text_sha256}
  std::string active_task_history = "replace_refinement"; // or "append"
  // Messages of the conversation left out of the history this request is compiled from. Set by the conversation
  // window when the sentence of a rejected proposal is sent as an ordinary message (chat_route.h): the exchange of
  // the proposal is not part of what the model reads, so the request is that of an ordinary message. Not a wire
  // field (from_json never sets it); empty = nothing is left out.
  std::vector<std::string> history_skip;
  // How the exchange of chat commands (metadata.routed: command, proposal, confirmation, result) enters the history: skip_exchange |
  // skip_all | include. Not a wire field; empty = setting chat_history_command_traffic (preset skip_exchange).
  std::string history_command_traffic;
  // Message id -> the text sent in its place (a lossy level of the guardian's representation, chosen by the context selector). Not a wire
  // field; empty = every message is sent as it is.
  std::map<std::string, std::string> history_text;
  // Standing rules about the person (the guardian's, normalized text only, allowed by the scope chain for this conversation); sent as one system message after the
  // system prompt. Not a wire field; empty = nothing is added.
  std::string guardian_rules;
  // The flow of this request (profile guardian, context_io.flows): how the selection is rendered for the model and in which form the answer is
  // asked for and taken back. Wire field "flow". Unset: the scope chain (setting chat_flow), else the flow of the model's class (preset plain =
  // chat turns in, text out, request bytes unchanged).
  std::optional<std::string> flow;
  // The proposal message whose offer to send the rejected sentence as a message the user takes (chat request field
  // send_as_message; chat_route.h). The conversation window reads it; the engine ignores it.
  std::string send_as_message;

  // {"conv_id","model","attachments":[...],"web_search","deep_research",
  //  "reasoning_effort","temperature","max_tokens","system_prompt",
  //  "context_depth","stream","include_memory","include_graph_memory",
  //  "include_history","knowledge_context","trace_context","active_task_spec",
  //  "active_task_bindings","active_task_history"} — loom_chat_ex.
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
  // Set when the shared usage guard paused the send (requires_confirmation or
  // an answered rejection). Nothing was sent; no assistant message exists.
  std::optional<Json> usage_pending;
  // P18: what the usage ledger only told while admitting the send (a spending limit with on_exceed notify): [{code, period, text: {lang: text}}].
  // Also kept in the assistant message's metadata (usage_notices). Null when there was nothing to tell.
  Json usage_notices = nullptr;
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

  // Read-only usage preview of a would-be send: compiles the same request body
  // without persisting anything or contacting the provider, and asks the shared
  // UsagePolicy for its decision (nothing reserved or recorded).
  // The selector's output for a conversation as a graph selection (loom.context_graph/1): every message and graph link with its source id, place and
  // whole row, the text as the selector chose it (representation, thread filter). It is read from the graph only. `exclude_message_id` leaves out the
  // message of the turn being sent.
  Result<Json> context_selection(std::string_view conv_id, const ChatOptions& opts = {}, std::string_view exclude_message_id = {});
  // The inverse: the output of the selector (or any packet of that schema) is embedded in the graph where it came from. A selection taken from the
  // graph changes nothing in it; only elements without a source are added, with provenance. Returns {unchanged, created_nodes, created_links, orphans, conflicts}.
  Result<Json> embed_selection(std::string_view conv_id, const Json& selection);

  Result<Json> preview_usage(std::string_view text, const ChatOptions& opts = {});

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
  // The context selector's choice for a conversation among the representations the guardian keeps beside the raw messages
  // (setting chat_representation, preset raw = `in` unchanged, byte for byte). Never fails a send: what cannot be used stays raw.
  ChatOptions select_representation(const ChatOptions& in, const std::string& conv_id);
  // Temporary projection-repair exemptions while send() is running. Protected
  // by db_.lock(); durable acceptance authority lives in EventLog.
  std::map<std::string, Json> active_task_in_flight_;
  mutable std::mutex mu_;
  std::optional<Conversation> conv_;
  std::optional<std::string> last_reasoning_;
};

}  // namespace loom
