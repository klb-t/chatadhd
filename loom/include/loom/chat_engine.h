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
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "loom/db.h"
#include "loom/context_engine.h"
#include "loom/result.h"
#include "loom/turn_carrier.h"
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
  std::optional<std::string> model;            // unset: default_model through the scope chain (conversation -> units -> user setting)
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
  // R67: the person's answer to the secret check (setting secrets_in_chat = ask): {approved: true} sends a message the check held. Without it
  // a held message is neither stored nor sent (chat/secret_filter.h).
  std::optional<Json> secret_confirmation;
  // Independent composition controls. Existing callers keep the legacy recipe.
  bool include_memory = true;
  bool include_graph_memory = true;
  bool include_history = true;
  // A send that names no conversation continues the one remembered from the last send; new_conversation starts a new one
  // instead (the "New chat" of a client). Wire field "new_conversation"; ignored when conv_id is set.
  bool new_conversation = false;
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
  // Message id -> the level its text was substituted at (L1, L2) and, for the ids in history_skip, why they are left out (thread_filter; an id that is
  // not here was left out by history_skip itself). Only the receipt reads them. Not wire fields.
  std::map<std::string, std::string> history_level;
  std::map<std::string, std::string> history_skip_reason;
  // Standing rules about the person (the guardian's, normalized text only, allowed by the scope chain for this conversation); sent as one system message after the
  // system prompt. Not a wire field; empty = nothing is added.
  std::string guardian_rules;
  // The flow of this request (profile guardian, context_io.flows): how the selection is rendered for the model and in which form the answer is
  // asked for and taken back. Wire field "flow". Unset: the scope chain (setting chat_flow), else the flow of the model's class (preset plain =
  // chat turns in, text out, request bytes unchanged).
  std::optional<std::string> flow;
  // The selector profile of this request (chat_context.pack selector_profiles: loom, loom_legacy, compat, lean, graph_in, ... or one of the
  // person's own). Wire field "selector_profile". Unset: the turn value context.profile, then the scope chain (rules on context.profile and the
  // setting selector_profile), else none: the request is compiled from the settings as they are (M1-K1a).
  std::optional<std::string> selector_profile;
  // Values that hold for THIS request only, keyed like the scoped values (context.profile, context.breadth_delta, context.depth_delta); a step id
  // of the value ("wider", "deeper") or a list of them. Not a wire field and never persisted: the next request starts empty. This kernel only READS
  // them (M1-K1a); what puts values here is the turn carrier of M1-K1b.
  Json turn_values = Json::object();
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
  // R67: the secret check held or refused the message (loom.chat_secret_pending/1). Nothing was stored or sent; the document names the rules and
  // places that matched, never the values.
  std::optional<Json> secret_pending;
  // P18: what the usage ledger only told while admitting the send (a spending limit with on_exceed notify): [{code, period, text: {lang: text}}].
  // Also kept in the assistant message's metadata (usage_notices). Null when there was nothing to tell.
  Json usage_notices = nullptr;
  // What happened to the attached files: [{file, status: cut|unreadable, original_chars, kept_chars}] (empty = every file went whole).
  // Also in the context receipt (attachments) and in context_trace.
  Json attachment_notes = Json::array();
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

  // An attachment is a file path, or `blob:<64 hex>/<file name>`: a file a client uploaded to the blob store (the web composer). The resolver
  // gives the stored file of a hash; the name after the slash decides how the file is read (image, text). Set once, before the first send.
  using AttachmentResolver = std::function<std::optional<std::filesystem::path>(const std::string& hash)>;
  void set_attachment_resolver(AttachmentResolver resolver);
  // What would keep an attachment from the model, checked before anything is stored or sent: a blob the store does not have or a path
  // that does not exist (NotFound), a kind of file that is not read (Unsupported: images and plain-text files are). The message ends
  // with the attachment exactly as given, so a client can say which one. send() and preview_usage() refuse with it.
  Status check_attachments(const std::vector<std::string>& attachments) const;

  // Exposed for tests and for wave-3 tooling (prompt inspection).
  Result<Json> build_messages(std::string_view conv_id, std::string_view current_text,
                              const std::vector<std::string>& attachments, const ChatOptions& opts = {},
                              std::string_view exclude_message_id = "", Json* context_trace = nullptr);
  Json build_content(std::string_view text, const std::vector<std::string>& attachments) const;
  // The real builder: an existing file that cannot be read is an error (preset chat_attachment_on_unreadable = refuse) or a visible warning;
  // a text file over chat_attachment_max_chars is cut with a marker. `notes` (optional) receives [{file, status: cut|unreadable, original_chars, kept_chars}].
  Result<Json> build_content_checked(std::string_view text, const std::vector<std::string>& attachments, Json* notes) const;
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

  // RI-2: the request the kernel puts on the wire for the provider, as the person may see it (loom.provider_request/1: provider, endpoint,
  // url, headers without credentials, the body parsed from the exact bytes, parts in words, tools, parameters, sizes, the list of masks).
  // Secrets are masked by the kernel (the chat_secrets recipe, the values of the secrets store, the credential of the route): a mark stands
  // where a value was, never the value. The preview builds the next request as send() would (the turn carrier is only looked at), with no
  // key needed, and stores or sends nothing. The record of the last sent one lives in memory per conversation (setting
  // request_inspector_record, preset true); `kind` "none" when there is none.
  Result<Json> provider_request_preview(std::string_view text, const ChatOptions& opts = {});
  Json last_provider_request(std::string_view conv_id) const;

  // The values that hold for the next request of a conversation only (M1-K1b; turn_carrier.h). The action context.modify fills it, the next
  // send() of that conversation takes everything it holds, preview_usage() only looks.
  TurnCarrier& turn_carrier() { return turn_carrier_; }
  const TurnCarrier& turn_carrier() const { return turn_carrier_; }

  // What the next request of a conversation would cost against what this kind of request cost so far (M1-K1b): the request is compiled as send()
  // would compile it (turn values of the carrier plus `extra_turn`, which is what is about to be added), its size is put to the shared usage guard
  // read only. {resource, bytes, baseline, ratio, growth_factor, asked, status, baseline_key, baseline_samples, decision} - `asked` is true when the
  // guard would ask the person (a jump of ten times or more over the baseline, or a spending limit); unknown numbers are null with a reason.
  // R67: the check of an outgoing message (text and attached text files) for secrets, before anything is stored, routed or sent. nullopt: it may go on.
  Result<std::optional<Json>> secret_gate(std::string_view text, const ChatOptions& opts = {}) const;
  Result<Json> context_guard(std::string_view conv_id, std::string_view text, const ChatOptions& opts = {}, const Json& extra_turn = Json::object());

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
  AttachmentResolver attachment_resolver_;
  TurnCarrier turn_carrier_;
  // The stored file of an attachment and the name that decides its kind; nullopt when there is none.
  std::optional<std::pair<std::filesystem::path, std::filesystem::path>> locate_attachment(const std::string& ref) const;
  // The context selector's choice for a conversation among the representations the guardian keeps beside the raw messages
  // (setting chat_representation, preset raw = `in` unchanged, byte for byte). Never fails a send: what cannot be used stays raw.
  ChatOptions select_representation(const ChatOptions& in, const std::string& conv_id);
  // Temporary projection-repair exemptions while send() is running. Protected
  // by db_.lock(); durable acceptance authority lives in EventLog.
  std::map<std::string, Json> active_task_in_flight_;
  mutable std::mutex mu_;
  std::optional<Conversation> conv_;
  std::optional<std::string> last_reasoning_;
  std::map<std::string, Json> last_provider_request_;  // conv id -> loom.provider_request/1 (masked), guarded by mu_
};

}  // namespace loom
