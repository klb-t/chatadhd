// Source-private: the two orthogonal chat formats (P27). Everything here is a function of data in the runtime profile guardian
// (context_format): the preset names, the recipes, the reply schema, the flows by model class, the texts.
//
//   chat_context_format  conversation (preset) | graph | both   how the context the selector chose is given to the model
//   chat_reply_format    text (preset) | graph                   how the model is asked to answer (loom.graph_reply/2)
//
// Both are values of the scope chain (scopes::Resolver): conversation -> units -> user -> installation. The presets leave the request
// bytes unchanged. The model of the conversation never decides a setting; the flow of a graph reply follows the class of the model
// (cheap | mid | frontier | unknown) as data.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "loom/db.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Config;
class Database;
}  // namespace loom

namespace loom::chat::formats {

// What the flow resolved to for one request.
struct Axes {
  std::string flow = "plain";      // name of the flow (data: context_io.flows)
  std::string renderer = "chat_turns";
  std::string output = "text";
  bool turns = true;               // the selection is rendered as chat turns
  bool packet = false;             // the selection is rendered as a graph packet
  bool reply_graph = false;        // the answer is asked for as loom.graph_reply/2
  std::string mode;                // response_format | prompt_only (only for a graph reply)
  std::string model_class;         // cheap | mid | frontier | unknown
  std::string reason;              // why this flow / mode
  Json section = nullptr;          // profile guardian, context_io
  bool plain() const { return turns && !packet && !reply_graph; }
};

// The flow: an override of the request, else the scope chain (setting chat_flow), else the flow of the model's class (data). Errors are
// the profile's; the caller degrades to plain, visibly (log).
Result<Axes> resolve(const std::filesystem::path& root, const Config& cfg, Database* db, const std::string& conv_id,
                     const std::string& model, const std::optional<std::string>& override_flow, bool reply_allowed);

struct Entry {
  Message msg;                  // the row, whole
  std::string text;             // the text of the selection (raw, or a lossy level chosen by the selector)
  bool selected_level = false;
};

// loom.context_graph/1: nodes (messages with provenance) and links (follows, reply_to), packet_id = sha256 of the rest.
// The SELECTION: every node and graph link keeps its source (table, id, position, the whole row) so that the embedder can put it back where it came
// from. `inputs` records what the selector read (representation, flow, settings). packet_id = sha256 of the rest.
Json packet(const Json& section, const std::string& conv_id, const std::vector<Entry>& entries, const std::vector<Link>& graph_links, const Json& inputs);
// A renderer may drop parts of the selection (renderers.<id>.drop, e.g. "source"); the selection itself never does.
Json render_packet(const Json& section, const std::string& renderer, const Json& packet);
// The system block that carries the packet: recipe + the packet as JSON.
std::string packet_block(const Json& section, const Json& packet);
// The instruction of a graph reply (recipe + the id the reply echoes + the schema when the flow has no response format).
std::string reply_instruction(const Json& section, const std::string& mode, const std::string& packet_id);
// Adds what the flow puts in the payload (response_format, provider constraints).
void apply_reply_request(Json& payload, const Json& section, const std::string& mode);

struct Reply {
  bool ok = false;
  std::string error;         // machine reason when !ok
  Json graph = nullptr;      // the validated loom.graph_reply/2
  std::string text;          // the rendered public text (root)
  bool fence_stripped = false;
  std::map<std::string, std::string> rendered;  // local node id -> its rendered text
  Json emergent = Json::array();  // [{claimed_id}]: ids referred to that were not in the selection sent (never rejects the reply)
};
// Parse + mechanical validation + the deterministic renderer (ordered exact concatenation of the children).
Reply parse_reply(const Json& section, const std::string& model_text, const std::string& packet_id, const std::set<std::string>& prior_ids);
// The visible note for an invalid reply (data text).
std::string invalid_note(const Json& section, const std::string& lang, const std::string& reason);

// PLACEMENT (an analysis-like step, not part of the embedder): where each new element of a reply goes. Cascade as data; here explicit refs and
// deterministic keys. Returns {nodes: local->id, links: [{from,to,predicate,placed_by}], unplaced: [...], cascade}.
// An id referred to that was not sent is an emergent node (P29); db is read for identity candidates outside the selection (never written).
Json plan_placement(Database& db, const Json& section, const std::string& message_id, const std::string& flow, const Reply& reply, const std::set<std::string>& prior_ids);
// The visible note for an emergent reference (data text).
std::string emergent_note(const Json& section, const std::string& lang, const std::string& claimed);
// OUTPUT node embedder (inputs."loom.graph_reply/2".embedder = reply_graph): PURE STRUCTURE, no model, no analysis. It applies a placement plan: writes
// the nodes and links of a valid reply into the graph with provenance to the message. Deterministic ids and upserting links make it idempotent.
Result<Json> embed(Database& db, const Json& section, const std::string& message_id, const std::string& conv_id,
                   const std::string& model, const Reply& reply, const Json& plan);

// INPUT node embedder (inputs."loom.context_graph/1".embedder = packet_messages): the inverse of the selector on its image. Every message of the
// packet is matched to the graph (by id, else by an earlier embedding of the same packet node); a match changes NOTHING (a lossy text_form is
// never written over the raw text); what is new is added to the conversation as a message with provenance metadata.embedded = {packet_id, node,
// origin}. So select(embed(select(G))) == select(G) and embedding twice adds nothing. Parsers that need a model are not part of this guarantee.
Result<Json> embed_packet(Database& db, const Json& section, const std::string& conv_id, const Json& packet);

// ---- M1-K1a: the receipt of a request, contract loom.context_receipt/1 --------------------------------------------------------------------
// What the selector put into ONE request, as references: sections in the order the request has them, items {ref, label, source_kind, why,
// tokens_est} (never the text of an item), what was left out and why (dropped: {ref, reason, count, refs}), the modifiers, the guard, the order,
// the flow, the renderer and the sha256 of the messages. It is compiled from the same parts as the request, not read back from a provider.
struct Receipt {
  Json sections = Json::array();
  Json dropped = Json::array();
  // `cls` is the class of the order (system, profile, rules, memory, project, history, graph) or one of task, reply_instruction, question.
  void section(const std::string& cls, Json items);
  // `group` is the section the dropped elements belonged to; reason: thread_filter | representation_level | budget | history_window | excluded |
  // source_off | option_off | command_traffic | replaced_by_task.
  void drop(const std::string& group, const std::string& reason, const std::vector<std::string>& refs, std::size_t count);
};
// tokens_est: the estimator of graph_memory.pack (codepoints_per_token), at least 1 for a text that is not empty.
long long estimate_tokens(std::string_view text);
Json receipt_item(const std::string& ref, const std::string& label, const std::string& source_kind, const std::string& why, std::string_view text);
// selection_packet -> what the receipt says about a graph packet: {packet_id, nodes, links, substituted, skipped}.
Json packet_summary(const Json& selection);
// header: {order, profile, modifiers, guard, flow, renderer, ...}; sha256 is that of the messages as sent.
Json receipt_finish(const Receipt& r, Json header, const Json& messages);
// store: "auto" (whole when it fits max_bytes, else a summary with the reason; max_bytes 0 = no cap), "always" (whole, no cap).
Json receipt_bound(Json receipt, const std::string& store, long long max_bytes);

// M1-K1c2: the record as it is stored, found from any message of the exchange. The record lives with the message the person sent
// (metadata.context_trace.receipt); asked about the answer, the request that made it is the message before it (its parent when that is a
// message of the person, else the last message of the person before it in the conversation). ONE reading for the action context.receipt and for
// the perspective sent_to_model, so both say the same thing about the same message.
struct StoredReceipt {
  std::string msg_id;                // the message that was named
  std::string request_msg_id;        // the message the person sent (the record lives with it); = msg_id when that was it
  std::optional<Message> request;    // that message, when the database has it
  Json receipt = nullptr;            // loom.context_receipt/1; null when this request has none (before M1, context_receipt_store=never, trace_context=false)
};
// Error: NotFound when the named message does not exist.
Result<StoredReceipt> stored_receipt(Database& db, const std::string& msg_id);

}  // namespace loom::chat::formats
